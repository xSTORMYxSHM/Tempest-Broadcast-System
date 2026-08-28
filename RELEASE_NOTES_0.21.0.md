# Tempest Broadcast System 0.21.0

Tempest Broadcast System 0.21.0 is a Windows x64 public-preview release built on OBS Studio 32.2.1. It brings the public build onto the current validated OBS engine while preserving the Tempest production workspace, reactive overlay system, application-wide scaling, performance controls, and isolated user configuration.

## Highlights

- Upgraded the underlying OBS engine from 32.1.2 to 32.2.1, including current capture, encoder, browser, WebSocket, audio, and frontend fixes.
- Added a guarded maintainer workflow for future OBS engine upgrades. Upstream changes are prepared in an isolated G-drive worktree, compiled and validated before a fast-forward-only apply to the Tempest branch.
- Preserved Tempest branding, `%APPDATA%\tempest-broadcast-system` isolation, public terminology, disabled upstream application updates, and the custom About experience across the engine upgrade.
- Added the persistent **Canvas On / Canvas Off** command-bar control. Canvas Off suspends local preview presentation while streaming, recording, projectors, scene rendering, and audio continue.
- Added performance throttling for hidden overlay previews, countdown UI, command-bar telemetry, interactive window movement, and preview color-space refreshes.
- Added application-wide color palettes and persistent UI scaling from 60% through 160%, including responsive dock minimums and automatic 1080p, 1440p, and ultrawide workspace profiles.
- Expanded **Scene Control** with the active scene's native source tree and separated detailed source work into **Source Operations**.
- Expanded **Stream Overlay** with portable content profiles, file-backed rotating-message playlists, a visual message editor, shared/profile variables, and independently deployable reactive elements.
- Expanded **Overlay Designer**, **Asset Library**, **Audio Reactor**, **Media Controls**, and **Sequence Director** for movable browser elements, reactive bindings, reusable media routing, and scene-aware automation.
- Added the authenticated localhost **Studio Integration** adapter. Tempest Studio remains the owner of Twitch events and Sound Alert definitions; Broadcast executes resolved audio, popup, and visual-effect actions.

## Engine-upgrade validation

- Built the complete Windows x64 Release configuration sequentially with all pinned OBS submodules.
- Started the release candidate in an isolated portable configuration and confirmed the Tempest 0.21.0 / OBS 32.2.1 identity.
- Confirmed D3D11 initialization on an RTX 3080 and NVENC 13 compatibility with the installed NVIDIA driver.
- Confirmed responsive startup, normal exit code `0`, and zero reported memory leaks.
- No CTest targets are currently registered by this Windows build, so runtime smoke testing remains an explicit release gate.

## Performance guidance

For demanding games, use Canvas Off, render optional fractals at 30 FPS or a lower resolution, and reduce Twitch Enhanced Broadcasting renditions when GPU or VRAM headroom is limited. Twitch Enhanced Broadcasting can create multiple simultaneous NVENC renditions; this is separate from Tempest's local preview cost.

## Known limitations

- This preview is unsigned unless the distributed executable and archive are signed after packaging. Windows SmartScreen may warn on first launch.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Manual stream-key operation remains available without them.
- Tempest Studio is optional, but Studio-routed Twitch interactions and Sound Alerts require a compatible Studio installation.
- AJA and DeckLink modules can log harmless initialization failures on systems without their hardware or vendor runtimes.
- Third-party OBS plugins are not bundled unless explicitly named in the package and must be compatible with OBS Studio 32.2.1.
- Preserve GPU and VRAM headroom when combining demanding games, live fractals, browser overlays, HDR capture, and multiple Twitch Enhanced Broadcasting renditions.

## Distribution

Publish these files together:

- `tempest-broadcast-system-0.21.0-windows-x64.zip`
- `tempest-broadcast-system-0.21.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- this release note

The binary and source archives must come from the same clean commit tagged `tempest-v0.21.0`.
