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

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "livekit/livekit.h"
#include "sdl_video_renderer.h"

using namespace livekit;

namespace {

constexpr const char *kDefaultTrackName = "encoded-h264";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

class MainThreadDispatcher {
public:
  static void dispatch(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(fn));
  }

  static void update() {
    std::queue<std::function<void()>> local;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::swap(local, queue_);
    }

    while (!local.empty()) {
      local.front()();
      local.pop();
    }
  }

private:
  static inline std::mutex mutex_;
  static inline std::queue<std::function<void()>> queue_;
};

struct Args {
  std::string url;
  std::string token;
  std::string from_identity = "encoded-sender";
  std::string track_name = kDefaultTrackName;
  int window_width = 640;
  int window_height = 480;
};

void printUsage(const char *program) {
  std::cerr << "Usage:\n"
            << "  " << program << " <ws-url> <token> [flags]\n"
            << "or:\n"
            << "  LIVEKIT_URL=... LIVEKIT_TOKEN=... " << program
            << " [flags]\n\n"
            << "Flags:\n"
            << "  --from <identity>       default: encoded-sender\n"
            << "  --track-name <name>     default: " << kDefaultTrackName
            << "\n"
            << "  --width <px>            initial window width, default: 640\n"
            << "  --height <px>           initial window height, default: 480\n";
}

std::string takeValue(int &index, int argc, char *argv[]) {
  if (index + 1 >= argc) {
    throw std::invalid_argument(std::string("missing value for ") +
                                argv[index]);
  }
  ++index;
  return argv[index];
}

bool parseArgs(int argc, char *argv[], Args &args) {
  args.url = getenvOrEmpty("LIVEKIT_URL");
  args.token = getenvOrEmpty("LIVEKIT_TOKEN");
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return false;
    }
    if (arg == "--from") {
      args.from_identity = takeValue(i, argc, argv);
    } else if (arg == "--track-name") {
      args.track_name = takeValue(i, argc, argv);
    } else if (arg == "--width") {
      args.window_width = std::stoi(takeValue(i, argc, argv));
    } else if (arg == "--height") {
      args.window_height = std::stoi(takeValue(i, argc, argv));
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.size() >= 2) {
    args.url = positional[0];
    args.token = positional[1];
  }

  return !(args.url.empty() || args.token.empty());
}

class ConsumerDelegate : public RoomDelegate {
public:
  ConsumerDelegate(const Args &args, SDLVideoRenderer &renderer)
      : args_(args), renderer_(renderer) {}

  void onTrackSubscribed(Room &, const TrackSubscribedEvent &event) override {
    if (!event.track || !event.participant || !event.publication) {
      return;
    }
    if (event.track->kind() != TrackKind::KIND_VIDEO) {
      return;
    }
    if (event.participant->identity() != args_.from_identity ||
        event.publication->name() != args_.track_name) {
      return;
    }

    VideoStream::Options options;
    options.capacity = 1;
    options.format = VideoBufferType::I420;
    auto stream = VideoStream::fromTrack(event.track, options);
    if (!stream) {
      std::cerr << "[consumer] failed to create video stream for "
                << args_.from_identity << " track=\"" << args_.track_name
                << "\"\n";
      return;
    }

    MainThreadDispatcher::dispatch([this, stream] {
      renderer_.setStream(stream);
      std::cout << "[consumer] rendering " << args_.from_identity
                << " track=\"" << args_.track_name << "\"\n";
    });
  }

  void onTrackUnsubscribed(Room &, const TrackUnsubscribedEvent &event) override {
    if (!event.participant || !event.publication) {
      return;
    }
    if (event.participant->identity() != args_.from_identity ||
        event.publication->name() != args_.track_name) {
      return;
    }

    MainThreadDispatcher::dispatch([this] {
      renderer_.setStream(nullptr);
      std::cout << "[consumer] stopped rendering " << args_.from_identity
                << " track=\"" << args_.track_name << "\"\n";
    });
  }

private:
  const Args &args_;
  SDLVideoRenderer &renderer_;
};

} // namespace

int main(int argc, char *argv[]) {
  Args args;
  try {
    if (!parseArgs(argc, argv, args)) {
      printUsage(argv[0]);
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "[consumer] argument error: " << error.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "[consumer] SDL_Init(SDL_INIT_VIDEO) failed: "
              << SDL_GetError() << "\n";
    return 1;
  }

  SDLVideoRenderer renderer;
  if (!renderer.init("LiveKit Encoded Ingest", args.window_width,
                     args.window_height)) {
    SDL_Quit();
    return 1;
  }

  livekit::initialize(livekit::LogLevel::Info, livekit::LogSink::kConsole);
  int exit_code = 0;

  {
    Room room;
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    ConsumerDelegate delegate(args, renderer);
    room.setDelegate(&delegate);

    std::cout << "[consumer] connecting to " << args.url << "\n";
    if (!room.Connect(args.url, args.token, options)) {
      std::cerr << "[consumer] failed to connect\n";
      exit_code = 1;
    } else {
      std::cout << "[consumer] connected as "
                << room.localParticipant()->identity() << " to room '"
                << room.room_info().name << "'\n";

      while (g_running.load(std::memory_order_relaxed)) {
        MainThreadDispatcher::update();
        renderer.render();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }

  renderer.shutdown();
  livekit::shutdown();
  SDL_Quit();
  return exit_code;
}
