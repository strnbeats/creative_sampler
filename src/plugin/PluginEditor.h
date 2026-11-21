#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
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

    CreativeSamplerAudioProcessor& audioProcessor;
    juce::TextButton importButton { "IMPORT" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton splitButton { "SPLIT" };
    juce::TextButton deleteSliceButton { "DELETE" };
    juce::TextButton resetSlicesButton { "RESET" };
    WaveformEditorComponent waveformEditor;
    juce::Slider volumeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    juce::Label sampleNameLabel;
    juce::Label metadataLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    AnalysisResult cachedAnalysis;
    AnalysisResultStamp cachedAnalysisStamp;
    bool hasCachedAnalysisStamp = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreativeSamplerAudioProcessorEditor)
};
