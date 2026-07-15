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
/// Defines Foxglove PointCloud and FrameTransform JSON Schemas, publishes
/// JSON-encoded data tracks for both, then sends synchronized frames.
///
/// Usage:
///   schema_mcap_publisher [<ws-url> <token>] --pointcloud-sequence <dir>
///
/// Or via environment variables:
///   LIVEKIT_URL defaults to ws://localhost:7880
///   LIVEKIT_PUBLISHER_TOKEN or LIVEKIT_TOKEN is required
///   SCHEMA_MCAP_POINTCLOUD_SEQUENCE supplies the sequence directory

#include <livekit/data_track_frame.h>
#include <livekit/data_track_options.h>
#include <livekit/local_data_track.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "schema_mcap_common.h"

using namespace livekit;

namespace {

constexpr std::uint32_t kPointStride = 4U * sizeof(float);
constexpr std::uint8_t kFoxgloveFloat32 = 7;

struct PublisherOptions {
  std::string url;
  std::string token;
  std::string pointcloud_sequence_path;
  std::size_t frame_count = 0;
};

struct Point {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float intensity = 0.0F;
};

struct FramePose {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double qx = 0.0;
  double qy = 0.0;
  double qz = 0.0;
  double qw = 1.0;
};

void printUsage(const char* program) {
  std::cerr << "Usage:\n"
            << "  " << program << " [<ws-url> <token>] --pointcloud-sequence <dir>\n"
            << "\n"
            << "Options:\n"
            << "  --pointcloud-sequence <dir>\n"
            << "                           replay frames listed in <dir>/frames.txt\n"
            << "  --frames <count>         frames to publish (default: full sequence)\n"
            << "\n"
            << "Environment:\n"
            << "  LIVEKIT_URL              defaults to " << schema_mcap::kDefaultLiveKitUrl << " when unset\n"
            << "  LIVEKIT_PUBLISHER_TOKEN  required unless LIVEKIT_TOKEN or "
               "CLI token is provided\n"
            << "  LIVEKIT_TOKEN            fallback token variable\n"
            << "  SCHEMA_MCAP_POINTCLOUD_SEQUENCE\n"
            << "                           point-cloud sequence directory\n"
            << "  SCHEMA_MCAP_FRAME_COUNT  optional frame count\n";
}

std::size_t parsePositiveSize(const std::string& text, const char* field_name) {
  std::size_t consumed = 0;
  const auto value = static_cast<std::size_t>(std::stoull(text, &consumed));
  if (consumed != text.size() || value == 0U) {
    throw std::runtime_error(std::string(field_name) + " must be a positive integer");
  }
  return value;
}

bool parseArgs(int argc, char* argv[], PublisherOptions& options, bool& requested_help) {
  requested_help = false;
  std::vector<std::string> positional;
  options.url = schema_mcap::getenvOrEmpty("LIVEKIT_URL");
  options.token = schema_mcap::getenvOrEmpty("LIVEKIT_PUBLISHER_TOKEN");
  if (options.token.empty()) {
    options.token = schema_mcap::getenvOrEmpty("LIVEKIT_TOKEN");
  }
  options.pointcloud_sequence_path = schema_mcap::getenvOrEmpty("SCHEMA_MCAP_POINTCLOUD_SEQUENCE");
  if (const std::string frame_count = schema_mcap::getenvOrEmpty("SCHEMA_MCAP_FRAME_COUNT"); !frame_count.empty()) {
    options.frame_count = parsePositiveSize(frame_count, "SCHEMA_MCAP_FRAME_COUNT");
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      requested_help = true;
      printUsage(argv[0]);
      return false;
    }
    if (arg == "--pointcloud-sequence") {
      if (++i >= argc) {
        printUsage(argv[0]);
        return false;
      }
      options.pointcloud_sequence_path = argv[i];
      continue;
    }
    if (arg == "--frames") {
      if (++i >= argc) {
        printUsage(argv[0]);
        return false;
      }
      options.frame_count = parsePositiveSize(argv[i], "--frames");
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      printUsage(argv[0]);
      return false;
    }

    positional.push_back(arg);
  }

