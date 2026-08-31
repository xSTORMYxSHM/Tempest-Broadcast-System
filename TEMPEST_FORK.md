# Tempest Broadcast System

Tempest Broadcast System is an open-source, Tempest-branded Windows live production workstation. It is built from OBS Studio and retains the upstream OBS license, authorship, and plugin compatibility.

Current Tempest product version: **1.0.1**. The separate OBS engine version remains visible in the title bar so upstream compatibility can still be diagnosed accurately.

## Identity and isolation

- Executable: `tempest-broadcast-system.exe`
- Product: `Tempest Broadcast System`
- Company: `Tempest Mainframe`
- User configuration root: `tempest-broadcast-system`
- Windows instance mutex: `TempestBroadcastSystemCore`
- Default theme: `com.tempestmainframe.Broadcast`
- Upstream automatic updates and the What's New feed are disabled.

The fork can run beside a normal OBS Studio installation without sharing profiles, scene collections, logs, crash reports, or plugin-manager settings.

Public UI labels use plain streaming terms. Compatibility-sensitive identifiers retain their original names, including the `tempest-mainframe` WebSocket vendor, `Protocol_*` configuration keys, request/event type names, and existing `Tempest HUD //` and `Tempest Vault //` source prefixes. This lets older scene collections, Stream Deck mappings, Studio clients, and saved layouts continue to work after the terminology update.

## Tempest Studio Integration

The native **Studio Integration** dock is Broadcast's authenticated adapter to Tempest Studio. It discovers Studio's local token file, connects to `ws://127.0.0.1:4765/v1/socket`, reconnects automatically, advertises its capabilities, publishes OBS output health, and accepts these Studio workflow commands:

- `broadcast.reaction.trigger`
- `broadcast.reaction.clear`
- `broadcast.audio.play`
- `broadcast.visual.show`
- `broadcast.visual.hide`
- `broadcast.status`

Studio owns viewer-facing Twitch OAuth, EventSub, chat, rewards, cheers, subscriptions, Sound Alerts normalization, deduplication, cooldowns, and cross-program workflow routing. Broadcast continues to own OBS/Twitch stream-service authentication, Stream Information, streaming credentials, scenes, sources, and output state.

Broadcast health includes a read-only inventory of live OBS audio sources and visual sources in the current scene. Studio uses that inventory to offer source selectors in its Sound Alert editor; the alert assignments themselves remain stored and managed only by Studio.

Reaction commands are idempotent by workflow run, action, and phase. Studio owns every Sound Alert definition and resolves the OBS audio, effect, and popup sources before issuing commands. Broadcast only executes those fully resolved commands and installs local lease timers so temporary effects and popups are restored even if Studio disconnects before sending a release command. Missing audio and visual sources are reported as degraded actions rather than crashing the broadcast. Video, Spout, NDI, and continuous audio frames remain outside the JSON Bridge.

The dock participates in application-wide UI scaling and is available from the standard Docks menu. Its token field stores only the path to Studio's token file; the token itself is loaded for the active connection and is not copied into Broadcast's profile configuration.

## Source and release workflow

- Development branch: `tempest-main`
- Official OBS remote: `upstream`
- No publishing remote is currently configured; public binaries must ship beside a complete source archive from the exact release commit.

The public release requirements, clean-build process, privacy boundary, and known preview limitations are documented in `PUBLIC_RELEASE.md`.

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

Upstream OBS updates are handled by the guarded stable-tag workflow in [OBS_UPSTREAM_UPDATES.md](OBS_UPSTREAM_UPDATES.md). It checks, prepares, builds, validates, and applies an update through an isolated worktree; it never replaces Tempest with an official OBS binary package. Do not enable the OBS binary updater for this fork.

## Stream Overlay

The built-in **Stream Overlay** dock manages reactive Starting Soon, Live, BRB, and Ending overlays. It is available from the Docks menu and appears automatically the first time a profile sees it.

