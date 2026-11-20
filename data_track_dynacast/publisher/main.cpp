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

/// Publishes a data track at 4 Hz with dynacast enabled and prints the result
/// of every push. When the subscriber unsubscribes, the push fails with
/// NO_SUBSCRIBERS. Usage: DataTrackDynacastPublisher <ws-url> <token>
/// (or set LIVEKIT_URL and LIVEKIT_TOKEN). Use a different identity than the
/// subscriber.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "livekit/livekit.h"

using namespace livekit;

int main(int argc, char* argv[]) {
  std::string url = argc >= 3 ? argv[1] : (std::getenv("LIVEKIT_URL") ? std::getenv("LIVEKIT_URL") : "");
  std::string token = argc >= 3 ? argv[2] : (std::getenv("LIVEKIT_TOKEN") ? std::getenv("LIVEKIT_TOKEN") : "");
  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or <ws-url> <token>) are required\n";
    return 1;
  }

  livekit::initialize(livekit::LogLevel::Warn);
  Room room;
  RoomOptions options;
  options.dynacast = true; // the server tells the publisher when nobody subscribes
  if (!room.connect(url, token, options)) {
    std::cerr << "Failed to connect\n";
    return 1;
  }

  auto publish_result = room.localParticipant().lock()->publishDataTrack("dynacast-demo");
  if (!publish_result) {
    std::cerr << "Failed to publish: " << publish_result.error().message << "\n";
    return 1;
  }
  auto track = publish_result.value();

  for (int i = 1;; ++i) { // runs until Ctrl+C
    const std::string text = "frame " + std::to_string(i);
    auto result = track->tryPush({text.begin(), text.end()});
    if (result) {
      std::cout << "sent   " << text << "\n";
    } else if (result.error().code == LocalDataTrackTryPushErrorCode::NO_SUBSCRIBERS) {
      std::cout << "FAILED " << text << " (no subscribers, dynacast dropped it)\n";
    } else {
      std::cout << "FAILED " << text << " (" << result.error().message << ")\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
}
