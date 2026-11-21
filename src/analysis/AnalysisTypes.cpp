#include "AnalysisTypes.h"
#include <cmath>
#include <type_traits>

namespace
{
bool isConfidenceValid(float confidence) noexcept
{
    return std::isfinite(confidence) && confidence >= 0.0f && confidence <= 1.0f;
}

bool isPitchClassValid(PitchClass pitchClass) noexcept
{
    return static_cast<unsigned int>(pitchClass) <= static_cast<unsigned int>(PitchClass::b);
}

bool isScaleModeValid(ScaleMode mode) noexcept
{
    return mode == ScaleMode::major || mode == ScaleMode::minor;
}

bool isBpmValid(const BpmAnalysis& bpm) noexcept
{
    if (!std::isfinite(bpm.minimumBpm) || !std::isfinite(bpm.maximumBpm)
        || bpm.minimumBpm <= 0.0 || bpm.maximumBpm <= bpm.minimumBpm
        || !isConfidenceValid(bpm.confidence) || !isConfidenceValid(bpm.score))
        return false;
    for (const auto& candidate : bpm.alternatives)
        if (!std::isfinite(candidate.value) || candidate.value < bpm.minimumBpm
            || candidate.value > bpm.maximumBpm || !isConfidenceValid(candidate.confidence)
            || !isConfidenceValid(candidate.score))
            return false;

    if (bpm.manualOverride && (!std::isfinite(*bpm.manualOverride)
        || *bpm.manualOverride < bpm.minimumBpm || *bpm.manualOverride > bpm.maximumBpm))
        return false;

    switch (bpm.detectionStatus)
    {
        case BpmDetectionStatus::detected:
        case BpmDetectionStatus::lowConfidence:
            return bpm.value && std::isfinite(*bpm.value) && *bpm.value >= bpm.minimumBpm
                && *bpm.value <= bpm.maximumBpm;
        case BpmDetectionStatus::insufficientData:
        case BpmDetectionStatus::noRhythmicContent:
            return !bpm.value && !bpm.manualOverride && bpm.confidence == 0.0f
                && bpm.score == 0.0f && bpm.alternatives.empty() && !bpm.octaveAmbiguous;
    }
    return false;
}

bool isKeyValid(const KeyAnalysis& key) noexcept
{
    if (!isConfidenceValid(key.confidence) || !isConfidenceValid(key.score))
        return false;
    for (const auto& candidate : key.alternatives)
        if (!isPitchClassValid(candidate.tonic) || !isScaleModeValid(candidate.mode)
            || !isConfidenceValid(candidate.confidence) || !isConfidenceValid(candidate.score))
            return false;

    switch (key.detectionStatus)
    {
        case KeyDetectionStatus::detected:
            return key.tonic && key.mode && isPitchClassValid(*key.tonic)
                && isScaleModeValid(*key.mode) && key.confidence >= 0.35f;
        case KeyDetectionStatus::lowConfidence:
            return key.tonic && key.mode && isPitchClassValid(*key.tonic)
                && isScaleModeValid(*key.mode) && key.confidence < 0.35f;
        case KeyDetectionStatus::insufficientData:
            return !key.tonic && !key.mode && key.confidence == 0.0f && key.score == 0.0f
                && key.alternatives.empty() && !key.ambiguous;
    }
    return false;
}

bool isTransientValid(const Transient& transient, const TransientAnalysis& analysis) noexcept
{
    const auto durationSeconds = analysis.durationSeconds();
    return std::isfinite(transient.timeSeconds) && transient.timeSeconds >= 0.0
        && transient.timeSeconds < durationSeconds && isConfidenceValid(transient.strength)
        && transient.samplePosition >= 0 && transient.samplePosition < analysis.sampleCount
        && std::isfinite(transient.normalizedPosition) && transient.normalizedPosition >= 0.0
        && transient.normalizedPosition <= 1.0
        && std::abs(transient.timeSeconds - static_cast<double>(transient.samplePosition)
                                              / analysis.sampleRate) <= 1.0 / analysis.sampleRate
        && std::abs(transient.normalizedPosition
                    - static_cast<double>(transient.samplePosition)
                          / static_cast<double>(juce::jmax<juce::int64>(1, analysis.sampleCount - 1))) <= 1.0e-6
        && std::isfinite(transient.spectralFlux) && transient.spectralFlux >= 0.0f
        && transient.spectralFlux <= 1.0f
        && (!transient.confidence || isConfidenceValid(*transient.confidence));
}

bool isTransientAnalysisValid(const TransientAnalysis& analysis) noexcept
{
    if (!std::isfinite(analysis.sampleRate) || analysis.sampleRate <= 0.0
        || analysis.sampleCount < 0 || analysis.totalEventCount < analysis.positions.size())
        return false;
    if (analysis.sampleCount == 0)
        return analysis.detectionStatus == TransientDetectionStatus::insufficientData
            && analysis.positions.empty() && analysis.totalEventCount == 0;
    switch (analysis.detectionStatus)
    {
        case TransientDetectionStatus::insufficientData:
            return analysis.positions.empty() && analysis.totalEventCount == 0;
        case TransientDetectionStatus::detected:
            if (analysis.totalEventCount != analysis.positions.size())
                return false;
            break;
        case TransientDetectionStatus::truncated:
            if (analysis.totalEventCount <= analysis.positions.size())
                return false;
            break;
        default:
            return false;
    }
    juce::int64 previous = -1;
    for (const auto& transient : analysis.positions)
    {
        if (!isTransientValid(transient, analysis) || transient.samplePosition <= previous)
            return false;
        previous = transient.samplePosition;
    }
    return true;
}

bool hasAnyPayload(const AnalysisResult& result) noexcept
{
    return result.bpm.has_value() || result.key.has_value() || result.transients.has_value();
}

bool isResultDataValid(const AnalysisResult& result) noexcept
{
    if (result.bpm && !isBpmValid(*result.bpm))
        return false;
    if (result.key && !isKeyValid(*result.key))
        return false;
    if (result.transients && !isTransientAnalysisValid(*result.transients))
        return false;

    return true;
}
}

