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

add_library(simple_joystick_sender_support STATIC
  json_utils.cpp
  json_utils.h
  utils.cpp
  utils.h
)

target_include_directories(simple_joystick_sender_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simple_joystick_sender_support PUBLIC nlohmann_json::nlohmann_json)

add_executable(SimpleJoystickSender
  main.cpp
)

target_include_directories(SimpleJoystickSender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SimpleJoystickSender PRIVATE simple_joystick_sender_support ${LIVEKIT_CORE_TARGET})
