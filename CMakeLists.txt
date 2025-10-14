cmake_minimum_required(VERSION 3.22)
project(FLAM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Download JUCE if not present
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE
)
FetchContent_MakeAvailable(JUCE)

# Define the plugin
juce_add_plugin(FLAM
    COMPANY_NAME "FLAM"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Flam
    PLUGIN_CODE Flm1
    FORMATS VST3 AU AAX Standalone
    PRODUCT_NAME "FLAM"
    STANDALONE_PLUGIN_EDITOR_REQUIRES_UI TRUE)

# Add source files
target_sources(FLAM
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Core/FlamEngine.cpp
        Source/Core/VoiceManager.cpp
        Source/Core/MixerBus.cpp
        Source/Core/SampleVoice.cpp
        Source/Core/SampleLoader.cpp
        Source/Formats/FlamKitLoader.cpp
        Source/DSP/IRConvolver.cpp)

# Add include directories
target_include_directories(FLAM
    PUBLIC
        Source)

# Compile definitions
target_compile_definitions(FLAM
    PUBLIC
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

# Link with JUCE modules
target_link_libraries(FLAM
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Platform-specific settings
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(FLAM PUBLIC JUCE_USE_PIPEWIRE=1)

    # Link X11 libraries explicitly for Linux GUI
    find_package(X11 REQUIRED)
    target_link_libraries(FLAM PRIVATE
        ${X11_LIBRARIES}
        ${X11_Xrandr_LIB}
        ${X11_Xinerama_LIB}
        ${X11_Xext_LIB}
        ${X11_Xcursor_LIB})
endif()

# Copy resources
add_custom_command(TARGET FLAM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:FLAM>/Resources)