The deck provides:

- editable heading and stream status;
- rotating message lines with an adjustable interval;
- a persistent start/restart/clear countdown;
- automatic generation of a transparent 1920x1080 HTML overlay; and
- automatic export of its nine visual layers as independent reactive browser assets; and
- one-click creation or updating of the `Tempest // Starting Soon` Browser Source in the active scene.

The generated overlay is stored beneath the active Tempest configuration root at:

```text
tempest-broadcast-system\control-deck\starting-soon.html
```

Edits are saved to the Tempest profile and automatically refresh an existing linked Browser Source. The overlay uses no external network assets.

The layer exports are stored in `control-deck\vault-elements`: scanlines, stream frame, reactive core, stream header, title plate, countdown, rotating message, spectrum, and footer plate. They read the saved Starting Soon state even when Stream Overlay is currently editing Live, BRB, or Ending mode.

The rotating-message element is backed by the shared `vault-elements\rotating-lines.txt` asset instead of a multiline dock editor. The first run merges and deduplicates previously saved lines from every overlay mode, preserving custom content. Users can open the library from Stream Overlay or Asset Library and add one message per line; blank lines and `#` comments are ignored. Unmarked lines are active, `[x] Message` explicitly enables a line, and `[ ] Message` keeps an entry available in the library without displaying it. `@playlist name` begins a named section; ungrouped lines and `@playlist common` form the shared section. Active assembled overlays and the independent rotating-message asset poll the file, so saving it updates the live rotation without regenerating or refreshing a Browser Source. Messages supplied by Sequence Director or Studio workflows append unique entries to the profile's common section.

The **Message playlist** selector provides **Auto**, **Common Only**, **All Sections**, each overlay mode, and any custom section discovered in the shared or profile library. Auto combines common lines with the section matching Starting Soon, Live, BRB, or Ending. Existing files remain compatible because all lines before the first directive are treated as common.

Rotating lines also accept `{{variable}}` placeholders. Shared values live in `vault-elements\message-variables.json`, while profile values live beside the profile message library in `profiles\<profile-id>\message-variables.json` and override matching shared keys. Both JSON assets are polled live, so a saved value can update the currently visible line without regenerating or refreshing the Browser Source. The scaled **Message Variable Manager** edits text, numeric, and Boolean values visually; shows each value's shared or current-profile scope; moves values between scopes; and preserves descriptions, structured values, and other advanced JSON it does not edit. Direct access to both portable JSON files remains available from the manager. Built-in values `{{time}}`, `{{date}}`, `{{profile}}`, `{{mode}}`, `{{title}}`, and `{{status}}` are displayed as read-only because they always update from the active overlay state. Unknown placeholders remain visible, making spelling or missing-data errors easy to diagnose.

An optional metadata block at the end of any line controls only that message: `Welcome {{channel}} [[duration=8 accent=#45d9ff effect=pulse]]`. `duration` accepts seconds or an `ms` suffix and is safely clamped from 2 to 60 seconds; `accent` accepts three- or six-digit hex colors; and `effect` accepts `none`, `pulse`, `glitch`, or `alert`. Metadata is removed before the text is displayed. Plain lines continue using the profile rotation speed, Tempest palette, and no added effect. Reduced-motion mode disables the optional animations while retaining the selected accent.

The Stream Overlay dock includes a **Compose Message** dialog for creating these lines without writing metadata manually. It targets common, mode-specific, or custom playlists; can add an active or disabled line; previews the selected accent and effect; and appends the portable result to the active profile. **Manage Profile Messages** provides a visual table for enabling, disabling, editing, duplicating, reordering, and removing existing profile entries while preserving comments and playlist directives. Existing entries reopen in a scaled visual editor with their state, duration, accent, and effect populated; unrecognized future metadata is shown and round-tripped unchanged. The underlying portable text asset remains available through **Open Text File** for advanced or bulk editing.

### Stream content profiles

