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
`STELLAR_NATIVE_EXE` pointing at the built native executable: **570 tests**,
results equal the documented baseline of 36 unsuccessful tests with **zero
new failures**. One baseline failure now passes
(`test_export.NativeRecovery.test_galaxy_loads_assets_relative_to_executable`).
Remaining failures are the documented fixture gaps: temporary packaging roots
without `data/planets/planet-art-v1.json` (SYNC-003), absent
`assets/source/galaxies-16x9/*.png` review masters, and the Sol-ordering
assertion (SYNC-004). The em-dash key mismatch inside SYNC-004 was fixed by
escaping non-ASCII keys in `export/galaxy-asset-edits.json`.

## Graphical runtime smokes

- `--galaxy-art-smoke`: Vulkan renderer, vsync, 1280x720, 500 systems,
  159 frames; six captures (galaxy, regional, system, close, max zoom).
  `paused:true` and `day_unchanged:true` confirm input-release handling.
  Territory fog, labels, colony thumbnails and stellar close-up artwork
  verified in captures.
- `--record` + `--replay` on `--new-game-smoke`: checkpoint recorded and
  replayed — `verified_checkpoints:1`, `commands_consumed:0`,
  `diverged:false`.
- `--developer-smoke` (developer-eligible): celestial index, empire monitor
  and live reveal pass; stops at the documented SYNC-006 planet-map
  assertion. Not a new regression.
- `--new-game-restart-smoke`: `saved_previous:true`, `activated:true`.

## Installer evidence

- Full internal release built via `tools/build-release-installer.ps1`:
  `D:/StellarContinuum/integration-release/StellarContinuum-Setup-0.1.14.2-dev`.
  Payload: 4,345,850,336 bytes, 20 files, 3,781 cooked assets (baseline had
  3,737). Both maintenance test suites and `--check-package` passed.
  Target build ID: `0.1.14.2-dev-5c4003dd3b821af9`.
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
  — 4,309,374,528 bytes; SHA-256
  `cfd6e2a15484b6f3d05a17f233fb40273499eecff41e47446944e685c2aa2af1`
  (`.sha256.txt` sidecar beside it). Matching PDBs retained under
  `work/cooker/symbols/0.1.14.2-dev/`.

## Boundaries

- Hosted CI was not run; the workflow now covers the actual integration
  configuration on `cpp/**`/`integration/**` pushes and `cpp/**` PRs.
- The update is large because every cooked package's bytes changed; the
  changed-files mechanism operates at file granularity, not intra-package.
- SYNC-001/002/003/004/005/006/008/009/010/011 remain open as documented.
