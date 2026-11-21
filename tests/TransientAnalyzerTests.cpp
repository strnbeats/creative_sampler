#include "TransientAnalyzerTests.h"
#include "../src/analysis/TransientAnalyzer.h"
#include "../src/analysis/AnalysisCoordinator.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>

namespace
{
struct Fixture
{
    std::shared_ptr<const SampleStorage> sample;
    std::vector<juce::int64> expected;
};

Fixture makePulseFixture(double rate, double duration, std::vector<double> seconds,
                         int channels = 1, bool highFrequency = true)
{
    const auto count = static_cast<int>(std::round(rate * duration));
    juce::AudioBuffer<float> audio(channels, count);
    audio.clear();
    std::vector<juce::int64> expected;
    for (size_t event = 0; event < seconds.size(); ++event)
    {
        const auto start = static_cast<int>(std::round(seconds[event] * rate));
        expected.push_back(start);
        const auto length = static_cast<int>(rate * 0.14);
        for (int i = 0; i < length && start + i < count; ++i)
        {
            const auto envelope = std::exp(-static_cast<double>(i) / (rate * 0.012))
                * (i < length - 1 ? 1.0 : 0.0);
            const auto phase = static_cast<double>(event) * 0.73;
            const auto low = std::sin(2.0 * juce::MathConstants<double>::pi * 85.0 * i / rate + phase);
            const auto high = highFrequency
                ? 0.35 * std::sin(2.0 * juce::MathConstants<double>::pi * 2700.0 * i / rate + phase) : 0.0;
            const auto amplitude = (event == 3 ? 0.09 : 0.8) * envelope * (low + high);
            for (int ch = 0; ch < channels; ++ch)
                audio.addSample(ch, start + i, static_cast<float>(amplitude * (ch == 1 ? -1.0 : 1.0)));
        }
    }
    SampleMetadata metadata;
    metadata.fileName = "transient-fixture.wav";
    metadata.sampleRate = rate;
    metadata.channelCount = channels;
    metadata.sampleCount = count;
    return { std::make_shared<const SampleStorage>(std::move(audio), metadata), std::move(expected) };
}

std::shared_ptr<const SampleStorage> makeSignal(double rate, double duration,
    const std::function<float(int, int)>& generator, int channels = 1)
{
    const auto count = static_cast<int>(std::round(rate * duration));
    juce::AudioBuffer<float> audio(channels, count);
    for (int channel = 0; channel < channels; ++channel)
        for (int i = 0; i < count; ++i)
            audio.setSample(channel, i, generator(i, channel));
    SampleMetadata metadata;
    metadata.fileName = "negative-fixture.wav";
    metadata.sampleRate = rate;
    metadata.channelCount = channels;
    metadata.sampleCount = count;
    return std::make_shared<const SampleStorage>(std::move(audio), metadata);
}

struct NoiseAttack
{
    double time;
    double level;
    double rise;
    bool broadband;
    double pitch = 610.0;
};

enum class NoiseEnvelope { steady, fade, dynamic, swells };

Fixture makeNoiseFixture(double duration, double level, unsigned seed, bool pink, NoiseEnvelope envelope,
                         const std::vector<NoiseAttack>& attacks = {})
{
    constexpr double rate = 48000.0;
    std::mt19937 rng(seed);
    std::mt19937 attackRng(seed ^ 0x9e3779b9U);
    std::normal_distribution<double> normal;
    std::normal_distribution<double> attackNoise;
    // Сумма независимых фильтрованных полос приближает наклон 1/f.
    constexpr std::array poles { 0.997, 0.967, 0.72 };
    std::array<double, poles.size()> bands {};
    std::vector<juce::int64> expected;
    for (const auto& attack : attacks)
        expected.push_back(std::llround(attack.time * rate));
    auto sample = makeSignal(rate, duration, [&](int i, int) {
        const auto t = i / rate;
        auto noise = normal(rng);
        if (pink)
        {
            noise *= 0.5;
            for (size_t band = 0; band < poles.size(); ++band)
            {
                bands[band] = poles[band] * bands[band]
                    + std::sqrt(1.0 - poles[band] * poles[band]) * normal(rng);
                noise += bands[band];
            }
            noise /= std::sqrt(3.25);
        }
        double gain = 1.0;
        if (envelope == NoiseEnvelope::fade)
            gain = std::pow(std::sin(std::numbers::pi * t / duration), 2);
        if (envelope == NoiseEnvelope::dynamic)
            gain = 0.65 + 0.35 * std::sin(2.0 * std::numbers::pi * 0.7 * t);
        if (envelope == NoiseEnvelope::swells)
            for (double center : { 0.7, 2.2, 3.1 })
                gain += 1.3 * std::exp(-std::pow((t - center) / 0.09, 2));
        auto value = level * gain * noise;
        for (size_t event = 0; event < attacks.size(); ++event)
        {
            const auto offset = i - expected[event];
            if (offset < 0 || offset > 4800)
                continue;
            const auto& attack = attacks[event];
            if (attack.rise < 0.0)
            {
                if (offset == 0)
                    value += attack.level;
                continue;
            }
            const auto dt = static_cast<double>(offset) / rate;
            const auto attackGain = attack.rise > 0.0 ? std::min(1.0, dt / attack.rise) : 1.0;
            const auto carrier = 0.65 * std::cos(2.0 * std::numbers::pi * attack.pitch * dt)
                + 0.35 * std::cos(2.0 * std::numbers::pi * (attack.pitch + 2540.0) * dt)
                + (attack.broadband ? 0.7 * attackNoise(attackRng) * std::exp(-dt / 0.006) : 0.0);
            value += attack.level * attackGain * std::exp(-dt / 0.012) * carrier;
        }
        return static_cast<float>(value);
    });
    return { std::move(sample), std::move(expected) };
}

struct CalibrationMetrics
{
    size_t hits = 0;
    size_t falsePositives = 0;
    double meanErrorMs = 0;
    double maxErrorMs = 0;
    float maxTrueConfidence = 0;
    float maxFalseConfidence = 0;
};

CalibrationMetrics measureFixture(const char* name, const Fixture& fixture,
                                  const TransientAnalysis& result, double toleranceMs)
{
    CalibrationMetrics metrics;
    const auto rate = fixture.sample->metadata().sampleRate;
    const auto tolerance = std::llround(toleranceMs * rate / 1000.0);
    std::vector<bool> matched(result.positions.size());
    std::vector<float> trueConfidence, falseConfidence;
    for (const auto onset : fixture.expected)
    {
        auto nearestDistance = tolerance + 1;
        auto nearest = result.positions.size();
        for (size_t i = 0; i < result.positions.size(); ++i)
        {
            const auto distance = std::abs(result.positions[i].samplePosition - onset);
            if (!matched[i] && distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = i;
            }
        }
        if (nearest == result.positions.size())
            continue;
        matched[nearest] = true;
        ++metrics.hits;
        const auto errorMs = static_cast<double>(nearestDistance) * 1000.0 / rate;
        metrics.meanErrorMs += errorMs;
        metrics.maxErrorMs = std::max(metrics.maxErrorMs, errorMs);
        trueConfidence.push_back(result.positions[nearest].confidence.value_or(0.0f));
    }
    for (size_t i = 0; i < result.positions.size(); ++i)
        if (!matched[i])
            falseConfidence.push_back(result.positions[i].confidence.value_or(0.0f));
    metrics.falsePositives = falseConfidence.size();
    if (metrics.hits > 0)
        metrics.meanErrorMs /= static_cast<double>(metrics.hits);
    std::cout << "Calibration " << name << " hits=" << metrics.hits << "/" << fixture.expected.size()
              << " FP=" << metrics.falsePositives << " FP/s="
              << static_cast<double>(metrics.falsePositives) / result.durationSeconds()
              << " MAEms=" << metrics.meanErrorMs << " maxMs=" << metrics.maxErrorMs;
    for (auto* values : { &trueConfidence, &falseConfidence })
    {
        std::sort(values->begin(), values->end());
        if (!values->empty())
            std::cout << (values == &trueConfidence ? " TPconf=" : " FPconf=")
                      << values->front() << "/" << (*values)[values->size() / 2] << "/" << values->back();
    }
    std::cout << "\n";
    if (!fixture.expected.empty())
        for (size_t i = 0; i < result.positions.size(); ++i)
            std::cout << "  " << name << (matched[i] ? " hit@" : " FP@")
                      << result.positions[i].timeSeconds << '\n';
    metrics.maxTrueConfidence = trueConfidence.empty() ? 0.0f : trueConfidence.back();
    metrics.maxFalseConfidence = falseConfidence.empty() ? 0.0f : falseConfidence.back();
    return metrics;
}

class TransientAnalyzerTests final : public juce::UnitTest
{
public:
    TransientAnalyzerTests() : UnitTest("Creative Sampler Transient Analyzer", "Analysis") {}

