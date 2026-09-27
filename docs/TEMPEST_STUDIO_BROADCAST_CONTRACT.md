# Tempest Studio / Broadcast production contract

Protocol version: `1.0`

Viewer interactions use the additive
[`Tempest interaction render contract 1.0`](TEMPEST_INTERACTION_RENDER_CONTRACT_V1.md). The Bridge transport remains
protocol `1.0` for backward compatibility.

Studio sends commands to `com.tempestmainframe.tempest-broadcast`. Command inputs are always under
`payload.arguments`. Broadcast responses include `commandId`, `success`, `status`, and `detail` and never include
stream keys or access tokens.

## Capabilities

- `broadcast.dual-format.configure`
- `broadcast.dual-format.preview`
- `broadcast.simulcast.configure`
- `broadcast.simulcast.preflight`
- `broadcast.simulcast.start`
- `broadcast.simulcast.stop`
- `broadcast.simulcast.retry-kick`
- `broadcast.status`
- `broadcast.interaction.show`
- `broadcast.interaction.update`
- `broadcast.interaction.clear`

The existing reaction, audio, and visual capabilities remain unchanged.

## Status schema

`broadcast.status.dualFormat` contains:

- `supported`, `enabled`, `enhancedBroadcastingEnabled`, `additionalCanvasSelected`
- `canvas`: `id`, `name`, `baseWidth`, `baseHeight`, `outputWidth`, `outputHeight`, `fpsNumerator`,
  `fpsDenominator`
- `sceneLinksReady`, `linkedScenes`, `totalScenes`
- `audioReady`, `browserSourcesReady`, `previewAvailable`
- optional `lastError`

`broadcast.status.simulcast` contains:

- `supported`, `enabled`, `configured`, `credentialsStored`, `secureStorageAvailable`
- `kickServerConfigured`, `twitchServiceReady`, `dualFormatReady`, `sharedEncoder`
- `uploadCapacityConfigured`, `uploadCapacityKbps`, `estimatedRequiredKbps`, `uploadHeadroomReady`
- `recordingWithStream`, `lastPreflightAt`, `lastPreflightPassed`, optional `lastPreflightError`
- `twitch` and `kick`: `state`, `active`, `bytesSent`, `droppedFrames`, `totalFrames`, `congestion`, optional
  `lastError`
- optional `lastError`

Broadcast estimates required upload with 20 percent headroom. A successful preflight is valid for four hours.

## Command arguments

`broadcast.dual-format.configure` accepts Studio's `enabled`, `enhancedBroadcasting`, `additionalCanvas`,
`selectAsAdditionalCanvas`, `preserveCanvasWhenDisabled`, and `verticalBrowserSources` envelope. Broadcast selects an
existing 1080x1920 or 720x1280 canvas; it does not delete or resize canvases. Disabling preserves the selected canvas.

`broadcast.dual-format.preview` accepts `orientation: "vertical"` and opens the existing portrait workspace.

`broadcast.simulcast.configure` accepts `enabled`, optional `kickServer`, optional `kickStreamKey`,
`keepStoredKey`, optional `uploadCapacityKbps`, and optional `recordingWithStream`. It is rejected while an output is
active or preparing.

`broadcast.simulcast.preflight` accepts no arguments and is rejected while on air. It verifies the destination,
encoder, output module, Dual Format readiness, protected credential storage, and upload headroom without opening a
network connection.

`broadcast.simulcast.start` accepts optional `recording` and required `operatorChecklistAccepted: true`. It requires a
successful, fresh preflight and uses the same coordinated start path as the Broadcast Go Live control.

`broadcast.simulcast.stop` accepts `scope: "all" | "kick"` and optional `force`. Kick-only stop leaves Twitch live.

`broadcast.simulcast.retry-kick` accepts no arguments and is available only while Twitch is live and Kick is offline.

## Ownership

Broadcast is the source of truth for output configuration and live state:

- Kick ingest URL, protected Kick stream key, Kick encoder and bitrates, and upload capacity live in the active
  Broadcast profile. Stream keys are protected with Windows DPAPI for the current Windows account.
- Recording-with-stream uses OBS's native `BasicWindow/RecordWhenStreaming` setting; Studio does not own a duplicate.
- Enhanced Broadcasting selection, the additional canvas UUID, scene links, portrait browser sources, and program
  audio readiness are owned by Broadcast and its scene collection.
- Studio owns operator intent, validation before dispatch, the rehearsal checklist, and transient incident history.
  It may collect settings for one command, but must not persist a second authoritative copy or echo credentials into
  logs, status, or UI state after dispatch.

All configuration and preflight commands are off-air operations. Start, stop, and Kick retry are explicit operator
commands; Broadcast never starts an output merely because Studio connects.
