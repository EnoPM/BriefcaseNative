# Native server administration

## Client interface

In **F1 → Servers**, select the shield action for a saved server. Administration
provides:

- service endpoint, certificate fingerprint and password authentication;
- runtime status, game build, Unreal status, uptime and loaded-mod count;
- mod names, versions, authors, dependencies and errors;
- saved and active values for registered mod settings;
- server configuration, balance data, logs and restart;
- explicit refresh and logout actions.

Saving does not change a match already in progress. Server settings apply at
startup, so the interface marks changes waiting for restart. Restart applies the
saved selection and configuration. Native libraries stay mapped until process exit.

Displayed uptime and state belong to the last refresh. Unreal backend availability
does not indicate player count or lobby state.

## Architecture

- `Briefcase.Admin.Crypto`: platform-independent Mbed TLS primitives and in-memory
  identity generation.
- `runtime/Briefcase.Admin`: TLS, identity storage, JSON protocol v1,
  authentication, server service and validated persistence.
- `runtime/Briefcase.Client.Admin`: deferred client networking, UI state and saved
  identities.
- `runtime/Briefcase.NativeHost/AdminHost.inc`: runtime inventory and schemas
  registered by mods.
- `runtime/Briefcase.Client.Menu/Administration.hpp`: ImGui interface.
- `tools/AdminSetup.cpp`: local setup and hidden password input.

Networking is linked statically into the common runtime. The server initializes no
client service and has no graphics dependency. The public mod ABI is unchanged.
Transport uses Mbed TLS 3.6.7 with its license preserved.

## Identity and authentication

- TLS 1.3 or TLS 1.2 with AES-GCM and ephemeral key exchange; older versions fail.
- The client verifies the exact SHA-256 certificate fingerprint, validity period
  and usage before sending a password.
- Obtain the fingerprint through a trusted channel from the server administrator.
  A certificate discovered over the network is never trusted automatically.
- The administration password is separate from any game password. The client may
  remember it on request, and no log records it.
- Configuration version 2 stores the administration password as plain text in
  `Briefcase/Admin/server.json`. The service derives its PBKDF2 verifier in memory
  at startup. Legacy version 1 verifier configurations remain accepted.
- Windows protects the RSA-3072 private key through DPAPI. Linux uses a private
  directory and restrictive owner-only permissions.
- Setup installs no certificate into an operating-system store and creates no
  firewall or router rule.
- The service handles at most four connections, four authentication attempts per
  address and twelve overall per minute, plus thirty commands per connection per
  second.
- Authentication must finish within ten seconds of the handshake. An authenticated
  session lasts until explicit logout, client closure, server change, restart or
  network loss. A lightweight client keepalive runs every 30 seconds; the server
  releases two minutes of silent traffic. There is no fixed session lifetime.

Changing the certificate requires approving its new fingerprint. Password or
identity changes take effect after restarting the server and revoke old sessions.
Automatic secret rotation is outside the current scope.

## Configure administration

Administration is disabled when `Briefcase/Admin/server.json` is absent. Stop the
server, then run the packaged setup tool from its platform binaries directory.

Windows:

```powershell
& ".\Briefcase\Core\Tools\Briefcase.AdminSetup.exe" `
    --root "$PWD\Briefcase" `
    --listen "127.0.0.1" `
    --port 50002 `
    --endpoint "127.0.0.1:50002"
```

The tool asks for the password twice without echoing it, then stores it in
`Briefcase/Admin/server.json`. It never appears on the command line. To generate a
strong 256-bit password instead, add `--generate-password`.

Source deployments may use:

```powershell
.\scripts\admin\Configure-Administration.ps1 `
    -ServerRoot "D:\DeceiveIncBackups\DeceiveIncNativeServer" `
    -ListenAddress "127.0.0.1" `
    -Port 50002 `
    -PublicEndpoint "127.0.0.1:50002" `
    -GeneratePassword
