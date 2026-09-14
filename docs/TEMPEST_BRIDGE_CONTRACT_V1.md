# Tempest Broadcast Bridge Contract 1.0

Tempest Broadcast System exposes a versioned vendor contract through the bundled OBS WebSocket server. The contract
is intended for Data Horizon, control surfaces, automation tools, and other local clients.

## Connection and safety

- Configure OBS WebSocket normally and keep its authentication enabled.
- Use the vendor name `tempest-mainframe`.
- `GetContract` and `GetBroadcastState` are read-only and remain available while external control is locked.
- State-changing requests require **Scene Control > Allow authenticated WebSocket control**. This switch is off by
  default and is stored only in the local Broadcast configuration.
- Responses never contain stream keys, service credentials, signing identity, or local file paths.

Clients should call `GetContract` after connecting and reject an unsupported `contractVersion`. Additive fields may be
introduced without changing the major contract version. Existing fields and request names will not be repurposed
within version 1.x.

## Calling a vendor request

Use the standard OBS WebSocket `CallVendorRequest` request with:

```json
{
  "vendorName": "tempest-mainframe",
  "requestType": "GetContract",
  "requestData": {}
}
```

Every vendor response includes:

```json
{
  "accepted": true,
  "message": "Human-readable result",
  "contractVersion": "1.0",
  "vendor": "tempest-mainframe"
}
```

When control is locked, a state-changing request returns `accepted: false` and
`errorCode: "control-disabled"` without changing Broadcast state.

## Discovery requests

### `GetContract`

Returns the product version, contract version, control state, authentication recommendation, supported requests, and
vendor events. Each request entry declares `access` as either `read` or `control`.

### `GetBroadcastState`

Returns:

- active scene name and UUID;
- streaming, recording, replay-buffer, and virtual-camera activity;
- base/output canvas dimensions and frame-rate numerator/denominator;
- the scene inventory as stable UUID/name pairs;
- whether state-changing vendor requests are currently enabled.

Scene UUIDs are preferred over names for saved Data Horizon mappings because users can rename scenes.

## Control requests

### `RunProtocol`

```json
{ "protocol": "starting" }
```

`protocol` is one of `starting`, `live`, `brb`, or `ending`.

### `RouteScene`

```json
{ "sceneUuid": "preferred-stable-uuid", "sceneName": "optional fallback" }
```

At least one identifier is required. UUID takes precedence when both are supplied.

### `SetOverlayState`

```json
{
  "mode": "live",
  "transmission": "Optional heading",
  "status": "Optional status",
  "messages": "Optional rotating message content",
  "startCountdown": false
}
```

### `RunSequence`

```json
{ "sequence": "starting" }
```

### `ControlSequence`

```json
{ "action": "next" }
```

Supported actions are `hold`, `resume`, `toggleHold`, `next`, `restart`, and `stop`.

### `TriggerSignal`

```json
{ "strength": 0.9 }
```

`strength` must be between 0.05 and 1.5.

### `TriggerReactionEvent`

```json
{
  "type": "follow",
  "name": "Incoming event",
  "strength": 0.9,
  "durationMs": 2200,
  "circuit": "alerts",
  "accent": "#45D9FF",
  "effect": "pulse",
  "origin": "data-horizon",
  "dedupeId": "provider-event-id",
  "cooldownMs": 0
}
```

### `ClearReactionEvent`

Takes an empty request object and restores the external reaction state.

## Vendor events

The contract advertises its current event list through `GetContract`. Every emitted event includes:

- `contractVersion` and `vendor`;
- a unique `eventId`;
- an ISO 8601 UTC `emittedAt` timestamp;
- event-specific data such as scene UUID, protocol, signal strength, or reaction information.

Clients should ignore unknown additive fields and event names. Events are observations; receiving one is not proof
that a queued command completed unless the event semantics explicitly say so.
