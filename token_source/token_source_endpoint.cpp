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

// Endpoint token source: fetch credentials from your own backend token endpoint.
//
// This is the recommended pattern for production: API keys stay server-side and
// the SDK POSTs request options (room, identity, ...) to your endpoint, which
// returns the server URL and a freshly minted JWT.
//
// Environment:
//   LIVEKIT_TOKEN_ENDPOINT          Token endpoint URL
//                                   (default http://127.0.0.1:3000/createToken)
//   LIVEKIT_TOKEN_ENDPOINT_METHOD   Optional HTTP method (default POST)
//   LIVEKIT_TOKEN_ENDPOINT_HEADERS  Optional newline-separated "Name: Value" headers,
//                                   e.g. "Authorization: Bearer abc123"

#include <livekit/livekit.h>
#include <livekit/token_source.h>

#include <iostream>
#include <string>
#include <utility>

#include "token_source_common.h"

namespace {

using namespace token_source_example;

bool endpointTokenSourceConnect() {
  std::string endpoint_url = getenvOrEmpty("LIVEKIT_TOKEN_ENDPOINT");
  if (endpoint_url.empty()) {
    endpoint_url = "http://127.0.0.1:3000/createToken";
  }

  auto endpoint_options = endpointOptionsFromEnv();
  std::cout << "Endpoint token source: " << endpoint_options.method << " " << endpoint_url << " ("
            << endpoint_options.headers.size() << " custom header(s))\n";

  auto token_source = livekit::EndpointTokenSource::create(endpoint_url, std::move(endpoint_options));

  // These options are sent to your endpoint, which embeds them into the JWT.
  livekit::TokenRequestOptions request_options;
  request_options.participant_identity = "robot-a";

  livekit::Room room;
  ParticipantLogDelegate delegate;
  room.setDelegate(&delegate);
  if (!room.connect(*token_source, request_options, livekit::RoomOptions())) {
    std::cerr << "Failed to connect to room\n";
    return false;
  }
  std::cout << "Connected to room: " << room.roomInfo().name << " (endpoint token source)\n";

  return runConnectedSession(room);
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Info);
  const bool ok = endpointTokenSourceConnect();
  livekit::shutdown();
  return ok ? 0 : 1;
}
