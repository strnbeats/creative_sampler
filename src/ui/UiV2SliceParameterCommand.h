#pragma once

#include "SliceParameterBinding.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

inline bool uiV2SliceParameterValueInRange(SliceField field, double value) noexcept
{
    if (!std::isfinite(value))
        return false;

    switch (field)
    {
        case SliceField::volume:  return value >= 0.0 && value <= 2.0;
        case SliceField::pan:     return value >= -1.0 && value <= 1.0;
        case SliceField::pitch:   return value >= -24.0 && value <= 24.0;
        case SliceField::attack:
        case SliceField::decay:
        case SliceField::release: return value >= 0.0 && value <= 10.0;
        case SliceField::sustain: return value >= 0.0 && value <= 1.0;
    }
    return false;
}

inline juce::var uiV2SliceParameterError(const juce::String& requestId,
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

inline juce::var handleUiV2SliceParameterCommand(
    const juce::var& command, SliceEngine& engine,
    const std::function<juce::var(const juce::String&)>& makeSnapshotAck)
{
    const auto* object = command.getDynamicObject();
    const auto requestId = object != nullptr ? object->getProperty("requestId").toString() : juce::String();
    if (object == nullptr)
        return uiV2SliceParameterError(requestId, "INVALID_ARGUMENT", "Expected a protocol v2 request object.");
    const auto protocol = object->getProperty("protocolVersion");
    if ((!protocol.isInt() && !protocol.isInt64() && !protocol.isDouble())
        || protocol.toString().getDoubleValue() != 2.0)
        return uiV2SliceParameterError(requestId, "UNSUPPORTED_PROTOCOL", "This UI supports bridge protocol version 2.");
    if (requestId.isEmpty() || requestId.length() > 128)
        return uiV2SliceParameterError(requestId, "INVALID_ARGUMENT", "A non-empty requestId is required.");

    const auto sampleText = object->getProperty("sampleId").toString();
    const auto sliceText = object->getProperty("sliceId").toString();
    const auto revisionText = object->getProperty("revision").toString();
    const auto fieldName = object->getProperty("parameter").toString();
    const auto valueVar = object->getProperty("value");
    if (sampleText.isEmpty() || sliceText.isEmpty() || revisionText.isEmpty()
        || !object->hasProperty("parameter") || !object->hasProperty("value")
        || (!valueVar.isDouble() && !valueVar.isInt() && !valueVar.isInt64()))
        return uiV2SliceParameterError(requestId, "INVALID_ARGUMENT", "Sample, slice, revision, parameter and numeric value are required.");

    const auto field = fieldName == "volume" ? std::optional<SliceField>(SliceField::volume)
        : fieldName == "pan" ? std::optional<SliceField>(SliceField::pan)
        : fieldName == "pitch" ? std::optional<SliceField>(SliceField::pitch)
        : fieldName == "attack" ? std::optional<SliceField>(SliceField::attack)
        : fieldName == "decay" ? std::optional<SliceField>(SliceField::decay)
        : fieldName == "sustain" ? std::optional<SliceField>(SliceField::sustain)
        : fieldName == "release" ? std::optional<SliceField>(SliceField::release) : std::nullopt;
    if (!field)
        return uiV2SliceParameterError(requestId, "INVALID_PARAMETER", "Unknown slice parameter.");

    const auto toId = [](const juce::String& text, std::uint64_t& result)
    {
        if (text.isEmpty() || text.length() > 19 || !text.containsOnly("0123456789")) return false;
        const auto parsed = text.getLargeIntValue();
        if (parsed <= 0 || juce::String(parsed) != text) return false;
        result = static_cast<std::uint64_t>(parsed);
        return true;
    };
    std::uint64_t sampleId = 0, sliceId = 0, revision = 0;
    if (!toId(sampleText, sampleId) || !toId(sliceText, sliceId) || !toId(revisionText, revision))
        return uiV2SliceParameterError(requestId, "INVALID_ARGUMENT", "Sample, revision or slice ID is malformed.");
    if (sampleId != engine.sampleId())
        return uiV2SliceParameterError(requestId, "STALE_SAMPLE", "The sample changed before the parameter update.");
    if (revision != engine.revision())
        return uiV2SliceParameterError(requestId, "STALE_REVISION", "The slice state changed before the parameter update.");
    const auto* slice = engine.find(sliceId);
    if (slice == nullptr)
        return uiV2SliceParameterError(requestId, "SLICE_NOT_FOUND", "The slice no longer exists.");
    const auto requestedValue = static_cast<double>(valueVar);
    if (!uiV2SliceParameterValueInRange(*field, requestedValue))
        return uiV2SliceParameterError(requestId, "INVALID_VALUE", "Parameter value must be finite and within its supported range.");
    const auto value = static_cast<float>(requestedValue);
    auto parameters = slice->parameters;
    sliceField(parameters, *field) = value;
    if (!parameters.isValid())
        return uiV2SliceParameterError(requestId, "INVALID_VALUE", "Parameter value is outside its supported range.");
    // A repeated drag value is a valid idempotent command. SliceEngine::setParameters
    // returns false for an unchanged value, so acknowledge the authoritative no-op
    // before interpreting a false return as an application failure.
    if (parameters == slice->parameters)
        return makeSnapshotAck(requestId);
    if (!engine.setParameters(sliceId, parameters))
    {
        const auto* currentSlice = engine.find(sliceId);
        auto currentParameters = currentSlice != nullptr ? currentSlice->parameters : SliceParameters {};
        const auto currentValue = currentSlice != nullptr ? sliceField(currentParameters, *field) : 0.0f;
        const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        juce::Logger::writeToLog(
            "UI-v2 unexpected SliceEngine parameter rejection: parameter=" + fieldName
            + " value=" + juce::String(value, 9)
            + " sampleId=" + juce::String(static_cast<juce::int64>(sampleId))
            + " currentSampleId=" + juce::String(static_cast<juce::int64>(engine.sampleId()))
            + " sliceId=" + juce::String(static_cast<juce::int64>(sliceId))
            + " requestedRevision=" + juce::String(static_cast<juce::int64>(revision))
            + " currentRevision=" + juce::String(static_cast<juce::int64>(engine.revision()))
            + " sliceFound=" + juce::String(currentSlice != nullptr ? "true" : "false")
            + " currentValue=" + juce::String(currentValue, 9)
            + " messageThread=" + juce::String(messageManager != nullptr
                && messageManager->isThisTheMessageThread() ? "true" : "false"));
        return uiV2SliceParameterError(requestId, "INTERNAL", "The slice parameter could not be applied.");
    }
    return makeSnapshotAck(requestId);
}
