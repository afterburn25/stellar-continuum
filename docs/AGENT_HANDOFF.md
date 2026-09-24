# Agent handoff — start here

> **STELLAR CONTINUUM IS NO LONGER A GODOT/C# PROJECT.**
>
> **CURRENT ARCHITECTURE:**
>
> **CUSTOM STELLAR ENGINE**
>
> **C++**
>
> **C++**
>
> **DO NOT MIGRATE BACK TO GODOT OR INTRODUCE UNITY/UNREAL.**

Snapshot: 2026-09-20. Repository: `afterburn25/stellar-continuum`.
Continue from **`cpp/codex-native-architecture-integration`**, not `main` or an
old research/editor branch. The default branch does not represent this native
working build. Engine `0.1.64`; game `0.1.14.2-dev`; source of truth:
[`export/runtime-config.json`](../export/runtime-config.json).

> **`work/foundation-1-30-codex-integration`** merges the
> `engine/foundation-expansion-1-30` engine work (save history, replay,
> input mapping, localization, developer tools, engine libraries) onto this
> line without replacing its renderer/artwork/installer systems. Evidence:
> [foundation-merge receipt](validation/2026-09-20-foundation-merge.md).
> It awaits PR integration into `cpp/codex-native-architecture-integration`.
> Consumer-audit conclusion and final validation:
> [consumer-audit receipt](validation/2026-09-22-consumer-audit.md).

## Read in this order

1. [Project state](PROJECT_STATE.md) and [verification receipt](validation/2026-09-20-development-sync.md).
2. [Architecture](ENGINE_ARCHITECTURE.md) and [capability matrix](ENGINE_CAPABILITIES.md).
3. [Known issues](KNOWN_ISSUES.md), [technical debt](TECHNICAL_DEBT.md), [roadmap](ROADMAP.md).
4. [Celestial/Sol status](CELESTIAL_CONTENT_STATUS.md), [asset pipeline](ASSET_PIPELINE_STATUS.md), [installer](WINDOWS_INSTALLER.md).
5. [Developer QA](DEVELOPER_QA.md), [workstream policy](DEVELOPMENT_WORKFLOW.md), and root [AGENTS.md](../AGENTS.md).

## Reproduce the native build

Install Git with Git LFS, Visual Studio 2022 Build Tools with Desktop C++ and a
Windows SDK, CMake 3.28+, Ninja and Python 3.12+ for tooling. Actual audit compiler
and Python version are recorded in the receipt. A working Vulkan device is
required for real graphics tests. Ordinary builds use embedded shaders and do
not require a shader SDK; regeneration requires the pinned shader toolchain.

In an **x64 Native Tools Command Prompt for VS 2022**:

```bat
git clone --branch cpp/codex-native-architecture-integration https://github.com/afterburn25/stellar-continuum.git
cd stellar-continuum
git lfs pull
cmake --preset windows-native-preview
cmake --build --preset windows-native-preview --parallel 4
ctest --preset windows-native-preview --parallel 2 --output-on-failure
set STELLAR_NATIVE_EXE=%CD%\build-native\preview\stellar-continuum.exe
python -m unittest discover -s tools/stellar-export -p "test_*.py"
```

The normal graphical development preset is `windows-native-preview`
(RelWithDebInfo, tests enabled). `windows-development` is Debug **without** the
graphical client by default. `windows-testing` and `windows-headless` provide
non-graphical configurations, but Windows-only image/tool tests can still exist.
Do not run configure and build simultaneously against one build directory.
The CMake file grants `/bigobj` specifically to the large persistence test.

```bat
build-native\preview\stellar-continuum-native.exe --dev-game
build-native\preview\stellar-continuum.exe --help
```

Development launch reads copied loose assets. `StellarCooker` and
`tools/build-cooked-game.ps1` construct isolated cooked content. After a full
target release is validated, `tools/build-update-installer.ps1` builds an
exact-base changed-files update. **The user wants update installers for future
downloads**, not repeated full packages. Follow [Windows installer](WINDOWS_INSTALLER.md)
for complete release commands and rollback/repair limitations.

## Current state and next workstream

The native game has real research, colonies/economy, fleets/logistics,
diplomacy, combat, multiple map views and Developer tools. Celestial rendering,
supplied planet/giant/ring/moon art, a faint shared sky, cooked packages and
per-user installation/update/repair are integrated. Current limitations and
failed checks are explicit in [PROJECT_STATE.md](PROJECT_STATE.md); this is an
unfinished development build, not a production-ready certification.

Recent work includes corrected cooked flare memory reservations, persistent
local crash reports, small updates, major Sol moon orbits, canonical appearance,
ice optics, sharper rings, independent flare timing and scoped simulation
lookup/image-memory optimizations. [Progress](DEVELOPMENT_PROGRESS.md) links the
owners and evidence. Do not infer that a historical report describes the latest
behavior when a newer correction supersedes it.

