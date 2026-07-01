# cpp-example-collection

This repository contains a collection of small, self-contained examples for the
[LiveKit C++ SDK](https://github.com/livekit/client-sdk-cpp).

The goal of these examples is to demonstrate common usage patterns of the
LiveKit C++ SDK (connecting to a room, publishing tracks, RPC, data streams,
etc.) without requiring users to build the SDK from source.

## How the SDK is provided

These examples **automatically download a prebuilt LiveKit C++ SDK release**
from GitHub at CMake configure time.

This is handled by the CMake helper:
[`LiveKitSDK.cmake`](https://github.com/livekit-examples/cpp-example-collection/blob/main/cmake/LiveKitSDK.cmake).

By default, CMake resolves `LIVEKIT_SDK_VERSION=latest` to the newest GitHub
release and downloads the matching archive for your platform. The extracted SDK
lands under **build/_deps/livekit-sdk/**.

## Building the examples

Run these from the repository root. All examples — including
[`token_source/`](token_source/) — build together in one pass.

### macOS / Linux

```bash
cmake -S . -B build
cmake --build build
```

### Windows (Visual Studio generator)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The token source examples require LiveKit C++ SDK **v1.3.0** or newer. If
configure succeeds but those targets fail to compile, pin the SDK version
explicitly:

```bash
rm -rf build
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.3.0
cmake --build build
```

### Selecting an SDK version

Pin a specific release with `-DLIVEKIT_SDK_VERSION`:

```bash
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.3.0
```

When changing versions, remove the build directory first so CMake re-downloads
the SDK:

```bash
rm -rf build
cmake -S . -B build -DLIVEKIT_SDK_VERSION=1.3.0
```

### Build against a local SDK

Use this when validating an unreleased `client-sdk-cpp` commit, or when a
release tag exists but its prebuilt archives are not yet published:

```bash
git clone --recurse-submodules https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
./build.sh release --bundle --prefix "$HOME/livekit-sdk-install"

cd /path/to/cpp-example-collection
rm -rf build
cmake -S . -B build -DLIVEKIT_LOCAL_SDK_DIR="$HOME/livekit-sdk-install"
cmake --build build
```

### Troubleshooting configure

- **Start from a clean build directory** after a failed configure or download:
  `rm -rf build`
- **GitHub API rate limits** when resolving `latest`: pin
  `-DLIVEKIT_SDK_VERSION=1.3.0`, or export `GITHUB_TOKEN` and re-run configure.
- **404 on SDK download**: the release may not have platform archives yet. Pin
  an older release that does, or use `-DLIVEKIT_LOCAL_SDK_DIR` as above.

## Running the examples

After building, example binaries are located under `build/<example-name>/`.

For example:

```bash
./build/basic_room/basic_room --url <ws-url> --token <token>
```

See [`token_source/README.md`](token_source/README.md) for the token-source
examples.

### PlatformAudio

The `platform_audio` examples show microphone capture and speaker playout using
WebRTC's platform Audio Device Module:

```bash
./build/platform_audio/player/PlatformAudioPlayer <ws-url> <player-token>
./build/platform_audio/sender/PlatformAudioSender <ws-url> <sender-token>
```

### Supported platforms

Prebuilt SDKs are downloaded automatically for:

- Windows: x64
- macOS: x64, arm64 (Apple Silicon)
- Linux: x64

If no matching SDK is available for your platform, CMake configuration will fail with a clear error.
