# Windows proxy startup

Install `version.dll`, `Briefcase/` and `ue4ss/` beside
`DeceiveIncServer-Win64-Shipping.exe`, then start the Shipping executable directly
from `DeceiveInc/Binaries/Win64`. That directory must also be the process working
directory. Briefcase does not install a separate server launcher and does not
modify the game executable on disk.

Windows loads the local `version.dll` proxy before the game entry point. The proxy
forwards the operating-system version APIs to System32 and asks
`Briefcase/Core/Tools/Briefcase.ServerUpdater.exe` to probe for updates. If none
is available, Shipping continues in that same process and can show its vanilla
configuration UI. An available update transfers control to the coordinator,
which installs it and starts Shipping directly. A private environment marker
prevents the coordinated process from repeating the probe. The proxy removes
that marker before loading the runtime so it is not inherited by child processes.

The server process loads `Briefcase.NativeHost.dll`, any installed early mod helpers,
and the pinned `ue4ss/UE4SS.dll` before the
executable entry point. NativeHost provides the
administration and lifecycle services during the migration. UE4SS owns Unreal
discovery and native gameplay mods. The dedicated UE4SS settings disable its GUI,
console and hot reload.

The coordinator creates `Briefcase/launch.json` on a fresh installation. An
existing file must identify the same Win64 directory; a mismatched path stops the
coordinated launch. It also ensures that `sb.DisableEAC=1` is present in
`DeceiveInc/Saved/Config/WindowsServer/Engine.ini` while preserving unrelated
settings.

Briefcase framework updates preserve the installed `ue4ss/Mods/mods.txt` and do
not own any mod directory. Administration restarts call the same update
coordinator, so early-loading mods can be replaced before the next server process
starts.

Direct Shipping launches can use the vanilla configuration UI. When an update
or administration restart requires a new process, the coordinator starts it
headless with `-unattended -NoSplash -NOCONSOLE -nullrhi -nosound` and reads the
game and query ports from `TripwireServer.ini`. Activity and failures are written to
`Briefcase/Logs/launcher.log`; per-launch records include the resulting server PID
and update status.

## Linux boundary

Linux continues to use its native executable launcher and preload bootstrap while
the UE4SS server migration is validated there. See `LinuxServer.md`.

## Validation

The proxy and updater contracts cover path validation, package identity, update
rollback, preservation of the UE4SS mod selection and removal of the retired
Windows launcher/bootstrap files. Deployment contracts also verify that an
existing mod selection and mod data survive a framework update.
