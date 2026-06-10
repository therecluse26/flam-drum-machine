cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Catch2 v3 — pinned for build hermeticism; no system install assumed
# ---------------------------------------------------------------------------
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# Make catch_discover_tests available (lives in Catch2's extras/ directory)
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

# ---------------------------------------------------------------------------
# flam-tests executable — core engine sources only, no VST3/AU wrapper
# ---------------------------------------------------------------------------
juce_add_console_app(flam-tests PRODUCT_NAME "FLAM Tests")

# FLAM_ENGINE_SOURCES is the absolute-path list defined in the root
# CMakeLists.txt; it excludes Source/Plugin/ (the VST3/AU ABI wrapper).
target_sources(flam-tests
    PRIVATE
        smoke_test.cpp
        # determinism_test.cpp    # Uncomment in FLA-53 (CTEST-2) once seedRNG APIs land
        # GoldenRenderTest.cpp    # Uncomment in FLA-56 (CTEST-5) once golden-render APIs land
        ${FLAM_ENGINE_SOURCES})

target_include_directories(flam-tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(flam-tests
    PRIVATE
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
        # Path to Tests/Fixtures/ baked at compile time so GoldenRenderTest can
        # locate fixture assets regardless of working directory at runtime.
        FLAM_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tests/Fixtures")

# juce_audio_processors → juce_gui_extra → juce_gui_basics (transitive chain);
# all modules except juce_audio_plugin_client (the VST3/AU ABI wrapper).
target_link_libraries(flam-tests
    PRIVATE
        Catch2::Catch2WithMain
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
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
        juce::juce_recommended_warning_flags)

# Mirror yaml-cpp support from the main target when present
if(YAML_CPP_FOUND)
    target_include_directories(flam-tests PRIVATE ${YAML_CPP_INCLUDE_DIRS})
    target_compile_definitions(flam-tests PRIVATE FLAM_YAML_SUPPORT=1)
    target_link_libraries(flam-tests PRIVATE ${YAML_CPP_LIBRARIES})
endif()

# Linux: juce_gui_basics pulls in X11 at link time
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(flam-tests PRIVATE JUCE_USE_PIPEWIRE=1)
    find_package(X11 REQUIRED)
    target_link_libraries(flam-tests PRIVATE
        ${X11_LIBRARIES}
        ${X11_Xrandr_LIB}
        ${X11_Xinerama_LIB}
        ${X11_Xext_LIB}
        ${X11_Xcursor_LIB})
endif()

catch_discover_tests(flam-tests)
