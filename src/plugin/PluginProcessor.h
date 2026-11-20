#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../analysis/AnalysisCoordinator.h"
#include "../analysis/BPMAnalyzer.h"
#include "../import/AudioImportService.h"
#include "../playback/SamplePlaybackEngine.h"
#include <optional>

class CreativeSamplerAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    CreativeSamplerAudioProcessor();
    ~CreativeSamplerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void importFile(const juce::File& file);
    void playSample() noexcept { playbackEngine.play(); }
    void stopSample() noexcept { playbackEngine.stop(); }
    juce::AudioProcessorValueTreeState& parameterState() noexcept { return parameters; }
    double playbackPositionSeconds() const noexcept { return playbackEngine.positionSeconds(); }
    bool isSamplePlaying() const noexcept { return playbackEngine.isPlaying(); }
    bool hasSample() const noexcept { return sampleMetadata.has_value(); }
    const std::optional<SampleMetadata>& currentSampleMetadata() const noexcept { return sampleMetadata; }
    std::shared_ptr<const WaveformData> currentWaveform() const noexcept { return waveform; }
    AnalysisResult currentAnalysisResult() const { return analysisCoordinator.latestResult(); }
    const juce::String& importStatus() const noexcept { return currentImportStatus; }
    float volumeValue() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleImportResult(AudioImportResult result);
    void timerCallback() override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* volumeParameter = nullptr;
    SamplePlaybackEngine playbackEngine;
    SampleHandle currentSample;
    AnalysisCoordinator analysisCoordinator;
    std::shared_ptr<const BPMAnalyzer> bpmAnalyzer;
    AudioImportService importService;
    std::optional<SampleMetadata> sampleMetadata;
    std::shared_ptr<const WaveformData> waveform;
    juce::String currentImportStatus { "Choose a WAV or AIFF sample to begin." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreativeSamplerAudioProcessor)
};
