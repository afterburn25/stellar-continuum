# Validation receipt — foundation-expansion integration (2026-09-20)

Scope: merge of `engine/foundation-expansion-1-30` (`aa90d0e6`, 13 commits
after `fbb3165b`) into `cpp/codex-native-architecture-integration` tip
`e20e83a9a10fe82b6ce886890e0f50fc0ad91707`.

- Work branch: `work/foundation-1-30-codex-integration` (renamed from
  `integration/foundation-1-30-into-codex`; the existing
  `refs/heads/integration` branch blocks `integration/*` ref creation)
- Merge commit: `0f6637a1`; post-merge fixes: `f1baa31b`, packaging compat
  and CI/docs commits on top.
- Authoritative versions: game `0.1.14.2-dev`, engine `0.1.64`
  (`export/runtime-config.json`).
- Machine: Windows x64, MSVC 14.44.35207, Vulkan device present.

## Build

`cmake --preset windows-native-preview` +
`cmake --build --preset windows-native-preview --parallel 4`: **PASS**, all
targets including `stellar-continuum-native`, `StellarCooker`,
`StellarContinuumSetup`, `StellarContinuumUninstall`.

## Native tests

`ctest --preset windows-native-preview --parallel 2 --output-on-failure`:
257 tests registered. Result equals the documented codex baseline exactly:
**236 passed, 21 failed, 0 new regressions**. The 21 failures are the
receipt's SYNC items (`stellar_objects`, `developer_qa_host`, generation/
catalog/persistence parity set, `native_research_controller`,
`native_fresh_progression`, `large_galaxy_2500`, `engine_generation_50000`).

Four merge regressions were found and fixed before this result (SYNC-R04…R07
in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md)):

- `native_video_platform` — dropped `scene_target` assignment restored.
- `native_campaign_session` — history-origin recovery notice restored;
  tactical-save section now corrupts the full history chain.
- `native_planet_disc_assets` — fixture root corrected to `assets/visual/sol`.
- `native_new_game_workspace` — codex's five-request contract restored; the
  expansion's card-art expectation was dropped with its superseded UI.

## Python exporter tests

`python -m unittest discover -s tools/stellar-export -p "test_*.py"` with
`STELLAR_NATIVE_EXE` pointing at the built **headless** executable
(`build-native/preview/stellar-continuum.exe`; the graphical
`stellar-continuum-native.exe` rejects the CLI options these tests use and
produces spurious "Unknown or incomplete native client option" failures):
**570 tests, 1 unsuccessful**. The 34 `NativeClientDependencyTests` fixture
failures (SYNC-003) are fixed — the fixture now seeds the full validator
contract — and the `Data/` packaging-casing collision is fixed in
`native_planet_runtime.py`/`native_environment_runtime.py`. The SYNC-004
Sol-ordering assertion was rewritten identity-based (last *primary* is Pluto;
final body is Charon) and now passes. The sole remaining unsuccessful test is
`test_galaxy_asset_import.test_widescreen_revisions_keep_both_variants`: the
`assets/source/galaxies-16x9/*.png` review masters were never committed to
the repository, so the sha256 cross-check cannot pass. The em-dash key
mismatch inside that test was fixed by escaping non-ASCII keys in
`export/galaxy-asset-edits.json`.

## Graphical runtime smokes

- `--galaxy-art-smoke`: Vulkan renderer, vsync, 1280x720, 500 systems,
  159 frames; six captures (galaxy, regional, system, close, max zoom).
  `paused:true` and `day_unchanged:true` confirm input-release handling.
  Territory fog, labels, colony thumbnails and stellar close-up artwork
  verified in captures.
- `--record` + `--replay` on `--new-game-smoke`: checkpoint recorded and
  replayed — `verified_checkpoints:1`, `commands_consumed:0`,
  `diverged:false`.
- `--developer-smoke` (developer-eligible): **passes end to end** after three
  stale smoke assumptions were repaired (SYNC-006 resolved — see
  KNOWN_ISSUES): canonical material LOD is 2048 with source-limited widths,
  `solid_scene()` now reads `small_body_scene()` instead of grabbing the sky
  dome's `Scene3DView`, and the icy dielectric check focuses an actual icy
  body via `focused_small_body()`. All sections green: celestial index,
  empire monitor, live reveal, alien colony/economy read-only inspection,
  nine authored Sol bodies + rotating globes, binary/triple systems,
  belt/debris solids, dielectric motion+pause, four spawn commands,
  save/load round trip, imported planet materials/ring shadows/portraits,
  eruption continuity, diagnostic bundles and the final capture.
- `--new-game-restart-smoke`: `saved_previous:true`, `activated:true`.
- `--navigation-smoke`: `keyboard_galaxy_playback:true`,
  `keyboard_system_playback:true`, `keyboard_save_requested:true`,
  `menu_blocked:true`, `modal_blocked:true`, four blocked contexts,
  `day_unchanged:true`, `no_charge:true`. Required two fixes after the
  merge: the InputMapper feed was moved back ahead of the system-workspace
  handler (its unconditional `continue` swallowed every key press/release
  in system view), `begin_frame()` now runs at the top of `update()`, and
  the planet material queue is pumped per frame so pending decodes cannot
  stall behind a closed view.