Stream Overlay stores reusable content profiles beneath `control-deck\profiles\<profile-id>`. Each folder contains a `profile.json` document, a profile-specific `rotating-lines.txt`, and a `message-variables.json` placeholder asset. The JSON stores its display name, active overlay mode, rotation interval, message order, selected playlist, countdown duration, headings, statuses, and independent countdown state for Starting Soon, Live, BRB, and Ending. Message order can be `sequential`, `random`, or `shuffle-bag`; Shuffle Bag plays the complete selected list before reshuffling and prevents an immediate repeat across bag boundaries. `includeGlobalMessages` controls whether the shared `vault-elements\rotating-lines.txt` library is merged ahead of the profile-specific list. Duplicate active messages are removed case-insensitively while preserving their first occurrence.

The compact profile selector provides **New**, **Duplicate**, and **Open Folder** actions. New profiles start from public-safe defaults; Duplicate copies the current content, profile-specific message library, and variable asset while clearing active countdowns. The selected profile is restored on startup. Editing its JSON, message file, or variables outside Broadcast is watched and applied live, and Sequence Director or Studio workflow messages append to the active profile's common playlist. Profile JSON and text files are indexed as editable assets in Asset Library.

Existing installations migrate into a **Default** profile on first launch. Their mode text, timer settings, countdown state, and global rotating-message library remain intact; migration does not delete the legacy configuration keys, preserving downgrade and compatibility options.

The same folder also contains modular elements for assembling alternate layouts: an orbit badge, telemetry plate, stream ticker, alert popup, vertical audio rail, and creator lower third. These work without external services by using the saved Starting Soon text, local clock, and Broadcast audio telemetry.

The generated **Radio Player** element is a transparent Tempest component that accepts a user-supplied AzuraCast station URL. It renders track title, artist, cover art, Auto DJ/live state, listener count, elapsed time, duration, progress, and a custom play/pause control without displaying the stock hosted widget. The shell remains driven by local Broadcast telemetry and shared external-event accents.

### Overlay modes

The mode selector maintains independent saved content, generated files, countdown state, and Browser Sources for:

- `Tempest // Starting Soon`
- `Tempest // BRB`
- `Tempest // Stream Ending`
- `Tempest // Live Overlay`

Select the intended mode, edit its content, switch to the scene that should receive it, and use the mode-specific **Create / Update Source** button.

### Audio reactivity

The native **Audio Reactor** can attach its independent Desktop Energy and Microphone / Voice inputs to any audio-capable OBS sources. It samples their peak meters, derives a music-transient Beat input, and publishes lightweight local telemetry:

```text
tempest-broadcast-system\control-deck\telemetry.json
```

Each generated overlay receives the same telemetry through the native browser event bridge. Generated elements use the input for both visible motion or intensity and a shared color response: silence rests at Tempest cyan, then stronger audio moves through electric violet toward magenta while increasing glow. Scanlines, countdowns, rotating messages, footer plates, stream tickers, and radio-player shells have dedicated opacity, scale, lift, pulse, fill, or framing responses in addition to rings, meters, title pulses, and equalizer bars. No local network server, WebSocket connection, or Browser Source reload is required. Use **Refresh** after adding or removing audio sources.

The Audio Reactor's **Reactivity Director** is the persistent global tuning surface for the assembled Stream Overlay modes and generated overlay elements. It provides Calm, Standard, High Energy, Alert Dance, and Alert Warning presets; Tempest, Ultraviolet, Ember, Verdant, and full-spectrum palettes; noise threshold; attack and release; independent motion and glow percentages; an adjustable test signal; and a reduced-motion mode that retains color and glow. Selecting a preset does not discard hand-tuned values until **Apply Preset** is pressed.

The same telemetry contract carries Studio, Warudo, and Twitch reaction-event accents. An active event can temporarily override the normal palette, and the existing Sound Alert Dance event selects full-spectrum color behavior. Director changes are consumed live without regenerating or reloading Browser Sources.

