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

/// frame_metadata_consumer
///
/// Receives remote video frames via `Room::setOnVideoFrameEventCallback()` and
/// logs `VideoFrameMetadata::user_timestamp_us`, `frame_id`, and `user_data`.
///
/// Usage:
///   frame_metadata_consumer [<ws-url> <token>]
///
/// Or via environment variables (LIVEKIT_URL defaults to ws://localhost:7880):
///   export LIVEKIT_TOKEN=<token>
///   frame_metadata_consumer

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>

#include "cli_utils.h"
#include "constants.h"
#include "json_converters.h"
#include "livekit/livekit.h"
#include "messages.h"

using namespace livekit;

namespace {

std::optional<frame_metadata::SensorReading> parseSensorReading(
    const std::optional<VideoFrameMetadata>& metadata) {
  if (!metadata || !metadata->user_data.has_value()) {
    return std::nullopt;
  }

  return frame_metadata::sensorReadingFromJson(frame_metadata::toString(*metadata->user_data));
}

class FrameMetadataConsumerDelegate : public RoomDelegate {
public:
  explicit FrameMetadataConsumerDelegate(Room& room) : room_(room) {}

  void registerExistingParticipants() {
    for (const auto& weak_participant : room_.remoteParticipants()) {
      if (auto participant = weak_participant.lock()) {
        registerRemoteVideoCallback(participant->identity());
      } else {
        throw std::runtime_error("unable to lock provided remote participant");
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
    room_.clearOnVideoFrameCallback(identity, std::string(frame_metadata::kVideoTrackName));

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

    room_.setOnVideoFrameEventCallback(
        identity, std::string(frame_metadata::kVideoTrackName),
        [](const VideoFrameEvent& event) {
          if (!event.metadata || !event.metadata->frame_id || !event.metadata->user_timestamp_us ||
              !event.metadata->user_data) {
            return;
          }

          const std::uint32_t frame_id = *event.metadata->frame_id;
          if (frame_id % 5 != 0) {
            return;
          }

          const auto reading = parseSensorReading(event.metadata);
          if (!reading) {
            return;
          }

          std::cout << std::fixed << std::setprecision(2) << "[consumer] frame_id=" << frame_id
                    << " capture_ts_us=" << event.timestamp_us
                    << " user_ts_us=" << *event.metadata->user_timestamp_us
                    << " temperature_c=" << reading->temperature_c
                    << " user_data_bytes=" << event.metadata->user_data->size() << "\n";
        },
        stream_options);

    std::cout << "[consumer] listening for video frames from " << identity << " track=\""
              << frame_metadata::kVideoTrackName << "\" with frame metadata\n";
  }

  Room& room_;
  std::mutex mutex_;
  std::unordered_set<std::string> registered_identities_;
};

} // namespace

int main(int argc, char* argv[]) {
  frame_metadata::CliOptions cli_options;

  const frame_metadata::ParseResult parse_result = frame_metadata::parseArgs(argc, argv, cli_options);
  if (parse_result != frame_metadata::ParseResult::Ok) {
    frame_metadata::printUsage(argv[0]);
    return parse_result == frame_metadata::ParseResult::Help ? 0 : 1;
  }

  frame_metadata::installSignalHandlers();

  livekit::initialize(livekit::LogLevel::Info);
  int exit_code = 0;

  {
    Room room;
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    FrameMetadataConsumerDelegate delegate(room);
    room.setDelegate(&delegate);

    std::cout << "[consumer] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, options)) {
      std::cerr << "[consumer] failed to connect\n";
      exit_code = 1;
    } else {
      if (auto lp = room.localParticipant().lock()) {
        std::cout << "[consumer] connected as " << (lp ? lp->identity() : std::string("<unknown>")) << " to room '"
                  << room.roomInfo().name << "'\n";
      } else {
        throw std::runtime_error("unable to lock local participant");
      }

      delegate.registerExistingParticipants();

      while (frame_metadata::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      for (const auto& weak_participant : room.remoteParticipants()) {
        if (auto participant = weak_participant.lock()) {
          room.clearOnVideoFrameCallback(participant->identity(), std::string(frame_metadata::kVideoTrackName));
        } else {
          throw std::runtime_error("unable to lock provided remote participant");
        }
      }
    }

    room.setDelegate(nullptr);
  }

  livekit::shutdown();
  return exit_code;
}