**Space-strategy specialization (branch
`engine/space-strategy-simulation-specialization`):** Stellar Engine is being
specialized into a space strategy/simulation engine — see
[SPACE_STRATEGY_ENGINE.md](SPACE_STRATEGY_ENGINE.md) for the charter and
milestone map. Milestone 1 landed at engine level: `SimulationExecutor`
(simulation_executor.hpp) drives `SimulationTask`s through the existing
tier-cadence `SimulationScheduler` — dependency ordering, dirty/event wakeups
(Dormant is the event-driven tier), aging under budgets so deferral cannot
starve keys, `elapsed_ticks` catch-up, JobSystem dependency-wave execution,
per-domain timing, pause, LOD promotion/demotion. Design/contracts in
[SIMULATION_LOD.md](SIMULATION_LOD.md); benchmarks are the
`simulation_scale_250…5000` ctest entries. Milestone 2 landed at engine
level: `EconomyCatalog`/`EconomyGraph`/`analyze_economy`
(economy_catalog.hpp) add validated `ResourceSpec`/`RecipeSpec`
definitions, dependency-graph queries and bottleneck/reserve/import
diagnostics, bridged to runtime via `to_runtime_recipe` — see
[ECONOMY_FRAMEWORK.md](ECONOMY_FRAMEWORK.md). Milestone 1's persistence
completion added `capture_state`/`restore_state` to both scheduler and
executor (exact tick/timer round-trip, pending dirty/wake sets, pause;
callbacks re-register after load — `simulation_persistence` tests).
Milestone 3 landed: `Population` cohort demographics (population.hpp) —
attributed births/deaths, aging, employment, education, explicit
migration slices; ~38M-headcount scale test — see
[POPULATION_FRAMEWORK.md](POPULATION_FRAMEWORK.md). Milestone 4 landed:
`Colony` settlement substrate (colony.hpp) — DistrictSpec/StructureSpec
templates, slot-bounded construction, shared utility pools, workforce
scaling, upkeep/input draws, condition repair — see
[COLONY_FRAMEWORK.md](COLONY_FRAMEWORK.md). Milestone 5 landed:
`FlowNetwork` (flow_network.hpp) — per-resource directed distribution
graphs with node supply/demand/storage, capacity edges, lazily rebuilt
union-find component cache on topology dirtying, deterministic
ascending-id greedy transport and unmet/saturation diagnostics — see
[INFRASTRUCTURE_FRAMEWORK.md](INFRASTRUCTURE_FRAMEWORK.md). Milestone 6
landed: `LogisticsNetwork` (logistics.hpp) — waypoint nodes, explicit
multi-leg `FreightRoute`s with transit days and in-flight capacity,
deterministic dispatch queue and (eta,id) deliveries — see
[LOGISTICS_FRAMEWORK.md](LOGISTICS_FRAMEWORK.md). Milestones 7–8
landed: `PlanetEnvironment` + `evaluate_habitability` (planetary.hpp)
adapter surface for Core planet state, and `Terraforming`
(terraforming.hpp) staged environment mutation — see
[TERRAFORMING_FRAMEWORK.md](TERRAFORMING_FRAMEWORK.md). Milestone 9
landed: `StrategicMind` (strategic_ai.hpp) deterministic utility
decision machinery — domain-partitioned actions, hysteresis, cooldowns,
bounded decision journal; cadence is caller-owned via executor tiers —
see [STRATEGIC_AI.md](STRATEGIC_AI.md). Milestone 10 landed:
`WarfareModel` (warfare.hpp) — ShipCohort aggregates, Interdict zones
gating hostile movement, deterministic Lanchester engagement
resolution, 2000-fleet scale — see [WARFARE_FRAMEWORK.md](WARFARE_FRAMEWORK.md).
Milestone 13 partially landed early: `combined_scale` test drives 400
settlements (population + colony + power grids + freight + fleets +
faction AI) through one `SimulationExecutor` at mixed tiers — 240
ticks, bit-identical checksums across runs. Milestone 14 landed:
`EventHistory` (history.hpp) — recorded strategic events with
observer-privacy query projection, significance thresholds, bounded
store + pruning, and `feed()` as the public-information/news substrate
— see [HISTORY_FRAMEWORK.md](HISTORY_FRAMEWORK.md). Cross-cutting
persistence landed: versioned `capture_state`/`restore_state` on every
specialization framework (Population cohorts, Colony instances,
FlowNetwork topology, LogisticsNetwork routes/shipments, WarfareModel
fleets/cohorts, StrategicMind incumbents/cooldowns/journal,
ResourceNetwork inventories/producers/transfers) —
`framework_persistence` tests prove identical continued evolution and
negative cases throw on missing definitions. `framework_state_json.hpp`
adds the byte-level layer: templated `to_json`/`from_json` codecs
(nlohmann-compatible, `galaxy_phenomena_json.hpp` convention) for all
State structs (including `EventHistory`), covered by
`framework_state_codec` tests — campaign save codecs can now embed
framework state without hand-written field lists. First Core adoption
landed: `core/campaign_event_history` maps authoritative
`IntegratedAdaptiveCampaignStepResult` events onto `HistoryEvent`
records; `IntegratedAdaptiveCampaignRuntime` owns an `EventHistory`
recording every completed advance (`runtime().history()` /
`frame().history()`), serialized into the v17 save payload as
`"EventHistory"` for player and developer saves (absent = empty in
older saves; strict ordered decode).
`GalaxySimulationStepCoordinator` now runs
its 12 strategic phases (economy → economy_storage) as Active-tier
`SimulationExecutor` tasks dependency-chained to the historical order —
same behavior (`campaign_coordinator_parity` green), but per-phase
cadence demotion, budgets and wakeups are now configuration instead of
restructuring. Next: further Core/game
adoption — per-entity executor cadence inside heavy phases and
framework consumers (population/colony/flow/logistics/AI/warfare)
against real campaign state. Second Core adoption landed:
`core/planetary_adapter` — `to_engine_environment(const
PlanetaryBody&)` projects authoritative planet environment into engine
`PlanetEnvironment` (direct temperature/gravity, kPa→atm pressure,
binary water-solvent presence, deterministic sorted
`atmosphere.*`/`solvent.*`/`high_radiation`/`immersed`/`gas_giant`/
body-flag tags) so engine frameworks evaluate real Core worlds;
`planetary_adapter` tests cover field conversion, the tag vocabulary
and `evaluate_habitability` over projected bodies. Core
`assess_species_planet`/`species_environment` remains authoritative for
campaign suitability — the adapter is a projection, not a replacement.
Milestone 15 partially landed: `seed_chronicle_notifications`
(native_notification_events) seeds the bounded player notification feed
from `runtime().history().feed(player)` at campaign admission — the
persisted chronicle now surfaces in-game after load with recorded dates
and summaries; `native_notification_events` tests cover ordering,
observer privacy, bounds and category labels. The scrollable chronicle
browser landed with it: `native_chronicle` (`snapshot()` newest-first
projection, 4000-entry cap with true total, `NativeChronicleView`
overlay with refresh) opened via a CHRONICLE button in the notification
panel header; `native_chronicle` tests cover projection, observer
privacy, lifecycle and render smoke. Category-domain filtering landed
(`snapshot()` `category_prefix`, cap applied after filtering, FILTER
button cycling All → six domains). Voice presentation was already
implemented — `NativeGameplayVoiceBridge::route_events` announces
every significant category per advance with authoritative names and
first-occurrence tracking; an earlier note listing it as pending was
wrong and has been corrected. Significance filtering landed too: the
browser cycles floors 0.0 → 0.3 → 0.5 → 0.7 (applied before the cap
alongside the domain filter) and admission seeding uses a fixed 0.35
report floor so high-volume trivia (damage ticks, detections) stays
out of the transient feed. The actor filter cycles all intel → MINE →
each civilization appearing in the visible feed (`actors` exact-match
with name resolution) — "what is civ 7 up to" is one button away.
Located entries navigate — clicking one returns its system
through `navigation()` and the client enters that system (the
workspace's observation check still gates visibility); entries with
exactly one foreign actor get a DIP action opening the diplomacy
workspace on that contact, and recorded reference tags render as
clickable chips applying `HistoryQuery::tag`'s exact-match focus, and
a TIME button cycles the feed's `since_day` bound (all → 30d → 1y →
10y) driven by a live campaign-day source — and while a window is
bounded, ◀ ▶ page buttons shift it by its own width (`since_day` +
`before_day`; both query bounds are inclusive so older pages pass a
`nextafter`-lowered upper edge — boundary-day entries tile onto
exactly one page, while the newest page keeps `now` inclusive) —
and a header search field
(case-insensitive substring over summary/category) joins the client's
`wants_text_input()` gate. The
transient
feed mirrors it: chronicle-seeded reports carry `system_id` and render
a VIEW SYSTEM action; the coalesced feedback path stays id-free.
Chronicle
retention landed too: `maintain_chronicle` (in
`campaign_event_history`) runs after each advance — once the history
reaches 90% of capacity it prunes routine records (<0.35 significance)
older than 365 days so the bounded oldest-first eviction cannot
discard majors; `campaign_event_history` tests cover trigger, content
and determinism. The chronicle now records diplomatic happenings too:
after each step's diplomacy phase the runtime pulls the new journal
tail via `DiplomacyState::history_events_since` (a monotonic event-id
watermark baselined at construction — restored journals never
re-record) and maps each entry to `diplomacy.<kind>` with its own
journal timestamp, per-kind significance, `civ:`/`system:` tags and
the journal's authoritative `known_to_civilization_ids` audience —
exempt from knowledge widening, with generic kind summaries (the raw
journal phrasing is internal). Every HistoryQuery axis (category,
significance, actor, tag, after_day, before_day) plus free-text
search now has a browser surface — `snapshot()` moved from `feed()`
to `query()` (the same observer projection plus the upper bound);
the remaining
presentation gap is multi-select within an axis (e.g. Combat +
Diplomacy domains together) — evaluated and deliberately deferred:
cross-axis composition already covers the real cases, and a domain
chip row cannot fit the panel at minimum scale.

