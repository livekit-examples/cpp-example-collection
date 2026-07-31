# Capture sources

This directory is the extensible home for examples built around
`livekit::CaptureSource`. Examples are grouped by producer or capture backend
so backend-specific dependencies, configuration, and shared helpers do not
leak into unrelated examples.

## Implemented source families

| Source family | Description |
| --- | --- |
| [`gstreamer`](gstreamer/) | SDK-owned GStreamer pipelines that publish pre-encoded video without sending each frame across the C++/Rust FFI boundary. |

The GStreamer family currently includes an animated test pipeline and a
default-webcam pipeline.

## Adding more source families

Future capture implementations should be added alongside `gstreamer/` and
named for the technology they exercise. Expected examples include:

- a native V4L2 source that reads directly from `/dev/video*` without a
  GStreamer pipeline;
- a dedicated RTSP source that owns connection, depacketization, and capture
  lifecycle behavior;
- additional platform-native camera or hardware capture backends.

An RTSP or V4L2 pipeline implemented through GStreamer belongs under
`gstreamer/`; an SDK-native implementation belongs in its own sibling source
family. Keep common helpers scoped to their family until behavior is genuinely
shared across capture backends.
