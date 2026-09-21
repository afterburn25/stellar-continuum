# Known issues — native branch, 2026-09-20

Scope: `cpp/codex-native-architecture-integration`. The
[verification receipt](validation/2026-09-20-development-sync.md) contains exact
current totals and every failed CTest name. Severity refers to development/release
impact, not a claim that every failure reproduces in normal play. **Do not label
all save/fixture differences harmless without inspecting their semantics.**

## SYNC-001 — generation baselines differ

- **Subsystem / severity:** Core galaxy generation; High release-validation blocker.
- **Description:** the complete baseline campaign fingerprint and a smaller
  outside-small-body-extension invariant fail after a full rebuild.
- **Reproduction:** `ctest --preset windows-native-preview -R "^(engine_generation_50000|large_galaxy_2500|fresh_campaign_parity)$" --output-on-failure`.
- **Suspected cause:** expanded celestial/classification/payload state versus
  historical fixture expectations; exact semantic differences need review.
- **Relevant files:** `native-tests/fresh_campaign_tests.cpp`,
  `native-tests/large_galaxy_tests.cpp`, generation benchmark tests, Core galaxy/
  planetary generation and appearance. Locate test commands in CTest inventory.
- **Workaround:** no correctness waiver. Inspect reproducible state diffs before
  updating any expected fingerprint.
- **Status:** OPEN; expected values deliberately not changed in this audit.

## SYNC-002 — save round-trip state changes

- **Subsystem / severity:** Core persistence/appearance and consuming workflows;
  High release-validation blocker.
- **Description:** rebuilt tests report differences across save/load, including
  added `PlanetAppearance` records. Native progression/research and recovery
  contracts are among affected consumers; see the exact failed-test receipt.
- **Reproduction:** `ctest --preset windows-native-preview -R "(player_campaign|native_fresh_progression|native_research_controller|developer_qa_host)" --output-on-failure`.
- **Suspected cause:** current appearance/migration enrichment is applied during
  load to state or fixtures lacking that enrichment. Idempotence, schema boundary
  and intended semantic changes must be checked; not yet root-caused completely.
- **Relevant files:** `core/src/player_campaign_json.cpp`,
  `core/src/player_campaign_persistence.cpp`, `core/src/planet_appearance.cpp`,
  `core/src/planetary_catalog.cpp`, corresponding persistence/native tests.
- **Workaround:** preserve original saves and test copies. Do not certify a release
  solely from a successful graphical launch.
- **Status:** OPEN; no gameplay or migration change made during documentation task.

## SYNC-003 — Python packaging fixture omissions

- **Subsystem / severity:** Export verification; High validation blocker.
- **Description:** `test_native_client_runtime` constructs temporary roots without
  `data/planets/planet-art-v1.json`; the expanded exporter now requires it.
  Many subtest errors share this cause. Parent-test and subtest counts differ.
- **Reproduction:** `python -m unittest discover -s tools/stellar-export -p "test_native_client_runtime.py"`.
- **Suspected cause:** synthetic packaging fixtures were not expanded with the
  production planet/moon/content contract.
- **Relevant files:** `tools/stellar-export/test_native_client_runtime.py`,
  `native_client_runtime.py`, `native_planet_runtime.py` in the same directory.
- **Workaround:** real cooked-release evidence is separately recorded, but does
  not replace these fixture checks. Repair fake roots and retain negative tests.
- **Status:** OPEN; tests are not disabled or expected failures.

## SYNC-004 — galaxy-art audit key and Sol ordering assumptions

- **Subsystem / severity:** Python import/export tests; Medium.
- **Description:** widescreen import test cannot find an irregular-galaxy source
  filename in the edit map. Native recovery test expects the final body to be
  Pluto; expanded Sol ends with Charon.
- **Reproduction:** run the full Python suite with `STELLAR_NATIVE_EXE` pointing
  at the freshly built headless executable.
- **Suspected cause:** source-name/metadata drift and an ordering assumption that
  predates the added moons. Review identity-based assertions instead of merely
  changing names to silence the failures.
- **Relevant files:** `test_galaxy_asset_import.py`, `test_export.py`,
  `export/galaxy-asset-edits.json`, `data/stellar/galaxy-visuals-v1.json`.
