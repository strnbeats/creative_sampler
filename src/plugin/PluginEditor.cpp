#include "PluginEditor.h"
#include <algorithm>
#include <BinaryData.h>
#include <cstring>

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
      waveformEditor(processorRef.sliceEngineForUi()),
      parametersPanel(processorRef.sliceEngineForUi(), [this] { return waveformEditor.selectedId(); })
{
    setSize(900, 686);
    setResizable(true, true);
    setResizeLimits(680, 566, 1600, 1000);

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
    addAndMakeVisible(parametersPanel);

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

    auto options = juce::WebBrowserComponent::Options {}
        .withNativeIntegrationEnabled()
        .withNativeFunction("getState", [this](const juce::Array<juce::var>&,
                                                 juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            done(webViewState());
        })
        .withNativeFunction("selectSlice", [this](const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            if (args.size() > 0)
                waveformEditor.selectSlice(static_cast<SliceId>(static_cast<juce::int64>(args[0])));
            done(webViewState());
        })
        .withNativeFunction("editParameter", [this](const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            auto& engine = audioProcessor.sliceEngineForUi();
            if (args.size() >= 3)
            {
                const auto id = static_cast<SliceId>(static_cast<juce::int64>(args[0]));
                const auto name = args[1].toString();
                const auto value = static_cast<float>(args[2]);
                auto field = name == "volume" ? std::optional<SliceField>(SliceField::volume)
                    : name == "pan" ? std::optional<SliceField>(SliceField::pan) : std::nullopt;
                if (field)
                    (void) editSliceField(engine, engine.sampleId(), id, *field, value);
            }
            done(webViewState());
        })
        .withNativeFunction("importSample", [this](const juce::Array<juce::var>&,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            openFileChooser();
            done(webViewState());
        })
        .withNativeFunction("playSample", [this](const juce::Array<juce::var>&,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            audioProcessor.playSample();
            done(webViewState());
        })
        .withNativeFunction("stopSample", [this](const juce::Array<juce::var>&,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            audioProcessor.stopSample();
            done(webViewState());
        })
        .withNativeFunction("splitSlice", [this](const juce::Array<juce::var>&,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            (void) waveformEditor.splitSelectedAtMidpoint();
            done(webViewState());
        })
        .withNativeFunction("resetSlices", [this](const juce::Array<juce::var>&,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            (void) waveformEditor.resetSlices();
            done(webViewState());
        })
        .withNativeFunction("deleteSlice", [this](const juce::Array<juce::var>&,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            (void) waveformEditor.deleteSelectedSlice();
            done(webViewState());
        })
        .withResourceProvider([](const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            juce::ignoreUnused(path);
            int size = 0;
            const auto* data = BinaryData::getNamedResource("index_html", size);
            if (data == nullptr || size <= 0)
                return std::nullopt;
            juce::WebBrowserComponent::Resource resource;
            resource.mimeType = "text/html";
            resource.data.resize(static_cast<size_t>(size));
            std::memcpy(resource.data.data(), data, static_cast<size_t>(size));
            return resource;
        });
#if JUCE_WINDOWS
    options = options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2 {}
            .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("CreativeSamplerWebView")));
#endif
    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);
    webView->toFront(false);
    loadWebUi();

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
    resized();
    webView->toFront(false);
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
    parametersPanel.refresh();
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
    if (webView != nullptr)
    {
        webView->setBounds(getLocalBounds());
        webView->toFront(false);
    }
    importButton.setBounds(getWidth() - 164, 38, 122, 40);
    sampleNameLabel.setBounds(42, 96, getWidth() - 84, 28);
    metadataLabel.setBounds(42, 126, getWidth() - 84, 22);
    statusLabel.setBounds(42, getHeight() - 50, getWidth() - 84, 24);

    waveformEditor.setBounds(42, 174, getWidth() - 84,
                             juce::jmax(120, getHeight() - 426));
    parametersPanel.setBounds(42, waveformEditor.getBottom() + 8, getWidth() - 84, 116);
    const auto controlY = getHeight() - 100;
    playButton.setBounds(42, controlY, 86, 42);
    stopButton.setBounds(138, controlY, 86, 42);
    splitButton.setBounds(242, controlY, 74, 42);
    deleteSliceButton.setBounds(324, controlY, 82, 42);
    resetSlicesButton.setBounds(414, controlY, 74, 42);
    volumeSlider.setBounds(506, controlY + 4, getWidth() - 548, 34);
}