    void runTest() override
    {
        TransientAnalyzer analyzer;
        AnalysisCancellationToken cancellation;
        beginTest("Stationary and changing noise with labelled attacks");
        const auto measure = [&](const char* name, const Fixture& fixture, double tolerance) {
            const auto output = analyzer.analyze(*fixture.sample, cancellation);
            expect(output.payload && std::holds_alternative<TransientAnalysis>(*output.payload));
            return output.payload && std::holds_alternative<TransientAnalysis>(*output.payload)
                ? measureFixture(name, fixture, std::get<TransientAnalysis>(*output.payload), tolerance)
                : CalibrationMetrics {};
        };
        using Envelope = NoiseEnvelope;
        const auto verifyNoise = [&](const char* name, const Fixture& fixture, size_t allowed) {
            const auto metrics = measure(name, fixture, 12);
            expect(metrics.falsePositives <= allowed, name);
            expect(metrics.maxFalseConfidence < 0.75f, name);
        };
        verifyNoise("white-quiet", makeNoiseFixture(5.0, 0.003, 313, false, Envelope::steady), 0);
        verifyNoise("white-loud", makeNoiseFixture(5.0, 0.2, 221, false, Envelope::steady), 0);
        verifyNoise("pink-quiet", makeNoiseFixture(5.0, 0.004, 851, true, Envelope::steady), 1);
        verifyNoise("pink-loud", makeNoiseFixture(5.0, 0.18, 97, true, Envelope::steady), 1);
        verifyNoise("pink-long", makeNoiseFixture(30.0, 0.045, 1831, true, Envelope::steady), 2);
        verifyNoise("pink-fades", makeNoiseFixture(5.0, 0.1, 2203, true, Envelope::fade), 1);
        verifyNoise("pink-dynamic", makeNoiseFixture(5.0, 0.08, 319, true, Envelope::dynamic), 1);
        verifyNoise("pink-smooth-swells", makeNoiseFixture(5.0, 0.04, 1119, true, Envelope::swells), 1);
        verifyNoise("pink-long-other-seed",
                    makeNoiseFixture(30.0, 0.11, 73393, true, Envelope::dynamic), 3);

        beginTest("Keep meaningful attacks over noise and reject unmatched markers");
        const auto masked = measure("quiet-on-loud", makeNoiseFixture(3.0, 0.09, 471, true, Envelope::steady,
            { { 1.0, 0.15, 0.0, false } }), 12);
        expect(masked.falsePositives == 0);

        const auto quietPluck = measure("quiet-bright-pluck",
            makeNoiseFixture(3.0, 0.09, 5327, true, Envelope::steady,
                { { 1.0, 0.10, 0.001, false, 6500.0 } }), 12);
        expectEquals(static_cast<int>(quietPluck.hits), 1);
        expect(quietPluck.falsePositives == 0);

        const auto loud = measure("loud-on-quiet", makeNoiseFixture(3.0, 0.012, 623, true, Envelope::steady,
            { { 1.0, 0.48, 0.0, true } }), 12);
        expectEquals(static_cast<int>(loud.hits), 1);
        expect(loud.falsePositives == 0);
        expect(loud.maxTrueConfidence > 0.80f);
        const auto mixed = measure("mixed-changing-bed", makeNoiseFixture(5.0, 0.065, 957, true, Envelope::dynamic,
            { { 0.45, 0.40, -1.0, false }, { 1.13, 0.34, 0.008, false },
              { 2.22, 0.38, 0.002, true }, { 2.257, 0.24, 0.0, true },
              { 2.325, 0.28, 0.0, true }, { 4.11, 0.20, 0.0, false } }), 14);
        expectEquals(static_cast<int>(mixed.hits), 6);
        expect(mixed.falsePositives == 0);
        beginTest("Transient positions are sample-accurate, ordered, and deterministic across rates and stereo phase");
        for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto fixture = makePulseFixture(rate, 2.0, { 0.25, 0.75, 1.22, 1.245, 1.72 },
                                            rate == 48000.0 ? 1 : 2);
            auto first = analyzer.analyze(*fixture.sample, cancellation);
            auto second = analyzer.analyze(*fixture.sample, cancellation);
            expect(first.payload.has_value());
            if (!first.payload || !std::holds_alternative<TransientAnalysis>(*first.payload))
                continue;
            const auto& result = std::get<TransientAnalysis>(*first.payload);
            expect(result.detectionStatus != TransientDetectionStatus::insufficientData);
            expectEquals(static_cast<int>(result.positions.size()), static_cast<int>(fixture.expected.size()));
            expect(second.payload.has_value());
            if (second.payload && std::holds_alternative<TransientAnalysis>(*second.payload))
            {
                const auto& repeated = std::get<TransientAnalysis>(*second.payload);
                expectEquals(static_cast<int>(result.positions.size()), static_cast<int>(repeated.positions.size()));
                const auto matches = std::min(result.positions.size(), fixture.expected.size());
                double totalError = 0.0;
                double maxError = 0.0;
                double confidenceTotal = 0.0;
                for (size_t i = 0; i < matches; ++i)
                {
                    const auto error = std::abs(result.positions[i].samplePosition - fixture.expected[i]);
                    totalError += static_cast<double>(error);
                    maxError = std::max(maxError, static_cast<double>(error));
                    expect(static_cast<double>(error) <= rate * 0.012);
                    expectEquals(static_cast<int>(result.positions[i].samplePosition),
                                 static_cast<int>(repeated.positions[i].samplePosition));
                    expect(result.positions[i].confidence.has_value());
                    confidenceTotal += result.positions[i].confidence.value_or(0.0f);
                }
                if (matches == fixture.expected.size() && matches > 3)
                    expect(result.positions[3].strength < result.positions[2].strength);
                if (matches > 0)
                    std::cout << "Transient timing @ " << rate << " Hz: mean "
                              << totalError / static_cast<double>(matches) << " samples, max "
                              << maxError << " samples (" << maxError * 1000.0 / rate << " ms)\n";
                if (matches > 0)
                    std::cout << "Transient mean heuristic confidence @ " << rate << " Hz: "
                              << confidenceTotal / static_cast<double>(matches) << "\n";
            }
        }

