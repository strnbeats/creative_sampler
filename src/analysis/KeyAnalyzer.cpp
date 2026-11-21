#include "KeyAnalyzer.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
constexpr double minimumAnalysisFrequency = 45.0;
constexpr double maximumAnalysisFrequency = 5000.0;
constexpr double maximumSpectralFlatness = 0.82;
constexpr int profileCount = 24;
constexpr int segmentCount = 4;

using Chroma = std::array<double, 12>;

struct CandidateScore
{
    PitchClass tonic = PitchClass::c;
    ScaleMode mode = ScaleMode::major;
    double correlation = 0.0;
};

constexpr std::array<double, 12> majorProfile
{
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
};

constexpr std::array<double, 12> minorProfile
{
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
};

AnalyzerOutput insufficientResult()
{
    KeyAnalysis result;
    result.detectionStatus = KeyDetectionStatus::insufficientData;
    return AnalyzerOutput::success(AnalysisPayload { std::move(result) });
}

double correlation(const Chroma& chroma, const std::array<double, 12>& profile,
                   int tonic) noexcept
{
    double chromaMean = 0.0;
    double profileMean = 0.0;
    for (int pitch = 0; pitch < 12; ++pitch)
    {
        chromaMean += chroma[static_cast<size_t>(pitch)];
        profileMean += profile[static_cast<size_t>(pitch)];
    }
    chromaMean /= 12.0;
    profileMean /= 12.0;

    double covariance = 0.0;
    double chromaVariance = 0.0;
    double profileVariance = 0.0;
    for (int pitch = 0; pitch < 12; ++pitch)
    {
        const auto observed = chroma[static_cast<size_t>(pitch)] - chromaMean;
        const auto expected = profile[static_cast<size_t>((pitch - tonic + 12) % 12)] - profileMean;
        covariance += observed * expected;
        chromaVariance += observed * observed;
        profileVariance += expected * expected;
    }
    const auto denominator = std::sqrt(chromaVariance * profileVariance);
    return denominator > 1.0e-15 ? covariance / denominator : 0.0;
}

std::vector<CandidateScore> rankCandidates(const Chroma& chroma)
{
    std::vector<CandidateScore> candidates;
    candidates.reserve(profileCount);
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        candidates.push_back({ static_cast<PitchClass>(tonic), ScaleMode::major,
                               correlation(chroma, majorProfile, tonic) });
        candidates.push_back({ static_cast<PitchClass>(tonic), ScaleMode::minor,
                               correlation(chroma, minorProfile, tonic) });
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right)
    {
        if (left.correlation < right.correlation)
            return false;
        if (left.correlation > right.correlation)
            return left.correlation > right.correlation;
        if (left.tonic != right.tonic)
            return left.tonic < right.tonic;
        return left.mode < right.mode;
    });
    return candidates;
}

float normalizedScore(double correlationValue) noexcept
{
    return static_cast<float>(juce::jlimit(0.0, 1.0, 0.5 + 0.5 * correlationValue));
}

bool isRelativeMajorMinor(const CandidateScore& first, const CandidateScore& second) noexcept
{
    if (first.mode == second.mode)
        return false;
    const auto firstTonic = static_cast<int>(first.tonic);
    const auto secondTonic = static_cast<int>(second.tonic);
    return first.mode == ScaleMode::major
        ? secondTonic == (firstTonic + 9) % 12
        : firstTonic == (secondTonic + 9) % 12;
}

void addFrequencyToChroma(Chroma& chroma, double frequency, double magnitude,
                          double sampleRate, int fftSize) noexcept
{
    if (frequency < minimumAnalysisFrequency || frequency > maximumAnalysisFrequency
        || magnitude <= 0.0)
        return;

    const auto midiNote = 69.0 + 12.0 * std::log2(frequency / 440.0);
    const auto lowerNote = static_cast<int>(std::floor(midiNote));
    const auto fraction = midiNote - static_cast<double>(lowerNote);
    const auto lowerPitch = (lowerNote % 12 + 12) % 12;
    const auto upperPitch = (lowerPitch + 1) % 12;
    const auto relativeFrequency = frequency / 440.0;
    const auto harmonicWeight = 1.0 / std::sqrt(juce::jmax(1.0, relativeFrequency));
    const auto binScale = static_cast<double>(fftSize) / sampleRate;
    const auto resolutionCompensation = juce::jlimit(0.55, 1.0, frequency * binScale / 2.0);
    const auto weighted = magnitude * harmonicWeight * resolutionCompensation;
    chroma[static_cast<size_t>(lowerPitch)] += weighted * (1.0 - fraction);
    chroma[static_cast<size_t>(upperPitch)] += weighted * fraction;
}