```

The relevant configuration field is:

```json
{
  "version": 2,
  "password": "your-administration-password"
}
```

Passwords accept 12–256 UTF-8 bytes. To change one, edit only `password`, preserve
the other fields and restart. The private file must never enter a package or Git.
On Windows, the password can be copied to the clipboard with:

```powershell
.\scripts\admin\Copy-AdministrationPassword.ps1 -ServerConfig "<Win64>\Briefcase\Admin\server.json"
```

The legacy `--generate-password-file` option additionally creates a local
DPAPI-protected recovery copy. `-PasswordFile` can still read such files.

Choose an explicit listen address and reachable public endpoint for a remote
client. `127.0.0.1` restricts access to the server machine. The administrator is
responsible for firewall and TCP forwarding rules.

Setup creates:

```text
Briefcase/Admin/
├── server.json    # private identity, private-key reference and password
└── pairing.json   # public endpoint, identity and fingerprint
```

Share only `pairing.json`. The Shipping executable must always use its Win64 or
Linux binaries directory as the working directory.

## Connect from the client

1. Open **F1 → Servers** and select the server's shield action.
2. Copy the administration endpoint and fingerprint from `pairing.json`.
3. Enter the password and select **Connect**.
4. Inspect status and mods, then edit a setting.
5. Select **Save for next startup**.
6. Confirm that the Active column still shows the running server value.
7. Under Server, request restart, confirm, wait and reconnect to inspect active values.

Approved identities persist in `Win64/Briefcase/Admin/clients.json` and bind to the
favorite's game address. **Remember the password on this client** stores it in the
Windows DPAPI-protected `Admin/passwords.json` vault. The binding includes game
address, administration endpoint and certificate fingerprint, so a changed identity
cannot reuse the secret. **Forget** removes the local copy.

Only successful authentication saves a password; UI JSON stores only a presence
flag. The server-list shield connects automatically when a password exists. A
rejected password returns to the login screen without a retry loop. Returning to
the list or toggling F1 preserves the session. The logout icon closes it explicitly.

## Protocol and writes

Every request has a version, random ID, operation and parameter object. UTF-8 JSON
messages use a four-byte network-order length prefix. Limits are 64 KiB per request,
1 MiB per response and JSON depth 16; duplicate keys fail.

Operations include `hello`, `authenticate`, `server.status`, `mods.list`,
`mod.config.read/write`, `logout`, `server.config.read/write`,
`mods.selection.read/write`, `balance.read/write`, `server.logs`, `server.restart`
and administration-v3 `translations.read`.

A mod write sends the mod ID, expected revision, complete values and a write ID.
The server chooses the path, validates values against the registered schema and
atomically replaces the file. No client-provided path is used. The three supported
application mod IDs are explicitly allowlisted.

`mod.config.write` uses idempotency receipts. Other saves use an expected revision
and are not retried automatically. Two administrators cannot silently overwrite the
same revision. The latest 128 write receipts remain in memory: an identical retry is
idempotent, while reusing an ID with different content fails. After network loss,
refresh or reconnect before writing again.

Redirected paths and linked files fail. Write errors preserve the previous file.

## Administration tabs

Internal game names remain visible where needed to identify exact balance rows.
Bounds come from `Community Balance Template/CommunityBalanceProfile.default.json`;
fields absent from that catalog are read-only. Saves change only requested values
and preserve profile metadata. The active file is
`DeceiveInc/CommunityBalanceProfile.json`, limited to 1 MiB and 2,048 entries. The
game reloads and hashes it after restart.

Server settings use `Saved/Config/WindowsServer/TripwireServer.ini`. Unknown keys,
other sections and existing administration passwords are preserved. Each change
creates a backup in `Briefcase/Admin`. Active values are the service's startup
snapshot rather than live game-memory measurements.

The server package includes `Briefcase.ServerRestart`. Its helper verifies the
exact process path and creation time, holds the process handle and prepares the
launch command before shutdown. After the TLS response is acknowledged, it requests
mod cleanup, terminates a remaining process and starts Shipping with the platform
binaries directory as working directory. Existing arguments are preserved and
ports are reread from configuration. The client cannot provide a path or system
command. Results are written to `Admin/restart-result.json`.

## Labels, languages and search

See [Localization and presentation](Localization.md) for framework, mod and server
catalogs, custom labels and categories.

Server settings are grouped into Identity, Network, Match, Bots, Maps and Heat.
Search is case-insensitive across label, key and value; password values are excluded.
Table columns retain padding in their headers and bodies. Notifications use success,
danger, warning or information colors.

Mod decimal settings display and edit three decimals. Changes round to that step
before transmission, so repeated subtraction by 0.1 reaches exactly zero. Smaller
existing balance values remain supported and viewing never rewrites on-disk values.

## Validation and limits

Native tests cover identity storage, real TLS, authentication, unauthenticated
access, wrong certificates and passwords, rate limiting, revisions, mod bounds,
concurrency, idempotency, cancellation and client persistence.

An independent CPython/OpenSSL client checks TLS 1.2/1.3, fragmented framing,
forbidden sizes, duplicate JSON keys, depth, truncation and shutdown with an idle
connection. The ImGui fixture checks the shield, login, password-field clearing,
saves and active/saved columns.

Local fixtures do not replace an in-match playtest. Precise lobby/player state, hot
reload, remote secret changes and arbitrary system commands remain outside scope.
