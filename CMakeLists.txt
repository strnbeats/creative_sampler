cmake_minimum_required(VERSION 3.22)

project(CreativeSampler VERSION 0.1.0 LANGUAGES C CXX)

option(CREATIVE_SAMPLER_USE_UI_V2 "Load the read-only React UI-v2 in the plugin editor" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(NPM_EXECUTABLE npm REQUIRED)
execute_process(COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/web-ui"
    RESULT_VARIABLE WEB_UI_BUILD_RESULT)
if (NOT WEB_UI_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Creative Sampler Web UI production build failed")
endif()
execute_process(COMMAND ${NPM_EXECUTABLE} run build:v2
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/web-ui"
    RESULT_VARIABLE WEB_UI_V2_BUILD_RESULT)
if (NOT WEB_UI_V2_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Creative Sampler UI-v2 production build failed")
endif()

# JUCE can be supplied either as an installed CMake package or as a source tree.
if (DEFINED JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_plugin(CreativeSampler
    COMPANY_NAME "Creative Sampler"
    BUNDLE_ID "audio.creativesampler.plugin"
    PLUGIN_MANUFACTURER_CODE CrSm
    PLUGIN_CODE CrS1
    PRODUCT_NAME "Creative Sampler"
    DESCRIPTION "Turn samples into instruments"
    VERSION ${PROJECT_VERSION}
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    NEEDS_WEBVIEW2 TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_add_binary_data(CreativeSamplerWebUI SOURCES web-ui/dist/index.html)
juce_add_binary_data(CreativeSamplerWebUIV2
    NAMESPACE BinaryDataV2
    HEADER_NAME BinaryDataV2.h
    SOURCES web-ui/dist-v2/index.html)

juce_generate_juce_header(CreativeSampler)

target_sources(CreativeSampler PRIVATE
    src/analysis/AnalysisCoordinator.cpp
    src/analysis/AnalysisTypes.cpp
    src/analysis/BPMAnalyzer.cpp
    src/analysis/KeyAnalyzer.cpp
    src/analysis/TransientAnalyzer.cpp
    src/dsp/BandlimitedResampler.cpp
    src/dsp/ADSREnvelope.cpp
    src/sample/SampleStorage.cpp
    src/slice/SliceEngine.cpp
    src/import/AudioImportService.cpp
    src/playback/SamplePlaybackEngine.cpp
    src/playback/SlicePlaybackSnapshot.cpp
    src/playback/SliceVoice.cpp
    src/playback/SlicePlaybackEngine.cpp
    src/waveform/WaveformData.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/ui/SliceParametersPanel.cpp
    src/ui/WaveformEditorComponent.cpp
)

target_include_directories(CreativeSampler PRIVATE src)
target_compile_definitions(CreativeSampler PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
if (CREATIVE_SAMPLER_USE_UI_V2)
    target_compile_definitions(CreativeSampler PRIVATE CREATIVE_SAMPLER_USE_UI_V2=1)
else()
    target_compile_definitions(CreativeSampler PRIVATE CREATIVE_SAMPLER_USE_UI_V2=0)
endif()

target_link_libraries(CreativeSampler PRIVATE
    CreativeSamplerWebUI
    CreativeSamplerWebUIV2
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

include(CTest)
if (BUILD_TESTING)
    juce_add_console_app(CreativeSamplerTests PRODUCT_NAME "Creative Sampler Tests")
    juce_generate_juce_header(CreativeSamplerTests)
    target_sources(CreativeSamplerTests PRIVATE
        src/ui/SliceParametersPanel.cpp
        tests/SliceParametersPanelTests.cpp
        tests/TestMain.cpp
        tests/AnalysisInfrastructureTests.cpp
        tests/BPMAnalyzerTests.cpp
        tests/KeyAnalyzerTests.cpp
        tests/TransientAnalyzerTests.cpp
        tests/SliceEngineTests.cpp
        tests/UiV2SliceParameterCommandTests.cpp
        tests/UiV2TransportCommandTests.cpp
        tests/SlicePlaybackEngineTests.cpp
        tests/ADSREnvelopeTests.cpp
        tests/SampleEngineTests.cpp
        src/analysis/AnalysisCoordinator.cpp
        src/analysis/AnalysisTypes.cpp
        src/analysis/BPMAnalyzer.cpp
        src/analysis/KeyAnalyzer.cpp
        src/analysis/TransientAnalyzer.cpp
        src/dsp/BandlimitedResampler.cpp
        src/dsp/ADSREnvelope.cpp
        src/sample/SampleStorage.cpp
        src/slice/SliceEngine.cpp
        src/import/AudioImportService.cpp
        src/playback/SamplePlaybackEngine.cpp
        src/playback/SlicePlaybackSnapshot.cpp
        src/playback/SliceVoice.cpp
        src/playback/SlicePlaybackEngine.cpp
        src/waveform/WaveformData.cpp
    )
    target_include_directories(CreativeSamplerTests PRIVATE src)
    target_compile_definitions(CreativeSamplerTests PRIVATE JUCE_UNIT_TESTS=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(CreativeSamplerTests PRIVATE
        juce::juce_gui_basics
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
        juce::juce_events
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    add_test(NAME CreativeSamplerTests COMMAND CreativeSamplerTests)
endif()