double normalizedChromaCorrelation(const Chroma& chroma, const CandidateScore& candidate) noexcept
{
    if (candidate.mode == ScaleMode::major)
        return correlation(chroma, majorProfile, static_cast<int>(candidate.tonic));
    return correlation(chroma, minorProfile, static_cast<int>(candidate.tonic));
}
}

KeyAnalyzer::KeyAnalyzer() : KeyAnalyzer(Options {})
{
}

KeyAnalyzer::KeyAnalyzer(Options options) : settings(options)
{
    if (!std::isfinite(settings.minimumDurationSeconds) || settings.minimumDurationSeconds < 0.5
        || !std::isfinite(settings.frameDurationSeconds) || settings.frameDurationSeconds < 0.15
        || settings.frameDurationSeconds > 1.0 || !std::isfinite(settings.hopFraction)
        || settings.hopFraction <= 0.0 || settings.hopFraction > 0.5
        || settings.minimumFrames < 2 || settings.maximumCandidates < 1
        || settings.maximumCandidates >= profileCount)
        throw std::invalid_argument("Invalid KeyAnalyzer options.");
}

AnalyzerOutput KeyAnalyzer::analyze(const SampleStorage& sample,
                                    const AnalysisCancellationToken& cancellation) const
{
    if (cancellation.isCancellationRequested())
        return AnalyzerOutput::failure("Key analysis cancelled.");

    const auto& audio = sample.audio();
    const auto& metadata = sample.metadata();
    const auto sampleRate = metadata.sampleRate;
    const auto duration = metadata.durationSeconds();
    if (audio.getNumSamples() <= 0
        || (audio.getNumChannels() != 1 && audio.getNumChannels() != 2)
        || metadata.channelCount != audio.getNumChannels()
        || metadata.sampleCount != audio.getNumSamples()
        || !std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0
        || !std::isfinite(duration) || duration <= 0.0)
        return AnalyzerOutput::failure("Key analysis requires a valid non-empty mono or stereo Sample.");

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* channelData = audio.getReadPointer(channel);
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            if ((frame & 65535) == 0 && cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("Key analysis cancelled.");
            if (!std::isfinite(channelData[frame]))
                return AnalyzerOutput::failure("Sample contains non-finite audio data.");
        }
    }

    if (duration < settings.minimumDurationSeconds)
        return insufficientResult();

    try
    {
        const auto requestedSize = static_cast<size_t>(std::ceil(sampleRate * settings.frameDurationSeconds));
        const auto fftSize = static_cast<int>(std::bit_ceil(std::max<size_t>(1024, requestedSize)));
        if (fftSize > 131072 || audio.getNumSamples() < fftSize)
            return insufficientResult();

        const auto fftOrder = static_cast<int>(std::countr_zero(static_cast<unsigned int>(fftSize)));
        juce::dsp::FFT fft(fftOrder);
        juce::dsp::WindowingFunction<float> window(static_cast<size_t>(fftSize),
            juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> fftBuffer(static_cast<size_t>(fftSize) * 2, 0.0f);
        const auto hopSize = juce::jmax(1, juce::roundToInt(fftSize * settings.hopFraction));
        const auto firstBin = juce::jmax(1, static_cast<int>(std::ceil(
            minimumAnalysisFrequency * fftSize / sampleRate)));
        const auto lastBin = juce::jmin(fftSize / 2, static_cast<int>(std::floor(
            juce::jmin(maximumAnalysisFrequency, sampleRate * 0.5) * fftSize / sampleRate)));
        if (lastBin - firstBin < 16)
            return insufficientResult();

        Chroma totalChroma {};
        std::array<Chroma, segmentCount> segmentChroma {};
        std::array<double, segmentCount> segmentWeight {};
        auto validFrameCount = 0;
        auto tonalFrameCount = 0;
        auto totalFrameWeight = 0.0;
        auto tonalWeightSum = 0.0;
        const auto frameCount = 1 + (audio.getNumSamples() - fftSize) / hopSize;
        const auto segmentStride = juce::jmax(1, frameCount / segmentCount);

        for (auto frame = 0; frame < frameCount; ++frame)
        {
            if (cancellation.isCancellationRequested())
                return AnalyzerOutput::failure("Key analysis cancelled.");

            Chroma frameChroma {};
            auto frameWeight = 0.0;
            auto frameTonality = 0.0;
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            {
                std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
                const auto* source = audio.getReadPointer(channel);
                const auto start = frame * hopSize;
                double channelEnergy = 0.0;
                std::array<double, 4> quarterEnergy {};
                for (int offset = 0; offset < fftSize; ++offset)
                {
                    const auto value = source[start + offset];
                    if (!std::isfinite(value))
                        return AnalyzerOutput::failure("Sample contains non-finite audio data.");
                    fftBuffer[static_cast<size_t>(offset)] = value;
                    const auto power = static_cast<double>(value) * value;
                    channelEnergy += power;
                    quarterEnergy[static_cast<size_t>(offset * 4 / fftSize)] += power;
                }
                if (channelEnergy <= 1.0e-14)
                    continue;

                const auto maximumQuarter = *std::max_element(quarterEnergy.begin(), quarterEnergy.end());
                const auto minimumQuarter = *std::min_element(quarterEnergy.begin(), quarterEnergy.end());
                const auto sustainRatio = maximumQuarter > 1.0e-14
                    ? std::sqrt(minimumQuarter / maximumQuarter) : 0.0;
                const auto sustainQuality = juce::jlimit(0.0, 1.0,
                    (sustainRatio - 0.025) / 0.40);
                if (sustainQuality <= 0.0)
                    continue;

                window.multiplyWithWindowingTable(fftBuffer.data(), static_cast<size_t>(fftSize));
                fft.performFrequencyOnlyForwardTransform(fftBuffer.data(), true);

                double magnitudeSum = 0.0;
                double logMagnitudeSum = 0.0;
                for (auto bin = firstBin; bin <= lastBin; ++bin)
                {
                    const auto magnitude = static_cast<double>(fftBuffer[static_cast<size_t>(bin)]);
                    magnitudeSum += magnitude;
                    logMagnitudeSum += std::log(juce::jmax(magnitude, 1.0e-12));
                }
                const auto binCount = lastBin - firstBin + 1;
                const auto meanMagnitude = magnitudeSum / binCount;
                const auto flatness = meanMagnitude > 1.0e-15
                    ? std::exp(logMagnitudeSum / binCount) / meanMagnitude : 1.0;
                const auto tonality = juce::jlimit(0.0, 1.0,
                    (maximumSpectralFlatness - flatness) / maximumSpectralFlatness);
                if (tonality <= 0.025 || magnitudeSum <= 1.0e-12)
                    continue;

                const auto channelWeight = tonality * sustainQuality * std::sqrt(meanMagnitude);
                Chroma channelChroma {};
                for (auto bin = firstBin + 2; bin < lastBin - 2; ++bin)
                {
                    const auto magnitude = static_cast<double>(fftBuffer[static_cast<size_t>(bin)]);
                    const auto sideMagnitude = (static_cast<double>(fftBuffer[static_cast<size_t>(bin - 2)])
                        + static_cast<double>(fftBuffer[static_cast<size_t>(bin - 1)])
                        + static_cast<double>(fftBuffer[static_cast<size_t>(bin + 1)])
                        + static_cast<double>(fftBuffer[static_cast<size_t>(bin + 2)])) * 0.25;
                    const auto peakExcess = juce::jmax(0.0, magnitude - sideMagnitude * 1.05);
                    if (peakExcess <= 1.0e-10)
                        continue;
                    const auto frequency = static_cast<double>(bin) * sampleRate / fftSize;
                    addFrequencyToChroma(channelChroma, frequency, peakExcess,
                                         sampleRate, fftSize);
                }

                auto chromaSum = 0.0;
                for (const auto value : channelChroma)
                    chromaSum += value;
                if (chromaSum <= 1.0e-12)
                    continue;
                for (size_t pitch = 0; pitch < channelChroma.size(); ++pitch)
                    frameChroma[pitch] += channelWeight * channelChroma[pitch] / chromaSum;
                frameWeight += channelWeight;
                frameTonality += tonality;
            }

            if (frameWeight <= 1.0e-12)
                continue;
            ++validFrameCount;
            if (frameTonality > 0.0)
                ++tonalFrameCount;
            totalFrameWeight += frameWeight;
            tonalWeightSum += frameTonality / audio.getNumChannels();
            const auto segment = juce::jmin(segmentCount - 1, frame / segmentStride);
            segmentWeight[static_cast<size_t>(segment)] += frameWeight;
            for (size_t pitch = 0; pitch < frameChroma.size(); ++pitch)
            {
                totalChroma[pitch] += frameChroma[pitch];
                segmentChroma[static_cast<size_t>(segment)][pitch] += frameChroma[pitch];
            }
        }

        if (validFrameCount < settings.minimumFrames || tonalFrameCount < settings.minimumFrames / 2
            || totalFrameWeight <= 1.0e-12)
            return insufficientResult();

        auto totalChromaSum = 0.0;
        auto maximumChroma = 0.0;
        for (const auto value : totalChroma)
        {
            totalChromaSum += value;
            maximumChroma = juce::jmax(maximumChroma, value);
        }
        if (totalChromaSum <= 1.0e-12 || maximumChroma <= 1.0e-12)
            return insufficientResult();
        for (auto& value : totalChroma)
            value /= totalChromaSum;

        auto activePitchClasses = 0;
        auto chromaEntropy = 0.0;
        for (const auto value : totalChroma)
        {
            if (value >= 0.035)
                ++activePitchClasses;
            if (value > 1.0e-12)
                chromaEntropy -= value * std::log(value);
        }
        chromaEntropy /= std::log(12.0);
        if (activePitchClasses < 3 || chromaEntropy > 0.94
            || tonalWeightSum / validFrameCount < 0.12)
            return insufficientResult();

        const auto candidates = rankCandidates(totalChroma);
        if (candidates.size() < 2 || candidates.front().correlation < 0.08)
            return insufficientResult();

        const auto& primary = candidates.front();
        const auto& runnerUp = candidates[1];
        const auto margin = juce::jmax(0.0, primary.correlation - runnerUp.correlation);
        const auto relativeAmbiguity = isRelativeMajorMinor(primary, runnerUp)
            && margin < 0.10;

        auto activeSegments = 0;
        auto supportedSegments = 0;
        for (size_t segment = 0; segment < segmentChroma.size(); ++segment)
        {
            if (segmentWeight[segment] <= 1.0e-12)
                continue;
            auto segmentSum = 0.0;
            for (const auto value : segmentChroma[segment])
                segmentSum += value;
            if (segmentSum <= 1.0e-12)
                continue;
            Chroma normalizedSegment = segmentChroma[segment];
            for (auto& value : normalizedSegment)
                value /= segmentSum;
            ++activeSegments;
            if (normalizedChromaCorrelation(normalizedSegment, primary) >= 0.10
                || (relativeAmbiguity
                    && normalizedChromaCorrelation(normalizedSegment, runnerUp) >= 0.10))
                ++supportedSegments;
        }

        const auto stability = activeSegments > 0
            ? static_cast<double>(supportedSegments) / activeSegments : 0.0;
        const auto profileFit = juce::jlimit(0.0, 1.0, (primary.correlation - 0.05) / 0.55);
        const auto marginQuality = juce::jlimit(0.0, 1.0, margin / 0.18);
        const auto frameCoverage = juce::jlimit(0.0, 1.0,
            static_cast<double>(validFrameCount) / (settings.minimumFrames * 3.0));
        const auto pitchCoverage = juce::jlimit(0.0, 1.0, (activePitchClasses - 2) / 6.0);
        const auto tonalQuality = juce::jlimit(0.0, 1.0,
            (tonalWeightSum / validFrameCount) / 0.55);
        const auto confidence = profileFit * (0.35 + 0.65 * marginQuality)
            * (0.45 + 0.55 * stability) * (0.45 + 0.55 * frameCoverage)
            * pitchCoverage * tonalQuality * (relativeAmbiguity ? 0.68 : 1.0);

        KeyAnalysis result;
        result.tonic = primary.tonic;
        result.mode = primary.mode;
        result.confidence = static_cast<float>(juce::jlimit(0.0, 1.0, confidence));
        result.score = normalizedScore(primary.correlation);
        result.detectionStatus = result.confidence < 0.35f
            ? KeyDetectionStatus::lowConfidence : KeyDetectionStatus::detected;
        result.ambiguous = relativeAmbiguity || margin < 0.06;

        const auto candidateLimit = juce::jmin(settings.maximumCandidates,
                                                static_cast<int>(candidates.size()));
        for (auto index = 1; index < candidateLimit; ++index)
        {
            const auto candidateConfidence = juce::jlimit(0.0, 1.0,
                juce::jmax(0.0, (candidates[static_cast<size_t>(index)].correlation - 0.05) / 0.55)
                    * (0.5 + 0.5 * stability));
            result.alternatives.push_back({ candidates[static_cast<size_t>(index)].tonic,
                candidates[static_cast<size_t>(index)].mode,
                static_cast<float>(candidateConfidence),
                normalizedScore(candidates[static_cast<size_t>(index)].correlation) });
        }
        return AnalyzerOutput::success(AnalysisPayload { std::move(result) });
    }
    catch (const std::bad_alloc&)
    {
        return AnalyzerOutput::failure("Not enough memory for Key analysis.");
    }
}
