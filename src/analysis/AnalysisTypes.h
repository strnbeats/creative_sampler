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

struct AnalysisResultStamp
{
    AnalysisIdentity identity;
    AnalysisStatus status = AnalysisStatus::notAnalyzed;

    bool operator==(const AnalysisResultStamp&) const noexcept = default;
};

enum class AnalyzerKind
{
    bpm,
    key,
    transients
};

enum class BpmDetectionStatus
{
    detected,
    lowConfidence,
    insufficientData,
    noRhythmicContent
};

struct BpmCandidate
{
    double value = 0.0;
    // Heuristic candidate confidence; score is normalized periodicity support.
    float confidence = 0.0f;
    float score = 0.0f;
};

struct BpmAnalysis
{
    BpmAnalysis() = default;
    BpmAnalysis(double estimatedBpm, float estimatedConfidence,
                std::vector<BpmCandidate> alternativeCandidates = {})
        : value(estimatedBpm), confidence(estimatedConfidence),
          alternatives(std::move(alternativeCandidates))
    {
    }

    // Empty for insufficient/no-rhythm results. Confidence is a reliability heuristic,
    // not a calibrated probability; score is the candidate's normalized autocorrelation support.
    std::optional<double> value;
    float confidence = 0.0f;
    std::vector<BpmCandidate> alternatives;
    float score = 0.0f;
    BpmDetectionStatus detectionStatus = BpmDetectionStatus::detected;
    bool octaveAmbiguous = false;
    double minimumBpm = 50.0;
    double maximumBpm = 220.0;
    std::optional<double> manualOverride;

    std::optional<double> effectiveValue() const noexcept
    {
        return manualOverride.has_value() ? manualOverride : value;
    }

    bool setManualOverride(double bpm) noexcept;
    bool isValid() const noexcept;
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

enum class KeyDetectionStatus
{
    detected,
    lowConfidence,
    insufficientData
};

struct KeyCandidate
{
    PitchClass tonic = PitchClass::c;
    ScaleMode mode = ScaleMode::major;
    float confidence = 0.0f;
    float score = 0.0f;
};

struct KeyAnalysis
{
    KeyAnalysis() = default;
    KeyAnalysis(PitchClass detectedTonic, ScaleMode detectedMode, float detectedConfidence,
                std::vector<KeyCandidate> candidateAlternatives = {})
        : tonic(detectedTonic), mode(detectedMode), confidence(detectedConfidence),
          alternatives(std::move(candidateAlternatives)),
          detectionStatus(detectedConfidence < 0.35f ? KeyDetectionStatus::lowConfidence
                                                     : KeyDetectionStatus::detected)
    {
    }

    std::optional<PitchClass> tonic;
    std::optional<ScaleMode> mode;
    float confidence = 0.0f;
    std::vector<KeyCandidate> alternatives;
    float score = 0.0f;
    KeyDetectionStatus detectionStatus = KeyDetectionStatus::insufficientData;
    bool ambiguous = false;

    juce::String fullName() const;
    bool isValid() const noexcept;
};

struct Transient
{
    double timeSeconds = 0.0;
    float strength = 0.0f;
    std::optional<float> confidence;
    juce::int64 samplePosition = -1;
    double normalizedPosition = -1.0;
    float spectralFlux = 0.0f;
};

enum class TransientDetectionStatus { detected, insufficientData, truncated };

struct TransientAnalysis
{
    std::vector<Transient> positions;
    double sampleRate = 0.0;
    juce::int64 sampleCount = 0;
    size_t totalEventCount = 0;
    TransientDetectionStatus detectionStatus = TransientDetectionStatus::insufficientData;

    double durationSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(sampleCount) / sampleRate : 0.0;
    }
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
