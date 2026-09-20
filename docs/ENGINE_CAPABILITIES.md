# Stellar Engine capability registry — foundation expansion 1–30

Baseline: `fbb3165b` (merged Developer-mode + design-system line, 172/172 CTest,
425 Python, sealed export `6ad1650b` green). Work branch:
`engine/foundation-expansion-1-30`.

Status vocabulary: **MISSING** (greenfield), **PARTIAL** (exists but does not
meet the requirement), **PRESENT** (meets the requirement), **EXTERNAL**
(engine side complete, outside dependency pending).

| # | Capability | Prior state | Files (before) | Work this expansion |
|---|---|---|---|---|
| 1 | Unified entity/world | PARTIAL — `EntityId`/`EntityRegistry` exist but game uses int IDs in vectors; no components/hierarchy/queries | `engine/…/foundation.hpp`, `core/fresh_campaign.hpp` | engine world store + hierarchy + queries + ID mapping |
| 2 | Simulation scheduler + LOD | PARTIAL — `StrategicClock` speeds {0,1,2,3,8,24}, `CampaignFrame` routing, per-civ AI review throttle; no LOD tiers | `core/strategic_clock.hpp`, `core/campaign_frame.hpp` | LOD tier policy + scheduler in engine |
| 3 | Job/threading system | PARTIAL — `JobSystem` FIFO+futures; no priorities/deps/cancel/naming/profiling; used only by save writer | `engine/…/foundation.hpp` | priorities, named workers, stats, adoption |
| 4 | Render graph | MISSING — one immediate-mode `scene()` + `DrawList` buckets | `app/native_client/main.cpp`, `native_map_platform.hpp` | pass scheduler over DrawList layers |
| 5 | GPU-driven rendering | MISSING — SDL_Renderer 2D batches only; CPU culling | `native_map_platform.cpp` | instance-batch DrawList primitive; raw SDL_GPU pipeline is follow-on |
| 6 | Texture streaming | PARTIAL — bounded LRU caches (128/192MiB GPU, per-producer CPU caps); synchronous decode | `native_map_platform.hpp`, `native_*_assets.*` | streaming requests, priorities, async decode, promotion |
| 7 | Shader library + cache | MISSING — SDL built-in shaders only | — | registry/variants/cache manifest; consumption pending SDL_GPU pipeline |
| 8 | Particle/VFX framework | MISSING — procedural flare polylines + battle VisualEvents | `native_celestial_appearance.cpp` | engine VFX definitions (emitters, curves, attachment, LOD); migrate flares |
| 9 | Physics layer | MISSING — combat-only grid; abstract damage | `core/massive_combat_engine.cpp` | engine collision primitives, broadphase, raycast, overlap |
| 10 | Spatial query framework | PARTIAL — private combat `SpatialIndex`, lane graph, linear scans elsewhere | `core/massive_combat_engine.cpp`, `core/lane_network.*` | shared engine spatial index + queries |
| 11 | Route engine | PRESENT-PARTIAL — lane Dijkstra, leg-range, whitelist, fuel post-walk, caches; no hostile penalty, no fuel-optimized search | `core/lane_network.*`, `fleet_reach.*` | cost hooks, blocked sets, refuel-aware search |
| 12 | Knowledge/FoW | PRESENT-PARTIAL — `CivilizationKnowledgeState` (unknown/detected/partial/full), observer filtering per-callsite | `core/knowledge.*`, `settlement_knowledge.*` | unified filtered-view helper; per-body granularity documented |
| 13 | Generic economy/resources | MISSING — per-resource scalar fields/ledgers | `core/*economy*`, `freight.*`, `logistics.*` | `ResourceDefinition`/`Inventory`/`Recipe`/`Reservation`/`TransportOrder` framework |
| 14 | Event bus | PARTIAL — `EventQueue<T>` single-type owner-affine; domain events flow via step-result vectors | `engine/…/foundation.hpp`, `campaign_coordinator.hpp` | typed subscription bus, lifecycle-safe |
| 15 | Mission/event framework | MISSING — implicit fleet-role missions; UI board only | `core/exploration_*`, `native_missions.*` | data-driven definitions: triggers/stages/choices/timers |
| 16 | Advanced saves | MOSTLY PRESENT — v17 schema, legacy migration, autosave scheduler, atomic+`.bak`, async writer; no rolling slots/checksum/metadata preview | `core/player_campaign_*`, `engine/atomic_file_write.*` | rolling slots, checksum sidecar, preview reader |
| 17 | Deterministic replay | MISSING — repeat-run hash comparison only | `app/galaxy_main.cpp` | session journal + replay harness |
| 18 | Crash reporter | PARTIAL — user-triggered support bundle + top-level exception boundary; no crash handler | `native_support.*`, `headless_main.cpp` | unhandled-exception/minidump capture → bundle |
| 19 | Profiler | MISSING — ad-hoc chrono + smoke metrics | `main.cpp` smoke block | scoped spans, counters, JSON export, overlay |
| 20 | Memory tracking | MISSING — cache byte counters only | `native_map_platform.hpp` | subsystem registry, high-water marks |
| 21 | Input actions | PARTIAL — normalized `InputEvent`; literal key switch; no rebind/gamepad/contexts | `native_map_platform.hpp`, `main.cpp` key switch | action map, contexts, bindings file, gamepad |
| 22 | Audio engine | PARTIAL — 48kHz CPU mixer, 1 music bed (fully decoded), 8 sfx voices, ducking, SAPI voice | `native_audio*`, `native_voice*` | buses, per-category gains, positional pan/attenuation, streaming music |
| 23 | Animation | MISSING — stateless procedural time functions | `native_celestial_appearance.cpp` | clip/track/easing/state-machine module |
| 24 | Advanced UI | PARTIAL — theme helpers + per-view widgets; responsive scale; no shared list/table/tree/drag-drop | `native_ui_theme.hpp`, `native_ui_layout.hpp` | shared widget module (scroll list, table, tabs, tree) |
| 25 | Localization | MISSING — hardcoded English literals | — | `TextCatalog` + locale JSON + formatting/plurals; migrate a slice |
| 26 | Accessibility | PARTIAL — subtitle size, auto layout scale | `native_voice_settings.*`, `native_ui_layout.hpp` | ui/text scale, high-contrast, reduced motion, colorblind accents |
| 27 | Platform layer | PARTIAL — window/paths/atomic-write/image; Win32+SDL+GDI+WIC | `engine/*` | `IPlatformServices` seam, documented non-Windows path |
| 28 | Steam layer | MISSING — `windows-steam` preset blocked | `export/stellar-presets.json` | service interface + null backend + hooks |
| 29 | Mod architecture | MISSING — data catalogs with manifests; no packages/overrides | `data/research/v1`, `adaptive_research_catalog.cpp` | package manifest, load order, dependency/version checks, data overrides |
| 30 | Stellar Tools | PARTIAL — DEVELOPMENT submenu + 6-command tools panel | `native_development_menu.*`, `native_developer_tools.*` | tabbed tools host: entity/asset/profiler/event/job/save views |

## Notes

- `engine/foundation.hpp` primitives are scaffolding: `EntityRegistry`,
  `FixedClock`, `DeterministicRandom`, `EventQueue` are used only by
  `headless_main` + tests; `JobSystem` is used once (save writer). A major
  theme of this expansion is adopting/extending rather than duplicating them.
- Renderer constraint: the GPU path is SDL3's 2D `SDL_GPURenderer` over a
  Vulkan device — no custom pipelines/shaders. Requirements 4–7 are therefore
  implemented at the DrawList/pass layer with real batching, streaming and
  shader-asset management; a raw `SDL_GPU` pipeline migration is the
  documented follow-on for true indirect draw / custom shader execution.
- Save compatibility is frozen by the Player17 contract; save upgrades are
  additive (sidecars/envelopes), never reinterpretation.
