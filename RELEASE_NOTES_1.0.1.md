# Tempest Broadcast System 1.0.1

Tempest Broadcast System 1.0.1 adds the first Tempest-specific update path for installed Windows systems.

## Tempest updater

- Includes `tempest-broadcast-updater.exe` in the signed Windows payload.
- Adds **Check for Updates** to the application Help menu.
- Adds a separate **Check for Updates** shortcut to the installer-created Start Menu folder.
- Adds an Apps & Features **Modify** action that opens the updater.
- Checks only the latest final release from the official `xSTORMYxSHM/Tempest-Broadcast-System` GitHub repository.
- Compares the Tempest Broadcast product version using numeric `major.minor.patch` semantics.
- Downloads the matching signed Windows installer when a newer version is available.
- Verifies the GitHub-provided SHA-256 digest, exact file size, embedded product version, and Windows Authenticode trust before installation.
- Requests a normal application close and never force-terminates an active Broadcast process.
- Runs the installer in update mode and relaunches Tempest Broadcast System after installation.

## Installer behavior

- Remains a per-user installer and does not require administrator elevation.
- Preserves `%APPDATA%\tempest-broadcast-system` during installation, updates, and uninstall.
- Installs the application and updater under `%LOCALAPPDATA%\Programs\Tempest Broadcast System`.
- Registers the application in the current user's Apps & Features list.

## Update compatibility

Version 1.0.0 does not contain the Tempest updater and must be upgraded manually by running the 1.0.1 installer. Once 1.0.1 is installed, future final Broadcast releases can be discovered and installed through the bundled updater.

## Release artifacts

- `tempest-broadcast-system-1.0.1-windows-x64-installer.exe`
- `tempest-broadcast-system-1.0.1-windows-x64.zip`
- `tempest-broadcast-system-1.0.1-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.0.1.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.0.1`.
