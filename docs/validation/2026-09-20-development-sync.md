# Development synchronization verification — 2026-09-20

**Build succeeds; the full test gate fails. This is a development-state handoff,
not a release certification.** Current architecture: custom Stellar Engine,
C++23 Engine and C++23 game. See [project state](../PROJECT_STATE.md),
[known issues](../KNOWN_ISSUES.md) and [machine-readable results](2026-09-20-development-sync.json).

## Scope and Git evidence

Repository `afterburn25/stellar-continuum`, existing branch
`cpp/codex-native-architecture-integration`. Audit began at `36ac3205`, nine
commits ahead of the fetched origin, with extensive uncommitted source/data/art
and documentation. The user expressly authorized including current source,
tests and build changes. No branch was force-pushed, shared history rewritten,
or work on other branches declared integrated.

Source/art/build synchronization commits:

- `bd7f9621` — assets: publish reviewed native runtime artwork with Git LFS
- `544ccce0` — feat: synchronize native engine, celestial content, diagnostics and maintenance
- `84159ebe` — build: enable large COFF objects for campaign persistence tests
- `7552d3f0` — build: link native audio resources and fetch LFS inputs in native CI

Verified code baseline: `7552d3f0d5ac7b8b4a4d80dbb5222df3d9e11e92`. Documentation is committed
after that baseline. The branch contains the previous nine commits too. Exact
local/remote/tag inventory is in the JSON receipt; local worktree locations are
omitted. GitHub Releases returned an empty list. The observed migration-baseline
tag is historical, not a tag for this development snapshot. No release is made.

## Build and tools

Windows x64, Visual Studio 2022 Build Tools, MSVC 19.44 / toolset 14.44.35207,
C++23, Ninja, preset `windows-native-preview`, RelWithDebInfo,
`BUILD_TESTING=ON`, `STELLAR_BUILD_NATIVE_CLIENT=ON`; pinned SDL3 3.4.16.
Python 3.14.7. Compiler environment came from `VsDevCmd.bat -arch=x64 -host_arch=x64`.

```bat
cmake --preset windows-native-preview
cmake --build --preset windows-native-preview --parallel 4
ctest --preset windows-native-preview --parallel 2 --output-on-failure
set STELLAR_NATIVE_EXE=%CD%\build-native\preview\stellar-continuum.exe
python -m unittest discover -s tools/stellar-export -p "test_*.py"
```

The first all-target attempt failed with MSVC C1128 (persistence-test COFF
section count). The second reached an unresolved `read_resource` audio link.
Two narrowly scoped CMake fixes resolved those defects. The final all-target
build completed, including native game, headless host, tools, setup/uninstall
and registered test binaries. Final tests used that successful build.
An earlier test pass count from a partially rebuilt directory is **not** used
as acceptance evidence. This was an all-target incremental development build,
not a claimed clean-room rebuild of every unchanged translation unit.

Warnings remain: CMake CMP0175 at `NativePlanetAssets.cmake:15` (missing explicit
custom-command phase); MSVC C4996 (`filesystem::u8path`) in import/cook tools,
C4456 local shadowing in construction tests, C4244 double/float in giant visual
tests, and C4127 at `installer/src/main.cpp:59`. The first four compiler categories
refer to the whole build-attempt sequence; the final attempt logged C4127.
No warnings were suppressed to obtain this result.

## Final automated results

| Suite | Total | Passed | Unsuccessful | Skipped | Unavailable | Time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Registered native CTest | 234 | 213 | 21 | 0 | 0 | 390.33 s |
| Python exporter/tool parent tests | 549 | 513 | 36 | 0 | 0 | 33.312 s |

Python unittest reports **1 assertion-failure event and 129 error events**.
Parameterized subtests produce multiple events within one parent; adding those
events to 549 would be incorrect. Of the error events, 128 fail because temporary
packaging roots omit `data/planets/planet-art-v1.json`, and one is the galaxy-art
filename KeyError. The assertion expects Pluto where the expanded Sol list ends
with Charon. No test was converted to skipped or expected-failure status.

Every failed native test, from the final JUnit output:

