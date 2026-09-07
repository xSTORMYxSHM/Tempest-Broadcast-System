# Tempest Broadcast System 1.1.4

Tempest Broadcast System 1.1.4 is a startup and shutdown stability hotfix for the plugin-path regression introduced in 1.1.3.

## Plugin loading correction

- Prevents the installed application from registering the same bundled OBS plugin directory twice when its working directory is `bin\64bit`.
- Canonicalizes executable-relative and working-directory module paths before deciding whether the development fallback is required.
- Ensures browser, image, media, filter, encoder, capture, and output modules load only once.
- Resolves the access-violation crash involving `obs-browser.dll`, `obs.dll`, and `w32-pthreads.dll` during browser-source cleanup.
- Retains executable-relative discovery for launches from Start Menu shortcuts, desktop shortcuts, the updater, direct execution, and third-party launchers.

## Included 1.1.3 improvements

- Keeps the launch-time theme, locale, shader, and plugin discovery fixes from 1.1.3.
- Keeps general Asset Library support for ordinary PNG, GIF, JPEG, MP4, MOV, WebM, MKV, MP3, WAV, HTML, SVG, and other OBS-compatible stream assets.
- Keeps the selectable installer location and the standalone signed updater.

## Settings safety

- Does not install into, overwrite, or remove `%APPDATA%\tempest-broadcast-system`.
- Preserves preferences, profiles, scenes, plugin configuration, generated overlays, indexed asset folders, and Asset Library collection assignments during update, repair, and removal.

## Updating from 1.1.3

Use **Check for Updates** in Broadcast or the Start Menu's standalone updater. If Broadcast crashes before the in-app updater can be opened, run `bin\64bit\tempest-broadcast-updater.exe` directly or install 1.1.4 manually over the existing application folder.

## Release artifacts

- `tempest-broadcast-system-1.1.4-windows-x64-installer.exe`
- `tempest-broadcast-system-1.1.4-windows-x64.zip`
- `tempest-broadcast-system-1.1.4-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.1.4.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.1.4`.
