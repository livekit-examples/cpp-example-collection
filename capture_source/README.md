# Capture sources

This directory is the extensible home for examples built around
`livekit::CaptureSource`. Examples are grouped by producer or capture backend
so backend-specific dependencies, configuration, and shared helpers do not
leak into unrelated examples.

## Implemented source families

| Source family | Description |
| --- | --- |
| [`device`](device/) | Platform-native camera capture (AVFoundation on macOS, V4L2 on Linux) with device selection and format negotiation. |
| [`gstreamer`](gstreamer/) | SDK-owned GStreamer pipelines that publish pre-encoded video without sending each frame across the C++/Rust FFI boundary. |

The device family includes a device enumerator and a camera publisher. The
GStreamer family currently includes an animated test pipeline and a
default-webcam pipeline.

Note that `gstreamer/webcam` also publishes a camera, but through a GStreamer
pipeline that encodes on the CPU. Prefer `device/camera` for cameras: it uses
the platform capture stack directly and lets WebRTC own encoding.

## Adding more source families

Future capture implementations should be added alongside `device/` and
`gstreamer/` and named for the technology they exercise. Expected examples
include:

- a dedicated RTSP source that owns connection, depacketization, and capture
  lifecycle behavior;
- additional platform-native hardware capture backends.

An RTSP or V4L2 pipeline implemented through GStreamer belongs under
`gstreamer/`; an SDK-native implementation belongs in its own sibling source
family. Keep common helpers scoped to their family until behavior is genuinely
shared across capture backends.
