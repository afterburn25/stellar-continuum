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
Leaf-level localization: `--record` now also writes each capture's
canonical document to `<recording>.expected/<tick>.json`; on divergence
`--replay` leaf-diffs it against the actual dump via the engine's
`document_leaf_diff` and writes `replay-divergence-<tick>.diff.txt`
(`path` + expected/actual values, first 32 leaves) — the divergence
message names the first leaf path. Sidecars are dev artifacts alongside
the recording; their absence degrades to the previous section-only
report. `--replay <file> --replay-until <tick>` adds the bisect half:
the client dumps the canonical document at the requested simulated tick
to `replay-until-<tick>.json` beside the recording (same canonicalization
— wall-clock provenance stripped), leaf-diffs it against the expected
sidecar when one exists (`replay-until-<tick>.diff.txt`), prints the
first leaf, then exits. A stall guard covers the case where the tick can
never reach the target: pending commands key off the simulated tick, so
a frozen tick cannot unpause — 600 frames without progress prints a
"tick stalled" diagnostic and exits instead of hanging.
`--replay-info <file>` is the inventory companion: it parses the recording
and prints a JSON `replay_info={...}` line — header seed/build/version,
command count + tick span + per-kind counts, per-tick checkpoint section
counts, and which `<recording>.expected/<tick>.json` sidecars exist — then
exits before window creation (headless, no GPU). Each present sidecar is
also verified: `expected_verified` recomputes the section checkpoints from
the retained document and compares them against the recorded hashes, so a
stale or mismatched sidecar (which would silently poison a later
leaf-diff) is flagged instead of trusted — `expected_mismatch` names the
first diverging section label (or `<unparseable>`/`<section count>` for
structural failures). It is standalone
(rejects `--replay`/`--record` pairing) and verified live against a
synthesized recording including matching, stale, unparseable, missing-file
and malformed-JSON paths. The journal is bounded:
`ReplayRecorder::set_memory_budget` (the client sets 128 MiB on `--record`)
makes the recorder drop entries once the occupancy estimate would pass the
bound — the recording stays an honest prefix (no later commands or
checkpoints claim fidelity), `serialize` carries `truncated:true`, and
`--replay-info`/the flush path report it. The bound is soft: a vector
capacity growth on the last accepted entry may overshoot by one step.
`--replay` emits a one-shot `replay_verified={"commands":N,
"checkpoints":M}` line once the recorded stream is fully consumed and
every recorded checkpoint verified without divergence — a scripted run
can grep it instead of timing out on the absence of failure.
`--replay-exit` closes the scripted loop: the run exits 0 on that line
instead of continuing the session, and a stall guard (600 frames with
pending commands/checkpoints and no tick or cursor progress) throws an
inconclusive-verification error rather than hanging.

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
Entities tree (row-24 tree adoption in the client): the diagnostics
panel's ENTITIES inspector now renders the read-only campaign-world
projection through `TreeModel` — the native client's first tree
consumer. Entities are placed parent-before-child (`add` resolves
links eagerly; leftovers land as roots defensively) and rows render
depth-indented with ·/›/▾ glyphs over `label_key`. A matched
press+release on a list row toggles the node's subtree (entity rows
encode `pressed_=100+row` on the existing button-press path);
expansion state persists across `sync_campaign_world` rebuilds by
node id — roots default open and remember collapses, deeper levels
default closed and remember expansions. `native_developer_diagnostics`
covers collapse/expand/persist-across-sync against rendered row
identity — the 14-row viewport clamps while the projection is larger,
so the oracle compares ordered row labels and depth indents
(`clip.x`), not row counts; 10/10 diagnostics/projection/UI surface
green (qa_host 42 s).
Entities tree keyboard contract (row-24 follow-on): `TreeModel` owns
an id-stable selection — `select`/`selected`/`move_selection` over the
flattened view; the selection clears when its id is absent (a
`select` after rebuild) or hidden inside a collapsed subtree (the next
`move_selection` drains it). The ENTITIES inspector consumes it:
arrows/Home/End move the selection with the scroll window following
(`first_` clamps so the row stays visible), Left collapses an
expanded parent or jumps to the parent row, Right expands or descends
to the first child, Return/Space toggles, a row click selects as well
as toggles, and the selected row renders with an accent fill + cyan
label. Selection persists across `sync_campaign_world` rebuilds by
node id, same as the expansion sets. `batcher_ui` covers the model
semantics (walk/clamp/hidden-clear/absent-clear);
`native_developer_diagnostics` drives the key contract end-to-end;
10/10 diagnostics/projection/UI surface green (qa_host 44 s).
Chronicle keyboard contract (row-26 accessibility): the chronicle
browser joins the workspace focus contract — Tab/arrows ring every
actionable control in (y,x) order (header controls, intro-row cyclers
plus the conditional page/focus buttons, located cards, DIP actions
and tag chips — each clipped to the list viewport), Home/End jump to
the ends, and Return/Space replay the press/release pair through the
same dispatch pointer input takes, so located cards navigate, chips
toggle their entity focus and cyclers run unchanged. The search field
owns its keys while editing (Tab/Return commit out); pointer presses,
cancels, open and close reset the ring. `native_chronicle` covers
traversal/activation/edit-mode capture/pointer reset/Escape-close;
3/3 chronicle+notification suites green.
Notification feed keyboard contract (row-26 accessibility): the
RECENT EVENTS panel joins the same focus contract — Tab/arrows ring
the actionable rects in (y,x) order (CHRONICLE/close header buttons,
then each card's explicit action buttons), Home/End jump to the ends,
and Return/Space replay the press/release pair through the same
dispatch a click takes, so Contact activates OPEN RELATIONS and
located cards emit OpenSystem unchanged. Card bodies stay inert and
only action buttons fully inside the list viewport focus — the same
`contains_rect` gate pointer activation applies. Pointer presses,
cancels, open and close reset the ring; unhandled keys still fall
through to global shortcuts. `native_notifications` covers
cycling/Home/End/shift-Tab, keyboard dispatch of Contact/System/
Chronicle commands, the rendered ring over the focused rect (empty
feed included) and pointer reset; 2/2 notification suites green.
Entities tree detail pane (row-24 follow-on): the ENTITIES inspector
splits its list region into the row scroll view (left ~62%) and a
read-only detail pane (right) for the selected row — the same
list+details idiom as the celestial index. The pane decodes the node
id back to its `EntityId` (`e<index|generation<<32>`), guards with
`World::alive`, and dumps the projected record: legacy ref, parent
name, child count and every field of whichever campaign tag the
entity carries (all nine `Campaign*Tag` domains). Row hit-testing
bounds to the rows region so clicks in the pane never toggle;
`native_developer_diagnostics` covers the pre-selection hint, the
keyboard-selected entity's tag dump and click-select feeding the pane.
`native_developer_diagnostics` green; the panel stays read-only (the
byte-identical campaign JSON assertion stands).
Controlled Assets TreeModel (row-24 player-surface adoption): the
navigator's hand-rolled collapse/flatten bookkeeping now runs through
`TreeModel` — the first player-facing consumer. Category headers are
`"c:<n>"` parent nodes over `"r:<index>"` row children; `flattened()`
produces `entries_` (row index rides the node id, the category
ordinal rides the header `label_key`), so hit-testing, rendering,
tooltips and virtualization are unchanged. Collapse truth stays in
`Preferences::collapsed` (the persisted contract); search and
external selection force-expand via the same reveal policy applied to
each header's `expanded` flag at rebuild. `native_controlled_assets`
covers the collapse→expand→collapse round-trip on top of the existing
search-reveal/temporary-reveal/generation-guard/virtualization suite;
suite green and the client TU compiles under /W4 /WX.
Diplomacy keyboard contract (row-26 accessibility): the RELATIONS
workspace joins the focus contract — `focusables()` walks actionable
rects in (y,x) order (close, the nine-button filter grid, contact
rows clipped to their viewport, the conditional
transmission/negotiate/war action stack, the tab strip, and the
detail region's proposal buttons or intelligence FOCUS link) and an
open modal narrows the ring to its terms or confirm/cancel pair.
Tab/arrows/Home/End ring the set; Return/Space replay the click at
the focused rect through the same dispatch, so SelectContact, Action,
ProposalAction and FocusSystem commands emit unchanged. Pointer
presses, cancels, open/close and discard reset the ring; unhandled
keys fall through to global shortcuts. `native_diplomacy_workspace`
covers cycling/wrap/Home/End, keyboard-driven negotiate→term→confirm
issuing propose_non_aggression with the state quote intact,
Space-select on a contact row, ring rendering and pointer reset;
8/8 diplomacy suites green.
Colony roster keyboard contract (row-26 accessibility): the OWNED
COLONIES roster joins the focus contract — `focusables()` walks the
search field, refresh/close, the sort-column headers (the same hit
zones `header_column` answers, including the compact-mode collapse
to the single population header) and every list row clipped to the
viewport, in (y,x) order. Tab/arrows/Home/End ring the set;
Return/Space replay the matched press+release pair through the same
dispatch, so sort headers toggle, refresh fires and rows emit their
generation/player-guarded open-colony command unchanged. The search
field enters edit mode on activation and owns its keys until
Tab/Return commit out; pointer presses, cancels, open/close reset
the ring. `native_colony_roster` covers cycling/wrap/Home/End, row
activation, sort-header toggling, edit-mode key ownership, ring
rendering and pointer reset; suite green.
Diagnostics VirtualizedList (row-24 scroll-model adoption): the
panel's hand-rolled `first_` index is gone — all four scroll views
share one engine `VirtualizedList` configured per frame (33·s phase
rows and entity rows, 57·s event snapshots, 61·s asset records).
`scroll_to` clamps wheel deltas and re-clamps every render, so a
collapse or refresh that shrinks content can no longer leave a stale
`first_` past the tail (the prior render-time-only clamp); keyboard
selection follow is `ensure_visible` instead of the manual clamp.
Offsets snap to whole rows (`snap_list`) so the top row always renders
fully — no row clipping needed. `native_developer_diagnostics` covers
wheel-to-tail, scroll-follows-selection and click row math through the
model; 11/11 diagnostics/projection/UI surface green.
Controlled-assets keyboard contract (row-26 accessibility): the
navigator joins the focus contract — `focusables()` walks hide,
search, the conditional clear button and every clipped-visible tree
entry (category headers and rows) in (y,x) order. Tab/arrows/Home/End
ring the set with scroll-follow keeping the focused entry fully
inside the viewport; Return/Space replay the press/release pair
through the same dispatch, so headers toggle persisted collapse and
rows select/manage with generation+observer guards unchanged. Hidden
mode narrows the ring to the restore control (Return unhides); the
search field owns its keys while editing (Tab/Return commit out);
pointer presses and cancels reset the ring. `native_controlled_assets`
covers cycling/wrap/Home/End, edit-mode ownership, header collapse
via keyboard, row selection via Space, ring rendering, pointer reset
and hidden-mode restore; suite green.
Diagnostics TableModel (row-24 sort-model adoption): the default
view's phase table (Phase/Samples/Mean ms/Maximum ms) now sorts
through `TableModel` — header clicks cycle ascending→descending with
the roster's `^`/`v` marker and reset the scroll. Rows are rebuilt
each frame into `set_rows` (numeric cells carry the timing values, so
mean/maximum sort numerically and "Unmeasured" rows order at zero);
`sort_state_` survives the per-frame refresh, and the model's stable
tie-break is the row id (phase name). `native_developer_diagnostics`
asserts the first-row phase flips between ascending and descending and
the direction marker renders.
Small-body panel keyboard contract + focus suppression (row-26
accessibility): the system workspace's small-body survey panel joins
the focus contract — `small_body_ring_` cycles the always-on
launcher/motion chrome plus the open panel's close, field/body/focus
controls and developer debug/spawn buttons in (y,x) order;
Return/Space replay the press dispatch so toggle_motion, field/body
cycling, focus_small_body and spawn commands emit unchanged, and the
ring keeps focus after activation. Pointer presses/cancels and
close()/survey-loss reset it; the ring renders over launcher/motion
when the panel is closed too. Dispatch plumbing: the new
`wants_keyboard_focus()` guard joins `wants_text_input()` in the
galaxy input-mapper suppression, so a focused ring on any post-mapper
surface (research/shipyard/economy/supply/fleet/construction
workspaces, colony roster, controlled assets, small-body panel) keeps
bound galaxy actions — notably Space→toggle_pause — from preempting
Return/Space activation. Pre-mapper surfaces (notifications,
chronicle, navigator) already claim their keys first.
`native_system_workspace` covers ring traversal, motion-toggle
activation emitting toggle_motion, launcher Return opening the panel,
ring-on-close rendering, Next-field cycling, and pointer/cancel resets.
Pause-menu keyboard contract (row-26 accessibility): `menu_focus_`
rings the seven stacked pause-menu actions in layout order; Tab/arrows
cycle with `menu_hover_feedback_.cue` playing the hover audio,
Home/End jump the ends, and Return/Space dispatch through the new
shared `activate_menu_action` (extracted from the pointer chain so
keyboard and click take identical paths). Pointer presses/cancels,
toggle_menu, and new-game pending/cancel transitions reset focus; the
pending menu narrows to Continue and Return/Space cancel the pending
campaign exactly like Escape/click. The ring renders only over drawn
buttons (pending mode clamps to index 0). Note: the pause menu lives
in the windowed campaign class — no headless test drives its event
loop, so this slice is verified by the client build + inspection
rather than a unit test.
Inspection-card keyboard contract (row-26 accessibility): the map
inspection card's close control joins the focus contract — Tab/arrows/
Home/End land the single-target ring (drawn over the X button),
Return/Space replay the press dispatch so `closed`/`selected_id_
reset` flows identically to a click, and pointer presses/cancels plus
clear() reset focus. Escape now dismisses a visible card before the
Escape chain falls through to the menu. `inspection_card_.focus()`
feeds `wants_keyboard_focus()`. `native_inspection` covers Tab focus,
ring rendering, single-target cycling, pointer reset, and Return
dismissal; the pre-existing "card does not swallow unmapped keys"
assertion still passes (zero-key events stay uncaptured).
`NativeMissionView` (missions/settlement panel) is now wired into the
client behind a secondary-rail MISSIONS button (`UiAction::Missions`,
rect via `secondary(5)`, localized `NAV_MISSIONS` label, HUD ring entry,
text-glyph affordance — no nav art asset exists). `refresh_missions`
rebuilds the board/settlement-fleets/colony-rows feed on generation
change while open, and its commands route to real paths: FocusFleet →
`fleet_controller_.select` + camera center, OpenColony → the overview
entry, LandColony → planetary surface entry, CollectOutpostFreight →
surface entry + freight preview (paused, gesture-captured). It is a
non-modal floating panel: Escape/pointer-cancel close it, sibling
navigation workspaces close mutually, `toggle_menu` and `enter_system`
close it. The panel adopts the keyboard focus contract — Tab/arrows ring
the actionable controls in (y,x) order (close, tabs, enabled site
pagers, select-ship, colony View/Land/Collect), Return/Space replay the
matched press/release dispatch so FocusFleet/OpenColony/LandColony/
CollectOutpostFreight emit unchanged, Escape releases the ring before
the host closes the panel, and `map_hud_visible()` excludes it so the
map focus groups cannot preempt its keys. `focused_label`/
`focused_bounds` feed `announce_focus` with Button control kind;
`native_missions` pins the ring order, activation dispatch, Escape
layering and pointer reset, and the navigation smoke exercises the
affordance, ring arming and Escape layering end-to-end. Panel chrome
(title, tabs, pager/select/row buttons, empty state, focus and phase
labels — roles reuse `FLEET_ROLE_*`) resolves through
`set_localization`; the producer-built data strings (card summaries,
site details, freight reasons) remain authored English.
Entities search (row-24 diagnostics tree usability): the ENTITIES
inspector gained a pointer-focused search field in the census header
band (navigator contract — StrokedRectangle chrome, cyan focus ring,
muted placeholder). Typing rebuilds the tree to matching entities plus
their expanded ancestor chain (reveal semantics — kept nodes force
expanded, non-matches never place), and the census appends the kept
count ("· N shown"); clearing the text restores the user's persisted
expansion sets. The field owns the keyboard while focused (Tab/Return
commit out, everything else captured), Escape blurs instead of
closing, Backspace pops a UTF-8 code point, and the new
`wants_text_input()` joins the client's text-input gate in main.cpp
so SDL delivers text while it holds focus. Clicking elsewhere blurs;
a row click still selects. The test covers focus, filtered leaves,
expanded ancestors, the census count, Escape-blur, and the
clear→restore round trip. `native_developer_diagnostics` green;
client builds clean under /W4 /WX.
Events search (row-24 diagnostics usability): the RECENT EVENTS view
shares the same pointer-focused field contract (header-band search
rect, shared chrome helper) over the 512-record retained ring — the
filter compiles to a render-time index list (`event_view_`) so
hit-testing, scroll bounds and rendered cards all agree on the same
visible set, and the header appends the kept count. The test
isolates the always-present session record by needle, asserts the
filtered card count and census, and covers Escape-blur.
`native_developer_diagnostics` green; client builds clean.
Developer-panel VirtualizedList sweep (row 24): the celestial index,
planet-type index, empire monitor (empire + active-research lists),
and phenomena debug dump all migrated from hand-rolled `first_`/`scroll_`
windows to the engine `VirtualizedList` — configured per handle/render
call so rebuilds that shrink the row set can never leave a stale
offset past the tail, and whole-row scrolling preserved by quantizing
the pixel offset. The phenomena dump's old offset had no upper clamp
at all; the model's `scroll_to` bounds it now. Tests gained wheel
oracles asserting rows shift on scroll and the view returns to the
head / clamps at the tail. `native_developer_index`,
`native_developer_diagnostics`, `galaxy_phenomena` green; client
builds clean.
Screen-reader substrate, first slice (row-26 accessibility):
`AccessibilityAnnouncer` (engine/accessibility.hpp) is the bounded
live-region queue a platform AT bridge will eventually drain —
polite/assertive priorities (assertive preempts queued polite),
consecutive-duplicate collapse, capacity evicts oldest polite first,
monotonic sequence numbers. The client consumes it immediately two
ways: `publish_notification` announces every feed item, and the pause
menu announces each focused action's localized label on navigation
(`announce_menu_focus` shares `menu_action_label` with the renderer).
Pending announcements surface through the existing voice-caption
channel — `render_voice_caption` gained a UI-announcement fallback
shown only while subtitles are enabled (4s expiry). `economy_animation`
covers dedup/preemption/eviction/drain order; the client links clean.
Still open: platform AT bridging (UIA/AT-SPI) and per-surface
focused-label announcements beyond the menu and HUD chrome.
Map focus groups + HUD chrome ring (row-26 accessibility): the clean
map now chains its always-on focus groups — `map_focus_group_` orders
assets navigator -> fleet outliner -> HUD chrome. A nav key that would
wrap a group's boundary releases the ring uncaptured (`focus_` resets)
so the dispatcher hands the same key to the next group; activation
keys only reach the group holding focus, pointer presses/cancels and
`map_hud_visible()` transitions reset the chain, and nav keys never
fall through to raw handlers (claimed or not, they are consumed by the
chain). `NativeUiLayout::hud_actions()` exposes the 17 HUD actions in
(y,x) order — top strip, nav bar, rail; the EVENTS item drops out when
the feed is unavailable — and `hud_focus_` rings the focused control
while activation replays the pointer dispatch paths (pause/resume,
speed cycle, feed toggle, menu, `route_navigation`); focus changes cue
the hover sound and announce the localized label. This also repaired a
latent gap: the fleet outliner's keyboard block was unreachable
because the navigator claimed every nav key first. Navigator and fleet
tests gained wrap-out coverage; the client links clean. The windowed
campaign class still has no headless event-loop harness, so the chain
itself is verified by unit coverage of the wrap-out contract plus
build/inspection.
VirtualizedList::sync_rows (row 24): the configure+clamp+snap step
every VirtualizedList consumer hand-rolled (assign row count/height/
viewport, re-bound a stale offset, quantize to a whole row, derive the
first visible row) is now one engine call —
`size_t sync_rows(rows, row_height, viewport_height)` clamps the old
offset against the new `max_scroll`, snaps it to a row edge, and
returns the first visible row. The diagnostics panel's `scroll_window`
helper, both developer indexes, the empire monitor's two lists, and
the phenomena dump all delegate to it, deleting five copies of the
same arithmetic. `batcher_ui` covers snap-to-edge, stale-offset
tail clamp, and empty-list reset.
VirtualizedList configure/set_row_count (row 24): the fractional-scroll
consumers (colony roster, editor system/detail/picker lists,
engine-shell asset/key/project/entity/scene lists) cannot snap to row
edges — their first row renders partially by design — but their direct
`row_count`/`viewport_height` field assignments never re-clamped
`scroll_offset`, so a filtered or shrunk row set could strand it past
`max_scroll` and render blank space. `configure(rows, row_height,
viewport_height)` applies geometry + re-clamps without snapping;
`set_row_count(rows)` does the same for row-only mutations; every
assignment site now routes through one of them (a shrink while scrolled
deep lands on the tail instead of blank space). `batcher_ui` covers
fractional preservation, shrink clamp, and viewport-growth re-clamp.
`batcher_ui`,
`native_developer_diagnostics`, `native_developer_index`,
`galaxy_phenomena`, `native_colony_roster`, `native_controlled_assets`,
`campaign_world_projection`, `engine_diagnostics` green; client, editor
and engine-shell targets build clean.
ScrollView (row 24): variable-height surfaces duplicated the same
pixel-scroll contract — `max(0, content - viewport)` clamps, wheel/drag
deltas, non-finite guards, and proportional scrollbar thumbs. Engine
`ScrollView` (ui_viewmodels.hpp) owns it now: `sync(content, viewport)`
re-clamps on reflow, `scroll_by`/`scroll_to` bound deltas, and
`thumb(track, min_size)` returns proportional thumb geometry ({0,0}
when content fits). The chronicle browser, notification feed, body
inspection panel, and economy workspace adopted it; their layout
structs carry `ScrollView` members instead of raw
content_height/max_scroll/scroll triples. `batcher_ui` covers bounds,
non-finite reset, proportional thumb math, and fit-content hiding;
`native_notifications`, `native_chronicle`, `native_economy_workspace`,
`native_system_workspace` green; client builds clean.

ScrollView sweep (row 24): the remaining raw-scroll surfaces adopted the
same model — diplomacy contact/detail panes, colony freight review,
research inspector plus the guided-card grid and horizontal
active-program strip, fleet outliner, new-game species/detail panes,
settings screenshot-path field, system-inspection card, supply-network
workspace, controlled-assets navigator, planetary screen
(facts/slots/details/queue) and shipyard designs/orders/details.
Negative-offset conventions (fleet, colony, construction, planetary,
shipyard designs/orders) were inverted to positive `scroll_offset`
subtracted from row geometry; the shipyard `detail_limit_` cache folded
into the model's `content_height`. Two fixed-stride lists went to
`VirtualizedList` instead: the startup save-slot list (`sync_rows`) and
the construction project/order lists (fractional `configure`, since
their wheel delta is not a row multiple). `native_diplomacy_workspace`,
`native_colony_workspace`, `native_research_workspace`,
`native_fleet_workspace`, `native_new_game_workspace`,
`native_logistics_workspace`, `native_inspection`,
`native_construction_workspace`, `native_startup_workspace`,
`native_shipyard_workspace`, `native_planetary_screen` and
`native_controlled_assets` all green. The only remaining hand-rolled
scroll is the general-settings screenshot-path field (file was
write-locked by a concurrent editor session during the sweep — migrate
`path_scroll_` to `ScrollView` when free).
ScrollView sweep follow-up (row 24): the planetary screen kept the
negative-offset measurement formula after the convention inversion —
its facts/queue/details cursors start at `pane.y - scroll_offset`, so
`height = cursor - pane.y - scroll_offset` produced `H - 2*offset`,
content height became scroll-dependent, `sync` clamped to a moving
maximum, and wheel scrolling oscillated instead of converging (the
economy pane could never reach "Collect materials"). The trailing term
now adds the offset back so heights are measured in unscrolled content
coordinates (`5f5684e7`); `native_colony_workspace` green again. Same
session: focused_label adoption batch 2 — chronicle, colony roster,
colony freight modal and notification feed expose `focused_label()`
and the dispatcher announces on focus change (`2581fbd8`).
The general-settings `path_scroll_` ScrollView migration noted above
landed once the editor's file lock cleared (`141d8408`). Same session:
settings surfaces complete the focused_label sweep — the settings hub
and General/Audio/Video/Voice panels expose `focused_label()` (dynamic
controls announce "Name: value" like they render; modal states narrow
to their live controls) and both dispatch layers announce on focus
change (the client's settings block and the startup entry's
`route_settings`). `native_audio_settings`, `native_general_settings`,
`native_video_settings`, `native_voice_settings`,
`native_startup_workspace` all green.
Announcement speech playback (row 26): announcements can now reach
actual speech output — `VoicePreferences` gains an opt-in
`interface_announcements` flag exposed as "Speak interface
announcements" in the Voice & Subtitles panel (15th focusable; panel
grew 680→725px). The strict settings schema accepts the optional
`interfaceAnnouncements` key so pre-existing 10-key files load with it
defaulting off. The campaign's announcer drain submits each item to
`NativeVoicePlayback::speak` as an Important-priority `interface`-
category request with `ReplaceCategory` queueing (rapid Tab/arrow runs
collapse to the latest label), per-announcement dedupe keys (re-focused
identical labels still speak), 10s expiry and `interruptible` honoring
`no_interruptions`; captions remain independent under the subtitles
preference. Startup-flow announcements stay caption-only for speech
because the voice pipeline starts with the campaign session.
UIA bridging (row 26): `NativeAccessibilityBridge` is the platform AT
slice — it subclasses the game HWND (`GWLP_WNDPROC`; SDL's message hook
cannot answer WM_GETOBJECT because the hook cannot supply a return
value), answers `UiaRootObjectId` with a minimal server-side
`IRawElementProviderSimple` (pane, "Stellar Continuum", outside the
control tree) and raises `UiaRaiseNotificationEvent` per announcement
when `UiaClientsAreListening()`. `Window::native_window_handle()`
exposes the HWND read-only; both announcer drains feed the bridge.
`native_accessibility_bridge` verifies the contract end-to-end through
the real UIA client (`ElementFromHandle` resolves the provider's name
and control type) plus WM_GETOBJECT fallthrough and detach. Same
session: `AccessibilityAnnouncement` gains a `Kind` (Status/Focus) —
every `focused_label` announce site routes through `announce_focus`,
and Focus items raise `UIA_AutomationFocusChangedEventId` on a
synthetic focus fragment (custom control, HasKeyboardFocus, label as
name, Parent/FragmentRoot navigation, `GetFocus` on the window root)
instead of a live-region notification; the UIA test walks the raw tree
to the fragment and checks its name/type/focus (UIA splices the host
HWND's native children into hostable providers — the test iterates
siblings to find ours). Open: AT-SPI/non-Windows backends and UIA
control patterns.
Per-control focus geometry (row 26): `AccessibilityAnnouncement` now
carries optional `AnnouncementBounds` and every focus-bearing surface
exposes `focused_bounds(...)` mirroring its `focused_label` — settings
panels, workspaces, modals, the assets navigator/fleet/HUD/map group
chain, the pause-menu ring and the startup screens included. The
dispatcher passes the ringed control's client-pixel rect through
`announce_focus`, the drains forward it, and the focus fragment reports
it as `BoundingRectangle` (client→screen projected) — magnifier and
tracking AT get the real control rect instead of the whole window. The
bridge test asserts the fragment's reported bounds against a
`ClientToScreen` expectation; `economy_animation` pins the bounds
round-trip on the announcer. Focus release is also honest now: an empty
`announce_focus` text is a release signal (still dropped for Status), the
fragment stops claiming `HasKeyboardFocus`, `GetFocus` falls back to the
window root, and a focus-changed event fires on the root — so a ring
releasing at a group boundary or on Escape no longer leaves a stale
control claiming focus. Empty items are skipped for speech/captions.
Slider announcements additionally carry `AnnouncementRange`
(min/max/value): the fragment exposes a read-only `IRangeValueProvider`
via `GetPatternProvider` (the raw-provider pattern entry point — not
QueryInterface), so Narrator-class AT reports slider position in range;
`SetValue` fails honestly since adjustment stays on the key/pointer
contract. Audio and voice settings populate it. Focus announcements also
carry `AnnouncementControl` (Button/CheckBox/Edit/Slider/Group/Custom) —
the fragment reports the matching UIA ControlType, with a valid range
implying Slider for unclassified announcements. Audio/voice settings
classify via `focused_control()`; the pause-menu ring and HUD chrome
announce as Button; the text-field surfaces classify their search/seed
fields Edit — colony roster, chronicle, controlled-assets navigator,
research and shipyard searches plus the new-campaign seed field (the
startup workspace delegates to it on the Setup screen), each
`focused_control` comparing the focused rect against the surface's
known text-field layout rect. The startup settings route now forwards
range and control too (previously label+bounds only). Remaining
surfaces default to Custom until they classify their focusables.
Planetary ring (row 26): `NativePlanetaryScreen` — the last
player-facing surface without a keyboard ring — now walks its
render-registered hit registry: every enabled button, layer/view-mode
control, tab, action row and clipped-visible slot cell sorts into a
(y,x) ring; Return/Space run a shared `activate(hit)` extracted from
the pointer release path so keyboard and pointer dispatch are
identical by construction; the confirmation modal narrows the ring
automatically (it clears `hits_` before registering Cancel/Confirm);
Escape releases the ring before the modal-cancel/deselect/Back chain;
pointer press/cancel reset it; globe region picking stays
pointer-spatial. `NativeColonyWorkspace` delegates
`focus()`/`focused_label`/`focused_bounds`/`focused_control` to the
screen outside the freight modal (`set_freight_preview` releases the
planetary ring so it cannot freeze under the overlay). Hits gained a
`label` field captured from the rendered title/slot name so
announcements carry real text. `native_planetary_screen` pins the
ring, Escape layering, activation replay and modal narrowing;
`native_colony_workspace` pins the delegation. Note: client-side the fragment resolves with
the host HWND runtime id `{42, hwnd}`, not our appended `{3, 1}` — the
bridge test walks raw children matching that shape.
Developer celestial index ring (row 26): `NativeDeveloperCelestialIndex` —
the first developer-tool surface to adopt the contract — rings its
rendered controls in (y,x) order: close, category filter, the search
field (Edit; activation enters edit mode, Tab/Return commit out), every
rendered row (scroll-follow via `VirtualizedList::ensure_visible`), the
central-state dropdown (central objects only) and the center-map action.
Keyboard activation replays the shared hit dispatch — dropdown opens,
row selection, `set_developer_central_black_hole_state` and the
focus-request path emit unchanged. Escape layers edit → ring → close;
pointer press/cancel clear the ring. The dispatcher announces focus
moves through `focused_label`/`focused_bounds`/`focused_control` and the
panel joins `wants_keyboard_focus()`. `native_developer_index` pins the
ring, Edit classification, edit commit, activation replay, pointer
reset and Escape layering. `NativeDeveloperPlanetIndex` follows the same
contract: header actions, class filter, rendered rows and the
conditional rules/go/generate footer ring in (y,x) order; keyboard
activation replays the press/release dispatch so the canonical
`force_developer_planet_type` command and the GO TO EXAMPLE focus
request emit unchanged. The phenomena-debug overlay (close, density
dropdown, region navigation, six CheckBox-classified option toggles)
and the system-background debug overlay (eight option/region buttons
through a shared `activate_button` dispatch) adopted the same contract;
`galaxy_phenomena` and `system_background` pin their rings. The
giant/ring test panel follows: all twenty buttons ring in (y,x) order,
activation replays the shared `activate_button` switch so spectrum,
distance, tilt and subclass changes run `apply_developer_giant_test`
unchanged, the ring/planet shadow toggles classify CheckBox, and the
3D preview stays pointer-spatial; `native_giant_visual` pins it. The
stellar activity panel follows: all twenty-four command buttons ring in
(y,x) order and activation replays the shared `activate_button` switch,
so force/cycle/scrub/move commands run `apply_developer_stellar_activity`
unchanged; `native_developer_index` pins it. The developer empire
monitor follows: close, refresh, show-home and the rendered empire
rows ring in (y,x) order through a shared `activate_hit` switch —
row selection and the home-system focus request replay the exact
press/release dispatch pointer input takes, scroll-follow via
`VirtualizedList::ensure_visible` keeps the focused row visible, and
the read-only Core snapshots stay inspection-only;
`native_developer_index` pins it. The developer simulation panel
follows: all sixteen rendered buttons ring in (y,x) order through a
shared `activate` switch — speed presets run `set_developer_speed`,
pause/resume runs the tactical/strategic clock toggle, AI control runs
`set_developer_ai_control`, and the index/diagnostics/empire/stellar
requests emit unchanged — while disabled controls (ADVANCE ONE TICK
without a pending step, EXPORT while busy) skip the ring exactly like
pointer input; `native_developer_index` pins it. The developer
diagnostics panel closes the sweep: the chrome, record-detail
dropdown, sortable phase headers, entities/events search fields
(Edit), and rendered entity rows ring in (y,x) order through shared
activate helpers — row activation replays the select+toggle dispatch,
the entity tree's arrow/Home/End selection model keeps its keys while
the ring is inactive (Tab enters the ring; arrows join it once
focused), and an open dropdown or focused search owns its keys first;
`native_developer_diagnostics` pins it. Every native panel — player
and developer — now carries the keyboard focus contract.
Accessibility substrate adoption (row 26): `GeneralPreferences` now embeds
the engine `AccessibilitySettings` struct as the canonical accessibility
carrier (`accessibility` member — reduce-motion/flashing, high-contrast
and the typed `ColorBlindMode`, plus text/subtitle scale) instead of
shadowing the same concepts as loose fields. The persisted JSON shape is unchanged (same keys, same
validation); `effective()` folds the interface-scale preset into
`ui_scale` and sanitizes for consumers. `AccessibilitySettings` gains a
defaulted `operator==` for draft/saved comparison. Client reads moved
from `saved().reduce_motion`-style fields to `saved().accessibility.*`;
the daltonization site no longer casts an int ordinal. `AccessibilitySettings`
is no longer dead engine surface.
Text/subtitle scale consumption (row 26 follow-up): General Settings
gains a third accessibility row — SUBTITLES toggle, SUBTITLE SIZE and
TEXT SIZE preset cyclers (0.75/1.0/1.25/1.5/2.0) — persisted as
`subtitlesEnabled`/`subtitleScale`/`textScale`. Consumers:
`render_voice_caption` takes the effective `AccessibilitySettings`
(gates captions on `subtitles_enabled`, multiplies the voice-preferred
pixel size by `subtitle_scale`; both the in-game and startup call sites
pass `general_settings.saved().effective()`), and
`NativeUiLayout::set_text_scale` multiplies only the shared font metrics
in `for_viewport` — text enlarges without growing chrome geometry.
`native_ui_layout` verifies fonts scale while rects stay identical.
In-app rebind UI (row 16): the settings hub Controls view binds against the
campaign's live `InputMapper` — `context("GALAXY")` enumerates Button actions
into rows ("Toggle pause — Space, P"), activation captures the next
non-modifier keypress as the primary binding (alternate bindings survive;
Ctrl/Shift/Alt fold into `chord_keys`), Escape or a click cancels, and the
client persists `save_contexts()` to `galaxy-controls.json` beside the other
settings files — loaded over the built-in defaults at startup via
`NativeCampaign::load_user_bindings` (a rejected file keeps defaults).
Without a mapper the view falls back to the static help card. Engine-side:
`InputMapper::context(name)` plus `key_name`/`describe_binding`/
`describe_bindings` display helpers (SDL-free, deterministic). Coverage:
`input_actions` (helpers + context accessor) and `native_startup_workspace`
(ring, capture, rebind + alternate preservation, chord, cancels, persist).
Capture now also accepts right mouse and gamepad buttons, and a captured
binding that conflicts with a sibling action's primary steals it — the
hub surfaces a "reassigned from X" notice the client announces. The
client's update loop now feeds GamepadButton/MouseButton raw events under
the same gameplay gate as keys (releases + axis state feed unconditionally
so held state clears), closing the gap where captured non-keyboard
bindings could never fire; `--record`/`--replay` journal them as
`gamepad_button`/`mouse_button` commands. Verified end-to-end by the
navigation smoke's pad/right-click rebind exercise. Note: commit
5cca8444 left a real defect the smoke exposed — Escape cleared the
inspection card without resetting `selected_id_`, so the unconditional
`refresh_inspection()` reopened it the same frame; the chain now clears
the selection with the card, matching the card's own close path.
Pad camera axes landed in the same lane: `GALAXY_PAD` (Axis1D over SDL
axes 0/1/3 — left stick pans, right stick zooms, 0.18 dead zone,
dt-scaled) is a separate context `load_user_bindings` injects only when a
saved map lacks it, so user axis rebinds persist through
galaxy-controls.json. The navigation smoke verifies stick pan + zoom
end-to-end and that the system view does not leak a galaxy-camera pan.
Axis rebinding is live in the Controls view: `set_input_mapper` takes a
second context name, its Axis1D actions list after the GALAXY buttons,
and capturing an axis row accepts a stick deflection past a 0.5 dead
zone or a wheel scroll (discrete keys are swallowed — they cannot drive
an axis). Startup workspace tests cover the axis ring, deflection
capture, cross-context steal and wheel binding.
Multi-pad is plumbed end-to-end: the platform opens up to four pads into
stable slots, `InputEvent.gamepad_device`/`RawInputEvent.device` carry
the slot, and `InputBinding.device` pins a binding to one pad via the
input-map JSON `device` field (omitted when unset). Device-unset events
are wildcards — replayed recordings still match pinned bindings — and
live stick values key on (device, axis); unpinned axis bindings sum
every pad. Captured bindings stay unpinned so either pad drives.
Open: an accessibility input layer and device-policy/focus-capture
tests — no pad-picker UI yet, so pinning is a JSON-level feature.
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
