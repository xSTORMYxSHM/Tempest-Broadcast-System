# Tempest Broadcast System 1.4.5

Tempest Broadcast System 1.4.5 aligns Broadcast with Tempest Streaming Studio's production output controls while
keeping Broadcast authoritative for output configuration, credentials, scene routing, and live state.

## Studio production integration

- Adds the exact Dual Format and simulcast capabilities consumed by the matching Tempest Streaming Studio release.
- Publishes read-only Dual Format readiness, simulcast readiness, per-destination state, bytes sent, dropped frames,
  total frames, congestion, preflight results, recording state, and upload-budget status.
- Adds guarded commands for Dual Format configuration and preview, production preflight, coordinated Go Live,
  Kick-only retry, Kick-only stop, and all-output stop.
- Keeps configuration and preflight changes off-air and requires explicit operator checklist acceptance plus a fresh
  successful preflight before Studio can request Go Live.
- Fixes direct Studio command deduplication so commands without workflow identifiers are keyed by their unique bridge
  message ID.

## Broadcast-owned output settings

- Makes the Broadcast Destinations dock the authoritative editor for Kick ingest URL, protected stream key, encoder,
  video and audio bitrates, measured upload capacity, and recording-with-stream behavior.
- Protects newly saved Kick stream keys with Windows Data Protection API encryption for the current Windows account.
- Reads existing profile keys for compatibility and migrates them to protected storage on the next save or Studio
  configuration request without returning credentials through status, logs, or bridge responses.
- Estimates combined Twitch and Kick upload requirements with a 20 percent reserve and blocks production preflight
  when measured capacity is insufficient.

## Dual Format readiness

- Selects an existing 1080x1920 or 720x1280 additional canvas without deleting or resizing canvases.
- Reports Enhanced Broadcasting selection, canvas metadata, horizontal-to-portrait scene links, program audio, and
  required portrait alert-browser sources.
- Opens the integrated portrait workspace for preview and leaves incomplete scene collections in a truthful
  setup-required state instead of silently rewriting operator scenes.

## Compatibility and updating

- The upstream OBS engine base remains 32.2.1.
- Existing profiles, scenes, destination settings, and portrait canvases are preserved.
- Broadcast remains usable without Studio. The new production control workflow requires the matching Tempest
  Streaming Studio release.
- Use **Check for Updates** in Broadcast or the Start Menu updater to install 1.4.5 while preserving
  `%APPDATA%\tempest-broadcast-system`.

## Release artifacts

- `tempest-broadcast-system-1.4.5-windows-x64-installer.exe`
- `tempest-broadcast-system-1.4.5-windows-x64.zip`
- `tempest-broadcast-system-1.4.5-source.zip`
- `SHA256SUMS.txt`
- `release-manifest.json`
- `NOTICE.txt`
- `PUBLIC_RELEASE.md`
- `RELEASE_NOTES_1.4.5.md`

All executable Windows files in the installer and portable archive must have valid timestamped Authenticode
signatures. The installer, binary archive, and source archive must come from the same clean commit tagged
`tempest-v1.4.5`.
