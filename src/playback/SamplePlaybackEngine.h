#pragma once

#include "../dsp/BandlimitedResampler.h"
#include "../realtime/SpscPointerQueue.h"
#include "../sample/SampleStorage.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

class SamplePlaybackEngine final
{
public:
    SamplePlaybackEngine();
    ~SamplePlaybackEngine();

    // Called on the message thread. Ownership remains here unless accepted.
    void submitSample(std::unique_ptr<SampleStorage> sample) noexcept;
    // Copies/moves the shared handle on the caller thread; process() only sees an owner envelope pointer.
    bool submitSharedSample(SampleHandle sample) noexcept;
    void prepare(double outputSampleRate) noexcept;
    void play() noexcept;
    void stop() noexcept;
    void setVolume(float linearGain) noexcept;

    // Audio-thread only. The output buffer is cleared and then filled from the source.
    void process(juce::AudioBuffer<float>& output) noexcept;
    // Message-thread only; drains deferred retirements and deletes their samples here.
    void reclaimRetired() noexcept;

    double positionSeconds() const noexcept { return publishedPosition.load(std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return publishedPlaying.load(std::memory_order_relaxed); }

private:
    struct SampleOwner final
    {
        SampleHandle sample;
    };
    static_assert(std::atomic<SampleOwner*>::is_always_lock_free,
                  "The sample handoff requires lock-free pointer atomics on the target platform.");

    void installPendingIfPossible() noexcept;
    void startPlayback() noexcept;
    void beginFadeOut() noexcept;
    float readInterpolated(int channel, double position) const noexcept;

    std::atomic<SampleOwner*> pendingSample { nullptr };
    SpscPointerQueue<SampleOwner, 128> retiredSamples;

    // The following members are owned exclusively by the audio thread after prepare().
    SampleOwner* activeSample = nullptr;
    SampleOwner* pendingInstall = nullptr;
    double outputRate = 44100.0;
    double sourcePosition = 0.0;
    double fadeFrames = 220.0;
    BandlimitedResampler resampler;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> envelope;
    std::atomic<float> requestedGain { 1.0f };
    std::atomic<juce::uint64> transportCommand { 0 };
    juce::uint64 observedCommand = 0;
    bool requestedPlaying = false;
    bool playAfterInstall = false;
    bool playbackActive = false;
    bool stopAtZero = false;
    bool endOfSampleFade = false;
    std::atomic<double> publishedPosition { 0.0 };
    std::atomic<bool> publishedPlaying { false };
};
