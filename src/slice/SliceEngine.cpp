#include "SliceEngine.h"
#include <algorithm>
#include <limits>
#include <new>

namespace
{
constexpr int firstDefaultMidiNote = 60;
constexpr int defaultMidiNoteCount = 68; // C4 through G#9, then wrap for further slices.

Slice makeSlice(SliceId id, std::int64_t startSample, std::int64_t endSample)
{
    const auto note = firstDefaultMidiNote
        + static_cast<int>((id - 1) % static_cast<SliceId>(defaultMidiNoteCount));
    return { id, startSample, endSample, note };
}
}

SliceEngine::SliceEngine(std::int64_t requestedMinimumLength) noexcept
    : minimumLength(std::max<std::int64_t>(1, requestedMinimumLength))
{
}

bool SliceEngine::setSample(std::uint64_t sampleId, std::int64_t sampleCount)
{
    if (sampleId == 0 || sampleCount <= 0)
    {
        clear();
        return false;
    }

    sourceSampleId = sampleId;
    sourceSampleCount = sampleCount;
    sliceList.clear();
    if (sampleCount >= minimumLength)
    {
        const auto id = allocateId();
        if (id == 0)
        {
            ++stateRevision;
            return false;
        }
        try
        {
            sliceList.push_back(makeSlice(id, 0, sampleCount));
        }
        catch (const std::bad_alloc&)
        {
            ++stateRevision;
            return false;
        }
    }
    ++stateRevision;
    return true;
}

void SliceEngine::clear() noexcept
{
    sourceSampleId = 0;
    sourceSampleCount = 0;
    sliceList.clear();
    ++stateRevision;
}

