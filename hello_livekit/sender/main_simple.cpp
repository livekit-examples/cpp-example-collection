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

  // Start the LiveKit SDK before creating rooms or tracks.
  initialize(LogLevel::Info, LogSink::kConsole);

  auto room = std::make_unique<Room>();
  // Connect to the room using a server URL and participant token.
  if (!room->connect(url, token, RoomOptions{})) {
    std::cerr << "Failed to connect to LiveKit\n";
    return 1;
  }

  auto* participant = room->localParticipant();

  // Publish a synthetic camera track named "camera0" backed by a VideoSource.
  auto video_source = std::make_shared<VideoSource>(640, 480);
  auto video_track = participant->publishVideoTrack("camera0", video_source, TrackSource::SOURCE_CAMERA);
  if (!video_track) {
    std::cerr << "Failed to publish video track\n";
    return 1;
  }

  // Publish a data track named "app-data" for app messages.
  auto data_track_result = participant->publishDataTrack("app-data");
  if (!data_track_result) {
    std::cerr << "Failed to publish data track\n";
    return 1;
  }
  auto data_track = data_track_result.value();

  for (std::uint64_t count = 0; count < 100; ++count) {
    // Push one video frame and one data message.
    video_source->captureFrame(VideoFrame::create(640, 480, VideoBufferType::RGBA));

    const std::string message = "hello #" + std::to_string(count);
    (void)data_track->tryPush(std::vector<std::uint8_t>(message.begin(), message.end()));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
