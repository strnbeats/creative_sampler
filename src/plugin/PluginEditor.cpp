#include "PluginEditor.h"
#include <algorithm>
#include <BinaryData.h>
#include <BinaryDataV2.h>
#include <cmath>
#include <cstring>
#include <string>

namespace
{
const auto background = juce::Colour(0xff101116);
const auto surface = juce::Colour(0xff1b1d25);
const auto foreground = juce::Colour(0xfff2f0f7);
const auto muted = juce::Colour(0xffa3a4b2);
const auto accent = juce::Colour(0xff72f2cf);

juce::DynamicObject::Ptr makeObject()
{
    return juce::DynamicObject::Ptr(new juce::DynamicObject());
}

juce::var makeV2Ack(const juce::String& requestId, juce::var result)
{
    auto ack = makeObject();
    ack->setProperty("protocolVersion", 2);
    ack->setProperty("type", "ack");
    ack->setProperty("requestId", requestId);
    ack->setProperty("ok", true);
    ack->setProperty("result", std::move(result));
    return juce::var(ack.get());
}

juce::var makeV2Error(const juce::String& requestId, const juce::String& code,
                      const juce::String& message, bool retryable = false)
{
    auto error = makeObject();
    error->setProperty("code", code);
    error->setProperty("message", message);
    error->setProperty("retryable", retryable);
    auto ack = makeObject();
    ack->setProperty("protocolVersion", 2);
    ack->setProperty("type", "ack");
    ack->setProperty("requestId", requestId);
    ack->setProperty("ok", false);
    ack->setProperty("error", juce::var(error.get()));
    return juce::var(ack.get());
}

juce::String uint64String(std::uint64_t value)
{
    return juce::String(std::to_string(value));
}

bool getV2Request(const juce::var& value, juce::String& requestId, juce::String& errorCode,
                  juce::String& errorMessage)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
    {
        errorCode = "INVALID_ARGUMENT";
        errorMessage = "Expected a protocol v2 request object.";
        return false;
    }
    requestId = object->getProperty("requestId").toString();
    const auto protocol = object->getProperty("protocolVersion");
    if ((!protocol.isInt() && !protocol.isInt64() && !protocol.isDouble())
        || protocol.toString().getDoubleValue() != 2.0)
    {
        errorCode = "UNSUPPORTED_PROTOCOL";
        errorMessage = "This UI supports bridge protocol version 2.";
        return false;
    }
    if (requestId.isEmpty() || requestId.length() > 128)
    {
        errorCode = "INVALID_ARGUMENT";
        errorMessage = "A non-empty requestId is required.";
        return false;
    }
    return true;
}

juce::String analysisStatusName(AnalysisStatus status)
{
    switch (status)
    {
        case AnalysisStatus::notAnalyzed: return "notAnalyzed";
        case AnalysisStatus::running: return "running";
        case AnalysisStatus::completed: return "completed";
        case AnalysisStatus::failed: return "failed";
        case AnalysisStatus::cancelled: return "cancelled";
    }
    return "notAnalyzed";
}

juce::String pitchClassName(PitchClass pitchClass)
{
    switch (pitchClass)
    {
        case PitchClass::c: return "C";
        case PitchClass::cSharp: return "C#";
        case PitchClass::d: return "D";
        case PitchClass::dSharp: return "D#";
        case PitchClass::e: return "E";
        case PitchClass::f: return "F";
        case PitchClass::fSharp: return "F#";
        case PitchClass::g: return "G";
        case PitchClass::gSharp: return "G#";
        case PitchClass::a: return "A";
        case PitchClass::aSharp: return "A#";
        case PitchClass::b: return "B";
    }
    return {};
}

juce::String bpmStatusName(BpmDetectionStatus status)
{
    switch (status)
    {
        case BpmDetectionStatus::detected: return "detected";
        case BpmDetectionStatus::lowConfidence: return "lowConfidence";
        case BpmDetectionStatus::insufficientData: return "insufficientData";
        case BpmDetectionStatus::noRhythmicContent: return "noRhythmicContent";
    }
    return "insufficientData";
}

