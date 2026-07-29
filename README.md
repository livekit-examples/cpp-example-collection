# cpp-example-collection

This repository contains a collection of small, self-contained examples for the
[LiveKit C++ SDK](https://github.com/livekit/client-sdk-cpp).

The goal of these examples is to demonstrate common usage patterns of the
LiveKit C++ SDK (connecting to a room, publishing tracks, RPC, data streams,
etc.) without requiring users to build the SDK from source.

## Examples

| Example | Description |
| --- | --- |
| [`basic_room`](basic_room/) | Connects to a room and publishes synthetic audio and video tracks. |
| [`frame_metadata`](frame_metadata/) | Publishes and consumes video frames with frame metadata such as IDs, timestamps, and user data. |
| [`hello_livekit/receiver`](hello_livekit/receiver/) | Subscribes to the sender example's video and data tracks. |
| [`hello_livekit/sender`](hello_livekit/sender/) | Publishes synthetic video and data for a paired receiver example. |
| [`logging_levels/basic_usage`](logging_levels/basic_usage/) | Demonstrates SDK log-level filtering and log callbacks. |
| [`logging_levels/custom_sinks`](logging_levels/custom_sinks/) | Shows custom SDK log sinks such as file, JSON, and ROS-style output. |
| [`ping_pong/ping`](ping_pong/ping/) | Sends ping messages over a data track and records response latency. |
| [`ping_pong/pong`](ping_pong/pong/) | Listens for ping messages and publishes matching pong responses. |
| [`platform_audio`](platform_audio/) | Demonstrates microphone capture and speaker playout with platform audio devices. |
| [`schema_mcap`](schema_mcap/) | Records schema-advertised PandaSet lidar frames and synchronized transforms as a Foxglove-native MCAP. |
| [`simple_data_stream`](simple_data_stream/) | Sends and receives text and byte streams over LiveKit data streams. |
| [`simple_room`](simple_room/) | Connects to a room, publishes local media, and subscribes to remote media. |
| [`simple_rpc`](simple_rpc/) | Demonstrates LiveKit RPC calls between participants. |
| [`token_source`](token_source/) | Shows ways to supply connection credentials through LiveKit token sources. |

## How the SDK is provided

These examples **automatically download a prebuilt LiveKit C++ SDK release**
from GitHub at CMake configure time.

This is handled by the CMake helper: [`LiveKitSDK.cmake`](https://github.com/livekit-examples/cpp-example-collection/blob/main/cmake/LiveKitSDK.cmake).

## Selecting a LiveKit SDK version

By default, the examples download the **latest released** LiveKit C++ SDK.

You can pin a specific SDK version using the `LIVEKIT_SDK_VERSION` CMake option.

### Version Selection Examples

Use the latest release:

```bash
cmake -S . -B build
```

Use a specific version:

```bash
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.5.0
```

Reconfigure to change versions:

```bash
rm -rf build
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.5.0
```

Build against a local SDK:

```bash
rm -rf build
# install the SDK into $HOME/livekit-sdk-install (or any other directory)
cmake --install <sdk-build-dir> --prefix "$HOME/livekit-sdk-install"

# build the examples against the local SDK
cmake -S . -B build -DLIVEKIT_LOCAL_SDK_DIR="$HOME/livekit-sdk-install"
```

### Building the examples

#### macOS / Linux

```bash
cmake -S . -B build
cmake --build build
```

#### Windows (Visual Studio generator)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The LiveKit Release SDK is downloaded into **build/_deps/livekit-sdk/**

### Running the examples

After building, example binaries are located under:

```bash
build/<example-name>/
```

For example:

```bash
./build/basic_room/basic_room --url <ws-url> --token <token>
```

### Supported platforms

Prebuilt SDKs are downloaded automatically for:

* Windows: x64
* macOS: x64, arm64 (Apple Silicon)
* Linux: x64

If no matching SDK is available for your platform, CMake configuration will fail with a clear error.
