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

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace token_source_example {

using namespace std::chrono_literals;

// How long to stay connected so participant join/leave events can be observed.
constexpr auto kObserveDuration = 5s;

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

/// Minimal delegate that logs remote participant join/leave activity.
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
    std::cout << "Participant disconnected: identity=" << event.participant->identity() << "\n";
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

/// Logs local/remote participants, stays connected briefly so join/leave events
/// surface, then disconnects gracefully. Returns false on any failure.
inline bool runConnectedSession(livekit::Room& room) {
  const auto local_participant = room.localParticipant().lock();
  if (!local_participant) {
    std::cerr << "Failed to get local participant\n";
    return false;
  }
  std::cout << "Local participant info: " << formatParticipant(*local_participant) << "\n";

  logRemoteParticipants(room);

  // Stay connected briefly so participant join/leave events are surfaced.
  std::this_thread::sleep_for(kObserveDuration);
  logRemoteParticipants(room);

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
