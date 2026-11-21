#include "KeyAnalyzerTests.h"
#include "../src/analysis/AnalysisCoordinator.h"
#include "../src/analysis/BPMAnalyzer.h"
#include "../src/analysis/KeyAnalyzer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
struct Fixture
{
    Fixture(double rate, double seconds, int channels = 1)
        : sampleRate(rate), audio(channels, juce::jmax(1, juce::roundToInt(rate * seconds)))
    {
        audio.clear();
    }

    SampleHandle finish() &&
    {
        SampleMetadata metadata;
        metadata.fileName = "key-analysis-fixture.wav";
        metadata.sampleRate = sampleRate;
        metadata.channelCount = audio.getNumChannels();
        metadata.sampleCount = audio.getNumSamples();
        return std::make_shared<const SampleStorage>(std::move(audio), std::move(metadata));
    }

    double sampleRate;
    juce::AudioBuffer<float> audio;
};

double midiFrequency(int note) noexcept
{
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

void addNote(Fixture& fixture, int channel, int midiNote, double startSeconds,
             double durationSeconds, float amplitude, bool bass = false)
{
    auto* output = fixture.audio.getWritePointer(juce::jlimit(0,
        fixture.audio.getNumChannels() - 1, channel));
    const auto start = juce::jlimit(0, fixture.audio.getNumSamples(),
        juce::roundToInt(startSeconds * fixture.sampleRate));
    const auto count = juce::jmin(fixture.audio.getNumSamples() - start,
        juce::jmax(0, juce::roundToInt(durationSeconds * fixture.sampleRate)));
    const auto fundamental = midiFrequency(midiNote);
    const auto harmonicCount = bass ? 5 : 4;
    constexpr double harmonicGain[] { 1.0, 0.34, 0.18, 0.10, 0.05 };

    for (int frame = 0; frame < count; ++frame)
    {
        const auto time = static_cast<double>(frame) / fixture.sampleRate;
        const auto attack = juce::jlimit(0.0, 1.0, time / 0.025);
        const auto release = juce::jlimit(0.0, 1.0,
            (durationSeconds - time) / (bass ? 0.12 : 0.08));
        const auto envelope = juce::jmin(attack, release);
        auto value = 0.0;
        for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic)
            value += harmonicGain[harmonic - 1]
                * std::sin(juce::MathConstants<double>::twoPi * fundamental * harmonic * time);
        output[start + frame] += static_cast<float>(value * envelope * amplitude);
    }
}

constexpr int majorScale[] { 0, 2, 4, 5, 7, 9, 11 };
constexpr int minorScale[] { 0, 2, 3, 5, 7, 8, 10 };

std::array<int, 3> chordNotes(int tonic, bool minor, int degree)
{
    const auto* scale = minor ? minorScale : majorScale;
    std::array<int, 3> notes {};
    for (int chordTone = 0; chordTone < 3; ++chordTone)
    {
        auto scaleDegree = degree + chordTone * 2;
        auto octave = 0;
        while (scaleDegree >= 7)
        {
            scaleDegree -= 7;
            ++octave;
        }
        notes[static_cast<size_t>(chordTone)] = 48 + tonic + scale[scaleDegree] + octave * 12;
    }
    // Raise the dominant's third in a minor key to provide a common harmonic-minor cadence.
    if (minor && degree == 4)
        ++notes[1];
    return notes;
}

SampleHandle renderProgression(double sampleRate, int tonic, bool minor,
                              double durationSeconds = 10.0, int channels = 1)
{
    Fixture fixture(sampleRate, durationSeconds, channels);
    constexpr int degrees[] { 0, 3, 4, 0, 5, 1, 4, 0 };
    const auto chordDuration = 1.0;
    const auto eventCount = static_cast<int>(std::ceil(durationSeconds / chordDuration));
    for (int event = 0; event < eventCount; ++event)
    {
        const auto start = event * chordDuration;
        const auto length = juce::jmin(chordDuration, durationSeconds - start);
        const auto notes = chordNotes(tonic, minor,
            degrees[event % static_cast<int>(std::size(degrees))]);
        for (const auto note : notes)
            addNote(fixture, 0, note, start, length, 0.23f);

        if (channels == 2)
        {
            // Different stereo content, including some phase inversion, exercises channel-safe analysis.
            const auto bassNote = notes[0] - 12;
            addNote(fixture, 1, bassNote, start, length, event % 2 == 0 ? -0.34f : 0.30f, true);
        }
    }
    return std::move(fixture).finish();
}

