# Tempest Broadcast System 1.0.6

Tempest Broadcast System 1.0.6 restores install-location selection while preserving the update and user-settings protections introduced in 1.0.5.

## Installer location selection

- Restores the installer directory page so users can choose another drive or parent location.
- Keeps the application in a dedicated folder named `Tempest Broadcast System` to prevent application cleanup from targeting a drive root or shared directory.
- Records the selected location and reuses it for in-app updates, the manual updater, and later installer runs.
- Supports upgrades of existing registered custom-location installations, including older installations whose final folder has a different name.

## Update and settings safety

- Keeps both in-app and manual updates on the same signed, verified installer path.
- Requires uninstall to match the exact application location recorded during installation.
- Removes only known application payload during uninstall.
- Never installs into or removes `%APPDATA%\tempest-broadcast-system`, where preferences, profiles, scenes, plugin configuration, generated overlays, and managed Asset Vault content are stored.
- Continues to reject release payloads containing configuration folders or portable-mode markers.

## Update compatibility

Installed versions beginning with 1.0.1 can discover and install 1.0.6 through **Help → Check for Updates** or the Start Menu updater shortcut. Any supported version can be upgraded by running the signed 1.0.6 installer manually.

## Release artifacts

- `tempest-broadcast-system-1.0.6-windows-x64-installer.exe`
- `tempest-broadcast-system-1.0.6-windows-x64.zip`
- `tempest-broadcast-system-1.0.6-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.0.6.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.0.6`.
