#include "AutoSlicePlannerTests.h"
#include "analysis/TransientAnalyzer.h"
#include "auto_slicing/AutoSlicePlanner.h"

#include <algorithm>
#include <limits>

namespace
{
using namespace auto_slicing;

struct Attack
{
    Frame frame;
    float strength = 0.95f;
    std::optional<float> confidence = 0.95f;
};

AnalysisResult fixture(const Source& source, std::initializer_list<Attack> attacks = {})
{
    AnalysisResult analysis;
    analysis.identity = { source.sampleId, source.analysisGeneration };
    analysis.status = AnalysisStatus::completed;
    auto& data = analysis.transients.emplace();
    data.sampleRate = source.sampleRate;
    data.sampleCount = source.totalFrames;
    data.totalEventCount = attacks.size();
    data.detectionStatus = attacks.size() == 0 ? TransientDetectionStatus::insufficientData
                                               : TransientDetectionStatus::detected;
    for (const auto& attack : attacks)
        data.positions.push_back({ static_cast<double>(attack.frame) / source.sampleRate,
                                   attack.strength, attack.confidence, attack.frame,
                                   static_cast<double>(attack.frame)
                                      / static_cast<double>(std::max<Frame>(1, source.totalFrames - 1)),
                                   0.8f });
    return analysis;
}

class AutoSlicePlannerTests final : public juce::UnitTest
{
public:
    AutoSlicePlannerTests() : UnitTest("Auto Slicing Planner", "Auto Slicing") {}

