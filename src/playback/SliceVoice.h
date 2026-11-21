#pragma once

#include "SlicePlaybackSnapshot.h"
#include "../dsp/BandlimitedResampler.h"
#include <cstdint>

// Fixed-state polyphonic voice. The snapshot pointer is borrowed; the owning engine
// keeps that snapshot alive until this voice has stopped using it.
class SliceVoice final
{
public:
    bool start(const SlicePlaybackSnapshot* snapshot, std::size_t regionIndex,
               int midiNote, int velocity, double outputSampleRate,
               std::uint64_t startFrame) noexcept;
    void noteOff() noexcept;
    bool renderFrame(float& left, float& right) noexcept;

    bool isActive() const noexcept { return active; }
    bool usesSnapshot(const SlicePlaybackSnapshot* snapshot) const noexcept
    {
        return active && sourceSnapshot == snapshot;
    }
    int assignedNote() const noexcept { return voiceMidiNote; }
    std::uint64_t startFrame() const noexcept { return voiceStartFrame; }

private:
    void beginRamp(float target, int frames) noexcept;
    void advanceRamp() noexcept;

    const SlicePlaybackSnapshot* sourceSnapshot = nullptr;
    std::size_t sourceRegionIndex = 0;
    double sourcePosition = 0.0;
    double sourceStep = 1.0;
    double outputRate = 44100.0;
    double naturalReleaseFrames = 1.0;
    float velocityGain = 0.0f;
    float envelope = 0.0f;
    float rampTarget = 0.0f;
    float rampIncrement = 0.0f;
    int rampRemaining = 0;
    int voiceMidiNote = -1;
    std::uint64_t voiceStartFrame = 0;
    bool active = false;
    bool releaseStarted = false;
    BandlimitedResampler resampler;
};
