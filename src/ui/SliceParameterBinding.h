#pragma once
#include <exception>
#include "../slice/SliceEngine.h"

enum class SliceField { attack, decay, sustain, release, volume, pan, pitch };

inline float& sliceField(SliceParameters& p, SliceField field) noexcept
{
    switch (field)
    {
        case SliceField::attack: return p.envelope.attackSeconds;
        case SliceField::decay: return p.envelope.decaySeconds;
        case SliceField::sustain: return p.envelope.sustainLevel;
        case SliceField::release: return p.envelope.releaseSeconds;
        case SliceField::volume: return p.volume;
        case SliceField::pan: return p.pan;
        case SliceField::pitch: return p.pitchSemitones;
    }
    std::terminate();
}

inline bool editSliceField(SliceEngine& model, std::uint64_t sample, SliceId id,
                           SliceField field, float value)
{
    if (model.sampleId() != sample)
        return false;
    if (const auto* slice = model.find(id))
    {
        auto parameters = slice->parameters;
        sliceField(parameters, field) = value;
        return model.setParameters(id, parameters);
    }
    return false;
}
