#pragma once

#include "../slice/SliceParameters.h"
#include <cstdint>

class ADSREnvelope final
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    bool prepare(double sampleRate) noexcept;
    bool setParameters(const ADSRParameters& parameters) noexcept;
    void noteOn() noexcept;
    void noteOff() noexcept;
    // Used only to guarantee a bounded fade at the end of a finite Slice.
    void finishWithinFrames(std::uint64_t frames) noexcept;
    float nextSample() noexcept;

    bool isActive() const noexcept { return currentStage != Stage::idle; }
    Stage stage() const noexcept { return currentStage; }
    float level() const noexcept { return currentLevel; }
    double preparedSampleRate() const noexcept { return sampleRate; }

private:
    std::uint64_t framesFor(float seconds) const noexcept;
    void begin(Stage newStage, float target, std::uint64_t frames) noexcept;
    void beginDecayOrSustain() noexcept;
    void advance() noexcept;
    void becomeIdle() noexcept;

    ADSRParameters settings {};
    double sampleRate = 0.0;
    float currentLevel = 0.0f;
    float segmentTarget = 0.0f;
    float increment = 0.0f;
    std::uint64_t framesRemaining = 0;
    Stage currentStage = Stage::idle;
};
