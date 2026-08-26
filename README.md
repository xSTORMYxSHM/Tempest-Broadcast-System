# Tempest Broadcast System

Tempest Broadcast System is an independent Windows live-production workstation built from OBS Studio. It retains OBS scene, source, encoder, recording, streaming, and plugin compatibility while adding a Tempest-focused command workspace, reactive overlays, asset management, automation, and optional Tempest Studio integration.

Version **0.20.0** is the first public preview and inherits the OBS Studio **32.1.2** engine. The supported public target is 64-bit Windows 10 or Windows 11.

## Highlights

- Command and Engineering workspaces with responsive dock layouts
- Scene Control, Source Operations, Media Controls, and stream-safety controls
- Stream Overlay profiles and modular browser-source elements
- Asset Library, Overlay Designer, Audio Reactor, and Sequence Director
- Application-wide scaling and configurable Tempest interface accents
- Optional authenticated localhost connection to Tempest Studio
- Separate `%APPDATA%\tempest-broadcast-system` configuration, allowing installation beside OBS Studio

## Build

Clone with submodules:

```powershell
git clone --recurse-submodules <repository-url>
cd "Tempest Broadcast System"
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo --parallel 1
```

The Windows preset currently targets Visual Studio 2026 with the newest installed Windows SDK (10.0.20348.0 or later).

Detailed architecture and development notes are in [TEMPEST_FORK.md](TEMPEST_FORK.md). Release installation, privacy, network behavior, known limitations, and the publisher checklist are in [PUBLIC_RELEASE.md](PUBLIC_RELEASE.md).

The final tagged release is packaged with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Build-PublicRelease.ps1
```

The release script requires a clean commit tagged `tempest-v0.20.0` and produces a Windows binary archive, complete corresponding source archive including pinned submodules, checksums, a release manifest, notices, and release notes.

## OBS foundation and independence

This project is based on [OBS Studio](https://github.com/obsproject/obs-studio). Tempest Broadcast System is independent and is not affiliated with or endorsed by the OBS Project. The OBS engine version remains visible separately for compatibility diagnosis.

## License

Tempest Broadcast System and its OBS-derived code are free software under the **GNU General Public License version 2 or, at your option, any later version** (`GPL-2.0-or-later`). See [COPYING](COPYING), [LICENSE](LICENSE), [NOTICE.txt](NOTICE.txt), [AUTHORS](AUTHORS), and dependency-specific notices. The software is provided without warranty.
