#include "BeatGridEstimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace auto_slicing::beat_grid
{
namespace
{
constexpr std::size_t phaseBinCount = 64;
constexpr double phasePeakSeparation = 0.12;
constexpr double phaseSupportRadius = 0.10;
constexpr double bpmEquivalentEpsilon = 0.01;

struct Event
{
    Frame frame = 0;
    double weight = 0.0;
};

struct PhaseAssessment
{
    std::optional<double> offset;
    float confidence = 0.0f;
    float support = 0.0f;
    std::size_t supportingEvents = 0;
    double observedBeatSpan = 0.0;
    bool competingPeaks = false;
};

const char* reliabilityName(Reliability reliability) noexcept
{
    switch (reliability)
    {
        case Reliability::reliable: return "reliable";
        case Reliability::ambiguous: return "ambiguous";
        case Reliability::unreliable: return "unreliable";
        case Reliability::insufficientData: return "insufficient data";
    }
    return "unknown reliability";
}

double positiveModulo(double value, double modulus) noexcept
{
    auto result = std::fmod(value, modulus);
    if (result < 0.0)
        result += modulus;
    return result >= modulus ? 0.0 : result;
}

double circularDistance(double first, double second) noexcept
{
    const auto difference = std::abs(first - second);
    return std::min(difference, 1.0 - difference);
}

std::optional<std::string_view> validateInput(const Input& input, const Config& config) noexcept
{
    if (input.sampleId == 0 || input.sampleRevision == 0 || input.analysisGeneration == 0
        || input.totalFrames <= 0 || !std::isfinite(input.sampleRate) || input.sampleRate <= 0.0)
        return "Beat grid requires a valid sample identity, revision, frame count and sample rate";

    if (!std::isfinite(config.minimumBpmConfidence) || config.minimumBpmConfidence < 0.0f
        || config.minimumBpmConfidence > 1.0f || !std::isfinite(config.minimumPhaseConfidence)
        || config.minimumPhaseConfidence < 0.0f || config.minimumPhaseConfidence > 1.0f
        || !std::isfinite(config.tempoConfidenceTie) || config.tempoConfidenceTie < 0.0f
        || config.tempoConfidenceTie > 1.0f || !std::isfinite(config.phaseConfidenceAdvantage)
        || config.phaseConfidenceAdvantage < 0.0f || config.phaseConfidenceAdvantage > 1.0f
        || !std::isfinite(config.competingPhaseRatio) || config.competingPhaseRatio < 0.0f
        || config.competingPhaseRatio > 1.0f || !std::isfinite(config.minimumObservedBeats)
        || config.minimumObservedBeats <= 0.0
        || !std::isfinite(config.minimumCandidateIntervalSeconds)
        || config.minimumCandidateIntervalSeconds <= 0.0 || config.minimumPhaseEvents < 2
        || config.maximumTempoCandidates == 0 || config.maximumTempoCandidates > 32)
        return "Beat grid configuration contains an invalid threshold or limit";

    if (input.analysis == nullptr)
        return std::nullopt;

    if (input.analysis->identity != AnalysisIdentity { input.sampleId, input.analysisGeneration })
        return "Analysis result belongs to a different sample or generation";
    if (!input.analysis->isValid())
        return "Analysis result is malformed or incomplete";

    if (input.analysis->transients)
    {
        const auto& transients = *input.analysis->transients;
        const auto rateTolerance = std::max(0.01, input.sampleRate * 1.0e-7);
        if (transients.sampleCount != input.totalFrames
            || std::abs(transients.sampleRate - input.sampleRate) > rateTolerance)
            return "Transient analysis geometry differs from the source sample";
    }
    return std::nullopt;
}

int subdivisionDivisor(Resolution resolution) noexcept
{
    switch (resolution)
    {
        case Resolution::beat: return 1;
        case Resolution::halfBeat: return 2;
        case Resolution::quarterBeat: return 4;
        case Resolution::eighthBeat: return 8;
        case Resolution::sixteenthBeat: return 16;
    }
    return 0;
}

Resolution chooseResolution(double beatPeriodFrames, double sampleRate,
                            const Config& config) noexcept
{
    const auto minimumFrames = std::max(1.0,
        std::ceil(config.minimumCandidateIntervalSeconds * sampleRate));
    for (const auto resolution : { Resolution::sixteenthBeat, Resolution::eighthBeat,
                                   Resolution::quarterBeat, Resolution::halfBeat, Resolution::beat })
    {
        const auto divisor = subdivisionDivisor(resolution);
        if (beatPeriodFrames / static_cast<double>(divisor) >= minimumFrames)
            return resolution;
    }
    return Resolution::beat;
}

PhaseAssessment assessPhase(const std::vector<Event>& events, double periodFrames,
                            double minimumObservedBeats, std::size_t minimumEvents,
                            float competingPeakRatio)
{
    PhaseAssessment result;
    if (events.empty() || !std::isfinite(periodFrames) || periodFrames <= 0.0)
        return result;

    std::array<double, phaseBinCount> histogram {};
    double totalWeight = 0.0;
    for (const auto& event : events)
    {
        const auto phase = positiveModulo(static_cast<double>(event.frame), periodFrames)
            / periodFrames;
        const auto centre = static_cast<long long>(std::llround(phase * phaseBinCount));
        constexpr std::array<double, 5> kernel { 1.0, 0.72, 0.38, 0.14, 0.0 };
        for (int offset = -3; offset <= 3; ++offset)
        {
            const auto distance = std::abs(offset);
            const auto bin = static_cast<std::size_t>(
                (centre + offset + static_cast<long long>(phaseBinCount) * 2)
                % static_cast<long long>(phaseBinCount));
            histogram[bin] += event.weight * kernel[static_cast<std::size_t>(distance)];
        }
        totalWeight += event.weight;
    }

    if (totalWeight <= 0.0)
        return result;

    struct Peak { std::size_t bin; double weight; };
    std::vector<Peak> peaks;
    for (std::size_t bin = 0; bin < phaseBinCount; ++bin)
    {
        const auto previous = (bin + phaseBinCount - 1) % phaseBinCount;
        const auto next = (bin + 1) % phaseBinCount;
        if (histogram[bin] >= histogram[previous] && histogram[bin] >= histogram[next]
            && histogram[bin] > 0.0)
            peaks.push_back({ bin, histogram[bin] });
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& left, const auto& right)
    {
        if (left.weight < right.weight)
            return false;
        if (left.weight > right.weight)
            return left.weight > right.weight;
        return left.bin < right.bin;
    });

    std::vector<Peak> separatedPeaks;
    for (const auto& peak : peaks)
    {
        const auto phase = static_cast<double>(peak.bin) / phaseBinCount;
        const auto separated = std::all_of(separatedPeaks.begin(), separatedPeaks.end(),
            [phase](const Peak& existing)
            {
                return circularDistance(phase,
                    static_cast<double>(existing.bin) / phaseBinCount) >= phasePeakSeparation;
            });
        if (separated)
            separatedPeaks.push_back(peak);
        if (separatedPeaks.size() == 2)
            break;
    }
    if (separatedPeaks.empty())
        return result;

    const auto primaryPhase = static_cast<double>(separatedPeaks.front().bin) / phaseBinCount;
    if (separatedPeaks.size() > 1
        && separatedPeaks[1].weight >= separatedPeaks[0].weight * competingPeakRatio)
        result.competingPeaks = true;

    double localCosine = 0.0;
    double localSine = 0.0;
    double localWeight = 0.0;
    double supportedWeight = 0.0;
    double localFirst = std::numeric_limits<double>::max();
    double localLast = -1.0;
    for (const auto& event : events)
    {
        const auto phase = positiveModulo(static_cast<double>(event.frame), periodFrames)
            / periodFrames;
        const auto distance = circularDistance(phase, primaryPhase);
        if (distance <= phaseSupportRadius)
        {
            const auto signedDifference = phase - primaryPhase > 0.5 ? phase - primaryPhase - 1.0
                : (phase - primaryPhase < -0.5 ? phase - primaryPhase + 1.0
                                               : phase - primaryPhase);
            const auto angle = 2.0 * juce::MathConstants<double>::pi * signedDifference;
            localCosine += event.weight * std::cos(angle);
            localSine += event.weight * std::sin(angle);
            localWeight += event.weight;
            ++result.supportingEvents;
            localFirst = std::min(localFirst, static_cast<double>(event.frame));
            localLast = std::max(localLast, static_cast<double>(event.frame));
        }
        if (distance <= phaseSupportRadius)
            supportedWeight += event.weight;
    }

    if (localWeight <= 0.0)
        return result;

    auto refinedPhase = primaryPhase + std::atan2(localSine, localCosine)
        / (2.0 * juce::MathConstants<double>::pi);
    refinedPhase = positiveModulo(refinedPhase, 1.0);
    result.offset = refinedPhase * periodFrames;
    result.support = static_cast<float>(juce::jlimit(0.0, 1.0, supportedWeight / totalWeight));
    result.observedBeatSpan = localLast >= localFirst
        ? (localLast - localFirst) / periodFrames : 0.0;

    const auto eventFactor = juce::jlimit(0.0, 1.0,
        static_cast<double>(result.supportingEvents) / static_cast<double>(minimumEvents));
    const auto spanFactor = juce::jlimit(0.0, 1.0,
        result.observedBeatSpan / minimumObservedBeats);
    result.confidence = static_cast<float>(juce::jlimit(0.0, 1.0,
        static_cast<double>(result.support) * eventFactor * spanFactor));
    return result;
}

bool isOctaveRelated(double first, double second) noexcept
{
    const auto ratio = first / second;
    return (ratio >= 1.90 && ratio <= 2.10) || (ratio >= 0.476 && ratio <= 0.526);
}

void setReason(BeatGrid& grid, Reliability reliability, Reason reason) noexcept
{
    grid.reliability = reliability;
    grid.reason = reason;
}

std::vector<TempoCandidate> makeTempoCandidates(const BpmAnalysis& analysis,
                                               double primaryTempo,
                                               float primaryConfidence,
                                               float primaryScore,
                                               double sampleRate,
                                               const Config& config,
                                               bool manualTempo)
{
    std::vector<TempoCandidate> result;
    result.reserve(std::min(config.maximumTempoCandidates, analysis.alternatives.size() + 1));
    const auto add = [&result, sampleRate, &config](double bpm, float confidence,
                                                   float score, bool alternative)
    {
        if (result.size() >= config.maximumTempoCandidates || !std::isfinite(bpm) || bpm <= 0.0
            || !std::isfinite(confidence) || confidence < 0.0f || confidence > 1.0f
            || !std::isfinite(score) || score < 0.0f || score > 1.0f)
            return;
        if (std::any_of(result.begin(), result.end(), [bpm](const auto& item)
            {
                return std::abs(item.bpm - bpm) / std::max(item.bpm, bpm) < bpmEquivalentEpsilon;
            }))
            return;
        const auto period = 60.0 * sampleRate / bpm;
        if (!std::isfinite(period) || period <= 0.0
            || static_cast<long double>(period) > static_cast<long double>(std::numeric_limits<Frame>::max()))
            return;
        result.push_back({ bpm, period, confidence, score, std::nullopt, 0.0f, 0.0f,
                           0, 0.0, false, alternative });
    };

    add(primaryTempo, primaryConfidence, primaryScore, false);
    if (!manualTempo)
        for (const auto& alternative : analysis.alternatives)
            add(alternative.value, alternative.confidence, alternative.score, true);
    return result;
}
}

