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

add_executable(basic_room
  main.cpp
  capture_utils.cpp
  capture_utils.h
)

target_include_directories(basic_room PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(basic_room PRIVATE LiveKit::livekit)

# Make -llivekit_ffi resolvable
get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)  # .../lib/cmake
get_filename_component(_lk_lib_dir  "${_lk_cmake_dir}" DIRECTORY) # .../lib
target_link_directories(basic_room PRIVATE "${_lk_lib_dir}")


# Nice-to-have: copy runtime DLLs next to the exe on Windows for "run from build dir".
# Only do this if your exported package provides these targets.
if(WIN32)
  # livekit_ffi.dll
  if(TARGET LiveKit::livekit_ffi)
    add_custom_command(TARGET basic_room POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:LiveKit::livekit_ffi>"
        "$<TARGET_FILE_DIR:basic_room>"
    )
  endif()

  # If you also export protobuf/abseil runtime targets, copy them too (optional).
  if(TARGET protobuf::libprotobuf)
    add_custom_command(TARGET basic_room POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:protobuf::libprotobuf>"
        "$<TARGET_FILE_DIR:basic_room>"
    )
  endif()

  if(TARGET absl::abseil_dll)
    add_custom_command(TARGET basic_room POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:absl::abseil_dll>"
        "$<TARGET_FILE_DIR:basic_room>"
    )
  endif()
endif()
