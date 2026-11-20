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
        return "BPM analysis…";
    if (result.status == AnalysisStatus::failed)
        return "BPM analysis error: " + result.error;
    if (result.status == AnalysisStatus::cancelled)
        return "BPM analysis cancelled";
    if (result.status == AnalysisStatus::notAnalyzed || !result.bpm)
        return "BPM not analyzed";

    const auto& bpm = *result.bpm;
    switch (bpm.detectionStatus)
    {
        case BpmDetectionStatus::insufficientData:
            return "BPM unavailable: sample is too short";
        case BpmDetectionStatus::noRhythmicContent:
            return "BPM unavailable: no stable rhythm";
        case BpmDetectionStatus::lowConfidence:
        case BpmDetectionStatus::detected:
            break;
    }

    if (!bpm.effectiveValue())
        return "BPM unavailable";
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
    return text;
}
}

CreativeSamplerAudioProcessorEditor::CreativeSamplerAudioProcessorEditor(
    CreativeSamplerAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef), audioProcessor(processorRef)
{
    setSize(900, 560);
    setResizable(true, true);
    setResizeLimits(680, 440, 1600, 1000);

    for (auto* button : { &importButton, &playButton, &stopButton })
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
    auto status = audioProcessor.importStatus();
    if (audioProcessor.hasSample())
        status += "  ·  " + makeAnalysisStatusText(audioProcessor.currentAnalysisResult());
    statusLabel.setText(status, juce::dontSendNotification);
    const auto ready = audioProcessor.hasSample();
    playButton.setEnabled(ready);
    stopButton.setEnabled(audioProcessor.isSamplePlaying());
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

void CreativeSamplerAudioProcessorEditor::drawWaveform(juce::Graphics& graphics,
                                                         juce::Rectangle<float> bounds,
                                                         const WaveformData& data,
                                                         const SampleMetadata& metadata)
{
    if (data.levelCount() == 0 || metadata.sampleCount <= 0)
        return;

    const auto framesPerPixel = static_cast<double>(metadata.sampleCount) / bounds.getWidth();
    const auto levelIndex = data.chooseLevel(framesPerPixel);
    const auto& level = data.level(levelIndex);
    const auto channelCount = juce::jmin(data.channelCount(), 2);
    const auto channelCountF = static_cast<float>(channelCount);
    graphics.setColour(juce::Colour(0xff343743));

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto channelBounds = bounds.withY(bounds.getY() + bounds.getHeight()
                                                 * static_cast<float>(channel) / channelCountF)
                                         .withHeight(bounds.getHeight() / channelCountF);
        const auto centreY = channelBounds.getCentreY();
        graphics.drawHorizontalLine(juce::roundToInt(centreY), channelBounds.getX(), channelBounds.getRight());

        const auto& peaks = level.channels[static_cast<size_t>(channel)];
        const auto pixels = juce::jmax(1, juce::roundToInt(channelBounds.getWidth()));
        graphics.setColour(channel == 0 ? accent : juce::Colour(0xff68b8ff));
        for (int x = 0; x < pixels; ++x)
        {
            const auto frame = static_cast<juce::int64>((static_cast<double>(x) / pixels)
                                                        * static_cast<double>(metadata.sampleCount));
            const auto peakIndex = static_cast<size_t>(juce::jlimit<juce::int64>(
                0, static_cast<juce::int64>(peaks.size() - 1), frame / level.framesPerPeak));
            const auto peak = peaks[peakIndex];
            const auto top = centreY - peak.maximum * channelBounds.getHeight() * 0.46f;
            const auto bottom = centreY - peak.minimum * channelBounds.getHeight() * 0.46f;
            graphics.drawVerticalLine(juce::roundToInt(channelBounds.getX() + static_cast<float>(x)), top, bottom);
        }
    }

    const auto position = audioProcessor.playbackPositionSeconds();
    const auto proportion = juce::jlimit(0.0, 1.0, position / metadata.durationSeconds());
    const auto x = bounds.getX() + static_cast<float>(proportion) * bounds.getWidth();
    graphics.setColour(juce::Colour(0xffff6b91));
    graphics.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
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
    graphics.drawText("SAMPLE PLAYER  /  BPM ANALYSIS", 42, 66, 360, 22, juce::Justification::centredLeft);

    auto waveformBounds = getLocalBounds().toFloat().reduced(42.0f);
    waveformBounds = { waveformBounds.getX(), 174.0f, waveformBounds.getWidth(),
                       juce::jmax(110.0f, static_cast<float>(getHeight()) - 292.0f) };
    graphics.setColour(juce::Colour(0xff13151b));
    graphics.fillRoundedRectangle(waveformBounds, 12.0f);
    graphics.setColour(juce::Colour(0xff383b47));
    graphics.drawRoundedRectangle(waveformBounds, 12.0f, 1.0f);

    const auto waveform = audioProcessor.currentWaveform();
    const auto& sample = audioProcessor.currentSampleMetadata();
    if (waveform != nullptr && sample.has_value())
        drawWaveform(graphics, waveformBounds.reduced(12.0f), *waveform, *sample);
    else
    {
        graphics.setColour(muted);
        graphics.setFont(juce::FontOptions(16.0f));
        graphics.drawText("Import or drop a WAV / AIFF file", waveformBounds.toNearestInt().reduced(18),
                          juce::Justification::centred);
    }
}

void CreativeSamplerAudioProcessorEditor::resized()
{
    importButton.setBounds(getWidth() - 164, 38, 122, 40);
    sampleNameLabel.setBounds(42, 96, getWidth() - 84, 28);
    metadataLabel.setBounds(42, 126, getWidth() - 84, 22);
    statusLabel.setBounds(42, getHeight() - 50, getWidth() - 84, 24);

    const auto controlY = getHeight() - 100;
    playButton.setBounds(42, controlY, 86, 42);
    stopButton.setBounds(138, controlY, 86, 42);
    volumeSlider.setBounds(252, controlY + 4, getWidth() - 294, 34);
}