Rendering any Stream Overlay change regenerates all four assembled mode files plus the element exports, so inactive mode files do not remain on an older reactive template.

## Tempest Broadcast workstation shell

The default 1920x1080 workspace is headed by the native **Tempest Broadcast control bar**. It provides:

- persistent Command and Engineering workspaces,
- an armed stream safety state before streaming can begin,
- direct stream and recording controls backed by the existing OBS output system,
- a confirmed force-stop action that leaves local recording intact,
- current-scene, render FPS, CPU, render-lag, recording, and stream-time telemetry,
- a Command layout optimized for a large program canvas with scenes, sources, audio, and Stream Overlay,
- one-click access to the full conventional OBS frontend in Engineering mode.

Each workspace stores its dock layout independently. Plugins and advanced dialogs remain available in Engineering mode while Command mode presents the Tempest-first operating surface.

### Application-wide UI scaling

Tempest Broadcast uses one persistent application UI scale across the top control bar, native Tempest docks, floating panels, and the inherited OBS interface. The compact **- / percentage / +** controls in the control bar and every Tempest dock support 60% through 160% scaling in 10% steps. The native Zoom In/Zoom Out shortcuts (`Ctrl++` or `Ctrl+=`, and `Ctrl+-`) plus `Ctrl+0` increase, decrease, and reset the scale from anywhere in the application; `Ctrl` plus the mouse wheel provides the same adjustment while a Tempest dock is under the pointer.

Scaling adjusts application typography plus control dimensions, panel minimums, maximums, margins, layout spacing, and stylesheet metrics together. Every inherited OBS dialog, wizard, plug-in tool window, and native floating dock captures its unscaled geometry and control metrics when opened, applies the saved percentage, and clamps the resulting frame to the active monitor's available work area. Dynamically added controls inherit the current scale. Tempest surfaces retain horizontal and vertical overflow scrolling so a compact dock remains usable even at large text sizes, and the command bar progressively hides secondary telemetry before its operational controls can be crowded out. Full-screen preview and projector windows are deliberately excluded so broadcast imagery keeps its requested pixel geometry.

The saved percentage is restored before the workspace appears. With **Auto-size window on startup** enabled, Broadcast restores a normal window from its 1920x1080 workstation baseline and automatically adopts the active display's aspect ratio on ultrawide screens, up to 32:9. It expands with the selected UI scale, accounts for window borders and the taskbar, clamps to the available work area, and centers itself. Full-screen sessions keep their restored state; users who prefer a maximized window can disable auto-sizing in Workspace Layout.

The control bar's **Layout** button opens **Workspace Layout**. It manages the single application UI scale, startup auto-sizing, visibility, docked or floating state, and focus for the complete Tempest dock suite, with Stream Information included whenever Twitch authentication makes that dock available. **Recover Command Layout** returns all panels to the standard Command workspace without discarding the user's accessibility scale.

Workspace Layout also provides an application-wide color selector. **Tempest Cyan** remains the default, with Ultraviolet, Neon Magenta, Ember, Emerald, Ice Blue, and a full custom-color picker available as persistent alternatives. The selected accent is derived into coordinated highlight, border, surface, and muted-text tones and is reapplied after UI-scale or dock-layout changes. Recording, error, and warning colors keep their standard operational meaning. This setting changes Broadcast's application chrome only; stream overlays and Browser Source artwork retain their own reactive palettes.

### Responsive workspace profiles

The Layout Director maintains independent Command and Engineering dock states for **Standard 16:9**, **Ultrawide**, and **Super Ultrawide** displays. Automatic mode detects the active monitor from its available work-area aspect ratio, saves the outgoing profile (including floating dock geometry), restores the matching profile after a monitor move, and identifies the active profile in the Command Nexus. Manual profile selection is available for unusual monitor arrangements.

