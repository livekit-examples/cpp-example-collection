# UserTimestampedVideo

This example is split into two executables and can demonstrate all four
producer/consumer combinations:

- `UserTimestampedVideoProducer` publishes a synthetic video track named
  `"timestamped-camera"` and stamps each frame with
  `VideoCaptureOptions::metadata.user_timestamp_us`.
- `UserTimestampedVideoConsumer` subscribes to the remote
  `"timestamped-camera"` track by name with either the rich or legacy callback
  path.

Run them in the same room with different participant identities:

```sh
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<producer-token> ./UserTimestampedVideoProducer
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<consumer-token> ./UserTimestampedVideoConsumer
```

Requirements:

- LiveKit C++ SDK `v0.3.4` or newer. This example uses
  `VideoFrameMetadata` and `setOnVideoFrameEventCallback`, which are not
  available in older SDK releases.
- To pin the SDK version when configuring the examples, pass
  `-DLIVEKIT_SDK_VERSION=0.3.4` to CMake.

Flags:

- Producer default: sends user timestamps
- Producer `--with-user-timestamp`: explicitly sends user timestamps
- Producer `--without-user-timestamp`: does not send user timestamps
- Consumer default: reads user timestamps through `setOnVideoFrameEventCallback`
- Consumer `--with-user-timestamp`: explicitly reads user timestamps through
  `setOnVideoFrameEventCallback`
- Consumer `--without-user-timestamp`: ignores metadata through the legacy
  `setOnVideoFrameCallback`

Matrix:

```sh
# 1. Producer sends, consumer reads
./UserTimestampedVideoProducer
./UserTimestampedVideoConsumer

# 2. Producer sends, consumer ignores
./UserTimestampedVideoProducer
./UserTimestampedVideoConsumer --without-user-timestamp

# 3. Producer does not send, consumer ignores
./UserTimestampedVideoProducer --without-user-timestamp
./UserTimestampedVideoConsumer --without-user-timestamp

# 4. Producer does not send, consumer reads
./UserTimestampedVideoProducer --without-user-timestamp
./UserTimestampedVideoConsumer
```

Timestamp note:

- `user_ts_us` is application metadata and is the value to compare end to end.
- `capture_ts_us` on the producer is the timestamp submitted to `captureFrame`.
- `capture_ts_us` on the consumer is the received WebRTC frame timestamp.
- Producer and consumer `capture_ts_us` values are not expected to match exactly,
  because WebRTC may translate frame timestamps onto its own internal
  capture-time timeline before delivery.
