#include "SlicePlaybackEngine.h"
#include <algorithm>
#include <cmath>
#include <new>

static_assert(std::atomic<SlicePlaybackSnapshot*>::is_always_lock_free,
              "Slice snapshot handoff must use lock-free pointer atomics.");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Slice identity handoff must use lock-free 64-bit atomics.");

SlicePlaybackEngine::SlicePlaybackEngine() noexcept
{
    volumeSmoother.reset(outputRate, 0.005);
    volumeSmoother.setCurrentAndTargetValue(1.0f);
}

SlicePlaybackEngine::~SlicePlaybackEngine()
{
    delete pendingSnapshot.exchange(nullptr, std::memory_order_acq_rel);
    delete pendingInstall;
    delete activeSnapshot;
    for (auto* snapshot : voiceHeldSnapshots)
        delete snapshot;
    while (auto* snapshot = retiredSnapshots.pop())
        delete snapshot;
}

void SlicePlaybackEngine::prepare(double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        outputRate = sampleRate;
    for (auto& voice : voices)
        voice.prepare(outputRate);
    volumeSmoother.reset(outputRate, 0.005);
    volumeSmoother.setCurrentAndTargetValue(requestedVolume.load(std::memory_order_relaxed));
    stopRequested.store(false, std::memory_order_relaxed);
    lastPublishedVoiceCount.store(0, std::memory_order_relaxed);
}

void SlicePlaybackEngine::setCurrentSampleId(std::uint64_t sampleId) noexcept
{
    requestedSampleId.store(sampleId, std::memory_order_release);
    requestedRevision.store(0, std::memory_order_release);
}

