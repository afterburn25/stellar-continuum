# Windows installation and release maintenance

Stellar Continuum now has a native C++ Windows setup application backed by a
separate maintenance library. The full offline setup also updates and repairs
an existing installation. It does not contain gameplay simulation or require
an installer service, .NET, a browser, or administrator privileges.

## Release and entry points

- Authoritative version: `export/runtime-config.json`, currently
  `0.1.14.2-dev`, engine `0.1.64`. CMake generates game/setup headers and Windows
  numeric resource versions from this file. Saves, diagnostics and existing
  About/version consumers continue using the generated game version.
- `StellarContinuumSetup.exe` sits beside `Payload/`. Extract the entire release
  archive before running setup. Its payload consists of cooked runtime files;
  the raw artwork library, rejected assets, tests, saves and debug symbols are
  not installed.
- `StellarContinuumUninstall.exe` is installed with the game and registered in
  Windows Settings > Apps. It copies itself to the user's installer cache
  before showing the removal window so its installed copy can be removed.
- `StellarContinuumSetup.exe --check-package` verifies the pinned metadata and
  every included payload file without installing, recovering or changing registration.
  It returns a nonzero exit status on failure and records the error in a log.
- `--capture-ui <absolute.bmp>` captures the application's own initial window
  and exits. It does not perform installation or recovery.

## Changed-files-only downloads (default for subsequent updates)

`tools/build-update-installer.ps1 -BaseReleaseDirectory <previous-Payload>
-FullReleaseDirectory <validated-target-Payload> -ArchivePath <Update.zip>`
compares verified file hashes and copies only changed files. A complete release
is still prepared internally for validation; users receive the smaller update.
Its complete target manifest includes `updateFrom` (version and build ID) and
`payloadPaths` (the subset physically supplied). Setup pins this manifest.

The existing installation must match the exact base, or the exact target for
repair. Every omitted file is hashed before any changes; a damaged/missing
omitted file requires repairing the baseline first. Clean installation and a
different base are rejected. Provided files use the same transactional staging,
rollback, registration and uninstall as full setup. Retained files keep their
timestamps. The window's space estimate uses the smaller payload allowance.
`--apply-update` applies this same operation without the setup window only to an
existing registration and preserves its shortcut choices. It refuses full setups
and is intended for controlled local maintenance; it is not a new update service.

Game session/error reports now live in `%LOCALAPPDATA%\Stellar Continuum\Logs`.
They record version, executable, renderer, last view/zoom and caught errors.
Windows faults and unhandled C++ termination attempt a `.dmp` beside a text
report. Keep the matching build's PDB files in the developer symbol archive.
Reports stay local, with eight retained files per type and 4 MiB normal log cap.
Forced process kills and power loss cannot guarantee a crash dump.

## 1. Technology choice

The custom Win32 bootstrapper reuses the project's C++ toolchain and shared
engine primitives. Its reusable transaction library owns maintenance rules;
the window is only a consumer. This avoids adding a second runtime or depending
on an external installer compiler for the offline development release.

## 2–5. Files, ownership and engine interfaces

- Engine: `product_version.{hpp,cpp}` owns strict numeric version parsing and
  precedence; `runtime_directory_lease.{hpp,cpp}` owns the game/installer launch
  interlock. Existing atomic file writes provide flushed journal publication.
- Maintenance: `installer/include/stellar/installer/maintenance.hpp` and
  `installer/src/maintenance.cpp` own `Release::parse`, `determine_mode`,
  `make_plan`, `execute`, `recover`, `uninstall`, path policy and streaming SHA-256.
- Windows integration: `windows_platform.{hpp,cpp}` implements `Platform`,
  current-user registration, shortcut snapshots, pending recovery discovery,
  process/Restart Manager checks and local logging.
- UI: `installer/src/main.cpp` owns the native dark window, folder selection,
  version/mode display, checkboxes, worker progress, cancellation and launch.
- Build: `installer/CMakeLists.txt`, its resource/manifest templates and
  `tools/build-release-installer.ps1` connect to the existing cooker/release
  script. The native game takes its directory lease before mounting assets.

These are tooling/engine capabilities. Core's simulation, clock, rendering,
asset resolution and save serialization do not gain alternate implementations.

## 6–9. Version source and comparison

Versions must be `major.minor.patch.build-channel`, with each numeric component
between 0 and 65535 and channel `dev`, `beta` or `stable`. Compare the numeric
tuple first; only an identical tuple compares channel precedence
`dev < beta < stable`. A numerically newer development release is therefore
protected from an older stable installer. Malformed or unknown versions fail
closed. There is no downgrade override in the shipping UI or command line.

| Installed state | Setup action |
|---|---|
| No current-user registration | Install into a new/empty writable folder |
| Older numeric/channel version | Update |
| Same numeric/channel version | Repair; verify and replace only damaged/missing files |
| Newer numeric/channel version | Downgrade blocked; primary action disabled |

Only a matching registration and installation identity authorize an update.
An executable's mere presence never identifies an installation. A nonempty
unregistered directory, including a source checkout, is not adopted.

## 10–14. Install, update, repair and manifests

