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

### Application-wide UI scaling

The Mainframe uses one persistent application UI scale across the top command bar, native Tempest docks, floating panels, and the inherited OBS interface. The compact **- / percentage / +** controls in the Command Nexus and every Tempest dock support 60% through 160% scaling in 10% steps. The native Zoom In/Zoom Out shortcuts (`Ctrl++` or `Ctrl+=`, and `Ctrl+-`) plus `Ctrl+0` increase, decrease, and reset the scale from anywhere in the application; `Ctrl` plus the mouse wheel provides the same adjustment while a Tempest dock is under the pointer.

Scaling adjusts application typography plus Tempest control dimensions, panel minimums, maximums, margins, layout spacing, and stylesheet metrics together. Dynamically added dock controls inherit the current scale, and each Tempest surface retains horizontal and vertical overflow scrolling so a compact dock remains usable even at large text sizes. The command bar progressively hides secondary telemetry before its operational controls can be crowded out.

The saved percentage is restored before the workspace appears. With **Auto-size window on startup** enabled, the Mainframe restores a normal window from its 1920x1080 workstation baseline and automatically adopts the active display's aspect ratio on ultrawide screens, up to 32:9. It expands with the selected UI scale, accounts for window borders and the taskbar, clamps to the available work area, and centers itself. Full-screen sessions keep their restored state; operators who prefer a maximized window can disable auto-sizing in the Layout Director.

The Command Nexus **Layout** button opens the native **Dock Layout Director**. It manages the single application UI scale, startup auto-sizing, visibility, docked or floating state, and focus for the complete Tempest dock suite, with Stream Information included whenever Twitch authentication makes that dock available. **Recover Command Layout** returns all panels to the canonical Command workspace without discarding the operator's accessibility scale.

### Responsive workspace profiles

The Layout Director maintains independent Command and Engineering dock states for **Standard 16:9**, **Ultrawide**, and **Super Ultrawide** displays. Automatic mode detects the active monitor from its available work-area aspect ratio, saves the outgoing profile (including floating dock geometry), restores the matching profile after a monitor move, and identifies the active profile in the Command Nexus. Manual profile selection is available for unusual monitor arrangements.

With **Live Dock Reflow** enabled, the Command workspace uses compact, standard, and wide breakpoints. Compact windows tab the Media Bay behind the Audio Mixer to protect the canvas and primary controls; standard and wide windows split those panels again and allocate progressively more width to the Transmission Matrix and right-side command stack. Reflow only runs when a breakpoint changes, so ordinary resizing does not continually disturb the layout. **Canvas Safe-area Guides** exposes OBS's broadcast-safe 16:9 canvas guides directly in the Layout Director, which is particularly useful when the application workspace itself is ultrawide.

### Twitch Stream Information dock

The standard authenticated OBS/Twitch **Stream Information** dock remains the owner of title, Go Live notification, category, audience, tags, language, content labels, rerun, and branded-content controls. When base OBS creates its `twitchInfo` dock, TBS detects it asynchronously, docks it into the right-side Mainframe operations stack beside the Control Deck, Sequence Director, Asset Vault, and HUD Composer, and includes it in both Command and Engineering workspace state. Its normal Docks menu action remains available.

OBS only compiles this authenticated service dock when a Twitch developer client ID and matching obfuscation hash are supplied at configure time. The current private build does not embed third-party credentials, so the integration slot is present but the dock will not instantiate until TBS is configured with its own Twitch application credentials.

## Transmission Command Matrix

Command mode replaces the conventional Scenes and Sources column with the native **Transmission Command Matrix**. Engineering mode retains those original OBS docks.

The matrix provides:

- a persistent **Basic** view containing only the dynamically refreshed scene grid for direct routing, responsively reflowing from one through four columns,
- a separate **Protocol** view containing the large Starting, Live, BRB, and Ending automation controls,
- a saved OBS scene assignment for each protocol,
- automatic assignment when a collection contains a single scene,
- synchronization with the corresponding Control Deck overlay mode,
- optional Starting Soon countdown activation,
- optional isolation of the matching Tempest overlay source inside the routed scene,
- current-scene highlighting and protocol status feedback.

The selected view is stored locally. Only one control surface is shown at a time, so scene destinations are not duplicated below the protocol controls.

## Signal Media Bay

The native **Signal Media Bay** sits beside the Audio Mixer in the Command workspace and is also available from the Docks menu. It discovers every OBS source that exposes controllable-media transport, including Media Source, VLC Video Source, and Image Slide Show inputs.

The bay provides:

- a persistent source selector that follows source additions, removals, and renames;
- play/pause, restart, stop, previous, and next transport controls;
- live playback state, elapsed time, total duration, and seeking;
- safe empty-state guidance when a scene collection has no controllable media; and
- native OBS media control with no browser bridge or external player.

