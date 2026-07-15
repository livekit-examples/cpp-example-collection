# Schema MCAP Example

This example demonstrates LiveKit data-track schema metadata with a
Foxglove-native point cloud recording flow. One participant streams lidar data
and synchronized frame transforms over two schema-advertised data tracks. A
second participant retrieves both JSON Schemas from LiveKit and writes both
channels into one MCAP file that Foxglove can open directly.

The key split is that the schemas travel as participant metadata while point
cloud and transform frames travel over data tracks. That lets the recorder write
the MCAP `Schema` and `Channel` records from LiveKit metadata before it writes
any messages.

The example publishes the included PandaSet autonomous-driving lidar sequence
as `foxglove.PointCloud` JSON frames, paired with synchronized
`foxglove.FrameTransform` messages. The recorder joins as a second participant,
discovers the advertised schema metadata, retrieves both JSON Schemas from the
publisher, and records the incoming frames to an MCAP file.

```text
+---------------------------- Publisher -----------------------------+
|                                                                    |
|  defineSchema                         publishDataTrack              |
|  +--------------------------+         +--------------------------+  |
|  | foxglove.PointCloud      |         | pointcloud-json          |  |
|  | foxglove.FrameTransform  |         | frame-transform-json     |  |
|  +------------+-------------+         +------------+-------------+  |
|               |                                    |                |
+---------------|------------------------------------|----------------+
                | schema metadata                    | JSON frames
                v                                    v
+--------------------------- LiveKit SFU -----------------------------+
|                                                                    |
|  Participant data blob                  Data tracks                 |
|  +--------------------------+           +------------------------+  |
|  | JSON Schema definitions  |           | lidar-local clouds     |  |
|  | and track schema IDs     |           | map -> lidar transforms|  |
|  +------------+-------------+           +-----------+------------+  |
|               |                                     |               |
+---------------|-------------------------------------|---------------+
                | getSchema(...)                      | subscriptions
                +------------------+------------------+
                                   v
+----------------------------- Recorder ------------------------------+
|                                                                    |
|  MCAP Schema records: foxglove.PointCloud, foxglove.FrameTransform |
|  MCAP Channels:       /pointcloud, /tf                              |
|  MCAP Messages:       synchronized cloud and transform frames       |
|                                                                    |
+----------------------------------+---------------------------------+
                                   |
                                   v
                     livekit_pointcloud_<date>.mcap
```

The recorder writes files named like:

```sh
livekit_pointcloud_20260708_183012.mcap
```

## Build

Configure and build from the repository root:

```sh
cmake -S . -B build
cmake --build build --target schema_mcap_publisher schema_mcap_recorder
```

The MCAP dependency is local to this example. CMake fetches the Foxglove MCAP
C++ headers into the build tree and does not add MCAP to the rest of the
repository.

## Local SFU

Run a local LiveKit server version that includes the data-track schema support.
The SFU must have both data tracks and participant data blobs enabled:

```yaml
enable_data_tracks: true
enable_participant_data_blob: true
```

With the LiveKit dev server defaults, generate two tokens for the same room with
different identities:

```sh
export LIVEKIT_URL=ws://localhost:7880

export LIVEKIT_PUBLISHER_TOKEN="$(
  lk token create \
    --api-key devkey \
    --api-secret secret \
    --token-only \
    --join \
    --room schema-mcap \
    --identity schema-publisher
)"

export LIVEKIT_RECORDER_TOKEN="$(
  lk token create \
    --api-key devkey \
    --api-secret secret \
    --token-only \
    --join \
    --room schema-mcap \
    --identity schema-recorder
)"
```

## Run

The easiest local loop is the runner script. It builds both targets, generates
token-only JWTs with the `lk` CLI, starts the recorder, then starts the
publisher:

```sh
./schema_mcap/run_local.sh
```

The runner uses the bundled PandaSet sample automatically. It contains all 80
consecutive 10 Hz scans from scene 001, covering eight seconds. The publisher
plays one complete pass and does not loop. Each scan is a real sensor frame,
not a static cloud transformed to emulate movement. Use
`--pointcloud-sequence <dir>` to replay another sequence in the same compact
fixture format.

The script uses these defaults:

