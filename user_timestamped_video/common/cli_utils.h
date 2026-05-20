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
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace user_timestamped_video {

enum class ParseResult { Ok, Help, Error };

struct CliOptions {
  std::string url;
  std::string token;
  bool use_user_timestamp = true;
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
            << "  " << program << " <ws-url> <token> "
            << "[--with-user-timestamp|--without-user-timestamp]\n"
            << "or:\n"
            << "  LIVEKIT_URL=... LIVEKIT_TOKEN=... " << program
            << " [--with-user-timestamp|--without-user-timestamp]\n";
}

inline ParseResult parseArgs(int argc, char* argv[], CliOptions& options) {
  std::vector<std::string> positional;
  options = CliOptions{};

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      return ParseResult::Help;
    }
    if (arg == "--without-user-timestamp") {
      options.use_user_timestamp = false;
      continue;
    }
    if (arg == "--with-user-timestamp") {
      options.use_user_timestamp = true;
      continue;
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

  return (options.url.empty() || options.token.empty()) ? ParseResult::Error : ParseResult::Ok;
}

} // namespace user_timestamped_video
