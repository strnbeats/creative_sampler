#include "SlicePlaybackSnapshot.h"
#include <algorithm>
#include <cmath>
#include <new>

SlicePlaybackSnapshot::SlicePlaybackSnapshot(
    SampleHandle sample, std::uint64_t sampleId, std::uint64_t revision,
    std::vector<SlicePlaybackRegion> regions,
    std::array<std::vector<std::size_t>, midiNoteCount> noteMap)
    : sourceSample(std::move(sample)), sourceSampleId(sampleId), sourceRevision(revision),
      sliceRegions(std::move(regions)), noteRegions(std::move(noteMap))
{
}

std::unique_ptr<const SlicePlaybackSnapshot> SlicePlaybackSnapshot::create(
    SampleHandle sample, std::uint64_t expectedSampleId, const SliceEngine& sliceEngine)
{
    if (sample == nullptr || !sliceEngine.isValid() || expectedSampleId == 0
        || sliceEngine.sampleId() != expectedSampleId
        || sample->metadata().sampleCount != sliceEngine.sampleCount()
        || sample->audio().getNumSamples() != sliceEngine.sampleCount()
        || !std::isfinite(sample->metadata().sampleRate) || sample->metadata().sampleRate <= 0.0
        || sample->audio().getNumChannels() < 1 || sample->audio().getNumChannels() > 2)
        return nullptr;

    try
    {
        std::vector<SlicePlaybackRegion> regions;
        regions.reserve(sliceEngine.slices().size());
        std::array<std::vector<std::size_t>, midiNoteCount> noteMap;
        for (const auto& slice : sliceEngine.slices())
        {
            if (slice.midiNote < 0 || slice.midiNote >= midiNoteCount)
                return nullptr;
            const auto index = regions.size();
            regions.push_back({ slice.id, slice.startSample, slice.endSample,
                                slice.midiNote, slice.parameters });
            noteMap[static_cast<std::size_t>(slice.midiNote)].push_back(index);
        }

        auto snapshot = std::unique_ptr<const SlicePlaybackSnapshot>(new SlicePlaybackSnapshot(
            std::move(sample), sliceEngine.sampleId(), sliceEngine.revision(),
            std::move(regions), std::move(noteMap)));
        return snapshot->isValid() ? std::move(snapshot) : nullptr;
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

const std::vector<std::size_t>& SlicePlaybackSnapshot::regionsForNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount)
        return emptyRegions;
    return noteRegions[static_cast<std::size_t>(midiNote)];
}

bool SlicePlaybackSnapshot::isValid() const noexcept
{
    if (sourceSample == nullptr || sourceSampleId == 0
        || sourceSample->metadata().sampleCount != sourceSample->audio().getNumSamples()
        || !std::isfinite(sourceSample->metadata().sampleRate)
        || sourceSample->metadata().sampleRate <= 0.0
        || sourceSample->audio().getNumChannels() < 1 || sourceSample->audio().getNumChannels() > 2)
        return false;

    const auto sampleCount = static_cast<std::int64_t>(sourceSample->audio().getNumSamples());
    std::int64_t previousEnd = 0;
    for (const auto& region : sliceRegions)
    {
        if (region.id == 0 || region.midiNote < 0 || region.midiNote >= midiNoteCount
            || !region.parameters.isValid()
            || region.startSample < 0 || region.endSample <= region.startSample
            || region.endSample > sampleCount || region.startSample < previousEnd)
            return false;
        previousEnd = region.endSample;
    }
    return true;
}