| Test | Observed failure | Issue |
| --- | --- | --- |
| `stellar_objects` | No intact engulfed planet at the 1,000-system step | SYNC-010 |
| `developer_qa_host` | Headless acceleration/checkpoint continuation diverged; diff includes stellar activity | SYNC-011 |
| `fresh_campaign_parity` | Fresh campaign Bodies count differs | SYNC-001 |
| `persistable_fresh_campaign_parity` | Persistable fresh campaign Bodies count differs | SYNC-001 |
| `sol_catalog` | Assertion still expects eight planets, Moon and Pluto only | SYNC-001 |
| `galaxy_catalog_parity` | Array index 10 is out of range | SYNC-001 |
| `civilization_parity` | Founding campaign Bodies array size changed | SYNC-001 |
| `adaptive_research_applicability_parity` | file_size exception for deliberately missing applicability_traits.json | SYNC-005 |
| `adaptive_research_policy_parity` | file_size exception for deliberately missing missing_facility.json | SYNC-005 |
| `adaptive_research_agenda_parity` | file_size exception for deliberately missing research_agenda_model.json | SYNC-005 |
| `adaptive_research_outcomes_parity` | file_size exception for deliberately missing research_outcome_runtime_policy.json | SYNC-005 |
| `galaxy_payload_persistence_parity` | Restored runtime bodies size differs | SYNC-002 |
| `galaxy_payload_json_parity` | Restored actual-source bodies size differs | SYNC-002 |
| `legacy_galaxy_payload_persistence_parity` | Legacy postmigration bodies size differs | SYNC-002 |
| `player_campaign_persistence_parity` | Capture-valid live bodies size differs | SYNC-002 |
| `player_campaign_json_parity` | Current17 migrated body count changed | SYNC-002 |
| `player_campaign_recovery_parity` | Restored state adds PlanetAppearance | SYNC-002 |
| `native_research_controller` | Cancelled research changed across full save/load | SYNC-002 |
| `native_fresh_progression` | Progressed Player17 roundtrip adds PlanetAppearance | SYNC-002 |
| `large_galaxy_2500` | Existing galaxy changed outside the small-body extension | SYNC-001 |
| `engine_generation_50000` | Indexed generation changed the complete baseline campaign | SYNC-001 |

These observations are not blanket proof of stale fixtures. Save enrichment,
engulfment safety and activity-clock continuation need semantic investigation.
The JSON receipt records the result and duration of every registered native test
and the outcome of every Python parent test.

## Coverage boundaries

Real GPU, audio, Windows maintenance and scene tests were available and ran.
Retired Godot/.NET fixture generation was not run; those legacy suites are not
part of the 234 registered native tests. Hosted CI was not run: the native
workflow still has obsolete explicit targets and an unsuitable push filter.
LFS checkout was added, but that does not make CI green.

Prior installed/cooked zoom and flare replays are separately documented in
[the crash investigation](../STAR_MAP_ZOOM_CRASH_FIX_20260920.md). They were not
rerun here. The prior broad `--developer-smoke` still has an unresolved supplied
planet-map assertion (SYNC-006). No new visual sign-off, combined late-game
benchmark, full release cook or installer delivery is claimed by this audit.
No real player save or installed game was modified by synchronization.

## Publication hygiene and artwork

The initial loose asset inventory contains 7,187 files / 3,759,528,000 bytes.
6,010 newly published reviewed runtime/build inputs total 3,561,621,863 bytes;
their images use Git LFS. Upload completed for 4,107 unique LFS objects
(deduplication makes object count differ from file count).
736 untracked review-only maps/unused original images, totaling 44,172,594 bytes,
remain local and are not deleted. Their exact paths are recorded in the JSON
receipt. They are the intended non-clean portion of the original working tree.

No cooked package, installer binary, temporary ZIP, runtime capture, private
credential or user save is included. Existing source/fixture/history files are
preserved. Hash-pinned provenance documents keep their reviewed bytes; current
architecture warnings live in entry documents and notices on historical docs.
Final publication and independent-checkout checks are recorded below when done.
