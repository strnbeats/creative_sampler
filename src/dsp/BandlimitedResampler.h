#pragma once

#include <cstddef>

// Stateless random-access polyphase windowed-sinc interpolation.
// The coefficient table is generated once when the engine is constructed,
// never from process().
class BandlimitedResampler final
{
public:
    static constexpr int tapCount = 63;
    static constexpr int phaseCount = 256;
    static constexpr int cutoffSteps = 64;
    static constexpr std::size_t tableMemoryBytes =
        static_cast<std::size_t>(cutoffSteps + 1) * phaseCount * tapCount * sizeof(float);

    BandlimitedResampler() noexcept;

    float interpolate(const float* source, int sourceLength, double sourcePosition,
                      double sourceFramesPerOutputFrame) const noexcept;
    float interpolateRange(const float* source, int sourceLength, int sourceStart,
                           int sourceEndExclusive, double sourcePosition,
                           double sourceFramesPerOutputFrame) const noexcept;

private:
    const float* coefficients = nullptr;
};
