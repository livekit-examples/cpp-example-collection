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

/// Publishes microphone audio using PlatformAudio.
///
/// Usage:
///   PlatformAudioSender <ws-url> <sender-token>
///
/// Or via environment variables:
///   LIVEKIT_URL, LIVEKIT_SENDER_TOKEN

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

constexpr const char* kAudioTrackName = "platform-microphone";

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false); }

std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

void printUsage() {
  std::cerr << "[error] Usage: PlatformAudioSender <ws-url> <sender-token>\n"
            << "  or set LIVEKIT_URL and LIVEKIT_SENDER_TOKEN\n";
}

} // namespace

int main(int argc, char* argv[]) {
  std::string url = getenvOrEmpty("LIVEKIT_URL");
  std::string sender_token = getenvOrEmpty("LIVEKIT_SENDER_TOKEN");

  if (argc >= 3) {
    url = argv[1];
    sender_token = argv[2];
  }

  if (url.empty() || sender_token.empty()) {
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

    auto recording_devices = platform_audio.recordingDevices();
    std::cout << "[info] [platform-audio-sender] Recording devices: " << recording_devices.size() << "\n";
    for (const auto& device : recording_devices) {
      std::cout << "  [" << device.index << "] " << device.name << " id=" << device.id << "\n";
    }

    auto room = std::make_unique<Room>();
    RoomOptions options;
    options.auto_subscribe = true;
    options.dynacast = false;

    if (!room->connect(url, sender_token, options)) {
      std::cerr << "[error] [platform-audio-sender] Failed to connect\n";
      room.reset();
      livekit::shutdown();
      return 1;
    }

    auto local_participant = room->localParticipant().lock();
    if (!local_participant) {
      throw std::runtime_error("unable to lock local participant");
    }

    std::cout << "[info] [platform-audio-sender] Connected as identity='" << local_participant->identity() << "' room='"
              << room->roomInfo().name << "'\n";

    livekit::PlatformAudioOptions audio_options;
    audio_options.echo_cancellation = true;
    audio_options.noise_suppression = true;
    audio_options.auto_gain_control = true;

    auto audio_source = platform_audio.createAudioSource(audio_options);
    auto audio_track = LocalAudioTrack::createLocalAudioTrack(kAudioTrackName, audio_source);

    TrackPublishOptions publish_options;
    publish_options.source = TrackSource::SOURCE_MICROPHONE;

    local_participant->publishTrack(audio_track, publish_options);
    local_participant.reset();
    auto publication = audio_track->publication();
    std::cout << "[info] [platform-audio-sender] Published microphone track";
    if (publication) {
      std::cout << " sid=" << publication->sid();
    }
    std::cout << "\n";

    std::cout << "[info] [platform-audio-sender] Sending microphone audio; Ctrl-C to exit\n";
    while (g_running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "[info] [platform-audio-sender] Disconnecting\n";
    room.reset();
  } catch (const std::exception& error) {
    std::cerr << "[error] [platform-audio-sender] " << error.what() << "\n";
    livekit::shutdown();
    return 1;
  }

  livekit::shutdown();
  return 0;
}
