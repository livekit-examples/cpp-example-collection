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

/// Publishes the default webcam selected by GStreamer's autovideosrc through
/// the LiveKit capture-source API.

#include <livekit/capture_source.h>

#include <exception>
#include <iostream>
#include <utility>

#include "gstreamer_capture_example.h"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

constexpr const char* kPipeline =
    "autovideosrc "
    "! videoconvert ! videoscale ! videorate "
    "! video/x-raw,format=I420,width=1280,height=720,framerate=30/1 "
    "! vp8enc name=lk_encoder deadline=1 cpu-used=8 keyframe-max-dist=30 "
    "lag-in-frames=0 target-bitrate=2500000 "
    "! video/x-vp8 "
    "! appsink name=lk_appsink sync=false max-buffers=2 drop=true";

} // namespace

int main(int argc, char* argv[]) {
  try {
    livekit::GstreamerVideoSourceConfig source;
    source.pipeline = kPipeline;
    source.codec = livekit::VideoCodec::VP8;
    source.resolution = livekit::CaptureResolution{kWidth, kHeight};
    source.rate_control =
        livekit::GstreamerRateControl{"lk_encoder", "target-bitrate", livekit::GstreamerBitrateUnit::Bps};

    return gstreamer_capture_example::run(argc, argv,
                                          {"gstreamer-capture-webcam", "gstreamer-webcam", std::move(source)});
  } catch (const std::exception& error) {
    std::cerr << "[error] [gstreamer-capture-webcam] " << error.what() << '\n';
    return 1;
  }
}
