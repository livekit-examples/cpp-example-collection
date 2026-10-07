# Camera device capture

Publishes a camera using the platform's native capture stack — AVFoundation on
macOS, V4L2 on Linux. The SDK's Rust core owns the capture session and pumps
frames directly into the RTC video source, so no frame crosses the C++/Rust FFI
boundary.

| Example | Description |
| --- | --- |
| [`list_devices`](list_devices/) | Prints every attached camera with its id, name, and (where the platform reports them) supported formats. |
| [`camera`](camera/) | Publishes a camera as a video track, with device selection and format negotiation on the command line. |

## Requirements

- A LiveKit C++ SDK built from the `ladvoc/livekit-capture` branch with
  `LIVEKIT_ENABLE_CAPTURE=ON`. Without it, both examples exit with an error
  from `listDevices()` rather than silently publishing nothing.
- macOS 12.3 or later, or Linux with V4L2.
- Camera permission. A CLI binary inherits the permission of the terminal that
  launched it, so on macOS the first run prompts — grant it under
  _System Settings ▸ Privacy & Security ▸ Camera_ for your terminal.

The released SDK downloaded by the example collection does not yet contain this
unreleased API, so build and install the local SDK first, then configure this
repository with `LIVEKIT_LOCAL_SDK_DIR` pointing to that install prefix.

## Build

From `client-sdk-cpp`, configure a release build with capture enabled and use
the repository build script to build and install it:

```bash
cmake --preset macos-release -DLIVEKIT_ENABLE_CAPTURE=ON
./build.sh release --bundle --prefix "$PWD/sdk-out/livekit-sdk-capture"
```

Use `linux-release` instead of `macos-release` on Linux. Then build this
collection from its repository root:

```bash
cmake -S . -B build \
  -DLIVEKIT_LOCAL_SDK_DIR=/path/to/client-sdk-cpp/sdk-out/livekit-sdk-capture
cmake --build build --target \
  LiveKitDeviceCaptureListDevices LiveKitDeviceCaptureCamera
```

## Listing devices

Enumeration needs no room and no credentials:

```bash
./build/capture_source/device/list_devices/LiveKitDeviceCaptureListDevices
```

```
- id: 0x212000032e40035
  name: 3D USB Camera
  model_id: UVC Camera VendorID_13028 ProductID_53
  manufacturer: 3D USB Camera
  formats: not enumerated on this platform
```

AVFoundation does not enumerate formats up front, so `formats` is empty and
`formats_complete` is false on macOS. Ask for a format and let the device
negotiate; V4L2 reports a complete list.

## Publishing a camera

Pass the LiveKit URL and participant token as arguments, or set `LIVEKIT_URL`
and `LIVEKIT_TOKEN`. Press Ctrl-C to stop capture and disconnect.

```bash
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<token>
cd build/capture_source/device/camera

# The platform default camera, device-chosen format
./LiveKitDeviceCaptureCamera

# A specific camera by name substring, nearest supported format
./LiveKitDeviceCaptureCamera --device "3D USB Camera" --size 1280x480 --fps 30 --select closest

# Whatever the camera's largest frame is
./LiveKitDeviceCaptureCamera --device 0x212000032e40035 --select highest-res
```

`--device` accepts a device id, an enumeration index, or a case-insensitive
substring of a device name. A name query is resolved to an id before the device
is opened, and an ambiguous one is rejected rather than guessed.

Run with `--help` for the full option list.

### Format selection

`--select` maps onto `livekit::DeviceFormatRequest`:

| Mode | Behavior |
| --- | --- |
| `default` | The device picks. |
| `exact` | Requires `--size` and `--fps`; fails if the camera offers no such format. |
| `closest` | Nearest supported to `--size` and `--fps`. |
| `highest-res` | Largest frame, optionally constrained by `--fps`. |
| `highest-fps` | Fastest frame rate, optionally constrained by `--size`. |

Only resolution and frame rate participate in selection, and `--format` applies
to `exact` and `closest` only. It is validated and then treated as a preference
the backend may substitute: macOS accepts `i420`, `nv12`, and `bgra` but always
delivers NV12, while V4L2 rejects `i420` and `bgra` and falls back through the
formats it supports. `nv12` is the only value both accept, which is the
default.

The negotiated resolution is reported by `CaptureSource::width()` and
`height()`, which the example logs once the device is open. The negotiated frame
rate and frame format are not carried back across the FFI.

## Stereo cameras

A side-by-side stereo camera needs no special handling: request its full width
and both eyes are published as one wide track. Splitting the eyes into separate
tracks is a downstream concern, not a capture one.

A "3D USB Camera" (UVC `0x32e4:0x0035`) presents its two sensors as a single
frame, so its advertised sizes are already the stereo pair:

| Requested | Per eye | Observed on USB 2.0 |
| --- | --- | --- |
| `1280x480` | 640x480 | delivered |
| `2560x720` | 1280x720 | delivered |
| `3840x1080` | 1920x1080 | downgraded to 2560x720 |

```sh
./LiveKitDeviceCaptureCamera --device "3D USB Camera" --size 2560x720 --fps 30 --select closest
```

`exact` matches against the formats the device *advertises*, but the source
reports whatever the device then *delivers*, and those can differ: the camera
above advertises 3840x1080 yet delivers 2560x720 on a 480 Mb/s link. Check
`CaptureSource::width()` and `height()` rather than assuming the request was
honored.

Frame rates need no special care. Matching is rounding-tolerant, so `--fps 30`
is satisfied by a camera advertising 30.00003 fps and the camera is then driven
at its advertised rate.