- LiveKit URL: `ws://localhost:7880`
- API key / secret: `devkey` / `secret`
- room: `schema-mcap`
- input: `schema_mcap/data/pandaset_sample`
- frames: the complete 80-frame sequence
- output directory: `./mcap`

For debugging, you can still run the two binaries manually. Start the recorder
first so it is waiting when the publisher advertises the data track:

```sh
./build/schema_mcap/recorder/schema_mcap_recorder --output-dir ./mcap
```

In another terminal, start the publisher:

```sh
./build/schema_mcap/publisher/schema_mcap_publisher \
  --pointcloud-sequence schema_mcap/data/pandaset_sample
```

The publisher defaults to the sequence's frame count. Use `--frames <count>` or
`SCHEMA_MCAP_FRAME_COUNT` to request a shorter recording; requests longer than
the sequence are capped to one complete pass.

The recorder emits one combined progress line every ten frame pairs with the
point-cloud and transform payload sizes, recent application-payload bitrate,
and observed publisher-to-recorder latency. At shutdown it prints frame count,
duration, total payload bytes, average payload bitrate, and average/minimum/
maximum latency for the full session. These console statistics are not written
to the MCAP file. Bitrate excludes LiveKit transport overhead, and latency uses
the publisher's wall-clock timestamp, so the two hosts should have synchronized
clocks when they are not the same machine.

## Inspect and Visualize

