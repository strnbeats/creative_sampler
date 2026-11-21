#include "WaveformEditorComponent.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace
{
const auto background = juce::Colour(0xff13151b);
const auto muted = juce::Colour(0xffa3a4b2);
const auto cyan = juce::Colour(0xff72f2cf);
const auto violet = juce::Colour(0xffb98cff);

juce::String midiNoteName(int midiNote)
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const auto clampedNote = juce::jlimit(0, 127, midiNote);
    return juce::String(names[clampedNote % 12]) + juce::String(clampedNote / 12 - 1);
}
}

WaveformEditorComponent::WaveformEditorComponent(SliceEngine& sliceEngine)
    : slices(sliceEngine)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformEditorComponent::setSource(std::shared_ptr<const WaveformData> newWaveform,
                                        const std::optional<SampleMetadata>& newMetadata,
                                        std::uint64_t sampleId)
{
    const auto changedSample = displayedSampleId != sampleId;
    if (!changedSample && waveform == newWaveform)
        return;
    waveform = std::move(newWaveform);
    if (changedSample)
        metadata = newMetadata;
    displayedSampleId = sampleId;
    if (changedSample)
    {
        const auto& currentSlices = slices.slices();
        selectedSlice = currentSlices.empty() ? std::nullopt
                                              : std::optional<SliceId>(currentSlices.front().id);
        transientPositions.clear();
        transientSampleId = 0;
        dragMode = DragMode::none;
    }
    repaint();
}

void WaveformEditorComponent::setTransientAnalysis(const TransientAnalysis* analysis,
                                                  std::uint64_t sampleId)
{
    if (sampleId != slices.sampleId() || analysis == nullptr)
    {
        transientPositions.clear();
        transientSampleId = 0;
        repaint();
        return;
    }

    transientPositions.clear();
    try
    {
        if (analysis->detectionStatus != TransientDetectionStatus::insufficientData
            && analysis->sampleCount > 0)
        {
            transientPositions.reserve(std::min<size_t>(analysis->positions.size(), 65536));
            for (const auto& event : analysis->positions)
                transientPositions.push_back(event.normalizedPosition);
        }
    }
    catch (const std::bad_alloc&)
    {
        transientPositions.clear();
        transientSampleId = 0;
        repaint();
        return;
    }
    transientSampleId = sampleId;
    repaint();
}

void WaveformEditorComponent::setPlaybackPosition(double seconds) noexcept
{
    playbackSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    repaint();
}

bool WaveformEditorComponent::splitSelectedAtMidpoint()
{
    if (!selectedSlice)
        return false;
    const auto* slice = slices.find(*selectedSlice);
    if (slice == nullptr)
        return false;
    const auto midpoint = slice->startSample + slice->length() / 2;
    if (const auto newId = slices.splitSlice(slice->id, midpoint))
    {
        chooseSlice(*newId);
        return true;
    }
    return false;
}

bool WaveformEditorComponent::canSplitSelectedAtMidpoint() const noexcept
{
    if (!selectedSlice)
        return false;
    const auto* slice = slices.find(*selectedSlice);
    return slice != nullptr && slice->length() / 2 >= slices.minimumSliceLength()
        && slice->length() - slice->length() / 2 >= slices.minimumSliceLength();
}

bool WaveformEditorComponent::deleteSelectedSlice()
{
    if (!selectedSlice)
        return false;
    const auto& before = slices.slices();
    const auto found = std::find_if(before.begin(), before.end(), [this](const auto& slice)
    {
        return slice.id == *selectedSlice;
    });
    if (found == before.end())
        return false;
    const auto oldIndex = static_cast<size_t>(std::distance(before.begin(), found));
    if (!slices.deleteSlice(*selectedSlice))
        return false;
    const auto& after = slices.slices();
    selectedSlice = after.empty() ? std::nullopt
                                  : std::optional<SliceId>(after[std::min(oldIndex, after.size() - 1)].id);
    repaint();
    return true;
}

bool WaveformEditorComponent::resetSlices()
{
    if (!slices.resetSlices())
        return false;
    const auto& allSlices = slices.slices();
    selectedSlice = allSlices.empty() ? std::nullopt : std::optional<SliceId>(allSlices.front().id);
    repaint();
    return true;
}

