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

// Shared helpers for the token_source examples.
//
// Each example focuses on constructing a single kind of token source; the
// logging delegate, the "connect then observe" session loop, and the small
// environment-variable helpers are factored out here so the per-type files stay
// short and highlight only the token-source-specific code.

#pragma once

#include <livekit/livekit.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace token_source_example {

using namespace std::chrono_literals;

// How frequently to check whether the user has requested shutdown.
constexpr auto kSignalPollPeriod = 50ms;

inline volatile std::sig_atomic_t g_running = 1;
inline std::atomic<bool> g_room_disconnected{false};

inline void handleSignal(int) { g_running = 0; }

inline void installSignalHandlers() {
  g_running = 1;
  g_room_disconnected.store(false, std::memory_order_relaxed);
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif
}

/// Returns the value of an environment variable, or an empty string when unset.
inline std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return (value == nullptr) ? std::string() : std::string(value);
}

/// Reads a required environment variable. Prints an error and returns false
/// when it is unset or empty.
inline bool requireEnv(const char* name, std::string& out) {
  out = getenvOrEmpty(name);
  if (out.empty()) {
    std::cerr << name << " not set\n";
    return false;
  }
  return true;
}

/// Builds the per-fetch request options common to configurable token sources.
///
/// LIVEKIT_ROOM_NAME - optional room name to request from the token server.
inline livekit::TokenRequestOptions tokenRequestOptionsFromEnv() {
  livekit::TokenRequestOptions options;
  options.participant_identity = "robot-a";

  if (const std::string room_name = getenvOrEmpty("LIVEKIT_ROOM_NAME"); !room_name.empty()) {
    options.room_name = room_name;
    std::cout << "Requesting room: " << room_name << "\n";
  }

  return options;
}

/// Trims leading and trailing ASCII whitespace.
inline std::string trimWhitespace(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

/// Renders a participant display name, falling back to "<unset>" when empty.
inline std::string displayName(const std::string& name) { return name.empty() ? "<unset>" : name; }

/// Standard one-line description of a participant (local or remote).
inline std::string formatParticipant(const livekit::Participant& participant) {
  return "identity=" + participant.identity() + ", name=" + displayName(participant.name());
}

inline const char* connectionStateName(livekit::ConnectionState state) {
  switch (state) {
    case livekit::ConnectionState::Disconnected:
      return "Disconnected";
    case livekit::ConnectionState::Connected:
      return "Connected";
    case livekit::ConnectionState::Reconnecting:
      return "Reconnecting";
  }
  return "Unknown";
}

inline const char* connectionQualityName(livekit::ConnectionQuality quality) {
  switch (quality) {
    case livekit::ConnectionQuality::Poor:
      return "Poor";
    case livekit::ConnectionQuality::Good:
      return "Good";
    case livekit::ConnectionQuality::Excellent:
      return "Excellent";
    case livekit::ConnectionQuality::Lost:
      return "Lost";
  }
  return "Unknown";
}

inline const char* disconnectReasonName(livekit::DisconnectReason reason) {
  switch (reason) {
    case livekit::DisconnectReason::Unknown:
      return "Unknown";
    case livekit::DisconnectReason::ClientInitiated:
      return "ClientInitiated";
    case livekit::DisconnectReason::DuplicateIdentity:
      return "DuplicateIdentity";
    case livekit::DisconnectReason::ServerShutdown:
      return "ServerShutdown";
    case livekit::DisconnectReason::ParticipantRemoved:
      return "ParticipantRemoved";
    case livekit::DisconnectReason::RoomDeleted:
      return "RoomDeleted";
    case livekit::DisconnectReason::StateMismatch:
      return "StateMismatch";
    case livekit::DisconnectReason::JoinFailure:
      return "JoinFailure";
    case livekit::DisconnectReason::Migration:
      return "Migration";
    case livekit::DisconnectReason::SignalClose:
      return "SignalClose";
    case livekit::DisconnectReason::RoomClosed:
      return "RoomClosed";
    case livekit::DisconnectReason::UserUnavailable:
      return "UserUnavailable";
    case livekit::DisconnectReason::UserRejected:
      return "UserRejected";
    case livekit::DisconnectReason::SipTrunkFailure:
      return "SipTrunkFailure";
    case livekit::DisconnectReason::ConnectionTimeout:
      return "ConnectionTimeout";
    case livekit::DisconnectReason::MediaFailure:
      return "MediaFailure";
  }
  return "Unknown";
}

inline std::string formatRoomInfo(const livekit::RoomInfoData& info) {
  std::string value = "name=" + displayName(info.name) + ", sid=" + (info.sid ? *info.sid : "<unset>") +
                      ", participants=" + std::to_string(info.num_participants) +
                      ", publishers=" + std::to_string(info.num_publishers);
  if (!info.metadata.empty()) {
    value += ", metadata=" + info.metadata;
  }
  if (info.active_recording) {
    value += ", active_recording=true";
  }
  return value;
}

/// Minimal delegate that logs room lifecycle and participant activity.
class ParticipantLogDelegate : public livekit::RoomDelegate {
public:
  void onParticipantConnected(livekit::Room& /*room*/, const livekit::ParticipantConnectedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }
    std::cout << "Participant connected: " << formatParticipant(*event.participant) << "\n";
  }

  void onParticipantDisconnected(livekit::Room& /*room*/,
                                 const livekit::ParticipantDisconnectedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }
    std::cout << "Participant disconnected: identity=" << event.participant->identity()
              << ", reason=" << disconnectReasonName(event.reason) << "\n";
  }

  void onRoomMetadataChanged(livekit::Room& /*room*/, const livekit::RoomMetadataChangedEvent& event) override {
    std::cout << "Room metadata changed: old=" << displayName(event.old_metadata)
              << ", new=" << displayName(event.new_metadata) << "\n";
  }

  void onRoomSidChanged(livekit::Room& /*room*/, const livekit::RoomSidChangedEvent& event) override {
    std::cout << "Room SID changed: " << displayName(event.sid) << "\n";
  }

  void onRoomUpdated(livekit::Room& /*room*/, const livekit::RoomUpdatedEvent& event) override {
    std::cout << "Room updated: " << formatRoomInfo(event.info) << "\n";
  }

  void onRoomMoved(livekit::Room& /*room*/, const livekit::RoomMovedEvent& event) override {
    std::cout << "Room moved: " << formatRoomInfo(event.info) << "\n";
  }

  void onConnectionQualityChanged(livekit::Room& /*room*/, const livekit::ConnectionQualityChangedEvent& event) override {
    if (event.participant == nullptr) {
      return;
    }
    std::cout << "Connection quality changed: " << formatParticipant(*event.participant)
              << ", quality=" << connectionQualityName(event.quality) << "\n";
  }

  void onConnectionStateChanged(livekit::Room& /*room*/, const livekit::ConnectionStateChangedEvent& event) override {
    std::cout << "Connection state changed: " << connectionStateName(event.state) << "\n";
  }

  void onDisconnected(livekit::Room& /*room*/, const livekit::DisconnectedEvent& event) override {
    std::cout << "Room disconnected: reason=" << disconnectReasonName(event.reason) << "\n";
    g_room_disconnected.store(true, std::memory_order_relaxed);
  }

  void onReconnecting(livekit::Room& /*room*/, const livekit::ReconnectingEvent& /*event*/) override {
    std::cout << "Room reconnecting...\n";
  }

  void onReconnected(livekit::Room& /*room*/, const livekit::ReconnectedEvent& /*event*/) override {
    std::cout << "Room reconnected\n";
  }

  void onTokenRefreshed(livekit::Room& /*room*/, const livekit::TokenRefreshedEvent& event) override {
    std::cout << "Room token refreshed: token_bytes=" << event.token.size() << "\n";
  }

  void onRoomEos(livekit::Room& /*room*/, const livekit::RoomEosEvent& /*event*/) override {
    std::cout << "Room reached end-of-stream\n";
    g_room_disconnected.store(true, std::memory_order_relaxed);
  }

  void onParticipantsUpdated(livekit::Room& /*room*/, const livekit::ParticipantsUpdatedEvent& event) override {
    std::cout << "Participants updated: count=" << event.participants.size() << "\n";
  }
};