If the [MCAP CLI](https://mcap.dev/guides/cli) is installed, validate the
recording with:

```sh
mcap info ./mcap/livekit_pointcloud_*.mcap
mcap doctor ./mcap/livekit_pointcloud_*.mcap
```

Open the MCAP file in the Foxglove desktop or web app and add a 3D panel. Enable
the `/pointcloud` topic. Set the point cloud's color mode to **Color map** and
its color field to `intensity`. The same MCAP contains `/tf` transforms:

- use `map` as the display frame for a world-fixed view;
- use `lidar` as the follow frame for a vehicle-following view.

## PandaSet Sample and License

The included animated sample is derived from all frames 00–79 of
[PandaSet](https://pandaset.org/) scene 001, using the mechanical 360-degree
lidar. PandaSet is provided under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) and the
[PandaSet Dataset Terms of Use](http://scalesd.org/pandaset-terms-of-use.html).
See [`data/pandaset_sample/NOTICE.md`](data/pandaset_sample/NOTICE.md) for
attribution, modifications, pinned source hashes, and the required citation.
The source dataset terms are preserved beside the fixture in
[`PANDASET_TERMS.txt`](data/pandaset_sample/PANDASET_TERMS.txt).

The fixture is checked in as compact runtime data; pandas is not required to
build or run this example. To regenerate it from an official PandaSet scene
download, install pandas and run:

```sh
python3 -m pip install pandas
python3 schema_mcap/tools/convert_pandaset_sample.py \
  /path/to/pandaset/001 \
  schema_mcap/data/pandaset_sample \
  --scene-id 001 \
  --frame-count 80 \
  --max-points 8192 \
  --source-repository https://huggingface.co/datasets/autoexpert-cvpr2026-workshop/seq \
  --source-revision 7af0c275d38291e4cbfe9481439c93bc48e01f3d
```

## Format

Foxglove recognizes both channels because their schema names are exactly
`foxglove.PointCloud` and `foxglove.FrameTransform`, their schema encoding is
`jsonschema`, and their message encoding is `json`. The JSON envelopes are
human-readable; only the packed point buffer is base64-encoded.

Each point-cloud message uses:

- schema name: `foxglove.PointCloud`
- schema encoding: `jsonschema`
- channel topic: `/pointcloud`
- message encoding: `json`

Each synchronized transform uses:

- schema name: `foxglove.FrameTransform`
- schema encoding: `jsonschema`
- channel topic: `/tf`
- message encoding: `json`
- parent/child frames: `map` → `lidar`

Each PandaSet cloud contains 8192 sampled points from a real lidar scan, encoded
as `x`, `y`, `z`, and normalized reflectivity. The converter applies PandaSet's
official inverse lidar-pose transform so points are in the lidar-local frame.
Every point cloud therefore uses `frame_id: "lidar"` while the synchronized
`/tf` message preserves its original world pose. The packed point layout is
four little-endian `FLOAT32` values with a 16-byte stride:

| Field | Offset |
| --- | ---: |
| `x` | 0 |
| `y` | 4 |
| `z` | 8 |
| `intensity` | 12 |

The packed bytes are base64-encoded in the JSON `data` property. The cloud uses
the canonical Foxglove `{sec, nsec}` timestamp and identity pose. This keeps the
example independent of ROS and Protobuf and adds no serialization dependency.

The publisher defines the point-cloud JSON Schema below and the canonical
[Foxglove FrameTransform JSON Schema](https://github.com/foxglove/foxglove-sdk/blob/main/schemas/jsonschema/FrameTransform.json):

```json
{
  "title": "foxglove.PointCloud",
  "description": "A collection of N-dimensional points, which may contain additional fields with information like normals, intensity, etc.",
  "$comment": "Generated by https://github.com/foxglove/foxglove-sdk",
  "type": "object",
  "properties": {
    "timestamp": {
      "type": "object",
      "title": "time",
      "properties": {
        "sec": {
          "type": "integer",
          "minimum": 0
        },
        "nsec": {
          "type": "integer",
          "minimum": 0,
          "maximum": 999999999
        }
      },
      "description": "Timestamp of point cloud"
    },
    "frame_id": {
      "type": "string",
      "description": "Frame of reference"
    },
    "pose": {
      "title": "foxglove.Pose",
      "description": "The origin of the point cloud relative to the frame of reference",
      "type": "object",
      "properties": {
        "position": {
          "title": "foxglove.Vector3",
          "description": "Point denoting position in 3D space",
          "type": "object",
          "properties": {
            "x": {
              "type": "number",
              "description": "x component"
            },
            "y": {
              "type": "number",
              "description": "y component"
            },
            "z": {
              "type": "number",
              "description": "z component"
            }
          },
          "required": ["x", "y", "z"]
        },
        "orientation": {
          "title": "foxglove.Quaternion",
          "description": "Quaternion denoting orientation in 3D space",
          "type": "object",
          "properties": {
            "x": {
              "type": "number",
              "description": "x value"
            },
            "y": {
              "type": "number",
              "description": "y value"
            },
            "z": {
              "type": "number",
              "description": "z value"
            },
            "w": {
              "type": "number",
              "description": "w value"
            }
          },
          "required": ["x", "y", "z", "w"]
        }
      },
      "required": ["position", "orientation"]
    },
    "point_stride": {
      "type": "integer",
      "minimum": 0,
      "description": "Number of bytes between points in the `data`"
    },
    "fields": {
      "type": "array",
      "items": {
        "title": "foxglove.PackedElementField",
        "description": "A field present within each element in a byte array of packed elements.",
        "type": "object",
        "properties": {
          "name": {
            "type": "string",
            "description": "Name of the field"
          },
          "offset": {
            "type": "integer",
            "minimum": 0,
            "description": "Byte offset from start of data buffer"
          },
          "type": {
            "title": "foxglove.NumericType",
            "description": "Type of data in the field. Integers are stored using little-endian byte order.",
            "oneOf": [
              {"title": "UNKNOWN", "const": 0, "description": "Unknown numeric type"},
              {"title": "UINT8", "const": 1, "description": "Unsigned 8-bit integer"},
              {"title": "INT8", "const": 2, "description": "Signed 8-bit integer"},
              {"title": "UINT16", "const": 3, "description": "Unsigned 16-bit integer"},
              {"title": "INT16", "const": 4, "description": "Signed 16-bit integer"},
              {"title": "UINT32", "const": 5, "description": "Unsigned 32-bit integer"},
              {"title": "INT32", "const": 6, "description": "Signed 32-bit integer"},
              {"title": "FLOAT32", "const": 7, "description": "32-bit floating-point number"},
              {"title": "FLOAT64", "const": 8, "description": "64-bit floating-point number"}
            ]
          }
        },
        "required": ["name", "offset", "type"]
      },
      "description": "Fields in `data`. At least 2 coordinate fields from `x`, `y`, and `z` are required for each point's position; `red`, `green`, `blue`, and `alpha` are optional for customizing each point's color."
    },
    "data": {
      "type": "string",
      "contentEncoding": "base64",
      "description": "Point data, interpreted using `fields`"
    }
  },
  "required": ["timestamp", "frame_id", "pose", "point_stride", "fields", "data"]
}
```
