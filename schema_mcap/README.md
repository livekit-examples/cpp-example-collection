# Schema MCAP Example

This example demonstrates the schema-metadata flow from the
`ladvoc/schema-metadata` branch of `livekit/client-sdk-cpp`:

- `schema_mcap_publisher` defines a JSON Schema, publishes a LiveKit data track
  with that schema and `json` frame encoding, and sends synthetic telemetry JSON.
- `schema_mcap_recorder` joins as a second participant, discovers the data
  track, retrieves the publisher's schema definition, writes it into an MCAP
  file, and records a fixed number of frames.

The recorder writes files named like:

```sh
livekit_schema_mcap_20260708_183012.mcap
```

## Why This Example Is Opt-In

The schema APIs are not part of the released SDK yet. Build this example against
a local SDK install from the upstream `ladvoc/schema-metadata` branch:

```sh
cmake -S . -B build-schema-mcap \
  -DLIVEKIT_LOCAL_SDK_DIR="$HOME/livekit-sdk-schema" \
  -DLIVEKIT_BUILD_SCHEMA_MCAP_EXAMPLE=ON

cmake --build build-schema-mcap --target schema_mcap_publisher schema_mcap_recorder
```

The MCAP dependency is local to this example. Enabling the example fetches the
Foxglove MCAP C++ headers into the CMake build tree and does not add MCAP to the
rest of the repository.

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
    --join \
    --room schema-mcap \
    --identity schema-publisher
)"

export LIVEKIT_RECORDER_TOKEN="$(
  lk token create \
    --api-key devkey \
    --api-secret secret \
    --join \
    --room schema-mcap \
    --identity schema-recorder
)"
```

## Run

Start the recorder first so it is waiting when the publisher advertises the data
track:

```sh
./build-schema-mcap/schema_mcap/recorder/schema_mcap_recorder --output-dir ./mcap --frames 100
```

In another terminal, start the publisher:

```sh
./build-schema-mcap/schema_mcap/publisher/schema_mcap_publisher
```

The recorder exits after `--frames` messages and closes the MCAP file. Each MCAP
message uses:

- schema name: `livekit.example.Telemetry`
- schema encoding: `jsonschema`
- channel topic: `/livekit/telemetry`
- message encoding: `json`

## Notes

This example intentionally uses JSON Schema and JSON frames because that keeps
the data human-readable while still exercising the schema-definition,
schema-discovery, and MCAP schema/channel export path. The same shape should
carry over to Protobuf, FlatBuffer, ROS message, or CDR payloads by changing the
schema definition and the advertised `DataTrackFrameEncoding`.
