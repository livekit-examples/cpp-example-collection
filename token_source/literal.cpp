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

// Literal token source: connect with a server URL and JWT you already have.
//
// Use this when your app mints or fetches tokens out of band (e.g. with
// `lk token create`) and just wants the SDK to consume them as-is. The room,
// identity, and grants all come from the token itself.
//
// Environment:
//   LIVEKIT_URL    WebSocket URL of the LiveKit server (e.g. ws://localhost:7880)
//   LIVEKIT_TOKEN  Participant JWT

#include <livekit/livekit.h>
#include <livekit/token_source.h>

#include <iostream>
#include <string>

#include "common.h"

namespace {

using namespace token_source_example;

bool literalTokenSourceConnect() {
  std::string url;
  std::string token;
  if (!requireEnv("LIVEKIT_URL", url) || !requireEnv("LIVEKIT_TOKEN", token)) {
    return false;
  }

  // Each fetch() returns these exact credentials; nothing is requested over the
  // network. Room and participant identity are encoded in the token.
  auto token_source = livekit::LiteralTokenSource::fromValue(url, token);

  livekit::Room room;
  ParticipantLogDelegate delegate;
  room.setDelegate(&delegate);
  if (!room.connect(*token_source, livekit::RoomOptions())) {
    std::cerr << "Failed to connect to room\n";
    return false;
  }
  std::cout << "Connected to room: " << room.roomInfo().name << " (literal token source)\n";

  return runConnectedSession(room);
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Info);
  const bool ok = literalTokenSourceConnect();
  livekit::shutdown();
  return ok ? 0 : 1;
}
