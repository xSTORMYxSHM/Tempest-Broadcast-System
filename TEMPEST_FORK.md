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
