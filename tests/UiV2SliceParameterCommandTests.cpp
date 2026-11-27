#include "UiV2SliceParameterCommandTests.h"
#include "../src/ui/UiV2SliceParameterCommand.h"
#include <limits>
#include <optional>

namespace
{
juce::var requestFor(const SliceEngine& engine, SliceId id, const juce::String& parameter,
                     double value)
{
    auto request = juce::DynamicObject::Ptr(new juce::DynamicObject());
    request->setProperty("protocolVersion", 2);
    request->setProperty("requestId", "test-request");
    request->setProperty("sampleId", juce::String(static_cast<juce::int64>(engine.sampleId())));
    request->setProperty("revision", juce::String(static_cast<juce::int64>(engine.revision())));
    request->setProperty("sliceId", juce::String(static_cast<juce::int64>(id)));
    request->setProperty("parameter", parameter);
    request->setProperty("value", value);
    return juce::var(request.get());
}

juce::var snapshotAck(const juce::String& requestId, const SliceEngine& engine, SliceId id)
{
    auto params = juce::DynamicObject::Ptr(new juce::DynamicObject());
    const auto& slice = engine.find(id)->parameters;
    params->setProperty("volume", slice.volume);
    params->setProperty("pan", slice.pan);
    params->setProperty("pitch", slice.pitchSemitones);
    params->setProperty("attack", slice.envelope.attackSeconds);
    params->setProperty("decay", slice.envelope.decaySeconds);
    params->setProperty("sustain", slice.envelope.sustainLevel);
    params->setProperty("release", slice.envelope.releaseSeconds);
    auto result = juce::DynamicObject::Ptr(new juce::DynamicObject());
    result->setProperty("type", "snapshot");
    result->setProperty("sampleId", juce::String(static_cast<juce::int64>(engine.sampleId())));
    result->setProperty("revision", juce::String(static_cast<juce::int64>(engine.revision())));
    result->setProperty("sliceId", juce::String(static_cast<juce::int64>(id)));
    result->setProperty("parameters", juce::var(params.get()));
    auto ack = juce::DynamicObject::Ptr(new juce::DynamicObject());
    ack->setProperty("protocolVersion", 2);
    ack->setProperty("type", "ack");
    ack->setProperty("requestId", requestId);
    ack->setProperty("ok", true);
    ack->setProperty("result", juce::var(result.get()));
    return juce::var(ack.get());
}

class UiV2SliceParameterCommandTest final : public juce::UnitTest
{
public:
    UiV2SliceParameterCommandTest() : UnitTest("UI v2 Slice Parameter Bridge Contract", "Bridge") {}