Result Estimator::estimate(const Input& input, const Config& config)
{
    Result result;
    result.grid.sampleId = input.sampleId;
    result.grid.sampleRevision = input.sampleRevision;
    result.grid.totalFrames = input.totalFrames;
    result.grid.sampleRate = input.sampleRate;

    if (const auto error = validateInput(input, config))
    {
        result.error = error;
        const auto invalidSource = input.sampleId == 0 || input.sampleRevision == 0
            || input.analysisGeneration == 0 || input.totalFrames <= 0
            || !std::isfinite(input.sampleRate) || input.sampleRate <= 0.0;
        const auto invalidAnalysis = input.analysis != nullptr
            && (input.analysis->identity != AnalysisIdentity { input.sampleId, input.analysisGeneration }
                || !input.analysis->isValid()
                || (input.analysis->transients
                    && (input.analysis->transients->sampleCount != input.totalFrames
                        || std::abs(input.analysis->transients->sampleRate - input.sampleRate)
                            > std::max(0.01, input.sampleRate * 1.0e-7))));
        setReason(result.grid, Reliability::unreliable,
                  invalidSource || invalidAnalysis ? Reason::invalidInput : Reason::invalidConfiguration);
        return result;
    }

    if (input.analysis == nullptr || input.analysis->status != AnalysisStatus::completed)
    {
        setReason(result.grid, Reliability::insufficientData, Reason::missingTempo);
        result.diagnostics.detail = "A completed matching AnalysisResult is required";
        return result;
    }

    // The phase histogram uses floating-point periods. Above 2^53 PCM frames a
    // binary64 frame coordinate is no longer integral, so refuse false precision.
    if (static_cast<long double>(input.totalFrames) > 0x1p53L)
    {
        setReason(result.grid, Reliability::unreliable, Reason::numericalLimit);
        result.diagnostics.detail = "Source length exceeds exact binary64 PCM-frame precision";
        return result;
    }

    if (!input.analysis->bpm || !input.analysis->bpm->effectiveValue())
    {
        setReason(result.grid, Reliability::insufficientData, Reason::missingTempo);
        result.diagnostics.detail = "BPM analysis contains no usable tempo estimate";
        return result;
    }

    const auto& bpmAnalysis = *input.analysis->bpm;
    const auto isManualTempo = bpmAnalysis.manualOverride.has_value();
    const auto primaryTempo = *bpmAnalysis.effectiveValue();
    const auto primaryConfidence = isManualTempo ? 1.0f : bpmAnalysis.confidence;
    const auto primaryScore = isManualTempo ? 1.0f : bpmAnalysis.score;
    result.grid.bpmConfidence = primaryConfidence;
    result.diagnostics.detectorReportedOctaveAmbiguity = bpmAnalysis.octaveAmbiguous;

    const auto beatPeriod = 60.0 * input.sampleRate / primaryTempo;
    if (!std::isfinite(beatPeriod) || beatPeriod <= 0.0
        || static_cast<long double>(beatPeriod) > static_cast<long double>(std::numeric_limits<Frame>::max()))
    {
        setReason(result.grid, Reliability::unreliable, Reason::numericalLimit);
        result.diagnostics.detail = "Beat period cannot be represented in source PCM frames";
        return result;
    }
    result.grid.bpm = primaryTempo;
    result.grid.beatPeriodFrames = beatPeriod;
    result.grid.resolution = chooseResolution(beatPeriod, input.sampleRate, config);
    result.grid.tempoCandidates = makeTempoCandidates(bpmAnalysis, primaryTempo,
        primaryConfidence, primaryScore, input.sampleRate, config, isManualTempo);

    if (primaryConfidence < config.minimumBpmConfidence
        || (!isManualTempo && bpmAnalysis.detectionStatus != BpmDetectionStatus::detected))
    {
        setReason(result.grid, Reliability::unreliable, Reason::weakTempo);
        result.diagnostics.detail = "The BPM detector confidence is below the grid threshold";
        return result;
    }

    const auto durationBeats = static_cast<double>(input.totalFrames) / beatPeriod;
    if (durationBeats < config.minimumObservedBeats)
    {
        setReason(result.grid, Reliability::insufficientData, Reason::insufficientDuration);
        result.diagnostics.observedBeatSpan = durationBeats;
        result.diagnostics.detail = "The source is too short to establish a stable beat phase";
        return result;
    }

    if (!input.analysis->transients)
    {
        setReason(result.grid, Reliability::insufficientData, Reason::missingTransients);
        result.diagnostics.detail = "No transient analysis is available for phase estimation";
        return result;
    }

    const auto& transientAnalysis = *input.analysis->transients;
    if (transientAnalysis.detectionStatus == TransientDetectionStatus::truncated)
    {
        setReason(result.grid, Reliability::unreliable, Reason::truncatedTransients);
        result.diagnostics.detail = "A truncated transient list cannot safely establish phase";
        return result;
    }
    if (transientAnalysis.positions.size() > 65536)
    {
        setReason(result.grid, Reliability::unreliable, Reason::numericalLimit);
        result.diagnostics.detail = "Transient input exceeds the Analyzer event budget";
        return result;
    }

    std::vector<Event> events;
    events.reserve(transientAnalysis.positions.size());
    for (const auto& transient : transientAnalysis.positions)
    {
        auto weight = static_cast<double>(transient.strength);
        if (transient.confidence)
            weight *= 0.35 + 0.65 * static_cast<double>(*transient.confidence);
        weight *= 0.85 + 0.15 * static_cast<double>(transient.spectralFlux);
        if (weight > 1.0e-6)
            events.push_back({ static_cast<Frame>(transient.samplePosition), weight });
    }
    result.diagnostics.validTransientCount = events.size();
    if (events.size() < config.minimumPhaseEvents)
    {
        setReason(result.grid, Reliability::insufficientData, Reason::insufficientEvents);
        result.diagnostics.detail = "Too few non-zero transient events are available for phase estimation";
        return result;
    }

    auto candidates = result.grid.tempoCandidates;
    for (auto& candidate : candidates)
    {
        const auto phase = assessPhase(events, candidate.beatPeriodFrames, config.minimumObservedBeats,
                                       config.minimumPhaseEvents, config.competingPhaseRatio);
        candidate.phaseOffsetFrames = phase.offset;
        candidate.phaseConfidence = phase.confidence;
        candidate.phaseSupport = phase.support;
        candidate.phaseSupportingEventCount = phase.supportingEvents;
        candidate.observedBeatSpan = phase.observedBeatSpan;
        candidate.phaseAmbiguous = phase.competingPeaks;
    }

    if (candidates.empty())
    {
        setReason(result.grid, Reliability::unreliable, Reason::numericalLimit);
        result.diagnostics.detail = "No BPM hypothesis has a representable frame period";
        return result;
    }

    const auto primary = candidates.front();
    auto selectedIndex = std::size_t { 0 };
    for (std::size_t i = 1; i < candidates.size(); ++i)
    {
        const auto& candidate = candidates[i];
        const auto phaseAdvantage = candidate.phaseConfidence - primary.phaseConfidence;
        const auto tempoComparable = candidate.bpmConfidence + config.tempoConfidenceTie
            >= primary.bpmConfidence;
        if (!candidate.phaseAmbiguous && tempoComparable
            && phaseAdvantage >= config.phaseConfidenceAdvantage)
        {
            selectedIndex = i;
            result.diagnostics.tempoWasResolvedByPhase = true;
            break;
        }
    }

    auto selected = candidates[selectedIndex];
    bool competingTempo = false;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (i == selectedIndex)
            continue;
        const auto& other = candidates[i];
        const auto confidenceClose = std::abs(other.bpmConfidence - selected.bpmConfidence)
            <= config.tempoConfidenceTie;
        const auto phaseClose = std::abs(other.phaseConfidence - selected.phaseConfidence)
            < config.phaseConfidenceAdvantage * 0.5f;
        const auto bothHaveSupport = other.phaseConfidence >= config.minimumPhaseConfidence
            && selected.phaseConfidence >= config.minimumPhaseConfidence;
        if (confidenceClose && phaseClose && bothHaveSupport)
            competingTempo = true;
    }

    const auto octaveResolvedByPhase = std::any_of(candidates.begin(), candidates.end(),
        [&selected, &config](const auto& candidate)
        {
            return std::abs(candidate.bpm - selected.bpm) > 1.0e-9
                && isOctaveRelated(selected.bpm, candidate.bpm)
                && std::abs(candidate.bpmConfidence - selected.bpmConfidence) <= config.tempoConfidenceTie
                && !selected.phaseAmbiguous
                && selected.phaseConfidence >= config.minimumPhaseConfidence
                && selected.phaseConfidence - candidate.phaseConfidence >= config.phaseConfidenceAdvantage;
        });
    if (!isManualTempo && bpmAnalysis.octaveAmbiguous && !octaveResolvedByPhase)
        competingTempo = true;

    result.grid.bpm = selected.bpm;
    result.grid.beatPeriodFrames = selected.beatPeriodFrames;
    result.grid.bpmConfidence = selected.bpmConfidence;
    result.grid.phaseConfidence = selected.phaseConfidence;
    result.diagnostics.primaryPhaseSupport = selected.phaseSupport;
    result.diagnostics.phaseSupportingEventCount = selected.phaseSupportingEventCount;
    result.diagnostics.observedBeatSpan = selected.observedBeatSpan;
    result.diagnostics.phaseHasCompetingPeaks = selected.phaseAmbiguous;
    result.grid.tempoCandidates = candidates;

    if (competingTempo)
    {
        setReason(result.grid, Reliability::ambiguous, Reason::competingTempos);
        result.grid.phaseOffsetFrames.reset();
        result.diagnostics.detail = "Comparable BPM hypotheses fit the transient timing; tempo is ambiguous";
        return result;
    }
    if (selected.phaseAmbiguous)
    {
        setReason(result.grid, Reliability::ambiguous, Reason::competingPhases);
        result.grid.phaseOffsetFrames.reset();
        result.diagnostics.detail = "Several separated phase offsets have comparable transient support";
        return result;
    }
    if (!selected.phaseOffsetFrames || selected.phaseConfidence < config.minimumPhaseConfidence)
    {
        setReason(result.grid, Reliability::unreliable, Reason::weakPhaseEvidence);
        result.grid.phaseOffsetFrames.reset();
        result.diagnostics.detail = "Transient timing does not form a sufficiently coherent phase";
        return result;
    }
    if (!isManualTempo && bpmAnalysis.detectionStatus != BpmDetectionStatus::detected)
    {
        setReason(result.grid, Reliability::unreliable, Reason::weakTempo);
        result.grid.phaseOffsetFrames.reset();
        result.diagnostics.detail = "The tempo remains below the detector reliability status";
        return result;
    }

    result.grid.phaseOffsetFrames = positiveModulo(*selected.phaseOffsetFrames,
                                                    selected.beatPeriodFrames);
    setReason(result.grid, Reliability::reliable, Reason::none);
    result.diagnostics.detail = result.diagnostics.tempoWasResolvedByPhase
        ? "The alternative tempo has materially stronger phase support"
        : reliabilityName(result.grid.reliability);
    return result;
}

