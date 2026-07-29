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

/// schema_mcap_recorder
///
/// Waits for schema-advertised LiveKit point-cloud and transform data tracks,
/// retrieves both Foxglove JSON Schemas, and records synchronized frames to one
/// MCAP file.
///
/// Usage:
///   schema_mcap_recorder [<ws-url> <token>] [--output-dir <dir>] [--frames
///   <count>]
///
/// Or via environment variables:
///   LIVEKIT_URL defaults to ws://localhost:7880
///   LIVEKIT_RECORDER_TOKEN or LIVEKIT_TOKEN is required
///   SCHEMA_MCAP_OUTPUT_DIR defaults to .
///   SCHEMA_MCAP_FRAME_COUNT defaults to 80

#define MCAP_IMPLEMENTATION
#include <livekit/data_track_stream.h>
#include <livekit/remote_data_track.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mcap/writer.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "schema_mcap_common.h"

using namespace livekit;

namespace {

constexpr auto kTrackWaitTimeout = std::chrono::seconds(30);

struct RecorderOptions {
  std::string url;
  std::string token;
  std::filesystem::path output_dir{"."};
  int frame_count = schema_mcap::kDefaultRecorderFrameCount;
};

void printUsage(const char* program) {
  std::cerr << "Usage:\n"
            << "  " << program << " [<ws-url> <token>] [--output-dir <dir>] [--frames <count>]\n"
            << "\n"
            << "Environment:\n"
            << "  LIVEKIT_URL              defaults to " << schema_mcap::kDefaultLiveKitUrl << " when unset\n"
            << "  LIVEKIT_RECORDER_TOKEN   required unless LIVEKIT_TOKEN or "
               "CLI token is provided\n"
            << "  LIVEKIT_TOKEN            fallback token variable\n"
            << "  SCHEMA_MCAP_OUTPUT_DIR   defaults to .\n"
            << "  SCHEMA_MCAP_FRAME_COUNT  defaults to " << schema_mcap::kDefaultRecorderFrameCount << "\n";
}

int parsePositiveInt(const std::string& text, const char* field_name) {
  std::size_t consumed = 0;
  const int value = std::stoi(text, &consumed);
  if (consumed != text.size() || value <= 0) {
    throw std::runtime_error(std::string(field_name) + " must be a positive integer");
  }
  return value;
}

bool parseArgs(int argc, char* argv[], RecorderOptions& options, bool& requested_help) {
  requested_help = false;
  std::vector<std::string> positional;

  options.url = schema_mcap::getenvOrEmpty("LIVEKIT_URL");
  options.token = schema_mcap::getenvOrEmpty("LIVEKIT_RECORDER_TOKEN");
  if (options.token.empty()) {
    options.token = schema_mcap::getenvOrEmpty("LIVEKIT_TOKEN");
  }

  if (const std::string output_dir = schema_mcap::getenvOrEmpty("SCHEMA_MCAP_OUTPUT_DIR"); !output_dir.empty()) {
    options.output_dir = output_dir;
  }
  if (const std::string frame_count = schema_mcap::getenvOrEmpty("SCHEMA_MCAP_FRAME_COUNT"); !frame_count.empty()) {
    options.frame_count = parsePositiveInt(frame_count, "SCHEMA_MCAP_FRAME_COUNT");
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      requested_help = true;
      printUsage(argv[0]);
      return false;
    }
    if (arg == "--output-dir") {
      if (++i >= argc) {
        printUsage(argv[0]);
        return false;
      }
      options.output_dir = argv[i];
      continue;
    }
    if (arg == "--frames") {
      if (++i >= argc) {
        printUsage(argv[0]);
        return false;
      }
      options.frame_count = parsePositiveInt(argv[i], "--frames");
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

  return true;
}

std::filesystem::path makeOutputPath(const std::filesystem::path& output_dir) {
  return output_dir / ("livekit_pointcloud_" + schema_mcap::localTimestampForFilename() + ".mcap");
}

class RecorderDelegate : public RoomDelegate {
public:
  void onDataTrackPublished(Room&, const DataTrackPublishedEvent& event) override {
    if (!event.track) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (event.track->info().name == schema_mcap::kDataTrackName && !pointcloud_track_) {
        pointcloud_track_ = event.track;
      } else if (event.track->info().name == schema_mcap::kTransformDataTrackName && !transform_track_) {
        transform_track_ = event.track;
      } else {
        return;
      }
    }
    cv_.notify_all();
  }

  std::pair<std::shared_ptr<RemoteDataTrack>, std::shared_ptr<RemoteDataTrack>> waitForTracks(
      std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return pointcloud_track_ != nullptr && transform_track_ != nullptr; })) {
      return {};
    }
    return {pointcloud_track_, transform_track_};
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<RemoteDataTrack> pointcloud_track_;
  std::shared_ptr<RemoteDataTrack> transform_track_;
};

