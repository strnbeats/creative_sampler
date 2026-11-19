#include "SampleStorage.h"

SampleStorage::SampleStorage(juce::AudioBuffer<float>&& audio, SampleMetadata metadata)
    : sampleAudio(std::move(audio)), sampleMetadata(std::move(metadata))
{
}
