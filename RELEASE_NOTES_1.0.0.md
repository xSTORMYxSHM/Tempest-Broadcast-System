# Tempest Broadcast System 1.0.0

Tempest Broadcast System 1.0.0 is the first signed Windows x64 release. It combines a signed installer and portable package with the stable Tempest production workspace on the validated OBS Studio 32.2.1 engine.

## Signed installer release

- Adds a per-user Windows installer that does not require administrator elevation.
- Installs under `%LOCALAPPDATA%\Programs\Tempest Broadcast System` and registers a normal Apps & Features uninstaller.
- Adds Start Menu shortcuts and offers an optional desktop shortcut.
- Preserves profiles, scenes, credentials, browser state, and other user configuration under `%APPDATA%\tempest-broadcast-system` during upgrades and uninstall.
- Signs and timestamps the generated uninstaller and final installer through Microsoft Trusted Signing.
- Builds the installer from the same signed payload published in the portable ZIP.

## Signed payload

- Every shipped `.exe`, `.dll`, and `.pyd` file must have a valid timestamped Authenticode signature before packaging succeeds.
- Existing valid vendor signatures are preserved; invalid signatures fail the release build.
- SHA-256 checksums are generated after the signed portable archive and installer are finalized.
- The release manifest records the installer filename, signature status, and timestamp status.

## Product baseline

- Uses the OBS Studio 32.2.1 capture, encoder, browser, WebSocket, audio, and frontend engine.
- Preserves Tempest branding, `%APPDATA%\tempest-broadcast-system` isolation, disabled upstream application updates, and the custom production workspace.
- Includes Scene Control, Source Operations, Stream Overlay, Overlay Designer, Asset Library, Audio Reactor, Media Controls, Sequence Director, reactive bindings, and optional authenticated Tempest Studio integration.

## Validation

- Build the complete Windows x64 Release configuration from the tagged source and pinned submodules.
- Verify all executable files in the final portable archive have valid timestamped Authenticode signatures.
- Verify the final installer and the installed uninstaller have valid timestamped Authenticode signatures.
- Perform an isolated silent install, application startup smoke test, and silent uninstall.
- Confirm installer removal does not delete `%APPDATA%\tempest-broadcast-system`.
- Verify all published asset sizes and SHA-256 digests after upload.

## Known limitations

- SmartScreen reputation can take time to develop even when Authenticode signatures are valid.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Manual stream-key operation remains available without them.
- The optional Apple CoreAudio AAC plugin is excluded because Windows Application Control can block that third-party module. FFmpeg AAC remains available.
- Third-party OBS plugins must be compatible with OBS Studio 32.2.1.

## Distribution

Publish these files together:

- `tempest-broadcast-system-1.0.0-windows-x64-installer.exe`
- `tempest-broadcast-system-1.0.0-windows-x64.zip`
- `tempest-broadcast-system-1.0.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- this release note

The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.0.0`.
