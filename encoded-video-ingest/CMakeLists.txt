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

add_executable(EncodedVideoIngestProducer
  producer.cpp
)

target_include_directories(EncodedVideoIngestProducer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EncodedVideoIngestProducer PRIVATE ${LIVEKIT_CORE_TARGET})

if(WIN32)
  target_link_libraries(EncodedVideoIngestProducer PRIVATE ws2_32)
endif()

livekit_copy_windows_runtime_dlls(EncodedVideoIngestProducer)

add_executable(EncodedVideoIngestConsumer
  consumer.cpp
  ../simple_room/sdl_video_renderer.cpp
  ../simple_room/sdl_video_renderer.h
)

target_include_directories(EncodedVideoIngestConsumer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../simple_room
)
target_link_libraries(EncodedVideoIngestConsumer PRIVATE ${LIVEKIT_CORE_TARGET} SDL3::SDL3)

livekit_copy_windows_runtime_dlls(EncodedVideoIngestConsumer)
