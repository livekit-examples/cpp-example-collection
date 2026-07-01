# UserData

This example shows how to pair small application data with video frames:

- `UserDataProducer` publishes a synthetic video track named `"sensor-camera"`.
  Each frame carries `VideoCaptureOptions::metadata.frame_id` and
  `VideoCaptureOptions::metadata.user_timestamp_us`.
- The producer also attaches a small JSON temperature reading to
  `VideoCaptureOptions::metadata.user_data`.
- `UserDataConsumer` subscribes to the video track and parses the inline
  `user_data` payload on each received frame.

Run them in the same room with different participant identities:

```sh
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<producer-token> ./UserDataProducer
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<consumer-token> ./UserDataConsumer
```

Requirements:

- LiveKit C++ SDK `v1.3.0` or newer. This example uses packet-trailer frame IDs
  and user data in `VideoFrameMetadata`.
- To pin the SDK version when configuring the examples, pass
  `-DLIVEKIT_SDK_VERSION=1.3.0` to CMake.

The data payload is intentionally small:

```json
{"frame_id":42,"timestamp_us":1782930000000000,"temperature_c":23.4}
```

In a robotics or sensor-fusion application, this lets a small telemetry value
travel with the frame it describes without requiring a companion data track.
