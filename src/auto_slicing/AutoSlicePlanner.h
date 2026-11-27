#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

struct AnalysisResult;
class AnalysisCancellationToken;

namespace auto_slicing
{
using Frame = std::int64_t;

struct Source
{
    std::uint64_t sampleId = 0;
    std::uint64_t sampleRevision = 0;
    std::uint64_t analysisGeneration = 0;
    Frame totalFrames = 0;
    double sampleRate = 0.0;

    bool operator==(const Source&) const noexcept;
};

struct Config
{
    static constexpr std::size_t maximumAutoSlices = 32;

    // The default matches the current Slice Engine. A future caller supplies its actual limit.
    Frame engineMinimumFrames = 64;
    // Starting point for percussive material. Convert to source frames, rounding up.
    double minimumSliceSeconds = 0.060;
    Frame minimumBoundaryDistanceFrames = 0;
    float sensitivity = 0.5f;
    float minimumSignificance = 0.35f;
    std::size_t maximumSlices = maximumAutoSlices;

    bool operator==(const Config&) const noexcept;
};

enum class Strategy { transient, wholeSample };
enum class BoundaryOrigin { sourceEdge, transient };
enum class FallbackReason
{
    none, noTransients, weakTransients, noFeasibleSplit, shortSource,
    sliceLimit, analysisFailed, truncatedAnalysis
};

struct Boundary
{
    Frame frame = 0;
    BoundaryOrigin origin = BoundaryOrigin::sourceEdge;

    bool operator==(const Boundary&) const = default;
};

// A slice is the half-open interval [start.frame, end.frame).
struct Slice
{
    Boundary start;
    Boundary end;
};

struct Diagnostics
{
    FallbackReason fallback = FallbackReason::none;
    std::size_t reportedEvents = 0;
    std::size_t eligibleEvents = 0;
    std::size_t retainedCandidates = 0;
    bool belowEngineMinimum = false;

    bool operator==(const Diagnostics&) const = default;
};

// Value type. Publish as const after construction. Slice intervals are derived from
// adjacent boundaries so their shared edge and provenance cannot diverge.
struct Plan
{
    static constexpr unsigned currentAlgorithmVersion = 1;

    unsigned algorithmVersion = currentAlgorithmVersion;
    Source source;
    Config config;
    Strategy strategy = Strategy::wholeSample;
    std::vector<Boundary> boundaries;
    Diagnostics diagnostics;

    std::size_t sliceCount() const noexcept { return boundaries.empty() ? 0 : boundaries.size() - 1; }
    std::optional<Slice> sliceAt(std::size_t index) const noexcept;

    bool operator==(const Plan&) const = default;
};

enum class ErrorKind { input, structure, configuration, cancelled };
struct Error
{
    ErrorKind kind;
    std::string_view message;
};

struct Result
{
    std::optional<Plan> plan;
    std::optional<Error> error;
};

// Check against the source and policy the caller currently expects, including revisions.
std::optional<Error> validatePlan(const Plan& plan, const Source& expectedSource,
                                  const Config& expectedConfig = {}) noexcept;

// Synchronous worker-side computation. Reads the existing native result without copying its
// transients or consulting engine state. Cancelled work returns no plan.
Result makePlan(const Source& source, const AnalysisResult& analysis,
                const Config& config = {}, const AnalysisCancellationToken* cancellation = nullptr);
}
