# Tempest Broadcast System public release

Tempest Broadcast System is an independent, Tempest-themed live production workstation built from OBS Studio. It keeps the OBS scene, source, plugin, encoder, recording, and streaming engine while adding Scene Control, Stream Overlay, reactive overlay tools, an Asset Library, an Audio Reactor, and optional Tempest Studio integration.

The supported public target is 64-bit Windows 10 or Windows 11. Version 1.0.0 is the first release whose Windows executable code, per-user installer, and generated uninstaller are signed and timestamped through Microsoft Trusted Signing.

Beginning with version 1.0.1, installed copies include the signed Tempest Broadcast Updater. It checks only final releases from the official Tempest GitHub repository, verifies the release asset's SHA-256 digest, Windows file version, and Authenticode trust, and then runs the signed installer to preserve the existing per-user configuration while updating the application.

Both **Check for Updates** inside Broadcast and the Start Menu's manual updater use that same verified installer path. Running a downloaded installer manually uses the same application-only update behavior. The installer is fixed to `%LOCALAPPDATA%\Programs\Tempest Broadcast System`; it never writes to or removes `%APPDATA%\tempest-broadcast-system`, where preferences, profiles, scenes, plugin configuration, generated overlays, and the managed Asset Vault are stored. Release builds fail if their installer payload contains a configuration folder or a portable-mode marker.

## Install and first run

1. Download the Windows x64 installer and `SHA256SUMS.txt` from the same GitHub release.
2. Confirm the installer checksum and that Windows reports a valid digital signature.
3. Run the installer. It installs for the current user under `%LOCALAPPDATA%\Programs\Tempest Broadcast System`, adds Start Menu shortcuts, and optionally adds a desktop shortcut.
4. Start Tempest Broadcast System from the Start Menu. SmartScreen reputation can still take time to develop for a new publisher certificate.

The portable ZIP remains available for users who do not want an installed copy. Extract the complete ZIP into a new folder, then start `bin/64bit/tempest-broadcast-system.exe`; do not run the executable from inside the archive.

Tempest Broadcast stores its user configuration in `%APPDATA%\tempest-broadcast-system`, independently from `%APPDATA%\obs-studio`. Public packages do not contain the publisher's profiles, accounts, scenes, browser cookies, tokens, logs, media files, or generated overlays.

To migrate an existing OBS setup without replacing the Tempest interface, close both applications and copy `basic\scenes`, `basic\profiles`, and only the required compatible folders from `plugin_config` into `%APPDATA%\tempest-broadcast-system`.

## Network behavior

- Normal OBS streaming, service authentication, browser sources, updateable service lists, and plugins can make network requests selected by the operator.
- The optional Tempest Studio Integration connects only to its configured WebSocket endpoint, which defaults to `ws://127.0.0.1:4765/v1/socket`. Its token is read from the local Studio token file and is not copied into Broadcast profile configuration.
- Radio Player overlays connect only to the AzuraCast station URL supplied by the user for metadata, cover artwork, and audio playback.
- Upstream OBS update services and the upstream What's New feed are disabled. Tempest's signed updater checks the official Tempest GitHub releases instead.

## Known limitations

- Official packages contain Authenticode-signed executable code. The installer and its generated uninstaller are also signed and timestamped. ZIP files themselves are authenticated by their published SHA-256 checksums and GitHub release provenance.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Builds without those credentials can still use a manually supplied stream key.
- Tempest Studio is optional, but Studio-routed Twitch events and Sound Alerts require a compatible Studio installation.
- AJA and DeckLink modules can report harmless initialization failures on systems without their vendor runtimes or hardware.
- Third-party OBS plugins are not bundled unless explicitly listed in a release. Users are responsible for checking plugin compatibility with the displayed OBS engine version.

## License and source

Tempest Broadcast System is distributed under GNU GPL version 2 or, at your option, any later version. It is a modified OBS Studio distribution and is not affiliated with or endorsed by the OBS Project.

Each binary release must publish all of the following together:

- `tempest-broadcast-system-<version>-windows-x64.zip`
- `tempest-broadcast-system-<version>-windows-x64-installer.exe`
- `tempest-broadcast-system-<version>-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- versioned release notes naming the Tempest version, OBS engine version, source commit, unsigned/signed status, and known limitations

The source archive must be generated from the same clean commit used for the binary and must include the pinned contents of every Git submodule. Publishing only a patch against OBS is not the release policy for this project.

## Publisher checklist

- Commit all intended source and documentation changes.
- Confirm `git status --short` is empty.
- Tag the exact commit with `tempest-v<version>`.
- Build on a clean Windows environment with PowerShell 7 and NSIS 3 using `pwsh -File scripts/Build-PublicRelease.ps1 -Sign`.
- Test first launch with `%APPDATA%\tempest-broadcast-system` absent or temporarily renamed.
- Verify preview, recording, streaming, browser sources, audio monitoring, hotkeys, UI scaling, dock recovery, scene switching, and shutdown.
- Scan the binary and source archives for personal paths, credentials, scene collections, logs, cookies, and generated assets.
- Use only the `Tempest Mainframe` public brand in repository pages, release notes, notices, manifests, and executable version metadata. Keep personal identity and signing-account details confined to private signing configuration, except for identity information that Authenticode must expose to verify the publisher.
- Confirm the public binary archive contains no PDB, crash-dump, log, or user-configuration files.
- Confirm an upgrade over a populated test profile leaves `%APPDATA%\tempest-broadcast-system` byte-for-byte unchanged except for settings that Broadcast itself writes during a normal clean shutdown.
- Confirm the release builder reports a valid Authenticode signature for every shipped `.exe`, `.dll`, and `.pyd` file before it creates the final archive.
- Install silently into an isolated location, verify the installed executable and generated uninstaller signatures, launch the application, and confirm silent uninstall leaves user configuration untouched.
- Upload the installer, portable binary, corresponding source, checksum file, manifest, NOTICE, release guide, and release notes together.
