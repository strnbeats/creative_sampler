#pragma once

#include "../sample/SampleStorage.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

using AnalysisSampleId = std::uint64_t;

struct AnalysisIdentity
{
    AnalysisSampleId sampleId = 0;
    std::uint64_t generation = 0;

    bool operator==(const AnalysisIdentity&) const noexcept = default;
};

enum class AnalysisStatus
{
    notAnalyzed,
    running,
    completed,
    failed,
    cancelled
};

enum class AnalyzerKind
{
    bpm,
    key,
    transients
};

struct BpmCandidate
{
    double value = 0.0;
    float confidence = 0.0f;
};

struct BpmAnalysis
{
    double value = 0.0;
    float confidence = 0.0f;
    std::vector<BpmCandidate> alternatives;
};

enum class PitchClass
{
    c, cSharp, d, dSharp, e, f, fSharp, g, gSharp, a, aSharp, b
};

enum class ScaleMode
{
    major,
    minor
};

struct KeyCandidate
{
    PitchClass tonic = PitchClass::c;
    ScaleMode mode = ScaleMode::major;
    float confidence = 0.0f;
};

struct KeyAnalysis
{
    PitchClass tonic = PitchClass::c;
    ScaleMode mode = ScaleMode::major;
    float confidence = 0.0f;
    std::vector<KeyCandidate> alternatives;
};

struct Transient
{
    double timeSeconds = 0.0;
    float strength = 0.0f;
    std::optional<float> confidence;
};

struct TransientAnalysis
{
    std::vector<Transient> positions;
};

using AnalysisPayload = std::variant<BpmAnalysis, KeyAnalysis, TransientAnalysis>;

struct AnalysisResult
{
    AnalysisIdentity identity;
    AnalysisStatus status = AnalysisStatus::notAnalyzed;
    std::optional<BpmAnalysis> bpm;
    std::optional<KeyAnalysis> key;
    std::optional<TransientAnalysis> transients;
    juce::String error;

    bool isValid() const noexcept;
};

class AnalysisCancellationToken final
{
public:
    void cancel() noexcept { cancellationRequested.store(true, std::memory_order_release); }
    bool isCancellationRequested() const noexcept
    {
        return cancellationRequested.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> cancellationRequested { false };
};

struct AnalyzerOutput
{
    std::optional<AnalysisPayload> payload;
    juce::String error;

    static AnalyzerOutput success(AnalysisPayload value)
    {
        return { std::move(value), {} };
    }

    static AnalyzerOutput failure(juce::String message)
    {
        return { std::nullopt, std::move(message) };
    }
};

class Analyzer
{
public:
    virtual ~Analyzer() = default;
    virtual AnalyzerKind kind() const noexcept = 0;
    virtual AnalyzerOutput analyze(const SampleStorage& sample,
                                   const AnalysisCancellationToken& cancellation) const = 0;
};

bool isValidAnalysisPayload(const AnalysisPayload& payload, double durationSeconds) noexcept;
