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

// Custom token source: plug your own async token-fetching logic into the SDK.
//
// Use this when you already have an internal auth/token system and want to
// integrate it without adopting the standardized token-endpoint format. Your
// callback receives the per-call TokenRequestOptions and returns a future of
// credentials.
//
// This example's callback simulates a private backend by reading credentials
// from the environment, but the body could call any internal service.
//
// Environment:
//   LIVEKIT_URL    WebSocket URL of the LiveKit server
//   LIVEKIT_TOKEN  Participant JWT

#include <livekit/livekit.h>
#include <livekit/result.h>
#include <livekit/token_source.h>

#include <future>
#include <iostream>
#include <string>

#include "token_source_common.h"

namespace {

using namespace token_source_example;

using TokenResult = livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>;

/// Wraps an already-computed value in a ready std::future.
std::future<TokenResult> makeReadyFuture(TokenResult result) {
  std::promise<TokenResult> promise;
  promise.set_value(std::move(result));
  return promise.get_future();
}

bool customTokenSourceConnect() {
  // The callback is invoked for each fetch with the request options. Here we
  // pretend to call an internal service; in a real app this is where you would
  // talk to your own auth backend.
  auto token_source = livekit::CustomTokenSource::fromCallback(
      [](const livekit::TokenRequestOptions& options) -> std::future<TokenResult> {
        (void)options; // A real backend would honor these (room, identity, ...).

        const std::string url = getenvOrEmpty("LIVEKIT_URL");
        const std::string token = getenvOrEmpty("LIVEKIT_TOKEN");
        if (url.empty() || token.empty()) {
          return makeReadyFuture(TokenResult::failure(
              livekit::TokenSourceError{"LIVEKIT_URL and LIVEKIT_TOKEN must be set for the custom example"}));
        }

        livekit::TokenSourceResponse response;
        response.server_url = url;
        response.participant_token = token;
        return makeReadyFuture(TokenResult::success(std::move(response)));
      });

  livekit::Room room;
  ParticipantLogDelegate delegate;
  room.setDelegate(&delegate);
  if (!room.connect(*token_source, livekit::TokenRequestOptions(), livekit::RoomOptions())) {
    std::cerr << "Failed to connect to room\n";
    return false;
  }
  std::cout << "Connected to room: " << room.roomInfo().name << " (custom token source)\n";

  return runConnectedSession(room);
}

} // namespace

int main() {
  livekit::initialize(livekit::LogLevel::Info);
  const bool ok = customTokenSourceConnect();
  livekit::shutdown();
  return ok ? 0 : 1;
}