SampleHandle renderTriad(double sampleRate, int tonic, bool minor, double durationSeconds = 6.0)
{
    Fixture fixture(sampleRate, durationSeconds);
    const auto notes = chordNotes(tonic, minor, 0);
    for (const auto note : notes)
        addNote(fixture, 0, note, 0.0, durationSeconds, 0.25f);
    return std::move(fixture).finish();
}

SampleHandle renderMelody(double sampleRate, int tonic, bool minor, double durationSeconds = 10.0)
{
    Fixture fixture(sampleRate, durationSeconds);
    const auto* scale = minor ? minorScale : majorScale;
    constexpr int melodyDegrees[] { 0, 2, 4, 6, 5, 3, 1, 4, 2, 0, 6, 4, 2, 0 };
    constexpr double noteDuration = 0.42;
    for (int event = 0; event * noteDuration < durationSeconds; ++event)
    {
        const auto degree = melodyDegrees[event % static_cast<int>(std::size(melodyDegrees))];
        const auto midi = 60 + tonic + scale[degree];
        addNote(fixture, 0, midi, event * noteDuration,
                juce::jmin(noteDuration, durationSeconds - event * noteDuration),
                degree == 0 || degree == 4 ? 0.62f : 0.38f);
    }
    return std::move(fixture).finish();
}

SampleHandle renderBassLine(double sampleRate, int tonic, bool minor, double durationSeconds = 10.0)
{
    Fixture fixture(sampleRate, durationSeconds);
    constexpr int degrees[] { 0, 0, 3, 3, 4, 4, 0, 0 };
    constexpr double noteDuration = 0.78;
    for (int event = 0; event * noteDuration < durationSeconds; ++event)
    {
        const auto note = chordNotes(tonic, minor,
            degrees[event % static_cast<int>(std::size(degrees))])[0] - 24;
        addNote(fixture, 0, note, event * noteDuration,
                juce::jmin(noteDuration, durationSeconds - event * noteDuration), 0.58f, true);
    }
    return std::move(fixture).finish();
}

SampleHandle renderScale(double sampleRate, int tonic, bool minor, double durationSeconds = 10.0)
{
    Fixture fixture(sampleRate, durationSeconds);
    const auto* scale = minor ? minorScale : majorScale;
    constexpr double noteDuration = 0.70;
    for (int event = 0; event * noteDuration < durationSeconds; ++event)
    {
        const auto degree = event % 7;
        addNote(fixture, 0, 60 + tonic + scale[degree], event * noteDuration,
                juce::jmin(noteDuration, durationSeconds - event * noteDuration),
                degree == 0 || degree == 4 ? 0.58f : 0.32f);
    }
    return std::move(fixture).finish();
}

SampleHandle renderNoise(double sampleRate, bool pink)
{
    constexpr double duration = 10.0;
    Fixture fixture(sampleRate, duration);
    auto* output = fixture.audio.getWritePointer(0);
    std::uint32_t state = 0x9e3779b9u;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0, b4 = 0.0, b5 = 0.0, b6 = 0.0;
    for (int frame = 0; frame < fixture.audio.getNumSamples(); ++frame)
    {
        state = 1664525u * state + 1013904223u;
        const auto white = static_cast<double>(static_cast<std::int32_t>(state))
            / static_cast<double>(std::numeric_limits<std::int32_t>::max());
        if (pink)
        {
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            const auto pinkValue = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;
            output[frame] = static_cast<float>(pinkValue * 0.11);
        }
        else
            output[frame] = static_cast<float>(white * 0.16);
    }
    return std::move(fixture).finish();
}

SampleHandle renderImpulse(double sampleRate, double durationSeconds = 8.0)
{
    Fixture fixture(sampleRate, durationSeconds);
    auto* output = fixture.audio.getWritePointer(0);
    for (const auto time : { 0.25, 1.10, 2.05, 3.30, 4.05, 5.90, 7.40 })
    {
        const auto start = juce::roundToInt(time * sampleRate);
        if (start < fixture.audio.getNumSamples())
            output[start] = 0.9f;
    }
    return std::move(fixture).finish();
}

