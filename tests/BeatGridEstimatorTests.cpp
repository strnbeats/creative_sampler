#include "BeatGridEstimatorTests.h"
#include "analysis/BPMAnalyzer.h"
#include "analysis/AnalysisTypes.h"
#include "analysis/TransientAnalyzer.h"
#include "auto_slicing/BeatGridEstimator.h"
#include "sample/SampleStorage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
using namespace auto_slicing::beat_grid;

constexpr std::uint64_t sampleId = 17;
constexpr std::uint64_t revision = 4;
constexpr std::uint64_t generation = 9;
constexpr double rate = 48000.0;
constexpr Frame frames = 8 * 48000;

BpmAnalysis bpmResult(double bpm = 120.0, float confidence = 0.92f)
{
    BpmAnalysis result;
    result.value = bpm;
    result.confidence = confidence;
    result.score = 0.88f;
    result.detectionStatus = BpmDetectionStatus::detected;
    return result;
}

TransientAnalysis transientResult(double sampleRate = rate, Frame sampleFrames = frames)
{
    TransientAnalysis result;
    result.sampleRate = sampleRate;
    result.sampleCount = sampleFrames;
    result.totalEventCount = 0;
    result.detectionStatus = TransientDetectionStatus::insufficientData;
    return result;
}

void addEvent(TransientAnalysis& analysis, Frame frame, float strength = 0.9f,
              std::optional<float> confidence = 0.9f, float flux = 0.8f)
{
    const auto denominator = static_cast<double>(std::max<Frame>(1, analysis.sampleCount - 1));
    analysis.positions.push_back({ static_cast<double>(frame) / analysis.sampleRate,
        strength, confidence, frame, static_cast<double>(frame) / denominator, flux });
    analysis.totalEventCount = analysis.positions.size();
    analysis.detectionStatus = TransientDetectionStatus::detected;
}

AnalysisResult analysisResult(BpmAnalysis bpm, TransientAnalysis transients)
{
    AnalysisResult result;
    result.identity = { sampleId, generation };
    result.status = AnalysisStatus::completed;
    result.bpm = std::move(bpm);
    result.transients = std::move(transients);
    return result;
}

Result estimate(const AnalysisResult& analysis, Frame sampleFrames = frames,
                double sampleRate = rate, Config config = {})
{
    return Estimator::estimate({ sampleId, revision, generation, sampleFrames, sampleRate, &analysis }, config);
}

class BeatGridEstimatorTests final : public juce::UnitTest
{
public:
    BeatGridEstimatorTests() : UnitTest("Beat Grid Estimator", "Auto Slicing") {}

    void runTest() override
    {
        beginTest("Ideal beat-aligned attacks produce a reliable grid and exact beat positions");
        auto transients = transientResult();
        for (int beat = 0; beat < 15; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        auto result = estimate(analysisResult(bpmResult(), transients));
        expect(!result.error && result.grid.reliability == Reliability::reliable);
        expect(result.grid.bpm && std::abs(*result.grid.bpm - 120.0) < 1.0e-9);
        expect(result.grid.phaseOffsetFrames && std::abs(*result.grid.phaseOffsetFrames) < 1.0);
        expect(result.grid.phaseConfidence >= 0.95f);
        if (result.grid.phaseOffsetFrames)
        {
            expect(Estimator::frameAtBeat(result.grid, 0).value_or(-1) == 0);
            expect(Estimator::frameAtBeat(result.grid, 1).value_or(-1) == 24000);
            expect(Estimator::frameAtBeat(result.grid, 2).value_or(-1) == 48000);
            expect(!Estimator::frameAtBeat(result.grid, -1));
        }

        beginTest("Non-zero phase uses source PCM frames and remains stable with strength variation");
        transients = transientResult();
        for (int beat = 0; beat < 15; ++beat)
            addEvent(transients, 6000 + static_cast<Frame>(beat * 24000),
                     beat % 3 == 0 ? 1.0f : 0.35f);
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reliability == Reliability::reliable);
        expect(result.grid.phaseOffsetFrames
            && std::abs(*result.grid.phaseOffsetFrames - 6000.0) < 180.0);
        expect(result.grid.phaseConfidence >= 0.60f);

        beginTest("Missing beats and weak extra attacks do not pull a clear phase");
        transients = transientResult();
        for (int beat = 0; beat < 15; ++beat)
            if (beat != 4 && beat != 9)
                addEvent(transients, 3600 + static_cast<Frame>(beat * 24000),
                         beat % 2 == 0 ? 0.95f : 0.55f);
        for (int beat = 0; beat < 12; ++beat)
            addEvent(transients, 3600 + static_cast<Frame>(beat * 24000) + 12000,
                     0.025f, 0.0f, 0.0f);
        std::sort(transients.positions.begin(), transients.positions.end(), [](const auto& left, const auto& right)
        {
            return left.samplePosition < right.samplePosition;
        });
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reliability == Reliability::reliable);
        expect(result.grid.phaseOffsetFrames
            && std::abs(*result.grid.phaseOffsetFrames - 3600.0) < 240.0);

