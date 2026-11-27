#include "AutoSlicePlanner.h"
#include "../analysis/AnalysisTypes.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <limits>
#include <utility>

namespace auto_slicing
{
namespace
{
bool isUnit(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

// Metadata and configuration come from the same source and policy, so exact
// numeric equality is intentional. An unordered comparison never matches.
bool sameValue(double left, double right) noexcept
{
    return std::is_eq(left <=> right);
}

std::optional<Error> validateSource(const Source& source) noexcept
{
    if (source.sampleId == 0 || source.sampleRevision == 0 || source.analysisGeneration == 0)
        return Error { ErrorKind::input, "Missing sample identity, revision or analysis generation" };
    if (source.totalFrames <= 0 || !std::isfinite(source.sampleRate) || source.sampleRate <= 0.0)
        return Error { ErrorKind::input, "Source requires positive PCM length and a finite positive sample rate" };
    return std::nullopt;
}

std::optional<Error> validateConfig(const Config& config, const Source& source) noexcept
{
    const auto musicalFrames = std::ceil(config.minimumSliceSeconds * source.sampleRate);
    // Exclusive upper bound is exactly representable even if double rounds INT64_MAX up.
    if (config.engineMinimumFrames <= 0 || config.minimumBoundaryDistanceFrames < 0
        || !std::isfinite(config.minimumSliceSeconds) || config.minimumSliceSeconds < 0.0
        || musicalFrames >= 0x1p63 || !isUnit(config.sensitivity) || !isUnit(config.minimumSignificance)
        || config.maximumSlices == 0 || config.maximumSlices > Config::maximumAutoSlices)
        return Error { ErrorKind::configuration, "Invalid slice duration, spacing, sensitivity, significance or count limit" };
    return std::nullopt;
}

Frame minimumDuration(const Config& config, const Source& source) noexcept
{
    return std::max(config.engineMinimumFrames,
                    static_cast<Frame>(std::ceil(config.minimumSliceSeconds * source.sampleRate)));
}

bool isFallbackReason(FallbackReason reason) noexcept
{
    switch (reason)
    {
        case FallbackReason::noTransients:
        case FallbackReason::weakTransients:
        case FallbackReason::noFeasibleSplit:
        case FallbackReason::shortSource:
        case FallbackReason::sliceLimit:
        case FallbackReason::analysisFailed:
        case FallbackReason::truncatedAnalysis: return true;
        case FallbackReason::none: return false;
    }
    return false;
}

bool isCancelled(const AnalysisCancellationToken* token) noexcept
{
    return token != nullptr && token->isCancellationRequested();
}

Result cancelledResult() noexcept
{
    return { std::nullopt, Error { ErrorKind::cancelled, "Auto slicing cancelled" } };
}

struct Candidate
{
    Frame frame;
    double benefit;
};

double eventSignificance(const Transient& event) noexcept
{
    // Strength and flux are normalized by the analyzer. Confidence, when supplied,
    // attenuates weak evidence. Missing confidence leaves that evidence unweighted.
    const auto confidenceWeight = event.confidence ? 0.35 + 0.65 * *event.confidence : 1.0;
    return event.strength * confidenceWeight * (0.85 + 0.15 * event.spectralFlux);
}

// Weighted interval selection by cut count. Each row gives the best total benefit
// for exactly k cuts in a prefix. The growing cost of each extra cut avoids filling
// the budget with marginal attacks. Equal scores keep the earlier prefix or fewer cuts.
std::optional<std::vector<Boundary>> chooseBoundaries(const std::vector<Candidate>& candidates,
                                                       Frame spacing, std::size_t maxCuts,
                                                       const AnalysisCancellationToken* cancellation)
{
    const auto count = candidates.size();
    maxCuts = std::min(maxCuts, count);
    std::vector<std::size_t> compatiblePrefix(count);
    std::size_t compatible = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (isCancelled(cancellation))
            return std::nullopt;
        while (compatible < i && candidates[i].frame - candidates[compatible].frame >= spacing)
            ++compatible;
        compatiblePrefix[i] = compatible;
    }

    constexpr double impossible = -std::numeric_limits<double>::infinity();
    constexpr double additionalCutCost = 0.015;
    std::vector<double> previous(count + 1, 0.0);
    std::vector<double> current(count + 1, impossible);
    std::vector<std::vector<bool>> selected(maxCuts, std::vector<bool>(count));
    std::size_t bestCuts = 0;
    double bestScore = 0.0;

    for (std::size_t cuts = 1; cuts <= maxCuts; ++cuts)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (isCancelled(cancellation))
                return std::nullopt;
            const auto take = previous[compatiblePrefix[i]] + candidates[i].benefit;
            selected[cuts - 1][i] = take > current[i];
            current[i + 1] = selected[cuts - 1][i] ? take : current[i];
        }

        const auto cutCost = additionalCutCost * static_cast<double>(cuts * (cuts - 1)) / 2.0;
        if (current.back() - cutCost > bestScore)
        {
            bestScore = current.back() - cutCost;
            bestCuts = cuts;
        }
        std::swap(previous, current);
        current.front() = impossible;
    }

