#pragma once

#include "../sample/SampleStorage.h"
#include "../waveform/WaveformData.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

struct AudioImportResult
{
    std::unique_ptr<SampleStorage> sample;
    std::shared_ptr<const WaveformData> waveform;
    juce::String error;

    bool succeeded() const noexcept { return sample != nullptr && error.isEmpty(); }
};

class AudioImporter final
{
public:
    AudioImportResult load(const juce::File& file,
                           const std::function<bool()>& shouldCancel = {}) const;
};

// Serial background importer. Results are delivered on JUCE's message thread.
class AudioImportService final : private juce::AsyncUpdater
{
public:
    using ResultCallback = std::function<void(AudioImportResult)>;

    explicit AudioImportService(ResultCallback callback);
    ~AudioImportService() override;

    void loadAsync(const juce::File& file);
    void shutdown();

private:
    class ImportJob;
    void publishResult(juce::uint64 generation, AudioImportResult result);
    void handleAsyncUpdate() override;

    juce::ThreadPool workers { 1 };
    ResultCallback resultCallback;
    juce::CriticalSection resultLock;
    AudioImportResult pendingResult;
    juce::uint64 pendingResultGeneration = 0;
    std::atomic<juce::uint64> latestGeneration { 0 };
    std::atomic<bool> shuttingDown { false };
};
