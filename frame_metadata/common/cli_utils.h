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

#pragma once

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace frame_metadata {

inline constexpr char kDefaultLiveKitUrl[] = "ws://localhost:7880";

enum class ParseResult { Ok, Help, Error };

struct CliOptions {
  std::string url;
  std::string token;
};

inline std::atomic<bool> g_running{true};

inline void handleSignal(int) { g_running.store(false); }

inline bool isRunning() { return g_running.load(std::memory_order_relaxed); }

inline void installSignalHandlers() {
  std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
  std::signal(SIGTERM, handleSignal);
#endif
}

inline std::string getenvOrEmpty(const char* name) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string{};
}

inline void printUsage(const char* program) {
  std::cerr << "Usage:\n"
            << "  " << program << " [<ws-url> <token>]\n"
            << "\n"
            << "Environment:\n"
            << "  LIVEKIT_URL    defaults to " << kDefaultLiveKitUrl << " when unset\n"
            << "  LIVEKIT_TOKEN  required unless passed as the second argument\n"
            << "\n"
            << "Example:\n"
            << "  export LIVEKIT_TOKEN=<token>\n"
            << "  " << program << "\n";
}

inline ParseResult parseArgs(int argc, char* argv[], CliOptions& options) {
  std::vector<std::string> positional;
  options = CliOptions{};

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return ParseResult::Help;
    }
    if (!arg.empty() && arg[0] == '-') {
      return ParseResult::Error;
    }

    positional.push_back(arg);
  }

  if (positional.size() > 2) {
    return ParseResult::Error;
  }

  options.url = getenvOrEmpty("LIVEKIT_URL");
  options.token = getenvOrEmpty("LIVEKIT_TOKEN");

  if (positional.size() == 2) {
    options.url = positional[0];
    options.token = positional[1];
  } else if (positional.size() == 1) {
    return ParseResult::Error;
  }

  if (options.url.empty()) {
    options.url = kDefaultLiveKitUrl;
  }

  return options.token.empty() ? ParseResult::Error : ParseResult::Ok;
}

inline std::vector<std::uint8_t> toPayload(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

inline std::string toString(const std::vector<std::uint8_t>& payload) {
  return std::string(payload.begin(), payload.end());
}

} // namespace frame_metadata