  if (positional.size() != 0 && positional.size() != 2) {
    printUsage(argv[0]);
    return false;
  }

  if (positional.size() == 2) {
    options.url = positional[0];
    options.token = positional[1];
  }

  if (options.url.empty()) {
    options.url = schema_mcap::kDefaultLiveKitUrl;
  }

  if (options.token.empty()) {
    printUsage(argv[0]);
    return false;
  }
  if (options.pointcloud_sequence_path.empty()) {
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

std::string stripTrailingCarriageReturn(std::string value) {
  if (!value.empty() && value.back() == '\r') {
    value.pop_back();
  }
  return value;
}

float readFloat32LittleEndian(const char* bytes) {
  const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0]));
  const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1]));
  const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2]));
  const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
  const std::uint32_t bits = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string joinPath(const std::string& directory, const std::string& filename) {
  if (directory.empty() || directory.back() == '/' || directory.back() == '\\') {
    return directory + filename;
  }
#if defined(_WIN32)
  return directory + "\\" + filename;
#else
  return directory + "/" + filename;
#endif
}

std::vector<Point> loadPointCloudFrame(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("failed to open point-cloud sequence frame: " + path);
  }

  const std::streamsize byte_count = input.tellg();
  if (byte_count <= 0 || byte_count % static_cast<std::streamsize>(kPointStride) != 0) {
    throw std::runtime_error("point-cloud sequence frame must contain x, y, z, intensity FLOAT32 records: " + path);
  }
  input.seekg(0);

  std::vector<char> bytes(static_cast<std::size_t>(byte_count));
  input.read(bytes.data(), byte_count);
  if (input.gcount() != byte_count) {
    throw std::runtime_error("failed to read complete point-cloud sequence frame: " + path);
  }

  std::vector<Point> points;
  points.reserve(bytes.size() / kPointStride);
  for (std::size_t offset = 0; offset < bytes.size(); offset += kPointStride) {
    points.push_back(Point{readFloat32LittleEndian(bytes.data() + offset),
                           readFloat32LittleEndian(bytes.data() + offset + 4U),
                           readFloat32LittleEndian(bytes.data() + offset + 8U),
                           readFloat32LittleEndian(bytes.data() + offset + 12U)});
  }
  return points;
}

std::vector<std::vector<Point>> loadPointCloudSequence(const std::string& directory) {
  const std::string manifest_path = joinPath(directory, "frames.txt");
  std::ifstream manifest(manifest_path);
  if (!manifest) {
    throw std::runtime_error("failed to open point-cloud sequence manifest: " + manifest_path);
  }

  std::vector<std::vector<Point>> frames;
  std::string line;
  while (std::getline(manifest, line)) {
    line = stripTrailingCarriageReturn(std::move(line));
    if (line.empty() || line[0] == '#') {
      continue;
    }
    frames.push_back(loadPointCloudFrame(joinPath(directory, line)));
  }

  if (frames.empty()) {
    throw std::runtime_error("point-cloud sequence manifest contains no frames: " + manifest_path);
  }
  return frames;
}

std::vector<FramePose> loadPointCloudPoses(const std::string& directory) {
  const std::string poses_path = joinPath(directory, "poses.txt");
  std::ifstream input(poses_path);
  if (!input) {
    throw std::runtime_error("failed to open point-cloud sequence poses: " + poses_path);
  }

  std::vector<FramePose> poses;
  std::string line;
  while (std::getline(input, line)) {
    line = stripTrailingCarriageReturn(std::move(line));
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream row(line);
    FramePose pose;
    if (!(row >> pose.x >> pose.y >> pose.z >> pose.qx >> pose.qy >> pose.qz >> pose.qw)) {
      throw std::runtime_error("invalid point-cloud sequence pose: " + poses_path);
    }
    poses.push_back(pose);
  }

  if (poses.empty()) {
    throw std::runtime_error("point-cloud sequence contains no poses: " + poses_path);
  }
  return poses;
}

