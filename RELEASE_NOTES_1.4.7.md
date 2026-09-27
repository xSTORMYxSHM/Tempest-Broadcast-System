# Tempest Broadcast System 1.4.7

Tempest Broadcast System 1.4.7 is a startup-stability hotfix for the 1.4.6 release.

## Startup hotfix

- Prevents the Destinations status snapshot from requesting the primary streaming output before the OBS output
  handler has been initialized.
- Fixes the repeatable `0xc0000005` access violation that could close Broadcast during launch immediately after the
  `Service 'rtmp_common' not found` startup log entry.
- Treats the primary streaming output as temporarily unavailable during early startup and shutdown, allowing the
  existing status payload to report an idle output until initialization completes.
- Preserves existing profiles, scenes, destination settings, recovery backups, and Studio Bridge configuration.

## Compatibility

- Includes the complete Viewer Interaction rendering contract introduced in 1.4.6.
- Remains aligned with Tempest Streaming Studio 1.4.6 and Studio Bridge protocol `1.0`.
- The upstream OBS engine base remains 32.2.1.

## Release artifacts

- `tempest-broadcast-system-1.4.7-windows-x64-installer.exe`
- `tempest-broadcast-system-1.4.7-windows-x64.zip`
- `tempest-broadcast-system-1.4.7-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.4.7.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode
signatures. The installer, binary archive, and source archive must come from the same clean commit tagged
`tempest-v1.4.7`.