- `--system-smoke`: entry/hit/pan/zoom/reset/back, `pause_retained:1`,
  `speed_retained:1`, `focused:1`, `day_unchanged:1`.
- `--planetary-smoke` and `--colony-smoke`: planetary views, colony roster
  (3 rows, selected/readonly/exclusive/scrolled), `pause_retained:true`,
  `speed_retained:true`, `day_unchanged:true`.

## Installer evidence

- Full internal release built via `tools/build-release-installer.ps1`:
  `D:/StellarContinuum/integration-release/StellarContinuum-Setup-0.1.14.2-dev`.
  Payload: 4,345,851,354 bytes, 20 files, 3,781 cooked assets (baseline had
  3,737). Both maintenance test suites and `--check-package` passed.
  Target build ID: `0.1.14.2-dev-b9692f416ad57ef4` (regenerated after the
  developer-smoke renderer/accessor fixes; the executable changed).
- Changed-files update via `tools/build-update-installer.ps1` against the
  exact verified base `0.1.14.1-dev-6e758b928d87af02` (manifest SHA-256
  `1a1773e6…5305`, matching the documented pin). 13 changed files —
  all seven `Content/*.stpak` packages carry new content-hash names because
  merged assets changed every package — plus manifest, README, two license
  texts, game executable and uninstaller. 7 files verified identical and
  retained. Package verification: passed.
- Isolated apply validation:
  `stellar_maintenance_tests <fresh-dir> --update-package <0.1.14.1 Payload>
  <update Payload>` — **PASS**: real baseline install, changed-files apply,
  full hash verification, no-op repair, damaged-executable repair, unchanged
  content timestamps, save/mod preservation and clean uninstall.
- Update download:
  `D:/StellarContinuum/Downloads/StellarContinuum-Update-0.1.14.2-dev-integration.zip`
  — 4,309,374,645 bytes; SHA-256
  `1d4739337b8779105c2967aa846f8d0ae3dfc30d55e75ace00f1c4304133006b`;
  update `StellarContinuumSetup.exe` SHA-256
  `d11ffa90dc2bbb6db5b98ea8af536052592d2668aab49a3e1e4ec669fa8b7b25`
  (`.sha256.txt` sidecar beside it). Matching PDBs retained under
  `work/cooker/symbols/0.1.14.2-dev/`.

## Hosted CI (subsequent runs)

- `build` workflow: **PASS** on `f4d36d44` after gating the historical Godot
  fixture smokes on materialized artwork (LFS pointer stubs fail the Godot
  import validator; the native tree keeps its art in LFS). SYNC-R09.
- `Stellar Engine native foundation` (`windows-export`): the outer
  `windows-native-preview` build + filtered CTest step honored the documented
  21-test baseline exclusion; the first run failed inside
  `stellar.py export windows-benchmark` because the internal headless CTest
  invocation ignored the exclusion. `native_build()` now honors
  `STELLAR_CTEST_EXCLUDE` (verified locally: 257→236 tests = exactly the
  documented 21) and `STELLAR_UNITTEST_EXCLUDE` (named test IDs via
  `filtered_test_runner.py`; currently unused — the SYNC-004 assertion was
  repaired instead of excluded). Follow-on run `0ca613af`: headless build,
  filtered CTest and all 22 Python test files passed; the packaged
  dependency audit then flagged `Cabinet.dll` (Windows Compression API used
  by `asset_registry.cpp`) — added to `SYSTEM_DLLS` as a system component.
  Build/suite subprocess timeouts were also raised for shared-runner
  variance (3600 s build, 900 s/1200 s suites).
- Later heads exposed three shared-runner issues, each fixed in turn:
  `df8a3189` failed `engine_runtime_diagnostics` when the child-fault
  fixture lost its report/minidump once (now retried, with artifact-specific
  diagnostics); `86fac6ec` failed `engine_asset_cooker` when `sha256_file`
  hit a transient antivirus open lock on a just-published chunk (retried on
  `EACCES`); `f0731623` passed all CTest/Python suites then failed in
  `relocated_smoke` on a stale `solBodies == 10` assertion — Sol now carries
  28 catalogued bodies, matching the `native_moon_tests` pin. Final hosted
  result pending on `1dccdbbd`.

## Boundaries

- The workflow covers the actual integration configuration on `cpp/**`/
  `integration/**` pushes and `cpp/**` PRs.
- The update is large because every cooked package's bytes changed; the
  changed-files mechanism operates at file granularity, not intra-package.
- SYNC-001/002/005/008/009/010/011 remain open as documented;
  SYNC-003 (fixture omissions), SYNC-004 (audit-key encoding, Sol-ordering
  assertion) and SYNC-006 (developer-smoke LOD/scene-selection/ice-focus
  assertions — now passing end to end) are fixed — see KNOWN_ISSUES. The
  widescreen review masters are still absent, so
  `test_widescreen_revisions_keep_both_variants` remains red.