std::vector<std::uint8_t> makePackedPoints(const std::vector<Point>& points) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(points.size() * kPointStride);

  for (const auto& point : points) {
    appendFloat32LittleEndian(bytes, point.x);
    appendFloat32LittleEndian(bytes, point.y);
    appendFloat32LittleEndian(bytes, point.z);
    appendFloat32LittleEndian(bytes, point.intensity);
  }

  return bytes;
}

std::string makePointCloudJson(const std::vector<std::uint8_t>& packed_points, std::uint64_t timestamp_us,
                               const char* frame_id) {
  const std::uint64_t seconds = timestamp_us / 1'000'000U;
  const std::uint64_t nanoseconds = (timestamp_us % 1'000'000U) * 1'000U;
  const std::string data = base64Encode(packed_points);
  std::ostringstream payload;
  payload << "{\"timestamp\":{\"sec\":" << seconds << ",\"nsec\":" << nanoseconds
          << "},\"frame_id\":\"" << frame_id << "\",\"pose\":{\"position\":{\"x\":0,\"y\":0,"
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

std::string makeFrameTransformJson(const FramePose& pose, std::uint64_t timestamp_us) {
  const std::uint64_t seconds = timestamp_us / 1'000'000U;
  const std::uint64_t nanoseconds = (timestamp_us % 1'000'000U) * 1'000U;
  std::ostringstream payload;
  payload << "{\"timestamp\":{\"sec\":" << seconds << ",\"nsec\":" << nanoseconds
          << "},\"parent_frame_id\":\"map\",\"child_frame_id\":\"lidar\","
             "\"translation\":{\"x\":"
          << pose.x << ",\"y\":" << pose.y << ",\"z\":" << pose.z << "},\"rotation\":{\"x\":" << pose.qx
          << ",\"y\":" << pose.qy << ",\"z\":" << pose.qz << ",\"w\":" << pose.qw << "}}";
  return payload.str();
}

} // namespace