    std::vector<Boundary> cuts;
    cuts.reserve(bestCuts);
    for (auto prefix = count; bestCuts > 0 && prefix > 0; )
    {
        const auto index = prefix - 1;
        if (selected[bestCuts - 1][index])
        {
            cuts.push_back({ candidates[index].frame, BoundaryOrigin::transient });
            --bestCuts;
            prefix = compatiblePrefix[index];
        }
        else
            --prefix;
    }
    std::reverse(cuts.begin(), cuts.end());
    return cuts;
}
}

bool Source::operator==(const Source& other) const noexcept
{
    return sampleId == other.sampleId && sampleRevision == other.sampleRevision
        && analysisGeneration == other.analysisGeneration && totalFrames == other.totalFrames
        && sameValue(sampleRate, other.sampleRate);
}

bool Config::operator==(const Config& other) const noexcept
{
    return engineMinimumFrames == other.engineMinimumFrames
        && sameValue(minimumSliceSeconds, other.minimumSliceSeconds)
        && minimumBoundaryDistanceFrames == other.minimumBoundaryDistanceFrames
        && sameValue(sensitivity, other.sensitivity)
        && sameValue(minimumSignificance, other.minimumSignificance)
        && maximumSlices == other.maximumSlices;
}

std::optional<Slice> Plan::sliceAt(std::size_t index) const noexcept
{
    if (index >= sliceCount())
        return std::nullopt;
    return Slice { boundaries[index], boundaries[index + 1] };
}

std::optional<Error> validatePlan(const Plan& plan, const Source& expectedSource,
                                  const Config& expectedConfig) noexcept
{
    if (const auto error = validateSource(expectedSource))
        return error;
    if (plan.source != expectedSource)
        return Error { ErrorKind::input, "Plan source or revision differs from the expected source" };
    if (const auto error = validateConfig(expectedConfig, expectedSource))
        return error;
    if (plan.config != expectedConfig)
        return Error { ErrorKind::configuration, "Plan configuration differs from the expected configuration" };
    if (plan.algorithmVersion != Plan::currentAlgorithmVersion)
        return Error { ErrorKind::structure, "Unsupported planner algorithm version" };

    const auto& boundaries = plan.boundaries;
    const auto length = expectedSource.totalFrames;
    const auto duration = minimumDuration(expectedConfig, expectedSource);
    if (boundaries.size() < 2)
        return Error { ErrorKind::structure, "Plan contains no slice" };
    if (boundaries.front() != Boundary { 0, BoundaryOrigin::sourceEdge }
        || boundaries.back() != Boundary { length, BoundaryOrigin::sourceEdge })
        return Error { ErrorKind::structure, "Plan must cover both source edges" };
    if (plan.sliceCount() > expectedConfig.maximumSlices)
        return Error { ErrorKind::configuration, "Slice count exceeds the configured limit" };

    switch (plan.strategy)
    {
        case Strategy::wholeSample:
            if (plan.sliceCount() != 1 || !isFallbackReason(plan.diagnostics.fallback))
                return Error { ErrorKind::structure, "Whole-sample plan requires one slice and a fallback reason" };
            break;
        case Strategy::transient:
            if (plan.sliceCount() < 2 || plan.diagnostics.fallback != FallbackReason::none)
                return Error { ErrorKind::structure, "Transient plan requires at least one cut and no fallback reason" };
            break;
        default:
            return Error { ErrorKind::structure, "Unknown slicing strategy" };
    }

    if (plan.diagnostics.belowEngineMinimum != (length < expectedConfig.engineMinimumFrames))
        return Error { ErrorKind::structure, "Incorrect engine minimum diagnostic" };

    for (std::size_t i = 1; i < boundaries.size(); ++i)
    {
        const auto& boundary = boundaries[i];
        const auto previous = boundaries[i - 1].frame;
        if (boundary.frame <= previous || boundary.frame > length)
            return Error { ErrorKind::structure, "Boundaries must increase strictly within the source" };
        if (i + 1 < boundaries.size() && boundary.origin != BoundaryOrigin::transient)
            return Error { ErrorKind::structure, "Interior boundary lacks transient provenance" };
        if (plan.strategy == Strategy::transient && boundary.frame - previous < duration)
            return Error { ErrorKind::configuration, "Slice is shorter than the configured minimum" };
        if (plan.strategy == Strategy::transient
            && boundary.frame - previous < expectedConfig.minimumBoundaryDistanceFrames)
            return Error { ErrorKind::configuration, "Boundaries are closer than the configured distance" };
    }
    return std::nullopt;
}

