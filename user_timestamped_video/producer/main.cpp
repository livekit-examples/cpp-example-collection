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

/// UserTimestampedVideoProducer
///
/// Publishes a synthetic camera track and stamps each frame with
/// `VideoCaptureOptions::metadata.user_timestamp_us`. Pair with
/// `UserTimestampedVideoConsumer` in another process to observe the user
/// timestamps flowing end to end.
///
/// Usage:
///   UserTimestampedVideoProducer <ws-url> <token>
///       [--with-user-timestamp|--without-user-timestamp]
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_TOKEN

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "../common/cli_utils.h"
#include "livekit/livekit.h"

using namespace livekit;

namespace {

constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 360;
constexpr int kFrameIntervalMs = 200;

std::uint64_t nowEpochUs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void fillFrame(VideoFrame& frame, std::uint32_t frame_index) {
  const std::uint8_t blue = static_cast<std::uint8_t>((frame_index * 7) % 255);
  const std::uint8_t green = static_cast<std::uint8_t>((frame_index * 13) % 255);
  const std::uint8_t red = static_cast<std::uint8_t>((frame_index * 29) % 255);

  std::uint8_t* data = frame.data();
  for (std::size_t i = 0; i < frame.dataSize(); i += 4) {
    data[i + 0] = blue;
    data[i + 1] = green;
    data[i + 2] = red;
    data[i + 3] = 255;
  }
}

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

    std::cout << "[producer] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, options)) {
      std::cerr << "[producer] failed to connect\n";
      exit_code = 1;
    } else {
      std::cout << "[producer] connected as " << room.localParticipant()->identity() << " to room '"
                << room.roomInfo().name << "'\n";

      auto source = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
      auto track = LocalVideoTrack::createLocalVideoTrack("timestamped-camera", source);

      try {
        TrackPublishOptions publish_options;
        publish_options.source = TrackSource::SOURCE_CAMERA;
        publish_options.packet_trailer_features.user_timestamp = cli_options.use_user_timestamp;

        room.localParticipant()->publishTrack(track, publish_options);
        std::cout << "[producer] published camera track with user timestamp "
                  << (cli_options.use_user_timestamp ? "enabled" : "disabled") << "\n";

        VideoFrame frame = VideoFrame::create(kFrameWidth, kFrameHeight, VideoBufferType::BGRA);
        const auto capture_start = std::chrono::steady_clock::now();
        std::uint32_t frame_index = 0;
        auto next_frame_at = std::chrono::steady_clock::now();

        while (user_timestamped_video::isRunning()) {
          fillFrame(frame, frame_index);

          VideoCaptureOptions capture_options;

          // a steady_clock to align with other data/video frames
          capture_options.timestamp_us = static_cast<std::int64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - capture_start)
                  .count());
          capture_options.rotation = VideoRotation::VIDEO_ROTATION_0;
          if (cli_options.use_user_timestamp) {
            capture_options.metadata = VideoFrameMetadata{};
            capture_options.metadata->user_timestamp_us = nowEpochUs();
          }

          source->captureFrame(frame, capture_options);

          if (frame_index % 5 == 0) {
            std::cout << "[producer] frame=" << frame_index << " capture_ts_us=" << capture_options.timestamp_us
                      << " user_ts_us="
                      << (cli_options.use_user_timestamp ? std::to_string(*capture_options.metadata->user_timestamp_us)
                                                         : std::string("disabled"))
                      << "\n";
          }

          ++frame_index;
          next_frame_at += std::chrono::milliseconds(kFrameIntervalMs);
          std::this_thread::sleep_until(next_frame_at);
        }
      } catch (const std::exception& error) {
        std::cerr << "[producer] error: " << error.what() << "\n";
        exit_code = 1;
      }

      if (track->publication()) {
        room.localParticipant()->unpublishTrack(track->publication()->sid());
      }
    }
  }

  livekit::shutdown();
  return exit_code;
}
