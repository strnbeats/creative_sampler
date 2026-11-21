#include "ADSREnvelopeTests.h"
#include "../src/dsp/ADSREnvelope.h"
#include <cmath>
#include <limits>

namespace
{
class ADSREnvelopeUnitTest final : public juce::UnitTest
{
public:
    ADSREnvelopeUnitTest() : UnitTest("Creative Sampler ADSR Envelope", "ADSR") {}

    void runTest() override
    {
        beginTest("Attack, decay, sustain, and release follow sample-counted segments");
        {
            ADSREnvelope envelope;
            expect(envelope.prepare(1000.0));
            const ADSRParameters parameters { 0.004f, 0.004f, 0.25f, 0.004f };
            expect(envelope.setParameters(parameters));
            envelope.noteOn();
            expect(envelope.stage() == ADSREnvelope::Stage::attack);
            const float attack[] { 0.25f, 0.5f, 0.75f, 1.0f };
            for (const auto expected : attack)
                expectWithinAbsoluteError(envelope.nextSample(), expected, 1.0e-6f);
            expect(envelope.stage() == ADSREnvelope::Stage::decay);
            const float decay[] { 0.8125f, 0.625f, 0.4375f, 0.25f };
            for (const auto expected : decay)
                expectWithinAbsoluteError(envelope.nextSample(), expected, 1.0e-6f);
            expect(envelope.stage() == ADSREnvelope::Stage::sustain);
            expectWithinAbsoluteError(envelope.nextSample(), 0.25f, 1.0e-6f);
            envelope.noteOff();
            expect(envelope.stage() == ADSREnvelope::Stage::release);
            const float release[] { 0.1875f, 0.125f, 0.0625f, 0.0f };
            for (const auto expected : release)
                expectWithinAbsoluteError(envelope.nextSample(), expected, 1.0e-6f);
            expect(envelope.stage() == ADSREnvelope::Stage::idle);
            expect(!envelope.isActive());
        }

        beginTest("Zero and sub-sample times remain finite and retrigger restarts at zero");
        {
            ADSREnvelope envelope;
            expect(envelope.prepare(96000.0));
            const ADSRParameters immediate { 0.0f, 0.0f, 0.3f, 0.0f };
            expect(envelope.setParameters(immediate));
            envelope.noteOn();
            expect(envelope.stage() == ADSREnvelope::Stage::sustain);
            expectWithinAbsoluteError(envelope.nextSample(), 0.3f, 1.0e-6f);
            envelope.noteOff();
            expect(!envelope.isActive());
            expectWithinAbsoluteError(envelope.nextSample(), 0.0f, 1.0e-6f);

            const ADSRParameters brief { 1.0e-8f, 0.0f, 0.5f, 1.0e-8f };
            expect(envelope.setParameters(brief));
            envelope.noteOn();
            expectWithinAbsoluteError(envelope.nextSample(), 0.5f, 1.0e-6f);
            envelope.noteOn();
            expectWithinAbsoluteError(envelope.nextSample(), 0.5f, 1.0e-6f);
            envelope.noteOff();
            expectWithinAbsoluteError(envelope.nextSample(), 0.0f, 1.0e-6f);
            expect(!envelope.isActive());
        }

        beginTest("Times scale with sample rate and Note Off during Attack releases from current level");
        {
            ADSREnvelope slow;
            ADSREnvelope fast;
            expect(slow.prepare(1000.0));
            expect(fast.prepare(2000.0));
            const ADSRParameters parameters { 0.01f, 0.0f, 1.0f, 0.01f };
            expect(slow.setParameters(parameters));
            expect(fast.setParameters(parameters));
            slow.noteOn();
            fast.noteOn();
            for (int i = 0; i < 5; ++i)
                slow.nextSample();
            for (int i = 0; i < 10; ++i)
                fast.nextSample();
            expectWithinAbsoluteError(slow.level(), fast.level(), 1.0e-6f);
            const auto releaseFrom = slow.level();
            slow.noteOff();
            expect(slow.stage() == ADSREnvelope::Stage::release);
            expect(slow.nextSample() < releaseFrom);
            for (int i = 0; i < 10; ++i)
                fast.nextSample();
            expectWithinAbsoluteError(fast.level(), 1.0f, 1.0e-6f);
            for (int i = 0; i < 9; ++i)
                slow.nextSample();
            expect(!slow.isActive());
        }

        beginTest("Invalid ADSR values are rejected without corrupting active settings");
        {
            ADSREnvelope envelope;
            expect(envelope.prepare(48000.0));
            const ADSRParameters valid {};
            expect(envelope.setParameters(valid));
            auto invalid = valid;
            invalid.attackSeconds = std::numeric_limits<float>::quiet_NaN();
            expect(!envelope.setParameters(invalid));
            invalid = valid;
            invalid.releaseSeconds = std::numeric_limits<float>::infinity();
            expect(!envelope.setParameters(invalid));
            invalid = valid;
            invalid.sustainLevel = 1.1f;
            expect(!envelope.setParameters(invalid));
            envelope.noteOn();
            auto remainsFiniteAndBounded = true;
            for (int i = 0; i < 1000; ++i)
            {
                const auto level = envelope.nextSample();
                remainsFiniteAndBounded = remainsFiniteAndBounded
                    && std::isfinite(level) && level >= 0.0f && level <= 1.0f;
            }
            expect(remainsFiniteAndBounded);
        }
    }
};

ADSREnvelopeUnitTest adsrEnvelopeUnitTest;
}

juce::UnitTest* getADSREnvelopeTest()
{
    return &adsrEnvelopeUnitTest;
}
