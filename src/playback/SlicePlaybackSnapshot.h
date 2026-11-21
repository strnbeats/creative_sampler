#pragma once

#include "../sample/SampleStorage.h"
#include "../slice/SliceEngine.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct SlicePlaybackRegion
{
    SliceId id = 0;
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;
    int midiNote = 60;
    SliceParameters parameters {};
};

// Built on a control thread and immutable once returned. It owns only a shared handle
// to the source SampleStorage, never a copy of the audio buffer.
class SlicePlaybackSnapshot final
{
public:
    static constexpr int midiNoteCount = 128;
    static constexpr int pitchReferenceNote = 60;

    static std::unique_ptr<const SlicePlaybackSnapshot> create(
        SampleHandle sample, std::uint64_t expectedSampleId, const SliceEngine& sliceEngine);

    std::uint64_t sampleId() const noexcept { return sourceSampleId; }
    std::uint64_t revision() const noexcept { return sourceRevision; }
    const SampleStorage& sample() const noexcept { return *sourceSample; }
    const SampleHandle& sampleHandle() const noexcept { return sourceSample; }
    const std::vector<SlicePlaybackRegion>& regions() const noexcept { return sliceRegions; }
    const std::vector<std::size_t>& regionsForNote(int midiNote) const noexcept;
    bool isValid() const noexcept;

private:
    SlicePlaybackSnapshot(SampleHandle sample, std::uint64_t sampleId, std::uint64_t revision,
                          std::vector<SlicePlaybackRegion> regions,
                          std::array<std::vector<std::size_t>, midiNoteCount> noteMap);

    const SampleHandle sourceSample;
    const std::uint64_t sourceSampleId;
    const std::uint64_t sourceRevision;
    const std::vector<SlicePlaybackRegion> sliceRegions;
    const std::array<std::vector<std::size_t>, midiNoteCount> noteRegions;
    const std::vector<std::size_t> emptyRegions;
};
