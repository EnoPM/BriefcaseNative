# Client server menu and icons

## Interface

The main menu is fixed at 5% horizontal margins and 10% vertical margins, using
90% of the game viewport width and 80% of its height. Position and size follow
resolution changes. Mouse movement and resizing are disabled.

- Home, Mods and Servers navigation uses icons and vertically centered labels.
- All tabs share 22 px horizontal and 20 px vertical content padding, scaled for DPI.
- Navigation rows are 44 px high with 20 px icons.
- Add, join and delete actions use 36 px icon buttons with tooltips.
- The close button uses an icon and a **Close menu (F1)** tooltip.
- Text buttons use 14 × 9 px padding.
- The themed add dialog has a fixed DPI-scaled width of 440 px; only its height
  follows content. A 180-frame test prevents width drift.
- Original vector icons render through `ImDrawList`, with no icon font, texture,
  third-party dependency or extra startup resource.

Off-screen DX11/ImGui fixture captures are generated under `build/menu-fixture/`:
`home.png`, `servers.png`, `add.png`, `tooltip.png` and `resized.png`. Fixture-only
addresses never ship in the client.

## Local directory

Each entry has a stable ID, name and address with port. IPv4, hostnames and
bracketed IPv6 are accepted. Addresses are normalized and duplicates rejected.
The limit is 64 entries.

The directory persists in `Briefcase/servers.json` under the client's Win64
directory. It is created with the first entry, is not part of the package and is
preserved during deployment. The host worker performs reads and writes; rendering
only exchanges snapshots and requests.

Saving writes a complete temporary file before replacement. Failure preserves the
old file and in-memory list. An invalid file remains untouched and its error is
shown in the menu. Links and junctions are rejected.

## Connection

The implementation follows the read-only C# reference
`managed/builtins/CommunityServerConnection.cs`. On the game thread, through the
existing backend queue, it calls
`/Script/DeceiveInc.EOSServerBrowserSubsystem:DirectConnect` on the live instance.

For build `6A96564B-06283000`, it first validates two input `FString` parameters:
`IPPort` at offset 0 and `Password` at offset 16 in a 32-byte buffer. The game
password is empty in this version. No Unreal or graphics object crosses the public
ABI.

Preparation errors leave the menu open and display a message. After dispatch to
`DirectConnect`, the menu closes and returns input to the game. Dispatch success
does not guarantee remote acceptance; normal game networking reports later errors.

## Main files

- `runtime/Briefcase.Client.Menu/Icons.hpp`, `Servers.hpp` and `Menu.cpp`
- `runtime/Briefcase.Client.Servers/ServerDirectory.hpp` and `ServerDirectory.cpp`
- `runtime/Briefcase.NativeHost/ClientServers.inc`, `ClientHost.inc` and `Shutdown.inc`
- `runtime/Briefcase.UnrealBackend/ClientConnection.hpp` and `ClientConnection.inc`
- `runtime/Briefcase.Client.Rendering/ClientBridge.h` and rendering validation
- `tests/ServerDirectoryContracts.cpp` and `ClientMenuFixture.cpp`

Only the private host/render bridge changed for this feature. The public mod ABI
and common runtime remain shared, and the server has no graphics dependency.

## Validation

Release x64 builds passed 14 CTest suites and 50 PowerShell checks. Tests cover
persistence, failed replacement, duplicates, invalid addresses, limits, the
`DirectConnect` contract and real fixture interactions: server tab, keyboard add,
delete, selected join ID, close, tooltip and reduced resolution.

Client and server packages were audited without PDBs or server-side graphics
dependencies. A user playtest confirmed adding an entry, selecting Join and
entering the dedicated server. The client log confirmed `DirectConnect`, menu
closure and input restoration.

To test, open **F1 → Servers → +**, enter a name and address such as
`127.0.0.1:7777`, then choose Join. Also verify deletion and persistence after a
client restart.