With **Live Dock Reflow** enabled, the Command workspace uses compact, standard, and wide breakpoints. Compact windows tab Media Controls behind the Audio Mixer to protect the canvas and primary controls; standard and wide windows split those panels again and allocate progressively more width to Scene Control and the right-side dock stack. Source Operations shares Scene Control's left-side dock bay as a tab by default, so separating detailed source work does not reduce canvas width. Reflow only runs when a breakpoint changes, so ordinary resizing does not continually disturb the layout. **Canvas Safe-area Guides** exposes OBS's broadcast-safe 16:9 canvas guides directly in Workspace Layout, which is particularly useful when the application workspace itself is ultrawide.

### Interactive window performance

On Windows, an interactive move or resize temporarily pauses presentation of the local GPU preview and any visible native browser-dock surface. Responsive workspace detection is held until the drag finishes, and preview color-space refreshes are coalesced rather than requested for every moved pixel. The last composed workstation frame remains available to Windows during the drag; preview and browser panels resume immediately on release. This affects only local interface presentation: OBS source rendering, audio processing, streaming, and recording continue normally.

### Twitch Stream Information dock

The standard authenticated OBS/Twitch **Stream Information** dock remains the owner of title, Go Live notification, category, audience, tags, language, content labels, rerun, and branded-content controls. When base OBS creates its `twitchInfo` dock, TBS detects it asynchronously, docks it into the right-side operations stack beside Stream Overlay, Sequence Director, Asset Library, and Overlay Designer, and includes it in both Command and Engineering workspace state. Its normal Docks menu action remains available.

OBS only compiles this authenticated service dock when a Twitch developer client ID and matching obfuscation hash are supplied at configure time. Public packages do not embed personal credentials, so the integration slot is present but the dock will not instantiate until TBS is configured with its own Twitch application credentials.

## Scene Control

Command mode replaces the conventional Scenes and Sources column with native **Scene Control** and a companion **Source Operations** dock. Engineering mode retains the original OBS docks.

The matrix provides:

- a persistent **Basic** view containing the dynamically refreshed scene grid for direct routing, responsively reflowing from one through four columns,
- a native active-scene source tree with visibility, lock state, ordering, selection, and the essential add, remove, properties, move-up, and move-down controls,
- a separate **Automation** view containing the large Starting, Live, BRB, and Ending controls,
- a saved OBS scene assignment for each automation,
- automatic assignment when a collection contains a single scene,
- synchronization with the corresponding Stream Overlay mode,
- optional Starting Soon countdown activation,
- optional isolation of the matching Tempest overlay source inside the routed scene,
- current-scene highlighting and automation status feedback.

The selected view is stored locally. Only one control surface is shown at a time, so scene destinations are not duplicated below the automation controls.

The separate **Source Operations** dock follows the source selected in Scene Control. It contains source-aware Fit, Center, Reset, Properties, Filters, Rename, Duplicate, Interact, Refresh, media transport, and audio mute actions, plus the collapsible **Precision Layout** and **Reactive Binding** consoles. Its transform fields, snapping, nudging, layout snapshots, and audio-effect binding remain connected to the Scene Control selection even when the dock is floated or moved to another dock bay. Source Operations has independent overflow scrolling and participates in the single persistent application scale. In the recovered Command layout it is tabbed behind Scene Control on the left; it is also independently visible, floatable, lockable, and focusable from the Docks menu and Workspace Layout.

## Media Controls

Native **Media Controls** sit beside the Audio Mixer in the Command workspace and are also available from the Docks menu. They discover every OBS source that exposes controllable-media transport, including Media Source, VLC Video Source, and Image Slide Show inputs.

The bay provides:

- a persistent source selector that follows source additions, removals, and renames;
- play/pause, restart, stop, previous, and next transport controls;
- live playback state, elapsed time, total duration, and seeking;
- safe empty-state guidance when a scene collection has no controllable media; and
- native OBS media control with no browser bridge or external player.

