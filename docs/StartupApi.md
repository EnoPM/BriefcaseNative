# Native startup services (0.2)

The 72-byte ABI v1 prefix is unchanged. A `get_service` function is appended to the
table; check `size` before reading it. Each service has its own version, currently 1.

## Configuration

The `config` capability exposes `briefcase.config`. `load` accepts a JSON schema up
to 64 KiB. Supported types are Boolean, signed 64-bit integer, finite number, UTF-8
string and string array. Each property declares `default` and `description`;
`minimum`, `maximum` and `enum` are optional.

Out-of-range values, wrong types and unknown or duplicate keys are rejected.
Defaults fill missing keys. `Data/config.json` is read once during
`BriefcaseModLoad`. The runtime writes defaults when the file is absent and exposes
`Data/config.schema.json`. There is no periodic read. A size probe returns
`BC_LIMIT`, with `required` including the null terminator. Later reads return the
same cached values. Only the owning mod may call the service during its load phase.

## Preparation before the executable entry point

Set `loadPhase` to `startup` in the manifest. The proxy places one breakpoint at
the PE entry point, restores the byte and removes its handler before loading the
runtime and mods. Loading does not occur under the loader lock. The game and its
tick have not started, so gameplay never waits on a worker or delay.

The Windows `EntryGateBeforeExe` test verifies ordering and complete breakpoint
removal. The process exits with code 119 if preparation fails; partially configured
startup is forbidden. Libraries stay loaded until process exit.

This uses [Windows vectored exception handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling).
The x64 context redirects to a normal function rather than performing work in the
handler. Executable TLS initializers run before the entry point, so this service
cannot intercept code before those initializers.

## Transactions

The `briefcase.startup` service provides:

- `server-config.ini`: `read_ini` and `stage_ini` use aliases declared by
  `iniBindings`. Each alias names one `.ini` file in `Saved/Config/WindowsServer`.
  Arbitrary paths and access to another mod directory are forbidden. Files are
  UTF-8/ASCII and limited to 1 MiB; ambiguous sections or keys and newline
  injection are rejected.
- `startup.immediate`: `stage_i32` accepts at most 64 `MOV register32, imm32`
  operands. The full expected `.text` window, exact SHA-256, bounded RVA and Zydis
  decoding are mandatory. No game pointer or general memory read/write primitive
  is exposed. Branches, relative addresses, memory writes and overlapping operands
  are rejected.

The runtime validates and stages work during `BriefcaseModLoad`, then verifies the
original values and files again before commit. Files are backed up and replaced
atomically. Each four-byte immediate receives temporary write protection and the
CPU cache is invalidated. Any failure rolls back completed changes and stops the
process. Instruction changes exist only in memory; the executable on disk remains
unchanged. INI values intentionally persist for the next startup. These APIs are
denied after startup and do not support hot reload.

## Selection and dependencies

`Briefcase/settings.json` accepts `enabledMods`, an explicit list of IDs. A requested
but missing package prevents startup. Without this file, all compatible installed
packages load. A `startup` dependency cannot require a mod whose load phase is
`ready`, after Unreal initialization.
