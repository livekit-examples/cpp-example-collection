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

#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "livekit/livekit.h"

namespace schema_mcap {

inline constexpr char kDefaultLiveKitUrl[] = "ws://localhost:7880";
inline constexpr char kDataTrackName[] = "telemetry-json";
inline constexpr char kSchemaName[] = "livekit.example.Telemetry";
inline constexpr char kMcapChannelTopic[] = "/livekit/telemetry";
inline constexpr int kDefaultRecorderFrameCount = 100;

inline constexpr char kTelemetryJsonSchema[] = R"json({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://livekit.io/examples/schema-mcap/telemetry.schema.json",
  "title": "LiveKit Example Telemetry",
  "type": "object",
  "additionalProperties": false,
  "required": ["sequence", "timestamp_us", "temperature_c", "humidity_percent"],
  "properties": {
    "sequence": {
      "type": "integer",
      "minimum": 0
    },
    "timestamp_us": {
      "type": "integer",
      "minimum": 0,
      "description": "Unix epoch timestamp in microseconds."
    },
    "temperature_c": {
      "type": "number"
    },
    "humidity_percent": {
      "type": "number",
      "minimum": 0,
      "maximum": 100
    }
  }
})json";

inline std::atomic<bool> g_running{true};

inline void handleSignal(int) { g_running.store(false); }

inline void installSignalHandlers() {
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif
}

inline bool isRunning() { return g_running.load(std::memory_order_relaxed); }

inline std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

inline std::uint64_t nowEpochUs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

inline std::uint64_t nowEpochNs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

inline std::string localTimestampForFilename() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &now);
#else
  localtime_r(&now, &local_time);
#endif

  std::ostringstream out;
  out << std::put_time(&local_time, "%Y%m%d_%H%M%S");
  return out.str();
}

inline std::vector<std::uint8_t> toPayload(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

inline std::string toString(const std::vector<std::uint8_t>& payload) {
  return std::string(payload.begin(), payload.end());
}

inline livekit::DataTrackSchemaId telemetrySchemaId() {
  return livekit::DataTrackSchemaId{kSchemaName, livekit::DataTrackSchemaEncoding::JsonSchema};
}

inline std::string schemaEncodingName(const livekit::DataTrackSchemaEncoding& encoding) {
  if (encoding.isCustom()) {
    return encoding.customIdentifier();
  }

  switch (encoding.wellKnown()) {
    case livekit::DataTrackSchemaEncoding::Protobuf:
      return "protobuf";
    case livekit::DataTrackSchemaEncoding::Flatbuffer:
      return "flatbuffer";
    case livekit::DataTrackSchemaEncoding::Ros1Msg:
      return "ros1msg";
    case livekit::DataTrackSchemaEncoding::Ros2Msg:
      return "ros2msg";
    case livekit::DataTrackSchemaEncoding::Ros2Idl:
      return "ros2idl";
    case livekit::DataTrackSchemaEncoding::OmgIdl:
      return "omgidl";
    case livekit::DataTrackSchemaEncoding::JsonSchema:
      return "jsonschema";
    case livekit::DataTrackSchemaEncoding::Other:
      return "unknown";
  }

  return "unknown";
}

inline std::string frameEncodingName(const livekit::DataTrackFrameEncoding& encoding) {
  if (encoding.isCustom()) {
    return encoding.customIdentifier();
  }

  switch (encoding.wellKnown()) {
    case livekit::DataTrackFrameEncoding::Ros1:
      return "ros1";
    case livekit::DataTrackFrameEncoding::Cdr:
      return "cdr";
    case livekit::DataTrackFrameEncoding::Protobuf:
      return "protobuf";
    case livekit::DataTrackFrameEncoding::Flatbuffer:
      return "flatbuffer";
    case livekit::DataTrackFrameEncoding::Cbor:
      return "cbor";
    case livekit::DataTrackFrameEncoding::Msgpack:
      return "msgpack";
    case livekit::DataTrackFrameEncoding::Json:
      return "json";
    case livekit::DataTrackFrameEncoding::Other:
      return "unknown";
  }

  return "unknown";
}

template <typename ErrorT>
std::string describeDataTrackError(const ErrorT& error) {
  return "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + " message=" + error.message;
}

} // namespace schema_mcap
