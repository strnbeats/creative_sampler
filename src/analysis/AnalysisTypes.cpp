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
    if (!std::isfinite(bpm.value) || bpm.value <= 0.0 || !isConfidenceValid(bpm.confidence))
        return false;
    for (const auto& candidate : bpm.alternatives)
        if (!std::isfinite(candidate.value) || candidate.value <= 0.0
            || !isConfidenceValid(candidate.confidence))
            return false;
    return true;
}

bool isKeyValid(const KeyAnalysis& key) noexcept
{
    if (!isPitchClassValid(key.tonic) || !isScaleModeValid(key.mode)
        || !isConfidenceValid(key.confidence))
        return false;
    for (const auto& candidate : key.alternatives)
        if (!isPitchClassValid(candidate.tonic) || !isScaleModeValid(candidate.mode)
            || !isConfidenceValid(candidate.confidence))
            return false;
    return true;
}

bool isTransientValid(const Transient& transient, double durationSeconds) noexcept
{
    return std::isfinite(transient.timeSeconds) && transient.timeSeconds >= 0.0
        && transient.timeSeconds < durationSeconds && isConfidenceValid(transient.strength)
        && (!transient.confidence || isConfidenceValid(*transient.confidence));
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
    if (result.transients)
        for (const auto& transient : result.transients->positions)
            if (!std::isfinite(transient.timeSeconds) || transient.timeSeconds < 0.0
                || !isConfidenceValid(transient.strength)
                || (transient.confidence && !isConfidenceValid(*transient.confidence)))
                return false;

    return true;
}
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
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0)
        return false;

    return std::visit([durationSeconds](const auto& value)
    {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, BpmAnalysis>)
            return isBpmValid(value);
        else if constexpr (std::is_same_v<Value, KeyAnalysis>)
            return isKeyValid(value);
        else
        {
            for (const auto& transient : value.positions)
                if (!isTransientValid(transient, durationSeconds))
                    return false;
            return true;
        }
    }, payload);
}
