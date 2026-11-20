#include "BPMAnalyzerTests.h"
#include "../src/analysis/AnalysisCoordinator.h"
#include "../src/analysis/BPMAnalyzer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
enum class EventType { kick, snare, hat, pluck, vocal };

struct SampleBuilder
{
    SampleBuilder(double rate, double durationSeconds, int channelCount)
        : sampleRate(rate), audio(channelCount, juce::jmax(1, juce::roundToInt(rate * durationSeconds)))
    {
        audio.clear();
    }

    std::unique_ptr<SampleStorage> finish() &&
    {
        SampleMetadata metadata;
        metadata.fileName = "synthetic-fixture.wav";
        metadata.sampleRate = sampleRate;
        metadata.channelCount = audio.getNumChannels();
        metadata.sampleCount = audio.getNumSamples();
        return std::make_unique<SampleStorage>(std::move(audio), std::move(metadata));
    }

    double sampleRate;
    juce::AudioBuffer<float> audio;
};

void addEvent(SampleBuilder& sample, double timeSeconds, int channel, EventType type,
              float amplitude, std::uint32_t seed = 1)
{
    auto& audio = sample.audio;
    const auto rate = sample.sampleRate;
    const auto first = juce::jlimit(0, audio.getNumSamples(), juce::roundToInt(timeSeconds * rate));
    const auto duration = type == EventType::kick ? 0.11
        : (type == EventType::snare ? 0.075
            : (type == EventType::hat ? 0.025 : (type == EventType::vocal ? 0.34 : 0.19)));
    const auto count = juce::jmin(audio.getNumSamples() - first,
                                  juce::jmax(1, juce::roundToInt(duration * rate)));
    auto state = seed == 0 ? 1u : seed;
    auto* destination = audio.getWritePointer(juce::jlimit(0, audio.getNumChannels() - 1, channel));
    for (int i = 0; i < count; ++i)
    {
        const auto t = static_cast<double>(i) / rate;
        double value = 0.0;
        if (type == EventType::kick)
        {
            const auto envelope = std::exp(-t / 0.026);
            const auto frequency = 48.0 + 24.0 * std::exp(-t / 0.018);
            value = std::sin(juce::MathConstants<double>::twoPi * frequency * t) * envelope;
        }
        else if (type == EventType::snare || type == EventType::hat)
        {
            state = 1664525u * state + 1013904223u;
            const auto noise = static_cast<double>(static_cast<std::int32_t>(state))
                / static_cast<double>(std::numeric_limits<std::int32_t>::max());
            const auto decay = type == EventType::snare ? 0.012 : 0.004;
            value = noise * std::exp(-t / decay);
            if (type == EventType::snare)
                value += 0.25 * std::sin(juce::MathConstants<double>::twoPi * 180.0 * t)
                    * std::exp(-t / 0.04);
        }
        else if (type == EventType::pluck)
        {
            constexpr double notes[] { 220.0, 261.63, 293.66, 329.63, 392.0, 329.63, 293.66, 261.63 };
            const auto note = static_cast<size_t>(juce::roundToInt(timeSeconds * 8.0)) % std::size(notes);
            value = std::sin(juce::MathConstants<double>::twoPi * notes[note] * t)
                * std::exp(-t / 0.075);
        }
        else
        {
            const auto fundamental = 145.0 + 31.0 * std::sin(timeSeconds * 1.7);
            const auto envelope = (1.0 - std::exp(-t / 0.012)) * std::exp(-t / 0.19);
            value = envelope * (std::sin(juce::MathConstants<double>::twoPi * fundamental * t)
                + 0.42 * std::sin(juce::MathConstants<double>::twoPi * fundamental * 2.4 * t)
                + 0.2 * std::sin(juce::MathConstants<double>::twoPi * fundamental * 4.1 * t));
        }

        const auto existing = static_cast<double>(destination[first + i]);
        destination[first + i] = static_cast<float>(juce::jlimit(-1.0, 1.0,
            existing + value * amplitude));
    }
}

