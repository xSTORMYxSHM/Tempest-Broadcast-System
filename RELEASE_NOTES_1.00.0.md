# Tempest Broadcast System 1.00.0

Tempest Broadcast System 1.00.0 is the first signed Windows x64 release. It establishes the stable public baseline on OBS Studio 32.2.1 while preserving the Tempest production workspace, reactive overlay system, application-wide scaling, performance controls, and isolated user configuration.

## Signed-release milestone

- Previously unsigned Windows executable code in the public package is signed and timestamped through Microsoft Trusted Signing.
- Existing valid vendor signatures are preserved, and the release builder rejects invalid signatures.
- Packaging now fails unless every shipped `.exe`, `.dll`, and `.pyd` file has a valid Authenticode signature when signed mode is requested.
- SHA-256 checksums are generated only after signed binaries are placed into the final archive.
- The release manifest records signed status so installers and users can distinguish this release from the earlier unsigned previews.

## Product baseline

- Uses the validated OBS Studio 32.2.1 capture, encoder, browser, WebSocket, audio, and frontend engine.
- Preserves Tempest branding, `%APPDATA%\tempest-broadcast-system` isolation, public terminology, disabled upstream application updates, and the custom About experience.
- Includes the persistent **Canvas On / Canvas Off** command-bar control for suspending local preview presentation without stopping streaming, recording, projectors, scene rendering, or audio.
- Includes application-wide color palettes and persistent UI scaling from 60% through 160%, with responsive dock minimums and automatic 1080p, 1440p, and ultrawide workspace profiles.
- Includes **Scene Control**, **Source Operations**, **Stream Overlay**, **Overlay Designer**, **Asset Library**, **Audio Reactor**, **Media Controls**, and **Sequence Director**.
- Includes portable overlay content profiles, file-backed rotating-message playlists, reusable media routing, reactive bindings, scene-aware automation, and independently deployable reactive browser elements.
- Includes the authenticated localhost **Studio Integration** adapter. Tempest Studio remains the owner of Twitch events and Sound Alert definitions; Broadcast executes resolved audio, popup, and visual-effect actions.

## Validation

- Build the complete Windows x64 Release configuration sequentially with all pinned OBS submodules.
- Verify the Tempest 1.00.0 / OBS 32.2.1 identity in an isolated portable configuration.
- Verify every executable payload in the final package has a valid Authenticode signature and trusted timestamp.
- Exercise preview, recording, streaming, browser sources, audio monitoring, hotkeys, UI scaling, dock recovery, scene switching, and normal shutdown before publication.
- No CTest targets are currently registered by this Windows build, so runtime smoke testing remains an explicit release gate.

## Performance guidance

For demanding games, use Canvas Off, render optional fractals at 30 FPS or a lower resolution, and reduce Twitch Enhanced Broadcasting renditions when GPU or VRAM headroom is limited. Twitch Enhanced Broadcasting can create multiple simultaneous NVENC renditions; this is separate from Tempest's local preview cost.

## Known limitations

- SmartScreen reputation can take time to develop even when Authenticode signatures are valid. Users should confirm the digital signature and published SHA-256 checksum.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Manual stream-key operation remains available without them.
- Tempest Studio is optional, but Studio-routed Twitch interactions and Sound Alerts require a compatible Studio installation.
- AJA and DeckLink modules can log harmless initialization failures on systems without their hardware or vendor runtimes.
- The Windows public package excludes the optional Apple CoreAudio AAC plugin because Windows Application Control can block that third-party module at startup. FFmpeg AAC remains available.
- Third-party OBS plugins are not bundled unless explicitly named in the package and must be compatible with OBS Studio 32.2.1.
- Preserve GPU and VRAM headroom when combining demanding games, live fractals, browser overlays, HDR capture, and multiple Twitch Enhanced Broadcasting renditions.

## Distribution

Publish these files together:

- `tempest-broadcast-system-1.00.0-windows-x64.zip`
- `tempest-broadcast-system-1.00.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- this release note

The binary and source archives must come from the same clean commit tagged `tempest-v1.00.0`.
