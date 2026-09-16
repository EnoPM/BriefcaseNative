# Server launcher

Run Briefcase.ServerLauncher.exe from the server's DeceiveInc/Binaries/Win64 directory.
The executable opens no window. It copies the native update coordinator into the update work
directory, then exits so its installed executable can be replaced. The coordinator checks GitHub
releases, restores interrupted transactions and installs any valid newer package before launching
Shipping under the same launch lock. Initial launcher errors go to
`Briefcase/Logs/launcher-error.log`; coordinator activity and errors go to
`Briefcase/Logs/launcher.log`. Launch records include the actual server PID and update status.

From 0.5.1, a missing Briefcase/updater.json is created automatically with updates enabled
against EnoPM/BriefcaseNative. The first launch checks immediately; existing settings are
preserved, including an explicit opt-out. See Updates.md for configuration and recovery.
Administration restarts use this coordinator too. `--launch-child` is an internal injection
entry point retained for launcher validation; normal users should omit it.

The native launcher creates only the adjacent DeceiveIncServer-Win64-Shipping.exe.
Its working directory is always that executable's Win64 directory.
The arguments include -unattended -NoSplash -NOCONSOLE -nullrhi -nosound.
No graphical server console, renderer, UE4SS UI or client module is required.

Microsoft Detours 4.0.1, linked statically under its MIT license, loads
Briefcase/Core/Briefcase.ServerBootstrap.dll before the executable entry point.
The native coordinator uses WinHTTP and Windows CNG from the operating system and
miniz 3.1.2 for validated ZIP extraction; its license is included in the package.
The bootstrap calls the existing NativeHost on the original main thread, outside the loader lock.
It acknowledges success only after required startup mods have initialized.
Missing DLLs, failed preparation and timeouts stop the newly created process.
The game executable on disk is unchanged. The server package no longer contains version.dll;
the client continues to use its proxy.

Deploy the complete framework package using scripts/deploy/Deploy-Server.ps1 -RuntimeOnly.
This backs up and removes the old server proxy while preserving installed mods and their data.
Never keep the old proxy alongside the injected bootstrap.

## Linux boundary

Linux has its own native C++ launcher, preload bootstrap and updater, documented in
LinuxServer.md. It uses Binaries/Linux, Linux game profiles and a linux-x64 release asset.
Both platforms initialize the official update feed automatically and preserve user settings.

## Validation

The native fixture verifies loading before EXE CRT initialization, primary-thread preparation,
Win64 working directory, failed preparation, a missing host DLL, and command-line quoting.
Updater and deployment tests verify preservation of mod configuration and transaction recovery.
