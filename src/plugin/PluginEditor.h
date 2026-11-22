#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "../ui/SliceParametersPanel.h"
#include "../ui/WaveformEditorComponent.h"

class CreativeSamplerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                   private juce::FileDragAndDropTarget,
                                                   private juce::Timer
{
public:
    explicit CreativeSamplerAudioProcessorEditor(CreativeSamplerAudioProcessor& processor);
    ~CreativeSamplerAudioProcessorEditor() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    bool refreshAnalysisCache();
    void openFileChooser();
    juce::var webViewState() const;
    juce::var webViewV2Snapshot(const juce::String& requestId);
    juce::var webViewV2Waveform(const juce::var& request) const;
    juce::var webViewV2Transport(const juce::var& request) const;
    void publishUiV2Events();
    void loadWebUi();

    CreativeSamplerAudioProcessor& audioProcessor;
    juce::TextButton importButton { "IMPORT" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton splitButton { "SPLIT" };
    juce::TextButton deleteSliceButton { "DELETE" };
    juce::TextButton resetSlicesButton { "RESET" };
    WaveformEditorComponent waveformEditor;
    SliceParametersPanel parametersPanel;
    juce::Slider volumeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    juce::Label sampleNameLabel;
    juce::Label metadataLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    AnalysisResult cachedAnalysis;
    AnalysisResultStamp cachedAnalysisStamp;
    bool hasCachedAnalysisStamp = false;
    std::uint64_t webUiV2Sequence = 1;
    juce::String lastUiV2Fingerprint;
    juce::String lastUiV2TransportSample;
    bool uiV2Ready = false;
    bool hasUiV2TransportState = false;
    bool lastUiV2TransportPlaying = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreativeSamplerAudioProcessorEditor)
};
