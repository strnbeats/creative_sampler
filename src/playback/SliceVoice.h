#pragma once

#include "SlicePlaybackSnapshot.h"
#include "../dsp/ADSREnvelope.h"
#include "../dsp/BandlimitedResampler.h"
#include <cstdint>

// Fixed-state polyphonic voice. The snapshot pointer is borrowed; the owning engine
// keeps that snapshot alive until this voice has stopped using it.
class SliceVoice final
{
public:
    void prepare(double outputSampleRate) noexcept;
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
    void reset() noexcept;

private:
    const SlicePlaybackSnapshot* sourceSnapshot = nullptr;
    std::size_t sourceRegionIndex = 0;
    double sourcePosition = 0.0;
    double sourceStep = 1.0;
    double outputRate = 44100.0;
    float velocityGain = 0.0f;
    float sliceVolume = 1.0f;
    float leftPanGain = 1.0f;
    float rightPanGain = 1.0f;
    int voiceMidiNote = -1;
    std::uint64_t voiceStartFrame = 0;
    bool active = false;
    bool sliceEndReleaseStarted = false;
    ADSREnvelope envelope;
    BandlimitedResampler resampler;
};