void WaveformEditorComponent::paint(juce::Graphics& graphics)
{
    const auto card = getLocalBounds().toFloat();
    graphics.setColour(background);
    graphics.fillRoundedRectangle(card, 12.0f);
    graphics.setColour(juce::Colour(0xff383b47));
    graphics.drawRoundedRectangle(card, 12.0f, 1.0f);
    const auto bounds = waveformBounds();

    if (waveform != nullptr && metadata && metadata->sampleCount > 0)
    {
        drawWaveform(graphics, bounds);
        const auto sampleId = slices.sampleId();
        const auto& allSlices = slices.slices();
        for (size_t index = 0; index < allSlices.size(); ++index)
        {
            const auto& slice = allSlices[index];
            const auto left = xAtSample(slice.startSample);
            const auto right = xAtSample(slice.endSample);
            if (selectedSlice == slice.id)
            {
                graphics.setColour(cyan.withAlpha(0.12f));
                graphics.fillRect(juce::Rectangle<float>(left, bounds.getY(),
                                                          std::max(1.0f, right - left), bounds.getHeight()));
            }
            graphics.setColour(selectedSlice == slice.id ? cyan : violet.withAlpha(0.84f));
            const auto thickness = selectedSlice == slice.id ? 2.0f : 1.0f;
            graphics.fillRect(left - thickness * 0.5f, bounds.getY(), thickness, bounds.getHeight());
            if (index + 1 == allSlices.size() || allSlices[index + 1].startSample != slice.endSample)
                graphics.fillRect(right - thickness * 0.5f, bounds.getY(), thickness, bounds.getHeight());
            if (right - left > 35.0f)
            {
                graphics.setColour(selectedSlice == slice.id ? cyan : muted);
                graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                graphics.drawText("S" + juce::String(static_cast<juce::int64>(index + 1))
                                      + " · " + midiNoteName(slice.midiNote),
                                  juce::Rectangle<float>(left + 4.0f, bounds.getY() + 4.0f,
                                                         right - left - 8.0f, 16.0f),
                                  juce::Justification::centredLeft, false);
            }
        }

        if (hoveredBoundary)
        {
            if (const auto* slice = slices.find(hoveredBoundary->sliceId))
            {
                const auto sample = hoveredBoundary->boundary == SliceBoundary::start
                    ? slice->startSample : slice->endSample;
                const auto x = xAtSample(sample);
                graphics.setColour(juce::Colours::white.withAlpha(0.9f));
                graphics.fillEllipse(x - 4.0f, bounds.getY() + 4.0f, 8.0f, 8.0f);
            }
        }

        if (transientSampleId == sampleId)
        {
            graphics.setColour(juce::Colour(0xffffb45f).withAlpha(0.76f));
            const auto stride = std::max<size_t>(1, (transientPositions.size() + 1999) / 2000);
            for (size_t i = 0; i < transientPositions.size(); i += stride)
            {
                const auto x = bounds.getX() + static_cast<float>(
                    juce::jlimit(0.0, 1.0, transientPositions[i])) * bounds.getWidth();
                graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
            }
        }

        if (dragMode == DragMode::range)
        {
            const auto left = xAtSample(std::min(dragAnchorSample, dragCurrentSample));
            const auto right = xAtSample(std::max(dragAnchorSample, dragCurrentSample));
            graphics.setColour(cyan.withAlpha(0.2f));
            graphics.fillRect(juce::Rectangle<float>(left, bounds.getY(),
                                                      std::max(1.0f, right - left), bounds.getHeight()));
            graphics.setColour(cyan);
            graphics.drawRect(juce::Rectangle<float>(left, bounds.getY(),
                                                      std::max(1.0f, right - left), bounds.getHeight()), 1.0f);
        }

        const auto progress = juce::jlimit(0.0, 1.0, playbackSeconds / metadata->durationSeconds());
        const auto playheadX = bounds.getX() + static_cast<float>(progress) * bounds.getWidth();
        graphics.setColour(juce::Colour(0xffff6b91));
        graphics.drawVerticalLine(juce::roundToInt(playheadX), bounds.getY(), bounds.getBottom());

        graphics.setColour(muted);
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText(juce::String(static_cast<juce::int64>(allSlices.size())) + " slices",
                          getLocalBounds().removeFromTop(22).reduced(10, 0),
                          juce::Justification::centredRight, false);
        graphics.drawText("Drag to create  ·  drag a boundary to move",
                          getLocalBounds().removeFromTop(22).reduced(10, 0),
                          juce::Justification::centredLeft, false);
    }
    else
    {
        graphics.setColour(muted);
        graphics.setFont(juce::FontOptions(15.0f));
        graphics.drawText("Import or drop a WAV / AIFF file", getLocalBounds().reduced(18),
                          juce::Justification::centred);
    }
}

void WaveformEditorComponent::mouseMove(const juce::MouseEvent& event)
{
    hoveredBoundary = waveformBounds().contains(event.position)
        ? hitBoundary(event.position.x) : std::nullopt;
    setMouseCursor(hoveredBoundary ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformEditorComponent::mouseExit(const juce::MouseEvent&)
{
    hoveredBoundary.reset();
    if (dragMode == DragMode::none)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveformEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || waveform == nullptr || !metadata
        || !waveformBounds().contains(event.position))
        return;
    mouseDownPosition = event.position;
    const auto position = sampleAtX(event.position.x);
    if (const auto boundary = hitBoundary(event.position.x))
    {
        dragMode = DragMode::boundary;
        draggedBoundary = *boundary;
        chooseSlice(boundary->sliceId);
        return;
    }
    if (const auto slice = sliceAt(position))
        chooseSlice(*slice);
    dragAnchorSample = position;
    dragCurrentSample = position;
    dragMode = DragMode::range;
}

void WaveformEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode == DragMode::boundary)
    {
        (void) slices.moveBoundary(draggedBoundary.sliceId, draggedBoundary.boundary,
                                   sampleAtX(event.position.x));
        repaint();
        return;
    }
    if (dragMode == DragMode::range)
    {
        dragCurrentSample = sampleAtX(event.position.x);
        repaint();
    }
}

