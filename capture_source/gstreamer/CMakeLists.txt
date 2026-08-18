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

add_library(livekit_gstreamer_capture_example_common STATIC
  common/gstreamer_capture_example.cpp
)

target_include_directories(livekit_gstreamer_capture_example_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(livekit_gstreamer_capture_example_common PUBLIC ${LIVEKIT_CORE_TARGET})

# Collect all GStreamer capture example executables in this directory rather than
# one directory per example. The $<1:...> wrapper keeps multi-config generators
# from appending a per-config subdirectory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}>")

add_subdirectory(test_pipeline)
add_subdirectory(webcam)