SampleHandle renderKick(double sampleRate)
{
    Fixture fixture(sampleRate, 10.0);
    auto* output = fixture.audio.getWritePointer(0);
    for (int event = 0; event * 0.5 < 10.0; ++event)
    {
        const auto start = juce::roundToInt(event * 0.5 * sampleRate);
        const auto count = juce::jmin(fixture.audio.getNumSamples() - start,
            juce::roundToInt(0.13 * sampleRate));
        for (int frame = 0; frame < count; ++frame)
        {
            const auto time = static_cast<double>(frame) / sampleRate;
            const auto frequency = 48.0 + 30.0 * std::exp(-time / 0.02);
            output[start + frame] = static_cast<float>(0.85 * std::sin(
                juce::MathConstants<double>::twoPi * frequency * time) * std::exp(-time / 0.04));
        }
    }
    return std::move(fixture).finish();
}

SampleHandle renderSnare(double sampleRate)
{
    Fixture fixture(sampleRate, 10.0);
    auto* output = fixture.audio.getWritePointer(0);
    std::uint32_t state = 91;
    for (int event = 0; event * 0.5 < 10.0; ++event)
    {
        const auto start = juce::roundToInt(event * 0.5 * sampleRate);
        const auto count = juce::jmin(fixture.audio.getNumSamples() - start,
            juce::roundToInt(0.18 * sampleRate));
        for (int frame = 0; frame < count; ++frame)
        {
            state = 1664525u * state + 1013904223u;
            const auto white = static_cast<double>(static_cast<std::int32_t>(state))
                / static_cast<double>(std::numeric_limits<std::int32_t>::max());
            output[start + frame] += static_cast<float>(white * std::exp(-frame / (sampleRate * 0.035)) * 0.45);
        }
    }
    return std::move(fixture).finish();
}

SampleHandle renderNoiseSweep(double sampleRate)
{
    Fixture fixture(sampleRate, 10.0);
    auto* output = fixture.audio.getWritePointer(0);
    auto phase = 0.0;
    for (int frame = 0; frame < fixture.audio.getNumSamples(); ++frame)
    {
        const auto time = static_cast<double>(frame) / sampleRate;
        const auto frequency = 60.0 * std::pow(9000.0 / 60.0, time / 10.0);
        phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
        output[frame] = static_cast<float>(0.3 * std::sin(phase));
    }
    return std::move(fixture).finish();
}

SampleHandle renderAtonal(double sampleRate)
{
    constexpr double duration = 12.0;
    constexpr double noteDuration = 0.5;
    Fixture fixture(sampleRate, duration);
    for (int event = 0; event * noteDuration < duration; ++event)
    {
        // Equal exposure to all twelve pitch classes, with no tonal center.
        addNote(fixture, 0, 60 + (event % 12), event * noteDuration, noteDuration, 0.55f);
    }
    return std::move(fixture).finish();
}

SampleHandle renderShortFx(double sampleRate)
{
    Fixture fixture(sampleRate, 0.8);
    auto* output = fixture.audio.getWritePointer(0);
    for (int frame = 0; frame < fixture.audio.getNumSamples(); ++frame)
    {
        const auto time = static_cast<double>(frame) / sampleRate;
        output[frame] = static_cast<float>(0.4 * std::sin(juce::MathConstants<double>::twoPi
            * (120.0 + 6000.0 * time) * time) * std::exp(-time * 8.0));
    }
    return std::move(fixture).finish();
}

SampleHandle renderPhaseInvertedStereo(double sampleRate, int tonic, bool minor)
{
    auto mono = renderProgression(sampleRate, tonic, minor, 10.0);
    juce::AudioBuffer<float> stereo(2, mono->audio().getNumSamples());
    stereo.copyFrom(0, 0, mono->audio(), 0, 0, mono->audio().getNumSamples());
    stereo.copyFrom(1, 0, mono->audio(), 0, 0, mono->audio().getNumSamples());
    stereo.applyGain(1, 0, stereo.getNumSamples(), -1.0f);
    auto metadata = mono->metadata();
    metadata.channelCount = 2;
    metadata.sampleCount = stereo.getNumSamples();
    return std::make_shared<const SampleStorage>(std::move(stereo), std::move(metadata));
}

KeyAnalysis analyze(KeyAnalyzer& analyzer, const SampleStorage& sample)
{
    AnalysisCancellationToken cancellation;
    const auto output = analyzer.analyze(sample, cancellation);
    if (!output.payload)
        throw std::runtime_error(output.error.toStdString());
    const auto* key = std::get_if<KeyAnalysis>(&*output.payload);
    if (key == nullptr)
        throw std::runtime_error("KeyAnalyzer returned a non-key payload.");
    return *key;
}