void throwIfMcapError(const mcap::Status& status, const std::string& context) {
  if (!status.ok()) {
    throw std::runtime_error(context + ": " + status.message);
  }
}

std::string formatBitrate(double bits_per_second) {
  std::ostringstream output;
  output << std::fixed;
  if (bits_per_second >= 1'000'000.0) {
    output << std::setprecision(2) << bits_per_second / 1'000'000.0 << " Mbps";
  } else if (bits_per_second >= 1'000.0) {
    output << std::setprecision(1) << bits_per_second / 1'000.0 << " kbps";
  } else {
    output << std::setprecision(0) << bits_per_second << " bps";
  }
  return output.str();
}

std::string formatMilliseconds(double milliseconds) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << milliseconds << " ms";
  return output.str();
}

void validateTrackMetadata(const std::shared_ptr<RemoteDataTrack>& track, const DataTrackSchemaId& expected_schema) {
  const auto& info = track->info();
  if (!info.schema) {
    throw std::runtime_error("remote data track '" + info.name + "' does not advertise a schema");
  }
  if (*info.schema != expected_schema) {
    throw std::runtime_error("remote data track '" + info.name + "' advertises unexpected schema '" +
                             info.schema->name + "'");
  }
  if (!info.frame_encoding) {
    throw std::runtime_error("remote data track '" + info.name + "' does not advertise a frame encoding");
  }
  if (*info.frame_encoding != DataTrackFrameEncoding::Json) {
    throw std::runtime_error("remote data track '" + info.name + "' is not JSON encoded");
  }
}

std::string getRequiredSchema(const std::shared_ptr<LocalParticipant>& local_participant,
                              const DataTrackSchemaId& schema_id, const std::string& publisher_identity) {
  auto definition = local_participant->getSchema(schema_id, publisher_identity);
  if (!definition) {
    throw std::runtime_error("failed to retrieve schema '" + schema_id.name + "' from publisher '" +
                             publisher_identity + "'");
  }
  return std::move(*definition);
}

} // namespace

