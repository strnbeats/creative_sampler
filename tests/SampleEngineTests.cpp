#include "../src/import/AudioImportService.h"
#include "../src/playback/SamplePlaybackEngine.h"
#include "../src/waveform/WaveformData.h"
#include "SampleEngineTests.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cmath>
#include <thread>

namespace
{
juce::File makeTemporaryFile(const juce::String& extension)
{
    const auto unique = juce::String(juce::Time::getHighResolutionTicks());
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("creative_sampler_test_" + unique + extension);
}

bool writeAudioFile(const juce::File& file, bool useAiff, int channels, int frames,
                    double sampleRate = 48000.0)
{
    juce::AudioBuffer<float> audio(channels, frames);
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            audio.setSample(channel, frame, 0.4f * std::sin(juce::MathConstants<float>::twoPi
                * 440.0f * static_cast<float>(frame / sampleRate)));

    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(sampleRate).withNumChannels(channels).withBitsPerSample(24);
    std::unique_ptr<juce::AudioFormatWriter> writer = useAiff
        ? juce::AiffAudioFormat().createWriterFor(stream, options)
        : juce::WavAudioFormat().createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, frames);
}

std::unique_ptr<SampleStorage> makeSample(int channels, int frames, double sampleRate,
                                          float value = 0.5f)
{
    juce::AudioBuffer<float> buffer(channels, frames);
    buffer.clear();
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            buffer.setSample(channel, frame, value);
    SampleMetadata metadata;
    metadata.fileName = "memory.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = channels;
    metadata.sampleCount = frames;
    return std::make_unique<SampleStorage>(std::move(buffer), std::move(metadata));
}

std::unique_ptr<SampleStorage> makeSineSample(double sampleRate, double frequency)
{
    const auto frames = static_cast<int>(sampleRate);
    juce::AudioBuffer<float> buffer(1, frames);
    for (int frame = 0; frame < frames; ++frame)
        buffer.setSample(0, frame, static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi
            * frequency * static_cast<double>(frame) / sampleRate)));
    SampleMetadata metadata;
    metadata.fileName = "sine.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = 1;
    metadata.sampleCount = frames;
    return std::make_unique<SampleStorage>(std::move(buffer), std::move(metadata));
}

std::unique_ptr<SampleStorage> makeStereoSample(int frames, double sampleRate)
{
    juce::AudioBuffer<float> buffer(2, frames);
    for (int frame = 0; frame < frames; ++frame)
    {
        buffer.setSample(0, frame, 0.25f);
        buffer.setSample(1, frame, -0.5f);
    }
    SampleMetadata metadata;
    metadata.fileName = "stereo.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = 2;
    metadata.sampleCount = frames;
    return std::make_unique<SampleStorage>(std::move(buffer), std::move(metadata));
}

class SampleEngineTests final : public juce::UnitTest
{
public:
    SampleEngineTests() : UnitTest("Creative Sampler Sample Engine", "Sample Engine") {}