bool candidateMatches(const KeyAnalysis& key, PitchClass tonic, ScaleMode mode)
{
    if (key.tonic == tonic && key.mode == mode)
        return true;
    return std::any_of(key.alternatives.begin(), key.alternatives.end(),
        [tonic, mode](const auto& candidate)
        { return candidate.tonic == tonic && candidate.mode == mode; });
}

class KeyAnalyzerTests final : public juce::UnitTest
{
public:
    KeyAnalyzerTests() : UnitTest("Creative Sampler Key Analyzer", "Analysis") {}

    void runTest() override
    {
        KeyAnalyzer analyzer;
        beginTest("Key result distinguishes empty, valid, and invalid tonal states");
        {
            KeyAnalysis empty;
            expect(empty.isValid());
            expect(empty.fullName().isEmpty());
            expect(empty.detectionStatus == KeyDetectionStatus::insufficientData);

            KeyAnalysis aMinor(PitchClass::a, ScaleMode::minor, 0.72f,
                               { { PitchClass::c, ScaleMode::major, 0.31f, 0.64f } });
            expect(aMinor.isValid());
            expect(aMinor.fullName() == "A Minor");
            expect(aMinor.alternatives.size() == 1);
            aMinor.confidence = std::numeric_limits<float>::quiet_NaN();
            expect(!aMinor.isValid());

            KeyAnalysis malformed;
            malformed.detectionStatus = KeyDetectionStatus::insufficientData;
            malformed.tonic = PitchClass::f;
            expect(!malformed.isValid());
        }

        struct ExpectedKey { PitchClass tonic; ScaleMode mode; const char* name; };
        constexpr ExpectedKey expectedKeys[]
        {
            { PitchClass::c, ScaleMode::major, "C Major" },
            { PitchClass::a, ScaleMode::minor, "A Minor" },
            { PitchClass::g, ScaleMode::major, "G Major" },
            { PitchClass::e, ScaleMode::minor, "E Minor" },
            { PitchClass::d, ScaleMode::major, "D Major" },
            { PitchClass::b, ScaleMode::minor, "B Minor" },
            { PitchClass::f, ScaleMode::major, "F Major" },
            { PitchClass::cSharp, ScaleMode::minor, "C# Minor" }
        };

        beginTest("Major and minor progressions identify all requested tonic/mode pairs");
        {
            auto correctKeys = 0;
            auto correctTonics = 0;
            auto correctModes = 0;
            auto topThree = 0;
            std::cout << "Key fixture table: Fixture | Expected | Detected | Top-3 | Confidence | Profile score | Result\n";
            for (size_t index = 0; index < std::size(expectedKeys); ++index)
            {
                auto sample = renderProgression(index % 2 == 0 ? 44100.0 : 48000.0,
                    static_cast<int>(expectedKeys[index].tonic),
                    expectedKeys[index].mode == ScaleMode::minor, 10.0);
                const auto result = analyze(analyzer, *sample);
                expect(result.isValid());
                const auto exact = result.tonic == expectedKeys[index].tonic
                    && result.mode == expectedKeys[index].mode;
                const auto tonicCorrect = result.tonic == expectedKeys[index].tonic;
                const auto inTopThree = candidateMatches(result, expectedKeys[index].tonic,
                                                          expectedKeys[index].mode);
                correctKeys += exact ? 1 : 0;
                correctTonics += tonicCorrect ? 1 : 0;
                correctModes += result.mode == expectedKeys[index].mode ? 1 : 0;
                topThree += inTopThree ? 1 : 0;
                std::cout << "Progression " << index + 1 << " | " << expectedKeys[index].name
                    << " | " << (result.fullName().isEmpty() ? "insufficientData" : result.fullName())
                    << " | " << (inTopThree ? "yes" : "no") << " | " << result.confidence
                    << " | score=" << result.score
                    << " | " << (exact ? "PASS" : "MISS") << "\n";
                expect(exact, expectedKeys[index].name);
                expect(inTopThree, "expected key missing from top candidates");
            }
            std::cout << "Key metrics: exact key=" << correctKeys << "/8, tonic="
                << correctTonics << "/8, mode=" << correctModes
                << "/8, top-3=" << topThree << "/8\n";
        }

        beginTest("Triads, melodies, bass lines, and scale sequences retain tonal center");
        {
            const auto cMajorTriad = analyze(analyzer, *renderTriad(44100.0, 0, false));
            std::cout << "C Major triad: key=" << cMajorTriad.fullName() << ", confidence="
                << cMajorTriad.confidence << ", score=" << cMajorTriad.score
                << ", ambiguous=" << cMajorTriad.ambiguous << "\n";
            expect(candidateMatches(cMajorTriad, PitchClass::c, ScaleMode::major));
            expect(cMajorTriad.detectionStatus == KeyDetectionStatus::lowConfidence);
            expect(cMajorTriad.ambiguous);
            expect(candidateMatches(cMajorTriad, PitchClass::a, ScaleMode::minor));

            const auto melody = analyze(analyzer, *renderMelody(48000.0, 9, true));
            expect(candidateMatches(melody, PitchClass::a, ScaleMode::minor));

            const auto bass = analyze(analyzer, *renderBassLine(48000.0, 7, false));
            expect(candidateMatches(bass, PitchClass::g, ScaleMode::major));

            const auto scale = analyze(analyzer, *renderScale(44100.0, 2, false));
            expect(candidateMatches(scale, PitchClass::d, ScaleMode::major));
        }

        beginTest("Sample rate, duration, stereo phase inversion, and repeatability are stable");
        {
            auto firstSample = renderProgression(96000.0, 1, true, 12.0, 2);
            auto first = analyze(analyzer, *firstSample);
            auto second = analyze(analyzer, *firstSample);
            expect(first.isValid());
            expect(first.fullName() == second.fullName());
            expectWithinAbsoluteError(first.confidence, second.confidence, 1.0e-6f);
            expect(candidateMatches(first, PitchClass::cSharp, ScaleMode::minor));

            auto phaseInverted = renderPhaseInvertedStereo(44100.0, 7, false);
            const auto phaseResult = analyze(analyzer, *phaseInverted);
            expect(candidateMatches(phaseResult, PitchClass::g, ScaleMode::major));

            auto twoSeconds = renderProgression(44100.0, 0, false, 2.0);
            const auto shortButTonal = analyze(analyzer, *twoSeconds);
            expect(shortButTonal.isValid());
            expect(candidateMatches(shortButTonal, PitchClass::c, ScaleMode::major));
        }

        beginTest("Noise, percussive material, atonal content, and short FX return insufficient data");
        {
            auto countInsufficient = 0;
            auto falseDetections = 0;
            const auto checkInsufficient = [this, &countInsufficient, &falseDetections, &analyzer](const char* name,
                                                                                  SampleHandle sample)
            {
                const auto result = analyze(analyzer, *sample);
                const auto insufficient = result.detectionStatus == KeyDetectionStatus::insufficientData;
                countInsufficient += insufficient ? 1 : 0;
                falseDetections += insufficient ? 0 : 1;
                std::cout << "Negative fixture " << name << " | "
                    << (result.fullName().isEmpty() ? "insufficientData" : result.fullName())
                    << " | confidence=" << result.confidence << " | score=" << result.score << " | "
                    << (insufficient ? "PASS" : "FALSE KEY") << "\n";
                for (const auto& alternative : result.alternatives)
                    std::cout << "  alternative=" << static_cast<int>(alternative.tonic) << "/"
                        << static_cast<int>(alternative.mode) << " score=" << alternative.score << "\n";
                expect(result.isValid());
                expect(insufficient, name);
                expect(!result.tonic && !result.mode);
                expect(result.alternatives.empty());
            };

            Fixture silenceFixture(44100.0, 10.0);
            checkInsufficient("silence", std::move(silenceFixture).finish());
            checkInsufficient("white noise", renderNoise(44100.0, false));
            checkInsufficient("pink noise", renderNoise(48000.0, true));
            checkInsufficient("single transient", renderImpulse(44100.0));
            checkInsufficient("kick", renderKick(48000.0));
            checkInsufficient("snare", renderSnare(44100.0));
            checkInsufficient("noise sweep", renderNoiseSweep(48000.0));
            checkInsufficient("atonal note sequence", renderAtonal(44100.0));
            checkInsufficient("short FX", renderShortFx(44100.0));

            auto singleNote = renderTriad(44100.0, 0, false);
            // Replace the chord with one sustained pitch to ensure a pitch class isn't mistaken for a key.
            Fixture oneNoteFixture(44100.0, 6.0);
            addNote(oneNoteFixture, 0, 60, 0.0, 6.0, 0.7f);
            singleNote = std::move(oneNoteFixture).finish();
            checkInsufficient("single note", std::move(singleNote));
            std::cout << "Negative metrics: false keys=" << falseDetections << "/10, insufficientData="
                      << countInsufficient << "/10\n";
        }

        beginTest("Malformed audio and cooperative cancellation return explicit errors");
        {
            Fixture invalidFixture(44100.0, 4.0);
            invalidFixture.audio.getWritePointer(0)[invalidFixture.audio.getNumSamples() - 1]
                = std::numeric_limits<float>::quiet_NaN();
            auto invalid = std::move(invalidFixture).finish();
            AnalysisCancellationToken active;
            const auto invalidOutput = analyzer.analyze(*invalid, active);
            expect(!invalidOutput.payload);
            expect(invalidOutput.error.isNotEmpty());

            auto valid = renderProgression(48000.0, 0, false, 12.0);
            AnalysisCancellationToken cancelled;
            cancelled.cancel();
            const auto cancelledOutput = analyzer.analyze(*valid, cancelled);
            expect(!cancelledOutput.payload);
            expect(cancelledOutput.error.contains("cancelled"));

            auto longer = renderProgression(48000.0, 9, true, 24.0);
            AnalysisCancellationToken inFlightCancellation;
            std::thread canceller([&inFlightCancellation]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                inFlightCancellation.cancel();
            });
            const auto start = std::chrono::steady_clock::now();
            const auto inFlightOutput = analyzer.analyze(*longer, inFlightCancellation);
            const auto elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            canceller.join();
            std::cout << "In-flight cancellation returned in " << elapsedMs << " ms\n";
            expect(!inFlightOutput.payload);
            expect(inFlightOutput.error.contains("cancelled"));
        }

