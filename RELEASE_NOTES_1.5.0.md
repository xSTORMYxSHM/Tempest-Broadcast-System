# Tempest Broadcast System 1.5.0

Tempest Broadcast System 1.5.0 coordinates the Broadcast compositor with Tempest Streaming Studio 1.5.0 and adds
native dual-format production assets, startup recovery, and local runtime observability.

## Dual-format production

- Generates native 1920x1080 landscape and 1080x1920 portrait versions of every Stream Overlay mode.
- Exports a complete portrait-safe set of modular Asset Library elements instead of cropping landscape layouts.
- Routes portrait-marked assets to the active Tempest Portrait scene and scales them proportionally for supported
  portrait canvas sizes.
- Keeps Twitch Enhanced Broadcasting responsible for sending the selected horizontal and portrait canvases through
  the primary Twitch stream. Independent portrait RTMP outputs remain opt-in.

## Studio reconnect recovery

- Detects active Tempest Studio Browser Sources that remained disconnected after Studio became available.
- Queries Studio's authenticated status APIs and reinitializes only sources that explicitly report zero connected
  clients.
- Preserves source identity, scene references, transforms, audio routing, and operator-owned inactive sources.
- Bounds recovery to one reinitialization per source per Broadcast process.

## Telemetry compatibility

- Publishes an atomic local `telemetry/runtime.json` snapshot once per second without requiring the OBS WebSocket
  server to be enabled.
- Exposes OBS-compatible CPU, memory, FPS, render-time, render-lag, encoder-lag, stream-output, recording-output,
  congestion, reconnect, byte, frame, and dropped-frame counters.
- Keeps the authenticated OBS WebSocket interface optional and leaves its existing enabled/disabled state unchanged.

## Compatibility and quality policy

- Preserves Studio Bridge protocol `1.0` and Viewer Interaction render contract `1.0`.
- Keeps existing Twitch Enhanced Broadcasting resolution, frame-rate, bitrate, encoder, and additional-canvas
  settings. The validated production session showed 0.0336% render lag and 0.0442% worst encoder skip, with no
  quality-reduction trigger.
- Leaves Tempest Portrait Backtrack under operator control as an optional GPU/VRAM headroom saving.
- The upstream OBS engine base remains 32.2.1.

## Release-candidate gate

Before publication, the signed installed build must pass a clean cross-app rehearsal covering Twitch alert audio,
interaction alert audio, Emote Wall, DiceBox completion, Studio reconnect recovery, Twitch Enhanced Broadcasting with
horizontal and portrait canvases, and live telemetry/log review.
