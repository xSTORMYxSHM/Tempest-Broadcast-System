# Tempest Broadcast System 1.2.1

Tempest Broadcast System 1.2.1 is a focused performance and efficiency update for long-running streams. It keeps the
1.2.0 Studio bridge contract, scene format, and OBS engine compatibility unchanged.

## Runtime efficiency

- Stops the Sequence Director's 100 ms timer while no sequence is running or while a sequence is held.
- Pauses Media Controls source and playback polling while its dock is hidden, then refreshes immediately when shown.
- Keeps Audio Reactor browser telemetry responsive at 10 Hz while reducing atomic `telemetry.json` disk writes to
  4 Hz; external-event transitions are still written immediately.
- Adds an idle fast path for the source-reaction engine so disarmed or empty reaction networks do not repeatedly walk
  scene items after their outputs have been restored.

## Compatibility

- No Tempest Studio update is required. The existing `tempest-mainframe` bridge contract is unchanged.
- No scene collection, profile, or configuration migration is required.
- The upstream OBS engine base remains 32.2.1; the displayed fork revision advances for this release.

## Updating from 1.2.0

Use **Check for Updates** in Broadcast or the Start Menu's standalone updater. The updater downloads and verifies the
signed 1.2.1 installer, closes Broadcast without force-terminating active output, creates the recovery snapshot, and
installs into the location already registered for Broadcast.

## Release artifacts

- `tempest-broadcast-system-1.2.1-windows-x64-installer.exe`
- `tempest-broadcast-system-1.2.1-windows-x64.zip`
- `tempest-broadcast-system-1.2.1-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.2.1.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode
signatures. The installer, binary archive, and source archive must come from the same clean commit tagged
`tempest-v1.2.1`.
