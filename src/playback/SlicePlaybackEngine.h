#pragma once

#include "SlicePlaybackSnapshot.h"
#include "SliceVoice.h"
#include "../realtime/SpscPointerQueue.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// Fixed-capacity polyphonic slice renderer. Snapshot ownership is transferred on
// the control thread and reclaimed there; voices borrow immutable snapshot pointers.
class SlicePlaybackEngine final
{
public:
    static constexpr std::size_t maximumVoices = 16;
    static constexpr std::size_t retirementCapacity = 128;

    SlicePlaybackEngine() noexcept;
    ~SlicePlaybackEngine();

    void prepare(double outputSampleRate) noexcept;
    void setCurrentSampleId(std::uint64_t sampleId) noexcept;
    bool publishSnapshot(std::unique_ptr<const SlicePlaybackSnapshot> snapshot) noexcept;
    void setVolume(float volume) noexcept;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi,
                 bool baseSampleWasPlaying) noexcept;
    void stopVoices() noexcept;
    void reclaimRetired() noexcept;
    std::size_t activeVoiceCount() const noexcept { return lastPublishedVoiceCount.load(std::memory_order_relaxed); }

private:
    void acquirePendingSnapshot() noexcept;
    bool canRetire(const SlicePlaybackSnapshot* snapshot) const noexcept;
    bool retire(SlicePlaybackSnapshot* snapshot) noexcept;
    bool retainForVoices(SlicePlaybackSnapshot* snapshot) noexcept;
    void serviceRetirement() noexcept;
    void installPendingSnapshot() noexcept;
    void renderRange(juce::AudioBuffer<float>& output, int start, int end,
                     bool baseSampleWasPlaying) noexcept;
    void dispatchMidi(const juce::MidiMessage& message) noexcept;
    void startNote(int note, int velocity) noexcept;
    void releaseNote(int note) noexcept;
    void beginSampleTransition(std::uint64_t sampleId) noexcept;

    std::array<SliceVoice, maximumVoices> voices;
    std::array<SlicePlaybackSnapshot*, maximumVoices> voiceHeldSnapshots {};
    SpscPointerQueue<SlicePlaybackSnapshot, retirementCapacity> retiredSnapshots;
    std::atomic<SlicePlaybackSnapshot*> pendingSnapshot { nullptr };
    std::atomic<std::uint64_t> requestedSampleId { 0 };
    std::atomic<std::uint64_t> requestedRevision { 0 };
    std::atomic<float> requestedVolume { 1.0f };
    std::atomic<bool> stopRequested { false };
    SlicePlaybackSnapshot* activeSnapshot = nullptr;
    SlicePlaybackSnapshot* pendingInstall = nullptr;
    double outputRate = 44100.0;
    float volume = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> volumeSmoother;
    std::uint64_t audioFrame = 0;
    std::atomic<std::size_t> lastPublishedVoiceCount { 0 };
};