        beginTest("Coordinator combines actual BPM and key outputs and suppresses stale generations");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            coordinator.setCurrentSample(sampleId);
            auto sample = renderProgression(48000.0, 0, false, 12.0);
            auto bpmAnalyzer = std::make_shared<BPMAnalyzer>();
            auto keyAnalyzer = std::make_shared<KeyAnalyzer>();
            const auto request = coordinator.requestAnalysis(sampleId, sample,
                { bpmAnalyzer, keyAnalyzer });
            expect(request.has_value());
            const auto latestRequest = coordinator.requestAnalysis(sampleId, sample,
                { bpmAnalyzer, keyAnalyzer });
            expect(latestRequest.has_value());
            if (request && latestRequest)
                expect(latestRequest->generation > request->generation);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (coordinator.latestResult().status == AnalysisStatus::running
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto combined = coordinator.latestResult();
            expect(combined.status == AnalysisStatus::completed);
            expect(combined.bpm.has_value() && combined.key.has_value());
            if (combined.key)
                expect(combined.key->fullName() == "C Major");
            if (latestRequest)
                expect(combined.identity == *latestRequest);
            expect(combined.isValid());
            coordinator.shutdown();
        }
    }
};
}

juce::UnitTest* getKeyAnalyzerTest()
{
    static KeyAnalyzerTests test;
    return &test;
}

