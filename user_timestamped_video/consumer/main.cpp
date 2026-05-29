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

/// UserTimestampedVideoConsumer
///
/// Receives remote video frames via `Room::setOnVideoFrameEventCallback()` and
/// logs any `VideoFrameMetadata::user_timestamp_us` values that arrive. Pair
/// with `UserTimestampedVideoProducer` running in another process.
///
/// Usage:
///   UserTimestampedVideoConsumer <ws-url> <token>
///       [--with-user-timestamp|--without-user-timestamp]
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_TOKEN

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

#include "../common/cli_utils.h"
#include "livekit/livekit.h"

using namespace livekit;

namespace {

constexpr const char* kTrackName = "timestamped-camera";

std::string formatUserTimestamp(const std::optional<VideoFrameMetadata>& metadata) {
  if (!metadata || !metadata->user_timestamp_us.has_value()) {
    return "n/a";
  }

  return std::to_string(*metadata->user_timestamp_us);
}

class UserTimestampedVideoConsumerDelegate : public RoomDelegate {
public:
  UserTimestampedVideoConsumerDelegate(Room& room, bool read_user_timestamp)
      : room_(room), read_user_timestamp_(read_user_timestamp) {}

  void registerExistingParticipants() {
    for (const auto& participant : room_.remoteParticipants()) {
      if (participant) {
        registerRemoteVideoCallback(participant->identity());
      }
    }
  }

  void onParticipantConnected(Room&, const ParticipantConnectedEvent& event) override {
    if (!event.participant) {
      return;
    }

    std::cout << "[consumer] participant connected: " << event.participant->identity() << "\n";
    registerRemoteVideoCallback(event.participant->identity());
  }

  void onParticipantDisconnected(Room&, const ParticipantDisconnectedEvent& event) override {
    if (!event.participant) {
      return;
    }

    const std::string identity = event.participant->identity();
    room_.clearOnVideoFrameCallback(identity, std::string(kTrackName));

    {
      std::lock_guard<std::mutex> lock(mutex_);
      registered_identities_.erase(identity);
    }

    std::cout << "[consumer] participant disconnected: " << identity << "\n";
  }

private:
  void registerRemoteVideoCallback(const std::string& identity) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!registered_identities_.insert(identity).second) {
        return;
      }
    }

    VideoStream::Options stream_options;
    stream_options.format = VideoBufferType::RGBA;

    if (read_user_timestamp_) {
      room_.setOnVideoFrameEventCallback(
          identity, std::string(kTrackName),
          [identity](const VideoFrameEvent& event) {
            std::cout << "[consumer] from=" << identity << " size=" << event.frame.width() << "x"
                      << event.frame.height() << " capture_ts_us=" << event.timestamp_us
                      << " user_ts_us=" << formatUserTimestamp(event.metadata)
                      << " rotation=" << static_cast<int>(event.rotation) << "\n";
          },
          stream_options);
    } else {
      room_.setOnVideoFrameCallback(
          identity, std::string(kTrackName),
          [identity](const VideoFrame& frame, const std::int64_t timestamp_us) {
            std::cout << "[consumer] from=" << identity << " size=" << frame.width() << "x" << frame.height()
                      << " capture_ts_us=" << timestamp_us << " user_ts_us=ignored\n";
          },
          stream_options);
    }

    std::cout << "[consumer] listening for video frames from " << identity << " track=\"" << kTrackName
              << "\" with user timestamp " << (read_user_timestamp_ ? "enabled" : "ignored") << "\n";
  }

  Room& room_;
  bool read_user_timestamp_;
  std::mutex mutex_;
  std::unordered_set<std::string> registered_identities_;
};

} // namespace

int main(int argc, char* argv[]) {
  user_timestamped_video::CliOptions cli_options;

  const user_timestamped_video::ParseResult parse_result = user_timestamped_video::parseArgs(argc, argv, cli_options);
  if (parse_result != user_timestamped_video::ParseResult::Ok) {
    user_timestamped_video::printUsage(argv[0]);
    return parse_result == user_timestamped_video::ParseResult::Help ? 0 : 1;
  }

  user_timestamped_video::installSignalHandlers();

  livekit::initialize(livekit::LogLevel::Info);
  int exit_code = 0;

  {
    Room room;
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    UserTimestampedVideoConsumerDelegate delegate(room, cli_options.use_user_timestamp);
    room.setDelegate(&delegate);

    std::cout << "[consumer] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, options)) {
      std::cerr << "[consumer] failed to connect\n";
      exit_code = 1;
    } else {
      std::cout << "[consumer] connected as " << room.localParticipant()->identity() << " to room '"
                << room.roomInfo().name << "' with user timestamp "
                << (cli_options.use_user_timestamp ? "enabled" : "ignored") << "\n";

      delegate.registerExistingParticipants();

      while (user_timestamped_video::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      for (const auto& participant : room.remoteParticipants()) {
        if (participant) {
          room.clearOnVideoFrameCallback(participant->identity(), std::string(kTrackName));
        }
      }
    }

    room.setDelegate(nullptr);
  }

  livekit::shutdown();
  return exit_code;
}
