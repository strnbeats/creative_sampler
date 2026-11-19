#pragma once

#include "../sample/SampleStorage.h"
#include <functional>
#include <vector>

struct WaveformPeak
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

// Multiresolution peak data. Constructed off the message and audio threads.
class WaveformData final
{
public:
    struct Level
    {
        juce::int64 framesPerPeak = 1;
        std::vector<std::vector<WaveformPeak>> channels;
    };

    static WaveformData generate(const SampleStorage& sample, int baseFramesPerPeak = 64,
                                 const std::function<bool()>& shouldCancel = {});

    int channelCount() const noexcept { return numberOfChannels; }
    int levelCount() const noexcept { return static_cast<int>(levels.size()); }
    const Level& level(int index) const noexcept { return levels[static_cast<size_t>(index)]; }
    int chooseLevel(double framesPerPixel) const noexcept;

private:
    int numberOfChannels = 0;
    std::vector<Level> levels;
};
