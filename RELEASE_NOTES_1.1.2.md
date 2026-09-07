# Tempest Broadcast System 1.1.2

Tempest Broadcast System 1.1.2 corrects the fresh-install regression in 1.1.1 and strengthens recovery from partial installations.

## Installer correction

- Creates and write-tests a new destination before checking whether it contains unrelated files.
- Restores silent and interactive installation into a brand-new writable folder.
- Write-tests the application, theme, and plugin subdirectories before repairing or updating an existing installation.
- Stops before overwriting the application when existing subfolder permissions would prevent a complete repair.
- Continues to remove only known partial application files after a failed fresh installation.
- Writes a concise diagnostic log to the Windows temporary folder when validation or extraction fails.

## Theme and resource recovery

- Resolves packaged data from the application executable when Broadcast is launched with an unexpected working directory.
- Keeps the existing install-root and executable-folder lookup paths for compatibility.
- Ensures installer, Start Menu, desktop, updater, and direct executable launches can locate the same packaged themes and locale data.

## Settings safety

- Does not install into, overwrite, or remove `%APPDATA%\tempest-broadcast-system`.
- Preserves preferences, profiles, scenes, plugin configuration, generated overlays, and managed Asset Vault content during install, update, repair, and removal.

## Recovery from 1.1.1

Run the signed 1.1.2 installer and select a new writable folder, preferably the default folder under Local AppData. An existing installation is repaired in place only when its critical application subdirectories pass the write test.

## Release artifacts

- `tempest-broadcast-system-1.1.2-windows-x64-installer.exe`
- `tempest-broadcast-system-1.1.2-windows-x64.zip`
- `tempest-broadcast-system-1.1.2-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.1.2.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.1.2`.