Replay save-checkpoints now localize divergence: the client's
save-capture observer emits `engine::document_section_checkpoints`
(one labeled hash per top-level JSON section, object members one
level deep — "save:World.Fleets") instead of a single whole-document
hash, and `verify_checkpoint_sequence` names the diverging section
in the replay divergence message. On divergence the client also
dumps the actual canonical document to
`replay-divergence-<tick>.json` next to the save and appends the
path to the message — diffing it against the original capture names
the changed leaf. A completeness check closes the other direction:
once every recorded command is consumed, a recorded checkpoint whose
tick passes without the capture firing is reported as a skipped save
(the smoke line's `verified_checkpoints`/`of` pair already surfaced
the count; now it also fails). Older recordings diverge at the first
checkpoint with a label mismatch — recordings are session artifacts,
not save files. On `--replay` the parsed header's seed/game_version is
now compared against the session and a mismatch prints a provenance
warning to stderr (advisory — cross-build replay is a legitimate
compatibility probe). `build_id` now carries `STELLAR_SOURCE_COMMIT`
(the generated git hash) instead of duplicating the version, so the
check also catches same-version/different-commit replays; headers
whose build_id equals their game_version are treated as legacy and
skip the commit check. Coverage: `replay` unit tests.

**Standalone engine platform:** `stellar-engine.exe` is the engine-only tools
host (no game module). Its Projects tool drives the full game-project loop:
`engine::EngineProject` manifests (`project.stellar.json`, `EngineProject::save`
atomic rewrite), `create_project` scaffolding (base package, content dirs,
`mods/`, generated consumer `CMakeLists.txt`, windowed ECS starter
`src/main.cpp` running a `World` Transform/Velocity loop), asset import,
generic `scan_content` cooking into project-namespaced packages with
streamed `done/total` progress, BUILD with a live `build.log` tail, RUN,
EDITOR (launches `stellar-editor.exe --project <root>`; its documents live
in `<project>/editor/`), PACKAGE (distributable `dist/<name>/` = host +
runtime + `Content/` + `packages/`), and RENAME via the name field.
BUILD/RUN consume the exported `engine-sdk/` beside the shell (headers,
prebuilt libs, SDL3 runtime + default font, `stellar::engine`/
`stellar::cooker`/`stellar::platform`/`stellar::audio` consumer targets);
the shell itself accepts `--project <root>` and `--tool <name>`, and the
whole loop is scriptable headlessly via `--create/--cook/--build/
--package/--run/--test` (verbs dispatch before the asset-browser scan
and may follow `--project <root>`). The Assets tool re-roots to project content and can toggle
between SOURCE files and COOKED `runtime.stmanifest` records. The Scene
tool authors `editor/scene.json` (`engine::SceneDocument` — named
entities with position/extent/velocity/tint/optional sprite) with
bounded undo/redo (`engine::UndoHistory`, Ctrl+Z/Y + buttons),
DUPLICATE, and preview click-select/drag; the
windowed starter is now a ~20-line `RuntimeHost` client: the engine's
`stellar_engine_runtime` lib (`engine::RuntimeHost`, exported as
`stellar::runtime`) owns the SDL loop, package scan, `ContentResolver`
(cooked `Content/` or `build/cooked/` first, loose
`packages/<id>/content/` fallback, `pkg:path` qualified lookups
for mod packages), the `scene_components` ECS set (registered codecs
for every field), `spawn_scene`/`scene_from_world`, scene-file hot
reload, action-mapped player input, music/effects audio, F5/F9 quicksave
through `saves/quicksave.stw` (atomic write plus a rotated `.bak`
history chain — load recovers through the slots), and
`RuntimeDiagnostics` crash dumps in `<root>/logs/`. Input runs through
`InputMapper`: a built-in "game" context binds WASD/arrows/D-pad,
left-stick `move_x`/`move_y` axes, Space/LMB/pad-RB fire and C/pad-West
mine; `host.input()` exposes the mapper and `--input-map` stacks project
contexts (gamepads hot-plug via `SDL_INIT_GAMEPAD`, first pad wins).
Game code hooks
in via `on_update`/`on_event`/`on_status`/`on_collision`/`on_draw`
callbacks and drives the loop with `request_quit()`/`set_paused()`/
`set_time_scale()`/`set_scene()` (level switching)/`spawn_entity()`/
`destroy_entity()`/`set_camera()`/`tile_at()`/`set_tile_at()`.
Runtime options/args: `--scene`,
`--fixed-hz` (deterministic N-step-per-frame under `--frames`),
`--frames` (bounded CI runs), `--snapshot-out` (byte-comparable world
dumps), `--world-w/--world-h`, `--speed`, `--width/--height`,
`--fullscreen`, `--input-map`, `--move-speed`, `--jump`, `--save`;
P pauses, F12 screenshots to `<root>/screenshots/`.
`SceneEntity` authoring surface: name, position/extent/velocity,
tint, sprite path, layer (stable-sorted draw order), parallax
(0 = screen-pinned), text label, gravityScale + solid (platformer
physics: doc-level gravity, landing on solid tops, side blocking,
ceiling bumps, grounded W/Up jump — solid/tilemap blocking applies
to all movers, so gravity-free scenes get top-down walls), sprite-sheet `frames`/`fps`/`fcols` (sim-time
indexed; `fcols` slices grid sheets, 0 = strip; `animLoop`
false holds the last frame),
`rotation`, `ttl` (sim-time self-destruct),
`flipX`/`flipY`, `visible`, `bounce`, `spin`, `data`, `opacity`,
`oneway`, `vfx` (named emitter auto-attached on spawn and re-attached after save restore — definitions can be declared in the document's `emitters` array: rate/lifetime/velocity/spread/gravity/over-life curves/max/LOD, so particles need no game code),
and `parent` — name-keyed attachment resolved by
`resolve_hierarchy` each sim step: children keep their authored
offset and follow the resolved parent (chains root-first; cycles
and missing parents keep the last position), while a child's own
world-space motion re-bakes into its stored offset. Attachments
survive save/load since the component stores the parent's name, not
its entity id. Scenes also carry a `tilemaps`
array (`SceneTilemap`: tileset image path, `x`/`y` grid origin in
world px, `tileW`/`tileH`, `columns`, `layer`, `parallax`, `collide`,
row-major `cells` with `-1` empty; legacy single-`"tilemap"` documents
still parse) —
each tilemap lives in the world as a `Tilemap` component on its own
dedicated entity in document order (`host.tilemap_entities()`,
`tilemap_entity()` returns the first), so cell state is authoritative:
runtime edits (destructible terrain) snapshot with F5/F9 saves and
`scene_from_world` re-exports every map. RuntimeHost renders each map
through the sprite path at its own layer/parallax (stable layer sort
interleaved with entities) and runs cell collision against every
`collide` map using that map's geometry (side-blocking, top landing,
grounded) in the same authoritative pass. `tile_at`/`set_tile_at`
address the first map; layered games use `tilemap_entities()`. The
Scene tool exposes every field in an adaptive multi-column property
list with an animated/flipped/rotated preview that paints all maps,
plus TILES + / MAP k/n / TILES - layer-stack controls, tilemap fields
(tileset, tile size, columns, collide, layer, parallax, cells CSV,
brush id) that edit the selected map, and a PAINT mode that
click/drag-writes cells in the preview with a grid overlay, a
tileset picker strip along the preview's top edge (click a tile to
make it the brush), and one undo step per stroke.
`stellar-editor.exe` is the separate authoritative-world editor
(galaxy/system/body workspaces, annotations, undo, atomic project
documents, `--project` interop). Both are registry rows in
[ENGINE_CAPABILITIES.md](ENGINE_CAPABILITIES.md).

