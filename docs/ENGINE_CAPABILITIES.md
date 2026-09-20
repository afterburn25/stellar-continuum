# Stellar Engine capability registry — foundation expansion 1–30

Baseline: `fbb3165b` (merged Developer-mode + design-system line, 172/172 CTest,
425 Python, sealed export `6ad1650b` green). Work branch:
`engine/foundation-expansion-1-30` — latest `1c2df367`, 188/188 CTest green.

Status vocabulary: **MISSING** (greenfield), **PARTIAL** (exists but does not
meet the requirement), **PRESENT** (meets the requirement), **EXTERNAL**
(engine side complete, outside dependency pending).

| # | Capability | Prior state | Current state | Files | Tests |
|---|---|---|---|---|---|
| 1 | Unified entity/world | PARTIAL — `EntityId`/`EntityRegistry` only | **ENGINE-COMPLETE** — `World` store: components, hierarchy, queries, binary snapshot/restore, legacy ID map. Game-side adoption pending. | `engine/…/world.hpp`, `engine/src/world.cpp` | `engine_world` |
| 2 | Simulation scheduler + LOD | PARTIAL — `StrategicClock`, frame routing | **ENGINE-COMPLETE** — `SimulationScheduler`: tier policies (ACTIVE/NEARBY/NORMAL/BACKGROUND/DORMANT), cadence, deterministic ordering, dormant analytic skip. Integration into campaign frame pending. | `engine/…/simulation_scheduler.hpp` | `simulation_scheduler` |
| 3 | Job/threading system | PARTIAL — FIFO+futures | **ENGINE-COMPLETE** — priorities, cooperative cancellation, dependency graphs, named workers, per-tag stats, error propagation. Wider adoption pending. | `engine/…/foundation.hpp`, `foundation.cpp` | `job_system` |
| 4 | Render graph | MISSING | **ENGINE-COMPLETE (policy layer)** — `RenderGraph`: pass/resource declarations, single-writer validation, dependency+ordering edges, deterministic topological order. Backend adoption pending (DrawList layer today; SDL_GPU follow-on). | `engine/…/render_graph.hpp` | `render_pipeline` |
| 5 | GPU-driven rendering | MISSING | **PARTIAL** — `DrawBatcher`: stable opaque (layer,material,mesh) batching, back-to-front transparent sort, culling hooks. True indirect draw requires the SDL_GPU pipeline follow-on. | `engine/…/draw_batcher.hpp` | `batcher_ui` |
| 6 | Texture streaming | PARTIAL — bounded LRU caches, sync decode | **ENGINE-COMPLETE (policy layer)** — `TextureStreamer`: mip residency, priorities, VRAM budget, pin/evict, per-frame load queue. Backend consumption pending. | `engine/…/texture_streaming.hpp` | `render_pipeline` |
| 7 | Shader library + cache | MISSING — SDL built-ins only | **ENGINE-COMPLETE (management layer)** — `ShaderLibrary`: families, canonical variant keys, artifact hashes, version invalidation, diagnostics. Consumption pending SDL_GPU pipeline. | `engine/…/shader_library.hpp` | `render_pipeline` |
| 8 | Particle/VFX framework | MISSING — procedural flares | **ENGINE-COMPLETE** — `VfxSystem`: data-driven emitters, deterministic per-instance RNG pools, gravity/integration, LOD rate scaling, curve-driven scale/opacity/tint. Flare migration pending. | `engine/…/vfx.hpp`, `vfx.cpp` | `render_pipeline` |
| 9 | Physics layer | MISSING — combat-only grid | **ENGINE-COMPLETE** — `PhysicsWorld`: circle/AABB/segment primitives, broadphase over SpatialGrid, overlap/raycast/sweep, trigger enter/stay/exit events. | `engine/…/physics.hpp`, `physics.cpp` | `spatial_physics` |
| 10 | Spatial query framework | PARTIAL — private combat index | **ENGINE-COMPLETE** — `SpatialGrid`: deterministic cell order, insert/remove/update, radius/AABB/ray queries, broadphase candidates. Combat-index adoption pending parity review. | `engine/…/spatial_index.hpp` | `spatial_physics` |
| 11 | Route engine | PRESENT-PARTIAL | **EXTENDED** — `RoutePolicy` (blocked sets, per-system traversal cost = hostile-territory penalties) + `find_fuel_feasible_route` waypoint insertion with refuel callbacks. | `core/lane_network.*` | `route_policy` |
| 12 | Knowledge/FoW | PRESENT-PARTIAL | **UNCHANGED** — `CivilizationKnowledgeState` covers observer filtering; per-callsite discipline retained. | `core/knowledge.*` | `settlement_knowledge_parity` |
| 13 | Generic economy/resources | MISSING — per-resource fields | **ENGINE-COMPLETE** — `ResourceDefinition`/`Inventory`/`Recipe`/`Producer`/`TransferOrder`/`ResourceNetwork` with shortage reporting and bounded transfers. | `engine/…/resource_economy.hpp` | `economy_animation` |
| 14 | Event bus | PARTIAL — `EventQueue<T>` | **ENGINE-COMPLETE** — `EventBus`: typed subscribe, RAII `Subscription`, deferred tick-ordered queue, owner-thread enforcement. Core event-flow adoption pending. | `engine/…/event_bus.hpp` | `event_bus` |
| 15 | Mission/event framework | MISSING | **ENGINE-COMPLETE** — `MissionGraph`: JSON-defined triggers/conditions/stages/choices/timers, persistent instances, serialize/restore, effects emitted via EventBus. | `engine/…/mission_graph.hpp` | `mission_graph` |
| 16 | Advanced saves | MOSTLY PRESENT | **EXTENDED** — fnv1a64 integrity sidecars (atomic, incl. `.bak`), rolling history `.bak.2`..`.bak.4` with loader fallback, `read_player_campaign_preview` metadata reader (player + developer envelopes). Existing: v17 schema, migrations, autosave scheduler, async writer. | `engine/…/save_integrity.hpp`, `save_history.hpp`, `core/save_preview.*`, `core/player_campaign_*.cpp` | `save_integrity`, `save_history` |
| 17 | Deterministic replay | MISSING | **ENGINE-COMPLETE** — `ReplayRecorder`/`ReplayPlayer`: ordered command stream, FNV checkpoints, JSON round-trip. Session-journal integration pending. | `engine/…/replay.hpp` | `economy_animation` |
| 18 | Crash reporter | PARTIAL — support bundle only | **INTEGRATED** — `CrashReporter`: unhandled-exception filter, minidump + context/event-log bundle, installed at client startup beside save dir. | `engine/…/crash_reporter.hpp`, `app/native_client/main.cpp` | `crash_reporter` |
| 19 | Profiler | MISSING | **ENGINE-COMPLETE** — `Profiler`: scoped spans, per-frame counters, thread-buffer drain, JSON export. Overlay + app instrumentation pending. | `engine/…/profiler.hpp` | `engine_diagnostics` |
| 20 | Memory tracking | MISSING | **ENGINE-COMPLETE** — `MemoryTracker`: subsystem registry, high-water marks, `TrackedAllocator` adapter, JSON export. Adoption pending. | `engine/…/memory_tracker.hpp` | `engine_diagnostics` |
| 21 | Input actions | PARTIAL — raw events | **ENGINE-COMPLETE** — `InputMapper`: JSON contexts (stacked, exclusive), Button/Axis1D/Axis2D actions, chords, rebinding, gamepad kinds. SDL adapter wiring pending. | `engine/…/input_actions.hpp` | `input_actions` |
| 22 | Audio engine | PARTIAL — CPU mixer | **UNCHANGED** — existing 48kHz mixer + SAPI voice retained; bus abstraction deferred. | `native_audio*`, `native_voice*` | `native_audio` |
| 23 | Animation | MISSING | **ENGINE-COMPLETE** — `FloatCurve` (5 easings), `Timeline` tracks + loop modes (Once/Loop/PingPong) + crossed events. Skeletal blending out of scope. | `engine/…/animation.hpp` | `economy_animation` |
| 24 | Advanced UI | PARTIAL — theme helpers | **PARTIAL** — `VirtualizedList`, `TableModel` (sort/filter), `TreeModel` (expand/flatten) added; screen adoption pending. | `engine/…/ui_viewmodels.hpp` | `batcher_ui` |
| 25 | Localization | MISSING | **ENGINE-COMPLETE** — `LocalizationTable`/`LocalizationService`: JSON locales, fallback chain, positional+named formatting, plurals, runtime reload. UI string migration pending. | `engine/…/localization.hpp` | `localization` |
| 26 | Accessibility | PARTIAL — subtitle size | **ENGINE-COMPLETE (settings layer)** — `AccessibilitySettings`: ui/text scale, high contrast, color-blind modes, reduced motion/flashing, subtitles; sanitize + JSON round-trip. Presentation adoption pending. | `engine/…/accessibility.hpp` | `economy_animation` |
| 27 | Platform layer | PARTIAL — Win32+SDL+GDI | **UNCHANGED-PARTIAL** — existing paths/atomic-write/image layer retained; `PlatformServices` (Req 28) adds the services seam. Full OS abstraction documented as follow-on. | `engine/*` | — |
| 28 | Steam layer | MISSING | **ENGINE-COMPLETE — EXTERNAL** — `PlatformServices` facade + `NullPlatformBackend`; feature gating, user identity, achievement/presence/cloud calls. Live Steamworks SDK backend pending credentials. | `engine/…/platform_services.hpp` | `package_platform` |
| 29 | Mod architecture | MISSING | **ENGINE-COMPLETE** — `PackageManifest` (semver, deps, provides), `PackageRegistry` (protected namespaces, deterministic topo load order, conflict reporting), `scan_packages` directory discovery. | `engine/…/package.hpp` | `package_platform` |
| 30 | Stellar Tools | PARTIAL — dev submenu + panel | **UNCHANGED** — existing DEVELOPMENT submenu + tools panel retained; tabbed inspector host is the documented follow-on. | `native_development_menu.*`, `native_developer_tools.*` | `native_developer_session` |

## Notes

- `engine/foundation.hpp` primitives are scaffolding: `EntityRegistry`,
  `FixedClock`, `DeterministicRandom`, `EventQueue` are used only by
  `headless_main` + tests; `JobSystem` is used once (save writer). A major
  theme of this expansion is adopting/extending rather than duplicating them.
- Renderer constraint: the GPU path is SDL3's 2D `SDL_GPURenderer` over a
  Vulkan device — no custom pipelines/shaders. Requirements 4–7 are therefore
  implemented at the DrawList/pass layer with real batching, streaming policy
  and shader-asset management; a raw `SDL_GPU` pipeline migration is the
  documented follow-on for true indirect draw / custom shader execution.
- Save compatibility is frozen by the Player17 contract; save upgrades are
  additive (sidecars/envelopes), never reinterpretation. The integrity
  sidecar (`<save>.integrity`, `fnv1a64:<hex>`) verifies on load when present
  and is silently absent for pre-expansion saves.
