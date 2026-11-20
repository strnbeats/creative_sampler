cmake_minimum_required(VERSION 3.22)

project(CreativeSampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(CreativeSampler)

target_sources(CreativeSampler PRIVATE
    src/analysis/AnalysisCoordinator.cpp
    src/analysis/AnalysisTypes.cpp
    src/analysis/BPMAnalyzer.cpp
    src/dsp/BandlimitedResampler.cpp
    src/sample/SampleStorage.cpp
    src/import/AudioImportService.cpp
    src/playback/SamplePlaybackEngine.cpp
    src/waveform/WaveformData.cpp
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
)

target_include_directories(CreativeSampler PRIVATE src)
target_compile_definitions(CreativeSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(CreativeSampler PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
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
        tests/TestMain.cpp
        tests/AnalysisInfrastructureTests.cpp
        tests/BPMAnalyzerTests.cpp
        tests/SampleEngineTests.cpp
        src/analysis/AnalysisCoordinator.cpp
        src/analysis/AnalysisTypes.cpp
        src/analysis/BPMAnalyzer.cpp
        src/dsp/BandlimitedResampler.cpp
        src/sample/SampleStorage.cpp
        src/import/AudioImportService.cpp
        src/playback/SamplePlaybackEngine.cpp
        src/waveform/WaveformData.cpp
    )
    target_include_directories(CreativeSamplerTests PRIVATE src)
    target_compile_definitions(CreativeSamplerTests PRIVATE JUCE_UNIT_TESTS=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(CreativeSamplerTests PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_events
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    add_test(NAME CreativeSamplerTests COMMAND CreativeSamplerTests)
endif()
