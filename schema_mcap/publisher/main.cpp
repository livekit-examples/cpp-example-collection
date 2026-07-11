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

/// schema_mcap_publisher
///
/// Defines a JSON Schema, publishes a JSON-encoded data track with that schema,
/// then sends synthetic telemetry frames until interrupted.
///
/// Usage:
///   schema_mcap_publisher [<ws-url> <token>]
///
/// Or via environment variables:
///   LIVEKIT_URL defaults to ws://localhost:7880
///   LIVEKIT_PUBLISHER_TOKEN or LIVEKIT_TOKEN is required

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "schema_mcap_common.h"

using namespace livekit;

namespace {

struct PublisherOptions {
  std::string url;
  std::string token;
};

void printUsage(const char* program) {
  std::cerr << "Usage:\n"
            << "  " << program << " [<ws-url> <token>]\n"
            << "\n"
            << "Environment:\n"
            << "  LIVEKIT_URL              defaults to " << schema_mcap::kDefaultLiveKitUrl << " when unset\n"
            << "  LIVEKIT_PUBLISHER_TOKEN  required unless LIVEKIT_TOKEN or CLI token is provided\n"
            << "  LIVEKIT_TOKEN            fallback token variable\n";
}

bool parseArgs(int argc, char* argv[], PublisherOptions& options) {
  if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
    printUsage(argv[0]);
    return false;
  }

  if (argc != 1 && argc != 3) {
    printUsage(argv[0]);
    return false;
  }

  options.url = schema_mcap::getenvOrEmpty("LIVEKIT_URL");
  options.token = schema_mcap::getenvOrEmpty("LIVEKIT_PUBLISHER_TOKEN");
  if (options.token.empty()) {
    options.token = schema_mcap::getenvOrEmpty("LIVEKIT_TOKEN");
  }

  if (argc == 3) {
    options.url = argv[1];
    options.token = argv[2];
  }

  if (options.url.empty()) {
    options.url = schema_mcap::kDefaultLiveKitUrl;
  }

  if (options.token.empty()) {
    printUsage(argv[0]);
    return false;
  }

  return true;
}

std::string makeTelemetryJson(std::uint64_t sequence, std::uint64_t timestamp_us) {
  const double phase = static_cast<double>(sequence) * 0.12;
  const double temperature_c = 22.0 + (2.5 * std::sin(phase));
  const double humidity_percent = 45.0 + (8.0 * std::cos(phase * 0.7));

  std::ostringstream payload;
  payload << std::fixed << std::setprecision(3) << "{\"sequence\":" << sequence
          << ",\"timestamp_us\":" << timestamp_us << ",\"temperature_c\":" << temperature_c
          << ",\"humidity_percent\":" << humidity_percent << "}";
  return payload.str();
}

} // namespace

int main(int argc, char* argv[]) {
  PublisherOptions cli_options;
  if (!parseArgs(argc, argv, cli_options)) {
    return (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) ? 0 : 1;
  }

  schema_mcap::installSignalHandlers();

  livekit::initialize(livekit::LogLevel::Info);
  int exit_code = 0;

  {
    Room room;
    RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;

    std::cout << "[publisher] connecting to " << cli_options.url << "\n";
    if (!room.connect(cli_options.url, cli_options.token, room_options)) {
      std::cerr << "[publisher] failed to connect\n";
      livekit::shutdown();
      return 1;
    }

    auto local_participant = room.localParticipant().lock();
    if (!local_participant) {
      std::cerr << "[publisher] local participant unavailable\n";
      livekit::shutdown();
      return 1;
    }

    std::cout << "[publisher] connected as identity='" << local_participant->identity() << "' room='"
              << room.roomInfo().name << "'\n";

    try {
      const auto schema_id = schema_mcap::telemetrySchemaId();
      local_participant->defineSchema(schema_id, schema_mcap::kTelemetryJsonSchema);
      std::cout << "[publisher] defined schema '" << schema_id.name << "' encoding=jsonschema\n";

      DataTrackPublishOptions publish_options;
      publish_options.name = schema_mcap::kDataTrackName;
      publish_options.schema = schema_id;
      publish_options.frame_encoding = DataTrackFrameEncoding::Json;

      auto publish_result = local_participant->publishDataTrack(publish_options);
      if (!publish_result) {
        std::cerr << "[publisher] failed to publish data track: "
                  << schema_mcap::describeDataTrackError(publish_result.error()) << "\n";
        livekit::shutdown();
        return 1;
      }

      auto data_track = publish_result.value();
      std::cout << "[publisher] published data track '" << data_track->info().name
                << "' with frame encoding=json\n";

      std::uint64_t sequence = 0;
      auto next_frame_at = std::chrono::steady_clock::now();
      while (schema_mcap::isRunning()) {
        const std::uint64_t timestamp_us = schema_mcap::nowEpochUs();
        const std::string payload_text = makeTelemetryJson(sequence, timestamp_us);
        DataTrackFrame frame(schema_mcap::toPayload(payload_text), timestamp_us);

        auto push_result = data_track->tryPush(frame);
        if (!push_result) {
          std::cerr << "[publisher] failed to push frame: "
                    << schema_mcap::describeDataTrackError(push_result.error()) << "\n";
        } else if (sequence % 10 == 0) {
          std::cout << "[publisher] sequence=" << sequence << " timestamp_us=" << timestamp_us
                    << " bytes=" << frame.payload.size() << "\n";
        }

        ++sequence;
        next_frame_at += std::chrono::milliseconds(100);
        std::this_thread::sleep_until(next_frame_at);
      }

      data_track->unpublishDataTrack();
    } catch (const std::exception& error) {
      std::cerr << "[publisher] error: " << error.what() << "\n";
      exit_code = 1;
    }
  }

  livekit::shutdown();
  return exit_code;
}
