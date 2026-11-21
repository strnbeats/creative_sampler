#include "AnalysisInfrastructureTests.h"
#include "../src/analysis/AnalysisCoordinator.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace
{
std::shared_ptr<const SampleStorage> makeAnalysisSample()
{
    juce::AudioBuffer<float> audio(1, 4800);
    audio.clear();
    SampleMetadata metadata;
    metadata.fileName = "shared-source.wav";
    metadata.sampleRate = 48000.0;
    metadata.channelCount = 1;
    metadata.sampleCount = audio.getNumSamples();
    return std::make_shared<const SampleStorage>(std::move(audio), std::move(metadata));
}

class FixedAnalyzer final : public Analyzer
{
public:
    FixedAnalyzer(AnalyzerKind analyzerKind, AnalyzerOutput analyzerOutput)
        : analyzerType(analyzerKind), output(std::move(analyzerOutput))
    {
    }

    AnalyzerKind kind() const noexcept override { return analyzerType; }
    AnalyzerOutput analyze(const SampleStorage&,
                           const AnalysisCancellationToken&) const override { return output; }

private:
    AnalyzerKind analyzerType;
    AnalyzerOutput output;
};

class CooperativeBlockingAnalyzer final : public Analyzer
{
public:
    AnalyzerKind kind() const noexcept override { return AnalyzerKind::bpm; }
    AnalyzerOutput analyze(const SampleStorage&,
                           const AnalysisCancellationToken& cancellation) const override
    {
        started.store(true, std::memory_order_release);
        while (!cancellation.isCancellationRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        observedCancellation.store(true, std::memory_order_release);
        return AnalyzerOutput::failure("cancelled by test");
    }

    mutable std::atomic<bool> started { false };
    mutable std::atomic<bool> observedCancellation { false };
};

bool waitFor(const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return predicate();
}

class AnalysisInfrastructureTests final : public juce::UnitTest
{
public:
    AnalysisInfrastructureTests() : UnitTest("Creative Sampler Analysis Infrastructure", "Analysis") {}

    void runTest() override
    {
        beginTest("AnalysisResult represents empty, running, terminal, and invalid states");
        {
            AnalysisResult result;
            expect(result.isValid());
            expectEquals(static_cast<int>(result.status), static_cast<int>(AnalysisStatus::notAnalyzed));
            expect(!result.bpm && !result.key && !result.transients);

            result.identity = { 1, 1 };
            result.status = AnalysisStatus::running;
            expect(result.isValid());
            result.bpm = BpmAnalysis { 120.0, 0.8f, {} };
            expect(!result.isValid()); // Running must not expose unfinished values.

            result = {};
            result.identity = { 1, 1 };
            result.status = AnalysisStatus::completed;
            expect(!result.isValid()); // Completed without any analyzer output is invalid.
            result.bpm = BpmAnalysis { 120.0, 0.8f, { { 60.0, 0.3f } } };
            expect(result.isValid());
            expect(result.bpm->isValid());
            expect(result.bpm->setManualOverride(121.5));
            expectWithinAbsoluteError(*result.bpm->effectiveValue(), 121.5, 0.0001);
            expect(!result.bpm->setManualOverride(221.0));
            expect(!result.bpm->setManualOverride(std::numeric_limits<double>::quiet_NaN()));
            result.identity = {};
            expect(!result.isValid()); // Terminal results must identify their Sample generation.
            result.identity = { 1, 1 };
            result.bpm->confidence = std::numeric_limits<float>::quiet_NaN();
            expect(!result.isValid());

            result = {};
            result.identity = { 1, 1 };
            result.status = AnalysisStatus::failed;
            expect(!result.isValid());
            result.error = "Analyzer failed";
            expect(result.isValid());
            result.status = AnalysisStatus::cancelled;
            result.error.clear();
            expect(result.isValid());
        }

        beginTest("Sample IDs distinguish reloads and request generations advance");
        {
            AnalysisCoordinator coordinator;
            const auto firstId = AnalysisCoordinator::createSampleId();
            const auto firstIdentity = coordinator.setCurrentSample(firstId);
            expect(firstId != 0);
            expectEquals(static_cast<int>(coordinator.latestResult().status),
                         static_cast<int>(AnalysisStatus::notAnalyzed));
            expect(!coordinator.requestAnalysis(firstId + 1, makeAnalysisSample(), {}));

            auto analyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { 123.0, 0.9f, {} }));
            const auto firstRequest = coordinator.requestAnalysis(firstId, makeAnalysisSample(), { analyzer });
            expect(firstRequest.has_value());
            if (firstRequest)
            {
                expectEquals(static_cast<int>(firstRequest->sampleId), static_cast<int>(firstId));
                expect(firstRequest->generation > firstIdentity.generation);
            }
            expect(waitFor([&coordinator]
                { return coordinator.latestResult().status == AnalysisStatus::completed; }));

            const auto secondId = AnalysisCoordinator::createSampleId();
            expect(secondId != firstId);
            const auto secondIdentity = coordinator.setCurrentSample(secondId);
            expect(secondIdentity.sampleId == secondId);
            expect(secondIdentity.generation > firstRequest->generation);
            expectEquals(static_cast<int>(coordinator.latestResult().status),
                         static_cast<int>(AnalysisStatus::notAnalyzed));
        }

        beginTest("Repeated requests cancel obsolete work and publish only the newest generation");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            coordinator.setCurrentSample(sampleId);
            auto blocking = std::make_shared<CooperativeBlockingAnalyzer>();
            auto older = coordinator.requestAnalysis(sampleId, makeAnalysisSample(), { blocking });
            expect(older.has_value());
            expect(waitFor([&blocking] { return blocking->started.load(std::memory_order_acquire); }));

            auto latest = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { 97.0, 0.75f, {} }));
            auto newer = coordinator.requestAnalysis(sampleId, makeAnalysisSample(), { latest });
            expect(newer.has_value());
            expect(blocking->observedCancellation.load(std::memory_order_acquire)
                   || waitFor([&blocking]
                      { return blocking->observedCancellation.load(std::memory_order_acquire); }));
            expect(waitFor([&coordinator]
                { return coordinator.latestResult().status == AnalysisStatus::completed; }));

            const auto result = coordinator.latestResult();
            expect(newer && result.identity == *newer);
            expect(result.bpm.has_value());
            if (result.bpm)
                expectWithinAbsoluteError(*result.bpm->value, 97.0, 0.0001);
        }

        beginTest("Sample replacement invalidates an in-flight analysis for the old identity");
        {
            AnalysisCoordinator coordinator;
            const auto oldSampleId = AnalysisCoordinator::createSampleId();
            coordinator.setCurrentSample(oldSampleId);
            auto blocking = std::make_shared<CooperativeBlockingAnalyzer>();
            coordinator.requestAnalysis(oldSampleId, makeAnalysisSample(), { blocking });
            expect(waitFor([&blocking] { return blocking->started.load(std::memory_order_acquire); }));

            const auto newSampleId = AnalysisCoordinator::createSampleId();
            const auto newIdentity = coordinator.setCurrentSample(newSampleId);
            expect(blocking->observedCancellation.load(std::memory_order_acquire)
                   || waitFor([&blocking]
                      { return blocking->observedCancellation.load(std::memory_order_acquire); }));
            const auto current = coordinator.latestResult();
            expect(current.identity == newIdentity);
            expectEquals(static_cast<int>(current.status),
                         static_cast<int>(AnalysisStatus::notAnalyzed));

            auto currentSampleAnalyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { 111.0, 0.82f, {} }));
            const auto newRequest = coordinator.requestAnalysis(newSampleId, makeAnalysisSample(),
                                                                 { currentSampleAnalyzer });
            expect(newRequest.has_value());
            expect(waitFor([&coordinator]
                { return coordinator.latestResult().status == AnalysisStatus::completed; }));
            const auto completed = coordinator.latestResult();
            expect(newRequest && completed.identity == *newRequest);
            expect(completed.bpm.has_value());
            if (completed.bpm)
                expectWithinAbsoluteError(*completed.bpm->value, 111.0, 0.0001);
        }

        beginTest("Independent analyzer outputs combine and explicit cancellation stays terminal");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            coordinator.setCurrentSample(sampleId);
            auto bpmAnalyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { 120.0, 0.9f, {} }));
            auto keyAnalyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::key,
                AnalyzerOutput::success(KeyAnalysis { PitchClass::a, ScaleMode::minor, 0.7f, {} }));
            Transient transient { 0.02, 0.8f, 0.9f, 960, 960.0 / 4799.0, 0.6f };
            TransientAnalysis transientPayload;
            transientPayload.positions.push_back(transient);
            transientPayload.sampleRate = 48000.0;
            transientPayload.sampleCount = 4800;
            transientPayload.totalEventCount = 1;
            transientPayload.detectionStatus = TransientDetectionStatus::detected;
            auto transientAnalyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::transients,
                AnalyzerOutput::success(std::move(transientPayload)));
            const auto request = coordinator.requestAnalysis(sampleId, makeAnalysisSample(),
                { bpmAnalyzer, keyAnalyzer, transientAnalyzer });
            expect(request.has_value());
            expect(waitFor([&coordinator]
                { return coordinator.latestResult().status == AnalysisStatus::completed; }));
            auto result = coordinator.latestResult();
            expect(result.bpm.has_value() && result.key.has_value() && result.transients.has_value());
            expect(result.isValid());

            auto blocking = std::make_shared<CooperativeBlockingAnalyzer>();
            coordinator.requestAnalysis(sampleId, makeAnalysisSample(), { blocking });
            expect(waitFor([&blocking] { return blocking->started.load(std::memory_order_acquire); }));
            coordinator.cancel();
            expectEquals(static_cast<int>(coordinator.latestResult().status),
                         static_cast<int>(AnalysisStatus::cancelled));
            expect(waitFor([&blocking]
                { return blocking->observedCancellation.load(std::memory_order_acquire); }));
            result = coordinator.latestResult();
            expectEquals(static_cast<int>(result.status), static_cast<int>(AnalysisStatus::cancelled));
            expect(result.isValid());
        }

        beginTest("Invalid analyzer outputs and invalid requests become explicit failures");
        {
            AnalysisCoordinator coordinator;
            const auto sampleId = AnalysisCoordinator::createSampleId();
            coordinator.setCurrentSample(sampleId);
            const auto emptyRequest = coordinator.requestAnalysis(sampleId, makeAnalysisSample(), {});
            expect(emptyRequest.has_value());
            auto failed = coordinator.latestResult();
            expectEquals(static_cast<int>(failed.status), static_cast<int>(AnalysisStatus::failed));
            expect(failed.isValid());

            juce::AudioBuffer<float> emptyAudio(1, 0);
            SampleMetadata emptyMetadata;
            emptyMetadata.sampleRate = 48000.0;
            emptyMetadata.channelCount = 1;
            emptyMetadata.sampleCount = 0;
            auto emptySample = std::make_shared<const SampleStorage>(std::move(emptyAudio), emptyMetadata);
            auto analyzer = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { 100.0, 0.5f, {} }));
            const auto emptySampleRequest = coordinator.requestAnalysis(sampleId, emptySample, { analyzer });
            expect(emptySampleRequest.has_value());
            failed = coordinator.latestResult();
            expectEquals(static_cast<int>(failed.status), static_cast<int>(AnalysisStatus::failed));
            expect(failed.error.isNotEmpty());

            auto malformed = std::make_shared<FixedAnalyzer>(AnalyzerKind::bpm,
                AnalyzerOutput::success(BpmAnalysis { -20.0, 0.7f, {} }));
            const auto request = coordinator.requestAnalysis(sampleId, makeAnalysisSample(), { malformed });
            expect(request.has_value());
            expect(waitFor([&coordinator]
                { return coordinator.latestResult().status == AnalysisStatus::failed; }));
            failed = coordinator.latestResult();
            expect(failed.isValid());
            expect(failed.error.isNotEmpty());
            expect(!failed.bpm);
        }

        beginTest("Shutdown cancels and joins active cooperative analysis");
        {
            auto analyzer = std::make_shared<CooperativeBlockingAnalyzer>();
            {
                AnalysisCoordinator coordinator;
                const auto sampleId = AnalysisCoordinator::createSampleId();
                coordinator.setCurrentSample(sampleId);
                coordinator.requestAnalysis(sampleId, makeAnalysisSample(), { analyzer });
                expect(waitFor([&analyzer] { return analyzer->started.load(std::memory_order_acquire); }));
            }
            expect(analyzer->observedCancellation.load(std::memory_order_acquire));
        }
    }
};
}

juce::UnitTest* getAnalysisInfrastructureTest()
{
    static AnalysisInfrastructureTests test;
    return &test;
}
