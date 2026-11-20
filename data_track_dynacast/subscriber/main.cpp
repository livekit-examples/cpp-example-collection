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

/// Subscribes to the publisher's data track, then toggles the subscription
/// every 3 seconds. Usage: DataTrackDynacastSubscriber <ws-url> <token>
/// (or set LIVEKIT_URL and LIVEKIT_TOKEN). Use a different identity than the
/// publisher.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "livekit/livekit.h"
#include "livekit/remote_data_track.h"

using namespace livekit;

// Remembers the publisher's data track when the room announces it.
class Delegate : public RoomDelegate {
public:
  void onDataTrackPublished(Room&, const DataTrackPublishedEvent& e) override {
    std::lock_guard<std::mutex> lock(mutex_);
    track_ = e.track;
  }
  std::shared_ptr<RemoteDataTrack> track() {
    std::lock_guard<std::mutex> lock(mutex_);
    return track_;
  }

private:
  std::mutex mutex_;
  std::shared_ptr<RemoteDataTrack> track_;
};

int main(int argc, char* argv[]) {
  std::string url = argc >= 3 ? argv[1] : (std::getenv("LIVEKIT_URL") ? std::getenv("LIVEKIT_URL") : "");
  std::string token = argc >= 3 ? argv[2] : (std::getenv("LIVEKIT_TOKEN") ? std::getenv("LIVEKIT_TOKEN") : "");
  if (url.empty() || token.empty()) {
    std::cerr << "LIVEKIT_URL and LIVEKIT_TOKEN (or <ws-url> <token>) are required\n";
    return 1;
  }

  livekit::initialize(livekit::LogLevel::Warn);
  Room room;
  Delegate delegate;
  room.setDelegate(&delegate);
  if (!room.connect(url, token, RoomOptions{})) {
    std::cerr << "Failed to connect\n";
    return 1;
  }

  std::cout << "waiting for the publisher's data track...\n";
  std::shared_ptr<RemoteDataTrack> track;
  while (!(track = delegate.track())) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  while (true) { // runs until Ctrl+C
    auto result = track->subscribe();
    if (!result) {
      std::cerr << "Subscribe failed: " << result.error().message << "\n";
      return 1;
    }
    auto stream = result.value();
    std::cout << ">>> SUBSCRIBED\n";
    std::thread reader([stream] {
      DataTrackFrame frame;
      while (stream->read(frame)) {
        std::cout << "received " << std::string(frame.payload.begin(), frame.payload.end()) << "\n";
      }
    });

    std::this_thread::sleep_for(std::chrono::seconds(3));
    stream->close(); // unsubscribe; wakes the blocked read()
    reader.join();
    std::cout << ">>> UNSUBSCRIBED\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
  }
}
