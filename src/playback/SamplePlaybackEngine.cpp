#include "SamplePlaybackEngine.h"
#include <algorithm>
#include <cmath>

static_assert(std::atomic<SampleStorage*>::is_always_lock_free,
              "The sample handoff requires lock-free pointer atomics on the target platform.");
static_assert(std::atomic<juce::uint64>::is_always_lock_free
              && std::atomic<double>::is_always_lock_free
              && std::atomic<float>::is_always_lock_free
              && std::atomic<size_t>::is_always_lock_free,
              "Realtime transport state requires lock-free atomics on the target platform.");

SamplePlaybackEngine::SamplePlaybackEngine()
{
    gain.reset(44100.0, 0.005);
    gain.setCurrentAndTargetValue(0.0f);
    envelope.reset(44100.0, 0.005);
    envelope.setCurrentAndTargetValue(0.0f);
}

SamplePlaybackEngine::~SamplePlaybackEngine()
{
    // The host must have stopped processBlock before destroying an AudioProcessor.
    delete pendingSample.exchange(nullptr, std::memory_order_acq_rel);
    delete pendingInstall;
    delete activeSample;
    reclaimRetired();
}

void SamplePlaybackEngine::submitSample(std::unique_ptr<SampleStorage> sample) noexcept
{
    if (sample == nullptr)
        return;
    auto* incoming = sample.release();
    auto* displaced = pendingSample.exchange(incoming, std::memory_order_acq_rel);
    // A displaced pending pointer was never acquired by the audio thread.
    delete displaced;
}

void SamplePlaybackEngine::prepare(double sampleRate) noexcept
{
    outputRate = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    gain.reset(outputRate, 0.005);
    gain.setCurrentAndTargetValue(requestedGain.load(std::memory_order_relaxed));
    envelope.reset(outputRate, 0.005);
    envelope.setCurrentAndTargetValue(0.0f);
    sourcePosition = 0.0;
    fadeFrames = outputRate * 0.005;
    playbackActive = false;
    stopAtZero = false;
    playAfterInstall = false;
    requestedPlaying = false;
    publishedPlaying.store(false, std::memory_order_relaxed);
}

void SamplePlaybackEngine::play() noexcept
{
    auto current = transportCommand.load(std::memory_order_relaxed);
    juce::uint64 desired;
    do
    {
        desired = (((current >> 1) + 1) << 1) | juce::uint64 { 1 };
    }
    while (!transportCommand.compare_exchange_weak(current, desired, std::memory_order_release,
                                                    std::memory_order_relaxed));
}

void SamplePlaybackEngine::stop() noexcept
{
    auto current = transportCommand.load(std::memory_order_relaxed);
    juce::uint64 desired;
    do
    {
        desired = (((current >> 1) + 1) << 1);
    }
    while (!transportCommand.compare_exchange_weak(current, desired, std::memory_order_release,
                                                    std::memory_order_relaxed));
}

void SamplePlaybackEngine::setVolume(float linearGain) noexcept
{
    requestedGain.store(juce::jlimit(0.0f, 1.0f, linearGain), std::memory_order_relaxed);
}

void SamplePlaybackEngine::beginFadeOut() noexcept
{
    envelope.setTargetValue(0.0f);
    stopAtZero = true;
}

void SamplePlaybackEngine::startPlayback() noexcept
{
    if (activeSample == nullptr)
        return;

    const auto sampleFramesAtHostRate = static_cast<double>(activeSample->audio().getNumSamples())
        * outputRate / activeSample->metadata().sampleRate;
    fadeFrames = juce::jmax(1.0, juce::jmin(outputRate * 0.005, sampleFramesAtHostRate * 0.2));
    envelope.reset(outputRate, fadeFrames / outputRate);
    sourcePosition = 0.0;
    playbackActive = true;
    stopAtZero = false;
    endOfSampleFade = false;
    envelope.setCurrentAndTargetValue(0.0f);
    envelope.setTargetValue(1.0f);
    gain.setTargetValue(requestedGain.load(std::memory_order_relaxed));
}