juce::var CreativeSamplerAudioProcessorEditor::webViewState() const
{
    auto state = juce::DynamicObject::Ptr(new juce::DynamicObject());
    state->setProperty("sampleLoaded", audioProcessor.hasSample());
    state->setProperty("status", audioProcessor.importStatus());
    state->setProperty("playing", audioProcessor.isSamplePlaying());
    state->setProperty("file", "DROP A SAMPLE TO BEGIN");
    state->setProperty("duration", 0.0);
    state->setProperty("sampleRate", 0);
    state->setProperty("channels", 0);
    state->setProperty("selectedSliceId", static_cast<juce::int64>(waveformEditor.selectedId()));
    state->setProperty("analysisReady", false);
    state->setProperty("bpm", juce::var());
    state->setProperty("key", juce::var());
    state->setProperty("transientCount", juce::var());
    auto slices = juce::Array<juce::var>();
    for (const auto& slice : audioProcessor.sliceEngineForUi().slices())
    {
        auto item = juce::DynamicObject::Ptr(new juce::DynamicObject());
        item->setProperty("id", static_cast<juce::int64>(slice.id));
        item->setProperty("start", slice.startSample);
        item->setProperty("end", slice.endSample);
        item->setProperty("note", slice.midiNote);
        item->setProperty("volume", slice.parameters.volume);
        item->setProperty("pan", slice.parameters.pan);
        item->setProperty("pitch", slice.parameters.pitchSemitones);
        item->setProperty("attack", slice.parameters.envelope.attackSeconds);
        item->setProperty("decay", slice.parameters.envelope.decaySeconds);
        item->setProperty("sustain", slice.parameters.envelope.sustainLevel);
        item->setProperty("release", slice.parameters.envelope.releaseSeconds);
        slices.add(juce::var(item.get()));
    }
    state->setProperty("slices", slices);
    const auto analysis = audioProcessor.currentAnalysisResult();
    if (analysis.identity.sampleId == audioProcessor.sliceEngineForUi().sampleId()
        && analysis.status == AnalysisStatus::completed)
    {
        state->setProperty("analysisReady", true);
        if (analysis.bpm && analysis.bpm->effectiveValue())
            state->setProperty("bpm", *analysis.bpm->effectiveValue());
        if (analysis.key && analysis.key->isValid())
            state->setProperty("key", analysis.key->fullName());
        if (analysis.transients)
            state->setProperty("transientCount", static_cast<juce::int64>(analysis.transients->totalEventCount));
    }
    auto peaks = juce::Array<juce::var>();
    if (const auto data = audioProcessor.currentWaveform(); data != nullptr && data->levelCount() > 0
        && audioProcessor.currentSampleMetadata())
    {
        const auto& metadata = *audioProcessor.currentSampleMetadata();
        const auto levelIndex = data->chooseLevel(static_cast<double>(metadata.sampleCount) / 256.0);
        const auto& level = data->level(levelIndex);
        if (!level.channels.empty() && !level.channels.front().empty())
        {
            const auto& channel = level.channels.front();
            constexpr size_t displayPeaks = 256;
            for (size_t pixel = 0; pixel < displayPeaks; ++pixel)
            {
                const auto index = std::min(channel.size() - 1, pixel * channel.size() / displayPeaks);
                peaks.add(std::max(std::abs(channel[index].minimum), std::abs(channel[index].maximum)));
            }
        }
    }
    state->setProperty("peaks", peaks);
    if (const auto& metadata = audioProcessor.currentSampleMetadata(); metadata)
    {
        state->setProperty("file", metadata->fileName);
        state->setProperty("duration", metadata->durationSeconds());
        state->setProperty("sampleRate", metadata->sampleRate);
        state->setProperty("channels", metadata->channelCount);
    }
    return juce::var(state.get());
}

void CreativeSamplerAudioProcessorEditor::loadWebUi()
{
    if (webView == nullptr)
        return;
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}
