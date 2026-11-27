#pragma once

#include "../analysis/AnalysisTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace auto_slicing::beat_grid
{
using Frame = std::int64_t;

enum class Reliability { reliable, ambiguous, unreliable, insufficientData };
enum class Reason
{
    none,
    invalidInput,
    invalidConfiguration,
    missingTempo,
    weakTempo,
    missingTransients,
    insufficientEvents,
    insufficientDuration,
    truncatedTransients,
    weakPhaseEvidence,
    competingPhases,
    competingTempos,
    numericalLimit
};

enum class Resolution { beat, halfBeat, quarterBeat, eighthBeat, sixteenthBeat };

struct Config
{
    float minimumBpmConfidence = 0.38f;
    float minimumPhaseConfidence = 0.60f;
    float tempoConfidenceTie = 0.08f;
    float phaseConfidenceAdvantage = 0.20f;
    float competingPhaseRatio = 0.78f;
    double minimumObservedBeats = 3.0;
    double minimumCandidateIntervalSeconds = 0.060;
    std::size_t minimumPhaseEvents = 4;
    std::size_t maximumTempoCandidates = 8;

    bool operator==(const Config&) const noexcept = default;
};

struct Input
{
    std::uint64_t sampleId = 0;
    std::uint64_t sampleRevision = 0;
    std::uint64_t analysisGeneration = 0;
    Frame totalFrames = 0;
    double sampleRate = 0.0;
    const AnalysisResult* analysis = nullptr;
};

struct TempoCandidate
{
    double bpm = 0.0;
    double beatPeriodFrames = 0.0;
    float bpmConfidence = 0.0f;
    float bpmScore = 0.0f;
    // Candidate-local peak is diagnostic only unless this is the reliable selected grid.
    std::optional<double> phaseOffsetFrames;
    float phaseConfidence = 0.0f;
    float phaseSupport = 0.0f;
    std::size_t phaseSupportingEventCount = 0;
    double observedBeatSpan = 0.0;
    bool phaseAmbiguous = false;
    bool isAlternative = false;
};

struct BeatGrid
{
    std::uint64_t sampleId = 0;
    std::uint64_t sampleRevision = 0;
    Frame totalFrames = 0;
    double sampleRate = 0.0;
    std::optional<double> bpm;
    std::optional<double> beatPeriodFrames;
    // Offset is in [0, beatPeriodFrames); beat index 0 is at this source frame.
    // Earlier beats use negative indices. Empty unless phase is sufficiently reliable.
    std::optional<double> phaseOffsetFrames;
    float bpmConfidence = 0.0f;
    float phaseConfidence = 0.0f;
    Reliability reliability = Reliability::insufficientData;
    Reason reason = Reason::missingTempo;
    Resolution resolution = Resolution::beat;
    std::vector<TempoCandidate> tempoCandidates;
};

struct Diagnostics
{
    std::size_t validTransientCount = 0;
    std::size_t phaseSupportingEventCount = 0;
    double observedBeatSpan = 0.0;
    float primaryPhaseSupport = 0.0f;
    bool detectorReportedOctaveAmbiguity = false;
    bool phaseHasCompetingPeaks = false;
    bool tempoWasResolvedByPhase = false;
    std::string_view detail;
};

struct Result
{
    BeatGrid grid;
    Diagnostics diagnostics;
    std::optional<std::string_view> error;

    bool hasUsablePhase() const noexcept
    {
        return grid.reliability == Reliability::reliable && grid.phaseOffsetFrames.has_value();
    }
};

class Estimator final
{
public:
    // Pure worker-side estimator. It performs no audio I/O and does not depend on
    // SliceEngine, the transient planner, UI, or playback state.
    static Result estimate(const Input& input, const Config& config = {});

    // Calculate tick i from the original phase and period (rather than incrementing
    // a rounded prior tick). Out-of-file positions return nullopt.
    static std::optional<Frame> frameAtSubdivision(const BeatGrid& grid,
                                                   std::int64_t subdivisionIndex,
                                                   Resolution resolution) noexcept;

    static std::optional<Frame> frameAtBeat(const BeatGrid& grid,
                                             std::int64_t beatIndex) noexcept
    {
        return frameAtSubdivision(grid, beatIndex, Resolution::beat);
    }
};
}
