cmake_minimum_required(VERSION 3.20)
project(livekit_cpp_example_collection LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------- User knobs --------
# Default to the version you mentioned. Users can override:
#   cmake -S . -B build -DLIVEKIT_SDK_VERSION=0.1.9
set(LIVEKIT_SDK_VERSION "0.1.9" CACHE STRING "LiveKit C++ SDK version to download")

# Optional: enforce integrity if you have the sha256 for that specific asset.
# Users can pass:
#   -DLIVEKIT_SDK_SHA256=<hash>
set(LIVEKIT_SDK_SHA256 "" CACHE STRING "Optional sha256 for the downloaded SDK archive")

# Where to unpack inside the build directory
set(LIVEKIT_SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk" CACHE PATH "Where to unpack the LiveKit SDK")

# -------- Detect platform triple --------
# We’re detecting the *host* because these examples are built natively.
# (If you later want cross-compile, this should key off CMAKE_SYSTEM_* instead.)
set(_host_os "")
if(WIN32)
  set(_host_os "windows")
elseif(APPLE)
  set(_host_os "macos")
elseif(UNIX)
  set(_host_os "linux")
else()
  message(FATAL_ERROR "Unsupported host OS")
endif()

set(_host_arch "")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(_host_arch "x64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(_host_arch "arm64")
else()
  message(FATAL_ERROR "Unsupported host arch: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

# Map to your release artifact naming
# Adjust if you use different naming (e.g., macos-x86_64 vs macos-x64)
if(_host_os STREQUAL "macos" AND _host_arch STREQUAL "x64")
  # If your asset is named macos-x86_64 instead, change this to "macos-x86_64"
  set(SDK_TRIPLE "macos-x64")
else()
  set(SDK_TRIPLE "${_host_os}-${_host_arch}")
endif()

# -------- Compute archive name + URL --------
if(_host_os STREQUAL "windows")
  set(SDK_EXT "zip")
else()
  set(SDK_EXT "tar.gz")
endif()

set(SDK_ARCHIVE "livekit-sdk-${SDK_TRIPLE}-${LIVEKIT_SDK_VERSION}.${SDK_EXT}")
set(SDK_URL "https://github.com/livekit/client-sdk-cpp/releases/download/v${LIVEKIT_SDK_VERSION}/${SDK_ARCHIVE}")

# Where to download
set(SDK_DL_DIR "${CMAKE_BINARY_DIR}/_downloads")
set(SDK_ARCHIVE_PATH "${SDK_DL_DIR}/${SDK_ARCHIVE}")

# The extracted SDK root will look like:
#   <LIVEKIT_SDK_DIR>/livekit-sdk-<triple>-<ver>/
set(SDK_EXTRACTED_ROOT "${LIVEKIT_SDK_DIR}/livekit-sdk-${SDK_TRIPLE}-${LIVEKIT_SDK_VERSION}")

# -------- Download + extract (configure-time) --------
file(MAKE_DIRECTORY "${SDK_DL_DIR}")
file(MAKE_DIRECTORY "${LIVEKIT_SDK_DIR}")

if(NOT EXISTS "${SDK_EXTRACTED_ROOT}")
  message(STATUS "LiveKit SDK not found at: ${SDK_EXTRACTED_ROOT}")
  message(STATUS "Downloading: ${SDK_URL}")

  if(LIVEKIT_SDK_SHA256 STREQUAL "")
    file(DOWNLOAD
      "${SDK_URL}"
      "${SDK_ARCHIVE_PATH}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
  else()
    file(DOWNLOAD
      "${SDK_URL}"
      "${SDK_ARCHIVE_PATH}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
      EXPECTED_HASH "SHA256=${LIVEKIT_SDK_SHA256}"
    )
  endif()

  list(GET _dl_status 0 _dl_code)
  list(GET _dl_status 1 _dl_msg)
  if(NOT _dl_code EQUAL 0)
    message(FATAL_ERROR "Failed to download ${SDK_URL}\nStatus: ${_dl_code}\nMessage: ${_dl_msg}")
  endif()

  message(STATUS "Extracting: ${SDK_ARCHIVE_PATH}")

  # Clean any previous partial extraction
  file(REMOVE_RECURSE "${SDK_EXTRACTED_ROOT}")

  if(_host_os STREQUAL "windows")
    # CMake can extract zip with -E tar
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xvf "${SDK_ARCHIVE_PATH}"
      WORKING_DIRECTORY "${LIVEKIT_SDK_DIR}"
      RESULT_VARIABLE _xret
    )
  else()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xvf "${SDK_ARCHIVE_PATH}"
      WORKING_DIRECTORY "${LIVEKIT_SDK_DIR}"
      RESULT_VARIABLE _xret
    )
  endif()

  if(NOT _xret EQUAL 0)
    message(FATAL_ERROR "Failed to extract ${SDK_ARCHIVE_PATH} (code=${_xret})")
  endif()
endif()

if(NOT EXISTS "${SDK_EXTRACTED_ROOT}/lib/cmake")
  message(FATAL_ERROR
    "Extracted SDK does not look valid (missing lib/cmake).\n"
    "Expected root: ${SDK_EXTRACTED_ROOT}\n"
    "If the archive root folder name differs, adjust SDK_EXTRACTED_ROOT logic."
  )
endif()

# -------- Make find_package(LiveKit) work --------
# Your SDK installs a config package under <prefix>/lib/cmake/...
# The most robust approach is to push <prefix> onto CMAKE_PREFIX_PATH.
list(PREPEND CMAKE_PREFIX_PATH "${SDK_EXTRACTED_ROOT}")

# Some packages also require explicit *_DIR; keep as a fallback option:
# set(LiveKit_DIR "${SDK_EXTRACTED_ROOT}/lib/cmake/LiveKit" CACHE PATH "" FORCE)

find_package(LiveKit CONFIG REQUIRED)

# -------- Build examples --------
add_subdirectory(basic_room)

