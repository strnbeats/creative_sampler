#include "WaveformData.h"
#include <algorithm>

WaveformData WaveformData::generate(const SampleStorage& sample, int baseFramesPerPeak,
                                    const std::function<bool()>& shouldCancel)
{
    WaveformData result;
    const auto& audio = sample.audio();
    result.numberOfChannels = audio.getNumChannels();
    if (audio.getNumSamples() <= 0 || result.numberOfChannels <= 0)
        return result;

    const auto bucketSize = juce::jmax(1, baseFramesPerPeak);
    Level base;
    base.framesPerPeak = bucketSize;
    base.channels.resize(static_cast<size_t>(result.numberOfChannels));
    const auto peakCount = (static_cast<juce::int64>(audio.getNumSamples()) + bucketSize - 1) / bucketSize;

    for (int channel = 0; channel < result.numberOfChannels; ++channel)
    {
        auto& peaks = base.channels[static_cast<size_t>(channel)];
        peaks.reserve(static_cast<size_t>(peakCount));
        const auto* source = audio.getReadPointer(channel);
        for (juce::int64 start = 0; start < audio.getNumSamples(); start += bucketSize)
        {
            if ((start % 4096) == 0 && shouldCancel && shouldCancel())
                return {};
            const auto startFrame = static_cast<int>(start);
            const auto endFrame = static_cast<int>(juce::jmin<juce::int64>(
                start + bucketSize, audio.getNumSamples()));
            auto minimum = source[startFrame];
            auto maximum = minimum;
            for (auto frame = startFrame + 1; frame < endFrame; ++frame)
            {
                minimum = juce::jmin(minimum, source[frame]);
                maximum = juce::jmax(maximum, source[frame]);
            }
            peaks.push_back({ minimum, maximum });
        }
    }
    result.levels.push_back(std::move(base));

    while (result.levels.back().channels.front().size() > 1)
    {
        const auto& previous = result.levels.back();
        Level next;
        next.framesPerPeak = previous.framesPerPeak * 2;
        next.channels.resize(static_cast<size_t>(result.numberOfChannels));
        for (int channel = 0; channel < result.numberOfChannels; ++channel)
        {
            if (shouldCancel && shouldCancel())
                return {};
            const auto& previousPeaks = previous.channels[static_cast<size_t>(channel)];
            auto& nextPeaks = next.channels[static_cast<size_t>(channel)];
            nextPeaks.reserve((previousPeaks.size() + 1) / 2);
            for (size_t i = 0; i < previousPeaks.size(); i += 2)
            {
                auto peak = previousPeaks[i];
                if (i + 1 < previousPeaks.size())
                {
                    peak.minimum = juce::jmin(peak.minimum, previousPeaks[i + 1].minimum);
                    peak.maximum = juce::jmax(peak.maximum, previousPeaks[i + 1].maximum);
                }
                nextPeaks.push_back(peak);
            }
        }
        result.levels.push_back(std::move(next));
    }
    return result;
}

int WaveformData::chooseLevel(double framesPerPixel) const noexcept
{
    if (levels.empty())
        return 0;
    auto selected = 0;
    for (int i = 1; i < static_cast<int>(levels.size()); ++i)
    {
        if (static_cast<double>(levels[static_cast<size_t>(i)].framesPerPeak) > framesPerPixel)
            break;
        selected = i;
    }
    return selected;
}
