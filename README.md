# BriefcaseNative

BriefcaseNative is a native mod framework for the Deceive Inc. dedicated server.
It runs the server without opening its graphical interface, loads separately
installed native mods and checks for framework updates before every launch.

The public release currently supports Windows x64 and Ubuntu 24.04 x86_64
dedicated servers. The client is not distributed yet.

## Install the dedicated server

Install [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD), then download
the Deceive Inc. dedicated server anonymously. Choose an empty directory that will
remain the permanent server directory.

On Windows:

```powershell
steamcmd.exe +force_install_dir "C:\DeceiveIncServer" +login anonymous +app_update 5007710 validate +quit
```

On Linux:

```bash
./steamcmd.sh +force_install_dir /opt/deceive-inc-server +login anonymous +app_update 5007710 validate +quit
```

SteamCMD creates the game below the chosen directory. Briefcase must be installed
directly beside the platform's Shipping executable.

## Install Briefcase on Windows

1. Stop the dedicated server.
2. Download `BriefcaseNative-Server-windows-x64-<version>.zip` from the
   [latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest).
   The SDK and source-code archives are not server installers.
3. Extract the ZIP directly into:
   `C:\DeceiveIncServer\DeceiveInc\Binaries\Win64`
4. Create `Briefcase\launch.json` in that Win64 directory. Replace the example
   path with the absolute path to your own installation and escape each backslash:

```json
{
  "serverWin64": "C:\\DeceiveIncServer\\DeceiveInc\\Binaries\\Win64"
}
```

The resulting layout starts like this:

```text
DeceiveInc/Binaries/Win64/
├── DeceiveIncServer-Win64-Shipping.exe
├── Briefcase.ServerLauncher.exe
├── StartBriefcaseNativeServer.ps1
└── Briefcase/
    ├── launch.json
    ├── Runtime/
    ├── Tools/
    └── Updater/
```

Start the server with `Briefcase.ServerLauncher.exe`. Do not start
`DeceiveIncServer-Win64-Shipping.exe` directly: doing so bypasses Briefcase, its
mods and its update check. The launcher opens no server UI or external console.

If startup fails, read `Briefcase\Logs\launcher-error.log` and the game logs.

## Install Briefcase on Linux

Install the runtime libraries on Ubuntu 24.04:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends libcurl4t64 libarchive13t64 ca-certificates unzip
```

Then:

1. Stop the dedicated server.
2. Download `BriefcaseNative-Server-linux-x64-<version>.zip` from the
   [latest release](https://github.com/EnoPM/BriefcaseNative/releases/latest).
3. Extract it directly into the server's `DeceiveInc/Binaries/Linux` directory.
4. Launch Briefcase from that directory:

```bash
cd /opt/deceive-inc-server/DeceiveInc/Binaries/Linux
chmod +x Briefcase.ServerLauncher
./Briefcase.ServerLauncher
```

The launcher always runs `DeceiveIncServer-Linux-Shipping` with
`Binaries/Linux` as its working directory. No Python, .NET runtime or shell script
is required by the installed Briefcase package.

## Install mods

Briefcase mods are distributed separately from the framework. Download the mod
archive matching the server operating system and extract it into the same
`Binaries/Win64` or `Binaries/Linux` directory. A correctly packaged mod is placed
under `Briefcase/Mods/<mod-id>/`.

Stop the server before installing or replacing a mod. Preserve an existing
`Data/config.json` when upgrading a mod because it contains your settings. If
`Briefcase/settings.json` does not exist, Briefcase loads all compatible installed
server mods. A mod marked for another environment or operating system is ignored.

## Automatic updates

Starting with BriefcaseNative 0.5.1, the launcher creates
`Briefcase/updater.json` on first use and checks the official GitHub release before
starting the server. Compatible updates are installed before launch. An unavailable
network or release does not remove the installed version.

Existing updater preferences are preserved. To disable automatic checks, stop the
server and set `enabled` to `false` in `Briefcase/updater.json`. Always launch the
server through `Briefcase.ServerLauncher.exe` on Windows or
`./Briefcase.ServerLauncher` on Linux for updates to run.

## More help

- [Server administration](docs/Administration.md)
- [Windows launcher and logs](docs/ServerLauncher.md)
- [Linux server notes](docs/LinuxServer.md)
- [Updater behavior and recovery](docs/Updates.md)
- [Contributing and building from source](CONTRIBUTING.md)

Before reporting a problem, include the operating system, Briefcase version, game
server build and relevant files from `Briefcase/Logs`. Never publish
`Briefcase/Admin/server.json`, passwords or private keys.