        beginTest("A single pluck-like one-shot returns one event without requiring a loop");
        auto oneShot = makePulseFixture(44100.0, 1.0, { 0.42 }, 1, true);
        const auto oneShotOutput = analyzer.analyze(*oneShot.sample, cancellation);
        expect(oneShotOutput.payload.has_value());
        if (oneShotOutput.payload && std::holds_alternative<TransientAnalysis>(*oneShotOutput.payload))
        {
            const auto& result = std::get<TransientAnalysis>(*oneShotOutput.payload);
            expectEquals(static_cast<int>(result.positions.size()), 1);
            if (!result.positions.empty())
                expect(std::abs(result.positions.front().timeSeconds - 0.42) < 0.006);
        }

        beginTest("Silence, DC, and gentle level changes do not create attack markers");
        const auto quietSignals = {
            makeSignal(48000.0, 1.0, [](int, int) { return 0.0f; }),
            makeSignal(48000.0, 1.0, [](int, int) { return 0.25f; }),
            makeSignal(48000.0, 1.0, [](int i, int) {
                const auto t = static_cast<double>(i) / 48000.0;
                return static_cast<float>(0.35 * t * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            }),
            makeSignal(48000.0, 1.0, [](int i, int) {
                const auto t = static_cast<double>(i) / 48000.0;
                return static_cast<float>((1.0 - t) * 0.35
                    * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            }),
            makeSignal(48000.0, 1.0, [](int i, int) {
                const auto t = static_cast<double>(i) / 48000.0;
                const auto phase = 2.0 * juce::MathConstants<double>::pi
                    * (80.0 * t + 0.5 * 700.0 * t * t);
                return static_cast<float>(0.2 * std::sin(phase));
            }),
            makeSignal(48000.0, 1.0, [](int i, int) {
                const auto t = static_cast<double>(i) / 48000.0;
                const auto amplitude = 0.25 * (1.0 + 0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * 3.0 * t));
                return static_cast<float>(amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * t));
            })
        };
        int falseEvents = 0;
        size_t quietIndex = 0;
        for (const auto& sample : quietSignals)
        {
            const auto output = analyzer.analyze(*sample, cancellation);
            expect(output.payload.has_value());
            if (output.payload && std::holds_alternative<TransientAnalysis>(*output.payload))
            {
                const auto& result = std::get<TransientAnalysis>(*output.payload);
                falseEvents += static_cast<int>(result.positions.size());
                std::cout << "Negative fixture " << quietIndex << ": " << result.positions.size()
                          << " detected event(s)\n";
                expect(result.positions.empty());
                expect(isValidAnalysisPayload(*output.payload, sample->metadata().durationSeconds()));
            }
            ++quietIndex;
        }
        std::cout << "Transient negative fixtures: " << falseEvents << " false events / "
                  << quietSignals.size() << " tonal/silence fixtures\n";