This makes a fractal visualizer, avatar sequence, or intro playlist controllable from the same Tempest workstation surface used to route the broadcast.

## Sequence Director

The **Sequence Director** shares the right-side Command workspace bay with Stream Overlay. It stores independent editable cue stacks for Starting, Live, BRB, and Ending.

Each timestamped cue can independently:

- control a Media Source, VLC Video Source, or Image Slide Show;
- show, hide, or toggle a named video source in the active scene, including grouped scene items;
- write heading, status, and message lines to the matching Stream Overlay; and
- hand off into any Starting, Live, BRB, or Ending automation after its other actions complete.

The transport provides run/restart, hold/resume, next-cue, and stop controls with a live sequence clock and progress display. Cue stacks are saved locally in the isolated TBS configuration. The initial Starting stack contains three neutral setup cues; it contains no media, visibility, or automation handoff actions until the user assigns them.

The Director registers native hotkeys for running each of the four stacks plus hold/resume, next cue, restart, and stop. These can be assigned to Stream Deck buttons under **Settings > Hotkeys** by filtering for `Tempest Broadcast`.

Automation and scene switching use the native OBS scene and transition system. They require no WebSocket bridge, browser control server, macro plugin, or external automation utility.

## Asset Library

The native **Asset Library** shares the Command workspace bay with Stream Overlay and Sequence Director. It indexes user-selected folders recursively without copying, moving, renaming, decoding, or modifying the files it finds. No user folders are selected by default; the generated Stream Overlay element folder is included automatically.

The Asset Library provides:

- incremental indexing for MP4, MOV, MKV, WebM, AVI, M4V, GIF, and local HTML overlay assets;
- filename and folder search plus persistent Fractal, Avatar, Text, Alert, Overlay, and Unassigned collections;
- direct folder access for locating an indexed file on disk;
- deliberate load and preview through one reusable OBS Media Source named `Tempest // Asset Bus`; and
- creation or updating of metadata-sized `Tempest Vault // ...` Browser Sources for HTML overlays; and
- one-click creation of a timestamped asset cue in the Sequence Director's currently selected stack.

Folder selection and rescanning are read-only operations. TBS creates or changes the Asset Bus only when **Preview Selected Asset** or **Add to Current Sequence** is pressed. The bus is added to the current scene once and reused for later assets, avoiding a separate OBS source for every indexed video. Asset cues retain their own file path, so the Director loads the correct video onto the bus immediately before executing that cue.

The Asset Library watches configured roots and their subdirectories. File changes schedule a short debounced rescan, allowing other local authoring tools to publish HTML overlays into the built-in `vault-elements` folder without requiring the user to press **Rescan Library**. Packages use a stable unique HTML filename and `tempest-size` metadata, so repeated publishing updates one correctly sized `Tempest Vault // ...` Browser Source instead of colliding through a generic `index.html` name.

## Overlay Designer

The native **Overlay Designer** creates transparent reactive visual elements that remain ordinary OBS Browser Sources. Every deployed element can be selected, moved, resized, cropped, grouped, hidden, and locked with the standard OBS canvas and Sources controls; the graphic is not baked into the background video.

The initial local overlay library contains:

- a canvas-sized **Stream Frame** with reactive rails, corners, glow, and stream labels;
- a movable **Chat Panel** that can load a Twitch popout chat or browser-overlay URL;
- an editable **Info Plate**;
- an editable **Now Playing** element for future Media Controls state; and
- a movable **Radio Player** for a user-supplied AzuraCast public-player or embed URL.

Custom info plates can be added from the same dock. Each definition stores its element type, primary and secondary text, optional browser URL, accent color, reaction mode, reaction strength, and independent Starting, Live, BRB, and Ending visibility. Reaction modes include audio pulse, glow, breathing core, and peak glitch. All local elements use the Audio Reactor's telemetry and require no additional audio capture plugin.

