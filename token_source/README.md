# Token Source Examples

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

## Types

| Type | Example | When to use |
| --- | --- | --- |
| `LiteralTokenSource` | `token_source_literal.cpp` | You already have a URL + JWT (minted out of band, e.g. `lk token create`). The SDK consumes them as-is. |
| `EndpointTokenSource` | `token_source_endpoint.cpp` | Recommended for production. The SDK POSTs request options to your backend token endpoint, which returns the URL + a fresh JWT. API keys stay server-side. |
| `DevelopmentTokenSource` | `token_source_development.cpp` | Local development only. Uses LiveKit Cloud's development token server. **Not for production.** |
| `CustomTokenSource` | `token_source_custom.cpp` | You have an internal auth/token system. Plug in your own async callback that returns credentials. |
| `CachingTokenSource` | `token_source_caching.cpp` | A decorator that adds JWT-aware caching around any configurable source (endpoint/development/custom) to cut down on fetch calls. |

`LiteralTokenSource` is *fixed* (no per-call options); the others are
*configurable* and accept
[`TokenRequestOptions`](https://github.com/livekit/client-sdk-cpp/blob/main/include/livekit/token_source.h)
(room name, participant identity, agent dispatch, ...).

## Configuring the Examples

All inputs come from environment variables, so no secrets or token server IDs are committed to source.

| Variable | Used by | Notes |
| --- | --- | --- |
| `LIVEKIT_URL` | literal, custom | WebSocket URL, e.g. `ws://localhost:7880`. |
| `LIVEKIT_TOKEN` | literal, custom | Participant JWT. |
| `LIVEKIT_TOKEN_ENDPOINT` | endpoint, caching | Token endpoint URL. Default `http://127.0.0.1:3000/createToken`. |
| `LIVEKIT_TOKEN_ENDPOINT_METHOD` | endpoint, caching | Optional HTTP method (default `POST`). |
| `LIVEKIT_TOKEN_ENDPOINT_HEADERS` | endpoint, caching | Optional newline-separated `Name: Value` headers. |
| `LIVEKIT_TOKEN_SERVER_ID` | development | Required. Development token server ID from LiveKit Cloud. |
| `LIVEKIT_AGENT_NAME` | development | Optional agent to dispatch into the room. |
| `LIVEKIT_AGENT_METADATA` | development | Optional metadata for the dispatched agent. |

These examples require LiveKit C++ SDK **v1.3.0** or newer. Build them with the
rest of the repo — see the root [README](../README.md#building-the-examples).

## Running the Examples

### Prerequisites

#### LiveKit Server

Start a development server via:

```bash
livekit-server --dev
```

#### Token Sources

For literal and custom, generate a development token with the
[LiveKit CLI](https://docs.livekit.io/home/cli/cli-setup/) (a dev server started
with `livekit-server --dev` uses `devkey` / `secret`):

```bash
export LIVEKIT_TOKEN=$(lk token create \
  --api-key devkey --api-secret secret \
  -i my-participant --join --room my-room \
  --valid-for 24h --token-only)
```

For the endpoint and caching examples, run a local token server such as
[token-server-node](https://github.com/livekit-examples/token-server-node). This can be run via:

```bash
cd <path-to>/token-server-node
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_API_KEY=devkey LIVEKIT_API_SECRET=secret PORT=3000 pnpm start
```

For development, enable the **Development Token Server** in [LiveKit Cloud](https://cloud.livekit.io), then copy the token server ID
(`token-server-xxxxxx`). See the
[development token server docs](https://docs.livekit.io/frontends/build/authentication/sandbox-token-server/)
for setup details. Do not use this in production.

```bash
export LIVEKIT_TOKEN_SERVER_ID=token-server-xxxxxx
# optional: dispatch a registered agent into the room with metadata
# export LIVEKIT_AGENT_NAME=my-agent
# export LIVEKIT_AGENT_METADATA='{"greeting": "hello from cpp"}'
```

### Executing

The built binaries live under `build/token_source/`:

```bash
# Literal: bring your own URL + token
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<participant-jwt>
./build/token_source/token_source_literal
```

```bash
# Endpoint / caching: point at your token endpoint
export LIVEKIT_TOKEN_ENDPOINT=http://127.0.0.1:3000/createToken
./build/token_source/token_source_endpoint
./build/token_source/token_source_caching
```

```bash
# Development: development-only, ID from the environment
export LIVEKIT_TOKEN_SERVER_ID=<your-token-server-id>
export LIVEKIT_AGENT_NAME=<your-agent-name>         # optional
export LIVEKIT_AGENT_METADATA=<your-agent-metadata> # optional
./build/token_source/token_source_development
```

```bash
# Custom: callback returns credentials (this example reads the env)
export LIVEKIT_URL=ws://localhost:7880
export LIVEKIT_TOKEN=<participant-jwt>
./build/token_source/token_source_custom
```
