#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class CreativeSamplerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CreativeSamplerAudioProcessorEditor(CreativeSamplerAudioProcessor& processor);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    CreativeSamplerAudioProcessor& audioProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreativeSamplerAudioProcessorEditor)
};
