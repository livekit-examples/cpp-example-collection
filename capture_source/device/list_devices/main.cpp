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

/// Prints the camera devices this machine can capture from.
///
/// Enumeration is a local query: it needs no room and no credentials. Use the
/// reported id with the `camera` example, or in a capture-portal
/// configuration.

#include <livekit/capture_source.h>

#include <exception>
#include <iostream>
#include <vector>

#include "device_capture_example.h"

int main() {
  const device_capture_example::LiveKitRuntime runtime;
  try {
    const std::vector<livekit::CaptureDeviceInfo> devices = livekit::CaptureSource::listDevices().get();
    device_capture_example::printDevices(devices);
    return 0;
  } catch (const livekit::CaptureSourceError& error) {
    // Also the path for an SDK built without capture support, and for
    // platforms with no capture backend.
    std::cerr << "[error] [device-list] Could not list capture devices: " << error.what() << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "[error] [device-list] " << error.what() << '\n';
    return 1;
  }
}
