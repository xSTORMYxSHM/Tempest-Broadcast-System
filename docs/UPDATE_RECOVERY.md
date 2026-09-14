# Update and recovery safeguards

Tempest Broadcast System creates a verified settings snapshot before its Windows updater starts an installer. The
update is cancelled if the snapshot cannot be completed.

Backups are stored in `recovery-backups` inside the active `tempest-broadcast-system` configuration folder. This
keeps installed and portable configurations independent. Each backup contains:

- a `settings` folder with user configuration, profiles, scene collections, plugin settings, control-deck data, and
  custom themes;
- a `manifest.json` recording the source and target versions, file and byte counts, and scene-collection JSON
  validation results.

Logs, including the embedded browser's volatile diagnostic log, crash reports, profiler data, downloaded updates,
the live crash sentinel, and older recovery backups are not copied. The five most recent completed backups are
retained.

The updater records a pending first-start checkpoint after creating the backup. Broadcast clears that checkpoint
only after its main window, graphics, audio, modules, profiles, and scene collection have initialized. If the first
start does not finish, the next launch shows the backup location and recommends recovery mode, which disables
third-party plugins for that launch.

The installer also adds **Back Up Broadcast Settings** to the Tempest Broadcast System Start menu folder. That
shortcut runs the same verified snapshot operation without downloading or installing an update. The standalone
equivalent is:

```text
tempest-broadcast-updater.exe --backup-only
```

These backups remain local to the Windows user account and are never uploaded by the updater.