    void runTest() override
    {
        const Source source { 23, 1, 7, 48000, 48000.0 };

        beginTest("No native transient events produce a valid whole-sample fallback");
        check(source, fixture(source), {}, FallbackReason::noTransients);
        auto absent = fixture(source);
        absent.transients.reset();
        check(source, absent, {}, FallbackReason::noTransients);

        beginTest("One strong transient preserves its exact source frame");
        check(source, fixture(source, { { 12001 } }), { 12001 });

        beginTest("Widely spaced attacks survive; weak events and low-confidence peaks are ignored");
        check(source, fixture(source, { { 6000 }, { 12000 }, { 18000 }, { 24000 } }),
              { 6000, 12000, 18000, 24000 });
        check(source, fixture(source, { { 6000, .1f }, { 12000, .9f, 0.0f },
                                       { 18000, .9f, std::nullopt }, { 24000 } }),
              { 18000, 24000 });

        beginTest("Close attacks prefer joint significance, with deterministic ties");
        check(source, fixture(source, { { 10000, .8f, .3f }, { 10100, .8f },
                                       { 10300, 1.0f, .6f }, { 10700, .3f } }), { 10100 });
        check(source, fixture(source, { { 10000 }, { 10100 }, { 11000 } }), { 10000 });

        beginTest("Two compatible attacks can beat a stronger intervening attack");
        check(source, fixture(source, { { 10000, .82f }, { 12000 }, { 14000, .82f } }),
              { 10000, 14000 });

        beginTest("Early and late events must leave playable edge segments");
        check(source, fixture(source, { { 0 }, { 2879 }, { 2880 }, { 45120 }, { 45121 }, { 47999 } }),
              { 2880, 45120 });
        check(source, fixture(source, { { 12 }, { 47999 } }), {}, FallbackReason::noFeasibleSplit);

        beginTest("Short sources get a whole interval and a below-engine-minimum diagnostic");
        for (const Frame frames : { 1, 63, 64, 5759 })
        {
            auto shortSource = source;
            shortSource.totalFrames = frames;
            check(shortSource, fixture(shortSource, { { frames / 2 } }), {}, FallbackReason::shortSource);
        }
        auto exact = source;
        exact.totalFrames = 5760;
        check(exact, fixture(exact, { { 2880 } }), { 2880 });

        beginTest("The minimum duration scales at 44.1, 48 and 96 kHz");
        for (const auto rate : { 44100, 48000, 96000 })
        {
            const Source scaled { 23, 1, 7, rate, static_cast<double>(rate) };
            const Frame minimum = rate * 60 / 1000;
            check(scaled, fixture(scaled, { { minimum - 1 }, { minimum } }), { minimum });
        }

        beginTest("Long sparse input does not require storage proportional to PCM length");
        auto longSource = source;
        longSource.totalFrames = std::numeric_limits<Frame>::max();
        check(longSource, fixture(longSource, { { 1234567890 }, { longSource.totalFrames - 48000 } }),
              { 1234567890, longSource.totalFrames - 48000 });

        beginTest("Dense input prunes windows, obeys spacing and respects the slice budget");
        auto denseSource = source;
        denseSource.totalFrames = 30000000;
        auto dense = fixture(denseSource);
        auto& positions = dense.transients->positions;
        positions.reserve(50000);
        for (Frame i = 1; i <= 50000; ++i)
            positions.push_back({ 0.0, .98f, .98f, i * 580, 0.0, .9f });
        dense.transients->totalEventCount = positions.size();
        dense.transients->detectionStatus = TransientDetectionStatus::detected;
        Config limited;
        limited.maximumSlices = 6;
        limited.minimumBoundaryDistanceFrames = 3600;
        if (const auto plan = checkedPlan(denseSource, dense, limited))
        {
            expect(plan->sliceCount() == limited.maximumSlices);
            expect(plan->diagnostics.retainedCandidates < plan->diagnostics.eligibleEvents);
            for (std::size_t i = 2; i + 1 < plan->boundaries.size(); ++i)
                expect(plan->boundaries[i].frame - plan->boundaries[i - 1].frame >= 3600);
        }

        beginTest("Marginal attacks do not fill the budget; sensitivity and one-slice limit are honored");
        const auto marginal = fixture(source, { { 6000, .48f, 1.0f }, { 12000, .48f, 1.0f },
                                               { 18000, .48f, 1.0f }, { 24000, .48f, 1.0f } });
        check(source, marginal, { 6000, 12000 });
        Config strict;
        strict.sensitivity = 0.0f;
        check(source, marginal, {}, FallbackReason::weakTransients, strict);
        Config single;
        single.maximumSlices = 1;
        check(source, fixture(source, { { 12000 } }), {}, FallbackReason::sliceLimit, single);

        beginTest("Silent and near-silent PCM analyzed by the native detector stay whole");
        for (const auto level : { 0.0f, 1e-8f })
        {
            juce::AudioBuffer<float> samples(2, static_cast<int>(source.totalFrames));
            for (int frame = 0; frame < samples.getNumSamples(); ++frame)
            {
                const auto value = frame % 2 == 0 ? level : -level;
                samples.setSample(0, frame, value);
                samples.setSample(1, frame, -value);
            }
            SampleStorage storage(std::move(samples),
                                  { {}, {}, source.sampleRate, 2, 32, source.totalFrames });
            const AnalysisCancellationToken token;
            const auto analyzed = TransientAnalyzer().analyze(storage, token);
            expect(analyzed.error.isEmpty() && analyzed.payload.has_value());
            if (analyzed.payload)
            {
                if (const auto* events = std::get_if<TransientAnalysis>(&*analyzed.payload))
                {
                    auto analysis = fixture(source);
                    analysis.transients = *events;
                    check(source, analysis, {}, FallbackReason::noTransients);
                }
                else
                    expect(false, "Expected native transient payload");
            }
        }

        beginTest("Failed or truncated analysis yields a reason; cancellation never yields a plan");
        auto analysis = fixture(source);
        analysis.status = AnalysisStatus::failed;
        check(source, analysis, {}, FallbackReason::analysisFailed);
        analysis = fixture(source, { { 12000 } });
        analysis.transients->totalEventCount = 10;
        analysis.transients->detectionStatus = TransientDetectionStatus::truncated;
        check(source, analysis, {}, FallbackReason::truncatedAnalysis);
        AnalysisCancellationToken token;
        token.cancel();
        checkError(makePlan(source, analysis, {}, &token), ErrorKind::cancelled);
        analysis.status = AnalysisStatus::cancelled;
        checkError(makePlan(source, analysis), ErrorKind::cancelled);

        beginTest("Invalid source, stale analysis, geometry mismatch and malformed events are errors");
        for (const auto invalid : { Source { 0, 1, 7, 48000, 48000 },
                                    Source { 23, 0, 7, 48000, 48000 },
                                    Source { 23, 1, 0, 48000, 48000 },
                                    Source { 23, 1, 7, 0, 48000 },
                                    Source { 23, 1, 7, 48000, 0 },
                                    Source { 23, 1, 7, 48000, std::numeric_limits<double>::infinity() } })
            checkError(makePlan(invalid, fixture(source)), ErrorKind::input);
        for (const auto& events : { fixture(source, { { -1 } }), fixture(source, { { 48000 } }),
                                  fixture(source, { { 12000 }, { 12000 } }),
                                  fixture(source, { { 12000, std::numeric_limits<float>::quiet_NaN() } }),
                                  fixture(source, { { 12000, 1.0f, 1.1f } }) })
            checkError(makePlan(source, events), ErrorKind::input);
        auto stale = source;
        ++stale.analysisGeneration;
        checkError(makePlan(stale, fixture(source)), ErrorKind::input);
        analysis = fixture(source, { { 12000 } });
        analysis.transients->sampleRate = 44100;
        checkError(makePlan(source, analysis), ErrorKind::input);
        analysis = fixture(source);
        analysis.status = AnalysisStatus::running;
        checkError(makePlan(source, analysis), ErrorKind::input);

        beginTest("Validator rejects empty, gapped, overlapping, unordered and out-of-range geometry");
        if (const auto valid = checkedPlan(source, fixture(source, { { 12000 }, { 24000 } })))
        {
            expect(!valid->sliceAt(valid->sliceCount()).has_value());
            for (const auto invalidFrame : { Frame { -1 }, Frame { 0 }, Frame { 24000 },
                                              Frame { 25000 }, Frame { 48001 } })
            {
                auto plan = *valid;
                plan.boundaries[1].frame = invalidFrame;
                checkValidation(plan, source, {}, ErrorKind::structure);
            }
            // Shared edges make internal gaps/overlaps unrepresentable. Reject missing
            // coverage at the outer edges, or reversed interior edges.
            auto plan = *valid;
            plan.boundaries.front().frame = 1;
            checkValidation(plan, source, {}, ErrorKind::structure);
            plan = *valid;
            plan.boundaries.back().frame = 47999;
            checkValidation(plan, source, {}, ErrorKind::structure);
            plan.boundaries.clear();
            checkValidation(plan, source, {}, ErrorKind::structure);
            plan = *valid;
            plan.boundaries[1].origin = BoundaryOrigin::sourceEdge;
            checkValidation(plan, source, {}, ErrorKind::structure);
            plan = *valid;
            plan.strategy = static_cast<Strategy>(99);
            checkValidation(plan, source, {}, ErrorKind::structure);
        }

        beginTest("Validator rejects stale revision, short segments, bad configuration and excess count");
        if (const auto valid = checkedPlan(source, fixture(source, { { 12000 }, { 24000 } })))
        {
            auto newer = source;
            ++newer.sampleRevision;
            checkValidation(*valid, newer, {}, ErrorKind::input);
            auto plan = *valid;
            plan.boundaries[1].frame = 2879;
            checkValidation(plan, source, {}, ErrorKind::configuration);
            plan = *valid;
            plan.config.maximumSlices = 2;
            checkValidation(plan, source, plan.config, ErrorKind::configuration);
            plan = *valid;
            plan.config.minimumBoundaryDistanceFrames = 13000;
            checkValidation(plan, source, plan.config, ErrorKind::configuration);

            Config invalid;
            invalid.minimumSliceSeconds = std::numeric_limits<double>::quiet_NaN();
            checkError(makePlan(source, fixture(source), invalid), ErrorKind::configuration);
            checkValidation(*valid, source, invalid, ErrorKind::configuration);
            invalid = {};
            invalid.minimumSliceSeconds = 0x1p63;
            checkError(makePlan(source, fixture(source), invalid), ErrorKind::configuration);
            invalid = {};
            invalid.maximumSlices = 33;
            checkError(makePlan(source, fixture(source), invalid), ErrorKind::configuration);
        }
    }

private:
    void checkError(const Result& result, ErrorKind kind)
    {
        expect(!result.plan && result.error && result.error->kind == kind
               && !result.error->message.empty());
    }

