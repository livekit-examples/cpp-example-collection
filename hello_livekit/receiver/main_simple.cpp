/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"

using namespace livekit;

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  const std::string url = "wss://my_url";
  const std::string token = "1234_token";
  const std::string sender_identity = "sender_identity";

  // Start the LiveKit SDK before creating rooms.
  initialize(LogLevel::Info, LogSink::kConsole);

  auto room = std::make_unique<Room>();
  // Connect to the room using a server URL and participant token.
  if (!room->connect(url, token, RoomOptions{})) {
    std::cerr << "Failed to connect to LiveKit\n";
    return 1;
  }

  // Subscribe to video frames from the sender's camera track "camera0"
  room->setOnVideoFrameCallback(sender_identity, "camera0", [](const VideoFrame& frame, std::int64_t) {
    std::cout << "video frame: " << frame.width() << "x" << frame.height() << "\n";
  });

  // Subscribe to messages from the sender's data track "app-data"
  room->addOnDataFrameCallback(sender_identity, "app-data",
                               [](const std::vector<std::uint8_t>& payload, std::optional<std::uint64_t>) {
                                 const std::string message(payload.begin(), payload.end());
                                 std::cout << "data message: " << message << "\n";
                               });

  std::this_thread::sleep_for(std::chrono::seconds(30));

  return 0;
}
