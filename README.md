# cpp-example-collection

This repository contains a collection of small, self-contained examples for the
[LiveKit C++ SDK](https://github.com/livekit/client-sdk-cpp).

The goal of these examples is to demonstrate common usage patterns of the
LiveKit C++ SDK (connecting to a room, publishing tracks, RPC, data streams,
etc.) without requiring users to build the SDK from source.

## How the SDK is provided

These examples **automatically download a prebuilt LiveKit C++ SDK release**
from GitHub at CMake configure time.

This is handled by the CMake helper: [`LiveKitSDK.cmake`](https://github.com/livekit-examples/cpp-example-collection/blob/main/cmake/LiveKitSDK.cmake).

## Selecting a LiveKit SDK version

By default, the examples download the **latest released** LiveKit C++ SDK.

You can pin a specific SDK version using the `LIVEKIT_SDK_VERSION` CMake option.

### Examples

Use the latest release:

```bash
cmake -S . -B build
```

Use a specific version:

```bash
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.3.0
```

Reconfigure to change versions:

```bash
rm -rf build
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.3.0
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

### Supported platforms

Prebuilt SDKs are downloaded automatically for:

* Windows: x64
* macOS: x64, arm64 (Apple Silicon)
* Linux: x64

If no matching SDK is available for your platform, CMake configuration will fail with a clear error.
