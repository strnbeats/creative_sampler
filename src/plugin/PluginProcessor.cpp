#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <new>

CreativeSamplerAudioProcessor::CreativeSamplerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()),
      bpmAnalyzer(std::make_shared<BPMAnalyzer>()),
      keyAnalyzer(std::make_shared<KeyAnalyzer>()),
      transientAnalyzer(std::make_shared<TransientAnalyzer>()),
      importService([this](AudioImportResult result) { handleImportResult(std::move(result)); })
{
    volumeParameter = parameters.getRawParameterValue("volume");
    startTimerHz(30);
}

CreativeSamplerAudioProcessor::~CreativeSamplerAudioProcessor()
{
    // Timer::stopTimer removes future ticks but does not join a callback that is
    // already running on the message thread. Hold the manager lock through teardown.
    juce::MessageManagerLock messageManagerLock;
    stopTimer();
    importService.shutdown();
    analysisCoordinator.shutdown();
    playbackEngine.reclaimRetired();
    slicePlaybackEngine.reclaimRetired();
    currentSample.reset();
}

void CreativeSamplerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    playbackEngine.prepare(sampleRate);
    slicePlaybackEngine.prepare(sampleRate);
}

void CreativeSamplerAudioProcessor::releaseResources()
{
    playbackEngine.stop();
    slicePlaybackEngine.stopVoices();
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
    const auto baseWasPlaying = playbackEngine.isPlaying();
    playbackEngine.setVolume(volumeValue());
    slicePlaybackEngine.setVolume(volumeValue());
    playbackEngine.process(buffer);
    slicePlaybackEngine.process(buffer, midiMessages,
                                baseWasPlaying || playbackEngine.isPlaying());
    midiMessages.clear();
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

    SampleHandle sample;
    try
    {
        sample = SampleHandle(std::move(result.sample));
    }
    catch (const std::bad_alloc&)
    {
        currentImportStatus = "Not enough memory to share the imported Sample.";
        return;
    }

    if (!playbackEngine.submitSharedSample(sample))
    {
        currentImportStatus = "Not enough memory to prepare the Sample for playback.";
        return;
    }

    sampleMetadata = sample->metadata();
    waveform = std::move(result.waveform);
    currentSample = sample;
    const auto sampleId = AnalysisCoordinator::createSampleId();
    (void) sliceEngine.setSample(sampleId, sample->metadata().sampleCount);
    slicePlaybackEngine.setCurrentSampleId(sampleId);
    publishedSliceRevision = std::numeric_limits<std::uint64_t>::max();
    publishSliceSnapshotIfNeeded();
    analysisCoordinator.setCurrentSample(sampleId);
    try
    {
        std::vector<std::shared_ptr<const Analyzer>> analyzers;
        analyzers.reserve(3);
        analyzers.push_back(bpmAnalyzer);
        analyzers.push_back(keyAnalyzer);
        analyzers.push_back(transientAnalyzer);
        (void) analysisCoordinator.requestAnalysis(sampleId, std::move(sample), std::move(analyzers));
    }
    catch (const std::bad_alloc&)
    {
        // Playback remains available even if the background analysis request cannot be allocated.
    }
    currentImportStatus = "Ready";
}

void CreativeSamplerAudioProcessor::timerCallback()
{
    playbackEngine.reclaimRetired();
    slicePlaybackEngine.reclaimRetired();
    publishSliceSnapshotIfNeeded();
}

void CreativeSamplerAudioProcessor::publishSliceSnapshotIfNeeded()
{
    if (currentSample == nullptr || sliceEngine.sampleId() == 0
        || publishedSliceRevision == sliceEngine.revision())
        return;

    auto snapshot = SlicePlaybackSnapshot::create(currentSample, sliceEngine.sampleId(), sliceEngine);
    if (snapshot == nullptr)
    {
        currentImportStatus = "Could not prepare the slice map for playback.";
        return;
    }
    if (!slicePlaybackEngine.publishSnapshot(std::move(snapshot)))
    {
        currentImportStatus = "Could not publish the slice map for playback.";
        return;
    }
    publishedSliceRevision = sliceEngine.revision();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CreativeSamplerAudioProcessor();
}
