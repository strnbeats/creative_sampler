#include "../src/import/AudioImportService.h"
#include "../src/playback/SamplePlaybackEngine.h"
#include "../src/waveform/WaveformData.h"
#include "SampleEngineTests.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
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

bool writeCafLpcmFile(const juce::File& file, int frames, juce::uint32 formatId = 0x6c70636d)
{
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    const auto writeTag = [&stream](const char* tag) { stream->write(tag, 4); };
    const auto writeBigEndian = [&stream](juce::uint64 value, int bytes)
    {
        for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
            stream->writeByte(static_cast<char>((value >> shift) & 0xff));
    };
    const auto writeLittleEndian = [&stream](juce::uint16 value)
    {
        stream->writeByte(static_cast<char>(value & 0xff));
        stream->writeByte(static_cast<char>((value >> 8) & 0xff));
    };

    writeTag("caff");
    writeBigEndian(1, 2); // CAF version
    writeBigEndian(0, 2); // flags
    writeTag("desc");
    writeBigEndian(32, 8);
    double rate = 48000.0;
    juce::uint64 rateBits = 0;
    std::memcpy(&rateBits, &rate, sizeof(rateBits));
    writeBigEndian(rateBits, 8);
    writeBigEndian(formatId, 4);
    writeBigEndian(12, 4); // signed, packed, little-endian LPCM
    writeBigEndian(2, 4);  // bytes per packet
    writeBigEndian(1, 4);  // frames per packet
    writeBigEndian(1, 4);  // channels
    writeBigEndian(16, 4); // bits per channel
    writeTag("data");
    writeBigEndian(static_cast<juce::uint64>(4 + frames * 2), 8);
    writeBigEndian(0, 4); // edit count
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto sample = static_cast<std::int16_t>(std::sin(frame * 0.071) * 16000.0);
        writeLittleEndian(static_cast<juce::uint16>(sample));
    }
    stream->flush();
    return stream->getPosition() == 68 + static_cast<juce::int64>(frames) * 2;
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

std::unique_ptr<SampleStorage> makeSineSample(double sampleRate, double frequency,
                                             float amplitude = 0.5f)
{
    const auto frames = static_cast<int>(sampleRate);
    juce::AudioBuffer<float> buffer(1, frames);
    for (int frame = 0; frame < frames; ++frame)
        buffer.setSample(0, frame, amplitude * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
            * frequency * static_cast<double>(frame) / sampleRate)));
    SampleMetadata metadata;
    metadata.fileName = "sine.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = 1;
    metadata.sampleCount = frames;
    return std::make_unique<SampleStorage>(std::move(buffer), std::move(metadata));
}

std::unique_ptr<SampleStorage> makeSweepSample(double sampleRate, double startFrequency,
                                              double endFrequency, float amplitude = 0.5f)
{
    const auto frames = static_cast<int>(sampleRate);
    juce::AudioBuffer<float> buffer(1, frames);
    const auto slope = endFrequency - startFrequency;
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto time = static_cast<double>(frame) / sampleRate;
        const auto phase = juce::MathConstants<double>::twoPi
            * (startFrequency * time + 0.5 * slope * time * time);
        buffer.setSample(0, frame, amplitude * static_cast<float>(std::sin(phase)));
    }
    SampleMetadata metadata;
    metadata.fileName = "sweep.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = 1;
    metadata.sampleCount = frames;
    return std::make_unique<SampleStorage>(std::move(buffer), std::move(metadata));
}

