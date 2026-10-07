/*
 * Copyright 2026 LiveKit, Inc.
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

#include <livekit/capture_source.h>

#include <string>
#include <vector>

namespace device_capture_example {

/// Owns SDK initialization for the lifetime of the scope.
class LiveKitRuntime {
public:
  LiveKitRuntime();
  ~LiveKitRuntime();

  LiveKitRuntime(const LiveKitRuntime&) = delete;
  LiveKitRuntime& operator=(const LiveKitRuntime&) = delete;
};

/// Name of a frame format as it appears in configuration and logs.
const char* frameFormatName(livekit::DeviceFrameFormat format);

/// Parses a frame format name, or returns false when unrecognized.
bool parseFrameFormat(const std::string& name, livekit::DeviceFrameFormat* out);

/// Resolves a device query to a selector.
///
/// A query is a device id, a decimal enumeration index, or a case-insensitive
/// substring of a device name. An empty query selects the platform default
/// device. Throws @ref livekit::CaptureSourceError when a name query matches no
/// device or more than one.
livekit::DeviceSelector resolveDevice(const std::string& query, const std::vector<livekit::CaptureDeviceInfo>& devices);

/// Prints every attached capture device to stdout.
void printDevices(const std::vector<livekit::CaptureDeviceInfo>& devices);

/// Connects, publishes the device as a camera track, and runs until the
/// capture ends or the process is interrupted.
///
/// @return A process exit status.
int publishDevice(const std::string& url, const std::string& token, const std::string& example_name,
                  const std::string& track_name, livekit::DeviceVideoSourceConfig source);

/// Reads the connection URL and token from `argv` then the environment.
///
/// Accepts a trailing `<ws-url> <token>` pair, otherwise falls back to
/// `LIVEKIT_URL` and `LIVEKIT_TOKEN`. Returns false when neither supplies both.
bool readConnection(const std::vector<std::string>& positional, std::string* url, std::string* token);

} // namespace device_capture_example
