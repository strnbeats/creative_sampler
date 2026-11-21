#include "SliceEngineTests.h"
#include "../src/slice/SliceEngine.h"
#include <juce_core/juce_core.h>
#include <limits>

namespace
{
class SliceEngineUnitTest final : public juce::UnitTest
{
public:
    SliceEngineUnitTest() : UnitTest("Creative Sampler Slice Engine", "Slice Engine") {}

    void runTest() override
    {
        beginTest("Sample binding creates one bounded slice and changes identity safely");
        {
            SliceEngine engine;
            expect(engine.isValid());
            expect(engine.setSample(101, 4096));
            expect(engine.sampleId() == 101);
            expect(engine.sampleCount() == 4096);
            expect(engine.slices().size() == 1);
            expect(engine.slices().front().startSample == 0);
            expect(engine.slices().front().endSample == 4096);
            const auto oldId = engine.slices().front().id;
            expect(engine.setSample(102, 4096));
            expect(engine.sampleId() == 102);
            expect(engine.slices().size() == 1);
            expect(engine.slices().front().id != oldId);
            expect(engine.isValid());
        }

        beginTest("Range creation carves a slice and preserves stable IDs and order");
        {
            SliceEngine engine;
            engine.setSample(1, 1000);
            const auto originalId = engine.slices().front().id;
            const auto middle = engine.createSlice(200, 400);
            expect(middle.has_value());
            expect(engine.slices().size() == 3);
            expect(engine.slices()[0].id == originalId);
            expect(engine.slices()[0].startSample == 0 && engine.slices()[0].endSample == 200);
            expect(engine.slices()[1].id == *middle);
            expect(engine.slices()[1].startSample == 200 && engine.slices()[1].endSample == 400);
            expect(engine.slices()[2].startSample == 400 && engine.slices()[2].endSample == 1000);
            expect(engine.isValid());

            const auto rightId = engine.slices()[2].id;
            const auto lastRevision = engine.revision();
            expect(!engine.createSlice(10, 30).has_value());
            expect(!engine.createSlice(100, 900).has_value());
            expect(!engine.createSlice(-10, 100).has_value());
            expect(!engine.createSlice(900, 1100).has_value());
            expect(engine.revision() == lastRevision);
            expect(engine.slices()[0].id == originalId && engine.slices()[2].id == rightId);
        }

        beginTest("Boundary movement carries a touching neighbor and rejects invalid ranges");
        {
            SliceEngine engine;
            engine.setSample(2, 1000);
            const auto middle = engine.createSlice(200, 400);
            expect(middle.has_value());
            const auto leftId = engine.slices()[0].id;
            const auto rightId = engine.slices()[2].id;
            expect(engine.moveBoundary(*middle, SliceBoundary::end, 450));
            expect(engine.find(*middle)->endSample == 450);
            expect(engine.find(rightId)->startSample == 450);
            expect(engine.moveBoundary(*middle, SliceBoundary::start, 100));
            expect(engine.find(*middle)->startSample == 100);
            expect(engine.find(leftId)->endSample == 100);
            const auto revision = engine.revision();
            expect(!engine.moveBoundary(*middle, SliceBoundary::start, 20));
            expect(!engine.moveBoundary(*middle, SliceBoundary::end, 950));
            expect(engine.revision() == revision);
            expect(engine.isValid());
        }

        beginTest("Split and delete preserve IDs and leave explicit gaps");
        {
            SliceEngine engine;
            engine.setSample(3, 1024);
            const auto leftId = engine.slices().front().id;
            const auto right = engine.splitSlice(leftId, 512);
            expect(right.has_value());
            expect(engine.slices().size() == 2);
            expect(engine.slices()[0].id == leftId);
            expect(engine.slices()[1].id == *right);
            expect(!engine.splitSlice(leftId, 20).has_value());
            expect(engine.deleteSlice(*right));
            expect(engine.slices().size() == 1);
            expect(engine.slices().front().id == leftId);
            expect(engine.slices().front().endSample == 512);
            const auto recreated = engine.createSlice(600, 800);
            expect(recreated.has_value());
            expect(engine.slices().size() == 2);
            expect(engine.slices()[1].startSample == 600);
            expect(engine.isValid());
        }

        beginTest("Reset, empty collection, and short sample states remain valid");
        {
            SliceEngine engine(64);
            expect(engine.setSample(8, 63));
            expect(engine.slices().empty());
            expect(engine.isValid());
            expect(!engine.createSlice(0, 63).has_value());
            expect(!engine.resetSlices());
            expect(engine.setSample(9, 128));
            const auto oldId = engine.slices().front().id;
            expect(engine.splitSlice(oldId, 64).has_value());
            expect(engine.resetSlices());
            expect(engine.slices().size() == 1);
            expect(engine.slices().front().id != oldId);
            expect(engine.slices().front().startSample == 0);
            expect(engine.slices().front().endSample == 128);
            expect(engine.setSample(0, 128) == false);
            expect(engine.sampleId() == 0 && engine.slices().empty());
            expect(engine.isValid());
        }

        beginTest("Creating ranges in a gap is ordered and overlapping ranges are rejected");
        {
            SliceEngine engine;
            engine.setSample(10, 1000);
            const auto originalId = engine.slices().front().id;
            expect(engine.deleteSlice(originalId));
            const auto later = engine.createSlice(600, 800);
            const auto earlier = engine.createSlice(100, 300);
            expect(later.has_value() && earlier.has_value());
            expect(engine.slices().size() == 2);
            expect(engine.slices()[0].id == *earlier);
            expect(engine.slices()[1].id == *later);
            expect(!engine.createSlice(250, 650).has_value());
            expect(engine.isValid());
        }

        beginTest("Slice parameters validate ranges, remain independent, and inherit on split");
        {
            SliceEngine engine;
            expect(engine.setSample(11, 2048));
            const auto originalId = engine.slices().front().id;
            const SliceParameters defaults {};
            expect(engine.find(originalId)->parameters == defaults);

            auto originalParameters = defaults;
            originalParameters.volume = 1.5f;
            originalParameters.pan = -0.25f;
            originalParameters.pitchSemitones = 7.0f;
            originalParameters.envelope = { 0.01f, 0.2f, 0.65f, 0.4f };
            expect(engine.setParameters(originalId, originalParameters));
            const auto rightId = engine.splitSlice(originalId, 1024);
            expect(rightId.has_value());
            if (rightId)
            {
                expect(engine.find(originalId)->parameters == originalParameters);
                expect(engine.find(*rightId)->parameters == originalParameters);
                auto rightParameters = originalParameters;
                rightParameters.volume = 0.5f;
                expect(engine.setParameters(*rightId, rightParameters));
                expect(engine.find(originalId)->parameters == originalParameters);
                expect(engine.find(*rightId)->parameters == rightParameters);

                auto invalid = originalParameters;
                invalid.volume = 2.01f;
                expect(!engine.setParameters(originalId, invalid));
                invalid = originalParameters;
                invalid.pan = std::numeric_limits<float>::quiet_NaN();
                expect(!engine.setParameters(originalId, invalid));
                invalid = originalParameters;
                invalid.pitchSemitones = std::numeric_limits<float>::infinity();
                expect(!engine.setParameters(originalId, invalid));
                invalid = originalParameters;
                invalid.envelope.attackSeconds = 10.01f;
                expect(!engine.setParameters(originalId, invalid));
                invalid = originalParameters;
                invalid.envelope.sustainLevel = std::numeric_limits<float>::quiet_NaN();
                expect(!engine.setParameters(originalId, invalid));
                expect(engine.find(originalId)->parameters == originalParameters);

                expect(engine.deleteSlice(*rightId));
                const auto gapSlice = engine.createSlice(1200, 1500);
                expect(gapSlice.has_value());
                if (gapSlice)
                    expect(engine.find(*gapSlice)->parameters == defaults);
                expect(engine.resetSlices());
                expect(engine.slices().size() == 1);
                expect(engine.slices().front().parameters == defaults);
            }

            expect(engine.setParameters(engine.slices().front().id, originalParameters));
            expect(engine.setSample(12, 1024));
            expect(engine.slices().size() == 1);
            expect(engine.slices().front().parameters == defaults);
            expect(engine.isValid());
        }
    }
};

SliceEngineUnitTest sliceEngineUnitTest;
}

juce::UnitTest* getSliceEngineTest()
{
    return &sliceEngineUnitTest;
}