int main(int argc, char* argv[]) {
  RecorderOptions cli_options;
  bool requested_help = false;

  try {
    if (!parseArgs(argc, argv, cli_options, requested_help)) {
      return requested_help ? 0 : 1;
    }
  } catch (const std::exception& error) {
    std::cerr << "[recorder] " << error.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  schema_mcap::installSignalHandlers();

  livekit::initialize(livekit::LogLevel::Info);
  int exit_code = 0;

  {
    Room room;
    RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;

    RecorderDelegate delegate;
    room.setDelegate(&delegate);

    try {
      std::cout << "[recorder] connecting to " << cli_options.url << "\n";
      if (!room.connect(cli_options.url, cli_options.token, room_options)) {
        throw std::runtime_error("failed to connect");
      }

      auto local_participant = room.localParticipant().lock();
      if (!local_participant) {
        throw std::runtime_error("local participant unavailable");
      }

      std::cout << "[recorder] connected as identity='" << local_participant->identity() << "' room='"
                << room.roomInfo().name << "'\n";
      std::cout << "[recorder] waiting for data tracks '" << schema_mcap::kDataTrackName << "' and '"
                << schema_mcap::kTransformDataTrackName << "'\n";

      auto [pointcloud_track, transform_track] =
          delegate.waitForTracks(std::chrono::duration_cast<std::chrono::milliseconds>(kTrackWaitTimeout));
      if (!pointcloud_track || !transform_track) {
        throw std::runtime_error("timed out waiting for schema data tracks");
      }

      if (pointcloud_track->publisherIdentity() != transform_track->publisherIdentity()) {
        throw std::runtime_error("point-cloud and transform data tracks have different publishers");
      }
      validateTrackMetadata(pointcloud_track, schema_mcap::pointCloudSchemaId());
      validateTrackMetadata(transform_track, schema_mcap::frameTransformSchemaId());

      const auto& pointcloud_info = pointcloud_track->info();
      const auto& transform_info = transform_track->info();
      std::cout << "[recorder] discovered schemas '" << pointcloud_info.schema->name << "' and '"
                << transform_info.schema->name << "' from publisher='" << pointcloud_track->publisherIdentity()
                << "'\n";

      const std::string pointcloud_schema_definition =
          getRequiredSchema(local_participant, *pointcloud_info.schema, pointcloud_track->publisherIdentity());
      const std::string transform_schema_definition =
          getRequiredSchema(local_participant, *transform_info.schema, transform_track->publisherIdentity());
      std::cout << "[recorder] retrieved schema definitions bytes=" << pointcloud_schema_definition.size() << "+"
                << transform_schema_definition.size() << "\n";

      std::filesystem::create_directories(cli_options.output_dir);
      const std::filesystem::path output_path = makeOutputPath(cli_options.output_dir);

      mcap::McapWriter writer;
      mcap::McapWriterOptions writer_options("");
      writer_options.compression = mcap::Compression::None;
      throwIfMcapError(writer.open(output_path.string(), writer_options), "failed to open MCAP file");

      mcap::Schema pointcloud_schema(pointcloud_info.schema->name,
                                     schema_mcap::schemaEncodingName(pointcloud_info.schema->encoding),
                                     pointcloud_schema_definition);
      writer.addSchema(pointcloud_schema);
      mcap::Schema transform_schema(transform_info.schema->name,
                                    schema_mcap::schemaEncodingName(transform_info.schema->encoding),
                                    transform_schema_definition);
      writer.addSchema(transform_schema);

      mcap::Channel pointcloud_channel(schema_mcap::kMcapChannelTopic,
                                       schema_mcap::frameEncodingName(*pointcloud_info.frame_encoding),
                                       pointcloud_schema.id);
      writer.addChannel(pointcloud_channel);
      mcap::Channel transform_channel(schema_mcap::kTransformMcapChannelTopic,
                                      schema_mcap::frameEncodingName(*transform_info.frame_encoding),
                                      transform_schema.id);
      writer.addChannel(transform_channel);

      auto pointcloud_subscribe_result = pointcloud_track->subscribe();
      if (!pointcloud_subscribe_result) {
        throw std::runtime_error("failed to subscribe to point-cloud data track: " +
                                 schema_mcap::describeDataTrackError(pointcloud_subscribe_result.error()));
      }
      auto transform_subscribe_result = transform_track->subscribe();
      if (!transform_subscribe_result) {
        throw std::runtime_error("failed to subscribe to transform data track: " +
                                 schema_mcap::describeDataTrackError(transform_subscribe_result.error()));
      }
      auto pointcloud_subscription = pointcloud_subscribe_result.value();
      auto transform_subscription = transform_subscribe_result.value();

      std::cout << "[recorder] writing " << cli_options.frame_count << " synchronized frame pairs to " << output_path
                << "\n";
      int recorded = 0;
      std::uint64_t total_payload_bytes = 0;
      std::uint64_t report_payload_bytes = 0;
      std::optional<std::chrono::steady_clock::time_point> first_received_at;
      std::optional<std::chrono::steady_clock::time_point> last_received_at;
      std::optional<std::chrono::steady_clock::time_point> report_started_at;
      std::size_t latency_sample_count = 0;
      double latency_sum_ms = 0.0;
      std::optional<double> minimum_latency_ms;
      std::optional<double> maximum_latency_ms;

      while (recorded < cli_options.frame_count && schema_mcap::isRunning()) {
        DataTrackFrame transform_frame;
        DataTrackFrame pointcloud_frame;
        if (!transform_subscription->read(transform_frame) || !pointcloud_subscription->read(pointcloud_frame)) {
          break;
        }
        if (transform_frame.user_timestamp != pointcloud_frame.user_timestamp) {
          throw std::runtime_error("received unsynchronized point-cloud and transform frames");
        }

        const auto received_at = std::chrono::steady_clock::now();
        const std::uint64_t received_epoch_us = schema_mcap::nowEpochUs();
        const std::uint64_t payload_bytes = pointcloud_frame.payload.size() + transform_frame.payload.size();
        total_payload_bytes += payload_bytes;

        if (!first_received_at) {
          first_received_at = received_at;
          report_started_at = received_at;
        } else {
          report_payload_bytes += payload_bytes;
        }
        last_received_at = received_at;

        std::optional<double> latency_ms;
        if (pointcloud_frame.user_timestamp) {
          const auto publisher_timestamp_us = *pointcloud_frame.user_timestamp;
          const std::int64_t latency_us = received_epoch_us >= publisher_timestamp_us
                                              ? static_cast<std::int64_t>(received_epoch_us - publisher_timestamp_us)
                                              : -static_cast<std::int64_t>(publisher_timestamp_us - received_epoch_us);
          latency_ms = static_cast<double>(latency_us) / 1000.0;
          latency_sum_ms += *latency_ms;
          ++latency_sample_count;
          if (!minimum_latency_ms || *latency_ms < *minimum_latency_ms) {
            minimum_latency_ms = latency_ms;
          }
          if (!maximum_latency_ms || *latency_ms > *maximum_latency_ms) {
            maximum_latency_ms = latency_ms;
          }
        }

        const auto write_frame = [&](const DataTrackFrame& frame, const mcap::Channel& channel) {
          mcap::Message message;
          message.channelId = channel.id;
          message.sequence = static_cast<std::uint32_t>(recorded + 1);
          message.logTime = frame.user_timestamp ? (*frame.user_timestamp * 1000U) : schema_mcap::nowEpochNs();
          message.publishTime = message.logTime;
          message.data = reinterpret_cast<const std::byte*>(frame.payload.data());
          message.dataSize = frame.payload.size();
          throwIfMcapError(writer.write(message), "failed to write MCAP message");
          return message.logTime;
        };
        write_frame(transform_frame, transform_channel);
        write_frame(pointcloud_frame, pointcloud_channel);

        if (recorded % 10 == 0) {
          std::cout << "[recorder] frame=" << recorded << " payload_bytes=" << payload_bytes
                    << " (pointcloud=" << pointcloud_frame.payload.size()
                    << ", transform=" << transform_frame.payload.size() << ")";
          if (recorded > 0 && report_started_at) {
            const double report_seconds = std::chrono::duration<double>(received_at - *report_started_at).count();
            if (report_seconds > 0.0) {
              std::cout << " bitrate="
                        << formatBitrate((static_cast<double>(report_payload_bytes) * 8.0) / report_seconds);
            }
            report_payload_bytes = 0;
            report_started_at = received_at;
          } else {
            std::cout << " bitrate=n/a";
          }
          std::cout << " latency=" << (latency_ms ? formatMilliseconds(*latency_ms) : "n/a") << "\n";
        }
        ++recorded;
      }

      pointcloud_subscription->close();
      transform_subscription->close();
      writer.close();
      std::cout << "[recorder] wrote " << recorded << " synchronized frame pairs to " << output_path << "\n";

      std::string session_duration = "n/a";
      std::string average_bitrate = "n/a";
      if (first_received_at && last_received_at && *last_received_at > *first_received_at) {
        const double session_seconds = std::chrono::duration<double>(*last_received_at - *first_received_at).count();
        std::ostringstream duration;
        duration << std::fixed << std::setprecision(2) << session_seconds << " s";
        session_duration = duration.str();
        average_bitrate = formatBitrate((static_cast<double>(total_payload_bytes) * 8.0) / session_seconds);
      }

      std::string average_latency = "n/a";
      std::string minimum_latency = "n/a";
      std::string maximum_latency = "n/a";
      if (latency_sample_count > 0) {
        average_latency = formatMilliseconds(latency_sum_ms / static_cast<double>(latency_sample_count));
        minimum_latency = formatMilliseconds(*minimum_latency_ms);
        maximum_latency = formatMilliseconds(*maximum_latency_ms);
      }

      const auto print_session_stat = [](const char* label, const std::string& value) {
        std::cout << "  " << std::left << std::setw(24) << label << value << "\n";
      };
      std::cout << "[recorder] session statistics\n";
      print_session_stat("Frames", std::to_string(recorded));
      print_session_stat("Duration", session_duration);
      print_session_stat("Payload bytes", std::to_string(total_payload_bytes));
      print_session_stat("Average bitrate", average_bitrate);
      print_session_stat("Average latency", average_latency);
      print_session_stat("Minimum latency", minimum_latency);
      print_session_stat("Maximum latency", maximum_latency);

      if (recorded < cli_options.frame_count && schema_mcap::isRunning()) {
        throw std::runtime_error("data track ended after " + std::to_string(recorded) + " of " +
                                 std::to_string(cli_options.frame_count) + " requested frames");
      }
    } catch (const std::exception& error) {
      std::cerr << "[recorder] error: " << error.what() << "\n";
      exit_code = 1;
    }

    room.setDelegate(nullptr);
  }

  livekit::shutdown();
  return exit_code;
}
