# Tempest Broadcast System 1.1.3

Tempest Broadcast System 1.1.3 corrects launch-time shader and plugin discovery and expands the Asset Library into a general importer for third-party stream elements.

## Startup and shortcut correction

- Resolves libobs shader files relative to the Broadcast executable instead of depending on the launcher's working directory.
- Resolves packaged plugin binaries and plugin data from the executable location while retaining development-layout fallbacks.
- Corrects installer-created application and updater shortcuts to start in `bin\64bit`.
- Prevents successfully initialized GPUs from being misreported as unsupported when packaged shader files could not be found.
- Supports Start Menu, desktop, updater, direct executable, and third-party launcher starts from the same installation.

## General-purpose Asset Library

- Accepts ordinary third-party stream assets without Tempest-specific names, metadata, or markup.
- Indexes static and animated images including PNG, JPEG, GIF, BMP, TGA, WebP, and JXR.
- Indexes common FFmpeg-supported video and audio assets including MP4, MOV, WebM, MKV, AVI, MP3, WAV, FLAC, OGG, AAC, M4A, and Opus.
- Indexes local HTML/HTM browser elements, SVG graphics, and TXT/JSON/LOG support assets.
- Classifies images, animations, videos, SVG, and browser elements as `OVERLAY` by default; audio defaults to `ALERT` and text/data defaults to `TEXT`.
- Shows each asset's detected type in the library.
- Adds images, media, and browser elements to the active scene using their matching persistent OBS source type.
- Keeps video and audio assets available to the Sequence Director through the managed Asset Bus.

## Settings safety

- Does not install into, overwrite, or remove `%APPDATA%\tempest-broadcast-system`.
- Preserves preferences, profiles, scenes, plugin configuration, generated overlays, indexed folders, and Asset Library collection assignments during update, repair, and removal.

## Updating from 1.1.2

Use **Check for Updates** in Broadcast or the Start Menu's standalone updater. If Broadcast 1.1.2 cannot start from its shortcut, run `bin\64bit\tempest-broadcast-updater.exe` directly or install 1.1.3 manually over the existing application folder. User settings remain outside the installation folder and are preserved.

## Release artifacts

- `tempest-broadcast-system-1.1.3-windows-x64-installer.exe`
- `tempest-broadcast-system-1.1.3-windows-x64.zip`
- `tempest-broadcast-system-1.1.3-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.1.3.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.1.3`.
