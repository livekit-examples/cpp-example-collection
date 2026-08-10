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

// Development token source: use LiveKit Cloud's development token server for quick
// local development. Do not use in production.
//
// The token server ID is read from the environment (never hard-coded) so this
// example does not link a personal token server ID into source control.
//
// Environment:
//   LIVEKIT_TOKEN_SERVER_ID Token server identifier from LiveKit Cloud (required)
//   LIVEKIT_AGENT_NAME      Optional registered agent to dispatch into the room
//   LIVEKIT_AGENT_METADATA  Optional metadata passed to the dispatched agent

#include <livekit/livekit.h>
#include <livekit/token_source.h>

#include <iostream>
#include <string>

#include "token_source_common.h"

namespace {

using namespace token_source_example;

bool DevelopmentTokenSourceConnect() {
  std::string token_server_id;
  if (!requireEnv("LIVEKIT_TOKEN_SERVER_ID", token_server_id)) {
    return false;
  }

  // POSTs to cloud-api.livekit.io/api/v2/sandbox/connection-details with
  // X-Sandbox-ID set from LIVEKIT_TOKEN_SERVER_ID.
  auto token_source = livekit::DevelopmentTokenSource::create(token_server_id);

  livekit::TokenRequestOptions request_options;
  request_options.participant_identity = "robot-a";

  // Optional agent dispatch: when LIVEKIT_AGENT_NAME is set, the request embeds
  // room_config.agents so the token server dispatches a named agent.
  if (const std::string agent_name = getenvOrEmpty("LIVEKIT_AGENT_NAME"); !agent_name.empty()) {
    request_options.agent_name = agent_name;
    if (const std::string agent_metadata = getenvOrEmpty("LIVEKIT_AGENT_METADATA"); !agent_metadata.empty()) {
      request_options.agent_metadata = agent_metadata;
    }
    std::cout << "Requesting development token with agent dispatch: agent_name=" << *request_options.agent_name << "\n";
  }
  const auto credentials = token_source->fetch(request_options).get();
  if (!credentials) {
    std::cerr << "Failed to fetch credentials: " << credentials.error().message << "\n";
    return false;
  }

  livekit::Room room;
  ParticipantLogDelegate delegate;
  room.setDelegate(&delegate);
  if (!room.connect(credentials.value().server_url, credentials.value().participant_token, livekit::RoomOptions())) {
    std::cerr << "Failed to connect to room\n";
    return false;
  }
  std::cout << "Connected to room: " << room.roomInfo().name << " (development token source)\n";

  return runConnectedSession(room);
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Info);
  const bool ok = DevelopmentTokenSourceConnect();
  livekit::shutdown();
  return ok ? 0 : 1;
}
