# FetchPluginval.cmake — downloads and pins the pluginval binary at configure time.
#
# LICENCE ISOLATION (DR3): pluginval is released under the GPLv3 by Tracktion.
# This module downloads it as a standalone executable that is invoked only via
# an external process — it is NEVER linked against FLAM source.  The GPLv3
# therefore does not propagate to FLAM's GPL-3.0 codebase.
#
# Outputs:
#   PLUGINVAL_EXECUTABLE — path to the pluginval binary (empty string if unavailable)
#
# Override: cmake -DPLUGINVAL_EXECUTABLE=/usr/local/bin/pluginval ...

cmake_minimum_required(VERSION 3.22)

set(PLUGINVAL_VERSION "1.0.3")
set(_pv_base "https://github.com/Tracktion/pluginval/releases/download/v${PLUGINVAL_VERSION}")
set(_pv_cache_dir "${CMAKE_BINARY_DIR}/_pluginval_bin")

# ------------------------------------------------------------------
# Platform selection
# ------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_pv_archive "pluginval_linux.zip")
    set(_pv_binary  "pluginval")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_pv_archive "pluginval_mac.zip")
    # macOS bundle layout: the real binary is inside the .app
    set(_pv_binary  "pluginval.app/Contents/MacOS/pluginval")
elseif(WIN32)
    set(_pv_archive "pluginval_windows.zip")
    set(_pv_binary  "pluginval.exe")
else()
    message(WARNING "[FetchPluginval] Unsupported platform '${CMAKE_SYSTEM_NAME}' — "
                    "pluginval tests will not be registered.")
    set(PLUGINVAL_EXECUTABLE "" PARENT_SCOPE)
    return()
endif()

set(_pv_expected_exe "${_pv_cache_dir}/${_pv_binary}")

# ------------------------------------------------------------------
# Cache variable — users can point at a system install or CI cache
# ------------------------------------------------------------------
set(PLUGINVAL_EXECUTABLE "${_pv_expected_exe}"
    CACHE FILEPATH "Path to pluginval binary (auto-downloaded if not overridden)")

if(EXISTS "${PLUGINVAL_EXECUTABLE}")
    message(STATUS "[FetchPluginval] Using cached pluginval: ${PLUGINVAL_EXECUTABLE}")
    return()
endif()

# ------------------------------------------------------------------
# Download
# ------------------------------------------------------------------
message(STATUS "[FetchPluginval] Downloading pluginval v${PLUGINVAL_VERSION} "
               "from GitHub (Tracktion/pluginval, GPLv3)...")

file(MAKE_DIRECTORY "${_pv_cache_dir}")
set(_pv_zip "${_pv_cache_dir}/${_pv_archive}")

file(DOWNLOAD
    "${_pv_base}/${_pv_archive}"
    "${_pv_zip}"
    TLS_VERIFY ON
    SHOW_PROGRESS
    STATUS _pv_status)

list(GET _pv_status 0 _pv_code)
list(GET _pv_status 1 _pv_msg)

if(NOT _pv_code EQUAL 0)
    message(WARNING "[FetchPluginval] Download failed (${_pv_code}: ${_pv_msg}). "
                    "L3 pluginval tests will not be registered. "
                    "Tip: set -DPLUGINVAL_EXECUTABLE=<path> to skip the download.")
    set(PLUGINVAL_EXECUTABLE "" CACHE FILEPATH "" FORCE)
    return()
endif()

# ------------------------------------------------------------------
# Extract
# ------------------------------------------------------------------
file(ARCHIVE_EXTRACT INPUT "${_pv_zip}" DESTINATION "${_pv_cache_dir}")

if(NOT EXISTS "${_pv_expected_exe}")
    message(WARNING "[FetchPluginval] Archive extracted but binary not found at "
                    "${_pv_expected_exe}. L3 tests will not be registered.")
    set(PLUGINVAL_EXECUTABLE "" CACHE FILEPATH "" FORCE)
    return()
endif()

# Ensure the binary is executable on POSIX platforms
if(NOT WIN32)
    file(CHMOD "${_pv_expected_exe}"
         PERMISSIONS
             OWNER_EXECUTE OWNER_READ OWNER_WRITE
             GROUP_EXECUTE GROUP_READ
             WORLD_EXECUTE WORLD_READ)
endif()

set(PLUGINVAL_EXECUTABLE "${_pv_expected_exe}" CACHE FILEPATH "" FORCE)
message(STATUS "[FetchPluginval] pluginval v${PLUGINVAL_VERSION} ready: ${_pv_expected_exe}")
