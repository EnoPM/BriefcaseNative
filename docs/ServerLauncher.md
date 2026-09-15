# Server launcher

Run Briefcase.ServerLauncher.exe from the server's DeceiveInc/Binaries/Win64 directory.
The executable opens no window. Its update coordinator waits for the initial launcher to exit,
checks GitHub releases, restores interrupted transactions and installs any valid newer package.
It then starts the native launcher under the same launch lock. Errors go to
Briefcase/Logs/launcher-error.log; launch records include the actual server PID and update status.

StartBriefcaseNativeServer.ps1 remains available for command-line use, with the same update path.
Administration restarts use this coordinator too. --launch-child is an internal invocation,
reserved for the coordinator after the update; normal users should omit it.

The native launcher creates only the adjacent DeceiveIncServer-Win64-Shipping.exe.
Its working directory is always that executable's Win64 directory.
The arguments include -unattended -NoSplash -NOCONSOLE -nullrhi -nosound.
No graphical server console, renderer, UE4SS UI or client module is required.

Microsoft Detours 4.0.1, linked statically under its MIT license, loads
Briefcase/Runtime/Briefcase.ServerBootstrap.dll before the executable entry point.
The bootstrap calls the existing NativeHost on the original main thread, outside the loader lock.
It acknowledges success only after required startup mods have initialized.
Missing DLLs, failed preparation and timeouts stop the newly created process.
The game executable on disk is unchanged. The server package no longer contains version.dll;
the client continues to use its proxy.

Deploy the complete framework package using scripts/deploy/Deploy-Server.ps1 -RuntimeOnly.
This backs up and removes the old server proxy while preserving installed mods and their data.
Never keep the old proxy alongside the injected bootstrap.

## Linux boundary

This implementation currently targets Windows x64. LaunchWindows.hpp and the bootstrap entry
gate are platform-specific. A Linux port needs a native process/preload adapter, the Linux game
binary, validated native contracts, and a compatible Unreal backend. The current Windows game
offsets must not be reused. The updater coordinator currently uses Windows PowerShell and also
needs a Linux implementation. The update format, public mod ABI and separation of framework/mods
can be retained. Hiding a Windows window is not a Linux port.

## Validation

The native fixture verifies loading before EXE CRT initialization, primary-thread preparation,
Win64 working directory, failed preparation, a missing host DLL, and command-line quoting.
Updater and deployment tests verify preservation of mod configuration and transaction recovery.
