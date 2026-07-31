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

#include "gstreamer_capture_example.h"

#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gstreamer_capture_example {
namespace {

using namespace std::chrono_literals;

std::atomic<bool> g_running{true};

void handleSignal(int /*signal*/) { g_running.store(false); }

std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string{value};
}

class LiveKitRuntime {
public:
  LiveKitRuntime() { livekit::initialize(livekit::LogLevel::Info); }
  ~LiveKitRuntime() { livekit::shutdown(); }

  LiveKitRuntime(const LiveKitRuntime&) = delete;
  LiveKitRuntime& operator=(const LiveKitRuntime&) = delete;
};

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

void printUsage(const char* executable) {
  std::cerr << "Usage: " << executable << " <ws-url> <token>\n"
            << "  or set LIVEKIT_URL and LIVEKIT_TOKEN\n";
}

const char* captureExitName(livekit::CaptureExit exit) {
  return exit == livekit::CaptureExit::EndOfStream ? "end of stream" : "stopped";
}

} // namespace

int run(int argc, char* argv[], GStreamerExampleConfig config) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string token = getenvOrEmpty("LIVEKIT_TOKEN");
  if (argc == 3) {
    url = argv[1];
    token = argv[2];
  } else if (argc != 1) {
    printUsage(argv[0]);
    return 1;
  }

  if (url.empty() || token.empty()) {
    printUsage(argv[0]);
    return 1;
  }

  g_running.store(true);
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  const LiveKitRuntime runtime;
  try {
    livekit::Room room;
    livekit::RoomOptions room_options;
    room_options.auto_subscribe = true;
    room_options.dynacast = false;
    if (!room.connect(url, token, room_options)) {
      std::cerr << "[error] [" << config.name << "] Failed to connect\n";
      return 1;
    }

    auto local_participant = room.localParticipant().lock();
    if (!local_participant) {
      throw std::runtime_error("unable to lock local participant");
    }
    std::cout << "[info] [" << config.name << "] Connected as identity='" << local_participant->identity() << "' room='"
              << room.roomInfo().name << "'\n";

    TerminalResult terminal_result;
    auto capture = livekit::CaptureSource::create(std::move(config.source)).get();
    std::cout << "[info] [" << config.name << "] GStreamer capture ready at " << capture->width() << 'x'
              << capture->height() << "\n";

    auto track = livekit::LocalVideoTrack::createLocalVideoTrack(config.track_name, capture->videoSource());
    livekit::TrackPublishOptions app_options;
    app_options.source = livekit::TrackSource::SOURCE_CAMERA;
    local_participant->publishTrack(track, capture->publishOptions(app_options));
    local_participant.reset();

    capture->setOnFinishedCallback(
        [&terminal_result](const livekit::CaptureResult& result) { terminal_result.set(result); });
    capture->start();

    std::cout << "[info] [" << config.name << "] Publishing track '" << config.track_name << "'; Ctrl-C to stop\n";
    while (g_running.load()) {
      std::this_thread::sleep_for(50ms);
    }

    if (!terminal_result.ready()) {
      capture->stop();
    }

    const auto result = terminal_result.waitFor(10s);
    if (!result) {
      std::cerr << "[error] [" << config.name << "] Timed out waiting for capture to stop\n";
      return 1;
    }

    if (auto participant = room.localParticipant().lock()) {
      if (const auto publication = track->publication()) {
        participant->unpublishTrack(publication->sid());
      }
    }

    if (result->error) {
      std::cerr << "[error] [" << config.name << "] Capture failed: " << *result->error << '\n';
      return 1;
    }

    std::cout << "[info] [" << config.name << "] Capture " << captureExitName(result->exit) << " after "
              << result->frames_captured << " frames\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "[error] [" << config.name << "] " << error.what() << '\n';
    return 1;
  }
}

} // namespace gstreamer_capture_example
