# Tempest Broadcast System 1.0.5

Tempest Broadcast System 1.0.5 makes audio-reactive controls easier to understand, organizes reactive elements in the Asset Vault, and strengthens update safety for existing installations.

## Audio Reactor

- Reorganizes the Audio Reactor around a simple enable, audio-source, response-preset, palette, and strength workflow.
- Uses clearer end-user labels and explanations throughout the dock.
- Moves sensitivity, beat detection, motion, glow, and test controls into a collapsible fine-tuning section.
- Moves compatible source-network and connected-app controls into separate advanced sections.
- Keeps the existing reaction engine, circuit controls, external event bridge, reduced-motion option, and test actions available.

## Asset Vault

- Adds a dedicated **Audio Reactive** collection.
- Automatically recognizes generated HUDs, managed Vault elements, and telemetry-aware browser elements as audio reactive.
- Scans the complete generated control-deck library while preventing duplicate entries.
- Hides runtime-only telemetry files from the user-facing asset list.
- Displays friendly element names and sorts assets by collection and name.

## Update and settings safety

- Keeps both in-app and manual updates on the same signed, verified installer path.
- Fixes the application install location at `%LOCALAPPDATA%\Programs\Tempest Broadcast System` so an update cannot target the settings directory.
- Continues to keep preferences, profiles, scenes, plugin configuration, generated overlays, and managed Vault content under `%APPDATA%\tempest-broadcast-system`.
- Rejects release payloads containing configuration folders or portable-mode markers.
- Removes only known application payload during uninstall, leaving unexpected user-created or portable configuration content untouched.

## Update compatibility

Installed versions beginning with 1.0.1 can discover and install 1.0.5 through **Help → Check for Updates**, the Start Menu updater shortcut, or Apps & Features. Version 1.0.0 can be upgraded by running the signed 1.0.5 installer manually.

## Release artifacts

- `tempest-broadcast-system-1.0.5-windows-x64-installer.exe`
- `tempest-broadcast-system-1.0.5-windows-x64.zip`
- `tempest-broadcast-system-1.0.5-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.0.5.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode signatures. The installer, binary archive, and source archive must come from the same clean commit tagged `tempest-v1.0.5`.