bool SlicePlaybackEngine::publishSnapshot(
    std::unique_ptr<const SlicePlaybackSnapshot> snapshot) noexcept
{
    if (snapshot == nullptr || !snapshot->isValid())
        return false;

    auto* raw = const_cast<SlicePlaybackSnapshot*>(snapshot.get());
    auto expected = pendingSnapshot.load(std::memory_order_acquire);
    for (;;)
    {
        const auto latestId = requestedSampleId.load(std::memory_order_acquire);
        const auto latestRevision = requestedRevision.load(std::memory_order_acquire);
        if ((latestId != 0 && snapshot->sampleId() != latestId)
            || (snapshot->sampleId() == latestId && snapshot->revision() < latestRevision))
            return false;

        requestedSampleId.store(snapshot->sampleId(), std::memory_order_release);
        requestedRevision.store(snapshot->revision(), std::memory_order_release);
        if (pendingSnapshot.compare_exchange_weak(expected, raw,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            snapshot.release();
            delete expected; // Any replaced pointer was still in the pending slot.
            return true;
        }
    }
}

void SlicePlaybackEngine::setVolume(float newVolume) noexcept
{
    requestedVolume.store(juce::jlimit(0.0f, 1.0f, newVolume), std::memory_order_relaxed);
}

void SlicePlaybackEngine::process(juce::AudioBuffer<float>& output,
                                  const juce::MidiBuffer& midi,
                                  bool baseSampleWasPlaying) noexcept
{
    volume = requestedVolume.load(std::memory_order_relaxed);
    volumeSmoother.setTargetValue(volume);
    if (output.getNumChannels() <= 0 || output.getNumSamples() <= 0)
        return;
    if (stopRequested.exchange(false, std::memory_order_acq_rel))
        for (auto& voice : voices)
            voice.noteOff();
    const auto sampleId = requestedSampleId.load(std::memory_order_acquire);
    if (activeSnapshot != nullptr && activeSnapshot->sampleId() != sampleId)
        beginSampleTransition(sampleId);

    acquirePendingSnapshot();
    serviceRetirement();
    installPendingSnapshot();

    const auto frames = output.getNumSamples();
    int cursor = 0;
    for (const auto metadata : midi)
    {
        const auto eventPosition = juce::jlimit(cursor, frames, metadata.samplePosition);
        renderRange(output, cursor, eventPosition, baseSampleWasPlaying);
        dispatchMidi(metadata.getMessage());
        cursor = eventPosition;
    }
    renderRange(output, cursor, frames, baseSampleWasPlaying);
    audioFrame += static_cast<std::uint64_t>(frames);

    std::size_t activeCount = 0;
    for (const auto& voice : voices)
        activeCount += voice.isActive() ? 1u : 0u;
    lastPublishedVoiceCount.store(activeCount, std::memory_order_relaxed);

    serviceRetirement();
    installPendingSnapshot();
}

void SlicePlaybackEngine::stopVoices() noexcept
{
    stopRequested.store(true, std::memory_order_release);
}

void SlicePlaybackEngine::reclaimRetired() noexcept
{
    while (auto* snapshot = retiredSnapshots.pop())
        delete snapshot;
}

void SlicePlaybackEngine::acquirePendingSnapshot() noexcept
{
    if (pendingInstall == nullptr)
        pendingInstall = pendingSnapshot.exchange(nullptr, std::memory_order_acq_rel);
}

bool SlicePlaybackEngine::canRetire(const SlicePlaybackSnapshot* snapshot) const noexcept
{
    for (const auto& voice : voices)
        if (voice.usesSnapshot(snapshot))
            return false;
    return true;
}

bool SlicePlaybackEngine::retire(SlicePlaybackSnapshot* snapshot) noexcept
{
    if (snapshot == nullptr)
        return true;
    return retiredSnapshots.push(snapshot);
}

bool SlicePlaybackEngine::retainForVoices(SlicePlaybackSnapshot* snapshot) noexcept
{
    if (snapshot == nullptr)
        return true;
    if (canRetire(snapshot))
        return retire(snapshot);
    for (auto& held : voiceHeldSnapshots)
    {
        if (held == nullptr)
        {
            held = snapshot;
            return true;
        }
    }
    return false;
}

void SlicePlaybackEngine::serviceRetirement() noexcept
{
    for (auto& held : voiceHeldSnapshots)
    {
        if (held != nullptr && canRetire(held) && retire(held))
        {
            held = nullptr;
        }
    }
}

void SlicePlaybackEngine::installPendingSnapshot() noexcept
{
    if (pendingInstall == nullptr)
        return;
    const auto wantedId = requestedSampleId.load(std::memory_order_acquire);
    const auto wantedRevision = requestedRevision.load(std::memory_order_acquire);
    if (pendingInstall->sampleId() != wantedId || pendingInstall->revision() != wantedRevision)
    {
        if (retire(pendingInstall))
            pendingInstall = nullptr;
        return;
    }

    if (activeSnapshot != nullptr && !retainForVoices(activeSnapshot))
        return; // Backpressure: retain both owners until the control thread drains the queue.

    activeSnapshot = pendingInstall;
    pendingInstall = nullptr;
}

void SlicePlaybackEngine::renderRange(juce::AudioBuffer<float>& output, int start, int end,
                                      bool baseSampleWasPlaying) noexcept
{
    if (start >= end)
        return;
    for (int frame = start; frame < end; ++frame)
    {
        const auto smoothedVolume = volumeSmoother.getNextValue();
        float sumLeft = 0.0f;
        float sumRight = 0.0f;
        std::size_t voiceLayers = 0;
        for (auto& voice : voices)
        {
            float left = 0.0f;
            float right = 0.0f;
            if (voice.renderFrame(left, right))
            {
                sumLeft += left;
                sumRight += right;
                ++voiceLayers;
            }
        }

        if (voiceLayers == 0)
            continue;
        const auto baseLayers = baseSampleWasPlaying ? 1u : 0u;
        const auto scale = 1.0f / static_cast<float>(voiceLayers + baseLayers);
        auto left = sumLeft * (scale * smoothedVolume);
        auto right = sumRight * (scale * smoothedVolume);
        if (baseLayers != 0)
        {
            left += output.getSample(0, frame) * scale;
            right += (output.getNumChannels() > 1 ? output.getSample(1, frame)
                                                   : output.getSample(0, frame)) * scale;
        }
        if (output.getNumChannels() == 1)
            output.setSample(0, frame, 0.5f * (left + right));
        else
        {
            output.setSample(0, frame, left);
            output.setSample(1, frame, right);
        }
    }
}

void SlicePlaybackEngine::dispatchMidi(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        startNote(message.getNoteNumber(), message.getVelocity());
    else if (message.isNoteOff())
        releaseNote(message.getNoteNumber());
}

void SlicePlaybackEngine::startNote(int note, int velocity) noexcept
{
    if (activeSnapshot == nullptr
        || activeSnapshot->sampleId() != requestedSampleId.load(std::memory_order_acquire)
        || activeSnapshot->revision() != requestedRevision.load(std::memory_order_acquire))
        return;
    const auto& regions = activeSnapshot->regionsForNote(note);
    for (const auto regionIndex : regions)
    {
        std::size_t slot = maximumVoices;
        for (std::size_t i = 0; i < voices.size(); ++i)
            if (!voices[i].isActive())
            {
                slot = i;
                break;
            }
        if (slot == maximumVoices)
        {
            slot = 0;
            for (std::size_t i = 1; i < voices.size(); ++i)
                if (voices[i].startFrame() < voices[slot].startFrame())
                    slot = i;
        }
        voices[slot].noteOff();
        // Reuse the slot immediately: the old voice has no owning reference, and
        // its snapshot remains protected by the engine's active/held owners.
        (void) voices[slot].start(activeSnapshot, regionIndex, note, velocity,
                                  outputRate, audioFrame);
    }
}

void SlicePlaybackEngine::releaseNote(int note) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.assignedNote() == note)
            voice.noteOff();
}

void SlicePlaybackEngine::beginSampleTransition(std::uint64_t sampleId) noexcept
{
    (void) sampleId;
    for (auto& voice : voices)
        if (voice.isActive() && voice.usesSnapshot(activeSnapshot))
            voice.noteOff();
}
