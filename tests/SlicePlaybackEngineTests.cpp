#include "SlicePlaybackEngineTests.h"
#include "../src/playback/SlicePlaybackEngine.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace
{
SampleHandle makeSample(int frames, int channels = 1, float value = 0.25f,
                        double sampleRate = 48000.0)
{
    juce::AudioBuffer<float> audio(channels, frames);
    for (int channel = 0; channel < channels; ++channel)
        audio.clear(channel, 0, frames);
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            audio.setSample(channel, frame, value * static_cast<float>(channel + 1));
    SampleMetadata metadata;
    metadata.fileName = "fixture.wav";
    metadata.sampleRate = sampleRate;
    metadata.channelCount = channels;
    metadata.sampleCount = frames;
    return std::make_shared<const SampleStorage>(std::move(audio), std::move(metadata));
}

class SlicePlaybackEngineUnitTest final : public juce::UnitTest
{
public:
    SlicePlaybackEngineUnitTest() : UnitTest("Creative Sampler Slice Playback", "Slice Playback") {}

    void runTest() override
    {
        beginTest("Snapshot pins the immutable sample, maps notes, and rejects stale identity");
        {
            auto sample = makeSample(1024);
            SliceEngine editable;
            expect(editable.setSample(501, 1024));
            const auto firstId = editable.slices().front().id;
            expect(editable.setMidiNote(firstId, 64));
            auto snapshot = SlicePlaybackSnapshot::create(sample, 501, editable);
            expect(snapshot != nullptr);
            if (snapshot != nullptr)
            {
                expect(snapshot->sampleHandle().get() == sample.get());
                expect(snapshot->regionsForNote(64).size() == 1);
                expect(snapshot->regionsForNote(60).empty());
                const auto snapshotRevision = snapshot->revision();
                expect(!SlicePlaybackSnapshot::create(sample, 500, editable));
                expect(editable.setMidiNote(firstId, 65));
                expect(snapshot->revision() == snapshotRevision);
                expect(snapshot->regionsForNote(64).size() == 1);
                expect(snapshot->regionsForNote(65).empty());
            }
        }

        beginTest("Voice reads only its half-open range and handles mono and stereo");
        {
            auto monoSample = makeSample(512, 1, 0.3f);
            SliceEngine monoModel;
            expect(monoModel.setSample(511, 512));
            auto monoSnapshot = SlicePlaybackSnapshot::create(monoSample, 511, monoModel);
            expect(monoSnapshot != nullptr);
            if (monoSnapshot != nullptr)
            {
                SliceVoice monoVoice;
                expect(monoVoice.start(monoSnapshot.get(), 0, 60, 127, 48000.0, 0));
                float left = 0.0f;
                float right = 0.0f;
                auto largestChannelDifference = 0.0f;
                auto maximumMonoOutput = 0.0f;
                for (int i = 0; i < 512 && monoVoice.isActive(); ++i)
                {
                    monoVoice.renderFrame(left, right);
                    largestChannelDifference = std::max(largestChannelDifference, std::abs(left - right));
                    maximumMonoOutput = std::max(maximumMonoOutput, std::abs(left));
                }
                expect(largestChannelDifference <= 1.0e-6f);
                expect(maximumMonoOutput > 0.29f && maximumMonoOutput < 0.31f);
                expect(!monoVoice.isActive());
            }

            juce::AudioBuffer<float> audio(2, 512);
            audio.clear();
            for (int i = 0; i < 512; ++i)
            {
                audio.setSample(0, i, i < 128 || i >= 384 ? 0.9f : 0.2f);
                audio.setSample(1, i, i < 128 || i >= 384 ? -0.9f : -0.4f);
            }
            SampleMetadata metadata;
            metadata.sampleRate = 48000.0;
            metadata.channelCount = 2;
            metadata.sampleCount = 512;
            auto sample = std::make_shared<const SampleStorage>(std::move(audio), metadata);
            SliceEngine model;
            expect(model.setSample(502, 512));
            const auto id = model.slices().front().id;
            expect(model.moveBoundary(id, SliceBoundary::start, 128));
            expect(model.moveBoundary(id, SliceBoundary::end, 384));
            auto snapshot = SlicePlaybackSnapshot::create(sample, 502, model);
            expect(snapshot != nullptr);
            if (snapshot != nullptr)
            {
                SliceVoice voice;
                expect(voice.start(snapshot.get(), 0, 60, 127, 48000.0, 0));
                float left = 0.0f;
                float right = 0.0f;
                auto maximumLeft = 0.0f;
                auto maximumRight = 0.0f;
                for (int i = 0; i < 256 && voice.isActive(); ++i)
                {
                    voice.renderFrame(left, right);
                    maximumLeft = std::max(maximumLeft, std::abs(left));
                    maximumRight = std::max(maximumRight, std::abs(right));
                }
                expect(maximumLeft > 0.19f && maximumLeft < 0.21f);
                expect(maximumRight > 0.39f && maximumRight < 0.41f);
                expect(!voice.isActive());
            }
        }

        beginTest("MIDI offsets, unknown notes, velocity, polyphony, and note-off are respected");
        {
            SliceEngine model;
            auto sample = makeSample(24000, 2, 0.2f, 48000.0);
            expect(model.setSample(503, 24000));
            const auto first = model.slices().front().id;
            const auto second = model.splitSlice(first, 12000);
            expect(second.has_value());
            if (second)
                expect(model.setMidiNote(*second, 62));
            auto snapshot = SlicePlaybackSnapshot::create(sample, 503, model);
            expect(snapshot != nullptr);
            if (snapshot != nullptr)
            {
                SlicePlaybackEngine engine;
                engine.prepare(48000.0);
                expect(engine.publishSnapshot(std::move(snapshot)));
                juce::AudioBuffer<float> output(2, 512);
                output.clear();
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 91, static_cast<juce::uint8>(100)), 32);
                midi.addEvent(juce::MidiMessage::noteOn(1, 70, static_cast<juce::uint8>(96)), 160);
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 200);
                midi.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
                engine.process(output, midi, false);
                expect(engine.activeVoiceCount() == 1);
                expect(output.getMagnitude(0, 0, 200) == 0.0f);
                expect(output.getMagnitude(0, 200, 100) > 0.0f);
                expect(output.getMagnitude(0, 400, 112) < output.getMagnitude(0, 200, 100));

                juce::MidiBuffer repeated;
                repeated.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
                repeated.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(127)), 0);
                output.clear();
                engine.process(output, repeated, false);
                expect(engine.activeVoiceCount() == 2);
            }
        }

        beginTest("Pitch and source-rate conversion preserve the intended output durations");
        {
            constexpr int sourceFrames = 48000;
            juce::AudioBuffer<float> audio(1, sourceFrames);
            for (int i = 0; i < sourceFrames; ++i)
                audio.setSample(0, i, static_cast<float>(std::sin(
                    2.0 * juce::MathConstants<double>::pi * 440.0
                    * static_cast<double>(i) / 48000.0)));
            SampleMetadata metadata;
            metadata.sampleRate = 48000.0;
            metadata.channelCount = 1;
            metadata.sampleCount = sourceFrames;
            auto sample = std::make_shared<const SampleStorage>(std::move(audio), metadata);
            SliceEngine model;
            expect(model.setSample(507, sourceFrames));
            auto snapshot = SlicePlaybackSnapshot::create(sample, 507, model);
            expect(snapshot != nullptr);
            if (snapshot != nullptr)
            {
                const auto countPositiveCrossings = [&snapshot](int note)
                {
                    SliceVoice voice;
                    if (!voice.start(snapshot.get(), 0, note, 127, 48000.0, 0))
                        return -1;
                    int crossings = 0;
                    float previous = 0.0f;
                    for (int frame = 0; frame < 6000; ++frame)
                    {
                        float left = 0.0f;
                        float right = 0.0f;
                        voice.renderFrame(left, right);
                        if (frame >= 1000 && frame < 5000 && previous <= 0.0f && left > 0.0f)
                            ++crossings;
                        previous = left;
                    }
                    return crossings;
                };
                const auto octaveUp = countPositiveCrossings(72);
                const auto octaveDown = countPositiveCrossings(48);
                expect(octaveUp >= 70 && octaveUp <= 76);
                expect(octaveDown >= 17 && octaveDown <= 20);
            }

            juce::AudioBuffer<float> convertedAudio(1, sourceFrames);
            for (int i = 0; i < sourceFrames; ++i)
                convertedAudio.setSample(0, i, static_cast<float>(std::sin(
                    2.0 * juce::MathConstants<double>::pi * 440.0
                    * static_cast<double>(i) / 44100.0)));
            auto convertedMetadata = SampleMetadata {};
            convertedMetadata.sampleRate = 44100.0;
            convertedMetadata.channelCount = 1;
            convertedMetadata.sampleCount = sourceFrames;
            auto convertedSample = std::make_shared<const SampleStorage>(
                std::move(convertedAudio), convertedMetadata);
            SliceEngine convertedModel;
            expect(convertedModel.setSample(509, sourceFrames));
            auto convertedSnapshot = SlicePlaybackSnapshot::create(
                convertedSample, 509, convertedModel);
            expect(convertedSnapshot != nullptr);
            if (convertedSnapshot != nullptr)
            {
                SliceVoice voice;
                expect(voice.start(convertedSnapshot.get(), 0, 60, 127, 48000.0, 0));
                int crossings = 0;
                float previous = 0.0f;
                for (int frame = 0; frame < 6000; ++frame)
                {
                    float left = 0.0f;
                    float right = 0.0f;
                    voice.renderFrame(left, right);
                    if (frame >= 1000 && frame < 5000 && previous <= 0.0f && left > 0.0f)
                        ++crossings;
                    previous = left;
                }
                expect(crossings >= 35 && crossings <= 38);
            }
        }

        beginTest("Sample replacement releases old voices and stale snapshots are rejected");
        {
            SliceEngine modelA;
            auto sampleA = makeSample(12000, 1, 0.3f);
            expect(modelA.setSample(504, 12000));
            auto snapshotA = SlicePlaybackSnapshot::create(sampleA, 504, modelA);
            SlicePlaybackEngine engine;
            engine.prepare(48000.0);
            expect(engine.publishSnapshot(std::move(snapshotA)));
            juce::AudioBuffer<float> output(2, 128);
            output.clear();
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
            engine.process(output, on, false);
            expect(engine.activeVoiceCount() == 1);

            engine.setCurrentSampleId(505);
            SliceEngine modelB;
            auto sampleB = makeSample(12000, 1, 0.1f);
            expect(modelB.setSample(505, 12000));
            expect(!engine.publishSnapshot(SlicePlaybackSnapshot::create(sampleA, 504, modelA)));
            expect(engine.publishSnapshot(SlicePlaybackSnapshot::create(sampleB, 505, modelB)));
            juce::MidiBuffer empty;
            output.clear();
            engine.process(output, empty, false);
            expect(engine.activeVoiceCount() == 1); // Old voice is in its short release.
            engine.reclaimRetired();
            output.clear();
            engine.process(output, empty, false);
            expect(engine.activeVoiceCount() == 0);
        }

        beginTest("Voice pool is bounded and repeated notes are deterministic");
        {
            SliceEngine model;
            auto sample = makeSample(4096);
            expect(model.setSample(506, 4096));
            const auto first = model.slices().front().id;
            expect(model.deleteSlice(first));
            for (int i = 0; i < 17; ++i)
                expect(model.createSlice(i * 128, (i + 1) * 128).has_value());
            auto snapshot = SlicePlaybackSnapshot::create(sample, 506, model);
            expect(snapshot != nullptr);
            if (snapshot != nullptr)
            {
                SlicePlaybackEngine engine;
                engine.prepare(48000.0);
                expect(engine.publishSnapshot(std::move(snapshot)));
                juce::MidiBuffer midi;
                for (int note = 61; note < 78; ++note)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127)), 0);
                juce::AudioBuffer<float> output(2, 32);
                output.clear();
                engine.process(output, midi, false);
                expect(engine.activeVoiceCount() == SlicePlaybackEngine::maximumVoices);
                expect(output.getMagnitude(0, 0, output.getNumSamples()) <= 0.26f);
            }
        }

        beginTest("Snapshot publication remains safe while the audio thread renders");
        {
            SliceEngine model;
            auto sample = makeSample(4096);
            expect(model.setSample(508, 4096));
            SlicePlaybackEngine engine;
            engine.prepare(48000.0);
            auto initial = SlicePlaybackSnapshot::create(sample, 508, model);
            expect(initial != nullptr && engine.publishSnapshot(std::move(initial)));

            std::atomic<bool> done { false };
            std::atomic<bool> audioThreadOkay { true };
            std::thread audioThread([&]
            {
                juce::AudioBuffer<float> output(2, 64);
                const juce::MidiBuffer noMidi;
                while (!done.load(std::memory_order_acquire))
                {
                    output.clear();
                    engine.process(output, noMidi, false);
                    if (output.getMagnitude(0, 0, output.getNumSamples()) != 0.0f)
                        audioThreadOkay.store(false, std::memory_order_relaxed);
                }
                for (int i = 0; i < 4; ++i)
                {
                    output.clear();
                    engine.process(output, noMidi, false);
                }
            });

            bool everyRequestAccepted = true;
            for (int i = 0; i < 500; ++i)
            {
                const auto note = 60 + (i % 12);
                if (note != model.slices().front().midiNote)
                    (void) model.setMidiNote(model.slices().front().id, note);
                auto snapshot = SlicePlaybackSnapshot::create(sample, 508, model);
                everyRequestAccepted = everyRequestAccepted && snapshot != nullptr
                    && engine.publishSnapshot(std::move(snapshot));
                engine.reclaimRetired();
            }
            done.store(true, std::memory_order_release);
            audioThread.join();
            engine.reclaimRetired();
            expect(everyRequestAccepted);
            expect(audioThreadOkay.load(std::memory_order_relaxed));
            expect(engine.activeVoiceCount() == 0);
        }

        beginTest("Retirement queue backpressure retains the newest mapping without freeing on audio thread");
        {
            SliceEngine model;
            auto sample = makeSample(4096);
            expect(model.setSample(510, 4096));
            SlicePlaybackEngine engine;
            engine.prepare(48000.0);
            juce::AudioBuffer<float> output(2, 32);
            juce::MidiBuffer noMidi;
            expect(engine.publishSnapshot(SlicePlaybackSnapshot::create(sample, 510, model)));
            output.clear();
            engine.process(output, noMidi, false);

            for (int i = 0; i < 140; ++i)
            {
                const auto note = 60 + (i % 2);
                const auto id = model.slices().front().id;
                if (model.slices().front().midiNote != note)
                    (void) model.setMidiNote(id, note);
                auto snapshot = SlicePlaybackSnapshot::create(sample, 510, model);
                expect(snapshot != nullptr && engine.publishSnapshot(std::move(snapshot)));
                output.clear();
                engine.process(output, noMidi, false);
            }
            engine.reclaimRetired();
            for (int i = 0; i < 3; ++i)
            {
                output.clear();
                engine.process(output, noMidi, false);
                engine.reclaimRetired();
            }
            juce::MidiBuffer finalNote;
            finalNote.addEvent(juce::MidiMessage::noteOn(1, model.slices().front().midiNote,
                                                          static_cast<juce::uint8>(127)), 0);
            output.clear();
            engine.process(output, finalNote, false);
            expect(engine.activeVoiceCount() == 1);
            expect(output.getMagnitude(0, 0, output.getNumSamples()) > 0.0f);
        }
    }
};

SlicePlaybackEngineUnitTest slicePlaybackEngineUnitTest;
}

juce::UnitTest* getSlicePlaybackEngineTest()
{
    return &slicePlaybackEngineUnitTest;
}