inline void logRemoteParticipants(const livekit::Room& room) {
  const auto participants = room.remoteParticipants();
  std::cout << "Remote participants currently in room: " << participants.size() << "\n";
  for (const auto& participant_weak : participants) {
    if (const auto participant = participant_weak.lock()) {
      std::cout << "  - " << formatParticipant(*participant) << "\n";
    }
  }
}

/// Logs local/remote participants, stays connected until interrupted, then
/// disconnects gracefully. Returns false on any failure.
inline bool runConnectedSession(livekit::Room& room) {
  const auto local_participant = room.localParticipant().lock();
  if (!local_participant) {
    std::cerr << "Failed to get local participant\n";
    return false;
  }
  std::cout << "Local participant info: " << formatParticipant(*local_participant) << "\n";

  logRemoteParticipants(room);

  installSignalHandlers();
  std::cout << "Waiting for participant activity. Press Ctrl-C to disconnect...\n";
  while (g_running != 0 && !g_room_disconnected.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(kSignalPollPeriod);
  }

  logRemoteParticipants(room);

  if (g_room_disconnected.load(std::memory_order_relaxed)) {
    std::cout << "Room already disconnected\n";
    return true;
  }

  if (!room.disconnect()) {
    std::cerr << "Failed to gracefully disconnect from room\n";
    return false;
  }

  std::cout << "Disconnected from room\n";
  return true;
}

/// Parses HTTP transport options for EndpointTokenSource from the environment.
///
/// LIVEKIT_TOKEN_ENDPOINT_METHOD  - optional HTTP method (default POST).
/// LIVEKIT_TOKEN_ENDPOINT_HEADERS - optional newline-separated "Name: Value"
///                                  pairs, e.g. "Authorization: Bearer ...".
inline livekit::TokenEndpointOptions endpointOptionsFromEnv() {
  livekit::TokenEndpointOptions options;

  if (const std::string method = getenvOrEmpty("LIVEKIT_TOKEN_ENDPOINT_METHOD"); !method.empty()) {
    options.method = method;
  }

  const std::string headers_text = getenvOrEmpty("LIVEKIT_TOKEN_ENDPOINT_HEADERS");
  if (headers_text.empty()) {
    return options;
  }

  std::size_t start = 0;
  while (start <= headers_text.size()) {
    const std::size_t newline = headers_text.find('\n', start);
    const std::string line =
        headers_text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
      const std::string name = trimWhitespace(line.substr(0, colon));
      const std::string value = trimWhitespace(line.substr(colon + 1));
      if (!name.empty()) {
        options.headers[name] = value;
      }
    }
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }

  return options;
}

} // namespace token_source_example
