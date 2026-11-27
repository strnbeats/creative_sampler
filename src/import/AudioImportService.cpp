#include "AudioImportService.h"
#include <limits>
#include <new>

namespace
{
bool formatManagerSupportsExtension(const juce::AudioFormatManager& formats,
                                    const juce::String& extension)
{
    for (int index = 0; index < formats.getNumKnownFormats(); ++index)
        for (auto knownExtension : formats.getKnownFormat(index)->getFileExtensions())
        {
            if (knownExtension.startsWithChar('.'))
                knownExtension = knownExtension.substring(1);
            if (knownExtension.equalsIgnoreCase(extension))
                return true;
        }
    return false;
}
}

AudioImportResult AudioImporter::load(const juce::File& file,
                                      const std::function<bool()>& shouldCancel) const
{
    AudioImportResult result;
    if (shouldCancel && shouldCancel())
        return result;
    if (!file.existsAsFile())
    {
        result.error = "File does not exist.";
        return result;
    }
    if (file.getSize() <= 0)
    {
        result.error = "The audio file is empty.";
        return result;
    }
    const auto isCaf = file.hasFileExtension("caf");
    if (!isCaf && !file.hasFileExtension("wav;aif;aiff"))
    {
        result.error = "Unsupported audio format. Choose a WAV or AIFF file, or a CAF file on macOS.";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    if (isCaf && !formatManagerSupportsExtension(formats, "caf"))
    {
        result.error = "CAF files are not supported by the audio formats available on this platform. Choose WAV or AIFF.";
        return result;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (shouldCancel && shouldCancel())
        return result;
    if (reader == nullptr)
    {
        result.error = isCaf
            ? "The CAF file is damaged or uses an audio codec that this system cannot decode."
            : "The audio file is damaged or could not be decoded.";
        return result;
    }
    if ((reader->numChannels != 1 && reader->numChannels != 2)
        || reader->lengthInSamples <= 0
        || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        result.error = reader->lengthInSamples <= 0 ? "The audio file is empty."
                                                    : "Only non-empty mono or stereo audio is supported.";
        return result;
    }

    try
    {
        const auto frames = static_cast<int>(reader->lengthInSamples);
        const auto decodedBytes = static_cast<juce::uint64>(reader->lengthInSamples)
            * static_cast<juce::uint64>(reader->numChannels) * sizeof(float);
        constexpr juce::uint64 maximumDecodedBytes = juce::uint64 { 2 } * 1024 * 1024 * 1024;
        if (decodedBytes > maximumDecodedBytes
            || decodedBytes > static_cast<juce::uint64>(std::numeric_limits<size_t>::max()))
        {
            result.error = "The decoded sample would exceed the 2 GiB in-memory limit.";
            return result;
        }
        juce::AudioBuffer<float> decoded(static_cast<int>(reader->numChannels), frames);
        decoded.clear();
        constexpr int readChunkSize = 65536;
        for (int offset = 0; offset < frames;)
        {
            if (shouldCancel && shouldCancel())
                return result;
            const auto count = juce::jmin(readChunkSize, frames - offset);
            if (!reader->read(&decoded, offset, count, offset, true, true))
            {
                result.error = isCaf ? "The CAF audio data could not be decoded."
                                     : "An error occurred while reading the audio data.";
                return result;
            }
            offset += count;
        }

        for (int channel = 0; channel < decoded.getNumChannels(); ++channel)
        {
            auto* data = decoded.getWritePointer(channel);
            for (int frame = 0; frame < frames; ++frame)
            {
                if ((frame % readChunkSize) == 0 && shouldCancel && shouldCancel())
                    return result;
                if (!std::isfinite(data[frame]))
                {
                    result.error = "The file contains invalid audio samples.";
                    return result;
                }
            }
        }

        SampleMetadata metadata;
        metadata.fileName = file.getFileName();
        metadata.filePath = file.getFullPathName();
        metadata.sampleRate = reader->sampleRate;
        metadata.channelCount = static_cast<int>(reader->numChannels);
        metadata.bitDepth = static_cast<int>(reader->bitsPerSample);
        metadata.sampleCount = reader->lengthInSamples;

        auto prepared = std::make_unique<SampleStorage>(std::move(decoded), std::move(metadata));
        auto generatedWaveform = WaveformData::generate(*prepared, 64, shouldCancel);
        if (shouldCancel && shouldCancel())
            return result;
        auto waveformData = std::make_shared<WaveformData>(std::move(generatedWaveform));
        result.sample = std::move(prepared);
        result.waveform = std::move(waveformData);
    }
    catch (const std::bad_alloc&)
    {
        result.error = "There is not enough memory to load this audio file.";
    }
    catch (...)
    {
        result.error = "The audio file could not be loaded.";
    }
    return result;
}

class AudioImportService::ImportJob final : public juce::ThreadPoolJob
{
public:
    ImportJob(AudioImportService& service, juce::File sourceFile, juce::uint64 requestGeneration)
        : ThreadPoolJob("Creative Sampler audio import"), owner(service), file(std::move(sourceFile)),
          generation(requestGeneration)
    {
    }

    JobStatus runJob() override
    {
        if (shouldExit())
            return jobHasFinished;
        AudioImporter importer;
        auto result = importer.load(file, [this] { return shouldExit(); });
        if (!shouldExit())
            owner.publishResult(generation, std::move(result));
        return jobHasFinished;
    }

private:
    AudioImportService& owner;
    juce::File file;
    juce::uint64 generation;
};

AudioImportService::AudioImportService(ResultCallback callback)
    : resultCallback(std::move(callback))
{
}

AudioImportService::~AudioImportService()
{
    shutdown();
}

void AudioImportService::loadAsync(const juce::File& file)
{
    const juce::ScopedLock lock(resultLock);
    if (shuttingDown.load(std::memory_order_acquire))
        return;
    const auto generation = latestGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    workers.addJob(new ImportJob(*this, file, generation), true);
}

void AudioImportService::publishResult(juce::uint64 generation, AudioImportResult result)
{
    if (shuttingDown.load(std::memory_order_acquire)
        || generation != latestGeneration.load(std::memory_order_acquire))
        return;
    {
        const juce::ScopedLock lock(resultLock);
        if (generation != latestGeneration.load(std::memory_order_relaxed))
            return;
        pendingResult = std::move(result);
        pendingResultGeneration = generation;
    }
    triggerAsyncUpdate();
}

void AudioImportService::handleAsyncUpdate()
{
    AudioImportResult result;
    {
        const juce::ScopedLock lock(resultLock);
        if (pendingResultGeneration != latestGeneration.load(std::memory_order_relaxed))
        {
            pendingResult = {};
            return;
        }
        result = std::move(pendingResult);
    }
    const juce::ScopedLock callbackGuard(callbackLock);
    if (!shuttingDown.load(std::memory_order_acquire) && resultCallback)
        resultCallback(std::move(result));
}

void AudioImportService::shutdown()
{
    {
        const juce::ScopedLock callbackGuard(callbackLock);
        if (shuttingDown.load(std::memory_order_relaxed))
            return;
        shuttingDown.store(true, std::memory_order_release);
        resultCallback = {};
    }
    {
        // Wait for a loadAsync() call that passed its shutdown check to finish
        // adding its job before removeAllJobs() takes its snapshot.
        const juce::ScopedLock loadGuard(resultLock);
    }
    latestGeneration.fetch_add(1, std::memory_order_relaxed);
    workers.removeAllJobs(true, -1);
    cancelPendingUpdate();
    const juce::ScopedLock lock(resultLock);
    pendingResult = {};
    pendingResultGeneration = 0;
}