    void checkValidation(const Plan& plan, const Source& source, const Config& config, ErrorKind kind)
    {
        const auto error = validatePlan(plan, source, config);
        expect(error && error->kind == kind && !error->message.empty());
    }

    std::optional<Plan> checkedPlan(const Source& source, const AnalysisResult& analysis, const Config& config = {})
    {
        auto result = makePlan(source, analysis, config);
        expect(result.plan.has_value() && !result.error.has_value());
        if (!result.plan)
            return std::nullopt;
        const auto& plan = *result.plan;
        expect(!validatePlan(plan, source, config).has_value());
        expect(makePlan(source, analysis, config).plan == result.plan);
        Frame priorEnd = 0;
        for (std::size_t i = 0; i < plan.sliceCount(); ++i)
            if (const auto slice = plan.sliceAt(i))
            {
                expect(slice->start.frame == priorEnd && slice->end.frame > priorEnd
                       && slice->end.frame <= source.totalFrames);
                priorEnd = slice->end.frame;
            }
            else
                expect(false, "Every slice index must resolve");
        expect(priorEnd == source.totalFrames);
        expect(plan.diagnostics.belowEngineMinimum == (source.totalFrames < config.engineMinimumFrames));
        return std::move(result.plan);
    }

    void check(const Source& source, const AnalysisResult& analysis, std::initializer_list<Frame> cuts,
               FallbackReason fallback = FallbackReason::none, const Config& config = {})
    {
        if (const auto plan = checkedPlan(source, analysis, config))
        {
            expect(plan->diagnostics.fallback == fallback);
            expect(plan->strategy == (cuts.size() == 0 ? Strategy::wholeSample : Strategy::transient));
            expect(plan->sliceCount() == cuts.size() + 1);
            if (plan->sliceCount() == cuts.size() + 1)
                expect(std::equal(cuts.begin(), cuts.end(), plan->boundaries.begin() + 1,
                                  [](Frame frame, const Boundary& boundary) { return frame == boundary.frame; }));
        }
    }
};
}

juce::UnitTest* getAutoSlicePlannerTest()
{
    static AutoSlicePlannerTests test;
    return &test;
}