void SamplePlaybackEngine::installPendingIfPossible() noexcept
{
    if (pendingInstall == nullptr)
        pendingInstall = pendingSample.exchange(nullptr, std::memory_order_acquire);
    if (pendingInstall == nullptr)
        return;

    if (activeSample != nullptr && (playbackActive || envelope.isSmoothing()
                                    || envelope.getCurrentValue() > 0.00001f))
    {
        beginFadeOut();
        return;
    }
    if (activeSample != nullptr && !retiredSamples.push(activeSample))
        return;

    activeSample = pendingInstall;
    pendingInstall = nullptr;
    sourcePosition = 0.0;
    playbackActive = false;
    stopAtZero = false;
    endOfSampleFade = false;
    envelope.setCurrentAndTargetValue(0.0f);
    publishedPosition.store(0.0, std::memory_order_relaxed);
    publishedPlaying.store(false, std::memory_order_relaxed);
    if (playAfterInstall)
    {
        playAfterInstall = false;
        startPlayback();
    }
}

float SamplePlaybackEngine::readInterpolated(int channel, double position) const noexcept
{
    const auto& source = activeSample->audio();
    return resampler.interpolate(source.getReadPointer(channel), source.getNumSamples(), position,
                                 activeSample->metadata().sampleRate / outputRate);
}

void SamplePlaybackEngine::process(juce::AudioBuffer<float>& output) noexcept
{
    output.clear();

    const auto command = transportCommand.load(std::memory_order_acquire);
    if (command != observedCommand)
    {
        observedCommand = command;
        requestedPlaying = (command & 1u) != 0;
        if (requestedPlaying)
        {
            if (activeSample != nullptr && pendingInstall == nullptr
                && pendingSample.load(std::memory_order_relaxed) == nullptr)
                startPlayback();
            else
            {
                playAfterInstall = true;
            }
        }
        else if (!requestedPlaying)
        {
            playAfterInstall = false;
            beginFadeOut();
        }
    }

    installPendingIfPossible();
    if (pendingInstall != nullptr)
        beginFadeOut();

    if (activeSample == nullptr || output.getNumChannels() == 0 || output.getNumSamples() == 0)
    {
        publishedPlaying.store(false, std::memory_order_relaxed);
        return;
    }

    const auto& source = activeSample->audio();
    const auto sourceRate = activeSample->metadata().sampleRate;
    const auto step = sourceRate / outputRate;
    if (!std::isfinite(step) || step <= 0.0)
        return;

    const auto outputChannels = output.getNumChannels();
    const auto sourceChannels = source.getNumChannels();
    const auto outputFrames = output.getNumSamples();
    gain.setTargetValue(requestedGain.load(std::memory_order_relaxed));

    for (int frame = 0; frame < outputFrames; ++frame)
    {
        if (playbackActive && !endOfSampleFade
            && (static_cast<double>(source.getNumSamples()) - sourcePosition) / step <= fadeFrames)
        {
            endOfSampleFade = true;
            beginFadeOut();
        }

        const auto hasSourceFrame = playbackActive && sourcePosition < source.getNumSamples();
        const auto currentGain = gain.getNextValue() * envelope.getNextValue();
        if (hasSourceFrame)
        {
            if (outputChannels == 1)
            {
                auto value = readInterpolated(0, sourcePosition);
                if (sourceChannels == 2)
                    value = 0.5f * (value + readInterpolated(1, sourcePosition));
                output.addSample(0, frame, value * currentGain);
            }
            else
            {
                for (int channel = 0; channel < outputChannels; ++channel)
                {
                    const auto sourceChannel = sourceChannels == 1 ? 0 : juce::jmin(channel, sourceChannels - 1);
                    output.addSample(channel, frame, readInterpolated(sourceChannel, sourcePosition) * currentGain);
                }
            }
            sourcePosition = juce::jmin(sourcePosition + step,
                                        static_cast<double>(source.getNumSamples()));
        }

        if (stopAtZero && !envelope.isSmoothing() && envelope.getCurrentValue() <= 0.00001f)
        {
            playbackActive = false;
            stopAtZero = false;
        }
        if (sourcePosition >= source.getNumSamples())
        {
            playbackActive = false;
        }
    }

    publishedPosition.store(juce::jmin(sourcePosition / sourceRate,
                                       static_cast<double>(source.getNumSamples()) / sourceRate),
                            std::memory_order_relaxed);
    publishedPlaying.store(playbackActive || envelope.isSmoothing(), std::memory_order_relaxed);
}

void SamplePlaybackEngine::reclaimRetired() noexcept
{
    while (auto* retired = retiredSamples.pop())
        delete retired;
}