bool BpmAnalysis::isValid() const noexcept
{
    return isBpmValid(*this);
}

juce::String KeyAnalysis::fullName() const
{
    if (!tonic || !mode)
        return {};

    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto pitchIndex = static_cast<unsigned int>(*tonic);
    if (pitchIndex >= std::size(names))
        return {};
    return juce::String(names[pitchIndex]) + (*mode == ScaleMode::major ? " Major" : " Minor");
}

bool KeyAnalysis::isValid() const noexcept
{
    return isKeyValid(*this);
}

bool BpmAnalysis::setManualOverride(double bpm) noexcept
{
    if (!value || !std::isfinite(bpm) || bpm < minimumBpm || bpm > maximumBpm)
        return false;
    manualOverride = bpm;
    return true;
}

bool AnalysisResult::isValid() const noexcept
{
    const auto hasIdentity = identity.sampleId != 0 && identity.generation != 0;
    switch (status)
    {
        case AnalysisStatus::notAnalyzed:
            return !hasAnyPayload(*this) && error.isEmpty()
                && ((identity.sampleId == 0 && identity.generation == 0) || hasIdentity);
        case AnalysisStatus::running:
        case AnalysisStatus::cancelled:
            return hasIdentity && !hasAnyPayload(*this) && error.isEmpty();
        case AnalysisStatus::completed:
            return hasIdentity && hasAnyPayload(*this) && error.isEmpty() && isResultDataValid(*this);
        case AnalysisStatus::failed:
            return hasIdentity && !hasAnyPayload(*this) && error.isNotEmpty();
    }

    return false;
}

bool isValidAnalysisPayload(const AnalysisPayload& payload, double durationSeconds) noexcept
{
    return std::visit([durationSeconds](const auto& value)
    {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, BpmAnalysis>)
            return std::isfinite(durationSeconds) && durationSeconds > 0.0 && isBpmValid(value);
        else if constexpr (std::is_same_v<Value, KeyAnalysis>)
            return std::isfinite(durationSeconds) && durationSeconds > 0.0 && isKeyValid(value);
        else
        {
            return isTransientAnalysisValid(value)
                && (value.sampleCount == 0
                    || (std::isfinite(durationSeconds) && durationSeconds > 0.0
                        && std::abs(value.durationSeconds() - durationSeconds) <= 1.0 / value.sampleRate));
        }
    }, payload);
}
