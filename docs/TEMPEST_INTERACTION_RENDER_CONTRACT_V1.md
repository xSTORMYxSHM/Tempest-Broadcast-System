# Tempest interaction render contract

Interaction render contract version: `1.0`

Transport remains Tempest Studio / Broadcast Bridge protocol `1.0`. The interaction contract is additive and does
not change the meaning of any 1.4.5 capability.

## Ownership boundary

Tempest Streaming Studio owns the interaction catalog, Twitch/Kick verification, eligibility and access locks,
cooldowns, queue policy, counters, approved assets, safety decisions, and audit history. Studio sends only an already
approved render instance to Broadcast.

Tempest Broadcast owns source lookup, horizontal/vertical canvas mapping, scene-item transforms, visibility, source
activation, fallback expiry, state restoration, and final audio/video output. Broadcast does not interpret Bits,
viewer identity, groups, counters, cooldowns, or queue policy.

## Discovery

Broadcast advertises these additive capabilities in `hello.payload.capabilities`:

- `broadcast.interaction.show`
- `broadcast.interaction.update`
- `broadcast.interaction.clear`

`hello.payload.interactionRenderContractVersion` is `1.0`.
`broadcast.status.interactionRender` reports the contract version, supported orientations and fit modes,
`coordinateSpace: "normalized-top-left"`, `audioPolicy: "source-native-single-path"`, the maximum lease duration,
and the number of active instances.

## Common identity

Studio should send a unique `payload.arguments.instanceId` for every activation and reuse it for that activation's
show, update, and clear commands. If it is absent, Broadcast falls back to `payload.runId/payload.actionId`, then to
the bridge message ID. `interactionId` is catalog identity and must not be used as instance identity when concurrent
activations are possible.

Commands remain idempotent under the Bridge command key (`runId/actionId/phase`). A clear for an instance that is
already clear succeeds with `status: "already-cleared"`.

## Show

Topic: `broadcast.interaction.show`

```json
{
  "payload": {
    "runId": "run-123",
    "actionId": "render-1",
    "phase": "show",
    "lease": { "durationMs": 5000 },
    "arguments": {
      "instanceId": "activation-456",
      "durationMs": 5000,
      "renderTargets": {
        "horizontal": {
          "sourceName": "Tempest Viewer Interaction",
          "placement": { "x": 0.68, "y": 0.08, "width": 0.25, "height": 0.28 },
          "fit": "contain"
        },
        "vertical": {
          "sourceName": "Tempest Viewer Interaction Vertical",
          "placement": { "x": 0.12, "y": 0.10, "width": 0.76, "height": 0.32 },
          "fit": "contain"
        }
      }
    }
  }
}
```

`horizontal` resolves against the current program scene. `vertical` resolves against that scene's link to the
additional portrait canvas selected in the active Broadcast profile. A requested target that cannot be resolved
rejects the entire show and restores any target already touched by that command.

Placement uses normalized top-left coordinates. `x`, `y`, `width`, and `height` are required together, must remain
inside `0..1`, and are mapped independently to each target canvas. Sources with normalized placement must be
top-level scene items. Omitting `placement` keeps the operator-authored transform. `fit` is `contain` (default),
`cover`, or `stretch`; optional `rotationDegrees` is between -360 and 360.

`restartMedia` defaults to `false`. If explicitly true, Broadcast restarts a controllable media source at most once
per show command, even if the same source is referenced more than once. Browser sources are not refreshed by this
contract.

The duration is clamped to 250-300000 ms. Broadcast clears the instance locally when the lease expires so a lost
Studio connection cannot leave an interaction on program indefinitely.

For backward-compatible horizontal-only callers, top-level `sourceName`, `placement`, and `restartMedia` are accepted
as an alias for `renderTargets.horizontal`.

## Update

Topic: `broadcast.interaction.update`

Update accepts the same `instanceId` and a subset of the active `renderTargets`. It may change placement, fit, or
rotation and may reset `durationMs`. It cannot replace an active source; Studio must clear and show to change source
identity. Omitting render targets while sending a duration is valid. Studio should include a monotonically increasing
string or numeric `arguments.revision` for each update. Broadcast includes that revision in its idempotency key; when
it is absent, the bridge message ID is used so later updates are not mistaken for retries of the first update.

## Clear

Topic: `broadcast.interaction.clear`

Clear requires the same instance identity. Broadcast stops fallback expiry and restores every target's exact prior
transform and visibility instead of assuming that the source was previously hidden.

## Responses and observations

Successful responses include `interactionRenderContractVersion: "1.0"` and `instanceId`. Show also returns applied
orientation, source, scene, canvas, and canvas dimensions. Broadcast publishes:

- `broadcast.interaction.shown`
- `broadcast.interaction.updated`
- `broadcast.interaction.cleared`
- `broadcast.interaction.expired`

These events describe compositor state only. Studio remains responsible for the authoritative interaction audit.

## Audio rule

Interaction audio is source-native and single-path. When an approved Studio interaction uses Web Audio in its
browser source, Studio must not also dispatch `broadcast.audio.play` for that activation. Broadcast does not synthesize
a second cue, does not refresh the browser source on show/update, and does not change the source's operator-selected
monitoring mode. The existing `broadcast.audio.play` capability remains available for legacy interactions whose audio
is intentionally supplied by a separate controllable OBS source.

For dual-format production, the operator must configure the horizontal/vertical browser-source pair so only the
intended program-audio source contributes audio; monitoring and track routing remain Broadcast-owned scene/profile
configuration.