**3D scene mode:** generated projects can also run 3D worlds —
`RuntimeHostOptions::scene3d` / `--scene3d` loads
`editor/scene3d.json` (`engine::Scene3dDocument`: camera pos/yaw/pitch/
fov/near/far, world-space key light + intensity, background, `gravity`,
`groundY` rest plane, `bounds`, `music`, shared `emitters`) into the
same World as a separate `Transform3D`/`Velocity3D`/`MeshRef`/
`TextureRef`/`DoubleSided`/`Parent3D` entity set (registered codecs —
F5/F9 snapshots cover it, `load_world` partitions it back out). Mesh
specs are `box[:sx,sy,sz]`/`annulus:i,o[,seg]`/`sphere[:cols,rows]` or
content-relative `.obj` paths (`load_obj_mesh`); `Mesh3D` carries local
AABB bounds that become each entity's `ObBox3D` under rotation+scale —
`obb_separation` (SAT over the 15 candidate axes, physics3d.hpp) gives
both the overlap test and the minimum translation vector for solid
push-out/landing, while the rotated world AABB still drives ground
resting, `bounds`, and broad-phase pair rejection. The host flies the
camera via the rebindable "game" context
(WASD + Space/C + right-drag look + wheel fov, `--fly-speed`),
integrates gravity/velocity at the fixed timestep, fires
`on_collision`/`on_land`/`on_spawn3d`, and renders through
`Scene3DView` under the 2D pass (2D entities remain HUD). Helpers:
`entities3d()`, `entities3d_in_radius`, `spawn_entity3d`,
`set_camera3d` + getters. Narrow-phase collision is SAT OBB over each
entity's rotated+scaled mesh bounds; the camera snapshots via a
`Camera3DState` carrier;
`lights` adds up to two directional fills; windowed projects ship a
starter `editor/scene3d.json`. `raycast3d(origin,dir,max)` casts
against actual mesh triangles in each mesh's local frame
(`physics3d::segment_triangle`, rotation+scale aware) and returns the
nearest `{entity,distance,point}`; `entity3d_at(sx,sy)` is the
screen-space pick counterpart of `entity_at`. The engine shell's
**Scene3D tab** authors `editor/scene3d.json` end-to-end: entity list +
all entity/document fields, undo history, a live `Scene3D` preview
(shares `resolve_mesh_spec`/texture decode with the runtime), right-drag
camera orbit, wheel fov, and click-select via `raycast_world3d` over a
scratch `spawn_scene3d` world. Limitations: OBB-over-mesh-bounds (not
per-triangle) collision, no rigid-body solver, ground/`bounds` still use
the world AABB, raycast is O(tris) per entity with no spatial partition,
editor has no transform gizmos — see the registry record.

