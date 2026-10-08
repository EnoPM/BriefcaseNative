# Windows server startup

Briefcase ServerApp owns the work that can be done before starting a dedicated server: checking release versions, downloading and validating archives, installing framework and mod updates, and preparing `Engine.ini`. Its installed mod records preserve the catalog URL and version so a later catalog refresh can identify updates. A user can also install or update components explicitly while the server is stopped.

On a ServerApp launch, `Native/Briefcase.ServerInjector.exe` beside the application starts Shipping headless with the app's `Native/Briefcase.ServerBootstrap.dll` injected through Detours. Neither injection component is included in the Briefcase server archive or copied into the game directory. The injected bootstrap loads Briefcase NativeHost, early mod helpers, and UE4SS before the game's entry point. The injector waits for a preparation handshake and returns the actual server PID to ServerApp.

The package keeps `version.dll` for users who launch Shipping directly. Before its first managed launch, ServerApp renames that file to `version.dll.disabled` and informs the user. A direct Steam launch then remains unmodded even though the Briefcase and UE4SS files are still installed. On a later package update ServerApp disables the newly installed proxy again before launch. As a defense in depth, the injector also sets `BRIEFCASE_EXTERNAL_BOOTSTRAP=1`; if a proxy is unexpectedly present, it only forwards version APIs and does not install its own entry gate.

ServerApp checks its own injection components and the server's Briefcase NativeHost separately. It removes any injection files left in the server folder by a previous development package. Its `Briefcase/updater.json` is set to disabled before launch so an older proxy cannot run a second update path.

ServerApp's package installer checks the GitHub release SHA-256 digest, the package manifest, the exact game build hash, every package file's size and SHA-256, and its allowed destination. It stages files before replacement, preserves marked files such as `ue4ss/Mods/mods.txt`, and restores prior files if an installation fails. A small journal lets the app restore an interrupted installation before the next launch. Game and mod configuration remains in the server directory.

ServerApp owns server restarts. The older in-server restart helper and native update coordinator are not included in new Briefcase archives. Linux's experimental launcher likewise no longer checks releases during startup; Linux distribution will be handled separately.

## Validation

`ServerLauncherInjection` checks preparation before the EXE entry point and failure handling. `ServerProxyCoordinator` checks both a direct proxy launch and a ServerApp-style injected launch with the proxy still present, including the absence of native update activity.
