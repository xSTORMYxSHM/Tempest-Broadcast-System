# Tempest Broadcast System 0.16.0

Tempest Broadcast System 0.16.0 is a Windows x64 public-preview release built on the OBS Studio engine. It expands the custom production workspace, makes the reactive overlay system more portable, and adds controls that reduce local interface overhead during demanding streams.

## Highlights

- Added the persistent **Canvas On / Canvas Off** command-bar control. Canvas Off suspends local preview presentation while streaming, recording, projectors, scene rendering, and audio continue.
- Added performance throttling for hidden overlay previews, countdown UI, command-bar telemetry, interactive window movement, and preview color-space refreshes.
- Added application-wide color palettes and persistent UI scaling from 60% through 160%, including responsive dock minimums and automatic 1080p, 1440p, and ultrawide workspace profiles.
- Expanded **Scene Control** with the active scene's native source tree and separated detailed source work into **Source Operations**.
- Expanded **Stream Overlay** with portable content profiles, file-backed rotating-message playlists, a visual message editor, shared/profile variables, and independently deployable reactive elements.
- Expanded **Overlay Designer**, **Asset Library**, **Audio Reactor**, **Media Controls**, and **Sequence Director** for movable browser elements, reactive bindings, reusable media routing, and scene-aware automation.
- Added the authenticated localhost **Studio Integration** adapter. Tempest Studio remains the owner of Twitch events and Sound Alert definitions; Broadcast executes resolved audio, popup, and visual-effect actions.
- Generalized public-facing terminology while retaining compatibility identifiers used by existing scene collections, hotkeys, Stream Deck mappings, and Studio clients.
- Added isolated `%APPDATA%\tempest-broadcast-system` configuration, public-release packaging, complete corresponding-source archives, notices, dependency licenses, and SHA-256 manifests.

## Performance validation

A monitored Star Citizen and Twitch session showed Tempest Broadcast using approximately 4-6% normalized aggregate CPU and about 1.5 GB aggregate working memory. A live audio fractal was the largest optional GPU load: the RTX 3080 frequently reached 97% utilization and 8.7-9.7 GB of 10 GB VRAM while it was active. The first sample after closing the fractal dropped to 29% GPU utilization, although Star Citizen later returned the GPU to 95-97% under game load.

For demanding games, use Canvas Off, render optional fractals at 30 FPS or a lower resolution, and reduce Twitch Enhanced Broadcasting renditions when GPU or VRAM headroom is limited.

## Known limitations

- This preview is unsigned unless the distributed executable and archive are signed after packaging. Windows SmartScreen may warn on first launch.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Manual stream-key operation remains available without them.
- Tempest Studio is optional, but Studio-routed Twitch interactions and Sound Alerts require a compatible Studio installation.
- AJA and DeckLink modules can log harmless initialization failures on systems without their hardware or vendor runtimes.
- A previous Star Citizen D3D12 capture format transition from an 8-bit to a 10-bit swap chain triggered NVIDIA driver Event 153, D3D11 device removal `887A0006`, failed shared-texture rebuilds, and an `obs-browser.dll` termination. Keep GPU drivers current, avoid unnecessary HDR/SDR mode changes during a live session, and preserve VRAM headroom.
- Twitch Enhanced Broadcasting can create multiple simultaneous NVENC renditions. On the tested profile it created five video encodes totaling approximately 10.2 Mbps before audio and protocol overhead.
- Third-party OBS plugins are not bundled unless explicitly named in the package and must match the displayed OBS engine version.

## Distribution

Publish these files together:

- `tempest-broadcast-system-0.16.0-windows-x64.zip`
- `tempest-broadcast-system-0.16.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- this release note

The binary and source archives must come from the same clean commit tagged `tempest-v0.16.0`.
