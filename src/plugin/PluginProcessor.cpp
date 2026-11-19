#include "PluginProcessor.h"
#include "PluginEditor.h"

CreativeSamplerAudioProcessor::CreativeSamplerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CreativeSamplerAudioProcessor::prepareToPlay(double, int)
{
}

void CreativeSamplerAudioProcessor::releaseResources()
{
}

bool CreativeSamplerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void CreativeSamplerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);
    buffer.clear();
}

juce::AudioProcessorEditor* CreativeSamplerAudioProcessor::createEditor()
{
    return new CreativeSamplerAudioProcessorEditor(*this);
}

void CreativeSamplerAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void CreativeSamplerAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CreativeSamplerAudioProcessor();
}