double measureRms(std::unique_ptr<SampleStorage> sample, double hostRate,
                  double startSeconds, double endSeconds, int outputChannel = 0)
{
    SamplePlaybackEngine engine;
    engine.prepare(hostRate);
    engine.submitSample(std::move(sample));
    constexpr int blockSize = 256;
    juce::AudioBuffer<float> block(2, blockSize);
    engine.process(block);
    engine.play();
    const auto endFrame = static_cast<juce::int64>(std::ceil(endSeconds * hostRate));
    auto squares = 0.0;
    auto count = juce::int64 { 0 };
    for (juce::int64 blockStart = 0; blockStart < endFrame; blockStart += blockSize)
    {
        engine.process(block);
        for (int frame = 0; frame < blockSize; ++frame)
        {
            const auto absoluteFrame = blockStart + frame;
            const auto time = static_cast<double>(absoluteFrame) / hostRate;
            if (time >= startSeconds && time < endSeconds)
            {
                const auto value = block.getSample(outputChannel, frame);
                squares += static_cast<double>(value) * value;
                ++count;
            }
        }
    }
    engine.stop();
    engine.reclaimRetired();
    return count > 0 ? std::sqrt(squares / static_cast<double>(count)) : 0.0;
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

        beginTest("CAF LPCM uses the shared importer when JUCE has a CAF reader");
        {
            const auto caf = makeTemporaryFile(".caf");
            expect(writeCafLpcmFile(caf, 2048));
            AudioImporter cafImporter;
            auto result = cafImporter.load(caf);
#if JUCE_MAC || JUCE_IOS
            expect(result.succeeded(), result.error);
            if (result.succeeded())
            {
                expectEquals(result.sample->metadata().channelCount, 1);
                expectEquals(static_cast<int>(result.sample->metadata().sampleCount), 2048);
                expectWithinAbsoluteError(result.sample->metadata().sampleRate, 48000.0, 0.01);
                expect(result.waveform != nullptr);
            }
#else
            expect(!result.succeeded());
            expect(result.error.containsIgnoreCase("CAF files are not supported"), result.error);
#endif
            caf.deleteFile();
        }

        beginTest("Damaged and unsupported-codec CAF files report a useful error");
        {
            AudioImporter cafImporter;
            const auto damagedCaf = makeTemporaryFile(".caf");
            expect(damagedCaf.replaceWithData("caff\0\1", 6));
            const auto damagedResult = cafImporter.load(damagedCaf);
            expect(!damagedResult.succeeded());
            expect(damagedResult.error.containsIgnoreCase("CAF"), damagedResult.error);
            damagedCaf.deleteFile();

            const auto unsupportedCodec = makeTemporaryFile(".caf");
            expect(writeCafLpcmFile(unsupportedCodec, 128, 0x7a7a7a7a)); // "zzzz"
            const auto codecResult = cafImporter.load(unsupportedCodec);
            expect(!codecResult.succeeded());
#if JUCE_MAC || JUCE_IOS
            expect(codecResult.error.containsIgnoreCase("CAF"), codecResult.error);
#else
            expect(codecResult.error.containsIgnoreCase("not supported"), codecResult.error);
#endif
            unsupportedCodec.deleteFile();
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
        {
            SamplePlaybackEngine engine;
            engine.prepare(96000.0);
            engine.submitSample(makeSineSample(96000.0, 440.0));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);
            engine.play();
            engine.process(block);
            engine.prepare(44100.0); // Simulates a host/device sample-rate change.
            engine.play();
            engine.process(block);
            expect(engine.isPlaying());
            expectWithinAbsoluteError(engine.positionSeconds(), 512.0 / 44100.0, 0.0001);
            for (int frame = 0; frame < block.getNumSamples(); ++frame)
                expect(std::isfinite(block.getSample(0, frame)));
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }
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
            engine.submitSample(nullptr); // Failed/empty import must not erase the active Sample.
            engine.stop();
            engine.process(block);
            engine.play();
            engine.process(block);
            expect(block.getSample(0, 350) < -0.1f);
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }

        beginTest("Band-limited resampling preserves passband and suppresses aliased tones");
        {
            const auto expectedSineRms = 0.5 / std::sqrt(2.0);
            const auto passbandRms = measureRms(makeSineSample(96000.0, 10000.0),
                                                44100.0, 0.08, 0.38);
            const auto passbandDb = 20.0 * std::log10(passbandRms / expectedSineRms);
            logMessage("96 kHz -> 44.1 kHz, 10 kHz passband: "
                       + juce::String(passbandRms, 6) + " RMS, "
                       + juce::String(passbandDb, 3) + " dB gain");
            expect(std::abs(passbandDb) < 0.15,
                   "10 kHz passband gain should remain within 0.15 dB; measured "
                       + juce::String(passbandDb, 3) + " dB");

            const auto aliasRms = measureRms(makeSineSample(96000.0, 30000.0),
                                             44100.0, 0.08, 0.38);
            const auto aliasDb = 20.0 * std::log10(juce::jmax(aliasRms, 1.0e-12)
                                                     / expectedSineRms);
            logMessage("96 kHz -> 44.1 kHz, 30 kHz alias: "
                       + juce::String(aliasRms, 8) + " RMS, "
                       + juce::String(aliasDb, 2) + " dB relative");
            expect(aliasDb < -45.0,
                   "30 kHz alias should be below -45 dB; measured "
                       + juce::String(aliasDb, 2) + " dB");
        }

        beginTest("A swept high-frequency signal is attenuated above output Nyquist");
        {
            const auto earlyRms = measureRms(makeSweepSample(96000.0, 5000.0, 40000.0),
                                             44100.0, 0.08, 0.18);
            const auto lateRms = measureRms(makeSweepSample(96000.0, 5000.0, 40000.0),
                                            44100.0, 0.82, 0.92);
            const auto attenuationDb = 20.0 * std::log10(juce::jmax(lateRms, 1.0e-12) / earlyRms);
            logMessage("96 kHz -> 44.1 kHz, sweep above Nyquist: "
                       + juce::String(attenuationDb, 2) + " dB vs passband window");
            expect(attenuationDb < -35.0,
                   "Swept content above Nyquist should be attenuated; measured "
                       + juce::String(attenuationDb, 2) + " dB");
        }

        beginTest("DC, boundaries, and short-sample fades remain finite and click-controlled");
        {
            const auto dcRms = measureRms(makeSample(1, 96000, 96000.0, 0.37f),
                                          44100.0, 0.08, 0.38);
            logMessage("96 kHz -> 44.1 kHz, 0.37 DC error: "
                       + juce::String(std::abs(dcRms - 0.37), 8));
            expectWithinAbsoluteError(dcRms, 0.37, 0.0001);

            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 128, 48000.0, 0.5f));
            juce::AudioBuffer<float> oneFrame(1, 1);
            engine.process(oneFrame);
            engine.play();
            auto previous = 0.0f;
            auto maximumDelta = 0.0f;
            auto lastValue = 0.0f;
            for (int frame = 0; frame < 160; ++frame)
            {
                engine.process(oneFrame);
                const auto value = oneFrame.getSample(0, 0);
                expect(std::isfinite(value));
                maximumDelta = juce::jmax(maximumDelta, std::abs(value - previous));
                previous = value;
                if (frame < 128)
                    lastValue = value;
            }
            expect(maximumDelta < 0.06f, "Short sample edge ramp should avoid an abrupt jump.");
            expect(std::abs(lastValue) < 0.01f, "Final source frame should fade toward silence.");
            logMessage("128-frame constant sample, maximum adjacent delta: "
                       + juce::String(maximumDelta, 6) + ", last frame: "
                       + juce::String(lastValue, 6));
            engine.reclaimRetired();
        }

        beginTest("Stop and rapid Play/Stop commands are click-controlled");
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 48000, 48000.0, 0.7f));
            juce::AudioBuffer<float> oneFrame(1, 1);
            engine.process(oneFrame);
            engine.play();
            auto previous = 0.0f;
            auto maximumDelta = 0.0f;
            for (int frame = 0; frame < 100; ++frame)
            {
                engine.process(oneFrame);
                const auto value = oneFrame.getSample(0, 0);
                maximumDelta = juce::jmax(maximumDelta, std::abs(value - previous));
                previous = value;
            }
            engine.stop();
            for (int frame = 0; frame < 300; ++frame)
            {
                engine.process(oneFrame);
                const auto value = oneFrame.getSample(0, 0);
                maximumDelta = juce::jmax(maximumDelta, std::abs(value - previous));
                previous = value;
            }
            expect(maximumDelta < 0.01f);
            expect(!engine.isPlaying());

            for (int i = 0; i < 8; ++i)
            {
                engine.play();
                engine.stop();
                engine.process(oneFrame);
                expect(std::isfinite(oneFrame.getSample(0, 0)));
            }
            engine.reclaimRetired();
        }

        beginTest("Output is independent of host block size");
        {
            std::array<std::vector<float>, 5> rendered;
            const std::array<int, 5> sizes { 64, 128, 256, 512, 1024 };
            for (size_t testIndex = 0; testIndex < sizes.size(); ++testIndex)
            {
                SamplePlaybackEngine engine;
                engine.prepare(44100.0);
                engine.submitSample(makeSineSample(96000.0, 1200.0));
                const auto blockSize = sizes[testIndex];
                juce::AudioBuffer<float> block(2, blockSize);
                engine.process(block);
                engine.play();
                const auto totalFrames = 44100 / 4;
                auto processed = 0;
                while (processed < totalFrames)
                {
                    engine.process(block);
                    const auto frames = juce::jmin(blockSize, totalFrames - processed);
                    for (int frame = 0; frame < frames; ++frame)
                        rendered[testIndex].push_back(block.getSample(0, frame));
                    processed += frames;
                }
                engine.reclaimRetired();
            }
            for (size_t testIndex = 1; testIndex < rendered.size(); ++testIndex)
            {
                auto maximumError = 0.0f;
                for (size_t frame = 0; frame < rendered[0].size(); ++frame)
                    maximumError = juce::jmax(maximumError,
                        std::abs(rendered[0][frame] - rendered[testIndex][frame]));
                expect(maximumError < 1.0e-6f);
            }
        }

        beginTest("Retirement queue backpressure preserves the newest submitted Sample");
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 48000, 48000.0, 0.1f));
            juce::AudioBuffer<float> block(1, 512);
            engine.process(block);
            for (int i = 0; i < 150; ++i)
            {
                const auto value = i == 149 ? -0.75f : static_cast<float>(i) / 200.0f;
                engine.submitSample(makeSample(1, 2048, 48000.0, value));
                engine.process(block);
            }
            engine.reclaimRetired();
            for (int i = 0; i < 4; ++i)
                engine.process(block);
            engine.play();
            engine.process(block);
            expect(block.getSample(0, 400) < -0.5f);
            engine.stop();
            engine.process(block);
            engine.reclaimRetired();
        }

        beginTest("Replacing a Sample during Stop completes the fade before installing the new source");
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            engine.submitSample(makeSample(1, 48000, 48000.0, 0.4f));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);
            engine.play();
            engine.process(block);
            engine.stop();
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
            juce::AudioBuffer<float> audioBlock(2, 128);
            std::thread audio([&]
            {
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

        beginTest("Shared Sample remains alive for analysis after playback replacement");
        {
            SamplePlaybackEngine engine;
            engine.prepare(48000.0);
            SampleHandle playbackAndAnalysis = SampleHandle(makeSample(1, 48000, 48000.0));
            std::weak_ptr<const SampleStorage> observedLifetime = playbackAndAnalysis;
            expect(engine.submitSharedSample(playbackAndAnalysis));
            juce::AudioBuffer<float> block(2, 512);
            engine.process(block);

            auto analysisLease = playbackAndAnalysis;
            playbackAndAnalysis.reset();
            expect(engine.submitSharedSample(SampleHandle(makeSample(1, 2048, 48000.0, -0.2f))));
            engine.process(block);
            engine.reclaimRetired();
            expect(!observedLifetime.expired());

            analysisLease.reset();
            expect(observedLifetime.expired());
        }
    }
};

