# Tempest Warudo reaction bridge

Tempest Broadcast System accepts Warudo and Twitch interaction events through the Mainframe Signal Reactor. The bridge reuses OBS WebSocket and the existing OBS hotkey system, so it does not require another background middleware service.

## Existing `obs-events` plugin path

The Veasu `obs-events` Warudo plugin exposes OBS's **Trigger Hotkey By Name** request. Connect the end of the relevant Warudo blueprint flow to that node and use one of these exact OBS hotkey identifiers:

- `TempestMainframe.ExternalEvent.SoundAlertDance`
- `TempestMainframe.ExternalEvent.TwitchInteraction`

The first event defaults to a six-second magenta spectrum surge across the selected Dance target. The second defaults to a 2.6-second violet glitch on the selected Twitch target. Choose the target circuits and duplicate cooldown in **Mainframe Signal Reactor > External Event Bridge**.

The hotkeys do not need physical key combinations. They only need to remain registered by Tempest Broadcast System; Warudo triggers them by their unique names.

## Rich OBS WebSocket request

Clients that support OBS WebSocket 5 vendor calls can use `CallVendorRequest` with:

```json
{
  "vendorName": "tempest-mainframe",
  "requestType": "TriggerReactionEvent",
  "requestData": {
    "type": "sound_alert_dance",
    "name": "SOUND ALERT // NEON TEMPEST",
    "strength": 1.2,
    "durationMs": 6000,
    "circuit": "all",
    "accent": "#FF3EC8",
    "effect": "spectrum",
    "origin": "warudo",
    "dedupeId": "sound-alert-12345",
    "cooldownMs": 800
  }
}
```

Supported circuits are `all`, `core`, `frame`, `chat`, `plates`, and `alerts`. Supported effects are `pulse`, `glow`, `glitch`, `spectrum`, and `surge`. Duration is limited to 250–30000 ms and cooldown to 0–10000 ms.

Use `ClearReactionEvent` to end the current event immediately:

```json
{
  "vendorName": "tempest-mainframe",
  "requestType": "ClearReactionEvent",
  "requestData": {}
}
```

Tempest emits `ReactionEventTriggered` and `ReactionEventCleared` vendor events for other local tools. The live event state is also included in `control-deck/telemetry.json`, which lets generated Tempest HUD elements switch accent and effect in sync.

## Initial Warudo blueprint wiring

1. In the Sound Alert Dancing blueprint, connect the same execution branch that starts the avatar dance to **Trigger Hotkey By Name**.
2. Enter `TempestMainframe.ExternalEvent.SoundAlertDance` as the hotkey name.
3. In the Twitch interaction blueprint, connect the interaction-success branch to a second **Trigger Hotkey By Name** node.
4. Enter `TempestMainframe.ExternalEvent.TwitchInteraction`.
5. Use **TEST DANCE** and **TEST TWITCH** in the Signal Reactor before testing from Warudo.

Generated local HUD elements react automatically after Tempest Broadcast System starts. Remote chat/radio pages remain controlled by their own websites; their OBS source transforms can still react when they are bound to a Source Reaction circuit in the Transmission Matrix.
