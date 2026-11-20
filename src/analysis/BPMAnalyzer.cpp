#include "BPMAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
constexpr double silenceFloor = 1.0e-8;
constexpr double minimumUsefulPeriodicity = 0.16;
constexpr double confidenceThreshold = 0.38;

AnalyzerOutput makeBpmOutput(BpmAnalysis result)
{
    return AnalyzerOutput::success(AnalysisPayload { std::move(result) });
}

BpmAnalysis emptyDetection(const BPMAnalyzer::Options& options, BpmDetectionStatus status)
{
    BpmAnalysis result;
    result.minimumBpm = options.minimumBpm;
    result.maximumBpm = options.maximumBpm;
    result.detectionStatus = status;
    return result;
}

double interpolatedValue(const std::vector<float>& values, double index) noexcept
{
    if (values.empty() || index < 0.0 || index >= static_cast<double>(values.size() - 1))
        return 0.0;
    const auto base = static_cast<size_t>(index);
    const auto fraction = index - static_cast<double>(base);
    return static_cast<double>(values[base]) * (1.0 - fraction)
        + static_cast<double>(values[base + 1]) * fraction;
}

double normalizedAutocorrelation(const std::vector<float>& signal, double lag,
                                 const AnalysisCancellationToken& cancellation)
{
    if (!std::isfinite(lag) || lag <= 0.0 || lag >= static_cast<double>(signal.size() - 1))
        return 0.0;

    double cross = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    const auto firstFrame = static_cast<size_t>(std::ceil(lag));
    for (size_t frame = firstFrame; frame < signal.size(); ++frame)
    {
        if ((frame & 16383u) == 0 && cancellation.isCancellationRequested())
            return 0.0;
        const auto left = static_cast<double>(signal[frame]);
        const auto sourcePosition = static_cast<double>(frame) - lag;
        const auto sourceFrame = static_cast<size_t>(sourcePosition);
        const auto fraction = sourcePosition - static_cast<double>(sourceFrame);
        const auto right = static_cast<double>(signal[sourceFrame]) * (1.0 - fraction)
            + static_cast<double>(signal[sourceFrame + 1]) * fraction;
        cross += left * right;
        leftEnergy += left * left;
        rightEnergy += right * right;
    }

    const auto normalization = std::sqrt(leftEnergy * rightEnergy);
    return normalization > 1.0e-15 ? juce::jlimit(0.0, 1.0, cross / normalization) : 0.0;
}

struct ScoredCandidate
{
    double bpm = 0.0;
    double score = 0.0;
};

bool isOctaveRelated(double first, double second) noexcept
{
    const auto ratio = first / second;
    return (ratio >= 1.90 && ratio <= 2.10) || (ratio >= 0.476 && ratio <= 0.526);
}

bool isIntegerTempoMultiple(double first, double second) noexcept
{
    const auto ratio = juce::jmax(first, second) / juce::jmin(first, second);
    for (const auto multiple : { 2.0, 3.0, 4.0 })
        if (ratio >= multiple * 0.95 && ratio <= multiple * 1.05)
            return true;
    return false;
}

