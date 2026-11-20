#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>

struct SampleMetadata
{
    juce::String fileName;
    juce::String filePath;
    double sampleRate = 0.0;
    int channelCount = 0;
    int bitDepth = 0;
    juce::int64 sampleCount = 0;

    double durationSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double>(sampleCount) / sampleRate : 0.0;
    }
};

// An immutable, shared source asset. Keep UI summaries and playback cursors elsewhere.
class SampleStorage final
{
public:
    SampleStorage(juce::AudioBuffer<float>&& audio, SampleMetadata metadata);

    const SampleMetadata& metadata() const noexcept { return sampleMetadata; }
    const juce::AudioBuffer<float>& audio() const noexcept { return sampleAudio; }

private:
    juce::AudioBuffer<float> sampleAudio;
    SampleMetadata sampleMetadata;
};

using SampleHandle = std::shared_ptr<const SampleStorage>;