SampleEngineTests sampleEngineTests;
}

juce::UnitTest* getSampleEngineTest()
{
    return &sampleEngineTests;
}

int runSampleEngineBenchmarks()
{
    using Clock = std::chrono::steady_clock;
    const auto tableStart = Clock::now();
    BandlimitedResampler resampler;
    const auto tableMs = std::chrono::duration<double, std::milli>(Clock::now() - tableStart).count();

    constexpr int sourceRate = 96000;
    constexpr int sourceFrames = sourceRate * 5;
    juce::AudioBuffer<float> benchmarkAudio(2, sourceFrames);
    for (int frame = 0; frame < sourceFrames; ++frame)
    {
        const auto time = static_cast<double>(frame) / sourceRate;
        benchmarkAudio.setSample(0, frame, 0.4f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
            * 731.0 * time)));
        benchmarkAudio.setSample(1, frame, 0.3f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi
            * 1703.0 * time)));
    }
    SampleMetadata benchmarkMetadata;
    benchmarkMetadata.fileName = "benchmark.wav";
    benchmarkMetadata.sampleRate = sourceRate;
    benchmarkMetadata.channelCount = 2;
    benchmarkMetadata.sampleCount = sourceFrames;
    auto sample = std::make_unique<SampleStorage>(std::move(benchmarkAudio), benchmarkMetadata);
    const auto sampleBytes = static_cast<std::size_t>(2) * sourceFrames * sizeof(float);
    const auto waveformStart = Clock::now();
    const auto waveform = WaveformData::generate(*sample);
    const auto waveformMs = std::chrono::duration<double, std::milli>(Clock::now() - waveformStart).count();
    std::size_t peakCount = 0;
    for (int level = 0; level < waveform.levelCount(); ++level)
        for (const auto& channel : waveform.level(level).channels)
            peakCount += channel.size();
    const auto waveformBytes = peakCount * sizeof(WaveformPeak);

    auto traversalChecksum = 0.0;
    const auto traversalStart = Clock::now();
    constexpr int renderPasses = 1000;
    constexpr int viewportPixels = 900;
    const auto selectedLevel = waveform.chooseLevel(
        static_cast<double>(sourceFrames) / viewportPixels);
    const auto& level = waveform.level(selectedLevel);
    for (int pass = 0; pass < renderPasses; ++pass)
        for (const auto& channelPeaks : level.channels)
            for (int x = 0; x < viewportPixels; ++x)
            {
                const auto frame = static_cast<juce::int64>(
                    static_cast<double>(x) / viewportPixels * sourceFrames);
                const auto peak = static_cast<size_t>(juce::jlimit<juce::int64>(
                    0, static_cast<juce::int64>(channelPeaks.size() - 1),
                    frame / level.framesPerPeak));
                traversalChecksum += channelPeaks[peak].maximum - channelPeaks[peak].minimum;
            }
    const auto traversalMs = std::chrono::duration<double, std::milli>(Clock::now() - traversalStart).count();

    std::cout << "Bandlimited resampler table init: " << tableMs << " ms\n"
              << "Shared coefficient table: " << BandlimitedResampler::tableMemoryBytes << " bytes\n"
              << "5 s stereo 96 kHz source: " << sampleBytes << " bytes\n"
              << "Waveform build: " << waveformMs << " ms; peak storage: " << waveformBytes << " bytes\n"
              << "900 px stereo peak traversal x " << renderPasses << ": " << traversalMs
              << " ms (checksum " << traversalChecksum << ")\n";

    for (const auto outputRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto blockSize : { 64, 128, 256, 512, 1024 })
        {
            SamplePlaybackEngine engine;
            engine.prepare(outputRate);
            engine.submitSample(makeSample(2, sourceFrames, sourceRate, 0.1f));
            juce::AudioBuffer<float> block(2, blockSize);
            engine.process(block);
            engine.play();
            const auto blockCount = static_cast<int>(std::ceil(outputRate / blockSize));
            const auto simulatedSeconds = blockCount * blockSize / outputRate;
            const auto start = Clock::now();
            for (int i = 0; i < blockCount; ++i)
                engine.process(block);
            const auto elapsedSeconds = std::chrono::duration<double>(Clock::now() - start).count();
            const auto realtimePercent = 100.0 * elapsedSeconds / simulatedSeconds;
            std::cout << "Playback host=" << outputRate << " Hz block=" << blockSize
                      << ": " << realtimePercent << "% realtime CPU\n";
            engine.reclaimRetired();
        }
    }
    return 0;
}
