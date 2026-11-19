#include "BandlimitedResampler.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr int halfTapCount = BandlimitedResampler::tapCount / 2;
constexpr double pi = 3.1415926535897932384626433832795;
constexpr std::size_t coefficientCount = static_cast<std::size_t>(BandlimitedResampler::cutoffSteps + 1)
    * BandlimitedResampler::phaseCount * BandlimitedResampler::tapCount;

struct CoefficientTable
{
    std::array<float, coefficientCount> values {};

    CoefficientTable() noexcept
    {
        for (int cutoffIndex = 0; cutoffIndex <= BandlimitedResampler::cutoffSteps; ++cutoffIndex)
        {
            const auto cutoff = static_cast<double>(juceCutoffIndex(cutoffIndex))
                / BandlimitedResampler::cutoffSteps;
            for (int phase = 0; phase < BandlimitedResampler::phaseCount; ++phase)
            {
                const auto fraction = static_cast<double>(phase) / BandlimitedResampler::phaseCount;
                auto sum = 0.0;
                const auto base = (static_cast<std::size_t>(cutoffIndex)
                                   * BandlimitedResampler::phaseCount + static_cast<std::size_t>(phase))
                    * BandlimitedResampler::tapCount;
                for (int tap = 0; tap < BandlimitedResampler::tapCount; ++tap)
                {
                    const auto offset = static_cast<double>(tap - halfTapCount) - fraction;
                    const auto distance = std::abs(offset) / static_cast<double>(halfTapCount + 1);
                    const auto window = distance < 1.0 ? 0.5 + 0.5 * std::cos(pi * distance) : 0.0;
                    const auto argument = cutoff * offset;
                    const auto sinc = std::abs(argument) < 1.0e-12
                        ? 1.0 : std::sin(pi * argument) / (pi * argument);
                    const auto coefficient = cutoff * sinc * window;
                    values[base + static_cast<std::size_t>(tap)] = static_cast<float>(coefficient);
                    sum += coefficient;
                }

                const auto inverseSum = 1.0 / sum;
                for (int tap = 0; tap < BandlimitedResampler::tapCount; ++tap)
                    values[base + static_cast<std::size_t>(tap)] = static_cast<float>(
                        static_cast<double>(values[base + static_cast<std::size_t>(tap)]) * inverseSum);
            }
        }
    }

    static constexpr int juceCutoffIndex(int index) noexcept
    {
        return index == 0 ? 1 : index;
    }
};

const CoefficientTable& sharedCoefficientTable() noexcept
{
    static const CoefficientTable table;
    return table;
}
}

BandlimitedResampler::BandlimitedResampler() noexcept
    : coefficients(sharedCoefficientTable().values.data())
{
}

float BandlimitedResampler::interpolate(const float* source, int sourceLength,
                                        double sourcePosition,
                                        double sourceFramesPerOutputFrame) const noexcept
{
    if (source == nullptr || sourceLength <= 0 || !std::isfinite(sourcePosition))
        return 0.0f;

    const auto clampedPosition = std::clamp(sourcePosition, 0.0,
                                             static_cast<double>(sourceLength - 1));
    auto centre = static_cast<int>(clampedPosition);
    const auto fraction = clampedPosition - static_cast<double>(centre);
    const auto roundedPhase = static_cast<int>(fraction * phaseCount + 0.5);
    auto phase = roundedPhase;
    if (phase == phaseCount)
    {
        ++centre;
        phase = 0;
    }

    const auto safeRatio = std::isfinite(sourceFramesPerOutputFrame)
        && sourceFramesPerOutputFrame > 0.0 ? sourceFramesPerOutputFrame : 1.0;
    const auto cutoffIndex = std::clamp(static_cast<int>(cutoffSteps / safeRatio), 1, cutoffSteps);
    const auto tableOffset = (static_cast<std::size_t>(cutoffIndex) * phaseCount
                              + static_cast<std::size_t>(phase)) * tapCount;

    auto output = 0.0f;
    for (int tap = 0; tap < tapCount; ++tap)
    {
        const auto sourceIndex = std::clamp(centre + tap - halfTapCount, 0, sourceLength - 1);
        output += source[sourceIndex] * coefficients[tableOffset + static_cast<std::size_t>(tap)];
    }
    return output;
}