This makes a fractal visualizer, avatar sequence, or intro playlist controllable from the same Tempest workstation surface used to route the broadcast.

## Mainframe Sequence Director

The **Mainframe Sequence Director** shares the right-side Command workspace bay with the Tempest Control Deck. It stores independent editable cue stacks for Starting, Live, BRB, and Ending.

Each timestamped cue can independently:

- control a Media Source, VLC Video Source, or Image Slide Show;
- show, hide, or toggle a named video source in the active scene, including grouped scene items;
- write transmission, status, and lore lines to the matching Control Deck overlay; and
- hand off into any Starting, Live, BRB, or Ending protocol after its other actions complete.

The transport provides run/restart, hold/resume, next-cue, and stop controls with a live sequence clock and progress display. Cue stacks are saved locally in the isolated TBS configuration. The initial Starting stack contains three safe lore cues based on the First Storm, archive recovery, and the Mainframe's preservation question; it contains no media, visibility, or protocol handoff actions until the operator assigns them.

The Director registers native hotkeys for running each of the four stacks plus hold/resume, next cue, restart, and stop. These can be assigned to Stream Deck buttons under **Settings > Hotkeys** by filtering for `Tempest Mainframe`.

Protocol and scene switching uses the native OBS scene and transition system. It requires no WebSocket bridge, browser control server, macro plugin, or external automation utility.

## Mainframe Asset Vault

The native **Mainframe Asset Vault** shares the Command workspace bay with the Control Deck and Sequence Director. It indexes operator-selected folders recursively without copying, moving, renaming, decoding, or modifying the files it finds. No folders are selected by default.

The Vault provides:

- incremental indexing for MP4, MOV, MKV, WebM, AVI, M4V, and GIF visual assets;
- filename and folder search plus persistent Fractal, Avatar, Lore, Interruption, and Unassigned banks;
- direct folder access for locating an indexed file on disk;
- deliberate load and preview through one reusable OBS Media Source named `Tempest // Asset Bus`; and
- one-click creation of a timestamped asset cue in the Sequence Director's currently selected stack.

Folder selection and rescanning are read-only operations. TBS creates or changes the Asset Bus only when **Load / Preview on Asset Bus** or **Add to Current Sequence** is pressed. The bus is added to the current scene once and reused for later assets, avoiding a separate OBS source for every indexed video. Asset cues retain their own file path, so the Director loads the correct video onto the bus immediately before executing that cue.

## Mainframe HUD Composer

The native **Mainframe HUD Composer** creates transparent reactive visual elements that remain ordinary OBS Browser Sources. Every deployed element can be selected, moved, resized, cropped, grouped, hidden, and locked with the standard OBS canvas and Sources controls; the graphic is not baked into the background video.

The initial local HUD library contains:

- a canvas-sized **Signal Frame** with reactive rails, corners, glow, and transmission labels;
- a movable **Chat Terminal** that can load a Twitch popout chat or browser-overlay URL;
- an editable **Transmission Plate**; and
- an editable **Now Playing** plate for future Media Bay state; and
- a movable **Storm Horizon Radio** browser console for an AzuraCast public-player or embed URL.

Custom signal plates can be added from the same dock. Each definition stores its element type, primary and secondary text, optional browser URL, accent color, reaction mode, reaction strength, and independent Starting, Live, BRB, and Ending visibility. Reaction modes include signal pulse, glow, breathing core, and peak glitch. All local elements use the Control Deck's existing audio telemetry and require no additional audio capture plugin.

Definitions and renderer files are created locally without modifying a scene. **Add Selected to Current Scene** deliberately creates or reuses one named Browser Source, while **Deploy All HUD Elements** adds the complete library to the active scene. New plates deploy unlocked at practical starting positions. The full-canvas frame deploys locked so its transparent center does not intercept normal canvas selection; it can be unlocked through the standard Sources list.

When a Starting, Live, BRB, or Ending protocol routes a scene, the Command Matrix applies the visibility switches saved for each HUD element found in that scene. Browser-backed presets use their local standby renderer while the URL is empty. Adding an HTTP or HTTPS Twitch chat URL applies the Tempest chat shell and Twitch cleanup CSS; adding an AzuraCast URL to the Storm Horizon Radio preset applies its cyan-violet console, now-playing metadata, album-art, controls, and animated live-state treatment. URLs are stored only in the local TBS configuration.

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
- `RunSequence` — `{ "sequence": "starting|live|brb|ending" }`
- `ControlSequence` — `{ "action": "hold|resume|toggleHold|next|restart|stop" }`

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
