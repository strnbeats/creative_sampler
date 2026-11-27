#include "UiV2TransportCommandTests.h"
#include "../src/ui/UiV2TransportCommand.h"
#include <limits>

namespace
{
juce::var requestFor(const juce::String& action, const juce::String& sampleId = "42")
{
    auto request = juce::DynamicObject::Ptr(new juce::DynamicObject());
    request->setProperty("protocolVersion", 2);
    request->setProperty("requestId", "transport-test");
    request->setProperty("action", action);
    request->setProperty("sampleId", sampleId);
    return juce::var(request.get());
}

class UiV2TransportCommandTest final : public juce::UnitTest
{
public:
    UiV2TransportCommandTest() : UnitTest("UI v2 Transport Bridge Contract", "Bridge") {}

    void runTest() override
    {
        beginTest("Play and stop commands dispatch once and return typed ACKs");
        {
            int playCalls = 0;
            int stopCalls = 0;
            const auto play = handleUiV2TransportCommand(requestFor("play"), 42, true,
                [&] { ++playCalls; }, [&] { ++stopCalls; });
            auto* playAck = play.getDynamicObject();
            expect(playAck != nullptr && playAck->getProperty("ok").toString() == "1");
            expect(playAck != nullptr && playAck->getProperty("requestId").toString() == "transport-test");
            auto* playResult = playAck != nullptr ? playAck->getProperty("result").getDynamicObject() : nullptr;
            expect(playResult != nullptr && playResult->getProperty("type").toString() == "transportCommand");
            expect(playResult != nullptr && playResult->getProperty("action").toString() == "play");
            expect(playResult != nullptr && playResult->getProperty("sampleId").toString() == "42");

            const auto stop = handleUiV2TransportCommand(requestFor("stop"), 42, true,
                [&] { ++playCalls; }, [&] { ++stopCalls; });
            expect(stop.getDynamicObject()->getProperty("ok").toString() == "1");
            expect(playCalls == 1 && stopCalls == 1);
        }

        beginTest("No sample, stale sample and malformed commands are rejected without dispatch");
        {
            int dispatches = 0;
            auto expectRejected = [&](juce::var command, std::uint64_t sampleId, bool loaded,
                                      const juce::String& expectedCode)
            {
                const auto response = handleUiV2TransportCommand(command, sampleId, loaded,
                    [&] { ++dispatches; }, [&] { ++dispatches; });
                const auto* ack = response.getDynamicObject();
                expect(ack != nullptr && ack->getProperty("ok").toString() == "0");
                expect(ack != nullptr && !ack->hasProperty("result"));
                const auto* error = ack != nullptr ? ack->getProperty("error").getDynamicObject() : nullptr;
                expect(error != nullptr && error->getProperty("code").toString() == expectedCode);
            };

            expectRejected(requestFor("play"), 0, false, "NO_SAMPLE");
            expectRejected(requestFor("play", "41"), 42, true, "STALE_SAMPLE");
            expectRejected(requestFor("rewind"), 42, true, "INVALID_ARGUMENT");
            auto wrongProtocol = requestFor("stop");
            wrongProtocol.getDynamicObject()->setProperty("protocolVersion", 99);
            expectRejected(wrongProtocol, 42, true, "UNSUPPORTED_PROTOCOL");
            auto missingSample = requestFor("play");
            missingSample.getDynamicObject()->removeProperty("sampleId");
            expectRejected(missingSample, 42, true, "INVALID_ARGUMENT");
            expectRejected(requestFor("stop", "42x"), 42, true, "INVALID_ARGUMENT");
            expect(dispatches == 0);
        }

        beginTest("Sample IDs retain full unsigned 64-bit precision");
        {
            constexpr auto maximumId = std::numeric_limits<std::uint64_t>::max();
            auto command = requestFor("play", "18446744073709551615");
            const auto response = handleUiV2TransportCommand(command, maximumId, true, [] {}, [] {});
            const auto* ack = response.getDynamicObject();
            const auto* result = ack != nullptr ? ack->getProperty("result").getDynamicObject() : nullptr;
            expect(ack != nullptr && ack->getProperty("ok").toString() == "1");
            expect(result != nullptr && result->getProperty("sampleId").toString() == "18446744073709551615");
        }
    }
};

UiV2TransportCommandTest test;
}

juce::UnitTest* getUiV2TransportCommandTest() { return &test; }
