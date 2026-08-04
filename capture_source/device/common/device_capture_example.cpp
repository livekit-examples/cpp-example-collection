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

#include "device_capture_example.h"

#include <livekit/livekit.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace device_capture_example {
namespace {

using namespace std::chrono_literals;

std::atomic<bool> g_running{true};

void handleSignal(int /*signal*/) { g_running.store(false); }

std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string{value};
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

/// True when `query` is a run of decimal digits, i.e. an enumeration index.
bool isIndex(const std::string& query) {
  return !query.empty() && std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c); });
}

class TerminalResult {
public:
  void set(const livekit::CaptureResult& result) {
    {
      const std::scoped_lock lock(mutex_);
      result_ = result;
    }
    condition_.notify_all();
    g_running.store(false);
  }

  bool ready() const {
    const std::scoped_lock lock(mutex_);
    return result_.has_value();
  }

  std::optional<livekit::CaptureResult> waitFor(std::chrono::seconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait_for(lock, timeout, [this] { return result_.has_value(); });
    return result_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::optional<livekit::CaptureResult> result_;
};

const char* captureExitName(livekit::CaptureExit exit) {
  return exit == livekit::CaptureExit::EndOfStream ? "end of stream" : "stopped";
}

} // namespace

LiveKitRuntime::LiveKitRuntime() { livekit::initialize(livekit::LogLevel::Info); }
LiveKitRuntime::~LiveKitRuntime() { livekit::shutdown(); }

const char* frameFormatName(livekit::DeviceFrameFormat format) {
  switch (format) {
    case livekit::DeviceFrameFormat::I420:
      return "i420";
    case livekit::DeviceFrameFormat::Nv12:
      return "nv12";
    case livekit::DeviceFrameFormat::Bgra:
      return "bgra";
    case livekit::DeviceFrameFormat::Rgb24:
      return "rgb24";
    case livekit::DeviceFrameFormat::Bgr24:
      return "bgr24";
    case livekit::DeviceFrameFormat::Yuyv:
      return "yuyv";
    case livekit::DeviceFrameFormat::Uyvy:
      return "uyvy";
    case livekit::DeviceFrameFormat::Grey:
      return "grey";
    case livekit::DeviceFrameFormat::Mjpeg:
      return "mjpeg";
  }
  return "unknown";
}

bool parseFrameFormat(const std::string& name, livekit::DeviceFrameFormat* out) {
  using livekit::DeviceFrameFormat;
  const std::string lowered = toLower(name);
  for (const DeviceFrameFormat candidate :
       {DeviceFrameFormat::I420, DeviceFrameFormat::Nv12, DeviceFrameFormat::Bgra, DeviceFrameFormat::Rgb24,
        DeviceFrameFormat::Bgr24, DeviceFrameFormat::Yuyv, DeviceFrameFormat::Uyvy, DeviceFrameFormat::Grey,
        DeviceFrameFormat::Mjpeg}) {
    if (lowered == frameFormatName(candidate)) {
      *out = candidate;
      return true;
    }
  }
  return false;
}

livekit::DeviceSelector resolveDevice(const std::string& query,
                                      const std::vector<livekit::CaptureDeviceInfo>& devices) {
  if (query.empty()) {
    return livekit::DeviceSelector{};
  }

  // An exact id match wins: ids are what listDevices reports and what a
  // configuration should pin to.
  for (const livekit::CaptureDeviceInfo& device : devices) {
    if (device.id == query) {
      return livekit::DeviceSelector::id(device.id);
    }
  }

  if (isIndex(query)) {
    // Range-check before narrowing, so an over-long digit string reports the
    // query rather than throwing a bare std::stoul message or wrapping around.
    unsigned long long index = 0;
    try {
      index = std::stoull(query);
    } catch (const std::exception&) {
      throw livekit::CaptureSourceError("device index '" + query + "' is out of range");
    }
    if (index > std::numeric_limits<std::uint32_t>::max()) {
      throw livekit::CaptureSourceError("device index '" + query + "' is out of range");
    }
    return livekit::DeviceSelector::index(static_cast<std::uint32_t>(index));
  }

  // Otherwise treat the query as a name substring, and resolve it to an id so
  // the selection survives re-enumeration.
  const std::string needle = toLower(query);
  const livekit::CaptureDeviceInfo* match = nullptr;
  for (const livekit::CaptureDeviceInfo& device : devices) {
    if (toLower(device.name).find(needle) == std::string::npos) {
      continue;
    }
    if (match != nullptr) {
      throw livekit::CaptureSourceError("device name '" + query + "' matches both '" + match->name + "' and '" +
                                        device.name + "'; use an id instead");
    }
    match = &device;
  }
  if (match == nullptr) {
    throw livekit::CaptureSourceError("no capture device matches '" + query + "'");
  }
  return livekit::DeviceSelector::id(match->id);
}

