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

/// frame_metadata_producer
///
/// Publishes a synthetic camera track and stamps each frame with v1.3.0 frame
/// metadata: `user_timestamp_us`, `frame_id`, and compact `user_data`.
///
/// Usage:
///   frame_metadata_producer [<ws-url> <token>]
///
/// Or via environment variables (LIVEKIT_URL defaults to ws://localhost:7880):
///   export LIVEKIT_TOKEN=<token>
///   frame_metadata_producer

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "cli_utils.h"
#include "constants.h"
#include "json_converters.h"
#include "livekit/livekit.h"
#include "messages.h"

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

double simulatedTemperatureC(std::uint32_t frame_id) {
  return 22.0 + (2.5 * std::sin(static_cast<double>(frame_id) * 0.15));
}

void fillFrame(VideoFrame& frame, double temperature_c) {
  const double normalized = std::max(0.0, std::min(1.0, (temperature_c - 18.0) / 10.0));
  const auto red = static_cast<std::uint8_t>(80.0 + (normalized * 175.0));
  const auto blue = static_cast<std::uint8_t>(255.0 - (normalized * 120.0));
  const auto green = static_cast<std::uint8_t>(90);

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

    std::cout << "[producer] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, options)) {
      std::cerr << "[producer] failed to connect\n";
      exit_code = 1;
    } else {
      {
        auto lp = room.localParticipant().lock();
        std::cout << "[producer] connected as " << (lp ? lp->identity() : std::string("<unknown>")) << " to room '"
                  << room.roomInfo().name << "'\n";
      }

      auto source = std::make_shared<VideoSource>(kFrameWidth, kFrameHeight);
      auto track = LocalVideoTrack::createLocalVideoTrack(frame_metadata::kVideoTrackName, source);

      try {
        TrackPublishOptions publish_options;
        publish_options.source = TrackSource::SOURCE_CAMERA;
        publish_options.frame_metadata_features = FrameMetadataFeatures{};
        publish_options.frame_metadata_features->user_timestamp = true;
        publish_options.frame_metadata_features->frame_id = true;
        publish_options.frame_metadata_features->user_data = true;

        {
          auto lp = room.localParticipant().lock();
          if (!lp) throw std::runtime_error("local participant unavailable");
          lp->publishTrack(track, publish_options);
          std::cout << "[producer] published camera track \"" << frame_metadata::kVideoTrackName
                    << "\" with frame metadata\n";
        }

        VideoFrame frame = VideoFrame::create(kFrameWidth, kFrameHeight, VideoBufferType::BGRA);
        const auto capture_start = std::chrono::steady_clock::now();
        std::uint32_t frame_id = 0;
        auto next_frame_at = std::chrono::steady_clock::now();

        while (frame_metadata::isRunning()) {
          const std::uint64_t user_timestamp_us = nowEpochUs();
          const double temperature_c = simulatedTemperatureC(frame_id);

          fillFrame(frame, temperature_c);

          VideoCaptureOptions capture_options;
          capture_options.timestamp_us = static_cast<std::int64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - capture_start)
                  .count());
          capture_options.rotation = VideoRotation::VIDEO_ROTATION_0;

          capture_options.metadata = VideoFrameMetadata{};
          capture_options.metadata->user_timestamp_us = user_timestamp_us;
          capture_options.metadata->frame_id = frame_id;
          capture_options.metadata->user_data = frame_metadata::toPayload(
              frame_metadata::sensorReadingToJson(frame_metadata::SensorReading{temperature_c}));

          source->captureFrame(frame, capture_options);

          if (frame_id % 5 == 0) {
            std::cout << std::fixed << std::setprecision(2) << "[producer] frame_id=" << frame_id
                      << " capture_ts_us=" << capture_options.timestamp_us
                      << " user_ts_us=" << user_timestamp_us << " temperature_c=" << temperature_c
                      << " user_data_bytes=" << capture_options.metadata->user_data->size() << "\n";
          }

          ++frame_id;
          next_frame_at += std::chrono::milliseconds(kFrameIntervalMs);
          std::this_thread::sleep_until(next_frame_at);
        }
      } catch (const std::exception& error) {
        std::cerr << "[producer] error: " << error.what() << "\n";
        exit_code = 1;
      }

      if (auto lp = room.localParticipant().lock()) {
        if (lp && track->publication()) {
          lp->unpublishTrack(track->publication()->sid());
        }
      }
    }
  }

  livekit::shutdown();
  return exit_code;
}