Result makePlan(const Source& source, const AnalysisResult& analysis, const Config& config,
                const AnalysisCancellationToken* cancellation)
{
    if (const auto error = validateSource(source))
        return { std::nullopt, error };
    if (const auto error = validateConfig(config, source))
        return { std::nullopt, error };
    if (analysis.identity != AnalysisIdentity { source.sampleId, source.analysisGeneration })
        return { std::nullopt, Error { ErrorKind::input, "Analysis belongs to a different sample or generation" } };
    if (isCancelled(cancellation) || analysis.status == AnalysisStatus::cancelled)
        return cancelledResult();
    if (analysis.status != AnalysisStatus::completed && analysis.status != AnalysisStatus::failed)
        return { std::nullopt, Error { ErrorKind::input, "Analysis has not completed" } };

    Plan plan;
    plan.source = source;
    plan.config = config;
    plan.diagnostics.belowEngineMinimum = source.totalFrames < config.engineMinimumFrames;

    const auto finish = [&](FallbackReason reason, std::vector<Boundary> cuts = {}) -> Result
    {
        if (isCancelled(cancellation))
            return cancelledResult();
        plan.strategy = cuts.empty() ? Strategy::wholeSample : Strategy::transient;
        plan.diagnostics.fallback = reason;
        plan.boundaries.reserve(cuts.size() + 2);
        plan.boundaries.push_back({ 0, BoundaryOrigin::sourceEdge });
        plan.boundaries.insert(plan.boundaries.end(), cuts.begin(), cuts.end());
        plan.boundaries.push_back({ source.totalFrames, BoundaryOrigin::sourceEdge });
        if (const auto error = validatePlan(plan, source, config))
            return { std::nullopt, error };
        return { std::move(plan), std::nullopt };
    };

    const auto duration = minimumDuration(config, source);
    if (analysis.status == AnalysisStatus::failed)
        return finish(FallbackReason::analysisFailed);

    const auto* transients = analysis.transients ? &*analysis.transients : nullptr;
    if (transients == nullptr)
        return finish(source.totalFrames / 2 < duration ? FallbackReason::shortSource
                                                       : FallbackReason::noTransients);

    plan.diagnostics.reportedEvents = transients->totalEventCount;
    const auto status = transients->detectionStatus;
    const auto stored = transients->positions.size();
    if (transients->sampleCount != source.totalFrames || !sameValue(transients->sampleRate, source.sampleRate)
        || (status == TransientDetectionStatus::detected && stored != transients->totalEventCount)
        || (status == TransientDetectionStatus::insufficientData && (stored != 0 || transients->totalEventCount != 0))
        || (status == TransientDetectionStatus::truncated && stored >= transients->totalEventCount)
        || (status != TransientDetectionStatus::detected && status != TransientDetectionStatus::insufficientData
            && status != TransientDetectionStatus::truncated))
        return { std::nullopt, Error { ErrorKind::input, "Transient analysis has inconsistent geometry or event counts" } };

    const auto spacing = std::max(duration, config.minimumBoundaryDistanceFrames);
    const auto windowWidth = std::max<Frame>(1, spacing / 4);
    const auto threshold = config.minimumSignificance
                         + (1.0 - config.sensitivity) * (1.0 - config.minimumSignificance) * 0.30;
    std::vector<Candidate> candidates;
    Frame previous = -1;
    bool hasSignificantEvent = false;
    for (const auto& event : transients->positions)
    {
        if (isCancelled(cancellation))
            return cancelledResult();
        const auto frame = static_cast<Frame>(event.samplePosition);
        if (frame <= previous || frame >= source.totalFrames || !isUnit(event.strength)
            || !isUnit(event.spectralFlux) || (event.confidence && !isUnit(*event.confidence)))
            return { std::nullopt, Error { ErrorKind::input, "Invalid or unordered transient position or score" } };
        previous = frame;

        const auto benefit = eventSignificance(event) - threshold;
        if (benefit <= 0.0)
            continue;
        hasSignificantEvent = true;
        if (frame < spacing || source.totalFrames - frame < spacing)
            continue;
        ++plan.diagnostics.eligibleEvents;

        // Retain the strongest attack in each small time window. Use sparse windows
        // so a long source with few attacks never allocates by PCM length.
        if (candidates.empty() || candidates.back().frame / windowWidth != frame / windowWidth)
            candidates.push_back({ frame, benefit });
        else if (benefit > candidates.back().benefit)
            candidates.back() = { frame, benefit };
    }

    plan.diagnostics.retainedCandidates = candidates.size();
    if (source.totalFrames / 2 < duration)
        return finish(FallbackReason::shortSource);
    if (status == TransientDetectionStatus::truncated)
        return finish(FallbackReason::truncatedAnalysis);
    if (stored == 0)
        return finish(FallbackReason::noTransients);
    if (!hasSignificantEvent)
        return finish(FallbackReason::weakTransients);
    if (candidates.empty())
        return finish(FallbackReason::noFeasibleSplit);
    if (config.maximumSlices == 1)
        return finish(FallbackReason::sliceLimit);

    auto cuts = chooseBoundaries(candidates, spacing, config.maximumSlices - 1, cancellation);
    if (!cuts)
        return cancelledResult();
    return finish(FallbackReason::none, std::move(*cuts));
}
}
