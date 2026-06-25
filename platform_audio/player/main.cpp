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

/// Plays subscribed room audio using PlatformAudio.
///
/// Usage:
///   PlatformAudioPlayer <ws-url> <player-token>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_PLAYER_TOKEN

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "livekit/livekit.h"

using namespace livekit;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

void printUsage() {
  std::cerr << "[error] Usage: PlatformAudioPlayer <ws-url> <player-token>\n"
            << "  or set LIVEKIT_URL and LIVEKIT_PLAYER_TOKEN\n";
}

class PlayerDelegate final : public RoomDelegate {
public:
  void onParticipantConnected(Room&, const ParticipantConnectedEvent& event) override {
    if (event.participant) {
      std::cout << "[info] [platform-audio-player] Participant connected identity='" << event.participant->identity()
                << "'\n";
    }
  }

  void onTrackSubscribed(Room&, const TrackSubscribedEvent& event) override {
    if (!event.track || event.track->kind() != TrackKind::KIND_AUDIO) {
      return;
    }

    const std::string participant_identity = event.participant ? event.participant->identity() : std::string("unknown");
    const std::string publication_name = event.publication ? event.publication->name() : event.track->name();
    std::cout << "[info] [platform-audio-player] Playing audio track '" << publication_name
              << "' from participant identity='" << participant_identity << "'\n";
  }

  void onTrackSubscriptionFailed(Room&, const TrackSubscriptionFailedEvent& event) override {
    const std::string participant_identity = event.participant ? event.participant->identity() : std::string("unknown");
    std::cerr << "[warn] [platform-audio-player] Audio subscription failed for participant identity='"
              << participant_identity << "' track_sid='" << event.track_sid << "'\n";
  }
};

} // namespace

int main(int argc, char* argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string player_token = getenvOrEmpty("LIVEKIT_PLAYER_TOKEN");

  if (argc >= 3) {
    url = argv[1];
    player_token = argv[2];
  }

  if (url.empty() || player_token.empty()) {
    printUsage();
    return 1;
  }

  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif

  livekit::initialize(livekit::LogLevel::Info);

  try {
    PlatformAudio platform_audio;

    auto playout_devices = platform_audio.playoutDevices();
    std::cout << "[info] [platform-audio-player] Playout devices: " << playout_devices.size() << "\n";
    for (const auto& device : playout_devices) {
      std::cout << "  [" << device.index << "] " << device.name << " id=" << device.id << "\n";
    }

    auto room = std::make_unique<Room>();
    PlayerDelegate delegate;
    room->setDelegate(&delegate);

    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    if (!room->connect(url, player_token, options)) {
      std::cerr << "[error] [platform-audio-player] Failed to connect\n";
      room.reset();
      livekit::shutdown();
      return 1;
    }

    if (auto local_participant = room->localParticipant().lock()) {
      std::cout << "[info] [platform-audio-player] Connected as identity='" << local_participant->identity()
                << "' room='" << room->roomInfo().name << "'\n";
    } else {
      throw std::runtime_error("unable to lock local participant");
    }

    std::cout << "[info] [platform-audio-player] Waiting for remote audio; Ctrl-C to exit\n";
    while (g_running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "[info] [platform-audio-player] Disconnecting\n";
    room->setDelegate(nullptr);
    room.reset();
  } catch (const std::exception& error) {
    std::cerr << "[error] [platform-audio-player] " << error.what() << "\n";
    livekit::shutdown();
    return 1;
  }

  livekit::shutdown();
  return 0;
}