        beginTest("Noise yields bounded false positives and empty short inputs are explicitly insufficient");
        std::mt19937 rng(112233);
        std::normal_distribution<float> normal(0.0f, 0.12f);
        auto noise = makeSignal(48000.0, 2.0, [&rng, &normal](int, int) { return normal(rng); });
        auto noiseResult = analyzer.analyze(*noise, cancellation);
        expect(noiseResult.payload.has_value());
        if (noiseResult.payload && std::holds_alternative<TransientAnalysis>(*noiseResult.payload))
        {
            const auto& result = std::get<TransientAnalysis>(*noiseResult.payload);
            std::cout << "White noise false events: " << result.positions.size() << " / 2 s\n";
            expect(result.positions.empty());
        }
        std::mt19937 pinkRng(998877);
        std::normal_distribution<float> pinkNormal(0.0f, 0.1f);
        float pinkState = 0.0f;
        auto pinkNoise = makeSignal(48000.0, 2.0, [&pinkRng, &pinkNormal, &pinkState](int, int) {
            pinkState = 0.97f * pinkState + 0.03f * pinkNormal(pinkRng);
            return pinkState;
        });
        const auto pinkResult = analyzer.analyze(*pinkNoise, cancellation);
        expect(pinkResult.payload.has_value());
        if (pinkResult.payload && std::holds_alternative<TransientAnalysis>(*pinkResult.payload))
        {
            const auto& pinkAnalysis = std::get<TransientAnalysis>(*pinkResult.payload);
            const auto falseEventsInPink = pinkAnalysis.positions.size();
            std::cout << "Pink noise false events: " << falseEventsInPink << " / 2 s\n";
            for (const auto& event : pinkAnalysis.positions)
                std::cout << "  pink event t=" << event.timeSeconds << " confidence="
                          << event.confidence.value_or(0.0f) << "\n";
            expect(falseEventsInPink == 0);
        }
        auto shortSample = makeSignal(48000.0, 0.02, [](int, int) { return 0.0f; });
        auto shortResult = analyzer.analyze(*shortSample, cancellation);
        expect(shortResult.payload.has_value());
        if (shortResult.payload && std::holds_alternative<TransientAnalysis>(*shortResult.payload))
            expectEquals(static_cast<int>(std::get<TransientAnalysis>(*shortResult.payload).detectionStatus),
                         static_cast<int>(TransientDetectionStatus::insufficientData));

