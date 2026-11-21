#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using SliceId = std::uint64_t;

struct Slice
{
    SliceId id = 0;
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;
    int midiNote = 60;

    std::int64_t length() const noexcept { return endSample - startSample; }
};

enum class SliceBoundary
{
    start,
    end
};

// Message-thread model for edits. Audio code must consume a future immutable snapshot,
// never this mutable collection.
class SliceEngine final
{
public:
    static constexpr std::int64_t defaultMinimumSliceLength = 64;

    explicit SliceEngine(std::int64_t minimumSliceLength = defaultMinimumSliceLength) noexcept;

    // Binds a new sample identity and starts with one whole-sample slice when it is long
    // enough. Short but valid samples are bound with an empty slice collection.
    bool setSample(std::uint64_t sampleId, std::int64_t sampleCount);
    void clear() noexcept;
    bool resetSlices();

    std::optional<SliceId> createSlice(std::int64_t startSample, std::int64_t endSample);
    std::optional<SliceId> splitSlice(SliceId id, std::int64_t splitSample);
    bool deleteSlice(SliceId id) noexcept;
    bool moveBoundary(SliceId id, SliceBoundary boundary, std::int64_t samplePosition) noexcept;
    bool setMidiNote(SliceId id, int midiNote) noexcept;

    const std::vector<Slice>& slices() const noexcept { return sliceList; }
    const Slice* find(SliceId id) const noexcept;
    std::uint64_t sampleId() const noexcept { return sourceSampleId; }
    std::int64_t sampleCount() const noexcept { return sourceSampleCount; }
    std::int64_t minimumSliceLength() const noexcept { return minimumLength; }
    std::uint64_t revision() const noexcept { return stateRevision; }
    bool isValid() const noexcept;

private:
    SliceId allocateId() noexcept;
    std::int64_t minimumLength;
    std::uint64_t sourceSampleId = 0;
    std::int64_t sourceSampleCount = 0;
    SliceId nextSliceId = 1;
    std::uint64_t stateRevision = 0;
    std::vector<Slice> sliceList;
};
