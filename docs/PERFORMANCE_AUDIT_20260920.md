# Performance and obsolete-asset cleanup — 2026-09-20

This pass removes two measured scaling bottlenecks and unnecessary texture
storage. It does not certify unlimited late-game scale or every hardware
configuration. Measurements below are local Windows release-build samples,
not portable performance guarantees. No original artwork or user saves were
deleted, and image dimensions/filtering were not reduced.

## Changes and correctness

Survey planning previously scanned the entire planetary catalog for every
candidate requiring a partial-survey estimate. `SurveyOperationsBatch` computes
the unchanged rules once per planning pass. The original single-query API
remains the parity reference. Source ordering, first duplicate system, orphan
body exclusion and NaN handling are covered.

Economic advancement repeatedly scanned all bodies for each colony's habitat,
demography, reserves and environmental maintenance. `SettlementBodyIndex`
collects requested identities, scans the bodies once, and reuses exact matches
through the existing rule functions. It stores only requested identities and
does not persist across ticks. Civilization and colony accumulation order is
unchanged. The separate credit-flow API creates its own scope; the tick shares
one scope across all civilization flows and colony updates.

Lossless cooked textures previously held both the mip-zero buffer and a decoded
copy of the same bytes. RGBA8 now shares that immutable buffer. CPU/2D loaders
read only the selected mip, avoiding unused lower-level reads and copies.
Compressed 3D planet materials retain their complete selected mip tails.
The 2D GPU cache counts CPU storage plus the RGBA upload rather than doubling
the whole resource. Quality gates, formats, upload pixels and filtering are
unchanged.

## Measurements

| Workload | Before | After | Correctness |
|---|---:|---:|---|
| 1,000 survey queries; 50,000 systems, 350,000 bodies | 1,687.52 ms | 13.14 ms | Every profile field exactly equal |
| Economic update; 5,000 colonies, 350,000 bodies, 25,000 fleet cost records | 11,814.25 ms | 13.85 ms | Same complete colony/economy test-state hash |
| Economic update; 200 colonies, 5,000 bodies, 1,000 fleet cost records | 3.25 ms | 0.37 ms | Same test-state hash |
| Native startup peak working set | 502,398,976 bytes | 412,823,552 bytes | Four static screenshots byte-identical |
| Native startup package reads | 398 | 115 | Zero registry failures; source fallback disabled |
| Native startup decoded bytes | 154,740,359 | 118,760,383 | Selected mip pixels identical |

Economic fleet records here exercise operating costs, not 25,000 moving or
fighting fleets. Synthetic colony fixtures contain a limited surface-building
workload. The large speedups apply to these subsystems and must not be presented
as whole-game speedups. The economic goldens were captured from the production
implementation before its lookup change: large `788429135333795526`, small
`13584217053300038634` (FNV-1a over the existing oracle serializer).

The 500-system, 50-year saved campaign was replayed for 800 ticks from day 18,000
to 18,200. The reference and optimized final saves match exactly after removing
only `SavedAtUtc`. Both pass 202 invariant checks and final save/load validation.
Mean tick timing is approximately 1.9 ms in both builds: this small campaign
does not demonstrate an overall speedup. A previously stranded exploration
fleet remains an operational finding in both results.

The first baseline replay used an older executable whose moon appearance
migration differed from the current source. It is retained as raw evidence but
excluded from canonical parity claims. `reference-late` uses the saved matching
pre-economy-optimization executable; `final-late` uses the final build.

## Scale and visual validation

The 50,000-system functional test produced 355,537 bodies and 89,639 lanes. It
passed graph connectivity and order independence, streaming atomic save, failed
write preservation and reload identity/count checks. Timings in this run were
10.96 s generation, 1.62 s lane building, 1.72 s route/repeat checks, 23.96 s
capture/stream and 34.55 s reload/checks. The uncompressed JSON save was
1,506,509,202 bytes. This is evidence that the data path completes, not evidence
that the largest saves are fast or that large full-AI battles are proven.

Seventeen targeted CTest checks pass: cooker, reach batch, survey batch/large
batch, survey/exploration parity, colony economy/biology/operations parity,
economy scale/large scale, campaign economy parity, galaxy backdrop, native GPU,
native platform and system background.

One additional check, `engine_generation_50000`, fails its historical hard-coded
440,690,994-byte/FNV baseline against the expanded current campaign payload.
Its expectation was left intact. The separate full 50,000-system functional
test passes. Auditing and versioning that old baseline remains outstanding;
this report does not claim a clean full-suite run.

Cooked-only startup/new campaign/audio, saved Sol system and planet-screen
checks pass on Vulkan, using isolated save copies. Package validation covers
all 40,327 chunks across 3,737 assets in seven packages. All successful launches
report no missing assets and `source_fallback=false`.

An initial Sol input replay used a fresh campaign whose home was another system;
its hard-coded Sol double-click did not enter Sol. Repeating with the established
Sol fixture passed entry, picking, pan, zoom, Back, clock controls and focus.
No product input behavior was changed to bypass that test.

Before/after galaxy-selection, galaxy-types, menu and setup PNGs have identical
hashes. The new Sol system and planetary captures were also visually inspected.
The system run measured 17.16 ms p95 frame time and the planet screen 17.00 ms
at 1280x720 with vsync; these are small-scene checks, not late-game render stress.

## Cleanup and distribution

Removed 146 files totaling **8,616,185,815 bytes**:

- 130 rejected starfield runtime duplicates: 340,745,176 bytes. Each matched its
  preserved external original and rejection-audit SHA-256 and was absent from
  the runtime allowlist.
- 14 explicitly superseded generated packages: 8,259,768,523 bytes.
- Two superseded package manifest backups: 15,672,116 bytes.

The cleanup script defaults to a review-only report; `-Apply` rechecks hashes
before deletion. Absolute path containment and linked-path rejection restrict
it to reviewed sky copies and the build's superseded folder. It never recursively
deletes an asset tree. Accepted images, original masters, audit provenance,
current packages, earlier downloads, saves and build inputs remain available.
Rejected giant/ring assets had no local derived runtime copies left to remove.
Disk space reclaimed is development workspace space, not ZIP size reduction.

The rebuilt portable package is `StellarContinuum-Performance-Dev-20260920`, with
`Play Game.cmd` and `Developer Game.cmd` and separate save directories. Extract
the whole archive before launching. Earlier downloads remain intact.

The final ZIP is 4,251,816,168 bytes. All 20 indexed files were verified inside
its 21 entries, both launchers are present, and the staged executable matches
the final build. SHA-256:
`74f77e23ca30d8b6aa6ccffe92f5f58317a601c25e98170258cf06b8a38b79fc`.

Raw evidence is under `work/performance-20260920/`: `final-tests.log`,
`economy-reference.json`, `economy-optimized.json`, `survey-scale.json`,
`replay-parity.json`, `scale-50000.log`, `visual-parity.json`, `native-*`,
`cleanup-applied.json`, `cleanup-manifests.json` and package validation logs.

## Remaining limits

Large save/load latency, full-AI and massive-combat stress, automatic VRAM
budgeting, incremental audio decoding and origin-aware VFX cropping remain
separate work. Existing cache/draw limits are preserved. Compressed 3D materials
can still retain decoded CPU pixels alongside GPU-format mip data when required
by their consumers. This pass does not change schematic orbit spacing or add
advanced ice reflection/refraction physics.
