# BriefcaseNative

BriefcaseNative adds a mod menu to the Windows client and native mod support to the Windows dedicated server for Deceive Inc. The server package includes a headless UE4SS runtime. Server mods are downloaded separately.

## Requirements

- Windows x64 and an installed, up-to-date copy of Deceive Inc. or its dedicated server.
- An internet connection for the first download and automatic framework updates.
- The server's `DeceiveInc/Binaries/Win64` directory. All archives below are extracted into this directory, beside the relevant Shipping executable.

The current release is for Windows. A new Linux release will be provided separately when its UE4SS integration is ready.

BriefcaseNative is licensed under the [MIT License](LICENSE). Bundled third-party components retain their own licenses, included in the installation packages.

## Install the client

1. Close the game.
2. Download `BriefcaseNative-Client-windows-x64-<version>.zip` from the [latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest).
3. Extract it into your game's `DeceiveInc/Binaries/Win64` directory.
4. Start `Briefcase.ClientLauncher.exe` from that directory. It launches `DeceiveInc-Win64-Shipping.exe` with Win64 as its working directory.
5. Press **F1** in game to open the Briefcase menu. You can change this key in **Settings**.

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
4. Start `DeceiveIncServer-Win64-Shipping.exe` **from the Win64 directory**. Keep that directory as the process working directory.

The installed `version.dll` starts Briefcase and UE4SS before the server enters the game. It does not open the vanilla server configuration window or a console. On first start, Briefcase creates its local launch and administration settings. Never share `Briefcase/Admin/server.json`; it contains the administration password.

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

Briefcase checks for a framework update before starting the server. To pause these checks during development, stop the server and set `enabled` to `false` in `Briefcase/updater.json`. When Windows UE4SS mod releases become available, install them using their individual guides and keep your existing configuration files.

For a graphical Windows installation and administration guide, see [Briefcase Server Manager](https://github.com/EnoPM/Briefcase.ServerManager). If the server fails to start, check `Briefcase/Logs/launcher-error.log`, `Briefcase/Logs/launcher.log`, and the game logs.
