# Tempest Broadcast System 1.1.0

Tempest Broadcast System 1.1.0 expands Audio Reactor with first-class internal radio and media routing and improves custom installation-folder support.

## Reactive radio and media

- Adds a dedicated **Internal radio / media** Audio Reactor input alongside desktop audio and microphone inputs.
- Automatically detects internal Storm Horizon Radio, VLC, media, music, and player sources without replacing existing source selections.
- Includes the radio/media channel in Master Mix reactions and music-transient beat detection.
- Adds **Radio / Media** as an audio-input choice for reactive HUD elements.
- Defaults newly created Radio Player elements to their dedicated radio/media signal.
- Keeps audio-bearing Asset Vault and HUD Composer browser elements under Broadcast mixer control so they can be metered and muted normally.
- Migrates existing Tempest browser elements containing audio into Broadcast-controlled audio routing when a scene collection loads.

## Audio Reactor usability

- Automatically refreshes the available audio-source list when scene sources are added or removed.
- Adds a separate live activity meter and sensitivity control for internal radio and media.
- Clarifies source labels and help text so users can see which inputs reactive elements can hear.
- Preserves existing desktop, microphone, reaction, scene, and profile settings while adding the new media channel.

## Installer location selection

- Allows installation into any empty custom folder or an existing Tempest Broadcast System installation, regardless of the folder's final name.
- Blocks drive roots, the Broadcast settings directory, its descendants, and unrelated non-empty shared folders.
- Shows a clear validation message on the directory page when a location is unsafe.
- Records the selected application location for in-app updates, the manual updater, and removal.

## Update and settings safety

- Keeps in-app and manual updates on the same signed and verified installer path.
- Installs and removes application files independently from `%APPDATA%\tempest-broadcast-system`.
- Preserves preferences, profiles, scenes, plugin configuration, generated overlays, and managed Asset Vault content during install, update, and removal.
- Continues to reject release payloads containing configuration folders or portable-mode markers.

## Update compatibility

Installed versions beginning with 1.0.1 can discover and install 1.1.0 through **Help → Check for Updates** or the Start Menu updater shortcut. Any supported version can be upgraded by running the signed 1.1.0 installer manually.

## Release artifacts

- `tempest-broadcast-system-1.1.0-windows-x64-installer.exe`
- `tempest-broadcast-system-1.1.0-windows-x64.zip`
- `tempest-broadcast-system-1.1.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.1.0.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.1.0`.
