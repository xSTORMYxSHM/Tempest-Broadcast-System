# Tempest Broadcast System

Tempest Broadcast System is a private Windows broadcast workstation for the Tempest Mainframe. It is built from OBS Studio and retains the upstream OBS license, authorship, and plugin compatibility.

## Identity and isolation

- Executable: `tempest-broadcast-system.exe`
- Product: `Tempest Broadcast System`
- Company: `Tempest Mainframe`
- User configuration root: `tempest-broadcast-system`
- Windows instance mutex: `TempestBroadcastSystemCore`
- Default theme: `com.tempestmainframe.Broadcast`
- Upstream automatic updates and the What's New feed are disabled.

The fork can run beside a normal OBS Studio installation without sharing profiles, scene collections, logs, crash reports, or plugin-manager settings.

## Local source workflow

- Private branch: `tempest-main`
- Official OBS remote: `upstream`
- No personal or publishing remote is configured.

Configure the existing Windows build tree with:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S . -B build_x64 -DENABLE_WHATSNEW=OFF
```

Build with:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-x64 --parallel
```

The development build is written to:

```text
build_x64\rundir\RelWithDebInfo\bin\64bit\tempest-broadcast-system.exe
```

Upstream OBS updates should be reviewed and merged deliberately into `tempest-main`; do not enable the OBS binary updater for this fork.
