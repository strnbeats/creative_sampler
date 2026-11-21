#include "PluginEditor.h"
#include <algorithm>

namespace
{
const auto background = juce::Colour(0xff101116);
const auto surface = juce::Colour(0xff1b1d25);
const auto foreground = juce::Colour(0xfff2f0f7);
const auto muted = juce::Colour(0xffa3a4b2);
const auto accent = juce::Colour(0xff72f2cf);

juce::String makeAnalysisStatusText(const AnalysisResult& result)
{
    if (result.status == AnalysisStatus::running)
        return "Analyzing sample…";
    if (result.status == AnalysisStatus::failed)
        return "Analysis error: " + result.error;
    if (result.status == AnalysisStatus::cancelled)
        return "Analysis cancelled";
    if (result.status == AnalysisStatus::notAnalyzed)
        return "Sample not analyzed";

    juce::StringArray descriptions;
    if (result.bpm)
    {
        const auto& bpm = *result.bpm;
        if (bpm.effectiveValue())
        {
            auto text = "BPM " + juce::String(*bpm.effectiveValue(), 1)
                + "  ·  confidence " + juce::String(juce::roundToInt(bpm.confidence * 100.0f)) + "%";
            if (bpm.octaveAmbiguous)
            {
                const auto octaveAlternative = std::find_if(bpm.alternatives.begin(), bpm.alternatives.end(),
                    [&bpm](const auto& candidate)
                    {
                        const auto ratio = candidate.value / *bpm.value;
                        return (ratio >= 1.90 && ratio <= 2.10) || (ratio >= 0.476 && ratio <= 0.526);
                    });
                if (octaveAlternative != bpm.alternatives.end())
                    text += "  ·  also " + juce::String(octaveAlternative->value, 1);
            }
            if (bpm.detectionStatus == BpmDetectionStatus::lowConfidence)
                text += "  ·  low confidence";
            descriptions.add(text);
        }
        else
        {
            descriptions.add(bpm.detectionStatus == BpmDetectionStatus::insufficientData
                ? "BPM unavailable: sample is too short"
                : "BPM unavailable: no stable rhythm");
        }
    }
    if (result.key)
    {
        const auto& key = *result.key;
        if (!key.tonic || !key.mode)
            descriptions.add("Key unavailable: insufficient tonal material");
        else
        {
            auto text = key.fullName() + "  ·  confidence "
                + juce::String(juce::roundToInt(key.confidence * 100.0f)) + "%";
            if (key.ambiguous)
                text += "  ·  ambiguous";
            else if (key.detectionStatus == KeyDetectionStatus::lowConfidence)
                text += "  ·  low confidence";
            descriptions.add(text);
        }
    }
    if (result.transients)
    {
        const auto& transients = *result.transients;
        if (transients.detectionStatus == TransientDetectionStatus::insufficientData)
            descriptions.add("Transients: none detected");
        else if (transients.detectionStatus == TransientDetectionStatus::truncated)
            descriptions.add("Transients: " + juce::String(static_cast<juce::int64>(transients.totalEventCount))
                             + " (display limited)");
        else
            descriptions.add("Transients: "
                             + juce::String(static_cast<juce::int64>(transients.totalEventCount)));
    }
    return descriptions.joinIntoString("  ·  ");
}
}

CreativeSamplerAudioProcessorEditor::CreativeSamplerAudioProcessorEditor(
    CreativeSamplerAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef), audioProcessor(processorRef),
      waveformEditor(processorRef.sliceEngineForUi())
{
    setSize(900, 560);
    setResizable(true, true);
    setResizeLimits(680, 440, 1600, 1000);

    for (auto* button : { &importButton, &playButton, &stopButton,
                          &splitButton, &deleteSliceButton, &resetSlicesButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff282b34));
        button->setColour(juce::TextButton::textColourOffId, foreground);
        addAndMakeVisible(*button);
    }
    importButton.setColour(juce::TextButton::buttonColourId, accent);
    importButton.setColour(juce::TextButton::textColourOffId, background);
    importButton.onClick = [this] { openFileChooser(); };
    playButton.onClick = [this] { audioProcessor.playSample(); };
    stopButton.onClick = [this] { audioProcessor.stopSample(); };
    splitButton.onClick = [this] { (void) waveformEditor.splitSelectedAtMidpoint(); };
    deleteSliceButton.onClick = [this] { (void) waveformEditor.deleteSelectedSlice(); };
    resetSlicesButton.onClick = [this] { (void) waveformEditor.resetSlices(); };
    addAndMakeVisible(waveformEditor);

    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
    volumeSlider.setRange(0.0, 1.0, 0.001);
    volumeSlider.setValue(audioProcessor.volumeValue(), juce::dontSendNotification);
    volumeSlider.textFromValueFunction = [](double value)
    {
        return juce::String(juce::roundToInt(value * 100.0)) + " %";
    };
    volumeSlider.valueFromTextFunction = [](const juce::String& text)
    {
        return text.getDoubleValue() / 100.0;
    };
    volumeSlider.setColour(juce::Slider::trackColourId, accent);
    volumeSlider.setColour(juce::Slider::thumbColourId, foreground);
    volumeSlider.setColour(juce::Slider::textBoxTextColourId, foreground);
    volumeSlider.setColour(juce::Slider::textBoxBackgroundColourId, surface);
    volumeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(volumeSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameterState(), "volume", volumeSlider);

    for (auto* label : { &sampleNameLabel, &metadataLabel, &statusLabel })
    {
        label->setColour(juce::Label::textColourId, foreground);
        label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    sampleNameLabel.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    metadataLabel.setColour(juce::Label::textColourId, muted);
    metadataLabel.setFont(juce::FontOptions(13.0f));
    statusLabel.setColour(juce::Label::textColourId, muted);
    statusLabel.setFont(juce::FontOptions(13.0f));
    timerCallback();
    startTimerHz(30);
}

