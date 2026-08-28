# Updating the OBS engine beneath Tempest Broadcast

Tempest Broadcast has two separate update problems:

1. **Tempest application updates** distribute a tested Tempest binary to users.
2. **OBS engine updates** merge a new stable OBS source release into the Tempest fork, resolve source conflicts, rebuild everything, and produce the next Tempest release.

Never point the inherited OBS binary updater at OBS Project packages. An official OBS package would replace the customized frontend and would not understand Tempest's isolated configuration, product identity, docks, overlays, or Studio bridge. The inherited updater remains disabled.

`scripts/Manage-OBSUpstream.ps1` manages OBS engine updates as a guarded source-integration workflow. It fetches only from the configured read-only `upstream` remote and considers only stable numeric tags such as `32.2.1`; beta and release-candidate tags are ignored.

## Workflow

Run these commands from a clean `tempest-main` checkout.

### 1. Check

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Check
```

The report identifies the current OBS base, latest stable tag, exact upstream commit, upstream commit count, changed files, and files changed by both OBS and Tempest. An overlap predicts review work but does not necessarily mean Git will report a conflict.

Use `-NoFetch` for an offline check against tags already present locally. A specific stable target can be selected with `-TargetVersion 32.2.1`.

### 2. Prepare an isolated integration

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Prepare -TargetVersion 32.2.1
```

The default integration location is `G:\Tempest Broadcast Upgrades\obs-<version>` when G: is available. The script creates an `obs-update/<version>` branch and merges the stable OBS tag there. It never merges into `tempest-main` during Prepare.

If Git reports conflicts, the worktree and a state file are retained. Resolve every conflict inside the integration worktree, preserve the Tempest product identity and configuration isolation, and commit the completed merge. Then continue with Validate.

Preview the operation without creating a branch or worktree:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Prepare -TargetVersion 32.2.1 -NoFetch -WhatIf
```

### 3. Validate the complete fork

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Validate -TargetVersion 32.2.1
```

Validation requires a clean, committed integration worktree containing the target OBS tag. It configures the fork, performs a deterministic Windows x64 build, runs CTest, verifies the Tempest executable exists, and records the tested commit plus the executable SHA-256 hash. A changed integration commit invalidates the validation.

After the automated gate, manually test at least:

- first start with a temporary or portable configuration;
- existing Tempest profiles and scene collections;
- game, window, display, browser, media, audio, and device capture;
- streaming, recording, replay buffer, and virtual camera;
- OBS WebSocket and compatible third-party plugins;
- Command and Engineering layouts, UI scaling, Canvas Off, and dock recovery;
- Stream Overlay, Audio Reactor, Studio Integration, and alert effects;
- clean shutdown and logs without device-removal or repeated browser errors.

Review the target OBS release notes for driver, operating-system, Qt, CEF, encoder, plugin ABI, and configuration migration requirements.

### 4. Apply only the validated commit

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Apply -TargetVersion 32.2.1
```

Apply refuses to continue unless:

- `tempest-main` is clean and has not changed since Prepare;
- Validate succeeded;
- the integration branch still points to the exact validated commit; and
- the update can be applied as a fast-forward.

This preserves a simple recovery point: before Apply, `tempest-main` is untouched. After Apply, the previous commit remains available in Git history.

### 5. Clean up and make a Tempest release

After Apply and any deliberate Tempest product-version bump, use the normal public release process in `PUBLIC_RELEASE.md`. Once the integrated branch is safely contained in `tempest-main`, its temporary worktree can be removed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Manage-OBSUpstream.ps1 -Action Cleanup -TargetVersion 32.2.1
```

## Recovery rules

- A failed Prepare never changes `tempest-main`.
- Do not use `git reset --hard` to resolve an upstream update.
- To abandon an unneeded integration, inspect it first, then remove its worktree and branch deliberately with Git.
- Never copy files from an official OBS binary ZIP over a Tempest installation.
- Never test a new engine for the first time with the only copy of a production configuration.
- Keep the prior Tempest release package available until the new engine has completed a real stream test.
