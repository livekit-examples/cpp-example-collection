# encoded-video-ingest

End-to-end encoded video ingest demo for the LiveKit C++ SDK. GStreamer serves
an encoded bytestream over TCP; the producer reads that stream, splits complete
frames, pushes them into an encoded `VideoSource`, and publishes a normal local
video track. The consumer subscribes to the decoded track and renders it with
SDL.

This example intentionally does not use the removed `EncodedTcpIngest` helper.
TCP reconnect and bytestream framing are application code; the SDK surface is
`VideoSource(width, height, EncodedVideoSourceOptions{codec})` plus
`captureEncodedFrame`.

## Prerequisites

- A LiveKit server, such as `livekit-server --dev`.
- GStreamer 1.22+ with `good`, `bad`, `ugly`, and `libav` plugins.
- Producer and consumer tokens for the same room.

```bash
export LIVEKIT_URL=ws://localhost:7880
export PRODUCER_TOKEN="$(lk token create -r encoded-video-demo -i encoded-sender --join --publish)"
export CONSUMER_TOKEN="$(lk token create -r encoded-video-demo -i encoded-receiver --join --subscribe)"
```

## H.264

Start a camera pipeline that serves AUD-delimited Annex-B H.264 on TCP port
5005:

```bash
gst-launch-1.0 -v \
  avfvideosrc device-index=0 ! \
  video/x-raw,width=640,height=480,format=NV12,framerate=30/1 ! \
  videoconvert ! \
  x264enc tune=zerolatency speed-preset=ultrafast bitrate=1000 key-int-max=60 aud=true ! \
  h264parse config-interval=1 ! \
  video/x-h264,stream-format=byte-stream,alignment=au ! \
  tcpserversink host=0.0.0.0 port=5005 sync=false async=false
```

Linux: replace `avfvideosrc device-index=0` with
`v4l2src device=/dev/video0`. Windows: use `mfvideosrc device-index=0`.

Run the producer:

```bash
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN="$PRODUCER_TOKEN" \
  ./build-release/cpp-example-collection/encoded-video-ingest/EncodedVideoIngestProducer \
    --tcp-host 127.0.0.1 --tcp-port 5005 \
    --width 640 --height 480 \
    --codec h264
```

Run the consumer:

```bash
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN="$CONSUMER_TOKEN" \
  ./build-release/cpp-example-collection/encoded-video-ingest/EncodedVideoIngestConsumer \
    --from encoded-sender \
    --track-name encoded-h264
```

## Other Codecs

H.265 uses the same Annex-B framing path:

```bash
gst-launch-1.0 -v \
  avfvideosrc device-index=0 ! \
  video/x-raw,width=640,height=480,format=NV12,framerate=30/1 ! \
  videoconvert ! \
  x265enc tune=zerolatency speed-preset=ultrafast bitrate=1000 key-int-max=60 \
    option-string="aud=1:repeat-headers=1" ! \
  h265parse config-interval=1 ! \
  video/x-h265,stream-format=byte-stream,alignment=au ! \
  tcpserversink host=0.0.0.0 port=5005 sync=false async=false
```

Pass `--codec h265` and use `--track-name encoded-h265` on the consumer.

VP8 and AV1 use IVF framing:

```bash
gst-launch-1.0 -v \
  avfvideosrc device-index=0 ! \
  video/x-raw,width=640,height=480,format=NV12,framerate=30/1 ! \
  videoconvert ! \
  vp8enc deadline=1 target-bitrate=1000000 keyframe-max-dist=60 ! \
  ivfmux ! \
  tcpserversink host=0.0.0.0 port=5005 sync=false async=false
```

Pass `--codec vp8` and use `--track-name encoded-vp8`. For AV1, replace the
encoder with `av1enc`, `svtav1enc`, or `rav1enc`, pass `--codec av1`, and use
`--track-name encoded-av1`.

VP9 is intentionally omitted from this example because the current passthrough
path does not yet provide the VP9 RTP descriptor plumbing needed for reliable
ingest.
