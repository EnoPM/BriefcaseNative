# Shared dedicated-server installation

Implemented in the BriefcaseNative runtime and the separate Briefcase.ServerApp
project on 2026-10-09. This is a local Windows implementation; it does not add
remote control or Linux support.

## Layout and identity

ServerApp can register several logical servers against one Shipping executable.
Steam branches are installed separately under `<data-root>/game/versions/<branch>/`
or in a chosen folder. An existing `<data-root>/game/dedicated` is registered
as the public branch when older settings are loaded. The application chooses
an installed version when creating a server; a stopped server can be moved to
another version while keeping its configuration and mods. Choosing the same
existing Shipping executable again also creates a separate instance. Each
entry has a stable GUID and its own `<data-root>/servers/<guid>/` containing:

```text
TripwireServer.ini
CommunityBalanceProfile.json
ue4ss/UE4SS.dll
ue4ss/UE4SS-settings.ini
ue4ss/Mods/mods.txt
ue4ss/Mods/<mod>/...                 # including editable Data/config.json
Briefcase/Mods/<native-mod>/...
Briefcase/Logs/...
Briefcase/Admin/...
```

The game files and Briefcase NativeHost stay in the shared installation. The
application passes the instance's INI and balance profile through the verified
game options `-TripwireServerIni` and `-CommunityBalanceProfile`. The private
injector sets `BRIEFCASE_INSTANCE_ROOT` and `BRIEFCASE_INSTANCE_ID` in the child
before its entry point. Bootstrap loads UE4SS from the instance directory;
UE4SS resolves its settings, mod selection, mod data and logs there. NativeHost
uses the same directory for its native mods, logs and admin files. ServerApp
refreshes the UE4SS runtime and built-in bridge from the shared installation
before starting an instance, while preserving each instance's mod selection.

The registry is schema 3. Opening a schema 1 registry copies existing server
configuration and mods into an instance directory, writes a `.v1-backup`, and
keeps the original installation in place. A schema 2 registry is backed up and
its existing public shared installation is registered when present. A migration
copy failure leaves that
entry in legacy mode instead of discarding it. Removing an instance with file
deletion removes its instance directory, never the shared game installation.
The shared Briefcase/game updater refuses an update while any process using that
installation runs; a mod operation checks only its own instance.

## Process and live channel

Each NativeHost publishes a Windows named event containing the instance GUID
and PID. ServerApp uses it to rediscover processes after an application restart,
even if all processes have the same executable path. The server bridge includes
the GUID in live snapshots; ServerApp verifies both PID and GUID before using
player data or controls. This preserves the existing transport interface for a
future Linux or network transport. Every simultaneous instance needs distinct
game and query ports; ServerApp checks collisions between its registered
instances before starting one.

## Validation and remaining limits

The ServerApp contract suite covers two registry entries with one executable and
independent mod selection/configuration. A live smoke test launched two
headless ManagedTestServer processes concurrently from one Shipping executable,
on ports 48670/48671 and 48672/48673. Both were rediscovered under the correct
GUID, both produced verified live snapshots, UE4SS loaded the bridge from each
instance directory, and both stopped cleanly. The game's log also confirmed
that `-CommunityBalanceProfile` loaded 1,632 overrides from a supplied path.

Game-owned files under the shared `DeceiveInc/Saved` directory, balance reports
and crash dumps have not all been proven instance-specific. These may still be
shared or overwrite one another. Player-visible behavior with different mod
settings has not yet been tested in simultaneous matches. The application does
not automatically assign free ports; configure distinct game/query pairs for
each server.