std::optional<Frame> Estimator::frameAtSubdivision(const BeatGrid& grid,
                                                    std::int64_t subdivisionIndex,
                                                    Resolution resolution) noexcept
{
    if (!grid.phaseOffsetFrames || !grid.beatPeriodFrames || grid.totalFrames <= 0
        || !std::isfinite(*grid.phaseOffsetFrames) || !std::isfinite(*grid.beatPeriodFrames)
        || *grid.phaseOffsetFrames < 0.0 || *grid.beatPeriodFrames <= 0.0)
        return std::nullopt;
    const auto divisor = subdivisionDivisor(resolution);
    if (divisor <= 0)
        return std::nullopt;

    const auto position = static_cast<long double>(*grid.phaseOffsetFrames)
        + static_cast<long double>(subdivisionIndex)
            * (static_cast<long double>(*grid.beatPeriodFrames) / divisor);
    if (!std::isfinite(position) || position < 0.0L
        || position >= static_cast<long double>(grid.totalFrames))
        return std::nullopt;
    const auto rounded = std::round(position);
    if (rounded < 0.0L || rounded >= static_cast<long double>(grid.totalFrames)
        || rounded > static_cast<long double>(std::numeric_limits<Frame>::max()))
        return std::nullopt;
    return static_cast<Frame>(rounded);
}
}