    void runTest() override
    {
        beginTest("Each real command updates one field and returns a single current ACK snapshot");
        {
            struct Case { const char* name; SliceField field; float minimum, maximum, middle; };
            const Case cases[] {
                { "volume", SliceField::volume, 0, 2, 0.73f },
                { "pan", SliceField::pan, -1, 1, 0.25f },
                { "pitch", SliceField::pitch, -24, 24, 7.5f },
                { "attack", SliceField::attack, 0, 10, 0.037f },
                { "decay", SliceField::decay, 0, 10, 0.42f },
                { "sustain", SliceField::sustain, 0, 1, 0.61f },
                { "release", SliceField::release, 0, 10, 1.25f }
            };
            for (const auto& item : cases)
            {
                SliceEngine engine;
                expect(engine.setSample(801, 4096));
                const auto id = engine.slices().front().id;
                const float values[] { item.minimum, item.maximum, item.middle };
                for (const auto value : values)
                {
                    auto before = engine.find(id)->parameters;
                    auto command = requestFor(engine, id, item.name, value);
                    auto response = handleUiV2SliceParameterCommand(command, engine,
                        [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
                    auto* ack = response.getDynamicObject();
                    expect(ack != nullptr);
                    if (ack == nullptr) continue;
                    expect(ack->getProperty("ok").toString() == "1");
                    expect(ack->getProperty("type").toString() == "ack");
                    expect(ack->getProperty("requestId").toString() == "test-request");
                    auto* result = ack->getProperty("result").getDynamicObject();
                    expect(result != nullptr && result->getProperty("type").toString() == "snapshot");
                    if (result == nullptr) continue;
                    expect(result->getProperty("sampleId").toString() == "801");
                    expect(result->getProperty("sliceId").toString() == juce::String(static_cast<juce::int64>(id)));
                    expect(result->getProperty("revision").toString() == juce::String(static_cast<juce::int64>(engine.revision())));
                    auto* reported = result->getProperty("parameters").getDynamicObject();
                    expect(reported != nullptr);
                    auto actual = engine.find(id)->parameters;
                    expectWithinAbsoluteError(sliceField(actual, item.field), value, 1.0e-6f);
                    if (reported != nullptr)
                        expectWithinAbsoluteError(static_cast<float>(reported->getProperty(item.name)), value, 1.0e-6f);
                    for (const auto& other : cases)
                        if (other.field != item.field)
                            expectWithinAbsoluteError(sliceField(actual, other.field), sliceField(before, other.field), 1.0e-6f);
                }
            }
        }

        beginTest("Repeated values are successful no-op ACKs and do not advance the revision");
        {
            SliceEngine engine;
            expect(engine.setSample(804, 4096));
            const auto id = engine.slices().front().id;
            const struct { const char* name; double value; } defaults[] {
                { "volume", 1.0 }, { "pan", 0.0 }, { "pitch", 0.0 }, { "attack", 0.005 },
                { "decay", 0.1 }, { "sustain", 1.0 }, { "release", 0.1 }
            };
            for (const auto& item : defaults)
            {
                const auto revision = engine.revision();
                const auto before = engine.find(id)->parameters;
                const auto response = handleUiV2SliceParameterCommand(requestFor(engine, id, item.name, item.value),
                    engine, [&](const juce::String& requestId)
                    { return snapshotAck(requestId, engine, id); });
                const auto* ack = response.getDynamicObject();
                expect(ack != nullptr && ack->getProperty("ok").toString() == "1");
                expect(ack != nullptr && ack->getProperty("type").toString() == "ack");
                expect(ack != nullptr && ack->getProperty("result").getDynamicObject() != nullptr);
                expect(engine.revision() == revision);
                expect(engine.find(id)->parameters == before);
            }

            auto changed = handleUiV2SliceParameterCommand(requestFor(engine, id, "volume", 1.4), engine,
                [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
            expect(changed.getDynamicObject()->getProperty("ok").toString() == "1");
            const auto changedRevision = engine.revision();
            const auto repeated = handleUiV2SliceParameterCommand(requestFor(engine, id, "volume", 1.4), engine,
                [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
            expect(repeated.getDynamicObject()->getProperty("ok").toString() == "1");
            expect(engine.revision() == changedRevision);
            const auto* result = repeated.getDynamicObject()->getProperty("result").getDynamicObject();
            expect(result != nullptr && result->getProperty("revision").toString()
                == juce::String(static_cast<juce::int64>(changedRevision)));
        }

        beginTest("All seven parameters accept boundaries, near-boundaries, interior values and repeated values");
        {
            struct Case { const char* name; SliceField field; double minimum, maximum, interior; };
            const Case cases[] {
                { "volume", SliceField::volume, 0.0, 2.0, 0.73 },
                { "pan", SliceField::pan, -1.0, 1.0, 0.25 },
                { "pitch", SliceField::pitch, -24.0, 24.0, 7.5 },
                { "attack", SliceField::attack, 0.0, 10.0, 0.037 },
                { "decay", SliceField::decay, 0.0, 10.0, 0.42 },
                { "sustain", SliceField::sustain, 0.0, 1.0, 0.61 },
                { "release", SliceField::release, 0.0, 10.0, 1.25 }
            };
            for (const auto& item : cases)
            {
                SliceEngine engine;
                expect(engine.setSample(805, 4096));
                const auto id = engine.slices().front().id;
                const double values[] { item.minimum, item.maximum, item.minimum + 0.001,
                    item.maximum - 0.001, item.interior, item.interior };
                for (const auto requested : values)
                {
                    const auto previousRevision = engine.revision();
                    const auto previous = engine.find(id)->parameters;
                    const auto response = handleUiV2SliceParameterCommand(
                        requestFor(engine, id, item.name, requested), engine,
                        [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
                    const auto* ack = response.getDynamicObject();
                    expect(ack != nullptr && ack->getProperty("ok").toString() == "1");
                    if (ack == nullptr || ack->getProperty("ok").toString() != "1")
                        continue;
                    auto actual = engine.find(id)->parameters;
                    expectWithinAbsoluteError(sliceField(actual, item.field), static_cast<float>(requested), 1.0e-6f);
                    const auto* result = ack->getProperty("result").getDynamicObject();
                    expect(result != nullptr);
                    if (result != nullptr)
                    {
                        const auto* reported = result->getProperty("parameters").getDynamicObject();
                        expect(reported != nullptr);
                        if (reported != nullptr)
                            expectWithinAbsoluteError(static_cast<float>(reported->getProperty(item.name)),
                                                      static_cast<float>(requested), 1.0e-6f);
                    }
                    if (previous == actual)
                        expect(engine.revision() == previousRevision);
                    else
                        expect(engine.revision() == previousRevision + 1);
                }
            }
        }

        beginTest("Sustain edge and fast sequences preserve exact 0..1 values including integer JSON 1");
        {
            SliceEngine engine;
            expect(engine.setSample(806, 4096));
            const auto id = engine.slices().front().id;
            const double values[] { 0.0, 0.5, 0.99, 0.999, 1.0, 1.0,
                0.95, 0.98, 0.99, 1.0, 1.0, 0.99, 1.0 };
            for (const auto requested : values)
            {
                const auto previousRevision = engine.revision();
                const auto previousParameters = engine.find(id)->parameters;
                auto command = juce::JSON::parse(juce::JSON::toString(
                    requestFor(engine, id, "sustain", requested)));
                expectWithinAbsoluteError(static_cast<double>(command.getDynamicObject()->getProperty("value")),
                                          requested, 0.0);
                const auto response = handleUiV2SliceParameterCommand(command, engine,
                    [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
                const auto* ack = response.getDynamicObject();
                expect(ack != nullptr && ack->getProperty("ok").toString() == "1");
                expectWithinAbsoluteError(engine.find(id)->parameters.envelope.sustainLevel,
                                          static_cast<float>(requested), 1.0e-7f);
                if (previousParameters == engine.find(id)->parameters)
                    expect(engine.revision() == previousRevision);
                else
                    expect(engine.revision() == previousRevision + 1);
            }

            auto integerOne = requestFor(engine, id, "sustain", 1.0);
            integerOne.getDynamicObject()->setProperty("value", 1);
            const auto integerAck = handleUiV2SliceParameterCommand(integerOne, engine,
                [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
            expect(integerAck.getDynamicObject()->getProperty("ok").toString() == "1");
            expectWithinAbsoluteError(engine.find(id)->parameters.envelope.sustainLevel, 1.0f, 0.0f);

            const auto revision = engine.revision();
            const auto justOverOne = handleUiV2SliceParameterCommand(requestFor(engine, id, "sustain", 1.00000001), engine,
                [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
            const auto* overAck = justOverOne.getDynamicObject();
            expect(overAck != nullptr && overAck->getProperty("ok").toString() == "0");
            if (overAck != nullptr)
                expect(overAck->getProperty("error").getDynamicObject()->getProperty("code").toString()
                    == "INVALID_VALUE");
            expect(engine.revision() == revision);
        }

        beginTest("Malformed, stale and invalid commands return errors without changing slice state");
        {
            SliceEngine engine;
            expect(engine.setSample(802, 4096));
            const auto id = engine.slices().front().id;
            auto runRejected = [&](juce::var command, const juce::String& expectedCode)
            {
                const auto* beforeSlice = engine.find(id);
                const auto before = beforeSlice != nullptr
                    ? std::optional<SliceParameters>(beforeSlice->parameters) : std::nullopt;
                const auto revision = engine.revision();
                auto response = handleUiV2SliceParameterCommand(command, engine,
                    [&](const juce::String& requestId) { return snapshotAck(requestId, engine, id); });
                auto* ack = response.getDynamicObject();
                expect(ack != nullptr && ack->getProperty("ok").toString() == "0");
                if (ack != nullptr)
                {
                    auto* error = ack->getProperty("error").getDynamicObject();
                    expect(error != nullptr && error->getProperty("code").toString() == expectedCode);
                    expect(!ack->hasProperty("result"));
                }
                const auto* afterSlice = engine.find(id);
                expect((before.has_value() && afterSlice != nullptr
                        && afterSlice->parameters == *before)
                       || (!before.has_value() && afterSlice == nullptr));
                expect(engine.revision() == revision);
            };
            auto wrongProtocol = requestFor(engine, id, "volume", 0.5); wrongProtocol.getDynamicObject()->setProperty("protocolVersion", 99); runRejected(wrongProtocol, "UNSUPPORTED_PROTOCOL");
            runRejected(juce::var("bad json"), "INVALID_ARGUMENT");
            for (const auto* field : { "sampleId", "revision", "sliceId", "parameter", "value" })
            {
                auto missing = requestFor(engine, id, "volume", .5);
                missing.getDynamicObject()->removeProperty(field);
                runRejected(missing, "INVALID_ARGUMENT");
            }
            auto unknown = requestFor(engine, id, "masterVolume", .5); runRejected(unknown, "INVALID_PARAMETER");
            auto badId = requestFor(engine, id, "volume", .5); badId.getDynamicObject()->setProperty("sliceId", "1x"); runRejected(badId, "INVALID_ARGUMENT");
            auto staleSample = requestFor(engine, id, "volume", .5); staleSample.getDynamicObject()->setProperty("sampleId", "999"); runRejected(staleSample, "STALE_SAMPLE");
            auto staleRevision = requestFor(engine, id, "volume", .5); staleRevision.getDynamicObject()->setProperty("revision", "999"); runRejected(staleRevision, "STALE_REVISION");
            auto tooLow = requestFor(engine, id, "volume", -.01); runRejected(tooLow, "INVALID_VALUE");
            auto tooHigh = requestFor(engine, id, "volume", 2.01); runRejected(tooHigh, "INVALID_VALUE");
            auto nan = requestFor(engine, id, "volume", std::numeric_limits<double>::quiet_NaN()); runRejected(nan, "INVALID_VALUE");
            auto infinity = requestFor(engine, id, "volume", std::numeric_limits<double>::infinity()); runRejected(infinity, "INVALID_VALUE");
            auto negativeInfinity = requestFor(engine, id, "volume", -std::numeric_limits<double>::infinity()); runRejected(negativeInfinity, "INVALID_VALUE");
            auto nonexistent = requestFor(engine, id + 1000, "volume", .5); runRejected(nonexistent, "SLICE_NOT_FOUND");
            auto deleted = requestFor(engine, id, "volume", .5); expect(engine.deleteSlice(id)); runRejected(deleted, "STALE_REVISION");
            auto replaced = requestFor(engine, 1, "volume", .5); expect(engine.setSample(803, 4096)); runRejected(replaced, "STALE_SAMPLE");
        }
    }
};

UiV2SliceParameterCommandTest test;
}

juce::UnitTest* getUiV2SliceParameterCommandTest() { return &test; }
