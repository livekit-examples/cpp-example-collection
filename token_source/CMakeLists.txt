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

# One executable per token source type. Each shares token_source_common.h for
# the logging delegate and the connect/observe session loop.

# Resolve the SDK lib directory so -llivekit_ffi is found at link time.
get_filename_component(_lk_cmake_dir "${LiveKit_DIR}" DIRECTORY)  # .../lib/cmake
get_filename_component(_lk_lib_dir  "${_lk_cmake_dir}" DIRECTORY) # .../lib

set(_token_source_examples
  token_source_literal      token_source_literal.cpp
  token_source_endpoint     token_source_endpoint.cpp
  token_source_development  token_source_development.cpp
  token_source_custom       token_source_custom.cpp
  token_source_caching      token_source_caching.cpp
)

list(LENGTH _token_source_examples _count)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE 0 ${_last} 2)
  list(GET _token_source_examples ${_i} _target)
  math(EXPR _src_idx "${_i} + 1")
  list(GET _token_source_examples ${_src_idx} _src)

  add_executable(${_target} ${_src})
  target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${_target} PRIVATE ${LIVEKIT_CORE_TARGET})
  target_link_directories(${_target} PRIVATE "${_lk_lib_dir}")

  livekit_copy_windows_runtime_dlls(${_target})
endforeach()

unset(_token_source_examples)