**Recommended next workstream: the Core-adoption architecture decision.**
The 2026-09-24 validation receipt records 294/294 native tests green on this
branch — the failures in the 2026-09-20 receipt were resolved through the
integration merge. A 2026-09-23 supplement
(`validation/2026-09-23-chronicle-suite.md`) records 295/295 runnable
tests green at `d68c98af` after the chronicle/history increments; the
15 `engine_shell_tool_*` smoke tests registered there need a desktop
run (no display in that environment). A desktop run at `23ba3577` records
310/310 tests green (suite now includes the 15 tool smoke tests and the
population-habitability bridge tests). Three save/diagnostics failures seen
in an interim run were stale test expectations, repaired at `23ba3577`: the
legacy composed-encoding oracle and frozen fixtures predated the v17
"EventHistory" tail (`afee13f2`), and the diagnostic monitor test assumed a
finding-free seeded world. A later desktop run at `3ea2710a` records
314/314 tests green after the GalaxyMap framework (engine star-chart model,
Core projection, GALAXY shell debugger, deterministic lane-graph routing,
framework codec) and the replay-provenance chain merged — including the
`stellar_engine PUBLIC stellar_json` fix for the public `replay.hpp`
nlohmann include. The M11 render frontier is now landed: HDR/tonemap
(RGBA16F scene targets + fullscreen resolve with a knee+headroom curve,
capability-checked with UNORM fallback; embedded shaders regenerated with a
verified glslang 16.6.0 toolchain — byte-identical scene3d output;
`tools/compile_scene3d_shaders.py` covers all shader pairs), SSBO instanced
rendering (per-instance transform/material records in storage buffers,
`gl_InstanceIndex` + flat varying; compatible draws merge — GPU suite
asserts one instanced call renders two objects correctly), `DrawBatcher`
owns submission ordering/batching (full GPU binding key interned into
material_id), `RenderGraph` schedules the scene→tonemap pass chain per
frame, and `TextureStreamer` owns texture residency under a runtime-tunable
byte budget (`Window::set_scene3d_texture_budget`) with pinned-fallback
pop-in on denied binds (`Scene3DStatistics::streamed_fallbacks`,
`streamed_evicted_bytes`, `Window::set_scene3d_texture_budget`), per-mip
partial residency (denied requests degrade to the coarsest fitting mip
tail; RGBA tails CPU-box-downsample the base, cooked/BC1 upload level
ranges), and screen-footprint LOD demand (desired mip from projected
bounding-sphere footprint; `anisotropic_texture` materials keep full
chains). Remaining render limitations: no indirect draw, bounded CPU
submission. A 315-test ctest run records 315/315
green after the M11 chain, the editor trait-override annotation layer
(AUTO/YES/NO overrides for anomaly/rare-resource/pre-warp flags on systems
and bodies, round-tripping through the project codec), and the
`campaign_colony_projection` adapter landed. A parallel-lane fix landed in `GalaxyMap::route_length_light_years`:
Dijkstra relaxes each lane independently, so the edge a route uses is the
cheapest connecting lane — the length helper now sums the minimum rather
than the first lane id (test: parallel lanes of 9/2 ly report 2). The
read-only projection family gained a third adapter:
`campaign_colony_projection.*` reshapes a Core colony (surface buildings +
catalog definitions + the authoritative powered allocation) into an
`engine::Colony`; `inspect_campaign_operations` now emits
`degraded_structures` findings for complete+enabled buildings at or below
the operational condition floor — a finding class no previous check
covered — plus authoritative logistics findings
(`logistics_strained`/`logistics_critical` per under-covered colony,
`freight_corridor_gap` when external imports have no represented corridor,
and `treasury_arrears`/`treasury_depleted` from `assess_treasury`;
severity stays Warning — Critical is invariant-reserved). The second frontier is now a recorded
decision (DECISION_LOG 2026-09-24): engine-framework adoption runs through
read-only projections first; per-framework graduation to authority requires
semantic superset/equivalence, a save migration path, a seeded-campaign
parity oracle, and preserved determinism/privacy. A 315-test ctest run at
`550a50d3` records 315/315 green after the app/editor lanes: editor
multi-file projects (directory form + asset scan/preview), `TableModel`
sortable colony-roster columns, `TreeModel` system→body hierarchy in the
editor list, and the `interfaceScale`/`reduceFlashing` accessibility
preferences. A later 315-test run after the accessibility completion
(`highContrast`, `colorBlind`), editor overrides (`radiusEarth`,
`orbitAu`), and TextureStreamer partial-mip-tail residency records one
failure in `native_scene3d_gpu` caused by the in-flight
screen-footprint LOD edit; the fixed binary passes standalone, so the
branch is effectively 315/315 at `7fdc8357`. A 317-test run at
`bd0fdca7` records 317/317 green after the logistics and population
projection adapters landed: `campaign_logistics_projection` (engine
freight network snapshot → `logistics_link_saturated` findings) and
`campaign_population_projection` (cohort query model →
`population_unrest` findings), each with dedicated ctest coverage.
A 317-test run at `f676a47b` records 317/317 green after the second
shipped locale: `data/locale/de.json` (full 1418-key German table with
English fallback), `Data/locale/<id>.json` enumeration, persisted
`GeneralPreferences::locale` + LANGUAGE cycler in General Settings,
in-place `LocalizationTable` rebuild on save, and shipped-catalog
key/placeholder parity asserted in `localization`. Earlier commits in
the lane added the `advisor_spotlight` diagnostic (StrategicMind picks
the civ's top operational finding) and the editor `massEarth` override
with derived effective gravity.
A 319-test run at `5035cfa3` records 319/319 green: the suite gained the
other agent's `campaign_world_projection` (Core container →
`engine::World` bridge with `sync_campaign_world` reconciliation) plus
this lane's profiler captures (`ProfileCapture` save/load + A/B
`compare_captures` in the shell Profiler tool), generic animation
(`AnimationPlayer` lifecycle + scene-authored entity `anim` tracks +
`AnimTimeline` component + `on_anim_event` + snapshot restore), package
save attestation (`<save>.packages.json` sidecar verified on load),
input rebinding persistence (`save_contexts`/`bindings()`), MemoryTracker
VRAM + 2D-cache attribution (`Window::draw` reports scene3d
textures/meshes/targets and ui-image/ui-text caches per frame), mip-tail
promotion verification, and `NativeAudioDirector` device-fault recovery
(bounded 5 s retry rebuilds the output and resumes music; decode
failures stay permanent).
A 319-test run at `9cc4256e` records 319/319 green after the
broadphase/budget/LOD lane: `UniformBroadphase<2|3>` uniform-grid
candidate pairs now drive both RuntimeHost contact scans (O(n+cells)
replacing O(n²), narrow-phase semantics unchanged), `VfxSystem` gained a
global particle budget (demand-proportional rate taper + hard headroom,
RuntimeHost defaults to 64k), the shell's scene3d preview decodes
textures on the JobSystem, and dielectric environment maps keep
full-chain residency (view-independent texel demand). The other agent's
`campaign_world_projection` test built and passes in this tree.
A 319-test run at `c633aec7` records 319/319 green after the keyboard-focus
accessibility lane: `native_menu_hover.hpp` gained a dedicated focus-cue path
(pointer-hover memory stays pointer-only) and a span `hit` overload for
ordered focusable arrays. Tab/Shift+Tab traversal, Home/End, Enter/Space
activation, rendered focus rings, and keep-focus semantics for
non-navigating actions now cover the settings hub, General/Audio/Video/Voice
Settings (sliders adjust via arrows, Home/End clamp to min/max), and the
startup workspace across Entry/ModeSelection/LoadSlots/Busy/Failure/
Development screens with per-screen focusable collection and transition
resets routed through `reset_pointer()`. Screen-reader/AT contracts and the
new-game Setup sub-surface remain open.
A diagnostics-hardening lane through `34243eae` mirrors every authoritative
validator inside `inspect_campaign_invariants`: `validate_galaxy_references`
(settlement kind/hub/capacity/site, freight/route/order/site consistency,
design and tactical validation, endurance bounds, work-progress ceilings,
combat-intel pairs/evidence/4096 bound), `validate_surface_construction`
(slots, placement, progress/upgrade consistency, catalog bounds),
`validate_construction` (project catalog, overlap, duplicates, cost bound),
shipyard `validate_for_capture`/`validate_identities`/population safety,
stellar physics/orbits/activity/small-body/core validators, leadership
character bounds, economy DTO bounds, and `out_of_range` semantic bounds
the loaders clamp. Deep mutable validators run on clones so diagnostics
stay read-only; the extended `campaign_colony_projection` corrupt fixture
classifies ~150 findings while seeded worlds stay finding-free (both
parity suites green). The 15 `engine_shell_tool_*` frame-render smokes
block on window creation in this agent session and were not exercised;
all other ~285 tests pass including every campaign/projection/parity
suite.
The `FreshCampaignState` surface is now fully mirrored through `7dd543c7`:
per-civ survey rows (orphaned system, level range, progress bounds,
level/progress consistency — defensive since writers and restore
normalize), `galactic_core_observers` refs, a guarded
`capture_galaxy_persistence_metadata` umbrella (`invalid_generation_metadata`
on seed/system-count/configuration/phenomena/core-landmark disagreement),
and developer provenance (`validate_developer_simulation_state` →
`invalid_simulation`, `validate_developer_coverage` → `invalid_coverage`).
Two build caveats in this agent session: `cmake --build` requires the
vcvars64 environment (a bare-shell invocation fails at dependency
scanning and any subsequent ctest run executes stale binaries — the
first verification pass of `f45f91ed` ran stale binaries and was
re-verified at 106/106 green after a proper rebuild); and the in-flight
`native_voice_settings_tests.cpp` fails /WX on an unused `kUp` local in
another lane, which stops the default `all` build — targeted test-target
builds work.
Runtime-held state outside `FreshCampaignState` is now covered through
`8875715c`: `inspect_diplomacy_invariants` (guarded
`DiplomacySnapshotInvariantValidator` umbrella + campaign-entity refs)
and `inspect_research_invariants` (codec capture + save-path `restore`
replay + orphaned-civ rows) are wired into the monitor, developer report
and QA host. A pre-existing failure was also repaired:
`native_developer_diagnostics` broke when the day's ops-findings lane
(`0c5d7f20`/`e75cd431`) pushed the monitor past the eight-row
newest-first view, hiding the oldest session-start record — the test now
scrolls to the tail before asserting it (stale assumption, not a
behavior regression). Fresh campaigns produce zero false-positive
research/diplomacy findings (`developer_qa_host` 41 s soak passes).
Follow-ups since: `a5bd5f97` reordered `inspect_research_invariants` so
precise `orphaned_civilization` findings precede the codec-restore
umbrella (restore rejects absent-civ rows itself, so the precise check
was unreachable). `453d540c`/`23d8df65`/`6878510b` landed
advance-failure attribution: every `CampaignFrame::advance` call retains
the `IntegratedAdaptiveCampaignAdvanceTrace`, and a mid-step throw
records `CampaignAdvanceFailure{phase,message}` on the frame
(`last_advance_failure()`, cleared by the next attempt) — phases
`core`/`sensor`/`research`/`diplomacy`/`chronicle` via
`campaign_advance_failure_phase`, `tactical` for the combat route,
`stellar_activity` for the weather-clock advance. Consumers: the QA host
logs `simulation/step_failure` before its critical checkpoint, and the
native client catches developer-session step throws into
`CampaignDiagnosticMonitor::observe_advance_failure` → the fault
capture's pause+bundle path instead of crashing (player sessions still
throw). `3f0a8fb0` added `inspect_continuation_invariants(runtime,…)` —
the last unvalidated runtime surface: it captures
`runtime.continuation()` (strategic coordinator cached plans + diplomacy
schedule) and replays `validate_campaign_runtime_continuation`, emitting
`invalid_continuation`/`orphaned_plan`; wired into monitor/report/QA
host, zero findings on seeded campaigns (4/4 diagnostics surface green,
qa_host 31 s). Runtime-held state coverage is now complete:
`FreshCampaignState`, diplomacy, adaptive research, continuation/schedules
— event history excluded as legitimately historical, lane caches as
derived/ephemeral. `90541065` moved profiler span/aggregate recording off
the global mutex into per-thread buffers (merged at frame boundaries and
thread exit; `aggregates()`/`export_json` merge un-drained buffers on
read; `enabled_` atomic) with a 4-thread recording test — row-19
lower-perturbation item, 5/5 profiler/diagnostics/shell green.
`e54d4177` exported `encode_player_campaign_v17_document` so the replay
checkpoint builds the canonical DOM once instead of serialize+re-parse
— identical document, hash-compatible recordings; 6/6 save-parity/
session tests green. Verified: `campaign_frame_parity` asserts the record
on its real StrategicFailure throw and its absence after the moved-owner
advance; 8/8 across diagnostics/fault/session/QA surface green
(qa_host 29 s).
World memory census (row 20): `World::estimated_memory_bytes()` plus
`EntityRegistry::memory_bytes()` and component-store `memory_bytes()`
measure container capacities (registry slots/free, sparse/dense vectors,
hierarchy + legacy maps, codecs, membership) — occupancy reporting for
`MemoryTracker::report`, not allocator truth; `TrackedAllocator` stays
opt-in rather than retrofitting the World speculatively. The developer
report registers the projected campaign world as the
`campaign-world-projection` subsystem and carries
`worldProjection.estimatedMemoryBytes` in session metadata.
`engine_diagnostics` covers census growth; `campaign_world_projection`
covers the census footprint field; 6/6 across the diagnostics/projection/
report/QA surface green (qa_host 50 s).
Row-30 ENTITIES inspector (`044076c9` + drill-down follow-up): the
diagnostics panel's ENTITIES view runs a read-only
`project_campaign_world` over the authoritative campaign on
open/refresh, listing every entity as `domain id · tag fields ← parent`
with census counts and `estimated_memory_bytes` in the header.
The panel keeps one projected world and reconciles it via
`sync_campaign_world` on refresh — the incremental path's first live
consumer; the header shows `synced +c ~u -d ↻r` drift counts.
`native_developer_diagnostics` clicks through the view (fresh
projection + synced refresh) and verifies the campaign stays
byte-identical.
Event-history memory census: `EventHistory::estimated_memory_bytes()`
counts inline deque storage plus per-event payload heap (strings,
actor/visibility/tag vectors); the diagnostic monitor reports it as the
`campaign-event-history` subsystem on the daily inspection cadence —
the journal is capacity-bounded but payload-heavy, so this tracks
chronicle growth over long campaigns. `engine_diagnostics` covers
payload accounting; 4/4 diagnostics/report/QA green (qa_host 32 s).
Replay-recorder census (follow-on): `ReplayRecorder::
estimated_memory_bytes()` counts command payloads — unbounded during
`--record` sessions — and the client reports it as `replay-recorder`
while recording. `replay` covers the estimate + parsed round-trip;
3/3 replay/session/persistence green.
Earlier caveat retired: `campaign_phase_cadence`/`phase_profile` now
compile and pass at HEAD (verified 13.2 s / 0.1 s) — the coordinator
refactor landed.
A 319-test run at `57ab71dc` records 317/319 green after the workspace
keyboard-focus lane completed: every remaining native surface adopted the
focus contract — settlement (choice rows), logistics (refresh/close),
colony freight-review modal, economy (refresh/close/industry priorities),
shipyard (search edit-mode precedence, sort/filter dropdowns, dynamic
design and queue-order cards, quantity/favorite/build controls,
cancel-confirmation narrowing), construction (clipped project and
status-order rows, two-stage PrepareCancel/Cancel), research (domain
tabs, every render-registered interface hit, guided/tree cards, inspector
action), fleet (outliner rows, release-gated military-order and Locate
controls activated through a matched press+release pair, recovery rail
with the disabled queued-return excluded, engage, preview confirm), and
battle (chrome + two-column order grid; field formation selection stays
pointer-spatial by design and the targeted pick state remains
Escape-cancellable). The two failures (`native_developer_diagnostics`,
`campaign_diagnostics`) are inside the other agent's actively-edited
diagnostics lane — both binaries were built from their unstaged
`inspect_diplomacy_invariants` mid-flight changes, not committed code;
every accessibility suite passes. Remaining accessibility gaps:
screen-reader contracts and per-surface text scaling.
Incremental audio streaming (`6c189626`): `AudioStreamDecoder` +
`open_audio_stream` pull-decode 48 kHz stereo F32 on demand through a
lazy Media Foundation reader (first `read()` binds COM on the consuming
thread), `rewind()` loops without rebinding, and neither the 16 MiB
source nor the 96 MiB decoded caps apply — music memory is bounded
regardless of track length. `AudioOutput::play_music` gained a decoder
overload feeding the same 0.75 s bounded SDL queue; `AudioDiagnostics`
reports `music_streaming`; `AudioStreamError` types stream failures so
the director classifies them as permanent asset faults instead of
arming device recovery. The client's music track now streams through
`open_audio_stream` — the largest whole-file decode is gone from the
startup job (`NativeAudioStats::music_streaming` exposes it). Effects
and voice stay on whole-clip decode. Coverage: chunked PCM parity vs
whole-file decode (wav+mp3), rewind replay, >16 MiB source streaming,
missing-source rejection, streamed-music queue/diagnostics, plus the
existing recovery/queue-bound tests; 3/3 audio suites and the full
native client build green. The phase-cadence caveat above is stale —
`campaign_phase_cadence`/`phase_profile` compile and pass at HEAD.
Positional effects (`4b82ca7a`): `play_effect(clip, pan)` applies an
equal-power stereo pan at queue time (SDL stream gain is scalar, so
per-channel weighting happens on the PCM; centered stays zero-copy);
RuntimeHost pans the player bounce cue by the entity's camera-relative
screen x — the first engine-side consumer of positional effects.
321/321 full suite green at `4b82ca7a` over the merged state including
the other agent's diagnostics/replay lane — the earlier
`campaign_diagnostics`/`native_developer_diagnostics` failures resolved
once that lane committed.
Roster search (row-24 Table filtering UI): `RosterWorkspace` gained a
pointer-focused search field driving `TableModel::refilter` — the
shared model's first filtering consumer. Case-insensitive contains runs
over all row cells; the field renders a localized placeholder +
caret-state stroke (`ROSTER_SEARCH` in en/de catalogs), Backspace drops
whole UTF-8 sequences, Escape blurs instead of closing, and
`wants_text_input()` joins the client's text-routing aggregate.
`display_order_` keeps its display-position→source-row contract so
filtered opens hit the right colony; the filter survives live `set_view`
refreshes exactly like the column sort does. `native_colony_roster`
covers focus/needle narrowing/open identity/refresh persistence/
Escape-blur/clear-restore; 3/3 roster+localization suites and the
native client build green.
Do not change the default branch or merge
this integration branch to main without explicit integration intent.