void printDevices(const std::vector<livekit::CaptureDeviceInfo>& devices) {
  if (devices.empty()) {
    std::cout << "No capture devices found.\n";
    return;
  }

  for (const livekit::CaptureDeviceInfo& device : devices) {
    std::cout << "- id: " << device.id << '\n';
    std::cout << "  name: " << device.name << '\n';
    if (device.model_id) {
      std::cout << "  model_id: " << *device.model_id << '\n';
    }
    if (device.manufacturer) {
      std::cout << "  manufacturer: " << *device.manufacturer << '\n';
    }
    if (device.formats.empty()) {
      // Not every backend enumerates formats; AVFoundation does not.
      std::cout << "  formats: " << (device.formats_complete ? "none reported" : "not enumerated on this platform")
                << '\n';
      continue;
    }
    std::cout << "  formats:" << (device.formats_complete ? "" : " (partial)") << '\n';
    for (const livekit::DeviceFormat& format : device.formats) {
      std::cout << "    - " << format.resolution.width << 'x' << format.resolution.height << " @ "
                << format.framerate_fps << "fps " << frameFormatName(format.frame_format) << '\n';
    }
  }
}

bool readConnection(const std::vector<std::string>& positional, std::string* url, std::string* token) {
  if (positional.size() == 2) {
    *url = positional[0];
    *token = positional[1];
  } else if (positional.empty()) {
    *url = getenvOrEmpty("LIVEKIT_URL");
    *token = getenvOrEmpty("LIVEKIT_TOKEN");
  } else {
    return false;
  }
  return !url->empty() && !token->empty();
}

int publishDevice(const std::string& url, const std::string& token, const std::string& example_name,
                  const std::string& track_name, livekit::DeviceVideoSourceConfig source) {
  g_running.store(true);
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  try {
    livekit::Room room;
    livekit::RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;
    if (!room.connect(url, token, room_options)) {
      std::cerr << "[error] [" << example_name << "] Failed to connect\n";
      return 1;
    }

    auto local_participant = room.localParticipant().lock();
    if (!local_participant) {
      throw std::runtime_error("unable to lock local participant");
    }
    std::cout << "[info] [" << example_name << "] Connected as identity='" << local_participant->identity()
              << "' room='" << room.roomInfo().name << "'\n";

    TerminalResult terminal_result;
    // Construction opens the device and negotiates the format, so the
    // resolution below is what the camera actually delivers.
    auto capture = livekit::CaptureSource::create(std::move(source)).get();
    std::cout << "[info] [" << example_name << "] Device capture ready at " << capture->width() << 'x'
              << capture->height() << '\n';

    auto track = livekit::LocalVideoTrack::createLocalVideoTrack(track_name, capture->videoSource());
    livekit::TrackPublishOptions app_options;
    app_options.source = livekit::TrackSource::SOURCE_CAMERA;
    local_participant->publishTrack(track, capture->publishOptions(app_options));
    local_participant.reset();

    capture->setOnFinishedCallback(
        [&terminal_result](const livekit::CaptureResult& result) { terminal_result.set(result); });
    capture->start();

    std::cout << "[info] [" << example_name << "] Publishing track '" << track_name << "'; Ctrl-C to stop\n";
    while (g_running.load()) {
      std::this_thread::sleep_for(50ms);
    }

    if (!terminal_result.ready()) {
      capture->stop();
    }

    const auto result = terminal_result.waitFor(10s);
    if (!result) {
      std::cerr << "[error] [" << example_name << "] Timed out waiting for capture to stop\n";
      return 1;
    }

    if (auto participant = room.localParticipant().lock()) {
      if (const auto publication = track->publication()) {
        participant->unpublishTrack(publication->sid());
      }
    }

    if (result->error) {
      std::cerr << "[error] [" << example_name << "] Capture failed: " << *result->error << '\n';
      return 1;
    }

    std::cout << "[info] [" << example_name << "] Capture " << captureExitName(result->exit) << " after "
              << result->frames_captured << " frames\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[error] [" << example_name << "] " << error.what() << '\n';
    return 1;
  }
}

} // namespace device_capture_example
