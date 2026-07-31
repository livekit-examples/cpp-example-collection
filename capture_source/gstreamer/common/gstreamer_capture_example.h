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

#pragma once

#include <livekit/capture_source.h>

#include <string>

namespace gstreamer_capture_example {

struct GStreamerExampleConfig {
  std::string name;
  std::string track_name;
  livekit::GstreamerVideoSourceConfig source;
};

int run(int argc, char* argv[], GStreamerExampleConfig config);

} // namespace gstreamer_capture_example
