# Validation receipt — engine-library consumer audit (2026-09-22)

Scope: completion of the foundation-expansion consumer audit on
`work/foundation-1-30-codex-integration` through head `2e8e4d4b`
(`native: adopt engine VirtualizedList for colony roster scrolling`).
Successor to [the foundation-merge receipt](2026-09-20-foundation-merge.md).

- Authoritative versions unchanged: game `0.1.14.2-dev`, engine `0.1.64`.
- Machine: Windows x64, MSVC 14.44.35207, Vulkan device present.

## Changes since the merge receipt

- Dead-code audit: superseded helpers removed (`render_planet_outliner` +
  layout rects + `hud_pressed_colony_`/`planet_outliner_scroll_`,
  `planet_summary`, `status_chip`, fleet `set_civilian_return_pending`,
  dead smoke-member declarations). `6d4b2bae`.
- Merge regression fixed: `update_fleet_hover_preview` call restored to the
  PointerMove path (dropped by `0f6637a1`); fleet smoke sequence restored —
  hover asserts a display-only preview, RightPressed arms the confirmable
  route, `handle_fleet_command` clears stale hover state. `cd1b98b8`,
  `eb3e0043`.
- `render_system_environment` survey-gated readout wired into the
  system-visible render branch (born dead in `544ccce0`). `eb3e0043`.
- JobSystem adoption complete: **zero `std::async`** remains in
  app/core/engine. Planet-material decode queue, support-bundle export and
  campaign-session load moved onto tagged `JobSystem` workers
  (promise-bridged results; `JobSystem::submit` takes `std::function`, so
  move-only promises ride `shared_ptr`). Voice SAPI keeps its dedicated COM
  apartment thread — architectural requirement, documented. `b59ff499`,
  `23ab1400`, `d46cbf88`.
- `SpatialGrid` adopted for galaxy `system_hit` (world-space lazy grid keyed
  on `cache().generation`; uniform-affine camera makes world-space nearest
  equivalent to screen-space). `cd1b98b8`.
- `VirtualizedList` adopted for the colony roster scroll (uniform row stride,
  `scroll_to`/`max_scroll` clamps, wheel input). `2e8e4d4b`.
- `PlatformServices` attached in the client (null backend) and surfaced in
  diagnostic bundles; `Profiler` frame bracket + LIVE PERFORMANCE table rows;
  `MemoryTracker` wired for material residency + `memory.json` in support
  bundles; `LocalizationTable` drives the General Settings surface via
  `Data/locale/en.json`; `AccessibilitySettings.reduce_motion` gates cosmetic
  animation. Earlier commits on this branch.
- Documentation corrections: removed stale consumer claims (developer-tools
  `VirtualizedList`, developer-chrome `LocalizationTable`, `CrashReporter`
  install) that predated actual consumers.

## Audit conclusion

Every engine header in `engine/include/stellar/engine/` was classified as
live-consumed, intentionally library-only, or pending a design decision.
The bounded-integration frontier is exhausted:

- **Pending parity review (declined):** massive-combat `SpatialIndex` is a
  3D Chebyshev cell-box scan returning all occupants with id-sorted order;
  `SpatialGrid` is 2D and `SpatialIndex3D` is k-nearest k-d — neither
  reproduces the exact candidate set. Forcing adoption risks deterministic
  ordering. Recorded in `docs/ENGINE_CAPABILITIES.md`.
- **No genuine consumer:** `EventBus` (notification feed already decouples),
  `TableModel` (no sortable table exists), `TreeModel` (research is a DAG;
  the assets Navigator is variable-height), `MissionGraph`,
  `ResourceNetwork`, `Vfx` (eruptions are volumetric, not particle-pool).
- **Needs authoritative-path design review:** `SimulationScheduler`,
  engine `World` store — tick/determinism semantics, not mechanical wiring.
- **Needs the GPU backend layer:** `RenderGraph`, `TextureStreamer`,
  `DrawBatcher`.
- **`CrashReporter` stays library-only:** `RuntimeDiagnostics` owns the
  installed (richer) exception filter.
- `MaterialCache::job_stats()` is consumed by the material tests
  (`submitted == completed` on the tagged worker); runtime surfacing is
  deferred — neither `memory.json` (residency schema) nor LIVE PERFORMANCE
  (per-span schema) fits per-tag job counters without new plumbing.

## Build

`cmake --build --preset windows-native-preview --parallel 4` full-tree:
**PASS** — every configured target links clean under `/WX`, including all
test executables that transitively touch the migrated headers.

## Native tests

`ctest --preset windows-native-preview --parallel 2 --output-on-failure`
on `2e8e4d4b` with fresh binaries: **257/257 passed** (618 s). Earlier
documented-baseline failures (SYNC items) are resolved; the suite is fully
green.

## Graphical smoke

`stellar-continuum-native.exe --load work/fleet.player17.json
--smoke-fleet --frames 120` on a scratch fixture copy (stale `.integrity`
sidecars cause `.bak` recovery — always clean all four sidecars or copy to a
scratch directory): `gpu_driver=vulkan systems=20 frames=120 save=ok
hover=1 civilian_recovery=1 fleet_located=1`. System smoke: `hit=1`
(SpatialGrid path).

## Export validation

`python tools/stellar-export/stellar.py export windows-benchmark` on
`44acfc58` (clean tree, fresh `build-native/headless` configure+build):

- Internal CTest suite: **232/232 passed** (533 s).
- NativeRecovery (29), package-integrity and client dependency-policy Python
  tests: all OK.
- Package sealed: `Builds/Windows/StellarContinuum-0.1.14.2-dev-
  windows-benchmark-44acfc58-*.zip` (3.0 MB, 77 manifest files, sha256
  sidecar, `sourceDirty=false`).
- Relocation checks green: relocatedLaunch, checkpointRoundtrip,
  relocatedGalaxyGeneration, founding/colony seeding.
- Packaged-binary benchmarks deterministic
  (`repeatFinalStatesDeterministic: true`); legacy sim step p95
  0.09–0.51 ms across 250–2500 systems.

Note: during this run the system drive reached 0 bytes free; reclaiming
`build-native/preview/qa-host-tests` seed dumps (~3.5 GB, regenerable test
artifacts) and linker PDBs (~14 GB, regenerated on next build) restored
headroom. Disk exhaustion had failed the first attempt's I/O-bound tests.

## Remaining risks

- `windows-export` CI runs the same CTest suite; the graphical fleet smoke
  is manual evidence only (runners lack a GPU device).
- The pending-design items above are intentional exclusions, not forgotten
  work — each is recorded in `docs/ENGINE_CAPABILITIES.md` with its reason.
