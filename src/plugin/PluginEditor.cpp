#include "PluginEditor.h"

CreativeSamplerAudioProcessorEditor::CreativeSamplerAudioProcessorEditor(
    CreativeSamplerAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef), audioProcessor(processorRef)
{
    juce::ignoreUnused(audioProcessor);
    setSize(900, 560);
}

void CreativeSamplerAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    graphics.fillAll(juce::Colour(0xff101116));

    graphics.setColour(juce::Colour(0xff1b1d25));
    graphics.fillRoundedRectangle(bounds.reduced(18.0f), 16.0f);

    graphics.setColour(juce::Colour(0xfff2f0f7));
    graphics.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    graphics.drawText("CREATIVE SAMPLER", 42, 34, getWidth() - 84, 36,
                      juce::Justification::centredLeft);

    graphics.setColour(juce::Colour(0xffa3a4b2));
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawText("Phase 0  /  Foundation", 42, 72, getWidth() - 84, 24,
                      juce::Justification::centredLeft);

    auto panel = getLocalBounds().reduced(42, 122).toFloat();
    graphics.setColour(juce::Colour(0xff13151b));
    graphics.fillRoundedRectangle(panel, 12.0f);
    graphics.setColour(juce::Colour(0xff383b47));
    graphics.drawRoundedRectangle(panel, 12.0f, 1.0f);

    graphics.setColour(juce::Colour(0xffb7b8c5));
    graphics.setFont(juce::FontOptions(16.0f));
    graphics.drawText("Drop an audio file here to begin", panel.toNearestInt().reduced(20),
                      juce::Justification::centred);
}

void CreativeSamplerAudioProcessorEditor::resized()
{
}
