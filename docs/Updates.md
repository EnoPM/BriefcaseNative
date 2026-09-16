# Server updates

The local launcher checks the latest stable GitHub release before starting Shipping
from `Binaries/Win64` on Windows or `Binaries/Linux` on Linux. Administration
restarts use the same launcher. No download or replacement occurs during a match.
Starting the game executable directly bypasses update checks.

## Automatic activation

Starting with 0.5.1, installation from an official ZIP requires no updater setup.
On first launch, `Briefcase.ServerLauncher.exe` or `Briefcase.ServerLauncher`
creates `Briefcase/updater.json` when absent, using:

```json
{
  "enabled": true,
  "repository": "EnoPM/BriefcaseNative",
  "updateMods": true,
  "timeoutSeconds": 20
}
```

The launcher immediately checks, then repeats the check at every launch or managed
restart. It installs a compatible update before starting the server.

An existing file is preserved exactly, including `enabled: false`, a custom
repository or invalid settings that produce an error. An empty repository also
disables checks. `Briefcase/Updater/updater.example.json` remains a reference. The
active file belongs to the administrator and is never part of update replacement.

Version 0.5.0 installations without configuration must install a newer package
once or copy the example to `Briefcase/updater.json`. Official public releases need
no GitHub token. Never place a token in the repository name.

`timeoutSeconds` ranges from 1 to 120 and separately limits metadata and archive
downloads. Network failure, GitHub rate limiting, a private repository or a missing
release leaves the installed version running with a warning.

## Release contract

`VERSION` at the repository root is the sole framework version source. It accepts
only `MAJOR.MINOR.PATCH`. A release uses tag `vMAJOR.MINOR.PATCH` and includes:

- `BriefcaseNative-Server-windows-x64-MAJOR.MINOR.PATCH.zip`;
- `BriefcaseNative-Server-linux-x64-MAJOR.MINOR.PATCH.zip`;
- the SDK ZIP.

GitHub-generated source archives are not installers.

The updater calls `GET /repos/{owner}/{repo}/releases/latest`. It rejects
prereleases, automatic downgrades, ambiguous assets, redirects outside GitHub,
archives without GitHub SHA-256 digests and incompatible game builds. Separate
`.sha256` assets are unnecessary because GitHub supplies the asset digest used by
the updater and the publication workflow. A new game
build must pass native contracts before its supported hash changes.

## Mod updates

Mod updates are available starting with BriefcaseNative 0.6.0.

After the framework check, the launcher checks every installed server mod whose
manifest explicitly contains:

```json
"update": {
  "provider": "github-releases",
  "repository": "OWNER/REPOSITORY"
}
```

The latest stable release must use tag `vMAJOR.MINOR.PATCH` and contain exactly one
platform archive named `REPOSITORY-windows-x64-MAJOR.MINOR.PATCH.zip` or
`REPOSITORY-linux-x64-MAJOR.MINOR.PATCH.zip`. The archive contains
`ModPackage.json`, whose identity, complete file inventory, sizes, modes and
SHA-256 hashes are validated before installation. Only files below the matching
`Briefcase/Mods/<mod-id>/` directory are accepted.

Mod updates run before any mod loads, including `startup` mods such as PlayerCap.
An administration restart returns to the launcher and performs the same checks.
When the framework itself is replaced, its updated launcher code runs before mod
checks continue. Set `updateMods` to `false` to disable only mod updates. Setting
`enabled` to `false` disables both framework and mod network checks; recovery of an
interrupted transaction still runs.

`Data/config.json` is created when missing and otherwise preserved byte for byte.
Managed binaries, manifests and licenses are replaced transactionally. A failed
download or invalid release keeps the installed mod and allows the server to start;
a failed recovery blocks startup rather than loading a mixed version. Results are
recorded in `Briefcase/Updates/last-result.json`.

The anonymous GitHub API supports public repositories. A private mod repository is
left at its installed version unless its releases become publicly readable. Never
put a GitHub token in a mod manifest.

## Installation and recovery

An archive downloads and extracts into `Briefcase/Updates/<id>/stage` under path and
size limits. `Package.json` lists every framework-managed file and hash. Mod updates
use their separate package manifest and transaction. Mod data, selection, administration configuration, passwords,
logs, updater settings and game configuration remain in place. Old managed files
absent from the new package are removed after backup.

An installation lock prevents concurrent mutation. The transaction journal is
written before any change, and old files are backed up under
`Briefcase/Updates/<id>/backup`. Failure restores them. After interruption, the next
launch restores backups before attempting startup. If recovery fails, the server
does not start with mixed libraries. Backup cleanup is manual in this version.

Successful installation does not prove gameplay behavior, and automatic rollback
after an in-game crash is outside scope. Inspect `Briefcase/Updates/last-result.json`
and `Briefcase/Logs/launch-*.json` for results.

Archive digests protect download integrity but do not provide an independent trust
root if the publishing GitHub account is compromised. Protect repository release
permissions accordingly.

## Platforms

Windows x64 and Linux x64 server packages share HTTPS, JSON, ZIP and SHA-256
protocols but use separate assets. The Linux launcher and updater are native C++.
The Windows coordinator currently uses PowerShell. The client does not update
itself automatically. See [Linux server](LinuxServer.md) for runtime requirements.

## Automatic and manual publication

`.github/workflows/release.yml` defines **Publish server release**. A push to
`main` that changes `VERSION` builds, tests and publishes `v<VERSION>`. Other pushes
publish nothing. The workflow reads the version from the triggering commit, and
CMake, SDK metadata and archive names derive from the same value.

To run it manually:

1. Update `VERSION` and commit the source to the branch to publish.
2. Open **Actions → Publish server release → Run workflow**.
3. Select the branch.
4. Enable **Keep the release as a draft** only when a reviewable draft is desired.
5. Select **Run workflow**.

The selected commit is fixed for all jobs even if the branch advances. The workflow
never edits `VERSION` for the operator.

The Windows 2025 runner fetches pinned dependencies, installs Rust 1.97.1, builds
Release x64, runs tests and verifies client/server packages. Graphics fixtures use
WARP and need no game installation. The client is built and tested but not published.

Publication uses GitHub's scoped `GITHUB_TOKEN` with `contents: write`. The build
uses repository secret `UPSTREAM_READ_TOKEN` to read the pinned private Unreal
submodule. It requires no server password, local deployment path or
`local.settings.json`. Third-party actions are official and pinned by full SHA.

The tag must point to the built commit. Existing tags on another commit and existing
releases fail rather than being replaced. Assets first enter a draft. The reusable
Linux workflow builds and appends Linux assets from the same commit. GitHub digests
are compared with local files before the complete three-asset release becomes stable
and latest. Manual draft mode retains the validated draft.

If a job fails after draft creation, the draft remains for inspection. Delete an
incomplete draft before retrying. Never replace a distributed release; publish a
new version. Release artifacts remain available for 14 days and failed CTest
reports for 7 days.

Local workflow validation uses `actionlint` and
`tests/ReleaseWorkflow.Contracts.ps1`, which simulates GitHub commands without
contacting GitHub or creating a release.

References:

- [GitHub Releases REST API](https://docs.github.com/en/rest/releases/releases)
- [Manually run a workflow](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow)
- [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create)
- [Repository boundaries](Repositories.md)