std::unique_ptr<SampleStorage> renderKickLoop(double sampleRate, double bpm,
                                             double durationSeconds, int channels = 1,
                                             int variation = 0)
{
    SampleBuilder sample(sampleRate, durationSeconds, channels);
    const auto beat = 60.0 / bpm;
    for (int beatIndex = 0; beatIndex * beat < durationSeconds; ++beatIndex)
    {
        const auto accent = beatIndex % 4 == 0 ? 0.95f
            : (beatIndex % 2 == 0 ? 0.68f : 0.78f);
        const auto time = static_cast<double>(beatIndex) * beat;
        addEvent(sample, time, 0, EventType::kick, accent);
        if (channels == 2 && beatIndex % 2 == 1)
            addEvent(sample, time + beat * 0.5, 1, EventType::snare, 0.7f,
                     static_cast<std::uint32_t>(beatIndex + 1));
        if (variation == 1 && beatIndex % 4 == 3)
            addEvent(sample, time + beat * 0.5, 0, EventType::hat, 0.3f,
                     static_cast<std::uint32_t>(beatIndex + 11));
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderBackbeat(double sampleRate, double bpm, double durationSeconds,
                                             bool hiHats = false)
{
    SampleBuilder sample(sampleRate, durationSeconds, 1);
    const auto beat = 60.0 / bpm;
    const auto beatCount = static_cast<int>(std::ceil(durationSeconds / beat));
    for (int index = 0; index < beatCount; ++index)
    {
        const auto time = static_cast<double>(index) * beat;
        if (index % 4 == 0 || index % 4 == 2)
            addEvent(sample, time, 0, EventType::kick, index % 4 == 0 ? 0.95f : 0.7f);
        else
            addEvent(sample, time, 0, EventType::snare, 0.65f,
                     static_cast<std::uint32_t>(index + 101));
        if (hiHats)
            addEvent(sample, time + beat * 0.5, 0, EventType::hat, 0.26f,
                     static_cast<std::uint32_t>(index + 501));
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderHalfTimeAmbiguity(double sampleRate)
{
    constexpr double baseTempo = 70.0;
    SampleBuilder sample(sampleRate, 12.0, 1);
    const auto beat = 60.0 / baseTempo;
    for (int index = 0; index * beat < 12.0; ++index)
    {
        const auto time = index * beat;
        addEvent(sample, time, 0, EventType::kick, index % 2 == 0 ? 0.9f : 0.65f);
        addEvent(sample, time + beat * 0.5, 0, EventType::snare, 0.48f,
                 static_cast<std::uint32_t>(index + 71));
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderDoubleTimeAmbiguity(double sampleRate)
{
    constexpr double fastTempo = 180.0;
    SampleBuilder sample(sampleRate, 10.0, 1);
    const auto fastBeat = 60.0 / fastTempo;
    for (int index = 0; index * fastBeat < 10.0; ++index)
    {
        const auto time = index * fastBeat;
        addEvent(sample, time, 0, EventType::kick, index % 6 == 0 ? 0.9f : 0.62f);
        if (index % 2 == 1)
            addEvent(sample, time, 0, EventType::snare, 0.42f,
                     static_cast<std::uint32_t>(index + 811));
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderMelodicLoop(double sampleRate, double bpm)
{
    constexpr double duration = 12.0;
    SampleBuilder sample(sampleRate, duration, 1);
    const auto eighth = 30.0 / bpm;
    for (int index = 0; index * eighth < duration; ++index)
    {
        const auto time = index * eighth;
        const auto accent = index % 2 == 0 ? 0.78f : 0.36f;
        addEvent(sample, time, 0, EventType::pluck, accent,
                 static_cast<std::uint32_t>(index + 901));
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderIrregular(double sampleRate)
{
    constexpr double duration = 9.0;
    SampleBuilder sample(sampleRate, duration, 1);
    constexpr double events[] { 0.2, 0.91, 1.25, 2.66, 3.1, 4.55, 5.0, 7.2, 7.51, 8.6 };
    for (size_t index = 0; index < std::size(events); ++index)
        addEvent(sample, events[index], 0,
                 index % 2 == 0 ? EventType::kick : EventType::snare,
                 index % 3 == 0 ? 0.9f : 0.55f, static_cast<std::uint32_t>(index + 17));
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderNoise(double sampleRate)
{
    constexpr double duration = 8.0;
    SampleBuilder sample(sampleRate, duration, 1);
    auto& audio = sample.audio;
    std::uint32_t state = 0x12345678u;
    auto* data = audio.getWritePointer(0);
    for (int frame = 0; frame < audio.getNumSamples(); ++frame)
    {
        state = 1664525u * state + 1013904223u;
        const auto signedValue = static_cast<std::int32_t>(state);
        data[frame] = static_cast<float>(static_cast<double>(signedValue)
            / static_cast<double>(std::numeric_limits<std::int32_t>::max()) * 0.18);
    }
    return std::move(sample).finish();
}

std::unique_ptr<SampleStorage> renderVocalFragment(double sampleRate)
{
    SampleBuilder sample(sampleRate, 6.0, 1);
    constexpr double syllables[] { 0.2, 0.54, 1.38, 1.72, 2.81, 3.05, 4.32, 4.71, 5.56 };
    for (size_t index = 0; index < std::size(syllables); ++index)
        addEvent(sample, syllables[index], 0, EventType::vocal,
                 index % 3 == 0 ? 0.66f : 0.42f, static_cast<std::uint32_t>(index + 1201));
    return std::move(sample).finish();
}

BpmAnalysis analyze(BPMAnalyzer& analyzer, const SampleStorage& sample)
{
    AnalysisCancellationToken cancellation;
    const auto output = analyzer.analyze(sample, cancellation);
    if (!output.payload)
        throw std::runtime_error(output.error.toStdString());
    const auto* bpm = std::get_if<BpmAnalysis>(&*output.payload);
    if (bpm == nullptr)
        throw std::runtime_error("BPMAnalyzer returned a non-BPM payload.");
    return *bpm;
}

bool candidateNear(const BpmAnalysis& result, double expected, double tolerancePercent = 2.5)
{
    const auto withinTolerance = [expected, tolerancePercent](double actual)
    {
        return std::abs(actual - expected) <= expected * tolerancePercent / 100.0;
    };
    if (result.value && withinTolerance(*result.value))
        return true;
    return std::any_of(result.alternatives.begin(), result.alternatives.end(),
        [&withinTolerance](const auto& candidate) { return withinTolerance(candidate.value); });
}

class BPMAnalyzerTests final : public juce::UnitTest
{
public:
    BPMAnalyzerTests() : UnitTest("Creative Sampler BPM Analyzer", "Analysis") {}

    void runTest() override
    {
        BPMAnalyzer analyzer;

        beginTest("Kick loop detects 120 BPM with scored octave alternatives");
        {
            auto sample = renderKickLoop(48000.0, 120.0, 10.0, 1, 1);
            const auto result = analyze(analyzer, *sample);
            expect(result.isValid());
            expect(result.value.has_value());
            if (result.value)
                expectWithinAbsoluteError(*result.value, 120.0, 2.0);
            expect(result.confidence > 0.35f);
            expect(result.score > 0.3f);
            expect(result.octaveAmbiguous);
            expect(candidateNear(result, 60.0, 2.0));
            expect(result.alternatives.size() <= 5);
            for (const auto& candidate : result.alternatives)
            {
                expect(candidate.value >= 50.0 && candidate.value <= 220.0);
                expect(candidate.score >= 0.0f && candidate.score <= 1.0f);
            }
            std::cout << "Kick loop 120 BPM: estimate="
                      << (result.value ? *result.value : 0.0) << ", confidence="
                      << result.confidence << ", score=" << result.score
                      << ", octave ambiguity=" << result.octaveAmbiguous << "\n";
        }

        beginTest("Snare backbeat and electronic 140 BPM loops are estimated");
        {
            auto snare = renderBackbeat(44100.0, 120.0, 10.0);
            const auto snareResult = analyze(analyzer, *snare);
            expect(snareResult.value.has_value());
            expect(candidateNear(snareResult, 120.0));

            auto electronic = renderBackbeat(48000.0, 140.0, 12.0, true);
            const auto electronicResult = analyze(analyzer, *electronic);
            expect(electronicResult.value.has_value());
            expect(candidateNear(electronicResult, 140.0));
        }

        beginTest("Tempo matrix remains accurate over varied rates and sample lengths");
        {
            constexpr double tempi[] { 60.0, 75.0, 90.0, 100.0, 120.0,
                                       128.0, 140.0, 160.0, 180.0, 200.0 };
            constexpr double rates[] { 44100.0, 48000.0, 96000.0 };
            auto primaryErrorTotal = 0.0;
            auto candidateErrorTotal = 0.0;
            auto measuredCount = 0;
            for (size_t index = 0; index < std::size(tempi); ++index)
            {
                const auto rate = rates[index % std::size(rates)];
                const auto duration = index % 2 == 0 ? 10.0 : 7.0;
                auto sample = renderKickLoop(rate, tempi[index], duration, 1,
                                             index % 3 == 0 ? 1 : 0);
                const auto result = analyze(analyzer, *sample);
                expect(result.value.has_value());
                if (result.value)
                {
                    const auto errorPercent = std::abs(*result.value - tempi[index]) / tempi[index] * 100.0;
                    primaryErrorTotal += errorPercent;
                    auto bestCandidateError = errorPercent;
                    for (const auto& candidate : result.alternatives)
                        bestCandidateError = juce::jmin(bestCandidateError,
                            std::abs(candidate.value - tempi[index]) / tempi[index] * 100.0);
                    candidateErrorTotal += bestCandidateError;
                    ++measuredCount;
                    expect(bestCandidateError <= 2.5,
                           "all tempo candidates exceeded 2.5 percent error");
                    if (errorPercent > 2.5)
                        expect(result.octaveAmbiguous,
                               "primary octave interpretation differs without ambiguity flag");
                }
                expect(result.isValid());
            }
            if (measuredCount > 0)
                std::cout << "Tempo matrix mean primary error: "
                          << primaryErrorTotal / measuredCount
                          << "%; best-candidate error: "
                          << candidateErrorTotal / measuredCount << "%\n";
        }

        beginTest("BPMAnalyzer publishes through AnalysisCoordinator for the current Sample identity");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            const auto identity = coordinator.setCurrentSample(sampleId);
            SampleHandle sample(renderKickLoop(48000.0, 128.0, 12.0));
            auto sharedAnalyzer = std::make_shared<BPMAnalyzer>();
            const auto request = coordinator.requestAnalysis(sampleId, std::move(sample), { sharedAnalyzer });
            expect(request.has_value());
            if (request)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (coordinator.latestResult().status == AnalysisStatus::running
                       && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                const auto result = coordinator.latestResult();
                expect(result.identity == *request);
                expect(result.status == AnalysisStatus::completed);
                expect(result.bpm.has_value());
                if (result.bpm && result.bpm->value)
                    expectWithinAbsoluteError(*result.bpm->value, 128.0, 1.0);
            }
            expect(identity.sampleId == sampleId);
            coordinator.shutdown();
        }

        beginTest("Half-time and double-time ambiguity retain octave-related alternatives");
        {
            auto halfTime = renderHalfTimeAmbiguity(48000.0);
            const auto halfResult = analyze(analyzer, *halfTime);
            expect(candidateNear(halfResult, 70.0) || candidateNear(halfResult, 140.0));
            expect(candidateNear(halfResult, 70.0) && candidateNear(halfResult, 140.0));
            expect(halfResult.octaveAmbiguous || halfResult.confidence < 0.75f);

            auto doubleTime = renderDoubleTimeAmbiguity(44100.0);
            const auto doubleResult = analyze(analyzer, *doubleTime);
            expect(candidateNear(doubleResult, 180.0) && candidateNear(doubleResult, 90.0));
            expect(doubleResult.octaveAmbiguous || doubleResult.confidence < 0.75f);
        }

        beginTest("Melodic and stereo loops produce tempo candidates without channel cancellation");
        {
            auto melodic = renderMelodicLoop(44100.0, 128.0);
            const auto melodicResult = analyze(analyzer, *melodic);
            expect(candidateNear(melodicResult, 128.0) || melodicResult.detectionStatus
                   == BpmDetectionStatus::lowConfidence);

            auto stereo = renderKickLoop(96000.0, 100.0, 10.0, 2);
            const auto stereoResult = analyze(analyzer, *stereo);
            expect(candidateNear(stereoResult, 100.0));
        }

        beginTest("Silence, irregular rhythms, noise, and short samples avoid false certainty");
        {
            SampleBuilder silenceBuilder(48000.0, 8.0, 1);
            auto silence = std::move(silenceBuilder).finish();
            const auto silenceResult = analyze(analyzer, *silence);
            expectEquals(static_cast<int>(silenceResult.detectionStatus),
                         static_cast<int>(BpmDetectionStatus::noRhythmicContent));
            expect(!silenceResult.value);

            auto irregular = renderIrregular(44100.0);
            const auto irregularResult = analyze(analyzer, *irregular);
            expect(!irregularResult.value || irregularResult.confidence < 0.7f);
            expect(irregularResult.isValid());

            auto noise = renderNoise(48000.0);
            const auto noiseResult = analyze(analyzer, *noise);
            expect(!noiseResult.value || noiseResult.confidence < 0.7f);
            expect(noiseResult.isValid());

            auto vocal = renderVocalFragment(44100.0);
            const auto vocalResult = analyze(analyzer, *vocal);
            expect(!vocalResult.value || vocalResult.confidence < 0.7f);
            expect(vocalResult.isValid());

            SampleBuilder oneHitBuilder(48000.0, 8.0, 1);
            addEvent(oneHitBuilder, 1.0, 0, EventType::kick, 1.0f);
            auto oneHit = std::move(oneHitBuilder).finish();
            const auto oneHitResult = analyze(analyzer, *oneHit);
            expectEquals(static_cast<int>(oneHitResult.detectionStatus),
                         static_cast<int>(BpmDetectionStatus::insufficientData));
            expect(!oneHitResult.value);

            auto shortLoop = renderKickLoop(48000.0, 120.0, 0.9);
            const auto shortResult = analyze(analyzer, *shortLoop);
            expectEquals(static_cast<int>(shortResult.detectionStatus),
                         static_cast<int>(BpmDetectionStatus::insufficientData));
            expect(!shortResult.value);
        }

        beginTest("Configurable tempo range, manual correction, and cancellation are bounded");
        {
            BPMAnalyzer::Options options;
            options.minimumBpm = 80.0;
            options.maximumBpm = 160.0;
            BPMAnalyzer constrained(options);
            auto sample = renderKickLoop(48000.0, 70.0, 12.0);
            auto result = analyze(constrained, *sample);
            if (result.value)
            {
                expect(*result.value >= 80.0 && *result.value <= 160.0);
                expect(result.setManualOverride(123.4));
                expectWithinAbsoluteError(*result.effectiveValue(), 123.4, 0.0001);
                expect(!result.setManualOverride(200.0));
            }

            AnalysisCancellationToken cancelled;
            cancelled.cancel();
            const auto output = constrained.analyze(*sample, cancelled);
            expect(output.payload == std::nullopt);
        }
    }
};
}

juce::UnitTest* getBPMAnalyzerTest()
{
    static BPMAnalyzerTests test;
    return &test;
}

int runBPMAnalyzerBenchmarks()
{
    BPMAnalyzer analyzer;
    for (const auto duration : { 2.0, 10.0, 60.0, 180.0 })
    {
        auto sample = renderKickLoop(48000.0, 120.0, duration, 1, 1);
        AnalysisCancellationToken cancellation;
        const auto start = std::chrono::steady_clock::now();
        const auto cpuStart = std::clock();
        const auto output = analyzer.analyze(*sample, cancellation);
        const auto cpuEnd = std::clock();
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        const auto cpuElapsed = static_cast<double>(cpuEnd - cpuStart) * 1000.0
            / static_cast<double>(CLOCKS_PER_SEC);
        const auto estimatedFeatureMemory = static_cast<std::size_t>(std::ceil(duration * 100.0))
            * sizeof(float);
        std::cout << "BPM analysis duration=" << duration << " s, elapsed=" << elapsed
                  << " ms, CPU=" << cpuElapsed
                  << " ms, estimated main feature buffer=" << estimatedFeatureMemory << " bytes, status=";
        if (!output.payload)
            std::cout << "failed: " << output.error << "\n";
        else
        {
            const auto& result = std::get<BpmAnalysis>(*output.payload);
            std::cout << static_cast<int>(result.detectionStatus) << ", bpm="
                      << (result.value ? *result.value : 0.0) << "\n";
        }
    }
    return 0;
}
