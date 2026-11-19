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
    playbackActive = false;
    stopAtZero = false;
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
        sourcePosition = 0.0;
        playbackActive = true;
        envelope.setCurrentAndTargetValue(0.0f);
        envelope.setTargetValue(1.0f);
        gain.setTargetValue(requestedGain.load(std::memory_order_relaxed));
    }
}

float SamplePlaybackEngine::readInterpolated(int channel, double position) const noexcept
{
    const auto& source = activeSample->audio();
    const auto lastFrame = source.getNumSamples() - 1;
    const auto first = juce::jlimit(0, lastFrame, static_cast<int>(position));
    const auto previous = juce::jmax(first - 1, 0);
    const auto second = juce::jmin(first + 1, lastFrame);
    const auto afterSecond = juce::jmin(first + 2, lastFrame);
    const auto fraction = static_cast<float>(position - static_cast<double>(first));
    const auto* data = source.getReadPointer(channel);
    // Four-point Catmull-Rom interpolation keeps pitch and duration correct when
    // the host and source rates differ, without needing audio-thread scratch buffers.
    const auto y0 = data[previous];
    const auto y1 = data[first];
    const auto y2 = data[second];
    const auto y3 = data[afterSecond];
    const auto halfY0 = 0.5f * y0;
    const auto halfY3 = 0.5f * y3;
    return y1 + fraction * ((0.5f * y2 - halfY0)
        + fraction * (((y0 + 2.0f * y2) - (halfY3 + 2.5f * y1)
        + fraction * ((halfY3 + 1.5f * y1) - (halfY0 + 1.5f * y2)))));
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
            {
                sourcePosition = 0.0;
                playbackActive = true;
                stopAtZero = false;
                endOfSampleFade = false;
                envelope.setCurrentAndTargetValue(0.0f);
                envelope.setTargetValue(1.0f);
                gain.setTargetValue(requestedGain.load(std::memory_order_relaxed));
            }
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
    const auto rampFrames = juce::jmax(1.0, outputRate * 0.005);
    gain.setTargetValue(requestedGain.load(std::memory_order_relaxed));

    for (int frame = 0; frame < outputFrames; ++frame)
    {
        if (playbackActive && !endOfSampleFade
            && (static_cast<double>(source.getNumSamples()) - sourcePosition) / step <= rampFrames)
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
            sourcePosition += step;
        }

        if (stopAtZero && !envelope.isSmoothing() && envelope.getCurrentValue() <= 0.00001f)
        {
            playbackActive = false;
            stopAtZero = false;
        }
        if (sourcePosition >= source.getNumSamples())
        {
            playbackActive = false;
            envelope.setCurrentAndTargetValue(0.0f);
        }
    }

    publishedPosition.store(sourcePosition / sourceRate, std::memory_order_relaxed);
    publishedPlaying.store(playbackActive || envelope.isSmoothing(), std::memory_order_relaxed);
}

void SamplePlaybackEngine::reclaimRetired() noexcept
{
    while (auto* retired = retiredSamples.pop())
        delete retired;
}