CreativeSamplerAudioProcessorEditor::~CreativeSamplerAudioProcessorEditor()
{
    stopTimer();
    fileChooser.reset();
}

bool CreativeSamplerAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1;
}

void CreativeSamplerAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1)
        audioProcessor.importFile(juce::File(files[0]));
}

void CreativeSamplerAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>("Import audio", juce::File {}, "*");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (file.existsAsFile())
                                     audioProcessor.importFile(file);
                             });
}

void CreativeSamplerAudioProcessorEditor::timerCallback()
{
    const auto analysisChanged = refreshAnalysisCache();
    const auto sampleId = audioProcessor.sliceEngineForUi().sampleId();
    waveformEditor.setSource(audioProcessor.currentWaveform(), audioProcessor.currentSampleMetadata(), sampleId);
    if (analysisChanged)
    {
        const auto currentAnalysis = cachedAnalysis.identity.sampleId == sampleId;
        waveformEditor.setTransientAnalysis(currentAnalysis && cachedAnalysis.transients
                                                ? &*cachedAnalysis.transients : nullptr,
                                            currentAnalysis ? sampleId : 0);
    }
    waveformEditor.setPlaybackPosition(audioProcessor.playbackPositionSeconds());
    auto status = audioProcessor.importStatus();
    if (audioProcessor.hasSample())
        status += "  ·  " + makeAnalysisStatusText(cachedAnalysis);
    statusLabel.setText(status, juce::dontSendNotification);
    const auto ready = audioProcessor.hasSample();
    playButton.setEnabled(ready);
    stopButton.setEnabled(audioProcessor.isSamplePlaying());
    const auto hasSlices = !audioProcessor.sliceEngineForUi().slices().empty();
    splitButton.setEnabled(ready && hasSlices && waveformEditor.canSplitSelectedAtMidpoint());
    deleteSliceButton.setEnabled(ready && hasSlices);
    resetSlicesButton.setEnabled(ready);
    if (ready)
    {
        const auto& metadata = *audioProcessor.currentSampleMetadata();
        sampleNameLabel.setText(metadata.fileName, juce::dontSendNotification);
        metadataLabel.setText(juce::String(metadata.durationSeconds(), 2) + " s    "
                                  + juce::String(metadata.sampleRate / 1000.0, 1) + " kHz    "
                                  + juce::String(metadata.channelCount) + " ch",
                              juce::dontSendNotification);
    }
    else
    {
        sampleNameLabel.setText("No sample loaded", juce::dontSendNotification);
        metadataLabel.setText("WAV and AIFF  /  mono or stereo", juce::dontSendNotification);
    }
    repaint();
}

bool CreativeSamplerAudioProcessorEditor::refreshAnalysisCache()
{
    const auto stamp = audioProcessor.currentAnalysisStamp();
    if (!hasCachedAnalysisStamp || !(stamp == cachedAnalysisStamp))
    {
        cachedAnalysis = audioProcessor.currentAnalysisResult();
        cachedAnalysisStamp = stamp;
        hasCachedAnalysisStamp = true;
        return true;
    }
    return false;
}

void CreativeSamplerAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(background);
    const auto card = getLocalBounds().toFloat().reduced(18.0f);
    graphics.setColour(surface);
    graphics.fillRoundedRectangle(card, 16.0f);

    graphics.setColour(foreground);
    graphics.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    graphics.drawText("CREATIVE SAMPLER", 42, 28, 380, 36, juce::Justification::centredLeft);
    graphics.setColour(muted);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.drawText("SAMPLE PLAYER  /  SLICE EDITOR", 42, 66, 360, 22, juce::Justification::centredLeft);
}

void CreativeSamplerAudioProcessorEditor::resized()
{
    importButton.setBounds(getWidth() - 164, 38, 122, 40);
    sampleNameLabel.setBounds(42, 96, getWidth() - 84, 28);
    metadataLabel.setBounds(42, 126, getWidth() - 84, 22);
    statusLabel.setBounds(42, getHeight() - 50, getWidth() - 84, 24);

    waveformEditor.setBounds(42, 174, getWidth() - 84,
                             juce::jmax(120, getHeight() - 300));
    const auto controlY = getHeight() - 100;
    playButton.setBounds(42, controlY, 86, 42);
    stopButton.setBounds(138, controlY, 86, 42);
    splitButton.setBounds(242, controlY, 74, 42);
    deleteSliceButton.setBounds(324, controlY, 82, 42);
    resetSlicesButton.setBounds(414, controlY, 74, 42);
    volumeSlider.setBounds(506, controlY + 4, getWidth() - 548, 34);
}
