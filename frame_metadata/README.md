# Frame Metadata Example

This example is split into two executables and demonstrates LiveKit C++ SDK video frame metadata feature:

- `frame_metadata_producer` publishes a synthetic video track named `"sensor-camera"`.
- Each video frame attaches additional data via the frame metadata features of the SDK:

  | Field | Description |
  | --- | --- |
  | `user_timestamp_us` | Wall-clock capture time (us) |
  | `frame_id` | Monotonic frame counter |
  | `user_data` | Free-form user data, in this example it is used to send a JSON temperature reading correlated with the video frame |

- `frame_metadata_consumer` subscribes to the remote track by name and reads the
  metadata through `setOnVideoFrameEventCallback`.

Run them in the same room with different participant identities. `LIVEKIT_URL`
defaults to `ws://localhost:7880` when unset:

```sh
export LIVEKIT_TOKEN=<producer-token>
./build/frame_metadata/producer/frame_metadata_producer

export LIVEKIT_TOKEN=<consumer-token>
./build/frame_metadata/consumer/frame_metadata_consumer
```

You can also pass URL and token explicitly:

```sh
./build/frame_metadata/producer/frame_metadata_producer ws://localhost:7880 <producer-token>
./build/frame_metadata/consumer/frame_metadata_consumer ws://localhost:7880 <consumer-token>
```

Metadata notes for this example:

- `frame_id` and `user_ts_us` come from `VideoFrameMetadata` and are the values
  to compare end to end.
- `user_data` is free-form frame metadata. This example uses it only for the
  temperature reading, similar to a robotics or sensor-fusion camera feed, in the following format:

```json
{"temperature_c":23.4}
```

- `capture_ts_us` on the producer is the timestamp submitted to `captureFrame`.
- `capture_ts_us` on the consumer is the received WebRTC frame timestamp.
- Producer and consumer `capture_ts_us` values are not expected to match exactly,
  because WebRTC may translate frame timestamps onto its own internal
  capture-time timeline before delivery.
