# Tempest Broadcast System 1.1.1

Tempest Broadcast System 1.1.1 is an installer reliability hotfix for new installations using custom folders.

## Installer reliability

- Verifies that Windows can create and write the selected installation folder before extracting application files.
- Explains when a protected location, such as Program Files, cannot be used by the per-user installer.
- Continues to allow any empty writable folder or an existing writable Tempest installation.
- Verifies the application executable, updater, and both fallback theme files before registering the installation or creating shortcuts.
- Removes known partial application files after a failed fresh installation instead of leaving a broken executable behind.
- Never removes an existing installation when an update attempt fails.

## Startup recovery

- Replaces the generic `Failed to load theme` startup error with guidance that the installation may be incomplete or unreadable and should be reinstalled into a writable folder.

## Settings safety

- Does not install into, overwrite, or remove `%APPDATA%\tempest-broadcast-system`.
- Preserves preferences, profiles, scenes, plugin configuration, generated overlays, and managed Asset Vault content during install, update, failed-install cleanup, and removal.

## Update compatibility

Installed versions beginning with 1.0.1 can discover and install 1.1.1 through **Help → Check for Updates** or the Start Menu updater shortcut. Any supported version can be upgraded by running the signed 1.1.1 installer manually.

## Release artifacts

- `tempest-broadcast-system-1.1.1-windows-x64-installer.exe`
- `tempest-broadcast-system-1.1.1-windows-x64.zip`
- `tempest-broadcast-system-1.1.1-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.1.1.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.1.1`.
