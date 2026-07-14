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
/// Waits for a schema-advertised LiveKit point-cloud data track, retrieves the
/// publisher's Foxglove JSON Schema, and records received frames to an MCAP
/// file.
///
/// Usage:
///   schema_mcap_recorder [<ws-url> <token>] [--output-dir <dir>] [--frames
///   <count>]
///
/// Or via environment variables:
///   LIVEKIT_URL defaults to ws://localhost:7880
///   LIVEKIT_RECORDER_TOKEN or LIVEKIT_TOKEN is required
///   SCHEMA_MCAP_OUTPUT_DIR defaults to .
///   SCHEMA_MCAP_FRAME_COUNT defaults to 100

#define MCAP_IMPLEMENTATION
#include <livekit/data_track_stream.h>
#include <livekit/remote_data_track.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mcap/writer.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
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
    if (!event.track || event.track->info().name != schema_mcap::kDataTrackName) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!track_) {
        track_ = event.track;
      }
    }
    cv_.notify_all();
  }

  std::shared_ptr<RemoteDataTrack> waitForTrack(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return track_ != nullptr; })) {
      return nullptr;
    }
    return track_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<RemoteDataTrack> track_;
};

void throwIfMcapError(const mcap::Status& status, const std::string& context) {
  if (!status.ok()) {
    throw std::runtime_error(context + ": " + status.message);
  }
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
        std::cerr << "[recorder] failed to connect\n";
        livekit::shutdown();
        return 1;
      }

      auto local_participant = room.localParticipant().lock();
      if (!local_participant) {
        throw std::runtime_error("local participant unavailable");
      }

      std::cout << "[recorder] connected as identity='" << local_participant->identity() << "' room='"
                << room.roomInfo().name << "'\n";
      std::cout << "[recorder] waiting for data track '" << schema_mcap::kDataTrackName << "'\n";

      auto remote_track =
          delegate.waitForTrack(std::chrono::duration_cast<std::chrono::milliseconds>(kTrackWaitTimeout));
      if (!remote_track) {
        throw std::runtime_error("timed out waiting for schema data track");
      }

      const auto& track_info = remote_track->info();
      if (!track_info.schema) {
        throw std::runtime_error("remote data track does not advertise a schema");
      }
      if (!track_info.frame_encoding) {
        throw std::runtime_error("remote data track does not advertise a frame encoding");
      }

      std::cout << "[recorder] discovered publisher='" << remote_track->publisherIdentity() << "' schema='"
                << track_info.schema->name
                << "' schema_encoding=" << schema_mcap::schemaEncodingName(track_info.schema->encoding)
                << " frame_encoding=" << schema_mcap::frameEncodingName(*track_info.frame_encoding) << "\n";

      const std::string schema_definition =
          local_participant->getSchema(*track_info.schema, remote_track->publisherIdentity());
      std::cout << "[recorder] retrieved schema definition bytes=" << schema_definition.size() << "\n";

      std::filesystem::create_directories(cli_options.output_dir);
      const std::filesystem::path output_path = makeOutputPath(cli_options.output_dir);

      mcap::McapWriter writer;
      mcap::McapWriterOptions writer_options("");
      writer_options.compression = mcap::Compression::None;
      throwIfMcapError(writer.open(output_path.string(), writer_options), "failed to open MCAP file");

      mcap::Schema mcap_schema(track_info.schema->name, schema_mcap::schemaEncodingName(track_info.schema->encoding),
                               schema_definition);
      writer.addSchema(mcap_schema);

      mcap::Channel channel(schema_mcap::kMcapChannelTopic, schema_mcap::frameEncodingName(*track_info.frame_encoding),
                            mcap_schema.id);
      writer.addChannel(channel);

      auto subscribe_result = remote_track->subscribe();
      if (!subscribe_result) {
        throw std::runtime_error("failed to subscribe to data track: " +
                                 schema_mcap::describeDataTrackError(subscribe_result.error()));
      }
      auto subscription = subscribe_result.value();

      std::cout << "[recorder] writing " << cli_options.frame_count << " frames to " << output_path << "\n";
      int recorded = 0;
      while (recorded < cli_options.frame_count && schema_mcap::isRunning()) {
        DataTrackFrame frame;
        if (!subscription->read(frame)) {
          break;
        }

        mcap::Message message;
        message.channelId = channel.id;
        message.sequence = static_cast<std::uint32_t>(recorded + 1);
        message.logTime = frame.user_timestamp ? (*frame.user_timestamp * 1000U) : schema_mcap::nowEpochNs();
        message.publishTime = message.logTime;
        message.data = reinterpret_cast<const std::byte*>(frame.payload.data());
        message.dataSize = frame.payload.size();

        throwIfMcapError(writer.write(message), "failed to write MCAP message");

        if (recorded % 10 == 0) {
          std::cout << "[recorder] frame=" << recorded << " bytes=" << frame.payload.size()
                    << " timestamp_ns=" << message.logTime << "\n";
        }
        ++recorded;
      }

      subscription->close();
      writer.close();
      std::cout << "[recorder] wrote " << recorded << " frames to " << output_path << "\n";
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
