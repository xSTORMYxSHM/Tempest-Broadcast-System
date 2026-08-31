# Tempest Broadcast System public release

Tempest Broadcast System is an independent, Tempest-themed live production workstation built from OBS Studio. It keeps the OBS scene, source, plugin, encoder, recording, and streaming engine while adding Scene Control, Stream Overlay, reactive overlay tools, an Asset Library, an Audio Reactor, and optional Tempest Studio integration.

The supported public target is 64-bit Windows 10 or Windows 11. Version 1.00.0 is the first release whose previously unsigned Windows executable code is signed and timestamped through Microsoft Trusted Signing.

## Install and first run

1. Download the Windows x64 binary ZIP and its matching SHA-256 checksum.
2. Extract the complete ZIP into a new folder. Do not run the executable from inside the archive.
3. Start `bin/64bit/tempest-broadcast-system.exe`.
4. Confirm that Windows reports a valid digital signature before first launch. SmartScreen reputation can still take time to develop for a new publisher certificate.

Tempest Broadcast stores its user configuration in `%APPDATA%\tempest-broadcast-system`, independently from `%APPDATA%\obs-studio`. Public packages do not contain the publisher's profiles, accounts, scenes, browser cookies, tokens, logs, media files, or generated overlays.

To migrate an existing OBS setup without replacing the Tempest interface, close both applications and copy `basic\scenes`, `basic\profiles`, and only the required compatible folders from `plugin_config` into `%APPDATA%\tempest-broadcast-system`.

## Network behavior

- Normal OBS streaming, service authentication, browser sources, updateable service lists, and plugins can make network requests selected by the operator.
- The optional Tempest Studio Integration connects only to its configured WebSocket endpoint, which defaults to `ws://127.0.0.1:4765/v1/socket`. Its token is read from the local Studio token file and is not copied into Broadcast profile configuration.
- Radio Player overlays connect only to the AzuraCast station URL supplied by the user for metadata, cover artwork, and audio playback.
- Upstream OBS automatic application updates and the upstream What's New feed are disabled. Public Tempest releases do not currently include an automatic updater.

## Known limitations

- Official packages contain Authenticode-signed executable code. The ZIP files themselves are authenticated by their published SHA-256 checksums and GitHub release provenance.
- Twitch OAuth and the authenticated Stream Information dock require Tempest-owned Twitch application credentials at build time. Builds without those credentials can still use a manually supplied stream key.
- Tempest Studio is optional, but Studio-routed Twitch events and Sound Alerts require a compatible Studio installation.
- AJA and DeckLink modules can report harmless initialization failures on systems without their vendor runtimes or hardware.
- Third-party OBS plugins are not bundled unless explicitly listed in a release. Users are responsible for checking plugin compatibility with the displayed OBS engine version.

## License and source

Tempest Broadcast System is distributed under GNU GPL version 2 or, at your option, any later version. It is a modified OBS Studio distribution and is not affiliated with or endorsed by the OBS Project.

Each binary release must publish all of the following together:

- `tempest-broadcast-system-<version>-windows-x64.zip`
- `tempest-broadcast-system-<version>-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- versioned release notes naming the Tempest version, OBS engine version, source commit, unsigned/signed status, and known limitations

The source archive must be generated from the same clean commit used for the binary and must include the pinned contents of every Git submodule. Publishing only a patch against OBS is not the release policy for this project.

## Publisher checklist

- Commit all intended source and documentation changes.
- Confirm `git status --short` is empty.
- Tag the exact commit with `tempest-v<version>`.
- Build on a clean Windows environment with PowerShell 7 using `pwsh -File scripts/Build-PublicRelease.ps1 -Sign`.
- Test first launch with `%APPDATA%\tempest-broadcast-system` absent or temporarily renamed.
- Verify preview, recording, streaming, browser sources, audio monitoring, hotkeys, UI scaling, dock recovery, scene switching, and shutdown.
- Scan the binary and source archives for personal paths, credentials, scene collections, logs, cookies, and generated assets.
- Confirm the public binary archive contains no PDB, crash-dump, log, or user-configuration files.
- Confirm the release builder reports a valid Authenticode signature for every shipped `.exe`, `.dll`, and `.pyd` file before it creates the final archive.
- Upload the binary, corresponding source, checksum file, NOTICE, and release notes together.
