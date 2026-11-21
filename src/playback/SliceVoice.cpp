#include "SliceVoice.h"
#include <algorithm>
#include <cmath>

bool SliceVoice::start(const SlicePlaybackSnapshot* snapshot, std::size_t regionIndex,
                       int midiNote, int velocity, double outputSampleRate,
                       std::uint64_t startFrame) noexcept
{
    if (snapshot == nullptr || regionIndex >= snapshot->regions().size()
        || midiNote < 0 || midiNote > 127 || velocity <= 0 || velocity > 127
        || !std::isfinite(outputSampleRate) || outputSampleRate <= 0.0)
        return false;

    const auto& sample = snapshot->sample();
    const auto& region = snapshot->regions()[regionIndex];
    if (region.startSample < 0 || region.endSample <= region.startSample
        || region.endSample > sample.audio().getNumSamples()
        || !std::isfinite(sample.metadata().sampleRate) || sample.metadata().sampleRate <= 0.0)
        return false;
    const auto semitones = juce::jlimit(-48, 48, midiNote - SlicePlaybackSnapshot::pitchReferenceNote);
    const auto pitchRatio = std::exp2(static_cast<double>(semitones) / 12.0);
    const auto step = sample.metadata().sampleRate / outputSampleRate * pitchRatio;
    if (!std::isfinite(step) || step <= 0.0)
        return false;

    sourceSnapshot = snapshot;
    sourceRegionIndex = regionIndex;
    sourcePosition = static_cast<double>(region.startSample);
    sourceStep = step;
    outputRate = outputSampleRate;
    velocityGain = static_cast<float>(velocity) / 127.0f;
    envelope = 0.0f;
    voiceMidiNote = midiNote;
    voiceStartFrame = startFrame;
    active = true;
    releaseStarted = false;

    const auto outputLength = static_cast<double>(region.endSample - region.startSample) / step;
    const auto attackFrames = juce::jmax(1, juce::roundToInt(
        std::min(outputRate * 0.003, outputLength * 0.2)));
    naturalReleaseFrames = std::max(1.0, std::min(outputRate * 0.005, outputLength * 0.2));
    beginRamp(1.0f, attackFrames);
    return true;
}

void SliceVoice::noteOff() noexcept
{
    if (!active)
        return;
    releaseStarted = true;
    beginRamp(0.0f, juce::jmax(1, juce::roundToInt(outputRate * 0.005)));
}

bool SliceVoice::renderFrame(float& left, float& right) noexcept
{
    left = 0.0f;
    right = 0.0f;
    if (!active || sourceSnapshot == nullptr)
        return false;

    const auto& sample = sourceSnapshot->sample();
    const auto& audio = sample.audio();
    const auto& region = sourceSnapshot->regions()[sourceRegionIndex];
    const auto remaining = (static_cast<double>(region.endSample) - sourcePosition) / sourceStep;
    if (remaining <= 0.0)
    {
        active = false;
        sourceSnapshot = nullptr;
        voiceMidiNote = -1;
        return false;
    }
    if (!releaseStarted && remaining <= naturalReleaseFrames)
    {
        releaseStarted = true;
        beginRamp(0.0f, juce::jmax(1, juce::roundToInt(remaining)));
    }

    const auto* first = audio.getReadPointer(0);
    const auto firstValue = resampler.interpolateRange(first, audio.getNumSamples(),
        static_cast<int>(region.startSample), static_cast<int>(region.endSample),
        sourcePosition, sourceStep);
    auto secondValue = firstValue;
    if (audio.getNumChannels() > 1)
        secondValue = resampler.interpolateRange(audio.getReadPointer(1), audio.getNumSamples(),
            static_cast<int>(region.startSample), static_cast<int>(region.endSample),
            sourcePosition, sourceStep);

    const auto amplitude = envelope * velocityGain;
    if (audio.getNumChannels() == 1)
    {
        left = firstValue * amplitude;
        right = left;
    }
    else
    {
        left = firstValue * amplitude;
        right = secondValue * amplitude;
    }

    sourcePosition += sourceStep;
    advanceRamp();
    const auto reachedEnd = sourcePosition >= static_cast<double>(region.endSample);
    const auto releasedToZero = releaseStarted && rampRemaining == 0 && rampTarget <= 0.0f;
    if (reachedEnd || releasedToZero)
    {
        active = false;
        sourceSnapshot = nullptr;
        voiceMidiNote = -1;
    }
    return true;
}

void SliceVoice::beginRamp(float target, int frames) noexcept
{
    rampTarget = target;
    rampRemaining = juce::jmax(1, frames);
    rampIncrement = (rampTarget - envelope) / static_cast<float>(rampRemaining);
}

void SliceVoice::advanceRamp() noexcept
{
    if (rampRemaining <= 0)
        return;
    envelope += rampIncrement;
    --rampRemaining;
    if (rampRemaining == 0)
        envelope = rampTarget;
}
