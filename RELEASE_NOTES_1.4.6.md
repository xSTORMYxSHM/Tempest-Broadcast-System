# Tempest Broadcast System 1.4.6

Tempest Broadcast System 1.4.6 adds the Broadcast compositor half of Tempest Streaming Studio Viewer Interactions.

## Viewer interaction rendering

- Adds the interaction render contract `1.0` over the existing Studio Bridge protocol `1.0`.
- Adds `show`, `update`, and `clear` lifecycle commands with idempotent activation identity.
- Maps normalized placement independently to the current horizontal scene and its linked portrait scene.
- Supports contain, cover, and stretch fit modes plus bounded rotation.
- Restores the exact prior scene-item transform and visibility on clear, replacement, rejection, or fallback expiry.
- Keeps Bits validation, viewer permissions, groups, counters, cooldowns, queue policy, assets, and audit history in
  Tempest Streaming Studio.

## Audio reliability

- Uses source-native, single-path interaction audio and does not synthesize a duplicate cue.
- Does not refresh browser sources during interaction lifecycle updates.
- Restarts controllable media only when Studio explicitly requests it, at most once per show command.
- Preserves operator-owned monitoring and audio-track routing.

Existing 1.4.5 reaction, sound-alert, visual-source, Dual Format, and simulcast commands remain compatible.