std::vector<ScoredCandidate> collectCandidates(const std::vector<float>& signal,
                                               const std::vector<float>& autocorrelation,
                                               int minimumLag, int maximumLag,
                                               double frameRate, int minimumCycles,
                                               const AnalysisCancellationToken& cancellation)
{
    const auto minimumStep = minimumLag * 2;
    const auto maximumStep = maximumLag * 2;
    std::vector<double> scores(static_cast<size_t>(maximumStep + 1), 0.0);
    for (int step = minimumStep; step <= maximumStep; ++step)
    {
        if (cancellation.isCancellationRequested())
            return {};
        const auto lag = static_cast<double>(step) * 0.5;
        if (signal.size() < static_cast<size_t>(lag * minimumCycles))
            continue;

        const auto base = normalizedAutocorrelation(signal, lag, cancellation);
        const auto faster = interpolatedValue(autocorrelation, lag * 0.5);
        const auto slower = interpolatedValue(autocorrelation, lag * 2.0);
        scores[static_cast<size_t>(step)] = 0.84 * base + 0.08 * faster + 0.08 * slower;
    }

    std::vector<ScoredCandidate> candidates;
    for (int step = minimumStep; step <= maximumStep; ++step)
    {
        const auto index = static_cast<size_t>(step);
        const auto score = scores[index];
        if (score < minimumUsefulPeriodicity || score < scores[index - 1]
            || (step < maximumStep && score < scores[index + 1]))
            continue;

        const auto left = scores[index - 1];
        const auto right = step < maximumStep ? scores[index + 1] : score;
        const auto curvature = left - 2.0 * score + right;
        auto offset = std::abs(curvature) > 1.0e-12
            ? 0.5 * (left - right) / curvature : 0.0;
        offset = juce::jlimit(-0.5, 0.5, offset);
        const auto refinedLag = (static_cast<double>(step) + offset) * 0.5;
        const auto bpm = 60.0 * frameRate / refinedLag;
        if (!std::isfinite(bpm))
            continue;

        candidates.push_back({ bpm, score });
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right)
    {
        if (left.score < right.score)
            return false;
        if (left.score > right.score)
            return true;
        return left.bpm > right.bpm;
    });

    std::vector<ScoredCandidate> unique;
    for (const auto& candidate : candidates)
    {
        const auto isDuplicate = std::any_of(unique.begin(), unique.end(), [&candidate](const auto& existing)
        {
            return std::abs(candidate.bpm - existing.bpm) / existing.bpm < 0.025;
        });
        if (!isDuplicate)
            unique.push_back(candidate);
    }
    return unique;
}

float computeConfidence(double score, double coverage, double separation,
                        bool octaveAmbiguous) noexcept
{
    const auto periodicity = juce::jlimit(0.0, 1.0, (score - 0.12) / 0.72);
    const auto confidence = periodicity * (0.45 + 0.55 * coverage)
        * (0.55 + 0.45 * separation) * (octaveAmbiguous ? 0.76 : 1.0);
    return static_cast<float>(juce::jlimit(0.0, 1.0, confidence));
}
}

BPMAnalyzer::BPMAnalyzer() : BPMAnalyzer(Options {})
{
}

BPMAnalyzer::BPMAnalyzer(Options options) : settings(options)
{
    if (!std::isfinite(settings.minimumBpm) || !std::isfinite(settings.maximumBpm)
        || settings.minimumBpm < 20.0 || settings.maximumBpm > 400.0
        || settings.maximumBpm <= settings.minimumBpm
        || !std::isfinite(settings.minimumDurationSeconds) || settings.minimumDurationSeconds < 0.5
        || !std::isfinite(settings.envelopeRateHz) || settings.envelopeRateHz < 50.0
        || settings.envelopeRateHz > 400.0 || settings.minimumOnsets < 2
        || settings.minimumCycles < 2 || settings.maximumCandidates < 1)
        throw std::invalid_argument("Invalid BPMAnalyzer options.");
}