int runKeyAnalyzerBenchmarks()
{
    KeyAnalyzer analyzer;
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        for (const auto duration : { 2.0, 10.0, 60.0, 180.0 })
        {
            auto sample = renderProgression(sampleRate, 0, false, duration);
            AnalysisCancellationToken cancellation;
            const auto wallStart = std::chrono::steady_clock::now();
            const auto cpuStart = std::clock();
            const auto output = analyzer.analyze(*sample, cancellation);
            const auto cpuEnd = std::clock();
            const auto wallMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - wallStart).count();
            const auto cpuMs = static_cast<double>(cpuEnd - cpuStart) * 1000.0 / CLOCKS_PER_SEC;
            const auto fftSize = std::bit_ceil(std::max<size_t>(1024,
                static_cast<size_t>(std::ceil(sampleRate * 0.32))));
            const auto workspaceBytes = fftSize * sizeof(float) * 3
                + sizeof(double) * (12 * (1 + 4) + 4);
            std::cout << "Key analysis rate=" << sampleRate << " Hz, duration=" << duration
                      << " s, wall=" << wallMs << " ms, CPU=" << cpuMs
                      << " ms, estimated explicit buffers=" << workspaceBytes << " bytes, status=";
            if (!output.payload)
                std::cout << "failed: " << output.error << "\n";
            else
            {
                const auto& result = std::get<KeyAnalysis>(*output.payload);
                std::cout << static_cast<int>(result.detectionStatus) << ", key="
                          << result.fullName() << "\n";
            }
        }
    }
    return 0;
}
