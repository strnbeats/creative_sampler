#pragma once

#include <algorithm>
#include <cmath>

namespace
{
inline bool nearlyEqual(float first, float second) noexcept
{
    const auto scale = std::max({ 1.0f, std::abs(first), std::abs(second) });
    return std::abs(first - second) <= 1.0e-7f * scale;
}
}

struct ADSRParameters
{
    float attackSeconds = 0.005f;
    float decaySeconds = 0.1f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.1f;

    bool isValid() const noexcept
    {
        return std::isfinite(attackSeconds) && attackSeconds >= 0.0f && attackSeconds <= 10.0f
            && std::isfinite(decaySeconds) && decaySeconds >= 0.0f && decaySeconds <= 10.0f
            && std::isfinite(sustainLevel) && sustainLevel >= 0.0f && sustainLevel <= 1.0f
            && std::isfinite(releaseSeconds) && releaseSeconds >= 0.0f && releaseSeconds <= 10.0f;
    }

    bool operator==(const ADSRParameters& other) const noexcept
    {
        return nearlyEqual(attackSeconds, other.attackSeconds)
            && nearlyEqual(decaySeconds, other.decaySeconds)
            && nearlyEqual(sustainLevel, other.sustainLevel)
            && nearlyEqual(releaseSeconds, other.releaseSeconds);
    }
};

struct SliceParameters
{
    float volume = 1.0f;
    float pan = 0.0f;
    float pitchSemitones = 0.0f;
    ADSRParameters envelope {};

    bool isValid() const noexcept
    {
        return std::isfinite(volume) && volume >= 0.0f && volume <= 2.0f
            && std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f
            && std::isfinite(pitchSemitones) && pitchSemitones >= -24.0f
            && pitchSemitones <= 24.0f && envelope.isValid();
    }

    bool operator==(const SliceParameters& other) const noexcept
    {
        return nearlyEqual(volume, other.volume) && nearlyEqual(pan, other.pan)
            && nearlyEqual(pitchSemitones, other.pitchSemitones)
            && envelope == other.envelope;
    }
};