AnalyzerOutput BPMAnalyzer::analyze(const SampleStorage& sample,
                                    const AnalysisCancellationToken& cancellation) const
{
    if (cancellation.isCancellationRequested())
        return AnalyzerOutput::failure("BPM analysis cancelled.");

    const auto& audio = sample.audio();
    const auto& metadata = sample.metadata();
    const auto sampleRate = metadata.sampleRate;
    if (audio.getNumSamples() <= 0
        || (audio.getNumChannels() != 1 && audio.getNumChannels() != 2)
        || metadata.channelCount != audio.getNumChannels()
        || metadata.sampleCount != audio.getNumSamples()
        || !std::isfinite(sampleRate) || sampleRate <= 0.0)
        return AnalyzerOutput::failure("BPM analysis requires a valid non-empty Sample.");
    if (sample.metadata().durationSeconds() < settings.minimumDurationSeconds)
        return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::insufficientData));

    try
    {
        const auto hopSamples = juce::jmax(1, juce::roundToInt(sampleRate / settings.envelopeRateHz));
        const auto sampleFrames = audio.getNumSamples();
        const auto featureFrameCount = static_cast<size_t>(
            (static_cast<juce::int64>(sampleFrames) + hopSamples - 1) / hopSamples);
        const auto frameRate = sampleRate / hopSamples;
        if (featureFrameCount < 2 || featureFrameCount > std::numeric_limits<int>::max())
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::insufficientData));

        std::vector<float> novelty(featureFrameCount, 0.0f);
        double peakRms = 0.0;
        for (size_t featureFrame = 0; featureFrame < featureFrameCount; ++featureFrame)
        {
            if ((featureFrame & 31u) == 0 && cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("BPM analysis cancelled.");

            const auto start = static_cast<int>(featureFrame * static_cast<size_t>(hopSamples));
            const auto end = static_cast<int>(juce::jmin<juce::int64>(sampleFrames,
                static_cast<juce::int64>(start) + hopSamples));
            const auto frameCount = juce::jmax(1, end - start);
            double power = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                const auto* channelData = audio.getReadPointer(channel);
                for (int frame = start; frame < end; ++frame)
                {
                    const auto value = static_cast<double>(channelData[frame]);
                    if (!std::isfinite(value))
                        return AnalyzerOutput::failure("Sample contains non-finite audio data.");
                    power += value * value;
                }
            }
            power /= static_cast<double>(frameCount) * audio.getNumChannels();
            const auto rms = std::sqrt(power);
            peakRms = juce::jmax(peakRms, rms);
            novelty[featureFrame] = static_cast<float>(std::log1p(rms * 32.0));
        }

        if (peakRms <= silenceFloor)
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::noRhythmicContent));

        float maximumNovelty = 0.0f;
        for (size_t frame = novelty.size(); frame-- > 1;)
        {
            const auto increase = juce::jmax(0.0f, novelty[frame] - novelty[frame - 1]);
            novelty[frame] = increase;
            maximumNovelty = juce::jmax(maximumNovelty, increase);
        }
        novelty[0] = 0.0f;
        if (maximumNovelty <= 1.0e-7f)
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::noRhythmicContent));

        std::vector<float> medianProbe;
        constexpr size_t maximumProbeSize = 4096;
        const auto probeStride = juce::jmax<size_t>(1,
            (novelty.size() - 1) / maximumProbeSize + 1);
        medianProbe.reserve(juce::jmin(maximumProbeSize, novelty.size()));
        for (size_t frame = 1; frame < novelty.size(); frame += probeStride)
            medianProbe.push_back(novelty[frame]);

        float medianNovelty = 0.0f;
        if (!medianProbe.empty())
        {
            const auto middle = medianProbe.begin() + static_cast<std::ptrdiff_t>(medianProbe.size() / 2);
            std::nth_element(medianProbe.begin(), middle, medianProbe.end());
            medianNovelty = *middle;
        }
        const auto threshold = juce::jmax(maximumNovelty * 0.025f, medianNovelty * 2.5f);
        float maximumOnset = 0.0f;
        for (auto& value : novelty)
        {
            value = juce::jmax(0.0f, value - threshold);
            maximumOnset = juce::jmax(maximumOnset, value);
        }
        if (maximumOnset <= 1.0e-8f)
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::noRhythmicContent));
        for (auto& value : novelty)
            value /= maximumOnset;

        auto onsetCount = 0;
        auto lastOnset = -100000;
        constexpr int minimumOnsetSpacingFrames = 5;
        for (size_t frame = 1; frame + 1 < novelty.size(); ++frame)
        {
            if (novelty[frame] < 0.15f || novelty[frame] < novelty[frame - 1]
                || novelty[frame] < novelty[frame + 1])
                continue;
            if (static_cast<int>(frame) - lastOnset < minimumOnsetSpacingFrames)
                continue;
            lastOnset = static_cast<int>(frame);
            ++onsetCount;
        }

        const auto minimumLag = juce::jmax(1, static_cast<int>(std::ceil(
            frameRate * 60.0 / settings.maximumBpm)));
        const auto maximumLag = juce::jmax(minimumLag, static_cast<int>(std::floor(
            frameRate * 60.0 / settings.minimumBpm)));
        if (onsetCount < settings.minimumOnsets
            || featureFrameCount < static_cast<size_t>(minimumLag * settings.minimumCycles))
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::insufficientData));

        const auto maximumCorrelationLag = juce::jmin(
            static_cast<int>(featureFrameCount) - 1, maximumLag * 2 + 2);
        std::vector<float> autocorrelation(static_cast<size_t>(maximumCorrelationLag + 1), 0.0f);
        for (int lag = 1; lag <= maximumCorrelationLag; ++lag)
        {
            if (cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("BPM analysis cancelled.");
            autocorrelation[static_cast<size_t>(lag)] = static_cast<float>(
                normalizedAutocorrelation(novelty, lag, cancellation));
        }

        auto candidates = collectCandidates(novelty, autocorrelation, minimumLag, maximumLag,
                                            frameRate, settings.minimumCycles, cancellation);
        if (cancellation.isCancellationRequested())
            return AnalyzerOutput::failure("BPM analysis cancelled.");
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [this](const auto& candidate)
        {
            return candidate.bpm < settings.minimumBpm || candidate.bpm > settings.maximumBpm;
        }), candidates.end());
        if (candidates.empty() || candidates.front().score < minimumUsefulPeriodicity)
            return makeBpmOutput(emptyDetection(settings, BpmDetectionStatus::noRhythmicContent));

        auto primaryIndex = size_t { 0 };
        for (size_t index = 1; index < candidates.size(); ++index)
            if (isIntegerTempoMultiple(candidates[primaryIndex].bpm, candidates[index].bpm)
                && candidates[index].score >= candidates[primaryIndex].score * 0.72
                && candidates[index].bpm > candidates[primaryIndex].bpm)
                primaryIndex = index;

        const auto primary = candidates[primaryIndex];
        auto bestNonOctaveScore = 0.0;
        auto octaveAmbiguous = false;
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            if (index == primaryIndex)
                continue;
            if (isOctaveRelated(primary.bpm, candidates[index].bpm))
            {
                octaveAmbiguous = octaveAmbiguous
                    || candidates[index].score >= primary.score * 0.70;
            }
            else
            {
                bestNonOctaveScore = juce::jmax(bestNonOctaveScore, candidates[index].score);
            }
        }

        const auto coverage = juce::jlimit(0.0, 1.0,
            static_cast<double>(onsetCount - 2) / 8.0);
        const auto separation = primary.score > 1.0e-9
            ? juce::jlimit(0.0, 1.0, (primary.score - bestNonOctaveScore) / primary.score) : 0.0;
        const auto confidence = computeConfidence(primary.score, coverage, separation, octaveAmbiguous);

        BpmAnalysis result;
        result.value = primary.bpm;
        result.confidence = confidence;
        result.score = static_cast<float>(primary.score);
        result.detectionStatus = confidence < confidenceThreshold
            ? BpmDetectionStatus::lowConfidence : BpmDetectionStatus::detected;
        result.octaveAmbiguous = octaveAmbiguous;
        result.minimumBpm = settings.minimumBpm;
        result.maximumBpm = settings.maximumBpm;
        for (size_t index = 0; index < candidates.size()
             && result.alternatives.size() < static_cast<size_t>(settings.maximumCandidates); ++index)
        {
            if (index == primaryIndex)
                continue;
            const auto& candidate = candidates[index];
            const auto candidateConfidence = computeConfidence(candidate.score, coverage,
                separation, octaveAmbiguous && isOctaveRelated(primary.bpm, candidate.bpm));
            result.alternatives.push_back({ candidate.bpm, candidateConfidence,
                                            static_cast<float>(candidate.score) });
        }

        return makeBpmOutput(std::move(result));
    }
    catch (const std::bad_alloc&)
    {
        return AnalyzerOutput::failure("There is not enough memory to analyze this Sample.");
    }
    catch (...)
    {
        return AnalyzerOutput::failure("BPM analysis failed unexpectedly.");
    }
}
