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

/// Publishes a camera device through the LiveKit capture-source API.
///
/// The SDK's Rust core owns the platform capture session (AVFoundation on
/// macOS, V4L2 on Linux) and pumps frames straight into the RTC video source,
/// so no frame crosses the C++/Rust FFI boundary.
///
/// A stereo (side-by-side) camera needs no special handling: request its full
/// width and the whole frame is published as one wide track. For a "3D USB
/// Camera" that reports two 1280x720 sensors as one 2560x720 frame:
///
///   camera --device "3D USB Camera" --size 2560x720 --fps 30 --select closest

#include <livekit/capture_source.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "device_capture_example.h"

namespace {

constexpr const char* kExampleName = "device-capture-camera";
constexpr const char* kTrackName = "device-camera";

struct Options {
  std::string device;
  std::string select = "default";
  std::string frame_format = "nv12";
  livekit::CaptureResolution resolution{0, 0};
  std::uint32_t framerate_fps = 0;
  std::vector<std::string> positional;
};

void printUsage(const char* executable, std::ostream& out) {
  out << "Usage: " << executable << " [options] [<ws-url> <token>]\n"
      << "  or set LIVEKIT_URL and LIVEKIT_TOKEN\n\n"
      << "Options:\n"
      << "  --device <query>   Device id, enumeration index, or name substring.\n"
      << "                     Defaults to the platform default device.\n"
      << "  --size <WxH>       Requested frame size, e.g. 2560x720.\n"
      << "  --fps <n>          Requested frame rate.\n"
      << "  --format <name>    Preferred frame format (default nv12). One of\n"
      << "                     i420, nv12, bgra, rgb24, bgr24, yuyv, uyvy, grey, mjpeg.\n"
      << "                     Used by --select exact and closest.\n"
      << "  --select <mode>    Format selection strategy (default 'default'):\n"
      << "                       default      let the device choose\n"
      << "                       exact        require --size and --fps exactly\n"
      << "                       closest      nearest supported to --size and --fps\n"
      << "                       highest-res  largest frame, optionally --fps constrained\n"
      << "                       highest-fps  fastest frame rate, optionally --size constrained\n"
      << "  --list             Print the attached devices and exit.\n"
      << "  -h, --help         Print this message and exit.\n";
}

/// Parses an unsigned decimal that consumes the whole of `value`.
///
/// Rejects a leading sign, embedded or trailing characters, and anything
/// outside [1, limit], so a typo becomes a usage message rather than a
/// silently wrong request.
bool parsePositive(const std::string& value, unsigned long limit, unsigned long* out) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    return false;
  }
  try {
    const unsigned long parsed = std::stoul(value);
    if (parsed == 0 || parsed > limit) {
      return false;
    }
    *out = parsed;
    return true;
  } catch (const std::exception&) {
    // Out of range for unsigned long.
    return false;
  }
}

/// Parses `WxH`, returning false when malformed or non-positive.
bool parseSize(const std::string& value, livekit::CaptureResolution* out) {
  const std::size_t separator = value.find('x');
  if (separator == std::string::npos) {
    return false;
  }
  const auto limit = static_cast<unsigned long>(std::numeric_limits<int>::max());
  unsigned long width = 0;
  unsigned long height = 0;
  if (!parsePositive(value.substr(0, separator), limit, &width) ||
      !parsePositive(value.substr(separator + 1), limit, &height)) {
    return false;
  }
  *out = livekit::CaptureResolution{static_cast<int>(width), static_cast<int>(height)};
  return true;
}

/// Outcome of argument parsing.
enum class ParseResult { Ok, HelpRequested, Invalid };

ParseResult parseOptions(int argc, char* argv[], Options* options, bool* list_only) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--list") {
      *list_only = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      return ParseResult::HelpRequested;
    }
    if (arg.rfind("--", 0) != 0) {
      options->positional.push_back(arg);
      continue;
    }
    if (i + 1 >= argc) {
      std::cerr << "[error] [" << kExampleName << "] " << arg << " requires a value\n";
      return ParseResult::Invalid;
    }
    const std::string value = argv[++i];
    if (arg == "--device") {
      options->device = value;
    } else if (arg == "--select") {
      options->select = value;
    } else if (arg == "--format") {
      options->frame_format = value;
    } else if (arg == "--fps") {
      unsigned long framerate = 0;
      if (!parsePositive(value, std::numeric_limits<std::uint32_t>::max(), &framerate)) {
        std::cerr << "[error] [" << kExampleName << "] --fps expects a positive integer, got '" << value << "'\n";
        return ParseResult::Invalid;
      }
      options->framerate_fps = static_cast<std::uint32_t>(framerate);
    } else if (arg == "--size") {
      if (!parseSize(value, &options->resolution)) {
        std::cerr << "[error] [" << kExampleName << "] --size expects WxH with positive values, got '" << value
                  << "'\n";
        return ParseResult::Invalid;
      }
    } else {
      std::cerr << "[error] [" << kExampleName << "] unknown option '" << arg << "'\n";
      return ParseResult::Invalid;
    }
  }
  return ParseResult::Ok;
}

