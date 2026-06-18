cmake_minimum_required(VERSION 3.22)
project(FLAM VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

# ------------------------------------------------------------
# L3/L4 testing options  (CTEST-6 / FLA-57)
# ------------------------------------------------------------
# FLAM_PLUGINVAL — download pluginval (Tracktion, GPLv3) and run it against
#   the built VST3 as an external subprocess at strictness 8.  pluginval is
#   NEVER linked into FLAM — process isolation is the DR3 licence firewall.
#
# FLAM_SANITIZE — compile L1/L2 test targets with ASan+UBSan (Linux/macOS).
#   Use the scripts/run-sanitized-tests.sh one-command wrapper, or configure
#   a separate build directory with -DFLAM_SANITIZE=ON.
option(FLAM_PLUGINVAL "Enable pluginval L3 host-contract tests (downloads binary)" OFF)
option(FLAM_SANITIZE  "Enable ASan+UBSan for L1/L2 test targets (L4, Linux/macOS only)" OFF)

# Find yaml-cpp — try system pkg first (NixOS/Linux), fall back to FetchContent
# for hermetic CI builds on Windows and macOS where libyaml-cpp-dev is not installed.
include(FetchContent)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(YAML_CPP yaml-cpp)
endif()

if(NOT YAML_CPP_FOUND)
    message(STATUS "yaml-cpp not found via pkg-config — fetching via FetchContent")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        c9371de7836d113c0b14bfa15ca70f00ebb3ac6f  # post-0.8.0: bumps cmake_minimum_required to 3.5 (required for CMake 3.28+)
    )
    set(YAML_CPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB  OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    set(YAML_CPP_FOUND         TRUE)
    set(YAML_CPP_INCLUDE_DIRS  "")  # headers exposed via yaml-cpp::yaml-cpp CMake target
    set(YAML_CPP_LIBRARIES     "")  # not needed — link via yaml-cpp::yaml-cpp below
    set(YAML_CPP_FETCHED       TRUE)
endif()

# Download JUCE if not present
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(JUCE)

# Define the plugin
juce_add_plugin(FLAM
    COMPANY_NAME "FLAM"
    IS_SYNTH TRUE
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
        Source/Plugin/StandaloneApp.cpp
        Source/Core/FlamEngine.cpp
        Source/Core/VoiceManager.cpp
        Source/Core/SampleVoice.cpp
        Source/Core/SampleStreamingManager.cpp
        Source/Core/Mixer.cpp
        Source/Formats/FlamKitLoader.cpp
        Source/Repository/RepositoryManager.cpp
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
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0
        # Use FLAM's custom standalone app (Source/Plugin/StandaloneApp.cpp) so the standalone
        # launches with a low default audio buffer (~128 frames) for imperceptible latency.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

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
        juce::juce_cryptography
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

# Link yaml-cpp if found (system pkg → ${YAML_CPP_LIBRARIES}; FetchContent → CMake target)
if(YAML_CPP_FOUND)
    if(DEFINED YAML_CPP_FETCHED)
        target_link_libraries(FLAM PRIVATE yaml-cpp::yaml-cpp)
    else()
        target_link_libraries(FLAM PRIVATE ${YAML_CPP_LIBRARIES})
    endif()
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

# Optional libcurl for JUCE URL support on Linux
find_package(CURL QUIET)
if(CURL_FOUND)
    target_compile_definitions(FLAM PRIVATE JUCE_USE_CURL=1)
    target_link_libraries(FLAM PRIVATE CURL::libcurl)
else()
    target_compile_definitions(FLAM PRIVATE JUCE_USE_CURL=0)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "libcurl not found — remote repository browsing will not work on Linux. Install libcurl-dev and reconfigure.")
    endif()
endif()

# Copy resources
add_custom_command(TARGET FLAM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:FLAM>/Resources)

# ============================================================
# flam-render — headless offline renderer (CLI tool / CTEST-4)
#
# Drives FlamEngine directly without a plugin wrapper.
# Usage: flam-render --kit kit.yaml --seq seq.json --sr 48000 --block 64 --seed 1 -o out.wav
# ============================================================

# Engine sources shared by the plugin, renderer, and test targets.
# Absolute paths so this list is portable across subdirectories.
set(FLAM_ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/Source/Core/FlamEngine.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/VoiceManager.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/SampleVoice.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/SampleStreamingManager.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/Mixer.cpp
    ${CMAKE_SOURCE_DIR}/Source/Formats/FlamKitLoader.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/IRConvolver.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/TenBandGraphicEQ.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/SaturationProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/CompressorProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/DSP/LimiterProcessor.cpp)

juce_add_console_app(flam-render
    PRODUCT_NAME "FLAM Render")

target_sources(flam-render
    PRIVATE
        Source/CLI/FlamRender.cpp
        ${FLAM_ENGINE_SOURCES})

target_include_directories(flam-render
    PRIVATE
        Source)

if(YAML_CPP_FOUND)
    target_include_directories(flam-render PRIVATE ${YAML_CPP_INCLUDE_DIRS})
    target_compile_definitions(flam-render PRIVATE FLAM_YAML_SUPPORT=1)
endif()

target_compile_definitions(flam-render
    PRIVATE
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

# juce_audio_processors transitively requires juce_gui_basics / juce_gui_extra;
# list them explicitly so platform-specific linker flags apply correctly.
target_link_libraries(flam-render
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
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

if(YAML_CPP_FOUND)
    if(DEFINED YAML_CPP_FETCHED)
        target_link_libraries(flam-render PRIVATE yaml-cpp::yaml-cpp)
    else()
        target_link_libraries(flam-render PRIVATE ${YAML_CPP_LIBRARIES})
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(flam-render PRIVATE
        ${X11_LIBRARIES}
        ${X11_Xrandr_LIB}
        ${X11_Xinerama_LIB}
        ${X11_Xext_LIB}
        ${X11_Xcursor_LIB})
endif()

# ============================================================
# FlamForge — standalone kit-recording companion app (FLA-119)
#
# A separate gui_app executable (NOT a plugin): it owns the audio device,
# guides the user through "fingerprint" velocity-layer capture, and exports a
# complete flamkit.yaml. This is the launchable skeleton; capture/synthesis/
# export stages (FLA-120/121/122/124) wire into it as they land. It will share
# engine/format code with FLAM via FlamCore (FLA-116) once extracted.
# ============================================================
juce_add_gui_app(FlamForge
    PRODUCT_NAME "FlamForge"
    COMPANY_NAME "FLAM")

target_sources(FlamForge
    PRIVATE
        Source/FlamForge/Main.cpp
        Source/FlamForge/CaptureEngine.cpp
        Source/FlamForge/LayerSynth.cpp
        Source/FlamForge/KitExporter.cpp
        Source/FlamForge/OfflineTransientDetector.cpp
        Source/FlamForge/SegmentExtractor.cpp
        Source/FlamForge/WaveformEditor.cpp
        Source/Formats/FlamKitLoader.cpp)

target_include_directories(FlamForge
    PRIVATE
        Source)

# yaml-cpp is required by FlamKitLoader (saveKit/loadKit round-trip on export).
if(YAML_CPP_FOUND)
    target_include_directories(FlamForge PRIVATE ${YAML_CPP_INCLUDE_DIRS})
    target_compile_definitions(FlamForge PRIVATE FLAM_YAML_SUPPORT=1)
endif()

target_compile_definitions(FlamForge
    PRIVATE
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(FlamForge
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Link yaml-cpp if found (system pkg → ${YAML_CPP_LIBRARIES}; FetchContent → CMake target)
if(YAML_CPP_FOUND)
    if(DEFINED YAML_CPP_FETCHED)
        target_link_libraries(FlamForge PRIVATE yaml-cpp::yaml-cpp)
    else()
        target_link_libraries(FlamForge PRIVATE ${YAML_CPP_LIBRARIES})
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(FlamForge PRIVATE
        ${X11_LIBRARIES}
        ${X11_Xrandr_LIB}
        ${X11_Xinerama_LIB}
        ${X11_Xext_LIB}
        ${X11_Xcursor_LIB})
endif()

# FlamForge headless self-test — data pipeline + per-channel metering.
# JUCE's X11 init runs before initialise(), so Linux needs a virtual display
# even though the test calls quit() without showing any window.
if(BUILD_TESTING)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME   FlamForge_SelfTest
            COMMAND xvfb-run -a $<TARGET_FILE:FlamForge> --selftest)
    else()
        add_test(
            NAME   FlamForge_SelfTest
            COMMAND $<TARGET_FILE:FlamForge> --selftest)
    endif()
    set_tests_properties(FlamForge_SelfTest PROPERTIES
        TIMEOUT 60
        LABELS  "L2;flamforge;selftest")
endif()

if(BUILD_TESTING)
    # L1Tests/ is intentionally different from tests/ — on case-insensitive
    # filesystems (macOS HFS+, Windows NTFS) `tests` and `Tests` mapped to the
    # same path, causing CMake to re-process the same CMakeLists.txt and error
    # on the duplicate target.  Renamed to L1Tests/ to guarantee uniqueness.
    add_subdirectory(tests)     # Catch2-based smoke + determinism tests (CTEST-2/5)
    add_subdirectory(L1Tests)   # JUCE UnitTest-based L1 unit tests (CTEST-3)

    # ------------------------------------------------------------------
    # L4 — ASan + UBSan (CTEST-6 / FLA-57)
    # Apply sanitizer flags to both test targets after subdirs define them.
    # ------------------------------------------------------------------
    if(FLAM_SANITIZE)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            message(STATUS "[CTEST-6] ASan+UBSan enabled on test targets (L4)")
            foreach(_san_tgt flam-tests FlamL1Tests)
                target_compile_options(${_san_tgt} PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
                    -g)
                target_link_options(${_san_tgt} PRIVATE
                    -fsanitize=address,undefined)
            endforeach()
        else()
            message(WARNING "[CTEST-6] FLAM_SANITIZE is only supported on Linux and macOS")
        endif()
    endif()
endif()

# ------------------------------------------------------------------
# L3 — pluginval host-contract validation (CTEST-6 / FLA-57)
# Registered unconditionally when FLAM_PLUGINVAL=ON so that the test
# appears in CTest output even without BUILD_TESTING=ON (the VST3 is
# always built; the plugin tests are orthogonal to the engine tests).
# ------------------------------------------------------------------
if(FLAM_PLUGINVAL)
    include(cmake/FetchPluginval.cmake)

    if(PLUGINVAL_EXECUTABLE AND EXISTS "${PLUGINVAL_EXECUTABLE}")
        # Build the VST3 path to match JUCE's $<CONFIG>-based output layout:
        #   <binary_dir>/FLAM_artefacts/$<CONFIG>/VST3/FLAM.vst3
        # For single-config generators, $<CONFIG> == CMAKE_BUILD_TYPE at build time.
        if(CMAKE_CONFIGURATION_TYPES)
            # Multi-config (MSVC, Xcode, Ninja Multi-Config): evaluate at CTest time.
            set(_flam_vst3 "${CMAKE_BINARY_DIR}/FLAM_artefacts/$<CONFIG>/VST3/FLAM.vst3")
        elseif(CMAKE_BUILD_TYPE)
            set(_flam_vst3 "${CMAKE_BINARY_DIR}/FLAM_artefacts/${CMAKE_BUILD_TYPE}/VST3/FLAM.vst3")
        else()
            # No build type → JUCE emits with an empty config dir (e.g. local dev builds).
            set(_flam_vst3 "${CMAKE_BINARY_DIR}/FLAM_artefacts/VST3/FLAM.vst3")
        endif()

        # On Linux use the Xvfb wrapper so GUI open/close tests run in CI.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_pv_cmd
                "${CMAKE_SOURCE_DIR}/scripts/pluginval-linux-launch.sh"
                "${PLUGINVAL_EXECUTABLE}")
        else()
            set(_pv_cmd "${PLUGINVAL_EXECUTABLE}")
        endif()

        # Output directory for pluginval XML reports
        set(_pv_out "${CMAKE_BINARY_DIR}/pluginval-results")
        file(MAKE_DIRECTORY "${_pv_out}")

        add_test(
            NAME FLAM_L3_PluginvalHostContract
            COMMAND ${_pv_cmd}
                --strictness-level 8
                --validate-in-process
                --output-dir "${_pv_out}"
                "${_flam_vst3}")

        set_tests_properties(FLAM_L3_PluginvalHostContract PROPERTIES
            TIMEOUT  300
            LABELS   "L3;pluginval;host-contract")
        # REQUIRED_FILES supports generator expressions only from CMake 3.27+.
        # For single-config generators the path is already a literal — always safe.
        # For multi-config, skip the pre-check; pluginval will error naturally if missing.
        if(NOT CMAKE_CONFIGURATION_TYPES)
            set_tests_properties(FLAM_L3_PluginvalHostContract PROPERTIES
                REQUIRED_FILES "${_flam_vst3}")
        endif()

        message(STATUS "[CTEST-6] pluginval L3 test registered "
                       "(strictness 8, VST3: ${_flam_vst3})")
    else()
        message(STATUS "[CTEST-6] pluginval executable not available — "
                       "L3 test not registered (check network or set PLUGINVAL_EXECUTABLE)")
    endif()
endif()