        beginTest("Empty input is insufficient and an event cap reports truncation explicitly");
        auto empty = makeSignal(48000.0, 0.0, [](int, int) { return 0.0f; });
        const auto emptyOutput = analyzer.analyze(*empty, cancellation);
        expect(emptyOutput.payload.has_value());
        if (emptyOutput.payload && std::holds_alternative<TransientAnalysis>(*emptyOutput.payload))
            expectEquals(static_cast<int>(std::get<TransientAnalysis>(*emptyOutput.payload).detectionStatus),
                         static_cast<int>(TransientDetectionStatus::insufficientData));
        auto dense = makePulseFixture(48000.0, 3.0, { 0.2, 0.5, 0.8, 1.1, 1.4, 1.7, 2.0, 2.3, 2.6 });
        TransientAnalyzer capped({ 0.012, 0.001, 0.012, 3 });
        const auto cappedOutput = capped.analyze(*dense.sample, cancellation);
        expect(cappedOutput.payload.has_value());
        if (cappedOutput.payload && std::holds_alternative<TransientAnalysis>(*cappedOutput.payload))
        {
            const auto& cappedResult = std::get<TransientAnalysis>(*cappedOutput.payload);
            expectEquals(static_cast<int>(cappedResult.positions.size()), 3);
            expectEquals(static_cast<int>(cappedResult.totalEventCount), 9);
            expectEquals(static_cast<int>(cappedResult.detectionStatus),
                         static_cast<int>(TransientDetectionStatus::truncated));
            expect(isValidAnalysisPayload(*cappedOutput.payload, dense.sample->metadata().durationSeconds()));
            auto invalidStatus = cappedResult;
            invalidStatus.detectionStatus = static_cast<TransientDetectionStatus>(99);
            expect(!isValidAnalysisPayload(AnalysisPayload { invalidStatus },
                                           dense.sample->metadata().durationSeconds()));
        }
        auto malformedAudio = makeSignal(48000.0, 0.25, [](int i, int) {
            return i == 3000 ? std::numeric_limits<float>::quiet_NaN() : 0.0f;
        });
        expect(analyzer.analyze(*malformedAudio, cancellation).error.isNotEmpty());