int main(int argc, char* argv[]) {
  PublisherOptions cli_options;
  bool requested_help = false;
  try {
    if (!parseArgs(argc, argv, cli_options, requested_help)) {
      return requested_help ? 0 : 1;
    }
  } catch (const std::exception& error) {
    std::cerr << "[publisher] error: " << error.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  std::vector<std::vector<Point>> pointcloud_sequence;
  std::vector<FramePose> pointcloud_poses;
  try {
    pointcloud_sequence = loadPointCloudSequence(cli_options.pointcloud_sequence_path);
    pointcloud_poses = loadPointCloudPoses(cli_options.pointcloud_sequence_path);
    if (pointcloud_poses.size() != pointcloud_sequence.size()) {
      throw std::runtime_error("point-cloud sequence frame and pose counts do not match");
    }
    std::cout << "[publisher] loaded " << pointcloud_sequence.size() << " frames from "
              << cli_options.pointcloud_sequence_path << "\n";
  } catch (const std::exception& error) {
    std::cerr << "[publisher] error: " << error.what() << "\n";
    return 1;
  }

  std::size_t frames_to_publish = cli_options.frame_count == 0 ? pointcloud_sequence.size() : cli_options.frame_count;
  if (frames_to_publish > pointcloud_sequence.size()) {
    std::cout << "[publisher] requested " << cli_options.frame_count << " frames, but sequence contains "
              << pointcloud_sequence.size() << "; publishing one complete pass\n";
    frames_to_publish = pointcloud_sequence.size();
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
      const auto pointcloud_schema_id = schema_mcap::pointCloudSchemaId();
      const auto transform_schema_id = schema_mcap::frameTransformSchemaId();
      local_participant->defineSchema(pointcloud_schema_id, schema_mcap::kPointCloudJsonSchema);
      local_participant->defineSchema(transform_schema_id, schema_mcap::kFrameTransformJsonSchema);
      std::cout << "[publisher] defined schemas '" << pointcloud_schema_id.name << "' and '"
                << transform_schema_id.name << "' encoding=jsonschema\n";

      DataTrackPublishOptions pointcloud_publish_options;
      pointcloud_publish_options.name = schema_mcap::kDataTrackName;
      pointcloud_publish_options.schema = pointcloud_schema_id;
      pointcloud_publish_options.frame_encoding = DataTrackFrameEncoding::Json;

      auto pointcloud_publish_result = local_participant->publishDataTrack(pointcloud_publish_options);
      if (!pointcloud_publish_result) {
        std::cerr << "[publisher] failed to publish point-cloud data track: "
                  << schema_mcap::describeDataTrackError(pointcloud_publish_result.error()) << "\n";
        livekit::shutdown();
        return 1;
      }

      DataTrackPublishOptions transform_publish_options;
      transform_publish_options.name = schema_mcap::kTransformDataTrackName;
      transform_publish_options.schema = transform_schema_id;
      transform_publish_options.frame_encoding = DataTrackFrameEncoding::Json;

      auto transform_publish_result = local_participant->publishDataTrack(transform_publish_options);
      if (!transform_publish_result) {
        std::cerr << "[publisher] failed to publish transform data track: "
                  << schema_mcap::describeDataTrackError(transform_publish_result.error()) << "\n";
        livekit::shutdown();
        return 1;
      }

      auto pointcloud_track = pointcloud_publish_result.value();
      auto transform_track = transform_publish_result.value();
      std::cout << "[publisher] published data tracks '" << pointcloud_track->info().name << "' and '"
                << transform_track->info().name << "' with frame encoding=json\n";

      // Give a waiting recorder time to discover and subscribe before the
      // finite sequence begins.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::uint64_t sequence = 0;
      auto next_frame_at = std::chrono::steady_clock::now();
      while (sequence < frames_to_publish && schema_mcap::isRunning()) {
        const std::uint64_t timestamp_us = schema_mcap::nowEpochUs();
        const auto& input_points = pointcloud_sequence[static_cast<std::size_t>(sequence)];
        const std::vector<std::uint8_t> packed_points = makePackedPoints(input_points);
        const FramePose pose = pointcloud_poses[static_cast<std::size_t>(sequence)];
        const std::string pointcloud_payload = makePointCloudJson(packed_points, timestamp_us, "lidar");
        const std::string transform_payload = makeFrameTransformJson(pose, timestamp_us);
        DataTrackFrame pointcloud_frame(schema_mcap::toPayload(pointcloud_payload), timestamp_us);
        DataTrackFrame transform_frame(schema_mcap::toPayload(transform_payload), timestamp_us);

        auto transform_push_result = transform_track->tryPush(transform_frame);
        if (!transform_push_result) {
          std::cerr << "[publisher] failed to push transform frame: "
                    << schema_mcap::describeDataTrackError(transform_push_result.error()) << "\n";
        }
        auto pointcloud_push_result = pointcloud_track->tryPush(pointcloud_frame);
        if (!pointcloud_push_result) {
          std::cerr << "[publisher] failed to push point-cloud frame: "
                    << schema_mcap::describeDataTrackError(pointcloud_push_result.error()) << "\n";
        } else if (transform_push_result && sequence % 10 == 0) {
          std::cout << "[publisher] frame=" << sequence << " points=" << input_points.size()
                    << " payload_bytes=" << pointcloud_frame.payload.size() + transform_frame.payload.size()
                    << "\n";
        }

        ++sequence;
        next_frame_at += std::chrono::milliseconds(100);
        std::this_thread::sleep_until(next_frame_at);
      }

      // Data-track pushes are asynchronous. Allow the final synchronized pair
      // to reach subscribers before unpublishing the finite tracks.
      if (sequence == frames_to_publish && schema_mcap::isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      pointcloud_track->unpublishDataTrack();
      transform_track->unpublishDataTrack();
      std::cout << "[publisher] published " << sequence << " point-cloud frames\n";
    } catch (const std::exception& error) {
      std::cerr << "[publisher] error: " << error.what() << "\n";
      exit_code = 1;
    }
  }

  livekit::shutdown();
  return exit_code;
}
