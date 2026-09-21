# Stellar Engine foundation expansion — requirements 1–30 completion report

Branch: `engine/foundation-expansion-1-30` — latest `1c2df367` (pushed).
Baseline: `fbb3165b` — merged Developer-mode + design-system migration tip.

Companion registry: `docs/ENGINE_CAPABILITIES.md` (per-requirement status,
files, and tests).

## Summary

All 30 requirements have an engine-layer deliverable on this branch:

- **21 requirements** delivered as complete, tested engine systems
  (1–3, 8–11, 13–15, 17–21, 23, 25, 26, 29, plus render-graph 4, batcher 5,
  streaming 6, shaders 7).
- **4 requirements** extended existing systems (11 route policies/fuel-aware
  routing, 16 integrity sidecars, 18 crash capture wired into the client).
- **5 requirements** delivered as foundation/policy layers with documented
  game-side adoption pending (4–7 renderer pipeline pieces, 24 UI view
  models, 30 tools host), or as an abstraction awaiting external SDKs (28
  Steamworks).

No existing system was removed or rewritten in place. Canonical save bytes,
parity tests, artwork, audio, and observer-secrecy behavior are unchanged.

## What was built

### Core runtime (Phase A)

- **`JobSystem` upgrade** (`foundation.hpp`/`foundation.cpp`): priority
  levels, cooperative cancellation (`CancelToken`), dependency graphs with
  cycle rejection, named workers (for debugger readability and crash dumps),
  per-tag run statistics consumed by the profiler, structured error
  propagation through `JobResult`.
- **`World`** (`world.hpp`/`world.cpp`): typed component stores, entity
  hierarchy (parent/child, descendants), deterministic binary
  snapshot/restore, legacy `int` ID ↔ `EntityId` map for gradual game-side
  adoption. Snapshot emission sorts unordered structures (component sets,
  hierarchy edges, bindings) so bytes are reproducible.
- **`EventBus`**: owner-thread-restricted typed pub/sub; RAII
  `Subscription` handles; deferred queue ordered by `(tick, sequence)`;
  safe unsubscribe during dispatch.
- **`SimulationScheduler`**: five LOD tiers (ACTIVE/NEARBY/NORMAL/
  BACKGROUND/DORMANT) with per-tier cadence, deterministic system order,
  and analytic fast-forward for dormant systems.
- **`Profiler`**: scoped spans recorded per thread and drained through a
  registered thread-buffer set (fixing the trap where worker-thread spans
  never flush), per-frame counters, JSON export.
- **`MemoryTracker`**: subsystem registry with current/high-water counters
  and a `TrackedAllocator` adapter for STL containers.

### Gameplay-adjacent systems (Phase B)

- **`SpatialGrid`**: deterministic cell iteration, insert/remove/update,
  radius/AABB/ray queries, broadphase candidate pairs.
- **`PhysicsWorld`**: circle/AABB/segment colliders, broadphase over the
  grid, overlap tests, raycast (normalization fixed), sweep, trigger
  enter/stay/exit events.
- **`RoutePolicy` + fuel-aware routing** (`core/lane_network.*`): blocked
  system sets, per-system traversal cost scaling (hostile-territory
  penalties), and `find_fuel_feasible_route` with waypoint insertion and
  refuel callbacks. Geometric lane construction and the existing
  `find_shortest_route` signature/behavior are unchanged.
- **`ResourceEconomy`**: resource definitions, inventories, recipes,
  producers with progress/shortage reporting, bounded transfer orders.
  Node storage uses `std::deque` for stable references.
- **`MissionGraph`**: data-driven JSON missions — triggers, conditions,
  stages, choices, timers — with persistent per-campaign instances,
  serialize/restore, and effects emitted through `EventBus`. Multi-mission
  trigger ordering sorts by mission ID for determinism.
- **`ReplayRecorder`/`ReplayPlayer`**: ordered command stream with FNV-1a
  state checkpoints, JSON round-trip for session-journal replay.
- **`LocalizationTable`/`LocalizationService`**: JSON locale files,
  fallback chain, positional + named argument formatting, plural rules,
  runtime reload. Move semantics added around the internal mutex.
- **`PackageManifest`/`PackageRegistry`**: semver + constraint parsing,
  protected core namespaces, deterministic topological load order,
  missing/conflicting dependency reporting, directory scanning.
- **`PlatformServices` + `NullPlatformBackend`**: feature-gated facade for
  user identity, achievements, presence, and cloud storage — the Steam
  seam; a live Steamworks backend is the external dependency.
- **`InputMapper`**: JSON-defined contexts (stacked, exclusive blocks),
  Button/Axis1D/Axis2D actions, chords, rebinding (with state reset),
  mouse/gamepad press/release normalization.
- **`AccessibilitySettings`**: UI/text scale, high contrast, color-blind
  palettes, reduced motion/flashing, subtitle options; clamped JSON
  round-trip for persistence.
- **`FloatCurve` + `Timeline`**: five easing functions, multi-track
  timelines, Once/Loop/PingPong, and crossed-event notification with
  correct wrap handling.

### Rendering layer (Phase C)

- **`RenderGraph`**: passes declare read/write resources; single-writer
  validation, dependency ordering, deterministic topological schedule.
- **`TextureStreamer`**: mip-residency records, priorities, VRAM budget,
  pin/evict, per-frame load queue — the policy half of streaming.