        beginTest("Coordinator publishes real transient output on the background worker");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            const auto fixture = makePulseFixture(48000.0, 1.5, { 0.2, 0.6, 1.0 });
            coordinator.setCurrentSample(sampleId);
            const auto request = coordinator.requestAnalysis(sampleId, fixture.sample,
                { std::make_shared<TransientAnalyzer>() });
            expect(request.has_value());
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (coordinator.latestResult().status == AnalysisStatus::running
                   && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto published = coordinator.latestResult();
            expectEquals(static_cast<int>(published.status), static_cast<int>(AnalysisStatus::completed));
            expect(published.identity == *request);
            expect(published.transients.has_value());
            expect(published.isValid());
        }

        beginTest("Cancellation is observed during long analysis");
        auto longSample = makeSignal(44100.0, 180.0, [](int i, int) {
            return static_cast<float>(0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * i / 44100.0));
        });
        AnalysisCancellationToken token;
        AnalyzerOutput cancelled;
        std::chrono::steady_clock::time_point cancellationTime;
        std::thread worker([&] { cancelled = analyzer.analyze(*longSample, token); });
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        cancellationTime = std::chrono::steady_clock::now();
        token.cancel();
        worker.join();
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - cancellationTime).count();
        std::cout << "Transient cancellation response: " << static_cast<double>(latency) / 1000.0 << " ms\n";
        expect(cancelled.error.isNotEmpty());
        expect(latency < 1000000);
    }
};
}

juce::UnitTest* getTransientAnalyzerTest()
{
    static TransientAnalyzerTests test;
    return &test;
}

int runTransientAnalyzerBenchmarks()
{
    TransientAnalyzer analyzer;
    AnalysisCancellationToken cancellation;
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
        for (const auto duration : { 2.0, 10.0, 60.0, 180.0 })
        {
            auto sample = makeSignal(rate, duration, [rate](int i, int) {
                const auto t = static_cast<double>(i) / rate;
                const auto phase = std::fmod(t, 0.5);
                const auto envelope = phase < 0.05 ? std::exp(-phase * 500.0) : 0.0;
                return static_cast<float>(envelope * std::sin(2.0 * juce::MathConstants<double>::pi * 70.0 * t));
            });
            const auto start = std::chrono::steady_clock::now();
            const auto cpuStart = std::clock();
            const auto result = analyzer.analyze(*sample, cancellation);
            const auto cpuSeconds = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const auto count = result.payload && std::holds_alternative<TransientAnalysis>(*result.payload)
                ? std::get<TransientAnalysis>(*result.payload).positions.size() : 0;
            const auto hop = std::llround(rate * 0.001);
            const auto sampleCount = sample->metadata().sampleCount;
            const auto frames = static_cast<size_t>((sampleCount + hop - 1) / hop);
            // 4 признаки, 8 полос, 2 префиксных массива и ёмкость списка кандидатов.
            constexpr size_t candidateBytes = 24; // size_t, int64, float и выравнивание (macOS 64-bit).
            const auto workingBytes = frames * (12 * sizeof(float) + 2 * sizeof(double))
                + 2 * sizeof(double) + std::min<size_t>(65536, frames / 8 + 1) * candidateBytes;
            std::cout << "Transient benchmark " << rate << " Hz, " << duration << " s: wall="
                      << elapsed << " s, CPU=" << cpuSeconds << " s, events=" << count
                      << ", feature arrays~" << workingBytes << " bytes\n";
        }
    return 0;
}
