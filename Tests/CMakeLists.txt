# CTEST-3 — L1 unit tests: FlamKitLoader, VoiceManager, DSP units
# Uses JUCE's built-in UnitTest framework; separate from the Catch2-based
# flam-tests target so no GUI modules are required (headless / CI-friendly).

cmake_minimum_required(VERSION 3.22)

# Engine sources required by the L1 tests — no GUI, no plugin wrapper, no
# IR convolver (no juce::dsp::Convolution dependency).
set(FLAM_L1_ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/Source/Core/VoiceManager.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/MixerBus.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/SampleVoice.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/SampleStreamingManager.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/Mixer.cpp
    ${CMAKE_SOURCE_DIR}/Source/Formats/FlamKitLoader.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/TenBandGraphicEQ.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/SaturationProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/CompressorProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/LimiterProcessor.cpp)

juce_add_console_app(FlamL1Tests PRODUCT_NAME "FlamL1Tests")

target_sources(FlamL1Tests
    PRIVATE
        L1Main.cpp              # Entry point with juce::initialiseJuce_NonGUI()
        FlamKitLoaderTest.cpp
        VoiceManagerTest.cpp
        DSPTest.cpp
        ${FLAM_L1_ENGINE_SOURCES})

target_include_directories(FlamL1Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(FlamL1Tests PRIVATE
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(YAML_CPP_FOUND)
    target_compile_definitions(FlamL1Tests PRIVATE FLAM_YAML_SUPPORT=1)
    target_include_directories(FlamL1Tests PRIVATE ${YAML_CPP_INCLUDE_DIRS})
    target_link_libraries(FlamL1Tests PRIVATE ${YAML_CPP_LIBRARIES})
endif()

# Non-GUI JUCE modules only — keeps the test build fast and CI-friendly
target_link_libraries(FlamL1Tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_dsp
        juce::juce_data_structures
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags)
# Note: juce_recommended_warning_flags is deliberately omitted here.
# The engine sources contain pre-existing sign-conversion warnings that
# would fail the build under -Werror. Tests use warnings-as-warnings only.

add_test(NAME FlamL1UnitTests COMMAND FlamL1Tests)
set_tests_properties(FlamL1UnitTests PROPERTIES TIMEOUT 60)
