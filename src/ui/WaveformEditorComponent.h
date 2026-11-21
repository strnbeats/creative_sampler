#pragma once

#include "../analysis/AnalysisTypes.h"
#include "../slice/SliceEngine.h"
#include "../waveform/WaveformData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

class WaveformEditorComponent final : public juce::Component
{
public:
    explicit WaveformEditorComponent(SliceEngine& sliceEngine);

    void setSource(std::shared_ptr<const WaveformData> waveform,
                   const std::optional<SampleMetadata>& metadata, std::uint64_t sampleId);
    void setTransientAnalysis(const TransientAnalysis* analysis, std::uint64_t sampleId);
    void setPlaybackPosition(double seconds) noexcept;

    bool splitSelectedAtMidpoint();
    bool canSplitSelectedAtMidpoint() const noexcept;
    bool deleteSelectedSlice();
    bool resetSlices();

    void paint(juce::Graphics& graphics) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class DragMode { none, boundary, range };
    struct BoundaryHit
    {
        SliceId sliceId = 0;
        SliceBoundary boundary = SliceBoundary::start;
    };

    juce::Rectangle<float> waveformBounds() const noexcept;
    std::int64_t sampleAtX(float x) const noexcept;
    float xAtSample(std::int64_t sample) const noexcept;
    std::optional<BoundaryHit> hitBoundary(float x) const noexcept;
    std::optional<SliceId> sliceAt(std::int64_t sample) const noexcept;
    void chooseSlice(SliceId id);
    void drawWaveform(juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    SliceEngine& slices;
    std::shared_ptr<const WaveformData> waveform;
    std::optional<SampleMetadata> metadata;
    std::uint64_t displayedSampleId = 0;
    std::vector<double> transientPositions;
    std::uint64_t transientSampleId = 0;
    double playbackSeconds = 0.0;
    std::optional<SliceId> selectedSlice;
    std::optional<BoundaryHit> hoveredBoundary;
    DragMode dragMode = DragMode::none;
    BoundaryHit draggedBoundary;
    std::int64_t dragAnchorSample = 0;
    std::int64_t dragCurrentSample = 0;
    juce::Point<float> mouseDownPosition;
};
