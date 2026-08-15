# Tempest Broadcast System

Tempest Broadcast System is a private Windows broadcast workstation for the Tempest Mainframe. It is built from OBS Studio and retains the upstream OBS license, authorship, and plugin compatibility.

## Identity and isolation

- Executable: `tempest-broadcast-system.exe`
- Product: `Tempest Broadcast System`
- Company: `Tempest Mainframe`
- User configuration root: `tempest-broadcast-system`
- Windows instance mutex: `TempestBroadcastSystemCore`
- Default theme: `com.tempestmainframe.Broadcast`
- Upstream automatic updates and the What's New feed are disabled.

The fork can run beside a normal OBS Studio installation without sharing profiles, scene collections, logs, crash reports, or plugin-manager settings.

## Local source workflow

- Private branch: `tempest-main`
- Official OBS remote: `upstream`
- No personal or publishing remote is configured.

Configure the existing Windows build tree with:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build_x64 -DENABLE_WHATSNEW=OFF
```

Build with:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-x64 --parallel
```

The development build is written to:

```text
build_x64\rundir\RelWithDebInfo\bin\64bit\tempest-broadcast-system.exe
```

Upstream OBS updates should be reviewed and merged deliberately into `tempest-main`; do not enable the OBS binary updater for this fork.

## Tempest Control Deck

The built-in **Tempest Control Deck** dock manages the reactive Starting Soon overlay. It is available from the Docks menu and appears automatically the first time a profile sees it.

The deck provides:

- editable transmission title and operator status;
- rotating lore/status lines with an adjustable interval;
- a persistent start/restart/clear countdown;
- automatic generation of a transparent 1920x1080 HTML overlay; and
- one-click creation or updating of the `Tempest // Starting Soon` Browser Source in the active scene.

The generated overlay is stored beneath the active Tempest configuration root at:

```text
tempest-broadcast-system\control-deck\starting-soon.html
```

Edits are saved to the Tempest profile and automatically refresh an existing linked Browser Source. The overlay uses no external network assets.

### Overlay modes

The mode selector maintains independent saved content, generated files, countdown state, and Browser Sources for:

- `Tempest // Starting Soon`
- `Tempest // BRB`
- `Tempest // Stream Ending`
- `Tempest // Live HUD`

Select the intended mode, edit its content, switch to the scene that should receive it, and use the mode-specific **Create / Update Source** button.

### Audio reactivity

The Reactive Audio selector can attach to any audio-capable OBS source. The native Control Deck samples its peak meter and writes a lightweight local telemetry stream at 20 Hz:

```text
tempest-broadcast-system\control-deck\telemetry.json
```

Each generated overlay reads that file locally and uses the signal to drive its core glow, archive rings, title pulse, and equalizer bars. No network server, WebSocket connection, or Browser Source reload is required. Use **Refresh** after adding or removing audio sources.

## Mainframe workstation shell

The default 1920x1080 workspace is headed by the native **Mainframe Command Nexus**. It provides:

- persistent Command and Engineering workspaces,
- an armed transmission safety state before streaming can begin,
- direct stream and recording controls backed by the existing OBS output system,
- a confirmed emergency uplink cut that leaves local recording intact,
- current-scene, render FPS, CPU, render-lag, archive, and transmission-time telemetry,
- a Command layout optimized for a large program canvas with scenes, sources, audio, and the Tempest Control Deck,
- one-click access to the full conventional OBS frontend in Engineering mode.

Each workspace stores its dock layout independently. Plugins and advanced dialogs remain available in Engineering mode while Command mode presents the Tempest-first operating surface.

## Transmission Command Matrix

Command mode replaces the conventional Scenes and Sources column with the native **Transmission Command Matrix**. Engineering mode retains those original OBS docks.

The matrix provides:

- large Starting, Live, BRB, and Ending protocol controls,
- a saved OBS scene assignment for each protocol,
- automatic assignment when a collection contains a single scene,
- synchronization with the corresponding Control Deck overlay mode,
- optional Starting Soon countdown activation,
- optional isolation of the matching Tempest overlay source inside the routed scene,
- a dynamically refreshed two-column scene grid for direct routing,
- current-scene highlighting and protocol status feedback.

## Signal Media Bay

The native **Signal Media Bay** sits beside the Audio Mixer in the Command workspace and is also available from the Docks menu. It discovers every OBS source that exposes controllable-media transport, including Media Source, VLC Video Source, and Image Slide Show inputs.

The bay provides:

- a persistent source selector that follows source additions, removals, and renames;
- play/pause, restart, stop, previous, and next transport controls;
- live playback state, elapsed time, total duration, and seeking;
- safe empty-state guidance when a scene collection has no controllable media; and
- native OBS media control with no browser bridge or external player.

This makes a fractal visualizer, avatar sequence, or intro playlist controllable from the same Tempest workstation surface used to route the broadcast.

Protocol and scene switching uses the native OBS scene and transition system. It requires no WebSocket bridge, browser control server, macro plugin, or external automation utility.

### Protocol action automation

The matrix's **Configure Protocol Actions** editor stores an independent native action sequence for each Starting, Live, BRB, and Ending command. A protocol can:

- wait up to ten seconds before routing its assigned scene;
- select an OBS transition and duration;
- mute, unmute, or preserve two chosen audio sources;
- play, pause, restart, stop, move to the previous or next item, or preserve a chosen media source;
- start, stop, or preserve local recording; and
- optionally launch a chosen local executable with arguments.

Actions default to **KEEP** or disabled, so adding the feature does not alter existing audio, recording, or application state. A direct scene route or newer protocol command cancels any older delayed route. Program launching uses Qt's detached process API directly and does not invoke a command shell.

## Mainframe Event Router

The Transmission Command Matrix registers four persistent native OBS hotkeys:

- `Tempest Mainframe: Run STARTING Protocol`
- `Tempest Mainframe: Run LIVE Protocol`
- `Tempest Mainframe: Run BRB Protocol`
- `Tempest Mainframe: Run ENDING Protocol`

Assign them under **Settings > Hotkeys** by filtering for `Tempest`. A Stream Deck can then use its ordinary Hotkey action to execute complete Tempest protocols; no Stream Deck plugin is required. TBS continues to expose the standard OBS hotkeys for streaming, recording, replay buffer, scene items, and other conventional controls.

### OBS WebSocket commands

When the bundled OBS WebSocket module is available, TBS registers the authenticated vendor `tempest-mainframe`. Enable and secure the server under **Tools > WebSocket Server Settings** before connecting another local program. The router does not open a second port or bypass OBS WebSocket authentication.

Send an OBS WebSocket `CallVendorRequest` with one of these vendor request types:

- `RunProtocol` — `{ "protocol": "starting|live|brb|ending" }`
- `RouteScene` — `{ "sceneUuid": "..." }` or `{ "sceneName": "..." }`
- `SetOverlayState` — `{ "mode": "starting|live|brb|ending", "transmission": "...", "status": "...", "messages": "line one\nline two", "startCountdown": false }`

For example, the request data passed to `CallVendorRequest` is:

```json
{
  "vendorName": "tempest-mainframe",
  "requestType": "RunProtocol",
  "requestData": {
    "protocol": "live"
  }
}
```

The vendor emits `ProtocolExecuted`, `SceneRouted`, and `OverlayStateUpdated` events so connected control surfaces can reflect completed Mainframe state changes. The Command Matrix reports whether both the hotkey and OBS WebSocket paths registered successfully.
