# Token sources

These examples show the different ways to obtain the credentials
(**WebSocket URL** + **participant JWT**) the SDK needs to join a room. Each
example builds its own executable, constructs a single kind of
[`TokenSource`](https://github.com/livekit/client-sdk-cpp/blob/main/include/livekit/token_source.h),
connects to a room, logs participant join/leave for a few seconds, then
disconnects.

> Token sources are for the **initial connection only**. Once connected, the
> LiveKit server refreshes the session token internally — your token source is
> not called again unless you connect again. See the SDK's
> [authentication docs](https://github.com/livekit/client-sdk-cpp/blob/main/docs/authentication.md).

## The types

| Example | Type | When to use |
|---|---|---|
| `token_source_literal.cpp` | `LiteralTokenSource` | You already have a URL + JWT (minted out of band, e.g. `lk token create`). The SDK consumes them as-is. |
| `token_source_endpoint.cpp` | `EndpointTokenSource` | Recommended for production. The SDK POSTs request options to your backend token endpoint, which returns the URL + a fresh JWT. API keys stay server-side. |
| `token_source_sandbox.cpp` | `SandboxTokenSource` | Local development only. Uses LiveKit Cloud's sandbox token server. Not for production. |
| `token_source_custom.cpp` | `CustomTokenSource` | You have an internal auth/token system. Plug in your own async callback that returns credentials. |
| `token_source_caching.cpp` | `CachingTokenSource` | A decorator that adds JWT-aware caching around any configurable source (endpoint/sandbox/custom) to cut down on fetch calls. |

`LiteralTokenSource` is *fixed* (no per-call options); the others are
*configurable* and accept
[`TokenRequestOptions`](https://github.com/livekit/client-sdk-cpp/blob/main/include/livekit/token_source.h)
(room name, participant identity, agent dispatch, ...).

## Configuration

All inputs come from environment variables, so no secrets or sandbox IDs are
committed to source.

| Variable | Used by | Notes |
|---|---|---|
| `LIVEKIT_URL` | literal, custom | WebSocket URL, e.g. `ws://localhost:7880`. |
| `LIVEKIT_TOKEN` | literal, custom | Participant JWT. |
| `LIVEKIT_TOKEN_ENDPOINT` | endpoint, caching | Token endpoint URL. Default `http://127.0.0.1:3000/createToken`. |
| `LIVEKIT_TOKEN_ENDPOINT_METHOD` | endpoint, caching | Optional HTTP method (default `POST`). |
| `LIVEKIT_TOKEN_ENDPOINT_HEADERS` | endpoint, caching | Optional newline-separated `Name: Value` headers. |
| `LIVEKIT_SANDBOX_ID` | sandbox | Required. Sandbox ID from LiveKit Cloud. |
| `LIVEKIT_AGENT_NAME` | sandbox | Optional agent to dispatch into the room. |
| `LIVEKIT_AGENT_METADATA` | sandbox | Optional metadata for the dispatched agent. |

## Building

The token source API is newer than the latest published SDK release, so build
these examples against a **local SDK build** of the
[`feature/token_source_api`](https://github.com/livekit/client-sdk-cpp/tree/feature/token_source_api)
branch rather than a downloaded release. The commands below pin the branch to
commit [`bbf6a41`](https://github.com/livekit/client-sdk-cpp/commit/bbf6a41fae42607ee19ff44ccddac786767b34e3)
so the examples build against a known-good API; drop the `git checkout` of the
hash to track the branch tip instead.

```bash
# 1. Build and install the SDK from the feature branch.
git clone https://github.com/livekit/client-sdk-cpp.git
cd client-sdk-cpp
git checkout bbf6a41fae42607ee19ff44ccddac786767b34e3
git submodule update --init --recursive
./build.sh release --bundle --prefix "$HOME/livekit-sdk-install"

# 2. Configure the examples against that local install.
cd /path/to/cpp-example-collection
cmake -S . -B build -DLIVEKIT_LOCAL_SDK_DIR="$HOME/livekit-sdk-install"
cmake --build build
```

Once the token source API ships in a release, you can drop
`-DLIVEKIT_LOCAL_SDK_DIR` and use the normal download flow (optionally pinning
`-DLIVEKIT_SDK_VERSION`).

## Running

The built binaries live under `build/token_source/`:

```bash
# Literal: bring your own URL + token
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<participant-jwt>
./build/token_source/token_source_literal

# Endpoint / caching: point at your token endpoint
export LIVEKIT_TOKEN_ENDPOINT=http://127.0.0.1:3000/createToken
./build/token_source/token_source_endpoint
./build/token_source/token_source_caching

# Sandbox: development-only, ID from the environment
export LIVEKIT_SANDBOX_ID=<your-sandbox-id>
./build/token_source/token_source_sandbox

# Custom: callback returns credentials (this example reads the env)
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<participant-jwt>
./build/token_source/token_source_custom
```

Generate a development token with the
[LiveKit CLI](https://docs.livekit.io/home/cli/cli-setup/) (a dev server started
with `livekit-server --dev` uses `devkey` / `secret`):

```bash
export LIVEKIT_TOKEN=$(lk token create \
  --api-key devkey --api-secret secret \
  -i my-participant --join --room my-room \
  --valid-for 24h --token-only)
```
