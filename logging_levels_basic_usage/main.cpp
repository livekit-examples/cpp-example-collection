/*
 * Copyright 2023 LiveKit
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

/// @file basic_usage.cpp
/// @brief Demonstrates LiveKit SDK log-level control and custom log callbacks.
///
/// The SDK internally uses spdlog for its own logging. Client applications
/// control SDK log output through two public APIs:
///
///   - livekit::setLogLevel()    — filter SDK messages by severity.
///   - livekit::setLogCallback() — redirect SDK log output to your own handler.
///
/// Usage:
///   LoggingLevelsBasicUsage [trace|debug|info|warn|error|critical|off]
///
/// If no argument is given, the example cycles through every level and then
/// demonstrates the custom callback API.

#include <cstring>
#include <iostream>
#include <string>

#include "livekit/livekit.h"

namespace {

const char* levelName(livekit::LogLevel level) {
  switch (level) {
    case livekit::LogLevel::Trace:
      return "TRACE";
    case livekit::LogLevel::Debug:
      return "DEBUG";
    case livekit::LogLevel::Info:
      return "INFO";
    case livekit::LogLevel::Warn:
      return "WARN";
    case livekit::LogLevel::Error:
      return "ERROR";
    case livekit::LogLevel::Critical:
      return "CRITICAL";
    case livekit::LogLevel::Off:
      return "OFF";
  }
  return "UNKNOWN";
}

livekit::LogLevel parseLevel(const char* arg) {
  if (std::strcmp(arg, "trace") == 0) return livekit::LogLevel::Trace;
  if (std::strcmp(arg, "debug") == 0) return livekit::LogLevel::Debug;
  if (std::strcmp(arg, "info") == 0) return livekit::LogLevel::Info;
  if (std::strcmp(arg, "warn") == 0) return livekit::LogLevel::Warn;
  if (std::strcmp(arg, "error") == 0) return livekit::LogLevel::Error;
  if (std::strcmp(arg, "critical") == 0) return livekit::LogLevel::Critical;
  if (std::strcmp(arg, "off") == 0) return livekit::LogLevel::Off;
  std::cerr << "Unknown level '" << arg << "', defaulting to Info.\n"
            << "Valid: trace, debug, info, warn, error, critical, off\n";
  return livekit::LogLevel::Info;
}

/// Demonstrate cycling through every log level.
void runLevelCycleDemo() {
  const livekit::LogLevel levels[] = {
      livekit::LogLevel::Trace, livekit::LogLevel::Debug,    livekit::LogLevel::Info, livekit::LogLevel::Warn,
      livekit::LogLevel::Error, livekit::LogLevel::Critical, livekit::LogLevel::Off,
  };

  for (auto level : levels) {
    std::cout << "\n========================================\n"
              << " Setting log level to: " << levelName(level) << "\n"
              << "========================================\n";
    livekit::setLogLevel(level);
    std::cout << "  Current SDK log level: " << levelName(livekit::getLogLevel()) << "\n";
  }
}

/// Demonstrate a custom log callback (e.g. for ROS2 integration).
void runCallbackDemo() {
  std::cout << "\n========================================\n"
            << " Custom LogCallback demo\n"
            << "========================================\n";

  livekit::setLogLevel(livekit::LogLevel::Trace);

  livekit::setLogCallback([](livekit::LogLevel level, const std::string& logger_name, const std::string& message) {
    std::cout << "[CALLBACK] [" << levelName(level) << "] [" << logger_name << "] " << message << "\n";
  });

  std::cout << "Installed custom callback. SDK log messages will now be "
               "routed through it.\n";

  // Restore default stderr sink by passing an empty callback.
  livekit::setLogCallback(nullptr);

  std::cout << "(Restored default stderr sink)\n";
}

} // namespace

int main(int argc, char* argv[]) {
  livekit::initialize();

  if (argc > 1) {
    if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
      std::cout << "Usage: LoggingLevelsBasicUsage "
                   "[trace|debug|info|warn|error|critical|off]\n";
      livekit::shutdown();
      return 0;
    }
    livekit::LogLevel level = parseLevel(argv[1]);
    std::cout << "Setting log level to: " << levelName(level) << "\n\n";
    livekit::setLogLevel(level);
    std::cout << "Current SDK log level: " << levelName(livekit::getLogLevel()) << "\n";
  } else {
    runLevelCycleDemo();
    runCallbackDemo();
  }

  livekit::shutdown();
  return 0;
}