/// Builds the format request, or returns false after reporting why it cannot.
bool buildFormatRequest(const Options& options, livekit::DeviceFrameFormat frame_format,
                        livekit::DeviceFormatRequest* out) {
  const bool has_size = options.resolution.width > 0 && options.resolution.height > 0;
  const bool has_fps = options.framerate_fps > 0;

  if (options.select == "default") {
    *out = livekit::DeviceFormatRequest{};
    return true;
  }
  if (options.select == "exact" || options.select == "closest") {
    if (!has_size || !has_fps) {
      std::cerr << "[error] [" << kExampleName << "] --select " << options.select
                << " requires both --size and --fps\n";
      return false;
    }
    const livekit::DeviceFormat format{options.resolution, options.framerate_fps, frame_format};
    *out = options.select == "exact" ? livekit::DeviceFormatRequest::exact(format)
                                     : livekit::DeviceFormatRequest::closest(format);
    return true;
  }
  if (options.select == "highest-res") {
    livekit::DeviceFormatRequest::HighestResolutionConstraint constraint;
    if (has_fps) {
      constraint.framerate_fps = options.framerate_fps;
    }
    *out = livekit::DeviceFormatRequest::highestResolution(constraint);
    return true;
  }
  if (options.select == "highest-fps") {
    livekit::DeviceFormatRequest::HighestFramerateConstraint constraint;
    if (has_size) {
      constraint.resolution = options.resolution;
    }
    *out = livekit::DeviceFormatRequest::highestFramerate(constraint);
    return true;
  }

  std::cerr << "[error] [" << kExampleName << "] unknown --select mode '" << options.select << "'\n";
  return false;
}

} // namespace

int main(int argc, char* argv[]) {
  Options options;
  bool list_only = false;
  switch (parseOptions(argc, argv, &options, &list_only)) {
    case ParseResult::Ok:
      break;
    case ParseResult::HelpRequested:
      printUsage(argv[0], std::cout);
      return 0;
    case ParseResult::Invalid:
      printUsage(argv[0], std::cerr);
      return 1;
  }

  livekit::DeviceFrameFormat frame_format = livekit::DeviceFrameFormat::Nv12;
  if (!device_capture_example::parseFrameFormat(options.frame_format, &frame_format)) {
    std::cerr << "[error] [" << kExampleName << "] unknown frame format '" << options.frame_format << "'\n";
    printUsage(argv[0], std::cerr);
    return 1;
  }

  const device_capture_example::LiveKitRuntime runtime;
  try {
    // Enumerate first so a --device query can be reported against the real
    // device list, and so --list needs no credentials.
    const std::vector<livekit::CaptureDeviceInfo> devices = livekit::CaptureSource::listDevices().get();
    if (list_only) {
      device_capture_example::printDevices(devices);
      return 0;
    }

    livekit::DeviceVideoSourceConfig source;
    source.device = device_capture_example::resolveDevice(options.device, devices);
    if (!buildFormatRequest(options, frame_format, &source.format)) {
      return 1;
    }

    std::string url;
    std::string token;
    if (!device_capture_example::readConnection(options.positional, &url, &token)) {
      std::cerr << "[error] [" << kExampleName << "] pass <ws-url> <token>, or set LIVEKIT_URL and LIVEKIT_TOKEN\n";
      printUsage(argv[0], std::cerr);
      return 1;
    }

    return device_capture_example::publishDevice(url, token, kExampleName, kTrackName, std::move(source));
  } catch (const livekit::CaptureSourceError& error) {
    std::cerr << "[error] [" << kExampleName << "] " << error.what() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "[error] [" << kExampleName << "] " << error.what() << '\n';
    return 1;
  }
}