    void runTest() override
    {
        beginTest("WAV and AIFF import produce immutable metadata and waveform data");
        for (const auto useAiff : { false, true })
        {
            const auto file = makeTemporaryFile(useAiff ? ".aiff" : ".wav");
            expect(writeAudioFile(file, useAiff, 2, 2048));
            AudioImporter importer;
            auto result = importer.load(file);
            expect(result.succeeded(), result.error);
            if (result.succeeded())
            {
                expectEquals(result.sample->metadata().channelCount, 2);
                expectEquals(static_cast<int>(result.sample->metadata().sampleCount), 2048);
                expectWithinAbsoluteError(result.sample->metadata().sampleRate, 48000.0, 0.01);
                expect(result.waveform != nullptr);
                expectEquals(result.waveform->channelCount(), 2);
                expect(result.waveform->levelCount() > 1);
            }
            file.deleteFile();
        }

        beginTest("Invalid, empty, missing, and unsupported files return errors without throwing");
        AudioImporter importer;
        const auto missing = makeTemporaryFile(".wav");
        expect(!importer.load(missing).succeeded());
        const auto unsupported = makeTemporaryFile(".mp3");
        expect(unsupported.replaceWithText("not an audio file"));
        expect(!importer.load(unsupported).succeeded());
        unsupported.deleteFile();
        const auto damaged = makeTemporaryFile(".wav");
        expect(damaged.replaceWithText("damaged wav"));
        expect(!importer.load(damaged).succeeded());
        damaged.deleteFile();
        const auto empty = makeTemporaryFile(".wav");
        expect(empty.create());
        const auto emptyResult = importer.load(empty);
        expect(!emptyResult.succeeded());
        expect(emptyResult.error.containsIgnoreCase("empty"));
        empty.deleteFile();
        std::atomic<int> cancellationChecks { 0 };
        const auto cancelFile = makeTemporaryFile(".wav");
        expect(writeAudioFile(cancelFile, false, 1, 4096));
        const auto cancelled = importer.load(cancelFile, [&cancellationChecks]
        {
            return cancellationChecks.fetch_add(1, std::memory_order_relaxed) >= 3;
        });
        expect(!cancelled.succeeded());
        cancelFile.deleteFile();

        beginTest("Import service can be shut down safely with a queued or active import");
        const auto shutdownFile = makeTemporaryFile(".wav");
        expect(writeAudioFile(shutdownFile, false, 2, 131072));
        std::atomic<int> callbacksAfterShutdown { 0 };
        {
            AudioImportService service([&callbacksAfterShutdown](AudioImportResult)
            {
                callbacksAfterShutdown.fetch_add(1, std::memory_order_relaxed);
            });
            service.loadAsync(shutdownFile);
            service.shutdown();
        }
        expectEquals(callbacksAfterShutdown.load(std::memory_order_relaxed), 0);
        shutdownFile.deleteFile();

        beginTest("Waveform peaks preserve mono and stereo signal ranges");
        {
            juce::AudioBuffer<float> mono(1, 8);
            mono.clear();
            mono.setSample(0, 2, -0.75f);
            mono.setSample(0, 4, 0.9f);
            SampleMetadata metadata;
            metadata.sampleRate = 44100.0;
            metadata.channelCount = 1;
            metadata.sampleCount = 8;
            SampleStorage sample(std::move(mono), metadata);
            const auto waveform = WaveformData::generate(sample, 2);
            expectEquals(waveform.channelCount(), 1);
            expectEquals(static_cast<int>(waveform.level(0).channels[0].size()), 4);
            expectWithinAbsoluteError(waveform.level(0).channels[0][1].minimum, -0.75f, 0.0001f);
            expectWithinAbsoluteError(waveform.level(0).channels[0][2].maximum, 0.9f, 0.0001f);
            expect(waveform.levelCount() >= 3);
        }
        {
            auto stereo = makeSample(2, 129, 48000.0);
            const auto waveform = WaveformData::generate(*stereo);
            expectEquals(waveform.channelCount(), 2);
            expectEquals(static_cast<int>(waveform.level(0).channels[0].size()), 3);
            expectEquals(static_cast<int>(waveform.level(0).channels[1].size()), 3);
        }
        {
            juce::AudioBuffer<float> emptyAudio(1, 0);
            SampleMetadata metadata;
            metadata.channelCount = 1;
            SampleStorage emptySample(std::move(emptyAudio), metadata);
            const auto waveform = WaveformData::generate(emptySample);
            expectEquals(waveform.channelCount(), 1);
            expectEquals(waveform.levelCount(), 0);
        }

        beginTest("Playback converts source rates and supports mono and stereo output");
        for (const auto sourceRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            for (const auto outputRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                auto sample = makeSineSample(sourceRate, 440.0);
                SamplePlaybackEngine engine;
                engine.prepare(outputRate);
                engine.submitSample(std::move(sample));
                juce::AudioBuffer<float> output(2, 512);
                engine.process(output); // Installs pending source on the audio thread.
                engine.play();
                auto nonZero = false;
                auto crossings = 0;
                auto previous = 0.0f;
                for (int block = 0; block < 40; ++block)
                {
                    engine.process(output);
                    for (int frame = 0; frame < output.getNumSamples(); ++frame)
                    {
                        const auto value = output.getSample(0, frame);
                        nonZero = nonZero || std::abs(value) > 0.0001f;
                        if (previous <= 0.0f && value > 0.0f)
                            ++crossings;
                        previous = value;
                    }
                }
                expect(nonZero);
                expectWithinAbsoluteError(engine.positionSeconds(), 40.0 * 512.0 / outputRate, 0.01);
                expectWithinAbsoluteError(static_cast<double>(crossings),
                                          440.0 * 40.0 * 512.0 / outputRate, 2.0);
                expect(engine.isPlaying());
                engine.reclaimRetired();
            }
        }
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeStereoSample(4096, 48000.0));
            juce::AudioBuffer<float> stereoOutput(2, 512);
            engine.process(stereoOutput);
            engine.play();
            engine.process(stereoOutput);
            expectWithinAbsoluteError(stereoOutput.getSample(0, 400), 0.25f, 0.002f);
            expectWithinAbsoluteError(stereoOutput.getSample(1, 400), -0.5f, 0.002f);
            engine.stop();
            engine.process(stereoOutput);
            engine.reclaimRetired();
        }
        {
            auto sample = makeSample(2, 4096, 48000.0, 0.25f);
            sample->audio().getNumChannels(); // Compile-time const access is part of the storage API.
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(std::move(sample));
            juce::AudioBuffer<float> monoOutput(1, 512);
            engine.process(monoOutput);
            engine.play();
            engine.process(monoOutput);
            expect(std::abs(monoOutput.getSample(0, 100)) > 0.0001f);
            engine.stop();
            juce::AudioBuffer<float> shortBlock(1, 64);
            engine.process(shortBlock);
            expect(engine.isPlaying()); // Stop is a short fade, not a hard cut.
            engine.process(monoOutput);
            expect(!engine.isPlaying());
            engine.reclaimRetired();
        }
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.setVolume(0.25f);
            engine.submitSample(makeSample(1, 48000, 48000.0, 0.8f));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);
            engine.play();
            engine.process(block);
            expectWithinAbsoluteError(block.getSample(0, 400), 0.2f, 0.002f);
            engine.setVolume(0.5f);
            engine.process(block);
            expectWithinAbsoluteError(block.getSample(0, 400), 0.4f, 0.002f);
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 2400, 48000.0));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);
            engine.play();
            for (int i = 0; i < 8; ++i)
                engine.process(block);
            expect(!engine.isPlaying());
            expectWithinAbsoluteError(engine.positionSeconds(), 0.05, 0.002);
            engine.reclaimRetired();
        }
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 48000, 48000.0, 0.4f));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);
            engine.play();
            engine.process(block);
            engine.submitSample(makeSample(1, 48000, 48000.0, -0.4f));
            for (int i = 0; i < 3; ++i)
                engine.process(block);
            expect(!engine.isPlaying());
            engine.play();
            engine.process(block);
            expect(block.getSample(0, 350) < -0.1f);
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }

        beginTest("Fast sample replacement is safe while audio processing continues");
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(2, 48000, 48000.0));
            juce::AudioBuffer<float> block(2, 128);
            engine.process(block);
            engine.play();

            std::atomic<bool> finished { false };
            std::thread audio([&]
            {
                juce::AudioBuffer<float> audioBlock(2, 128);
                for (int i = 0; i < 1000; ++i)
                    engine.process(audioBlock);
                finished.store(true, std::memory_order_release);
            });
            for (int i = 0; i < 100; ++i)
            {
                engine.submitSample(makeSample(1 + (i % 2), 4096 + i, 44100.0));
                engine.reclaimRetired();
            }
            audio.join();
            expect(finished.load(std::memory_order_acquire));
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }
    }
};

SampleEngineTests sampleEngineTests;
}

juce::UnitTest* getSampleEngineTest()
{
    return &sampleEngineTests;
}
