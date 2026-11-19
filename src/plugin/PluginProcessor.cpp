#include "PluginProcessor.h"
#include "PluginEditor.h"

CreativeSamplerAudioProcessor::CreativeSamplerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()),
      importService([this](AudioImportResult result) { handleImportResult(std::move(result)); })
{
    volumeParameter = parameters.getRawParameterValue("volume");
    startTimerHz(30);
}

CreativeSamplerAudioProcessor::~CreativeSamplerAudioProcessor()
{
    stopTimer();
    importService.shutdown();
    playbackEngine.reclaimRetired();
}

void CreativeSamplerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    playbackEngine.prepare(sampleRate);
}

void CreativeSamplerAudioProcessor::releaseResources()
{
    playbackEngine.stop();
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
    playbackEngine.setVolume(volumeValue());
    playbackEngine.process(buffer);
}

juce::AudioProcessorEditor* CreativeSamplerAudioProcessor::createEditor()
{
    return new CreativeSamplerAudioProcessorEditor(*this);
}

void CreativeSamplerAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    if (const auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary(*xml, destinationData);
}

void CreativeSamplerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout
CreativeSamplerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "volume", 1 }, "Volume",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.8f));
    return layout;
}

void CreativeSamplerAudioProcessor::importFile(const juce::File& file)
{
    currentImportStatus = "Loading " + file.getFileName() + "…";
    importService.loadAsync(file);
}

float CreativeSamplerAudioProcessor::volumeValue() const noexcept
{
    return volumeParameter != nullptr ? volumeParameter->load(std::memory_order_relaxed) : 0.8f;
}

void CreativeSamplerAudioProcessor::handleImportResult(AudioImportResult result)
{
    if (!result.succeeded())
    {
        currentImportStatus = result.error.isEmpty() ? "Audio import failed." : result.error;
        return;
    }

    sampleMetadata = result.sample->metadata();
    waveform = std::move(result.waveform);
    playbackEngine.submitSample(std::move(result.sample));
    currentImportStatus = "Ready";
}

void CreativeSamplerAudioProcessor::timerCallback()
{
    playbackEngine.reclaimRetired();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CreativeSamplerAudioProcessor();
}