juce::String keyStatusName(KeyDetectionStatus status)
{
    switch (status)
    {
        case KeyDetectionStatus::detected: return "detected";
        case KeyDetectionStatus::lowConfidence: return "lowConfidence";
        case KeyDetectionStatus::insufficientData: return "insufficientData";
    }
    return "insufficientData";
}

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
        .withNativeFunction("getUiV2Snapshot", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::String requestId, errorCode, errorMessage;
            if (args.isEmpty() || !getV2Request(args[0], requestId, errorCode, errorMessage))
                done(makeV2Error(requestId, errorCode.isEmpty() ? "INVALID_ARGUMENT" : errorCode,
                                 errorMessage.isEmpty() ? "Missing protocol request." : errorMessage));
            else
            {
                uiV2Ready = true;
                done(webViewV2Snapshot(requestId));
            }
        })
        .withNativeFunction("getUiV2Waveform", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            if (args.isEmpty())
            {
                done(makeV2Error({}, "INVALID_ARGUMENT", "Missing waveform request."));
                return;
            }
            done(webViewV2Waveform(args[0]));
        })
        .withNativeFunction("getUiV2Transport", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            if (args.isEmpty())
            {
                done(makeV2Error({}, "INVALID_ARGUMENT", "Missing transport request."));
                return;
            }
            done(webViewV2Transport(args[0]));
        })
        .withNativeFunction("importSampleV2", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::String requestId, errorCode, errorMessage;
            if (args.isEmpty() || !getV2Request(args[0], requestId, errorCode, errorMessage))
            {
                done(makeV2Error(requestId, errorCode.isEmpty() ? "INVALID_ARGUMENT" : errorCode,
                                 errorMessage.isEmpty() ? "Missing import request." : errorMessage));
                return;
            }
            openFileChooser();
            auto result = makeObject();
            result->setProperty("accepted", true);
            done(makeV2Ack(requestId, juce::var(result.get())));
        })
        .withResourceProvider([](const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            int size = 0;
            const auto* data = path.containsIgnoreCase("legacy")
                ? BinaryData::getNamedResource("index_html", size)
                : BinaryDataV2::getNamedResource("index_html", size);
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
    publishUiV2Events();

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

juce::var CreativeSamplerAudioProcessorEditor::webViewV2Snapshot(const juce::String& requestId)
{
    auto snapshot = makeObject();
    snapshot->setProperty("protocolVersion", 2);
    snapshot->setProperty("type", "snapshot");
    snapshot->setProperty("sequence", uint64String(webUiV2Sequence++));

    const auto importStatus = audioProcessor.importStatus();
    const auto isLoading = importStatus.startsWith("Loading ");
    const auto isError = importStatus.containsIgnoreCase("failed")
        || importStatus.containsIgnoreCase("could not")
        || importStatus.containsIgnoreCase("not enough memory");
    auto app = makeObject();
    app->setProperty("status", isLoading ? "loading" : isError ? "error" : "ready");
    app->setProperty("message", isLoading || isError || !audioProcessor.hasSample()
        ? juce::var(importStatus) : juce::var());
    snapshot->setProperty("app", juce::var(app.get()));

    const auto sampleId = audioProcessor.sliceEngineForUi().sampleId();
    const auto* loadedMetadata = (!isLoading && audioProcessor.currentSampleMetadata())
        ? &*audioProcessor.currentSampleMetadata() : nullptr;
    if (loadedMetadata != nullptr && sampleId != 0)
    {
        auto sample = makeObject();
        sample->setProperty("id", uint64String(sampleId));
        sample->setProperty("revision", "1");
        sample->setProperty("fileName", loadedMetadata->fileName);
        sample->setProperty("durationSeconds", loadedMetadata->durationSeconds());
        sample->setProperty("sampleRate", loadedMetadata->sampleRate);
        sample->setProperty("sampleCount", loadedMetadata->sampleCount);
        sample->setProperty("channelCount", loadedMetadata->channelCount);
        sample->setProperty("bitDepth", loadedMetadata->bitDepth > 0
            ? juce::var(loadedMetadata->bitDepth) : juce::var());
        snapshot->setProperty("sample", juce::var(sample.get()));

        auto slices = juce::Array<juce::var>();
        for (const auto& slice : audioProcessor.sliceEngineForUi().slices())
        {
            auto item = makeObject();
            item->setProperty("id", uint64String(slice.id));
            item->setProperty("startSample", slice.startSample);
            item->setProperty("endSample", slice.endSample);
            item->setProperty("midiNote", slice.midiNote);
            auto parameters = makeObject();
            parameters->setProperty("volume", slice.parameters.volume);
            parameters->setProperty("pan", slice.parameters.pan);
            parameters->setProperty("pitchSemitones", slice.parameters.pitchSemitones);
            auto envelope = makeObject();
            envelope->setProperty("attackSeconds", slice.parameters.envelope.attackSeconds);
            envelope->setProperty("decaySeconds", slice.parameters.envelope.decaySeconds);
            envelope->setProperty("sustainLevel", slice.parameters.envelope.sustainLevel);
            envelope->setProperty("releaseSeconds", slice.parameters.envelope.releaseSeconds);
            parameters->setProperty("envelope", juce::var(envelope.get()));
            item->setProperty("parameters", juce::var(parameters.get()));
            slices.add(juce::var(item.get()));
        }
        snapshot->setProperty("slices", slices);
        snapshot->setProperty("sliceRevision", uint64String(audioProcessor.sliceEngineForUi().revision()));

        const auto result = audioProcessor.currentAnalysisResult();
        if (result.identity.sampleId == sampleId)
        {
            auto analysis = makeObject();
            analysis->setProperty("sampleId", uint64String(result.identity.sampleId));
            analysis->setProperty("generation", uint64String(result.identity.generation));
            analysis->setProperty("status", analysisStatusName(result.status));
            analysis->setProperty("error", result.error.isNotEmpty() ? juce::var(result.error) : juce::var());

            if (result.bpm)
            {
                auto bpm = makeObject();
                bpm->setProperty("value", result.bpm->effectiveValue()
                    ? juce::var(*result.bpm->effectiveValue()) : juce::var());
                bpm->setProperty("confidence", result.bpm->confidence);
                bpm->setProperty("score", result.bpm->score);
                bpm->setProperty("detectionStatus", bpmStatusName(result.bpm->detectionStatus));
                analysis->setProperty("bpm", juce::var(bpm.get()));
            }
            else analysis->setProperty("bpm", juce::var());

            if (result.key)
            {
                auto key = makeObject();
                key->setProperty("tonic", result.key->tonic
                    ? juce::var(pitchClassName(*result.key->tonic)) : juce::var());
                key->setProperty("mode", result.key->mode
                    ? juce::var(*result.key->mode == ScaleMode::minor ? "minor" : "major") : juce::var());
                key->setProperty("confidence", result.key->confidence);
                key->setProperty("score", result.key->score);
                key->setProperty("detectionStatus", keyStatusName(result.key->detectionStatus));
                analysis->setProperty("key", juce::var(key.get()));
            }
            else analysis->setProperty("key", juce::var());

            if (result.transients)
            {
                constexpr size_t maximumTransientMarkers = 2048;
                const auto& events = result.transients->positions;
                const auto markerCount = std::min(events.size(), maximumTransientMarkers);
                auto positions = juce::Array<juce::var>();
                for (size_t i = 0; i < markerCount; ++i)
                {
                    const auto eventIndex = markerCount == events.size()
                        ? i : (i * events.size()) / markerCount;
                    const auto& event = events[eventIndex];
                    auto point = makeObject();
                    point->setProperty("samplePosition", event.samplePosition);
                    point->setProperty("strength", event.strength);
                    point->setProperty("confidence", event.confidence
                        ? juce::var(*event.confidence) : juce::var());
                    positions.add(juce::var(point.get()));
                }
                auto transients = makeObject();
                transients->setProperty("sampleRate", result.transients->sampleRate);
                transients->setProperty("sampleCount", result.transients->sampleCount);
                transients->setProperty("totalEventCount", static_cast<juce::int64>(result.transients->totalEventCount));
                transients->setProperty("truncated", result.transients->detectionStatus == TransientDetectionStatus::truncated
                    || events.size() < result.transients->totalEventCount || events.size() > maximumTransientMarkers);
                transients->setProperty("positions", positions);
                analysis->setProperty("transients", juce::var(transients.get()));
            }
            else analysis->setProperty("transients", juce::var());
            snapshot->setProperty("analysis", juce::var(analysis.get()));
        }
        else snapshot->setProperty("analysis", juce::var());

        auto transport = makeObject();
        transport->setProperty("status", audioProcessor.isBaseSamplePlaying() ? "playing" : "stopped");
        const auto position = audioProcessor.playbackPositionSeconds() * loadedMetadata->sampleRate;
        transport->setProperty("positionSamples", juce::jlimit(0.0,
            static_cast<double>(loadedMetadata->sampleCount), position));
        snapshot->setProperty("sampleTransport", juce::var(transport.get()));
    }
    else
    {
        snapshot->setProperty("sample", juce::var());
        snapshot->setProperty("slices", juce::Array<juce::var>());
        snapshot->setProperty("sliceRevision", juce::var());
        snapshot->setProperty("analysis", juce::var());
        snapshot->setProperty("sampleTransport", juce::var());
    }
    snapshot->setProperty("activeVoices", juce::var());

    return makeV2Ack(requestId, juce::var(snapshot.get()));
}

juce::var CreativeSamplerAudioProcessorEditor::webViewV2Waveform(const juce::var& request) const
{
    juce::String requestId, errorCode, errorMessage;
    if (!getV2Request(request, requestId, errorCode, errorMessage))
        return makeV2Error(requestId, errorCode, errorMessage);
    const auto* object = request.getDynamicObject();
    const auto sampleId = object->getProperty("sampleId").toString();
    const auto expectedSampleId = uint64String(audioProcessor.sliceEngineForUi().sampleId());
    if (!audioProcessor.hasSample() || sampleId.isEmpty())
        return makeV2Error(requestId, "NO_SAMPLE", "No sample is currently loaded.");
    if (sampleId != expectedSampleId)
        return makeV2Error(requestId, "STALE_SAMPLE", "The sample changed before waveform data was requested.", true);

    const auto rawBuckets = object->getProperty("maxBuckets");
    if ((!rawBuckets.isInt() && !rawBuckets.isInt64() && !rawBuckets.isDouble())
        || !std::isfinite(rawBuckets.toString().getDoubleValue()))
        return makeV2Error(requestId, "INVALID_ARGUMENT", "maxBuckets must be a finite number.");
    const auto maximumBuckets = static_cast<int>(juce::jlimit(64.0, 2048.0, rawBuckets.toString().getDoubleValue()));
    const auto data = audioProcessor.currentWaveform();
    const auto metadata = audioProcessor.currentSampleMetadata();
    if (data == nullptr || metadata == std::nullopt || data->levelCount() <= 0)
        return makeV2Error(requestId, "INTERNAL", "Waveform data is not available yet.", true);

    const auto framesPerPixel = static_cast<double>(metadata->sampleCount)
        / static_cast<double>(maximumBuckets);
    const auto levelIndex = data->chooseLevel(framesPerPixel);
    const auto& level = data->level(levelIndex);
    auto channels = juce::Array<juce::var>();
    for (const auto& channel : level.channels)
    {
        auto peaks = juce::Array<juce::var>();
        const auto bucketCount = std::min(static_cast<size_t>(maximumBuckets), channel.size());
        for (size_t bucket = 0; bucket < bucketCount; ++bucket)
        {
            const auto begin = bucket * channel.size() / bucketCount;
            const auto end = std::max(begin + 1, (bucket + 1) * channel.size() / bucketCount);
            auto minimum = channel[begin].minimum;
            auto maximum = channel[begin].maximum;
            for (auto index = begin + 1; index < end; ++index)
            {
                minimum = std::min(minimum, channel[index].minimum);
                maximum = std::max(maximum, channel[index].maximum);
            }
            auto peak = makeObject();
            peak->setProperty("min", minimum);
            peak->setProperty("max", maximum);
            peaks.add(juce::var(peak.get()));
        }
        channels.add(peaks);
    }

    auto waveform = makeObject();
    waveform->setProperty("protocolVersion", 2);
    waveform->setProperty("type", "waveform");
    waveform->setProperty("requestId", requestId);
    waveform->setProperty("sampleId", expectedSampleId);
    waveform->setProperty("framesPerPeak", level.framesPerPeak);
    waveform->setProperty("channels", channels);
    return makeV2Ack(requestId, juce::var(waveform.get()));
}

juce::var CreativeSamplerAudioProcessorEditor::webViewV2Transport(const juce::var& request) const
{
    juce::String requestId, errorCode, errorMessage;
    if (!getV2Request(request, requestId, errorCode, errorMessage))
        return makeV2Error(requestId, errorCode, errorMessage);
    auto transport = makeObject();
    transport->setProperty("protocolVersion", 2);
    transport->setProperty("type", "transport");
    transport->setProperty("requestId", requestId);
    const auto& metadata = audioProcessor.currentSampleMetadata();
    const auto sampleId = audioProcessor.sliceEngineForUi().sampleId();
    transport->setProperty("sampleId", metadata && sampleId != 0
        ? juce::var(uint64String(sampleId)) : juce::var());
    transport->setProperty("status", metadata && audioProcessor.isBaseSamplePlaying() ? "playing" : "stopped");
    const auto position = metadata ? audioProcessor.playbackPositionSeconds() * metadata->sampleRate : 0.0;
    transport->setProperty("positionSamples", metadata
        ? juce::jlimit(0.0, static_cast<double>(metadata->sampleCount), position) : 0.0);
    return makeV2Ack(requestId, juce::var(transport.get()));
}

void CreativeSamplerAudioProcessorEditor::publishUiV2Events()
{
    if (!uiV2Ready || webView == nullptr)
        return;

    const auto sampleId = audioProcessor.sliceEngineForUi().sampleId();
    const auto sliceRevision = audioProcessor.sliceEngineForUi().revision();
    const auto analysisStamp = audioProcessor.currentAnalysisStamp();
    const auto fingerprint = uint64String(sampleId) + ":" + uint64String(sliceRevision) + ":"
        + audioProcessor.importStatus() + ":" + uint64String(analysisStamp.identity.sampleId) + ":"
        + uint64String(analysisStamp.identity.generation) + ":" + analysisStatusName(analysisStamp.status);

    if (fingerprint != lastUiV2Fingerprint)
    {
        lastUiV2Fingerprint = fingerprint;
        const auto response = webViewV2Snapshot("event");
        if (const auto* responseObject = response.getDynamicObject())
            webView->emitEventIfBrowserIsVisible("samplerStateSnapshot", responseObject->getProperty("result"));
    }

    const auto& metadata = audioProcessor.currentSampleMetadata();
    const auto currentSample = metadata && sampleId != 0 ? uint64String(sampleId) : juce::String();
    const auto playing = metadata && audioProcessor.isBaseSamplePlaying();
    if (playing || !hasUiV2TransportState || currentSample != lastUiV2TransportSample
        || playing != lastUiV2TransportPlaying)
    {
        hasUiV2TransportState = true;
        lastUiV2TransportSample = currentSample;
        lastUiV2TransportPlaying = playing;
        auto transport = makeObject();
        transport->setProperty("protocolVersion", 2);
        transport->setProperty("type", "transportUpdate");
        transport->setProperty("sequence", uint64String(webUiV2Sequence++));
        transport->setProperty("sampleId", currentSample.isNotEmpty() ? juce::var(currentSample) : juce::var());
        transport->setProperty("status", playing ? "playing" : "stopped");
        const auto position = metadata ? audioProcessor.playbackPositionSeconds() * metadata->sampleRate : 0.0;
        transport->setProperty("positionSamples", metadata
            ? juce::jlimit(0.0, static_cast<double>(metadata->sampleCount), position) : 0.0);
        webView->emitEventIfBrowserIsVisible("samplerTransportUpdate", juce::var(transport.get()));
    }
}

void CreativeSamplerAudioProcessorEditor::loadWebUi()
{
    if (webView == nullptr)
        return;
#if CREATIVE_SAMPLER_USE_UI_V2
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
#else
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "legacy");
#endif
}
