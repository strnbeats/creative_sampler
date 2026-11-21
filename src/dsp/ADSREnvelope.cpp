#include "ADSREnvelope.h"
#include <algorithm>
#include <cmath>
#include <limits>

bool ADSREnvelope::prepare(double requestedSampleRate) noexcept
{
    if (!std::isfinite(requestedSampleRate) || requestedSampleRate <= 0.0)
        return false;
    sampleRate = requestedSampleRate;
    becomeIdle();
    return true;
}

bool ADSREnvelope::setParameters(const ADSRParameters& parameters) noexcept
{
    if (!parameters.isValid())
        return false;
    settings = parameters;
    return true;
}

void ADSREnvelope::noteOn() noexcept
{
    currentLevel = 0.0f;
    const auto attackFrames = framesFor(settings.attackSeconds);
    if (attackFrames == 0)
    {
        currentLevel = 1.0f;
        beginDecayOrSustain();
    }
    else
    {
        begin(Stage::attack, 1.0f, attackFrames);
    }
}

void ADSREnvelope::noteOff() noexcept
{
    begin(Stage::release, 0.0f, framesFor(settings.releaseSeconds));
}

void ADSREnvelope::finishWithinFrames(std::uint64_t frames) noexcept
{
    begin(Stage::release, 0.0f, frames);
}

float ADSREnvelope::nextSample() noexcept
{
    if (currentStage != Stage::idle && currentStage != Stage::sustain)
        advance();
    if (!std::isfinite(currentLevel))
        becomeIdle();
    return currentLevel;
}

std::uint64_t ADSREnvelope::framesFor(float seconds) const noexcept
{
    if (seconds <= 0.0f || sampleRate <= 0.0)
        return 0;
    const auto frames = static_cast<double>(seconds) * sampleRate;
    if (!std::isfinite(frames)
        || frames >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        return std::numeric_limits<std::uint64_t>::max();
    const auto roundedFrames = std::floor(frames + 0.5);
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(roundedFrames));
}

void ADSREnvelope::begin(Stage newStage, float target, std::uint64_t frames) noexcept
{
    if (frames == 0 || (currentLevel <= 0.0f && target <= 0.0f))
    {
        if (target <= 0.0f)
            becomeIdle();
        else
        {
            currentLevel = target;
            currentStage = Stage::sustain;
            framesRemaining = 0;
        }
        return;
    }

    currentStage = newStage;
    segmentTarget = target;
    framesRemaining = frames;
    increment = (segmentTarget - currentLevel) / static_cast<float>(frames);
}

void ADSREnvelope::beginDecayOrSustain() noexcept
{
    const auto decayFrames = framesFor(settings.decaySeconds);
    if (decayFrames == 0)
    {
        currentLevel = settings.sustainLevel;
        currentStage = Stage::sustain;
        framesRemaining = 0;
        increment = 0.0f;
    }
    else
    {
        begin(Stage::decay, settings.sustainLevel, decayFrames);
    }
}

void ADSREnvelope::advance() noexcept
{
    if (framesRemaining == 0)
    {
        if (currentStage == Stage::attack)
        {
            currentLevel = 1.0f;
            beginDecayOrSustain();
        }
        else if (currentStage == Stage::decay)
        {
            currentLevel = settings.sustainLevel;
            currentStage = Stage::sustain;
            increment = 0.0f;
        }
        else
        {
            becomeIdle();
        }
        return;
    }

    currentLevel += increment;
    --framesRemaining;
    if (framesRemaining != 0)
        return;

    currentLevel = segmentTarget;
    if (currentStage == Stage::attack)
        beginDecayOrSustain();
    else if (currentStage == Stage::decay)
    {
        currentStage = Stage::sustain;
        increment = 0.0f;
    }
    else
        becomeIdle();
}

void ADSREnvelope::becomeIdle() noexcept
{
    currentLevel = 0.0f;
    segmentTarget = 0.0f;
    increment = 0.0f;
    framesRemaining = 0;
    currentStage = Stage::idle;
}
