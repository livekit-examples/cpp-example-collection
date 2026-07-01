# Copyright 2026 LiveKit, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.20)
project(livekit_cpp_example_collection LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Make "include(LiveKitSDK)" search in ./cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(LIVEKIT_SDK_VERSION "latest" CACHE STRING "LiveKit C++ SDK version (e.g. 1.3.0 or latest)")
set(LIVEKIT_LOCAL_SDK_DIR "" CACHE PATH "Path to a local LiveKit SDK install prefix (skips download)")

if(LIVEKIT_LOCAL_SDK_DIR)
  message(STATUS "Using local LiveKit SDK: ${LIVEKIT_LOCAL_SDK_DIR}")
  list(PREPEND CMAKE_PREFIX_PATH "${LIVEKIT_LOCAL_SDK_DIR}")
else()
  include(LiveKitSDK)
  livekit_sdk_setup(
    VERSION "${LIVEKIT_SDK_VERSION}"
    SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
    GITHUB_TOKEN "$ENV{GITHUB_TOKEN}"
  )
endif()
find_package(LiveKit CONFIG REQUIRED)

if(TARGET LiveKit::livekit)
  set(LIVEKIT_CORE_TARGET LiveKit::livekit)
elseif(TARGET livekit)
  set(LIVEKIT_CORE_TARGET livekit)
else()
  message(FATAL_ERROR "Could not find a LiveKit core target (expected LiveKit::livekit or livekit).")
endif()

set(LIVEKIT_DATA_DIR "")
if(DEFINED LIVEKIT_ROOT_DIR AND EXISTS "${LIVEKIT_ROOT_DIR}/data")
  set(LIVEKIT_DATA_DIR "${LIVEKIT_ROOT_DIR}/data")
endif()

function(livekit_copy_windows_runtime_dlls target_name)
  if(NOT WIN32)
    return()
  endif()

  get_filename_component(_lk_cmake_parent "${LiveKit_DIR}" DIRECTORY)
  get_filename_component(_lk_lib_dir "${_lk_cmake_parent}" DIRECTORY)
  get_filename_component(_lk_prefix "${_lk_lib_dir}" DIRECTORY)
  set(_lk_bin_dir "${_lk_prefix}/bin")

  foreach(_lk_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
    if(EXISTS "${_lk_bin_dir}/${_lk_runtime_dll}")
      add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lk_bin_dir}/${_lk_runtime_dll}"
                "$<TARGET_FILE_DIR:${target_name}>"
      )
    endif()
  endforeach()

  if(TARGET SDL3::SDL3)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:SDL3::SDL3>"
              "$<TARGET_FILE_DIR:${target_name}>"
    )
  endif()
endfunction()

include(ExampleDeps)

add_subdirectory(basic_room)
add_subdirectory(token_source)
add_subdirectory(simple_room)
add_subdirectory(simple_rpc)
add_subdirectory(simple_data_stream)
add_subdirectory(logging_levels/basic_usage logging_levels_basic_usage)
add_subdirectory(logging_levels/custom_sinks logging_levels_custom_sinks)
add_subdirectory(hello_livekit/sender hello_livekit_sender)
add_subdirectory(hello_livekit/receiver hello_livekit_receiver)
add_subdirectory(platform_audio)
add_subdirectory(ping_pong/ping ping_pong_ping)
add_subdirectory(ping_pong/pong ping_pong_pong)
add_subdirectory(user_timestamped_video)