The default is `%LOCALAPPDATA%\Programs\Stellar Continuum`, with a custom local
folder option. Scope is deliberately current-user only: one registration for
that user, no elevation request and no machine-wide registry writes.

Registration is under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\StellarContinuum`.
It stores product/install identity, path, display version, channel, build ID,
install date, size, uninstall command, shortcut choices and a redundant copy
of the installation record. The local `install_manifest.json` contains those
identities plus the installed release's complete managed file inventory.
Repair can restore a missing or corrupt local record from its registry copy.
Conflicting identities or malformed version metadata stop maintenance.

`Payload/release-manifest.json` schema 2 contains product/platform, game and
engine versions, channel, build ID, cooker version, asset count, exact runtime
bytes, staging allowance, game executable hash, package hashes/sizes and all
managed relative file paths. The setup executable pins this manifest's SHA-256.
The record is bookkeeping; the game continues to mount the existing cooked
`Content/runtime.stmanifest` as its authoritative asset registry.

Planning hashes installed files. Unchanged paths retain their bytes and
timestamps. Missing/corrupt files are copied and verified. Removed managed
files are retired only as part of a successful transaction. Identical cooked
package bytes with a changed generation filename can be staged as a local
hard link after verification; unsupported filesystems fall back to streaming
copy. Binary delta patching is not implemented.

## 15–16. Staging, rollback and interruption recovery

1. Take a per-user maintenance lease and a per-install game-directory lease.
   Check running processes and registered file locks without terminating them.
2. Validate registration, paths, package metadata and disk space. Shipping
   paths follow a restrictive allowlist. Absolute paths, traversal, alternate
   streams, reserved Windows names, junctions and symlinks are rejected.
3. Persist a pending-folder pointer outside the installation. Create
   `.stellar-transaction/journal.json` and stage only changed files beside the
   installed game. Stream in 1 MiB buffers, flush, and verify each SHA-256.
4. Cancel is available during scanning/staging. No live files change then.
5. Flush the journal's committing state. Move originals into its backup tree,
   then move verified replacements into place using write-through renames.
   From this point cancellation/window close waits for commit or rollback.
6. Verify every installed file. Publish the local installation record and
   Windows registration/shortcuts only after content verification. Flush the
   committed journal, then remove the owned staging/backups.

An error during commit reverses file operations and restores both registration
and shortcut snapshots. The next normal setup launch discovers an interrupted
custom-folder transaction from the pending pointer. It discards incomplete
staging, rolls back an interrupted commit, or finishes committed cleanup.
The game refuses to start while its transaction directory remains.

Directory-level replacement cannot be perfectly atomic on Windows. This is a
journaled, recoverable multi-file transaction. If an external process changes
replacement files while recovery is in progress, recovery preserves the
unexpected file and backup and reports the conflict rather than deleting it.
After a successful commit, old backups are removed; this is failure rollback,
not a historical-version browser or an intentional downgrade feature.

Disk checks include all replacement staging bytes plus a 64 MiB metadata/safety
reserve. Old files are renamed on the same volume, not copied into another full
backup. The window shows a conservative supplied-payload maximum; the planner
checks the exact replacement requirement before writing. No multi-gigabyte
package is loaded into memory at once.

## 17–19. Player files, uninstall and shortcuts

Player saves/settings remain at
`%LOCALAPPDATA%\Stellar Continuum\NativePreview`. The developer launcher uses
its `developer` subdirectory unless an explicit save path is supplied. This
retains the established player path and keeps development saves separate.
Portable launchers now use these defaults too. Existing portable `UserData`
directories are not automatically migrated or deleted; their saves remain
available through the load interface.

The Start menu always receives game and uninstall shortcuts. Desktop and
Developer Game shortcuts are optional. They point to the real executable;
Developer Game supplies `--dev-game`. Launch after setup is opt-in.

Uninstall uses the same journal/rollback mechanism and removes only the managed
inventory and product registration/shortcuts. Unknown files, mods and all user
data remain. A folder containing retained files is left in place. No option to
delete player data is enabled implicitly. The small self-relocation helper and
maintenance logs remain in the user's installer cache for diagnostics.

## 20–23. Cooking, packaging, automation and integrity

From a Visual Studio developer shell with PowerShell 7:

```powershell
pwsh -File tools/build-release-installer.ps1 -ArchivePath D:/Releases/StellarContinuum-Setup.zip
```

The command builds native game/cooker/maintenance targets, cooks and validates
shipping assets, creates schema-2 release metadata, checks PE versions, compiles
the manifest pin into setup, runs maintenance tests, verifies the final offline
payload, emits `build-report.json`, and optionally creates the ZIP. A previously
cooked source can be supplied with `-CookedSource`; it is still validated by
the actual cooker. `-ReuseCookedOutput` retains an existing cooked output for a
tooling-only rebuild, again validating every cooked chunk. Do not use that
option when asset inputs change. `-VerifyLaunch` also runs the existing game smoke.
The native CRT is statically linked; the existing SDL3 DLL is included.
Symbols stay in the existing separate cooker symbols directory.

Signing is supported with `-SigningCertificateThumbprint` and an HTTPS RFC3161
`-TimestampUrl`. The private key stays in the Windows certificate store.
Game/uninstaller signing precedes file hashing; setup signing follows manifest
pinning. SignTool verifies each resulting PE signature. Beta/stable publication
fails without a signing identity. Development releases are clearly reported
as unsigned when none is supplied.

The embedded manifest hash binds the accompanying payload to a particular
setup executable. It does **not** authenticate an unsigned setup's publisher.
Authenticode is the distribution trust boundary for signed releases. No private
key, invented update endpoint, HTTP updater or telemetry service is included.

## 24. Errors and logs

Logs are written to
`%LOCALAPPDATA%\Stellar Continuum\Installer\Logs`. They include maintenance
mode, installed/incoming versions, target path, file verification/actions,
disk requirements, Windows errors and rollback results. The UI reports the
actionable error and log path, with Retry where appropriate. A running game or
other locking application is identified when Windows supplies that information.

## 25. Automated and visual verification

- `engine_windows_maintenance`: numeric/channel precedence, malformed metadata,
  dangerous paths, clean install, update, exact repair set, no-op file retention,
  downgrade rejection, cancellation, running-game guard, missing/corrupt payload,
  six rollback boundaries, four process-termination recovery points, runtime
  lease exclusion, locked-file uninstall rollback and user-file preservation.
- `engine_windows_maintenance_os`: actual isolated HKCU registration and COM
  shortcuts, spaces/Unicode in paths, shortcut targets/arguments, missing/corrupt
  local metadata repair, registration/shortcut rollback, pending-folder recovery
  and uninstall. It does not use the player's product key or real shortcut folders.
- Full-package integration and window screenshots are recorded in
  `work/installer-20260920`; final measured sizes/checks are in the release report.

Baseline 0.1.14.1 evidence: all seven targeted CTest cases passed. The 4,287,769,560-byte
runtime passed a complete install, no-op repair, corruption/missing-file repair
and uninstall sequence in 53.63 seconds, with 8,966,144 bytes peak working set
in the maintenance test process. The packaged game's startup check took
7.33 seconds and reported 3,737 assets, seven packages, zero failed asset reads
and no source fallback. Tests ran with a non-elevated Windows token and left
the real product registration absent. These are local test measurements;
they do not predict installation speed on another drive or computer.

Baseline Setup is 2,170,880 bytes before archive compression. Its release build ID is
`0.1.14.1-dev-6e758b928d87af02`, and its pinned manifest SHA-256 is
`1a1773e65b3215bd1dfc76a7512542353687c1af61b5d700198c3db4b5175305`.
The offline download is
`D:/StellarContinuum/Downloads/StellarContinuum-Setup-0.1.14.1-dev.zip`;
its `.sha256.txt` sidecar contains the archive digest.

Current changed-files update: `0.1.14.2-dev-6c8d95a655cd574b` from the exact
baseline above. The ZIP is **8,889,954 bytes** and contains three replacement
files (game, uninstaller, README), Setup, pinned metadata and instructions.
Seventeen existing runtime files, including all seven artwork/audio packages,
are verified and retained. Replacement payload: 20,856,924 bytes.
Download: `D:/StellarContinuum/Downloads/StellarContinuum-Update-0.1.14.2-dev.zip`.
Archive SHA-256:
`dc7983b5292eb4b878af3ea64dfd45b627f0cf7cb472610660c0f73f0596a648`.

Integration-line update (`work/foundation-1-30-codex-integration`):
`0.1.14.2-dev-f096329692cd50ba` from the same exact `0.1.14.1` base.
Thirteen files changed — all seven `Content/*.stpak` packages carry new
content-hash names because merged assets altered every package — so the ZIP
is 4,309,378,332 bytes despite the changed-files-only mechanism. Validated
against an isolated real `0.1.14.1` install (apply, hash check, no-op and
executable repair, save/mod preservation, uninstall). Download:
`D:/StellarContinuum/Downloads/StellarContinuum-Update-0.1.14.2-dev-integration.zip`.
Archive SHA-256:
`0b210eb9839e6e2104828b3888823da77655171d8801daeec0c661f83e056578`.

## 26–28. Size, future reuse and remaining limits

The release's exact payload/setup sizes, hashes, package inventory and signing
status are emitted in `build-report.json`. Test results are evidence for the
covered cases, not a claim that storage hardware cannot fail.

The maintenance library can back a future launcher or local update-check UI.
A future remote envelope should contain numeric version/build/channel,
minimum installer version, release notes and HTTPS package URLs with size/hash,
plus a signature verified against a pinned public key before accepting metadata.
Transport and signature verification must be implemented before enabling
network updates. Current setup is offline only.

Steam/portable distributions can retain their existing ownership of content;
neither is silently registered or modified by setup. Machine-wide deployment,
automatic network updates, independent signed web manifests, binary deltas,
historical rollback after success and certificate provisioning are not included.
The current unsigned development build is not a signed public release.

Windows API references: [kernel object namespaces](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces)
documents the global named mutex used across sessions;
[Restart Manager resources](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmregisterresources)
documents file-lock discovery. Maintenance never calls Restart Manager shutdown.
