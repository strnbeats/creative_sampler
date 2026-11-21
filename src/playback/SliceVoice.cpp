#include "SliceVoice.h"
#include <algorithm>
#include <cmath>

void SliceVoice::prepare(double sampleRate) noexcept
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
    (void) envelope.prepare(outputRate);
}

void SliceVoice::reset() noexcept
{
    active = false;
    sourceSnapshot = nullptr;
    voiceMidiNote = -1;
    sliceEndReleaseStarted = false;
}

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
        || !region.parameters.isValid()
        || !std::isfinite(sample.metadata().sampleRate) || sample.metadata().sampleRate <= 0.0)
        return false;
    if (envelope.preparedSampleRate() <= 0.0
        || std::abs(outputRate - outputSampleRate) > 0.001)
        prepare(outputSampleRate);
    if (!envelope.setParameters(region.parameters.envelope))
        return false;

    const auto semitones = juce::jlimit(-48.0, 48.0,
        static_cast<double>(midiNote - SlicePlaybackSnapshot::pitchReferenceNote)
            + static_cast<double>(region.parameters.pitchSemitones));
    const auto pitchRatio = std::exp2(semitones / 12.0);
    const auto step = sample.metadata().sampleRate / outputSampleRate * pitchRatio;
    if (!std::isfinite(step) || step <= 0.0)
        return false;

    sourceSnapshot = snapshot;
    sourceRegionIndex = regionIndex;
    sourcePosition = static_cast<double>(region.startSample);
    sourceStep = step;
    outputRate = outputSampleRate;
    velocityGain = static_cast<float>(velocity) / 127.0f;
    sliceVolume = region.parameters.volume;
    voiceMidiNote = midiNote;
    voiceStartFrame = startFrame;
    active = true;
    sliceEndReleaseStarted = false;

    constexpr auto halfPi = 1.57079632679489661923;
    const auto pan = static_cast<double>(region.parameters.pan);
    if (sample.audio().getNumChannels() == 1)
    {
        const auto angle = (pan + 1.0) * halfPi * 0.5;
        leftPanGain = static_cast<float>(std::cos(angle));
        rightPanGain = static_cast<float>(std::sin(angle));
    }
    else if (pan < 0.0)
    {
        leftPanGain = 1.0f;
        rightPanGain = static_cast<float>(std::cos(-pan * halfPi));
    }
    else
    {
        leftPanGain = static_cast<float>(std::cos(pan * halfPi));
        rightPanGain = 1.0f;
    }
    envelope.noteOn();
    return true;
}

void SliceVoice::noteOff() noexcept
{
    if (!active)
        return;
    envelope.noteOff();
}

bool SliceVoice::renderFrame(float& left, float& right) noexcept
{
    left = 0.0f;
    right = 0.0f;
    if (!active || sourceSnapshot == nullptr || !envelope.isActive())
    {
        reset();
        return false;
    }

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
    const auto endSafetyFrames = std::max(1.0, outputRate * 0.005);
    const auto beginSliceEndRelease = !sliceEndReleaseStarted && remaining <= endSafetyFrames;

    const auto* first = audio.getReadPointer(0);
    const auto firstValue = resampler.interpolateRange(first, audio.getNumSamples(),
        static_cast<int>(region.startSample), static_cast<int>(region.endSample),
        sourcePosition, sourceStep);
    auto secondValue = firstValue;
    if (audio.getNumChannels() > 1)
        secondValue = resampler.interpolateRange(audio.getReadPointer(1), audio.getNumSamples(),
            static_cast<int>(region.startSample), static_cast<int>(region.endSample),
            sourcePosition, sourceStep);

    const auto envelopeValue = envelope.nextSample();
    if (beginSliceEndRelease)
    {
        const auto boundedFrames = static_cast<std::uint64_t>(
            juce::jmax(1.0, std::ceil(remaining)));
        envelope.finishWithinFrames(boundedFrames > 0 ? boundedFrames - 1 : 0);
        sliceEndReleaseStarted = true;
    }
    const auto amplitude = envelopeValue * velocityGain * sliceVolume;
    if (audio.getNumChannels() == 1)
    {
        left = firstValue * amplitude * leftPanGain;
        right = firstValue * amplitude * rightPanGain;
    }
    else
    {
        left = firstValue * amplitude * leftPanGain;
        right = secondValue * amplitude * rightPanGain;
    }

    sourcePosition += sourceStep;
    const auto reachedEnd = sourcePosition >= static_cast<double>(region.endSample);
    if (reachedEnd || !envelope.isActive())
        reset();
    return true;
}