## Preserve these contracts

- Core owns gameplay and observer privacy; UI controllers issue validated
  commands and read snapshots. Do not create UI-only research/economy/AI rules.
- `PlanetAppearance`, stable IDs, accepted/rejected pools and source hashes are
  shared authority. Preserve supplied art, cloud policy, axes and tidal locking.
- Keep Sol bodies and moon parents reserved, Pluto/Charon barycentric handling,
  Triton's retrograde inclination and analytic multiple-star hierarchy.
- Separate strategic time from cosmetic rotation/tumble and the persisted
  stellar-activity clock. High game speed must not multiply flare frequency.
- `ImagePreparationQueue`, image byte reservations, GPU resource bounds and
  asset registry validation are correctness controls, not optional warnings.
- Keep Player/Developer save separation, recovery/migration, and maintenance
  locks/transactions. Never use real user saves for destructive tests.
- Preserve the current planetary colony screen; superseded surface-view files
  were intentionally removed. Do not resurrect them from old workflow targets.
- Large artwork is in Git LFS. Original import masters, rejected pixels,
  screenshots, cooked packages, compiler output, saves and credentials are not
  a substitute for source-controlled code and reviewed manifests.

## Engine-first and GitHub communication rules

If a requested feature exposes a reusable engine gap: identify its owner,
extend Stellar Engine in C++, test the capability, integrate Core/App consumers,
and update the capability registry. Do not fake behavior, hardcode a one-off
workaround, duplicate a subsystem or silently omit a requirement.

Each significant workstream needs an appropriate branch, meaningful commits,
updated status/limitations, a decision record when architecture changes, and a
handoff before stopping. GitHub must carry that context; chat is not the project
database. Do not force-push or erase unrelated work. A failed test must remain
visible until its cause and correction are reviewed.