        beginTest("Competing half-beat phases are marked ambiguous instead of choosing one");
        transients = transientResult();
        for (int halfBeat = 0; halfBeat < 30; ++halfBeat)
            addEvent(transients, static_cast<Frame>(halfBeat * 12000));
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reliability == Reliability::ambiguous);
        expect(result.grid.reason == Reason::competingPhases);
        expect(!result.grid.phaseOffsetFrames);

        beginTest("A phase-coherent alternative may win; tied octave tempos remain ambiguous");
        transients = transientResult();
        for (int beat = 0; beat < 15; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        auto alternatives = bpmResult(60.0, 0.88f);
        alternatives.octaveAmbiguous = true;
        alternatives.alternatives.push_back({ 120.0, 0.91f, 0.87f });
        result = estimate(analysisResult(alternatives, transients));
        expect(result.grid.reliability == Reliability::reliable,
               juce::String("phase evidence should resolve octave ambiguity; reason=")
                   + juce::String(static_cast<int>(result.grid.reason))
                   + ", selected BPM=" + juce::String(result.grid.bpm.value_or(-1.0))
                   + ", phase confidence=" + juce::String(result.grid.phaseConfidence));
        expect(result.grid.bpm && std::abs(*result.grid.bpm - 120.0) < 1.0e-9);
        expect(result.diagnostics.tempoWasResolvedByPhase);

        transients = transientResult();
        for (int beat = 0; beat < 8; ++beat)
            addEvent(transients, 6000 + static_cast<Frame>(beat * 48000));
        alternatives = bpmResult(120.0, 0.90f);
        alternatives.octaveAmbiguous = true;
        alternatives.alternatives.push_back({ 60.0, 0.88f, 0.85f });
        result = estimate(analysisResult(alternatives, transients));
        expect(result.grid.reliability == Reliability::ambiguous);
        expect(result.grid.reason == Reason::competingTempos);
        expect(!result.grid.phaseOffsetFrames);

        beginTest("Irregular attacks, short material, silence and absent phase evidence are rejected clearly");
        transients = transientResult();
        Frame irregular = 2400;
        for (const auto delta : { 8000, 31000, 17000, 43000, 9000, 36000, 15000,
                                  52000, 11000, 28000, 19000, 47000, 7000, 32000 })
        {
            irregular += delta;
            if (irregular < frames)
                addEvent(transients, irregular);
        }
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reliability != Reliability::reliable);
        expect(!result.grid.phaseOffsetFrames);

        transients = transientResult(rate, 60000);
        for (int beat = 0; beat < 3; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        result = estimate(analysisResult(bpmResult(), transients), 60000);
        expect(result.grid.reliability == Reliability::insufficientData);
        expect(result.grid.reason == Reason::insufficientDuration);

        auto noRhythm = bpmResult();
        noRhythm.value.reset();
        noRhythm.confidence = 0.0f;
        noRhythm.score = 0.0f;
        noRhythm.detectionStatus = BpmDetectionStatus::noRhythmicContent;
        transients = transientResult();
        result = estimate(analysisResult(noRhythm, transients));
        expect(result.grid.reliability == Reliability::insufficientData);
        expect(result.grid.reason == Reason::missingTempo);

        transients = transientResult();
        for (int beat = 0; beat < 12; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000), 0.0f);
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reason == Reason::insufficientEvents);

        beginTest("Weak BPM, weak phase and close BPM alternatives are reported separately");
        transients = transientResult();
        for (int beat = 0; beat < 12; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        auto lowBpm = bpmResult(120.0, 0.2f);
        lowBpm.detectionStatus = BpmDetectionStatus::lowConfidence;
        result = estimate(analysisResult(lowBpm, transients));
        expect(result.grid.reason == Reason::weakTempo);

        transients = transientResult();
        for (int i = 0; i < 18; ++i)
            addEvent(transients, static_cast<Frame>((i * 17931) % (frames - 1)));
        std::sort(transients.positions.begin(), transients.positions.end(), [](const auto& a, const auto& b)
        {
            return a.samplePosition < b.samplePosition;
        });
        for (std::size_t i = 0; i < transients.positions.size(); ++i)
        {
            auto& event = transients.positions[i];
            event.timeSeconds = static_cast<double>(event.samplePosition) / rate;
            event.normalizedPosition = static_cast<double>(event.samplePosition) / (frames - 1);
        }
        transients.totalEventCount = transients.positions.size();
        result = estimate(analysisResult(bpmResult(), transients));
        expect(result.grid.reason == Reason::weakPhaseEvidence
            || result.grid.reason == Reason::competingPhases);

        transients = transientResult();
        for (int event = 0; event < 4; ++event)
            addEvent(transients, static_cast<Frame>(event * 96000));
        auto closeAlternatives = bpmResult(120.0, 0.91f);
        closeAlternatives.alternatives.push_back({ 90.0, 0.89f, 0.86f });
        result = estimate(analysisResult(closeAlternatives, transients));
        expect(result.grid.tempoCandidates.size() == 2);
        expect(result.grid.reliability == Reliability::ambiguous);
        expect(result.grid.reason == Reason::competingTempos);
        expect(!result.grid.phaseOffsetFrames);

        beginTest("Production BPM and transient analyzer payloads are accepted directly");
        constexpr int integrationFrames = 8 * 48000;
        juce::AudioBuffer<float> audio(1, integrationFrames);
        audio.clear();
        for (int beat = 0; beat < 15; ++beat)
        {
            const auto start = 6000 + beat * 24000;
            for (int frame = 0; frame < 4800 && start + frame < integrationFrames; ++frame)
            {
                const auto time = static_cast<double>(frame) / rate;
                const auto envelope = std::exp(-time / 0.028);
                const auto tone = std::sin(2.0 * juce::MathConstants<double>::pi
                                           * (58.0 + 20.0 * std::exp(-time / 0.02)) * time);
                audio.addSample(0, start + frame, static_cast<float>(0.9 * envelope * tone));
            }
        }
        SampleMetadata metadata;
        metadata.fileName = "beat-grid-integration.wav";
        metadata.sampleRate = rate;
        metadata.channelCount = 1;
        metadata.bitDepth = 32;
        metadata.sampleCount = integrationFrames;
        SampleStorage sample(std::move(audio), std::move(metadata));
        const AnalysisCancellationToken cancellation;
        const auto bpmOutput = BPMAnalyzer().analyze(sample, cancellation);
        const auto transientOutput = TransientAnalyzer().analyze(sample, cancellation);
        expect(bpmOutput.payload.has_value(), "BPM Analyzer should return a payload for the synthetic loop");
        expect(transientOutput.payload.has_value(), "Transient Analyzer should return a payload for the synthetic loop");
        if (bpmOutput.payload && transientOutput.payload)
        {
            const auto* detectedBpm = std::get_if<BpmAnalysis>(&*bpmOutput.payload);
            const auto* detectedTransients = std::get_if<TransientAnalysis>(&*transientOutput.payload);
            expect(detectedBpm != nullptr && detectedTransients != nullptr,
                   "Native analyzers should return the expected payload types");
            if (detectedBpm != nullptr && detectedTransients != nullptr)
            {
                expect(detectedBpm->effectiveValue().has_value(),
                       "The native BPM detector should find the synthetic pulse train");
                expect(detectedTransients->positions.size() >= 4,
                       "The native transient detector should find repeated attacks");
                if (detectedBpm->effectiveValue())
                    expect(std::min(std::abs(*detectedBpm->effectiveValue() - 120.0),
                                    std::abs(*detectedBpm->effectiveValue() - 60.0)) < 3.0,
                           "The detected tempo should be close to the known 120/60 BPM octave pair");
                AnalysisResult nativeAnalysis;
                nativeAnalysis.identity = { sampleId, generation };
                nativeAnalysis.status = AnalysisStatus::completed;
                nativeAnalysis.bpm = *detectedBpm;
                nativeAnalysis.transients = *detectedTransients;
                expect(nativeAnalysis.isValid(), "Native analyzer payloads should share valid source geometry");
                const auto nativeGrid = estimate(nativeAnalysis, integrationFrames, rate);
                expect(!nativeGrid.error.has_value(), "Estimator should accept actual analyzer output");
                expect(nativeGrid.grid.bpm.has_value() == detectedBpm->effectiveValue().has_value());
                expect(nativeGrid.grid.tempoCandidates.size()
                       == (detectedBpm->effectiveValue() ? std::min<std::size_t>(8,
                              detectedBpm->alternatives.size() + 1) : 0));
                expect(nativeGrid.grid.reliability != Reliability::reliable
                           || nativeGrid.grid.phaseOffsetFrames.has_value(),
                       "A reliable native grid must publish its selected phase");
                if (nativeGrid.grid.reliability == Reliability::reliable
                    && nativeGrid.grid.phaseOffsetFrames)
                    expect(std::abs(*nativeGrid.grid.phaseOffsetFrames - 6000.0) < rate * 0.08,
                           "A reliable native phase should remain near the known non-zero onset");
            }
        }

        beginTest("Grid resolution respects minimum interval and is independent of sample rate");
        for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto sampleFrames = static_cast<Frame>(sampleRate * 8.0);
            transients = transientResult(sampleRate, sampleFrames);
            const auto period = sampleRate * 0.5;
            for (int beat = 0; beat < 15; ++beat)
                addEvent(transients, static_cast<Frame>(std::llround(beat * period)));
            result = estimate(analysisResult(bpmResult(), transients), sampleFrames, sampleRate);
            expect(result.grid.reliability == Reliability::reliable);
            expect(result.grid.beatPeriodFrames
                && std::abs(*result.grid.beatPeriodFrames - period) < 1.0);
            expect(result.grid.resolution == Resolution::eighthBeat);
            if (result.grid.phaseOffsetFrames)
            {
                const auto eighth = Estimator::frameAtSubdivision(result.grid, 2, Resolution::sixteenthBeat);
                expect(eighth && *eighth == static_cast<Frame>(std::llround(period / 8.0)));
            }
        }

        beginTest("Frame generation uses the original period, rejects out-of-file ticks, and is deterministic");
        transients = transientResult();
        for (int beat = 0; beat < 15; ++beat)
            addEvent(transients, 3000 + static_cast<Frame>(beat * 24000));
        const auto fixture = analysisResult(bpmResult(), transients);
        const auto first = estimate(fixture);
        const auto second = estimate(fixture);
        expect(first.grid.phaseOffsetFrames == second.grid.phaseOffsetFrames);
        expect(first.grid.tempoCandidates.size() == second.grid.tempoCandidates.size());
        if (first.grid.phaseOffsetFrames)
        {
            const auto tick0 = Estimator::frameAtSubdivision(first.grid, 0, Resolution::sixteenthBeat);
            const auto tick32 = Estimator::frameAtSubdivision(first.grid, 32, Resolution::sixteenthBeat);
            expect(tick0 && *tick0 == static_cast<Frame>(std::llround(*first.grid.phaseOffsetFrames)));
            expect(tick32 && *tick32 == static_cast<Frame>(std::llround(
                *first.grid.phaseOffsetFrames + 32.0 * *first.grid.beatPeriodFrames / 16.0)));
            expect(!Estimator::frameAtSubdivision(first.grid, 10000, Resolution::beat));
        }

        beginTest("Long sources stay bounded; unrepresentable phase precision and tick overflow are rejected");
        constexpr Frame twelveHours = static_cast<Frame>(rate * 60.0 * 60.0 * 12.0);
        transients = transientResult(rate, twelveHours);
        for (int beat = 0; beat < 15; ++beat)
            addEvent(transients, 1200 + static_cast<Frame>(beat * 24000));
        result = estimate(analysisResult(bpmResult(), transients), twelveHours, rate);
        expect(result.grid.reliability == Reliability::reliable);
        expect(result.grid.tempoCandidates.size() == 1);
        if (result.grid.phaseOffsetFrames)
        {
            auto extremeGrid = result.grid;
            extremeGrid.totalFrames = std::numeric_limits<Frame>::max();
            expect(!Estimator::frameAtBeat(extremeGrid, std::numeric_limits<std::int64_t>::max()));
        }

        constexpr Frame beyondExactDoubleFrames = Frame { 1 } << 54;
        transients = transientResult(rate, beyondExactDoubleFrames);
        for (int beat = 0; beat < 12; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        result = estimate(analysisResult(bpmResult(), transients), beyondExactDoubleFrames, rate);
        expect(result.grid.reason == Reason::numericalLimit);
        expect(!result.grid.phaseOffsetFrames);

        beginTest("Mismatched generation, geometry and malformed configuration fail safely");
        transients = transientResult();
        for (int beat = 0; beat < 12; ++beat)
            addEvent(transients, static_cast<Frame>(beat * 24000));
        auto malformed = analysisResult(bpmResult(), transients);
        auto wrongGeneration = Input { sampleId, revision, generation + 1, frames, rate, &malformed };
        result = Estimator::estimate(wrongGeneration);
        expect(result.error.has_value() && result.grid.reason == Reason::invalidInput);

        auto wrongGeometry = analysisResult(bpmResult(), transientResult(44100.0, frames));
        result = estimate(wrongGeometry);
        expect(result.error.has_value() && result.grid.reason == Reason::invalidInput);

        Config invalid;
        invalid.minimumCandidateIntervalSeconds = std::numeric_limits<double>::quiet_NaN();
        result = estimate(malformed, frames, rate, invalid);
        expect(result.error.has_value() && result.grid.reason == Reason::invalidConfiguration);
    }
};
}

juce::UnitTest* getBeatGridEstimatorTest()
{
    static BeatGridEstimatorTests test;
    return &test;
}