bool SliceEngine::resetSlices()
{
    if (sourceSampleId == 0 || sourceSampleCount < minimumLength)
        return false;

    try
    {
        sliceList.reserve(1);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    const auto id = allocateId();
    if (id == 0)
        return false;
    sliceList.clear();
    sliceList.push_back(makeSlice(id, 0, sourceSampleCount));
    ++stateRevision;
    return true;
}

std::optional<SliceId> SliceEngine::createSlice(std::int64_t startSample, std::int64_t endSample)
{
    if (sourceSampleId == 0 || startSample < 0 || endSample <= startSample
        || endSample > sourceSampleCount
        || endSample - startSample < minimumLength)
        return std::nullopt;

    const auto overlapStart = std::upper_bound(sliceList.begin(), sliceList.end(), startSample,
        [](std::int64_t position, const Slice& slice) { return position < slice.endSample; });
    const auto first = static_cast<size_t>(std::distance(sliceList.begin(), overlapStart));
    size_t overlapCount = 0;
    for (auto i = first; i < sliceList.size() && sliceList[i].startSample < endSample; ++i)
        ++overlapCount;

    if (overlapCount > 1)
        return std::nullopt;
    if (overlapCount == 0)
    {
        const auto id = allocateId();
        if (id == 0)
            return std::nullopt;
        try
        {
            sliceList.insert(sliceList.begin() + static_cast<std::ptrdiff_t>(first),
                             makeSlice(id, startSample, endSample));
        }
        catch (const std::bad_alloc&)
        {
            return std::nullopt;
        }
        ++stateRevision;
        return id;
    }

    const auto original = sliceList[first];
    if (startSample < original.startSample || endSample > original.endSample)
        return std::nullopt;
    if (startSample == original.startSample && endSample == original.endSample)
        return original.id;

    const auto leftLength = startSample - original.startSample;
    const auto rightLength = original.endSample - endSample;
    if ((leftLength > 0 && leftLength < minimumLength)
        || (rightLength > 0 && rightLength < minimumLength))
        return std::nullopt;

    const auto createdId = leftLength == 0 ? original.id : allocateId();
    if (createdId == 0)
        return std::nullopt;
    std::vector<Slice> replacement;
    try
    {
        replacement.reserve(static_cast<size_t>((leftLength > 0) + 1 + (rightLength > 0)));
        if (leftLength > 0)
            replacement.push_back(makeSlice(original.id, original.startSample, startSample));
        auto created = makeSlice(createdId, startSample, endSample);
        if (createdId == original.id)
            created.midiNote = original.midiNote;
        replacement.push_back(created);
        if (rightLength > 0)
        {
            const auto rightId = allocateId();
            if (rightId == 0)
                return std::nullopt;
            replacement.push_back(makeSlice(rightId, endSample, original.endSample));
        }
        sliceList.reserve(sliceList.size() + replacement.size() - 1);
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
    sliceList.erase(sliceList.begin() + static_cast<std::ptrdiff_t>(first));
    sliceList.insert(sliceList.begin() + static_cast<std::ptrdiff_t>(first),
                     replacement.begin(), replacement.end());
    ++stateRevision;
    return createdId;
}

std::optional<SliceId> SliceEngine::splitSlice(SliceId id, std::int64_t splitSample)
{
    const auto found = std::find_if(sliceList.begin(), sliceList.end(),
        [id](const Slice& slice) { return slice.id == id; });
    if (found == sliceList.end() || splitSample <= found->startSample
        || splitSample >= found->endSample
        || splitSample - found->startSample < minimumLength
        || found->endSample - splitSample < minimumLength)
        return std::nullopt;

    const auto index = static_cast<size_t>(std::distance(sliceList.begin(), found));
    const auto originalEnd = found->endSample;
    const auto rightId = allocateId();
    if (rightId == 0)
        return std::nullopt;
    try
    {
        sliceList.insert(sliceList.begin() + static_cast<std::ptrdiff_t>(index + 1),
                     makeSlice(rightId, splitSample, originalEnd));
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
    sliceList[index].endSample = splitSample;
    ++stateRevision;
    return rightId;
}

bool SliceEngine::deleteSlice(SliceId id) noexcept
{
    const auto found = std::find_if(sliceList.begin(), sliceList.end(),
        [id](const Slice& slice) { return slice.id == id; });
    if (found == sliceList.end())
        return false;
    sliceList.erase(found);
    ++stateRevision;
    return true;
}

bool SliceEngine::moveBoundary(SliceId id, SliceBoundary boundary,
                               std::int64_t samplePosition) noexcept
{
    const auto found = std::find_if(sliceList.begin(), sliceList.end(),
        [id](const Slice& slice) { return slice.id == id; });
    if (found == sliceList.end())
        return false;

    const auto index = static_cast<size_t>(std::distance(sliceList.begin(), found));
    if (boundary == SliceBoundary::start)
    {
        if (samplePosition == found->startSample)
            return false;
        const auto paired = index > 0 && sliceList[index - 1].endSample == found->startSample;
        const auto lower = paired ? sliceList[index - 1].startSample + minimumLength
                                  : (index > 0 ? sliceList[index - 1].endSample : 0);
        if (samplePosition < lower || samplePosition > found->endSample - minimumLength
            || samplePosition > sourceSampleCount)
            return false;
        if (paired)
            sliceList[index - 1].endSample = samplePosition;
        found->startSample = samplePosition;
    }
    else
    {
        if (samplePosition == found->endSample)
            return false;
        const auto paired = index + 1 < sliceList.size()
            && found->endSample == sliceList[index + 1].startSample;
        const auto upper = paired ? sliceList[index + 1].endSample - minimumLength
                                  : (index + 1 < sliceList.size()
                                      ? sliceList[index + 1].startSample : sourceSampleCount);
        if (samplePosition < found->startSample + minimumLength || samplePosition > upper
            || samplePosition < 0)
            return false;
        found->endSample = samplePosition;
        if (paired)
            sliceList[index + 1].startSample = samplePosition;
    }
    ++stateRevision;
    return true;
}

bool SliceEngine::setMidiNote(SliceId id, int midiNote) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    const auto found = std::find_if(sliceList.begin(), sliceList.end(),
        [id](const Slice& slice) { return slice.id == id; });
    if (found == sliceList.end() || found->midiNote == midiNote)
        return false;
    found->midiNote = midiNote;
    ++stateRevision;
    return true;
}

const Slice* SliceEngine::find(SliceId id) const noexcept
{
    const auto found = std::find_if(sliceList.begin(), sliceList.end(),
        [id](const Slice& slice) { return slice.id == id; });
    return found == sliceList.end() ? nullptr : &*found;
}

bool SliceEngine::isValid() const noexcept
{
    if ((sourceSampleId == 0) != (sourceSampleCount == 0) || sourceSampleCount < 0)
        return false;
    std::int64_t previousEnd = 0;
    bool first = true;
    SliceId largestId = 0;
    for (size_t i = 0; i < sliceList.size(); ++i)
    {
        const auto& slice = sliceList[i];
        if (slice.id == 0 || slice.midiNote < 0 || slice.midiNote > 127
            || slice.startSample < 0 || slice.endSample <= slice.startSample
            || slice.endSample > sourceSampleCount || slice.endSample - slice.startSample < minimumLength
            || (!first && slice.startSample < previousEnd))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (sliceList[j].id == slice.id)
                return false;
        first = false;
        previousEnd = slice.endSample;
        largestId = std::max(largestId, slice.id);
    }
    return nextSliceId > largestId;
}

SliceId SliceEngine::allocateId() noexcept
{
    if (nextSliceId == std::numeric_limits<SliceId>::max())
        return 0;
    return nextSliceId++;
}
