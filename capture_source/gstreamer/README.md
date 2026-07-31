# GStreamer capture sources

These examples use `livekit::CaptureSource` to let the Rust capture layer own
and run a GStreamer pipeline. Encoded access units move from GStreamer to the
LiveKit WebRTC source inside the SDK; video frames do not cross the C++/Rust FFI
boundary one at a time.

This is the GStreamer-specific source family under [`capture_source`](../).
Its targets, helper library, files, namespace, and runtime labels are prefixed
with `GStreamer` or `gstreamer` so additional source families can be added
without ambiguous names. A future native `/dev/video*` or dedicated RTSP
implementation should be a sibling of this directory; a V4L2 or RTSP pipeline
that is still owned by GStreamer belongs here.

Two standalone examples are provided:

- `LiveKitGStreamerCaptureTestPipeline` publishes a moving ball and timestamp generated
  entirely by `videotestsrc` and `timeoverlay`.
- `LiveKitGStreamerCaptureWebcam` reads the default camera selected by `autovideosrc`.

Both pipelines encode VP8 in GStreamer, expose the encoder as `lk_encoder` for
LiveKit bitrate updates, and terminate in `appsink name=lk_appsink` for encoded
capture.

## Requirements

- A LiveKit C++ SDK built from the `ladvoc/livekit-capture` branch with
  `LIVEKIT_ENABLE_CAPTURE=ON`.
- GStreamer 1.x runtime and development files.
- The GStreamer plugins that provide `videotestsrc`, `timeoverlay`,
  `autovideosrc`, `videoconvert`, `videoscale`, `videorate`, `vp8enc`, and
  `appsink`.
- Camera permission for the terminal or IDE when running the webcam example.

The released SDK downloaded by the example collection does not yet contain
this unreleased API, so build and install the local SDK first, then configure
this repository with `LIVEKIT_LOCAL_SDK_DIR` pointing to that install prefix.

## Build

From `client-sdk-cpp`, configure a release build with capture enabled and use
the repository build script to build and install it:

```bash
cmake --preset macos-release -DLIVEKIT_ENABLE_CAPTURE=ON
./build.sh release --bundle --prefix "$PWD/sdk-out/livekit-sdk-capture"
```

Use `linux-release` instead of `macos-release` on Linux. Then build this
collection from its repository root:

```bash
cmake -S . -B build \
  -DLIVEKIT_LOCAL_SDK_DIR=/path/to/client-sdk-cpp/sdk-out/livekit-sdk-capture
cmake --build build --target \
  LiveKitGStreamerCaptureTestPipeline LiveKitGStreamerCaptureWebcam
```

## Run

Pass the LiveKit URL and participant token as arguments:

```bash
./build/capture_source/gstreamer/test_pipeline/LiveKitGStreamerCaptureTestPipeline \
  <ws-url> <token>

./build/capture_source/gstreamer/webcam/LiveKitGStreamerCaptureWebcam \
  <ws-url> <token>
```

Alternatively, set `LIVEKIT_URL` and `LIVEKIT_TOKEN` and run either binary
without arguments. Press Ctrl-C to stop capture and disconnect.

The webcam pipeline is the following GStreamer launch description:

```text
autovideosrc ! videoconvert ! videoscale ! videorate \
  ! video/x-raw,format=I420,width=1280,height=720,framerate=30/1 \
  ! vp8enc name=lk_encoder deadline=1 cpu-used=8 keyframe-max-dist=30 \
      lag-in-frames=0 target-bitrate=2500000 \
  ! video/x-vp8 \
  ! appsink name=lk_appsink sync=false max-buffers=2 drop=true
```

To select a specific camera through GStreamer, replace `autovideosrc` in
`webcam/main.cpp` with the platform source and device property you need, such
as `v4l2src device=/dev/video0` on Linux or `avfvideosrc device-index=0` on
macOS. This remains a GStreamer-backed example; it is distinct from a future
SDK-native V4L2 source that would read `/dev/video*` directly.