- **Workaround:** none for a clean test gate.
- **Status:** OPEN. On `integration/foundation-1-30-into-codex` the edit-map
  key mismatch is fixed (`galaxy-asset-edits.json` now escapes non-ASCII keys
  so cp1252 checkouts match the manifest's `\u2014` entries); the test then
  correctly reports the still-absent `assets/source/galaxies-16x9/*.png`
  review masters. The Sol-ordering assertion is unchanged.

## SYNC-005 — missing-file error contract in research tests

- **Subsystem / severity:** Catalog/resource IO and native research tests; Medium.
- **Description:** negative-input fixtures fail through filesystem `file_size`
  exceptions for deliberately absent applicability/facility/agenda/outcome files.
- **Reproduction:** `ctest --preset windows-native-preview -R "^adaptive_research_(applicability|policy|agenda|outcomes)_parity$" --output-on-failure`.
- **Suspected cause:** catalog reading moved through the Engine resource boundary,
  changing exception behavior expected by existing tests; inspect full traces.
- **Relevant files:** Engine `asset_registry.cpp`; Core adaptive research catalog/
  applicability readers; corresponding `native-tests/adaptive_research_*` tests.
- **Workaround:** none; callers and negative tests need an explicit error contract.
- **Status:** OPEN; no exception suppression introduced.

## SYNC-006 — broad developer graphical smoke remains red

- **Subsystem / severity:** Native celestial presentation/QA; High visual-validation blocker.
- **Description:** prior broad `--developer-smoke` stopped at
  `3D screen did not submit the supplied planet map.` Targeted cooked flare and
  saved-campaign zoom replays passed, which does not settle this assertion.
- **Reproduction:** run the native `--developer-smoke` in an isolated output/save
  directory following [the crash report](STAR_MAP_ZOOM_CRASH_FIX_20260920.md).
- **Suspected cause:** canonical material/texture-size expectation (including a
  1024-width check) versus current supplied maps. Not proven to be test-only.
- **Relevant files:** `app/native_client/main.cpp`, native planet globe/materials.
- **Workaround:** inspect actual captured image/material identity before altering
  the assertion. Targeted `--eruption-smoke` covers only the zoom/flare path.
- **Status:** OPEN. Reproduced on `integration/foundation-1-30-into-codex`
  (2026-09-20): `--developer-smoke` passes celestial index, empire monitor and
  live-reveal checks, then stops at the same planet-map assertion.

## SYNC-007 — CI still references the retired surface workspace

- **Subsystem / severity:** Native build/release workflow; High CI blocker.
- **Description:** `stellar-engine.yml` still requests removed surface targets;
  its push filter covers `engine/**`, not the current `cpp/**` branch.
  Historical Godot workflows do not validate native code.
- **Reproduction:** inspect/dispatch the native workflow against this branch;
  compare explicit target names with current CMake.
- **Suspected cause:** pipeline lag after planetary-screen consolidation.
- **Relevant files:** `.github/workflows/stellar-engine.yml`,
  `cmake/StellarNativeClient.cmake`, `tools/stellar-export/stellar.py`.
- **Workaround:** use the current local build/CTest commands and manual GPU cook
  workflow; do not claim this is equivalent to green hosted CI.
- **Status:** RESOLVED on `integration/foundation-1-30-into-codex`
  (2026-09-20). The workflow now triggers for `cpp/**` and `integration/**`
  pushes and `cpp/**` pull requests, builds the full `windows-native-preview`
  preset (no retired explicit target list), and runs the entire CTest suite
  with only the 21 receipt-documented baseline failures excluded. Hosted CI
  has not yet run the updated workflow; the `-E` exclusion list must be kept
  in sync with the receipt until the SYNC items are fixed.

## SYNC-008 — stale runtime metadata label

- **Subsystem / severity:** Build/status metadata; Low.
- **Description:** `export/runtime-config.json` still says `mode: headless-foundation`
  although a real graphical client exists. `graphicalParity: false` must not be
  flipped to true merely because the client runs.
- **Reproduction:** compare the JSON with native CMake targets and current source.
- **Suspected cause:** incremental metadata lag.
- **Relevant files:** `export/runtime-config.json` and generated build version.
- **Workaround:** use this handoff's verified architecture/status; plan a reviewed
  metadata cleanup without claiming complete graphical parity.
- **Status:** OPEN, explicitly documented.

## SYNC-009 — combined late-game performance is not certified

- **Subsystem / severity:** Simulation/save/assets; Medium risk, not a reproduced universal crash.
- **Description:** scoped generation/spatial/save/colony measurements do not prove
  that a campaign with massive fleets, AI, research, combat and autosave is smooth.
  Large JSON saves, fixed residency limits and full-track audio decode remain costs.
- **Reproduction:** define and run a reproducible combined scenario; existing
  scale-test names/receipts describe only their own workload.
- **Suspected cause:** workload interactions and unmeasured bottlenecks.
- **Relevant files:** campaign frame/runtime, persistence, massive combat,
  image/GPU caches; [performance audit](PERFORMANCE_AUDIT_20260920.md).
- **Workaround:** bounded caches/phase timings and scoped indexes already exist;
  do not promise they remove all late-game lag.
- **Status:** OPEN measurement/optimization work.

## SYNC-010 — stellar engulfment invariant fails at 1,000 systems

- **Subsystem / severity:** Core stellar/planet generation; High correctness blocker.
- **Description:** `stellar_objects` passes its 250/500-system steps but stops
  during the 1,000-system step with `No intact engulfed planet`.
- **Reproduction:** `ctest --preset windows-native-preview -R "^stellar_objects$" --output-on-failure`.
- **Suspected cause:** interaction between generated stellar radius, orbital
  placement and planet survival/classification; not root-caused by this audit.
- **Relevant files:** `native-tests/stellar_object_tests.cpp`, Core stellar
  objects and planet generation. Use the CTest source registration to locate the test.
- **Workaround:** none established; preserve the invariant and investigate the seed.
- **Status:** OPEN, reproduced after the full build.

## SYNC-011 — headless acceleration/checkpoint continuation diverges

- **Subsystem / severity:** Developer QA clock/persistence; High replay blocker.
- **Description:** `developer_qa_host` ends with `Headless acceleration/checkpoint
  continuation diverged.` The diff includes stellar activity events. This is
  distinct from the generic appearance enrichment failures.
- **Reproduction:** `ctest --preset windows-native-preview -R "^developer_qa_host$" --output-on-failure`.
- **Suspected cause:** accelerated or resumed activity-clock ownership/state;
  inspect the checkpoint diff before claiming deterministic equivalence.
- **Relevant files:** `app/developer_qa_host.cpp`, Core developer campaign,
  campaign frame, stellar activity and Developer persistence.
- **Workaround:** none for replay certification; keep checkpoints and diagnostics.
- **Status:** OPEN, reproduced after the full build.

## Build defects corrected during synchronization

- **SYNC-R01 / resolved:** MSVC C1128 in the expanded persistence-test translation
  unit. Target-specific `/bigobj` added; no test/source semantics changed.
- **SYNC-R02 / resolved:** `stellar_native_audio_tests` could not resolve Engine
  `read_resource`. Added the missing native-audio link to `stellar_engine`.
- **SYNC-R03 / resolved:** fresh checkout rejected the shader hash because local
  CRLF bytes differed from committed LF bytes. Corrected the manifest without
  changing shader contents or compiled SPIR-V; seven existing PNGs were also
  normalized to LFS pointers. The isolated checkout then configured successfully.
- These fixes permit the all-target build; they do not resolve the failures above.

## Build defects corrected during the foundation-expansion merge

- **SYNC-R04 / resolved:** merge conflict resolution dropped the
  `scene_target=next;` assignment in `native_map_platform.cpp`, so the
  supersampled render target was never bound (`native_video_platform`
  regression). Assignment restored.
- **SYNC-R05 / resolved:** history-slot recoveries
  (`PlayerCampaignLoadOrigin::History`) were not reported as recovered by the
  session layer. Startup and async loads now publish the recovered notice for
  every non-primary origin (`native_campaign_session` regression).
- **SYNC-R06 / resolved:** the planet-disc test fixture root was pointed at
  `assets/visual` while per-world art lives under `assets/visual/sol`.
- **SYNC-R07 / resolved:** packaging scripts (`build-cooked-game.ps1`,
  `build-release-installer.ps1`, `build-update-installer.ps1`) required
  PowerShell 7 (`Path.GetRelativePath`, `Convert.ToHexString`,
  `SHA256::HashData`, `utf8NoBOM`, `Measure-Object` on hashtable keys) and
  emitted `CR CR LF` into `.cmd` files on `core.autocrlf` checkouts. They now
  run on Windows PowerShell 5.1 with identical output bytes; full release and
  changed-files update were built and validated with them.

Additional failed assertions in the machine-readable receipt remain open under
their subsystem owners even if not individually root-caused here. The installed
zoom-crash fix is documented separately; no user save was changed by this audit.
