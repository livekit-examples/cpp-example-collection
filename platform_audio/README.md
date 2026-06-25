# PlatformAudio

These examples demonstrate the platform Audio Device Module path:

- `PlatformAudioSender`: publishes microphone audio with echo cancellation, noise suppression, and auto gain control.
- `PlatformAudioPlayer`: joins the same room and plays subscribed remote audio through the platform output device.

Build the collection, then run the player and sender with different participant tokens for the same room:

```bash
./build/platform_audio/player/PlatformAudioPlayer <ws-url> <player-token>
./build/platform_audio/sender/PlatformAudioSender <ws-url> <sender-token>
```

Environment fallbacks:

```bash
export LIVEKIT_URL=wss://your-livekit-host
export LIVEKIT_PLAYER_TOKEN=<player-token>
export LIVEKIT_SENDER_TOKEN=<sender-token>
```

## Test environments

The sender captures the mic and runs the AEC/NS/AGC front-end; the player drives
hardware playout. The acoustic echo cancellation (AEC) reference is per Audio
Device Module (ADM), so the sender can only cancel playout from *its own* ADM.
Pick an environment based on what you want to prove out.

| Environment | What it proves | Notes |
|-------------|----------------|-------|
| **Two machines** (sender on A, player on B) | End-to-end capture → publish → subscribe → hardware playout over the network, like a real call. | Closest to a real LiveKit call. For full duplex (both apps on both boxes), use headphones or separate rooms to avoid feedback. |
| **One machine + headphones** | Mic capture and ADM playout both work on one box, with no acoustic feedback path. | Best for confirming device selection and round-trip latency in isolation. |
| **Noise suppression (NS)** | Steady background noise is attenuated while speech passes. | Add a fan / AC hum / typing near the mic. A/B by setting `noise_suppression = false` in `sender/main.cpp`. |
| **Auto gain control (AGC)** | Quiet vs. loud / near vs. far speech is normalized on the player side. | A/B by setting `auto_gain_control = false` in `sender/main.cpp`. |
| **`prefer_hardware = true`** | Platform hardware voice processing engages (e.g. macOS voice-processing I/O). | Set in the sender options; compare CPU and audio character vs. the software path. |
| **Device / hot-plug sanity** | `recordingDevices()` / `playoutDevices()` reflect attached hardware and route correctly. | Plug/unplug a USB or Bluetooth mic/headset before launch and check the startup device logs. |

> **AEC caveat:** these split sender/player apps cannot demonstrate AEC against
> each other on one machine over open speakers — the sender's AEC has no
> reference to the player's separate ADM, so the speaker output is treated as
> external sound and you get an echo/feedback loop. Genuine AEC requires a
> single application that both plays remote audio and captures the mic through
> the *same* ADM. Use headphones to avoid feedback with these examples.
