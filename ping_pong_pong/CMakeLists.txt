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

add_library(ping_pong_pong_support STATIC
  json_converters.cpp
  json_converters.h
  constants.h
  messages.h
  utils.h
)

target_include_directories(ping_pong_pong_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ping_pong_pong_support PRIVATE nlohmann_json::nlohmann_json)

add_executable(PingPongPong
  main.cpp
)

target_include_directories(PingPongPong PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PingPongPong PRIVATE ping_pong_pong_support ${LIVEKIT_CORE_TARGET})

livekit_copy_windows_runtime_dlls(PingPongPong)