Definitions and renderer files are created locally without modifying a scene. **Add Selected to Current Scene** deliberately creates or reuses one named Browser Source, while **Deploy All Overlays** adds the complete library to the active scene. New plates deploy unlocked at practical starting positions. The full-canvas frame deploys locked so its transparent center does not intercept normal canvas selection; it can be unlocked through the standard Sources list.

When a Starting, Live, BRB, or Ending automation routes a scene, Scene Control applies the visibility switches saved for each overlay element found in that scene. Browser-backed presets use their local standby renderer while the URL is empty. Adding an HTTP or HTTPS Twitch chat URL applies the Tempest chat shell and Twitch cleanup CSS. The Radio Player remains a local reactive Tempest renderer. Its user-supplied AzuraCast station URL is used only to derive the now-playing API and audio-stream endpoints; track, artist, cover art, live state, listeners, timing, progress, and playback controls are rendered in the native Tempest shell. Raw AzuraCast stream URLs are migrated to the matching station endpoint automatically. URLs are stored only in the local TBS configuration.

### Stream automation actions

Scene Control's **Configure Automation Actions** editor stores an independent native action sequence for each Starting, Live, BRB, and Ending command. An automation can:

- wait up to ten seconds before routing its assigned scene;
- select an OBS transition and duration;
- mute, unmute, or preserve two chosen audio sources;
- play, pause, restart, stop, move to the previous or next item, or preserve a chosen media source;
- start, stop, or preserve local recording; and
- optionally launch a chosen local executable with arguments.

Actions default to **KEEP** or disabled, so adding the feature does not alter existing audio, recording, or application state. A direct scene route or newer automation command cancels any older delayed route. Program launching uses Qt's detached process API directly and does not invoke a command shell.

## Event Router

Scene Control registers four persistent native OBS hotkeys:

- `Tempest Broadcast: Run STARTING Automation`
- `Tempest Broadcast: Run LIVE Automation`
- `Tempest Broadcast: Run BRB Automation`
- `Tempest Broadcast: Run ENDING Automation`

Assign them under **Settings > Hotkeys** by filtering for `Tempest Broadcast`. A Stream Deck can then use its ordinary Hotkey action to execute complete Tempest automations; no Stream Deck plugin is required. TBS continues to expose the standard OBS hotkeys for streaming, recording, replay buffer, scene items, and other conventional controls.

### OBS WebSocket commands

When the bundled OBS WebSocket module is available, TBS registers the authenticated vendor `tempest-mainframe`. Enable and secure the server under **Tools > WebSocket Server Settings** before connecting another local program. The router does not open a second port or bypass OBS WebSocket authentication.

Send an OBS WebSocket `CallVendorRequest` with one of these vendor request types:

- `RunProtocol` — `{ "protocol": "starting|live|brb|ending" }`
- `RouteScene` — `{ "sceneUuid": "..." }` or `{ "sceneName": "..." }`
- `SetOverlayState` — `{ "mode": "starting|live|brb|ending", "transmission": "...", "status": "...", "messages": "line one\nline two", "startCountdown": false }`
- `RunSequence` — `{ "sequence": "starting|live|brb|ending" }`
- `ControlSequence` — `{ "action": "hold|resume|toggleHold|next|restart|stop" }`
- `TriggerSignal` — `{ "strength": 0.05..1.5 }`
- `TriggerReactionEvent` — typed Warudo/Twitch event with optional strength, duration, circuit, accent, effect, origin, dedupe ID, and cooldown
- `ClearReactionEvent` — immediately clears the active external reaction event

Warudo setup and request examples are documented in [TEMPEST_WARUDO_BRIDGE.md](TEMPEST_WARUDO_BRIDGE.md).

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

The vendor emits `ProtocolExecuted`, `SceneRouted`, and `OverlayStateUpdated` events so connected control surfaces can reflect completed stream-state changes. Scene Control reports whether both the hotkey and OBS WebSocket paths registered successfully.
