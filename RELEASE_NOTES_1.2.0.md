# Tempest Broadcast System 1.2.0

Tempest Broadcast System 1.2.0 expands Broadcast from a single-canvas production workstation into a coordinated
horizontal-and-portrait production environment, while making migration, external control, and updates safer.

## Portrait production workspace

- Adds the integrated **Tempest Portrait Canvas** for vertical and short-form output alongside the main canvas.
- Adds guided creation and linking of portrait companion scenes from the Mainframe bar.
- Keeps linked portrait scenes synchronized with main-scene changes while leaving manual scene control available.
- Connects the portrait canvas to OBS Multitrack Video as the managed additional canvas.
- Prevents resolution changes while portrait output is active and keeps portrait updates under the main Broadcast
  updater.

## Coordinated destinations

- Adds a profile-aware destination coordinator for the primary OBS destination and an optional Kick destination.
- Starts and stops both configured destinations through the normal Broadcast output controls.
- Validates the Kick server, stream key, encoder, and bitrate before starting its H.264 output.
- Keeps credentials and destination configuration in the local profile instead of exposing them through public
  project pages or release metadata.

## Guest layouts

- Adds reusable two-person split, featured-host, four-person grid, and focus-with-guest-rail layouts.
- Creates managed placeholder scenes that can be populated with browser, camera, or collaboration sources.
- Uses unique names and stable metadata so generated layouts do not overwrite existing scenes.

## OBS installation migration

- Adds guided import from an existing OBS Studio configuration or portable installation.
- Imports scene collections and profiles without overwriting existing Tempest data.
- Safely renames conflicts and reports conversion or copy failures.
- Reports missing media paths, unavailable source/plugin types, and detected plugin data that requires a compatible
  plugin installation.
- Does not copy third-party binaries, browser cookies, or the old OBS window layout.

## Stable external bridge

- Adds the versioned `tempest-mainframe` OBS WebSocket vendor contract for Data Horizon and other authenticated
  clients.
- Provides read-only contract and Broadcast-state discovery even while vendor control is locked.
- Adds opt-in scene routing, overlay, sequence, protocol, and reaction-event controls.
- Includes stable request, response, event, error, capability, and compatibility documentation in
  `docs/TEMPEST_BRIDGE_CONTRACT_V1.md`.

## Update and recovery safeguards

- Creates a verified local settings snapshot before the updater launches an installer and cancels the update if the
  snapshot cannot complete.
- Preserves profiles, scene collections, application settings, plugin settings, Control Deck data, and custom themes.
- Retains the five newest backups while excluding logs, crash reports, profiler data, and downloaded update files.
- Records file counts, byte counts, and scene-collection JSON validation in each backup manifest.
- Detects an incomplete first startup after an update and offers recovery mode with third-party plugins disabled.
- Adds **Back Up Broadcast Settings** to the installed Start Menu shortcuts for an on-demand verified snapshot.
- Supports installed and portable configuration locations without moving or resetting user data.

## Updating from 1.1.4

Use **Check for Updates** in Broadcast or the Start Menu's standalone updater. The updater downloads and verifies the
signed 1.2.0 installer, closes Broadcast without force-terminating active output, creates the recovery snapshot, and
installs into the location already registered for Broadcast.

## Release artifacts

- `tempest-broadcast-system-1.2.0-windows-x64-installer.exe`
- `tempest-broadcast-system-1.2.0-windows-x64.zip`
- `tempest-broadcast-system-1.2.0-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.2.0.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode
signatures. The installer, binary archive, and source archive must come from the same clean commit tagged
`tempest-v1.2.0`.
