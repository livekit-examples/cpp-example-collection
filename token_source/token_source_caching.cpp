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

// Caching token source: a decorator that adds JWT-aware caching to any
// configurable token source (endpoint, development, or custom).
//
// Repeated fetches for the same request options reuse the cached token until it
// nears expiry or you call invalidate() to drop the cached credentials, cutting
// down calls to your backend. This example wraps an EndpointTokenSource.
//
// Environment:
//   LIVEKIT_TOKEN_ENDPOINT          Token endpoint URL
//                                   (default http://127.0.0.1:3000/createToken)
//   LIVEKIT_TOKEN_ENDPOINT_METHOD   Optional HTTP method (default POST)
//   LIVEKIT_TOKEN_ENDPOINT_HEADERS  Optional newline-separated "Name: Value" headers

#include <livekit/livekit.h>
#include <livekit/token_source.h>

#include <iostream>
#include <string>
#include <utility>

#include "token_source_common.h"

namespace {

using namespace token_source_example;

bool cachingTokenSourceConnect() {
  std::string endpoint_url = getenvOrEmpty("LIVEKIT_TOKEN_ENDPOINT");
  if (endpoint_url.empty()) {
    endpoint_url = "http://127.0.0.1:3000/createToken";
  }

  std::cout << "Caching token source wrapping endpoint: " << endpoint_url << "\n";

  // Build the inner source, then wrap it. CachingTokenSource::create takes
  // ownership of the inner source via unique_ptr.
  auto inner = livekit::EndpointTokenSource::create(endpoint_url, endpointOptionsFromEnv());
  auto token_source = livekit::CachingTokenSource::create(std::move(inner));

  livekit::TokenRequestOptions request_options;
  request_options.participant_identity = "robot-a";
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
  std::cout << "Connected to room: " << room.roomInfo().name << " (caching token source)\n";

  return runConnectedSession(room);
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Info);
  const bool ok = cachingTokenSourceConnect();
  livekit::shutdown();
  return ok ? 0 : 1;
}
