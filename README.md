# BriefcaseNative

BriefcaseNative loads UE4SS mods for the Windows client and dedicated server of Deceive Inc. Mods are downloaded separately and can provide their own menus.

## Requirements

- Windows x64 and an installed, up-to-date copy of Deceive Inc. or its dedicated server.
- The [Microsoft Visual C++ v14 Redistributable (x64)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) on the server PC. The Briefcase proxy, host and UE4SS DLLs use this runtime.
- An internet connection for the first download and updates through Briefcase ServerApp.
- The server's `DeceiveInc/Binaries/Win64` directory. All archives below are extracted into this directory, beside the relevant Shipping executable.

The current release is for Windows. A new Linux release will be provided separately when its UE4SS integration is ready.

BriefcaseNative is licensed under the [MIT License](LICENSE). Bundled third-party components retain their own licenses, included in the installation packages.

## Install the client

1. Close the game.
2. Download `BriefcaseNative-Client-windows-x64-<version>.zip` from the [latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest).
3. Extract it into your game's `DeceiveInc/Binaries/Win64` directory.
4. Start `Briefcase.ClientLauncher.exe` from that directory. It launches `DeceiveInc-Win64-Shipping.exe` with Win64 as its working directory.
5. Install a compatible client mod separately, following that mod's instructions. BriefcaseNative does not open a menu on its own; a mod may provide one.

If the launcher cannot start the game, read `Briefcase/Logs/client-launcher-error.log`.

## Install a dedicated server on Windows

If the dedicated server is already installed, skip to the next section. Otherwise, [download SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD), extract it, and run:

```powershell
.\steamcmd.exe +force_install_dir "C:\DeceiveIncServer" +login anonymous +app_update 5007710 validate +quit
```

Wait for SteamCMD to finish. The server executable should be at `C:\DeceiveIncServer\DeceiveInc\Binaries\Win64\DeceiveIncServer-Win64-Shipping.exe`.

## Install Briefcase on the server

1. Stop the dedicated server.
2. Download `BriefcaseNative-Server-windows-x64-<version>.zip` from the [latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest).
3. Extract the ZIP directly into `C:\DeceiveIncServer\DeceiveInc\Binaries\Win64`. Use your own server path if different. Do not create another `Win64` folder inside it.
4. Start `DeceiveIncServer-Win64-Shipping.exe` **from the Win64 directory**. Keep that directory as the process working directory. To run without the vanilla configuration window, open PowerShell in Win64 and use:

```powershell
Start-Process -FilePath ".\DeceiveIncServer-Win64-Shipping.exe" -ArgumentList "-unattended -NoSplash -NOCONSOLE -nullrhi -nosound" -WorkingDirectory (Get-Location).Path
```

The installed `version.dll` starts Briefcase and UE4SS before the server enters the game. Starting Shipping without arguments opens its vanilla configuration window; the command above runs the server headless.

A typical installation contains:

```text
DeceiveInc/Binaries/Win64/
├── DeceiveIncServer-Win64-Shipping.exe
├── version.dll
├── ue4ss/
│   ├── UE4SS.dll
│   ├── UE4SS-settings.ini
│   └── Mods/
│       └── mods.txt
└── Briefcase/
    └── Core/
```

## Install server mods

Each mod has its own release and installation guide. Install the Briefcase server package first, stop the server, then extract a **Windows UE4SS** mod archive into the same Win64 directory. The mod's guide provides the exact line to add to `ue4ss/Mods/mods.txt`. Keep an existing `Data/config.json` when replacing a mod, and restart the server to apply changes. UE4SS mod releases are being prepared; older archives that install into `Briefcase/Mods` are for the previous loader and must not be installed as UE4SS mods.

The framework archive deliberately contains no gameplay mods. UE4SS starts with an empty `mods.txt`.

## Updates and help

Use Briefcase ServerApp to check for and install framework and mod updates while the server is stopped. When it starts a server, the app prepares the configuration, renames `version.dll` to `version.dll.disabled`, and injects Briefcase before the game runs. A later direct Steam launch then runs without Briefcase mods. If you manually restore `version.dll` and start Shipping directly, the proxy still loads Briefcase and UE4SS but does not check for updates.

If the Shipping process disappears immediately, check `Briefcase/Logs/BriefcaseNative.log` first. If it contains no failure, check Windows Reliability Monitor for the faulting module and exception code. To distinguish a vanilla server problem from Briefcase startup, stop the server, temporarily rename only `Win64/version.dll` to `version.dll.disabled`, try Shipping once, then restore the original filename before further modded-server tests. Never share an existing `Briefcase/Admin/server.json`; it contains an administration password from older installations.
