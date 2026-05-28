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

add_executable(HelloLivekitSender
  main.cpp
)

target_include_directories(HelloLivekitSender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HelloLivekitSender PRIVATE ${LIVEKIT_CORE_TARGET})

livekit_copy_windows_runtime_dlls(HelloLivekitSender)

add_executable(HelloLivekitSenderSimple
  main_simple.cpp
)

target_include_directories(HelloLivekitSenderSimple PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HelloLivekitSenderSimple PRIVATE ${LIVEKIT_CORE_TARGET})

livekit_copy_windows_runtime_dlls(HelloLivekitSenderSimple)