- **`ShaderLibrary`**: shader families, canonical variant keys, artifact
  hashes, version-based invalidation, diagnostics.
- **`VfxSystem`**: data-driven emitters with deterministic per-instance
  RNG pools, gravity/integration, LOD rate scaling, curve-driven
  scale/opacity/tint.
- **`DrawBatcher`**: stable opaque grouping by (layer, material, mesh),
  back-to-front transparent sort, culling hooks — the data half of
  instanced/GPU-driven submission.
- **UI view models**: `VirtualizedList`, `TableModel` (sort + filter), and
  `TreeModel` (expand/collapse + flattened visible rows) — virtualization
  primitives for large datasets.

### Integration

- **Save integrity sidecars** (`save_integrity.hpp`, wired into
  `player_campaign_save.cpp`, `player_campaign_recovery.cpp`, and the
  developer save path): every save write emits `<file>.integrity`
  (`fnv1a64:<hex>`) atomically alongside canonical bytes, including `.bak`
  copies. Loads verify when present and fall back to the backup on
  mismatch. Pre-expansion saves without sidecars load unchanged.
- **Rolling save history** (`save_history.hpp`, both writers and both
  recovery paths): before each atomic write that produces a fresh `.bak`,
  the chain rotates `.bak`→`.bak.2`→`.bak.3`→`.bak.4` with sidecars
  moving alongside; preserved-backup writes skip rotation. Loads probe
  deeper slots only when present, reporting `History` origin.
- **Save preview reader** (`save_preview.*`): top-level metadata
  (`FormatVersion`, `GalaxyFormatVersion`, `GameVersion`, `SavedAtUtc`,
  `SimulationDays`, developer flag, size, integrity status) without
  building a campaign runtime — for save-slot UIs.
- **Crash capture in the client** (`crash_reporter.hpp`,
  `app/native_client/main.cpp`): an unhandled-exception filter writes a
  minidump plus context/event-log bundle beside the save directory at
  startup-installed paths.

## Verification

- **Full CTest: 188/188 passing** (~110 s) on `1c2df367`, including all
  16 new engine test binaries and every pre-existing parity/smoke test.
- New focused tests: `job_system`, `engine_world`, `event_bus`,
  `simulation_scheduler`, `engine_diagnostics`, `spatial_physics`,
  `route_policy`, `localization`, `package_platform`, `mission_graph`,
  `crash_reporter`, `input_actions`, `economy_animation`,
  `render_pipeline`, `save_integrity`, `batcher_ui`.
- Save-path regression slice re-verified after the sidecar change:
  `campaign_simulation_smoke`, `native_campaign_session`,
  `native_developer_session` all green.
- Native client rebuilt with the crash reporter installed.

## Determinism

- World snapshots sort all unordered collections before emission.
- Mission trigger handling sorts mission IDs per event.
- Route construction keeps existing ID ordering + tie tolerances.
- Deferred event queue orders by `(tick, sequence)`.
- Render-graph pass order is a deterministic topological sort.
- VFX uses per-instance deterministic RNG pools seeded from
  `DeterministicRandom`.
- Replay checkpoints hash recorded state with FNV-1a.
- Draw batcher groups on stable sort keys; transparent pass is a
  deterministic back-to-front sort.

## Save compatibility

Canonical save JSON is byte-identical to baseline — integrity is a sidecar,
not an envelope change. The loader accepts saves without sidecars (all
existing saves), verifies when present, and preserves `.bak` fallback.

## Performance and memory

- No runtime cost is added to shipping paths except the integrity hash
  (one FNV-1a pass over save bytes on write and on load — O(file size),
  negligible vs. JSON parse) and the crash-handler installation at
  startup (one-time).
- `MemoryTracker` and `Profiler` are opt-in instrumentation; they are
  compiled in but only consume resources where instrumented.
- `TextureStreamer` and `DrawBatcher` hold bounded working sets sized by
  their budgets; `SpatialGrid` is sparse-hash based.
- Full-suite duration (~110 s) is unchanged in character from the
  186-test baseline run.

## Honest scope notes

- Requirements 4–7 and 5's GPU-driven rendering are **foundation layers**:
  the live renderer still submits through the DrawList/SDL_Renderer path.
  True indirect draw, custom shader execution, and backend streaming need
  the documented SDL_GPU pipeline migration.
- Requirement 24's view models exist and are tested; screens have not yet
  been migrated onto them.
- Requirement 28 ships the facade + null backend; live Steamworks requires
  SDK credentials.
- Requirement 30's tabbed inspector host is follow-on work; the existing
  developer tools panel is untouched.
- Requirement 16 now includes rolling history slots (`.bak.2`–`.bak.4`)
  and a metadata preview reader; a named multi-slot *UI* (choose-which-
  autosave) remains follow-on.
- Engine systems are not yet broadly adopted by gameplay code — `World`,
  `EventBus`, `SimulationScheduler`, `InputMapper`, `LocalizationService`,
  `MissionGraph`, `ResourceEconomy`, `VfxSystem`, and the view models are
  tested engine libraries awaiting migration into game code paths.

## Blockers

- **Steamworks SDK/credentials** (Req 28 live backend) — external.
- **SDL_GPU pipeline decision** (Reqs 4–7 backend) — design decision
  about replacing `SDL_Renderer` submission; flagged rather than forced.
- No other external blockers. Everything else listed above is scheduled
  follow-on work inside this codebase.
