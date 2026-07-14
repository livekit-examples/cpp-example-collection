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
/// Defines the Foxglove PointCloud JSON Schema, publishes a JSON-encoded data
/// track with that schema, then sends five seconds of synthetic point clouds.
///
/// Usage:
///   schema_mcap_publisher [<ws-url> <token>]
///
/// Or via environment variables:
///   LIVEKIT_URL defaults to ws://localhost:7880
///   LIVEKIT_PUBLISHER_TOKEN or LIVEKIT_TOKEN is required

#include <livekit/data_track_frame.h>
#include <livekit/data_track_options.h>
#include <livekit/local_data_track.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "schema_mcap_common.h"

using namespace livekit;

namespace {

constexpr std::uint32_t kPointStride = 4U * sizeof(float);
constexpr std::uint8_t kFoxgloveFloat32 = 7;
constexpr double kPi = 3.14159265358979323846;

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
            << "  LIVEKIT_PUBLISHER_TOKEN  required unless LIVEKIT_TOKEN or "
               "CLI token is provided\n"
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

void appendFloat32LittleEndian(std::vector<std::uint8_t>& bytes, float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t), "Foxglove FLOAT32 requires a 32-bit float");
  static_assert(std::numeric_limits<float>::is_iec559, "Foxglove FLOAT32 requires IEEE-754 floats");

  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bytes.push_back(static_cast<std::uint8_t>(bits));
  bytes.push_back(static_cast<std::uint8_t>(bits >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(bits >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(bits >> 24U));
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);

  std::size_t offset = 0;
  while (offset + 3U <= bytes.size()) {
    const std::uint32_t block = (static_cast<std::uint32_t>(bytes[offset]) << 16U) |
                                (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                                static_cast<std::uint32_t>(bytes[offset + 2U]);
    encoded.push_back(kAlphabet[(block >> 18U) & 0x3fU]);
    encoded.push_back(kAlphabet[(block >> 12U) & 0x3fU]);
    encoded.push_back(kAlphabet[(block >> 6U) & 0x3fU]);
    encoded.push_back(kAlphabet[block & 0x3fU]);
    offset += 3U;
  }

  const std::size_t remaining = bytes.size() - offset;
  if (remaining > 0U) {
    std::uint32_t block = static_cast<std::uint32_t>(bytes[offset]) << 16U;
    if (remaining == 2U) {
      block |= static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U;
    }
    encoded.push_back(kAlphabet[(block >> 18U) & 0x3fU]);
    encoded.push_back(kAlphabet[(block >> 12U) & 0x3fU]);
    encoded.push_back(remaining == 2U ? kAlphabet[(block >> 6U) & 0x3fU] : '=');
    encoded.push_back('=');
  }

  return encoded;
}

std::vector<std::uint8_t> makePackedPoints(std::uint64_t sequence) {
  constexpr int kColumns = 32;
  constexpr int kRows = schema_mcap::kPointCloudPointsPerFrame / kColumns;
  static_assert(kColumns * kRows == schema_mcap::kPointCloudPointsPerFrame);

  std::vector<std::uint8_t> bytes;
  bytes.reserve(schema_mcap::kPointCloudPointsPerFrame * kPointStride);

  const double phase = static_cast<double>(sequence) * 0.12;
  for (int row = 0; row < kRows; ++row) {
    const double row_fraction = static_cast<double>(row) / static_cast<double>(kRows - 1);
    const float z = static_cast<float>((row_fraction * 3.0) - 1.5);
    for (int column = 0; column < kColumns; ++column) {
      const double azimuth = (2.0 * kPi * static_cast<double>(column) / kColumns) + phase;
      const double radius = 2.0 + (0.25 * std::sin((3.0 * azimuth) + phase + z));
      const float x = static_cast<float>(radius * std::cos(azimuth));
      const float y = static_cast<float>(radius * std::sin(azimuth));
      const float intensity = static_cast<float>(0.5 + (0.5 * std::sin(azimuth + (2.0 * z) + phase)));

      appendFloat32LittleEndian(bytes, x);
      appendFloat32LittleEndian(bytes, y);
      appendFloat32LittleEndian(bytes, z);
      appendFloat32LittleEndian(bytes, intensity);
    }
  }

  return bytes;
}

std::string makePointCloudJson(std::uint64_t sequence, std::uint64_t timestamp_us) {
  const std::uint64_t seconds = timestamp_us / 1'000'000U;
  const std::uint64_t nanoseconds = (timestamp_us % 1'000'000U) * 1'000U;
  const std::string data = base64Encode(makePackedPoints(sequence));
  std::ostringstream payload;
  payload << "{\"timestamp\":{\"sec\":" << seconds << ",\"nsec\":" << nanoseconds
          << "},\"frame_id\":\"lidar\",\"pose\":{\"position\":{\"x\":0,\"y\":0,"
             "\"z\":0},"
             "\"orientation\":{\"x\":0,\"y\":0,\"z\":0,\"w\":1}},\"point_stride\":"
          << kPointStride
          << ",\"fields\":[{\"name\":\"x\",\"offset\":0,\"type\":" << static_cast<unsigned int>(kFoxgloveFloat32)
          << "},{\"name\":\"y\",\"offset\":4,\"type\":" << static_cast<unsigned int>(kFoxgloveFloat32)
          << "},{\"name\":\"z\",\"offset\":8,\"type\":" << static_cast<unsigned int>(kFoxgloveFloat32)
          << "},{\"name\":\"intensity\",\"offset\":12,\"type\":" << static_cast<unsigned int>(kFoxgloveFloat32)
          << "}],\"data\":\"" << data << "\"}";
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
      const auto schema_id = schema_mcap::pointCloudSchemaId();
      local_participant->defineSchema(schema_id, schema_mcap::kPointCloudJsonSchema);
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
      std::cout << "[publisher] published data track '" << data_track->info().name << "' with frame encoding=json\n";

      // Give a waiting recorder time to discover and subscribe to the newly
      // advertised track before the finite five-second sequence begins.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::uint64_t sequence = 0;
      auto next_frame_at = std::chrono::steady_clock::now();
      while (sequence < schema_mcap::kPointCloudFrameCount && schema_mcap::isRunning()) {
        const std::uint64_t timestamp_us = schema_mcap::nowEpochUs();
        const std::string payload_text = makePointCloudJson(sequence, timestamp_us);
        DataTrackFrame frame(schema_mcap::toPayload(payload_text), timestamp_us);

        auto push_result = data_track->tryPush(frame);
        if (!push_result) {
          std::cerr << "[publisher] failed to push frame: " << schema_mcap::describeDataTrackError(push_result.error())
                    << "\n";
        } else if (sequence % 10 == 0) {
          std::cout << "[publisher] sequence=" << sequence << " timestamp_us=" << timestamp_us
                    << " points=" << schema_mcap::kPointCloudPointsPerFrame << " bytes=" << frame.payload.size()
                    << "\n";
        }

        ++sequence;
        next_frame_at += std::chrono::milliseconds(100);
        std::this_thread::sleep_until(next_frame_at);
      }

      data_track->unpublishDataTrack();
      std::cout << "[publisher] published " << sequence << " point-cloud frames\n";
    } catch (const std::exception& error) {
      std::cerr << "[publisher] error: " << error.what() << "\n";
      exit_code = 1;
    }
  }

  livekit::shutdown();
  return exit_code;
}
