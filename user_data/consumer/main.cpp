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

/// UserDataConsumer
///
/// Receives video frame events with v1.3.0 metadata and parses small
/// temperature readings from `VideoFrameMetadata::user_data`.
///
/// Usage:
///   UserDataConsumer <ws-url> <token>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_TOKEN

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

std::optional<std::uint32_t> metadataFrameId(const std::optional<VideoFrameMetadata>& metadata) {
  if (!metadata || !metadata->frame_id.has_value()) {
    return std::nullopt;
  }

  return *metadata->frame_id;
}

std::optional<user_data::SensorReading> metadataSensorReading(const std::optional<VideoFrameMetadata>& metadata) {
  if (!metadata || !metadata->user_data.has_value()) {
    return std::nullopt;
  }

  return user_data::sensorReadingFromJson(user_data::toString(*metadata->user_data));
}

std::string formatOptionalTimestamp(std::optional<std::uint64_t> timestamp_us) {
  return timestamp_us ? std::to_string(*timestamp_us) : std::string("n/a");
}

class UserDataConsumerDelegate : public RoomDelegate {
public:
  explicit UserDataConsumerDelegate(Room& room) : room_(room) {}

  void registerExistingParticipants() {
    for (const auto& weak_participant : room_.remoteParticipants()) {
      if (auto participant = weak_participant.lock()) {
        registerRemoteVideoCallback(participant->identity());
      } else {
        throw std::runtime_error("unable to lock provided remote participant");
      }
    }
  }

  void clearAllCallbacks() {
    std::unordered_set<std::string> identities;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      identities.swap(registered_identities_);
    }

    for (const auto& identity : identities) {
      room_.clearOnVideoFrameCallback(identity, std::string(user_data::kVideoTrackName));
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
    room_.clearOnVideoFrameCallback(identity, std::string(user_data::kVideoTrackName));
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
        identity, std::string(user_data::kVideoTrackName),
        [identity](const VideoFrameEvent& event) {
          try {
            const auto frame_id = metadataFrameId(event.metadata);
            const auto reading = metadataSensorReading(event.metadata);

            std::cout << "[consumer] from=" << identity << " size=" << event.frame.width() << "x"
                      << event.frame.height() << " capture_ts_us=" << event.timestamp_us
                      << " metadata_frame_id=" << (frame_id ? std::to_string(*frame_id) : std::string("n/a"))
                      << " user_ts_us=" << formatOptionalTimestamp(event.metadata ? event.metadata->user_timestamp_us
                                                                                  : std::nullopt);

            if (reading) {
              std::cout << std::fixed << std::setprecision(2) << " reading_frame_id=" << reading->frame_id
                        << " temperature_c=" << reading->temperature_c
                        << " reading_ts_us=" << reading->timestamp_us
                        << " user_data_bytes=" << event.metadata->user_data->size();
            } else {
              std::cout << " user_data=n/a";
            }

            std::cout << "\n";
          } catch (const std::exception& error) {
            std::cerr << "[consumer] failed to process frame metadata from " << identity << ": " << error.what()
                      << "\n";
          }
        },
        stream_options);

    std::cout << "[consumer] listening for video frames from " << identity << " track=\""
              << user_data::kVideoTrackName << "\"\n";
  }

  Room& room_;
  std::mutex mutex_;
  std::unordered_set<std::string> registered_identities_;
};

} // namespace

int main(int argc, char* argv[]) {
  user_data::CliOptions cli_options;

  const user_data::ParseResult parse_result = user_data::parseArgs(argc, argv, cli_options);
  if (parse_result != user_data::ParseResult::Ok) {
    user_data::printUsage(argv[0]);
    return parse_result == user_data::ParseResult::Help ? 0 : 1;
  }

  user_data::installSignalHandlers();

  livekit::initialize(livekit::LogLevel::Info);
  int exit_code = 0;

  {
    Room room;
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    UserDataConsumerDelegate delegate(room);
    room.setDelegate(&delegate);

    std::cout << "[consumer] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, options)) {
      std::cerr << "[consumer] failed to connect\n";
      exit_code = 1;
    } else {
      if (auto lp = room.localParticipant().lock()) {
        std::cout << "[consumer] connected as " << lp->identity() << " to room '" << room.roomInfo().name << "'\n";
      } else {
        throw std::runtime_error("unable to lock local participant");
      }

      delegate.registerExistingParticipants();

      while (user_data::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      delegate.clearAllCallbacks();
    }

    room.setDelegate(nullptr);
  }

  livekit::shutdown();
  return exit_code;
}
