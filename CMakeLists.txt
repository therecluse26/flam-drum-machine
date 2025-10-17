cmake_minimum_required(VERSION 3.22)
project(FLAM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find yaml-cpp (optional - will use JUCE JSON parser as fallback if not found)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP yaml-cpp)

if(NOT YAML_CPP_FOUND)
    message(WARNING "yaml-cpp not found via pkg-config. FlamKitLoader will only support JSON format.")
    message(WARNING "To enable YAML support on NixOS: run 'nix-shell' from the project root before building.")
    message(WARNING "The shell.nix file already includes yaml-cpp as a dependency.")
endif()

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
    PLUGIN_INPUTS 0
    PLUGIN_OUTPUTS 2
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
        Source/Core/SampleStreamingManager.cpp
        Source/Core/Mixer.cpp
        Source/Formats/FlamKitLoader.cpp
        Source/DSP/IRConvolver.cpp
        Source/DSP/TenBandGraphicEQ.cpp
        Source/DSP/SaturationProcessor.cpp
        Source/DSP/CompressorProcessor.cpp
        Source/DSP/LimiterProcessor.cpp)

# Add include directories
target_include_directories(FLAM
    PUBLIC
        Source)

# Add yaml-cpp include directories if found
if(YAML_CPP_FOUND)
    target_include_directories(FLAM PUBLIC ${YAML_CPP_INCLUDE_DIRS})
    target_compile_definitions(FLAM PRIVATE FLAM_YAML_SUPPORT=1)
endif()

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

# Link yaml-cpp if found
if(YAML_CPP_FOUND)
    target_link_libraries(FLAM PRIVATE ${YAML_CPP_LIBRARIES})
endif()

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