#pragma once

#include <juce_core/juce_core.h>
#include <charconv>
#include <cstdint>
#include <functional>
#include <string>

inline juce::var uiV2TransportError(const juce::String& requestId,
                                    const juce::String& code,
                                    const juce::String& message)
{
    auto error = juce::DynamicObject::Ptr(new juce::DynamicObject());
    error->setProperty("code", code);
    error->setProperty("message", message);
    error->setProperty("retryable", false);
    auto ack = juce::DynamicObject::Ptr(new juce::DynamicObject());
    ack->setProperty("protocolVersion", 2);
    ack->setProperty("type", "ack");
    ack->setProperty("requestId", requestId);
    ack->setProperty("ok", false);
    ack->setProperty("error", juce::var(error.get()));
    return juce::var(ack.get());
}

inline bool uiV2ParseTransportSampleId(const juce::String& text, std::uint64_t& result)
{
    if (text.isEmpty() || text.length() > 20 || !text.containsOnly("0123456789"))
        return false;
    const auto utf8 = text.toStdString();
    std::uint64_t parsed = 0;
    const auto conversion = std::from_chars(utf8.data(), utf8.data() + utf8.size(), parsed);
    if (conversion.ec != std::errc() || conversion.ptr != utf8.data() + utf8.size()
        || parsed == 0 || std::to_string(parsed) != utf8)
        return false;
    result = parsed;
    return true;
}

inline juce::var handleUiV2TransportCommand(
    const juce::var& command, std::uint64_t currentSampleId, bool sampleLoaded,
    const std::function<void()>& play, const std::function<void()>& stop)
{
    const auto* object = command.getDynamicObject();
    const auto requestId = object != nullptr ? object->getProperty("requestId").toString() : juce::String();
    if (object == nullptr)
        return uiV2TransportError(requestId, "INVALID_ARGUMENT", "Expected a protocol v2 request object.");

    const auto protocol = object->getProperty("protocolVersion");
    if ((!protocol.isInt() && !protocol.isInt64() && !protocol.isDouble())
        || protocol.toString().getDoubleValue() != 2.0)
        return uiV2TransportError(requestId, "UNSUPPORTED_PROTOCOL", "This UI supports bridge protocol version 2.");
    if (requestId.isEmpty() || requestId.length() > 128)
        return uiV2TransportError(requestId, "INVALID_ARGUMENT", "A non-empty requestId is required.");

    const auto sampleVar = object->getProperty("sampleId");
    const auto actionVar = object->getProperty("action");
    if (!object->hasProperty("sampleId") || !object->hasProperty("action") || !actionVar.isString())
        return uiV2TransportError(requestId, "INVALID_ARGUMENT", "Sample ID and transport action are required.");

    const auto action = actionVar.toString();
    if (action != "play" && action != "stop")
        return uiV2TransportError(requestId, "INVALID_ARGUMENT", "Transport action must be play or stop.");

    std::uint64_t requestedSampleId = 0;
    if (!uiV2ParseTransportSampleId(sampleVar.toString(), requestedSampleId))
        return uiV2TransportError(requestId, "INVALID_ARGUMENT", "Sample ID is malformed.");
    if (!sampleLoaded || currentSampleId == 0)
        return uiV2TransportError(requestId, "NO_SAMPLE", "Load a sample before using transport controls.");
    if (requestedSampleId != currentSampleId)
        return uiV2TransportError(requestId, "STALE_SAMPLE", "The sample changed before the transport command.");

    if (action == "play") play(); else stop();

    auto result = juce::DynamicObject::Ptr(new juce::DynamicObject());
    result->setProperty("protocolVersion", 2);
    result->setProperty("type", "transportCommand");
    result->setProperty("sampleId", juce::String(std::to_string(currentSampleId)));
    result->setProperty("action", action);
    auto ack = juce::DynamicObject::Ptr(new juce::DynamicObject());
    ack->setProperty("protocolVersion", 2);
    ack->setProperty("type", "ack");
    ack->setProperty("requestId", requestId);
    ack->setProperty("ok", true);
    ack->setProperty("result", juce::var(result.get()));
    return juce::var(ack.get());
}
