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
Next: combined simulation benchmarks, event/history framework, then
editor/renderer polish per the milestone map; adopt the executor for
real Core/game phases when consuming these.

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

**Recommended next workstream: native validation and release reliability.**
Start from this branch in an isolated checkout; fix the failures recorded in
the receipt before beginning the 30-item engine expansion. Use a descriptive
new branch for that work, e.g. `work/native-validation-reliability`, after
checking whether one already exists. Do not change the default branch or merge
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
