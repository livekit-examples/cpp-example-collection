# LiveKitSDK.cmake
#
# A small helper for example repos:
# - Downloads the appropriate prebuilt LiveKit C++ SDK release asset for the host OS/arch
# - Extracts it into a local directory (default: build/_deps/livekit-sdk)
# - Prepends the extracted prefix to CMAKE_PREFIX_PATH so find_package(LiveKit CONFIG REQUIRED) works
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#   include(LiveKitSDK)
#   livekit_sdk_setup(VERSION "0.1.9" SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk")
#
# Optional:
#   livekit_sdk_setup(VERSION "0.1.9" REPO "livekit/client-sdk-cpp" SHA256 "<sha256>")

include_guard(GLOBAL)

function(_lk_detect_host out_os out_arch)
  if(WIN32)
    set(_os "windows")
  elseif(APPLE)
    set(_os "macos")
  elseif(UNIX)
    set(_os "linux")
  else()
    message(FATAL_ERROR "LiveKitSDK: unsupported host OS")
  endif()

  set(_proc "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  if(_proc MATCHES "^(x86_64|AMD64)$")
    set(_arch "x64")
  elseif(_proc MATCHES "^(arm64|aarch64)$")
    set(_arch "arm64")
  else()
    message(FATAL_ERROR "LiveKitSDK: unsupported host arch: ${_proc}")
  endif()

  set(${out_os}   "${_os}"   PARENT_SCOPE)
  set(${out_arch} "${_arch}" PARENT_SCOPE)
endfunction()

function(_lk_default_triple out_triple)
  _lk_detect_host(_os _arch)

  set(_triple "${_os}-${_arch}")
  set(${out_triple} "${_triple}" PARENT_SCOPE)
endfunction()

function(_lk_archive_ext out_ext)
  _lk_detect_host(_os _arch)
  if(_os STREQUAL "windows")
    set(${out_ext} "zip" PARENT_SCOPE)
  else()
    set(${out_ext} "tar.gz" PARENT_SCOPE)
  endif()
endfunction()

# Public:
# livekit_sdk_setup(
#   VERSION <ver>
#   SDK_DIR <dir>
#   [REPO <org/repo>]                 default: livekit/client-sdk-cpp
#   [SHA256 <hash>]                   optional: verify download
#   [TRIPLE <os-arch>]                optional override
#   [DOWNLOAD_DIR <dir>]              default: <build>/_downloads
#   [NO_DOWNLOAD]                     error if not already present
# )
function(livekit_sdk_setup)
  set(options NO_DOWNLOAD)
  set(oneValueArgs VERSION SDK_DIR REPO SHA256 TRIPLE DOWNLOAD_DIR)
  set(multiValueArgs)
  cmake_parse_arguments(LK "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT LK_VERSION)
    message(FATAL_ERROR "livekit_sdk_setup: VERSION is required")
  endif()

  if(NOT LK_SDK_DIR)
    message(FATAL_ERROR "livekit_sdk_setup: SDK_DIR is required")
  endif()

  if(NOT LK_REPO)
    set(LK_REPO "livekit/client-sdk-cpp")
  endif()

  if(NOT LK_TRIPLE)
    _lk_default_triple(LK_TRIPLE)
  endif()

  _lk_archive_ext(_ext)
  set(_archive "livekit-sdk-${LK_TRIPLE}-${LK_VERSION}.${_ext}")
  set(_url "https://github.com/${LK_REPO}/releases/download/v${LK_VERSION}/${_archive}")

  if(NOT LK_DOWNLOAD_DIR)
    set(LK_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_downloads")
  endif()

  set(_dl_dir "${LK_DOWNLOAD_DIR}")
  set(_archive_path "${_dl_dir}/${_archive}")

  # Extracted root folder name (matches your bundle root)
  set(_extracted_root "${LK_SDK_DIR}/livekit-sdk-${LK_TRIPLE}-${LK_VERSION}")

  file(MAKE_DIRECTORY "${_dl_dir}")
  file(MAKE_DIRECTORY "${LK_SDK_DIR}")

  if(NOT EXISTS "${_extracted_root}")
    if(LK_NO_DOWNLOAD)
      message(FATAL_ERROR
        "LiveKitSDK: SDK not found at:\n  ${_extracted_root}\n"
        "and NO_DOWNLOAD was set."
      )
    endif()

    message(STATUS "LiveKitSDK: downloading ${_url}")
    if(LK_SHA256)
      file(DOWNLOAD "${_url}" "${_archive_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        EXPECTED_HASH "SHA256=${LK_SHA256}"
        STATUS _st
      )
    else()
      file(DOWNLOAD "${_url}" "${_archive_path}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS _st
      )
    endif()

    list(GET _st 0 _code)
    list(GET _st 1 _msg)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "LiveKitSDK: download failed\nURL: ${_url}\nStatus: ${_code}\nMessage: ${_msg}")
    endif()

    message(STATUS "LiveKitSDK: extracting ${_archive_path}")
    file(REMOVE_RECURSE "${_extracted_root}")

    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xvf "${_archive_path}"
      WORKING_DIRECTORY "${LK_SDK_DIR}"
      RESULT_VARIABLE _xret
    )
    if(NOT _xret EQUAL 0)
      message(FATAL_ERROR "LiveKitSDK: extraction failed (${_xret}) for ${_archive_path}")
    endif()
  endif()

  if(NOT EXISTS "${_extracted_root}/lib/cmake")
    message(FATAL_ERROR
      "LiveKitSDK: extracted SDK does not look valid (missing lib/cmake)\n"
      "Expected: ${_extracted_root}\n"
      "If your archive root folder name differs, adjust _extracted_root logic."
    )
  endif()

  # Make find_package(LiveKit CONFIG REQUIRED) work.
  list(PREPEND CMAKE_PREFIX_PATH "${_extracted_root}")

  # Export a few useful variables for callers (optional).
  set(LIVEKIT_SDK_EXTRACTED_ROOT "${_extracted_root}" CACHE PATH "LiveKit SDK extracted root" FORCE)
  set(LIVEKIT_SDK_URL_USED "${_url}" CACHE STRING "LiveKit SDK URL used" FORCE)

  message(STATUS "LiveKitSDK: using SDK at ${_extracted_root}")
endfunction()