void WaveformEditorComponent::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode == DragMode::range
        && mouseDownPosition.getDistanceFrom(event.position) >= 4.0f)
    {
        const auto start = std::min(dragAnchorSample, sampleAtX(event.position.x));
        const auto end = std::max(dragAnchorSample, sampleAtX(event.position.x));
        if (const auto created = slices.createSlice(start, end))
            chooseSlice(*created);
    }
    dragMode = DragMode::none;
    repaint();
}

juce::Rectangle<float> WaveformEditorComponent::waveformBounds() const noexcept
{
    return getLocalBounds().toFloat().reduced(12.0f, 22.0f);
}

std::int64_t WaveformEditorComponent::sampleAtX(float x) const noexcept
{
    if (!metadata || metadata->sampleCount <= 0)
        return 0;
    const auto bounds = waveformBounds();
    const auto proportion = juce::jlimit(0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return static_cast<std::int64_t>(std::llround(
        static_cast<double>(proportion) * static_cast<double>(metadata->sampleCount)));
}

float WaveformEditorComponent::xAtSample(std::int64_t sample) const noexcept
{
    if (!metadata || metadata->sampleCount <= 0)
        return waveformBounds().getX();
    const auto proportion = juce::jlimit(0.0, 1.0,
        static_cast<double>(sample) / static_cast<double>(metadata->sampleCount));
    const auto bounds = waveformBounds();
    return bounds.getX() + static_cast<float>(proportion) * bounds.getWidth();
}

std::optional<WaveformEditorComponent::BoundaryHit>
WaveformEditorComponent::hitBoundary(float x) const noexcept
{
    if (!metadata || metadata->sampleCount <= 0)
        return std::nullopt;
    constexpr float hitTolerance = 7.0f;
    std::optional<BoundaryHit> closest;
    auto closestDistance = hitTolerance + 1.0f;
    for (const auto& slice : slices.slices())
    {
        for (const auto boundary : { SliceBoundary::start, SliceBoundary::end })
        {
            const auto position = boundary == SliceBoundary::start ? slice.startSample : slice.endSample;
            const auto distance = std::abs(xAtSample(position) - x);
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closest = BoundaryHit { slice.id, boundary };
            }
        }
    }
    return closest;
}

std::optional<SliceId> WaveformEditorComponent::sliceAt(std::int64_t sample) const noexcept
{
    const auto& allSlices = slices.slices();
    const auto found = std::find_if(allSlices.begin(), allSlices.end(), [sample](const auto& slice)
    {
        return sample >= slice.startSample && sample < slice.endSample;
    });
    return found == allSlices.end() ? std::nullopt : std::optional<SliceId>(found->id);
}

void WaveformEditorComponent::chooseSlice(SliceId id)
{
    selectedSlice = slices.find(id) != nullptr ? std::optional<SliceId>(id) : std::nullopt;
    repaint();
}

void WaveformEditorComponent::drawWaveform(juce::Graphics& graphics,
                                           juce::Rectangle<float> bounds) const
{
    if (waveform == nullptr || !metadata || metadata->sampleCount <= 0 || waveform->levelCount() == 0)
        return;
    const auto framesPerPixel = static_cast<double>(metadata->sampleCount) / bounds.getWidth();
    const auto& level = waveform->level(waveform->chooseLevel(framesPerPixel));
    const auto channelCount = juce::jmin(waveform->channelCount(), 2);
    const auto channelCountF = static_cast<float>(channelCount);
    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto channelBounds = bounds.withY(bounds.getY() + bounds.getHeight()
                                                 * static_cast<float>(channel) / channelCountF)
                                         .withHeight(bounds.getHeight() / channelCountF);
        const auto centreY = channelBounds.getCentreY();
        graphics.setColour(juce::Colour(0xff343743));
        graphics.drawHorizontalLine(juce::roundToInt(centreY), channelBounds.getX(), channelBounds.getRight());
        const auto& peaks = level.channels[static_cast<size_t>(channel)];
        const auto pixels = juce::jmax(1, juce::roundToInt(channelBounds.getWidth()));
        graphics.setColour(channel == 0 ? cyan : juce::Colour(0xff68b8ff));
        for (int x = 0; x < pixels; ++x)
        {
            const auto frame = static_cast<juce::int64>((static_cast<double>(x) / pixels)
                                                        * static_cast<double>(metadata->sampleCount));
            const auto peakIndex = static_cast<size_t>(juce::jlimit<juce::int64>(
                0, static_cast<juce::int64>(peaks.size() - 1), frame / level.framesPerPeak));
            const auto peak = peaks[peakIndex];
            const auto top = centreY - peak.maximum * channelBounds.getHeight() * 0.46f;
            const auto bottom = centreY - peak.minimum * channelBounds.getHeight() * 0.46f;
            graphics.drawVerticalLine(juce::roundToInt(channelBounds.getX() + static_cast<float>(x)), top, bottom);
        }
    }
}
