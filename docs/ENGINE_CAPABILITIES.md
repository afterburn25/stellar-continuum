# Stellar Engine capability registry

## Current capability matrix — audited 2026-09-20

Current architecture: **custom Stellar Engine / C++23 / C++23 game**, branch
`cpp/codex-native-architecture-integration`. This matrix governs present scope.
The implementation entries below it are cumulative historical records; later
corrections supersede earlier visuals/limits. A named test is an evidence owner,
not an assertion that today's complete suite passes. Read the
[actual verification receipt](validation/2026-09-20-development-sync.md).
Status meanings are defined in [DEVELOPMENT_WORKFLOW.md](DEVELOPMENT_WORKFLOW.md).

| Capability | Status | Owner / important source | Tests | Known limitation / next work |
| --- | --- | --- | --- | --- |
| Window and platform | IMPLEMENTED BUT NEEDS POLISH | Engine `native_map_platform.cpp`, runtime paths/lease; App video controller | `native_client_platform`, `native_video_platform`, `native_video_controller` | Verified Windows x64 only; portable platform interface and device recovery need work |
| Native input | IMPLEMENTED (rebinding) / PARTIALLY IMPLEMENTED (device policy) | Engine `native_map_platform.hpp` (keyboard/mouse/gamepad events), `input_actions.hpp` (`InputMapper`: stacked action contexts with exclusive fall-through, Button/Axis1D/Axis2D, chords, held gamepad-axis semantics, runtime `rebind()`, `bindings()` inspection, `save_contexts()` persisting the rebound map through the `load_contexts` schema — `RuntimeHost` feeds all normalized events through it, projects override via input-map JSON); App `map_camera.hpp`, `map_interaction.hpp`, workspaces | `native_client_input`, `input_actions` (incl. rebind + save/load round-trip), `native_ui_layout` | In-app rebind UI landed: the settings hub Controls view lists every Button action of the GALAXY context (InputMapper::context/key_name/describe_bindings), activation captures the next non-modifier keypress, right-click or gamepad button as the primary binding (alternates survive, Ctrl/Shift/Alt fold into chord_keys, Escape/left-click cancel, a conflicting primary is stolen from its sibling action with a "reassigned from" notice), and the client persists the rebound map through save_contexts to galaxy-controls.json loaded over the defaults at startup. GamepadButton/MouseButton feeds are live in the client update loop under the same gameplay gate as keys (releases and axis state feed unconditionally so held bindings clear), and `--record`/`--replay` journals pad/mouse activations as `gamepad_button`/`mouse_button` commands so rebound sessions reproduce. Gamepad camera axes are live: the `GALAXY_PAD` context binds left-stick X/Y to `map_pan_x`/`map_pan_y` and right-stick Y to `map_zoom` (Axis1D over SDL axes 0/1/3, 0.18 dead zone, dt-scaled pan/zoom on the galaxy camera under the same surface gate as wheel input — the navigation smoke verifies stick pan/zoom end-to-end and that system view does not leak a galaxy-camera pan). Axis rebinding is live in the UI: the Controls view lists `GALAXY_PAD` Axis1D rows after the GALAXY buttons (a second context name on `set_input_mapper`), and capturing an axis row accepts a stick deflection past a 0.5 dead zone or a wheel scroll (discrete keys are swallowed — they cannot drive an axis); the same steal-on-conflict and persist path applies, and `load_user_bindings` injects the default pad context only when a saved map lacks it so user axis rebinds survive reload. Multi-pad is plumbed end-to-end: the platform opens up to four pads into stable slots, `InputEvent.gamepad_device`/`RawInputEvent.device` carry the slot, `InputBinding.device` pins a binding to one pad (`device` in the input-map JSON, omitted when unset), device-unset events stay wildcards so replayed recordings still match pinned bindings, and live stick values are keyed per (device, axis) — `input_actions` tests cover pin match/miss, wildcard matching, per-device axis sums and the save/load round-trip. An accessibility input layer and device-policy/focus-capture tests stay open |
| 2D/UI renderer | IMPLEMENTED BUT NEEDS POLISH | Engine native map platform, UI skin and text fit | `native_text_measure`, `native_navigation_visual` | Shared helpers, but application-driven widgets/layout and no general UI scene framework |
| 3D renderer | IMPLEMENTED BUT NEEDS POLISH | Engine `native_scene3d.hpp`, `native_scene3d_gpu.cpp`, `mesh3d_loader` + `box_mesh`/`annulus_mesh` primitives, `Scene3dDocument` + `RuntimeHost --scene3d` (fly camera, gravity/OBB sim, GPU composite under 2D HUD); **HDR pipeline**: scenes render into RGBA16F targets and resolve through a fullscreen tonemap pass (`tonemap.vert/.frag` — C1-continuous knee+headroom curve preserving the SDR band, premultiply-aware) when `SDL_GPUTextureSupportsFormat` reports float color targets, with automatic UNORM fallback; `Scene3DStatistics::hdr` reports the active path | `engine_scene3d`, `native_scene3d_gpu` (incl. HDR-engaged pixel assertion + budget accounting), scale3d tests; `engine_project`/`engine_world` 3D doc+component coverage | Bounded CPU submission; SAT OBB collision over mesh local bounds (no per-triangle or rigid-body solver); instancing + DrawBatcher ordering + RenderGraph scheduling + TextureStreamer residency incl. partial mip tails wired (no indirect draw — per-batch pipeline/sampler binds keep draw_count=1) |
| Mesh/geometry and culling | IMPLEMENTED BUT NEEDS POLISH | Engine solid/triangle meshes, billboard batch, scene bounds | scene/triangle/scale tests | Procedural geometry and conservative limits, not a general imported geometry cooker |
| Lighting/materials | IMPLEMENTED BUT NEEDS POLISH | Engine scene material/fragment shader; spherical material preparation | `engine_spherical_material`, `native_scene3d_gpu`, `native_planet_materials` | Approximate illumination/response from artwork; no full physically calibrated renderer |
| Canonical planet classification/art | IMPLEMENTED | Core `planet_appearance.hpp/.cpp`, taxonomy/art catalogs | `planet_appearance`, `native_planet_materials` | Scoped registry/generation contract; 66 definitions do not mean every subclass has admitted art |
| Planet globes and inspection | IMPLEMENTED BUT NEEDS POLISH | App native planet globe/materials/planetary screen, shared appearance | `native_planetary_screen`, `native_giant_visual` | Broad developer smoke material assertion remains open; inspect identity/resolution before claiming full visual parity |
| Giant replacement/rings | IMPLEMENTED BUT NEEDS POLISH | Core `planetary_rings.cpp`; Engine ring material; App ring assembly | `giant_ring_rules`, `engine_ring_material`, `native_giant_visual` | Accepted art and angular detail integrated; approximate shadow/optics, limited physical evolution |
| Satellite orbit/pose | IMPLEMENTED BUT NEEDS POLISH | Core `planetary_satellites.cpp`; Engine `analytic_orbit.hpp`; App system view | `sol_catalog`, `native_moons` | Mean elements/chart spacing, not ephemerides or full perturbations |
| Multiple-star orbits | IMPLEMENTED BUT NEEDS POLISH | Core `stellar_orbits.cpp`, App system view | `stellar_orbits`, `native_system_view` | Analytic binary/hierarchical triple model, not general N-body dynamics |
| Small-body solid fields | IMPLEMENTED BUT NEEDS POLISH | Core small-body configuration/motion; App native small-body renderer | `small_body_fields`, `native_small_body_renderer` | LOD/culling/representative caps and conservative picking; not simulation of every rock |
| Ice optics | IMPLEMENTED BUT NEEDS POLISH | Engine scene material/fragment shader and native small-body materials | `native_scene3d_gpu`, small-body tests | Environment reflection/refraction with approximate thickness; no caustics/multiple internal bounces |
| Galaxy configuration/population | IMPLEMENTED BUT NEEDS POLISH | Core galaxy configuration/catalog, stellar population profiles | `galaxy_configuration`, `stellar_population_profiles`, generation/large-galaxy tests | Old full-generation fingerprint fails; validate semantic baseline before replacing expected value |
| Stars / SMBH / coverage | IMPLEMENTED BUT NEEDS POLISH | Core stellar object/coverage; App star markers/celestial appearance | `stellar_objects`, `stellar_developer_coverage`, native star-art tests | Game-tuned populations and artistic LODs, not a calibrated census |
| Phenomena and skies | IMPLEMENTED BUT NEEDS POLISH | Core galaxy phenomena/system background; Engine organic region/background; App consumers | `galaxy_phenomena`, `system_background`, backdrop tests | One admitted faint sky; local decorative gas is separate from physical phenomenon metadata |
| Stellar activity scheduler | IMPLEMENTED | Core `stellar_activity.cpp`, `CampaignFrame` independent clock | `stellar_activity`, persistence tests | Scoped event timeline/history and CME hooks; damage/space-weather game effects remain unfinished |
| Stellar VFX rendering | IMPLEMENTED BUT NEEDS POLISH | App eruption art/effects; Engine surface attachments/curved mesh; engine `VfxSystem` general framework — shared `EmitterDefinition`s, deterministic fixed-capacity pools, per-instance LOD rate fade, entity attachments, global particle budget (demand-proportional rate taper + hard headroom; `RuntimeHost` defaults to 64k) | `native_stellar_eruptions`, scene GPU tests, `render_pipeline` (budget/LOD/determinism) | Image-derived curved surfaces; client eruption art still bespoke rather than authored on `VfxSystem` |
| Entity identity | PARTIALLY IMPLEMENTED | Engine `EntityRegistry` in `foundation.hpp`; `World` component store (`world.hpp`: components, hierarchy, queries, binary snapshot/restore, `bind_legacy` namespaced IDs, generational `EntityId`s); Core `campaign_world_projection` adapts authoritative `FreshCampaignState` containers into the typed store as a read model — `sync_campaign_world` reconciles incrementally (stable `EntityId`s across refreshes, campaign-namespaced sweep only) and feeds the developer report census | `foundation`, `engine_world`, `campaign_world_projection`, `campaign_diagnostics` tests | Projection is a read model over authoritative Core containers — no full entity lifecycle/write path across game domains yet |
| Clocks/scheduling | IMPLEMENTED BUT NEEDS POLISH | Engine `FixedClock`; `SimulationScheduler`/`SimulationExecutor` tiered LOD with dependency-chained tasks, dirty/event wakeups, `consume_tick` abort semantics, capture/restore; Core strategic/tactical/developer clocks; `GalaxySimulationStepCoordinator` routes all 12 phases through the executor preserving order — phase cadence policy (`set_phase_tier`/`wake_phase`, elapsed-span scaling) gated by the seeded `campaign_phase_cadence_oracle`; event-driven wake wiring — accepted `issue_*` commands wake the dormant domains they feed (military→combat+exploration, colony/outpost→colonization+exploration, freight→freight+exploration, civilian→freight+colonization(+exploration on return)); the oracle pins the per-phase activity matrix for both the minimal and seeded worlds (7 phases provably inert in fresh campaigns) plus command-wake exactly-once semantics | `foundation`, `strategic_clock_parity`, `campaign_frame_parity`, `simulation_executor`, `simulation_persistence`, `simulation_scale_*`, `campaign_coordinator*`, `campaign_phase_cadence_oracle` | Phases remain sequential (one dependency chain); all phases still Active in production — demotion policies must clear the parity oracle per phase before adoption; intra-step parallelism unexercised |
| Jobs/threading | PARTIALLY IMPLEMENTED | Engine `JobSystem` (priorities, cancel tokens, `submit_graph` dependency graphs, per-tag stats); save writer, image preparation, audio director, territory overlay, campaign session, planet-material decode queue | `job_system`, `foundation`, image preparation, campaign session, planet-material tests | Bounded specialized consumers; no work-stealing or affinity policy |
| Events | PARTIALLY IMPLEMENTED | Engine owner-thread `EventQueue<T>` + `event_bus.cpp` typed subscriptions; Core domain events | `foundation`, `event_bus`, `mission_graph`, notification/activity tests | Event bus library unconsumed by the game; no cross-thread dispatch policy |
| Physics utilities | PARTIALLY IMPLEMENTED | Engine `physics3d.hpp`, `analytic_orbit.hpp`, `broadphase.hpp` (`UniformBroadphase<2|3>` uniform-grid candidate pairs — sorted/deduplicated, never misses a true AABB overlap; `RuntimeHost` 2D+3D contact scans consume it, replacing O(n²) pairwise enumeration); `PhysicsWorld` (`physics.hpp`: circle/AABB/segment primitives, broadphase overlap/raycast/sweep, trigger enter/stay/exit, versioned capture/restore round-tripping the live overlap set) — consumed by the engine-shell PHYSICS inspector | scene/triangle/orbit/scale tests; `render_pipeline` broadphase correctness; `engine_world` collision/landing | Kinematics, continuous primitive queries and AABB broadphase; no general rigid-body/constraint/N-body world |
| Spatial queries | PARTIALLY IMPLEMENTED | Engine point/region/3D indices, parent chains; `SpatialGrid` (deterministic cell order, insert/remove/update, radius/AABB/ray queries) — galaxy-map `system_hit` hit-tests through a lazily rebuilt `SpatialGrid<int>`; Core batch indices | `engine_parent_chain_index`, `spatial_physics`, scale/survey/economy tests | Reusable pieces; no unified query scheduler or stable world-wide index lifetime policy |
| Navigation/logistics | IMPLEMENTED BUT NEEDS POLISH | Core lane network, reach, exploration, freight/logistics | route/reach/freight/exploration parity | Domain-specific rules, not generic Engine route service; combined fleet stress still needed |
| Knowledge/observation | IMPLEMENTED BUT NEEDS POLISH | Core knowledge, campaign observation, observer commands | knowledge/diplomacy invariants | Game-scoped privacy model; no general fog engine |
| Economy/production | IMPLEMENTED BUT NEEDS POLISH | Core economy/industry/construction/shipbuilding/biology | economy/production parity, 5k colony scale | Not a generic resource graph; combined late-game load unverified |
| Research | IMPLEMENTED BUT NEEDS POLISH | Core `adaptive_research_*` catalogs/services/snapshots | native adaptive research parity family | Existing domain runtime, not proof that every advanced research design is fully exposed in UI |
| Strategic AI | PARTIALLY IMPLEMENTED | Core strategic intent/planning and fleet intelligence | strategic/campaign/exploration tests | Correctness coverage does not demonstrate effective complete long-game AI |
| Diplomacy | IMPLEMENTED BUT NEEDS POLISH | Core diplomacy lifecycle/runtime/observer commands; App workspace | diplomacy parity and native controller/workspace | Current game feature set, not all design ambitions |
| Combat | PARTIALLY IMPLEMENTED | Core combat/massive combat state and 3D motion; App battle workspace | combat/massive persistence/engine/lifecycle tests | Large combined AI/fleet/tactical performance and final gameplay breadth unverified |
| Save/recovery | IMPLEMENTED BUT NEEDS POLISH | Core Player17 DTO/JSON/recovery; Engine atomic files | persistence/recovery/save tests | Large JSON latency/memory, no incremental world DB/cloud-save service |
| Replay | PARTIALLY IMPLEMENTED | Engine `replay.cpp` command journal consumed by the client (`--record`/`--replay`, verified `diverged:false`); deterministic parity fixtures, QA checkpoints; `document_section_checkpoints` + `verify_checkpoint_sequence` localize save-checkpoint divergence to the named JSON section (`save:World.Fleets`) instead of a whole-document hash; `--record` also retains each capture's canonical document under `<recording>.expected/<tick>.json`; on divergence the client dumps the actual document to `replay-divergence-<tick>.json` and leaf-diffs it against the retained expected sidecar via `document_leaf_diff` (member/array index paths with expected/actual values, bounded at 32 leaves) into `replay-divergence-<tick>.diff.txt`; `--replay-until <tick>` stops a replay at a chosen tick and dumps the canonical document to `replay-until-<tick>.json` next to the recording — leaf-diffed immediately against the expected sidecar when one exists (writes `replay-until-<tick>.diff.txt`, prints the first leaf), giving a bisect tool for isolating which tick introduces divergence; `--replay-info <file>` prints a headless JSON inventory (header seed/build/version, command count + tick span + per-kind counts, `truncated`, `memory_bytes` occupancy, `commands_ordered`/`checkpoints_ordered` monotonicity (the feed and cursor verifier assume non-decreasing ticks — a hand-edited recording is flagged), per-tick checkpoint section counts, and which `<tick>.json` expected sidecars are on disk) and exits before window creation, so it runs in scripts without a GPU; each present sidecar is verified — `expected_verified` recomputes `document_section_checkpoints` from the retained document and compares against the recorded hashes, flagging a stale or mismatched sidecar that would silently poison a later leaf-diff, and `expected_mismatch` names the first diverging section label (`<unparseable>`/`<section count>` for structural failures); `--record` bounds the journal — a 128 MiB `ReplayRecorder::set_memory_budget` truncates to an honest prefix (later commands/checkpoints drop, no gaps) and serializes `truncated:true`, which `parse` round-trips, `--replay-info` reports, and `--replay` advisories on stderr at load (nothing past the prefix is verified; non-monotonic streams warn that out-of-order entries never fire); once the whole recorded stream is consumed and every recorded checkpoint verifies without divergence, `--replay` prints a one-shot `replay_verified={"commands":N,"checkpoints":M}` line — a success signal for scripted runs instead of only an absence of failure; `--replay-exit` turns it into a verification command (exit 0 on verify, divergence/stall throws — a run whose tick or cursors stall 600 frames with pending work reports the stall rather than hanging) | QA/persistence/parity tests, replay smoke, `replay` unit tests (incl. leaf-diff path/value/absent-side cases and budget truncation/round-trip), `--replay-info` verified against a synthesized fixture incl. matching/stale/unparseable sidecars and error paths | `--replay-until` guards against a stalled tick: 600 frames without progress (paused campaign or exhausted command stream — pending commands key off the same tick, so a frozen tick can never unpause) reports the stall and exits instead of hanging; no interactive viewer yet |
| Asset registry/packages | IMPLEMENTED | Engine `asset_registry.cpp`, checksummed aliases/chunks | `engine_asset_cooker`, cooked validation evidence | Shipping marker forbids loose fallback; missing/corrupt content is an error |
| Asset cooker/compression | IMPLEMENTED BUT NEEDS POLISH | Engine asset cooker/texture cook/XPRESS+LZMS codecs, `StellarCooker`; **generic `scan_content` project mode** recursively indexes any content root into a project-namespaced package — no reviewed SC export manifests required | `engine_asset_cooker` (incl. scan-mode cook: namespaced package, texture cook, byte round-trip), image/GPU tests | HDR/mesh cooking absent; ~1,011 legitimate quality-gate fallbacks remain (LZMS cut stored bytes ~12%) |
| Texture streaming/residency | PARTIALLY IMPLEMENTED | Metadata/mip selection, bounded preparation and image/GPU caches | image preparation/GPU/cooked flare regressions | No virtual textures or adaptive device VRAM budget |
| Audio | PARTIALLY IMPLEMENTED | Engine native audio + Windows media decode; incremental pull streaming (`AudioStreamDecoder` + `open_audio_stream`: lazy Media Foundation reader bound on first `read`, normalized 48 kHz stereo F32 chunks, `rewind()` for looping, `AudioStreamError` typed failures; no 16 MiB source / 96 MiB decoded caps — bounded memory for arbitrary track length) feeding `AudioOutput::play_music` under the same 0.75 s bounded SDL queue; App director streams its music track through it (`stats().music_streaming`) with stream failures classified permanent vs recoverable device faults; device-fault recovery — device-level failures (unplugged/default-device/SDL stream faults) arm a bounded 5 s retry that rebuilds `AudioOutput`, restores persisted volumes, resubmits the decode job when needed, and resumes music via the normal `menu_ready` path (`stats().device_recoveries` counts rebuilds) | `native_audio` (chunked-decode PCM parity vs whole-file, rewind replay, oversized-source streaming, streamed-music queue + diagnostics, effect pan validation), `native_audio_director` (injected-fault recovery + decode-no-retry), settings tests | Whole-file decode remains for effects/voice; `play_effect(clip, pan, gain)` applies an equal-power stereo pan and a distance gain at queue time (RuntimeHost pans the bounce cue by the entity's camera-relative screen position and attenuates it by normalized distance from the view center — a corner impact plays at half gain); mixer buses remain open |
| Cosmetic animation | PARTIALLY IMPLEMENTED | Engine `FloatCurve`/`Timeline`/`AnimationPlayer` (eased keyframes, loop/pingpong, event markers, playhead lifecycle with speed/pause/seek/finish); scene documents author `animations` clips — `RuntimeHost` steps `AnimTimeline` components in sim time across x/y/w/h/vx/vy/opacity/rotation/spin/tintRGB channels, emits crossed events via `on_anim_event`, snapshots the playhead; celestial slow-spin/orbit/event interpolation unchanged | `economy_animation` (curve/timeline/player lifecycle), `engine_world` (clip codec + component + playhead save/load) | Skeletal/transform blending and LOD contracts open; channel ownership is per-track (a track sets its field each step) |
| Developer tools | IMPLEMENTED BUT NEEDS POLISH | Core developer indices/control; App diagnostics panel; QA host | developer index/coverage/host/fault tests | Not a complete editor, profiler or AI decision journal |
| Diagnostics/crash reports | IMPLEMENTED BUT NEEDS POLISH | Engine runtime diagnostics/log/bundle; App context and support service | `engine_runtime_diagnostics`, developer fault/support tests | Local/bounded/best-effort; forced kill/power loss not guaranteed, no automatic upload |
| Profiling/memory accounting | PARTIALLY IMPLEMENTED | Engine phase timing, queue/cache ledgers; `Window::draw` attributes the scene3d backend's resident texture/mesh/render-target VRAM plus the 2D image/text caches to MemoryTracker subsystems (`scene3d-textures`/`-meshes`/`-targets`, `ui-image-cache`, `ui-text-cache`) once per draw; World occupancy census — `World::estimated_memory_bytes()`/`EntityRegistry::memory_bytes()`/component-store `memory_bytes()` measure container capacities (sparse/dense vectors, hierarchy + legacy maps, codecs, membership) for `MemoryTracker::report`; the developer report registers the projected campaign world as `campaign-world-projection` and carries `worldProjection.estimatedMemoryBytes`; `EventHistory::estimated_memory_bytes()` counts inline deque storage plus per-event string/vector payloads — the diagnostic monitor reports it as `campaign-event-history` on the daily inspection cadence so chronicle growth over long campaigns is visible; `ReplayRecorder::estimated_memory_bytes()` counts command-payload growth and the client reports it as `replay-recorder` while a recorder is active; the client's `--record` recorder carries a 128 MiB `set_memory_budget` — past it the recorder keeps an honest prefix (no later commands/checkpoints claim fidelity) and serializes a `truncated` flag; the audio director reports bounded queue occupancy as `audio-queues` (queued music+voice bytes against their combined queue limit, refreshed per frame after `service()`) | diagnostics/scale tests; `native_scene3d_gpu` (attributed bytes equal renderer residency, all five subsystems present); `engine_diagnostics` (World + EventHistory census growth), `campaign_world_projection` (census footprint scales with entities) | No integrated GPU timeline; census is container-capacity occupancy, not allocator truth |
| Versioned Windows maintenance | IMPLEMENTED BUT NEEDS POLISH | Engine product version/lease; `installer/`; update scripts | `engine_windows_maintenance`, `engine_windows_maintenance_os` | Offline unsigned dev, exact-base whole-file updates; no remote updater or rollback after successful cleanup |
| Localization | PARTIALLY IMPLEMENTED | Engine `LocalizationTable`/`LocalizationService` (fallback chain, format/plural, reload); the startup screens, new-game setup (species details, galaxy selection, sandbox configuration), diplomacy, research (workspace plus the projection's purpose/benefit text, action reasons, domain tabs and notices), fleet, construction, inspection, economy (workspace plus the treasury view-model's cards, treasury/priority status, flow rows and notices), supply, colony-roster, colony-workspace, planetary-screen, system (including the small-body survey panel), battle, shipyard, overview, notification-feed, command-HUD, planet-globe, controlled-assets, settlement, galaxy-phenomena hover/label surfaces (secrecy labels preserved), the setup controller's preset labels and validation messages, and the startup/generation session status strings, pause menu, settings hub, and General/Audio/Video/Voice panels plus voice-cue subtitles consume `data/locale/en.json` | `localization`, `native_general_settings`, `native_audio_settings`, `native_video_settings`, `native_voice_settings`, `native_settings_hub`, `native_startup_workspace`, `native_new_game_workspace`, `native_diplomacy_workspace`, `native_research_workspace`, `native_research_controller`, `native_fleet_workspace`, `native_construction_workspace`, `native_inspection`, `native_economy_workspace`, `native_economy`, `native_logistics_workspace`, `native_colony_roster`, `native_colony_workspace`, `native_planetary_screen`, `native_system_workspace`, `native_small_body_panel`, `native_body_inspection`, `native_battle_workspace`, `native_shipyard_workspace`, `native_overview`, `native_notifications`, `native_command_hud`, `native_planet_globe`, `native_controlled_assets`, `native_settlement_workspace`, `native_missions`, `native_phenomena`, `native_new_campaign_setup`, `native_new_campaign_generation`, `native_startup_session`, `native_voice_playback`, `native_missions` | Startup chrome, setup wizard (including producer-side preset labels and validation messages), diplomacy/research/fleet/construction/inspection/economy/supply/colony-roster/colony/planetary/system/battle/shipyard/overview surfaces, small-body survey panel, phenomena hover cards (knowledge-gated labels resolve only at presentation), navigation/resource HUD chrome, notification feed (categories stay stable publisher IDs — only display labels translate), menus, and voice cue keys resolve through the catalog. **Second shipped locale**: `data/locale/de.json` (full 1418-key German table, `fallback: en`) — every `Data/locale/<id>.json` is staged by `stellar_runtime_data` and enumerated at startup; General Settings persists `GeneralPreferences::locale` with a LANGUAGE button cycling discovered ids, and saving rebuilds the live `LocalizationTable` in place (selected table + English fallback map — surfaces keep their borrowed pointers, so the switch applies mid-session). The `localization` test validates every shipped non-baseline catalog covers all baseline keys with matching placeholders; research card state badges and queue status resolve through the catalog too; test-only dead presentation code and a few HUD surfaces remain literal |
| Steam/platform services | PARTIALLY IMPLEMENTED | Engine `PlatformServices` facade + `NullPlatformBackend`; client reports backend status in support bundles | `package_platform` | No Steamworks backend yet; standalone behavior unchanged |
| Standalone engine shell | IMPLEMENTED BUT NEEDS POLISH | `app/engine_main.cpp` (`stellar-engine.exe`) links only `stellar_engine` + `stellar_native_platform` — windowed Vulkan tools host (Projects/Dashboard/Assets/Profiler/Localization sidebar), JobSystem demo work, Profiler aggregates/frame graph with `ProfileCapture` save/load + `compare_captures` A-vs-B delta table, `VirtualizedList` asset browser with image preview, `LocalizationTable` catalog inspector, diagnostics install. The Projects tool runs the complete game loop: create -> author (Scene tool: ~30-field entity+tilemap property list, animated/flipped/rotated/tilemap preview, drag-move, undo, duplicate, reorder) -> cook -> build -> test -> run -> package. **Projects tool** creates/opens `EngineProject` game projects (`project.stellar.json` manifest, scaffolded `packages/<id>` base package owning the project namespace, starter `src/main.cpp`), resolves their content packages through `PackageRegistry`, and re-roots the asset browser at the project's own content tree; **COOK** runs the asset cooker's generic `scan_content` mode on the JobSystem, producing `build/cooked` packages + validated manifest + cook report under the project directory, streaming per-asset progress (done/total) via `AssetCookOptions::progress`; **BUILD** configures/compiles the project's generated `CMakeLists.txt` + windowed `src/main.cpp` host (opens a `stellar::platform` Window, renders package status, Escape quits) against the exported `engine-sdk/` (headers, prebuilt libs, SDL3 runtime + default font, `StellarEngineSdk.cmake` consumer targets `stellar::engine`/`stellar::cooker`/`stellar::platform`/`stellar::audio`, staged by the `stellar-engine-sdk` target); **RUN** launches the built host with the project root as working directory; **IMPORT** copies a file into `packages/<id>/content/`; **NEW PACKAGE** scaffolds additional content packages under the project namespace with a dependency on the base; **EDITOR** launches `stellar-editor.exe --project <root>` (its document directory becomes `<project>/editor/`, the manifest seeds the project name); **TEST** smoke-runs the built host hidden for `--frames N` frames and reports pass/fail (30s timeout); **PACKAGE** assembles a distributable `dist/<name>/` folder (host exe + SDL3 runtime + default font + cooked `Content/` + source `packages/`); the starter host is a live ECS demo (`World` entities with Transform/Velocity components ticked per frame, rendered square) and validates cooked content via a local `AssetRegistry` probing `Content/` beside the exe (packaged layout) then `build/cooked/` (dev layout) — not the global mount, which would break loose-file font loading; scaffolded layout includes a `mods/` directory scanned after the project's protected namespace so mod packages cannot override the base | `engine_project` tests (scaffold layout incl. consumer CMakeLists, manifest round-trip, malformed rejection, discovery); `engine_asset_cooker` scan-mode coverage; verified end-to-end: scaffold → cook → configure against engine-sdk → build → run → windowed render | Tools host foundation: full create → open → browse → import → cook → build → run → edit → package loop works end-to-end and was verified on external projects via both the UI and the headless CLI (`--create/--cook/--build/--test/--package/--run`, plus `--project`/`--tool` deep-links); the Scene tool authors `editor/scene.json` with bounded whole-document undo/redo (engine `UndoHistory`, Ctrl+Z/Y + UNDO/REDO buttons) — (engine `SceneDocument`: named entities with position/extent/velocity/tint/optional content-relative `sprite` an integer `layer` draw order — higher layers render on top, stable within a layer; the tool exposes layer/parallax fields and preview/hit-test honor draw order — plus a per-entity `parallax` camera-scroll factor (0 pins to screen) a document `background` clear color, and a `text` field whose Label component draws a centered caption inside the entity rect (sprite/tint + text = buttons and HUD banners) — all honored by RuntimeHost and the tool preview; a document-level `gravity` (px/s^2) turns the host into a platformer sim — gravity-scaled entities integrate downward, rest on the floor instead of bouncing, up/W becomes a grounded jump impulse (grounded = resting on the world floor or a platform top), held-key vertical velocity only applies when gravity is off, and entities marked `solid` act as static platforms — downward movers land on their tops, movers stop against their sides and bump their heads underneath (all-direction blocking works in top-down scenes too, not just under gravity) (full AABB blocking for gravity-affected entities; `oneway` entities are landable from above but never side-block); solids with velocity are kinematic moving platforms — they integrate, stop at world bounds, and carry riders standing on their tops), sprite-sheet animation (`frames`/`fps`/`fcols` — frame index from accumulated sim time, deterministic under --fixed-hz; `fcols` slices grid sheets row-major, 0 = horizontal strip; `animLoop` false holds the last frame for one-shots), and sprite `rotation` (degrees about the rect center; tinted rects cannot rotate), and entity `ttl` — a sim-time countdown that self-destructs spawned effects (the starter demo's Space-fired spark expires after 3s if it never collides), and sprite `flipX`/`flipY` mirroring (new `Image` flip fields route through SDL_RenderTextureRotated even at 0 degrees), and a `visible` flag — hidden entities simulate and collide but are skipped by the renderer (ghosted in the Scene tool preview, which also now shows live frame animation/rotation/flip), and entity `parent` — name-keyed attachment resolved by `resolve_hierarchy` each sim step: the child keeps its authored offset and follows the resolved parent (chains resolve root-first, cycles/missing parents keep the last position), while the child's own world-space motion (velocity, collisions, game writes) re-bakes into its stored offset — verified end-to-end with a turret tracking a moving ship)) which the windowed starter spawns into its `World` — sprites decode once under the base package's content dir, the starter polls the document for changes so Scene-tool saves hot-reload into the running game, and the entity named `player` is driven by WASD/arrow keys (held-key velocity control fed by KeyPressed/KeyReleased events); `audio/bounce.wav|mp3` under the base package content decodes through `engine::audio::decode_audio_clip` and plays via `AudioOutput::play_effect` when the player bounces; `runtime_host.hpp` (`stellar::runtime`) is a ready-made windowed game host owning the SDL loop, package scan + namespace protection, ECS world, scene hot-reload, WASD player input, velocity/bounce integration, sprite rendering, audio and F5/F9 quicksave — games customize via on_update/on_event/on_status/on_draw callbacks, so a scaffolded main.cpp is ~20 lines; options support fixed-timestep simulation (`--fixed-hz`; frame-limited runs step once per frame so `--frames N --fixed-hz R --snapshot-out <path>` produces byte-identical world dumps across runs — verified deterministic), bounce clamps position to the frame, for deterministic ticks and CI smoke tests, P toggles a sim pause (rendering continues), `time_scale`/`--speed` scales sim dt, and game code can drive the loop through `request_quit`/`set_paused`/`set_scene` (project-relative scene switching — level loads); `--scene <path>`/`--width`/`--height`/`--fullscreen`/`--speed` adjust the scene, window and sim rate, F12 screenshots land in project `screenshots/`, `rng()` exposes a host-owned `DeterministicRandom` living on a world entity so its state snapshots with F5/F9 saves — same `--seed` reproduces the same stream, verified across runs; generated games install `RuntimeDiagnostics` (session log + crash minidumps under `logs/`), and `spawn_entity`/`destroy_entity`/`on_collision`/`on_collision_exit`/`on_land`/`on_tile_land` give dynamic entity spawning plus AABB contact enter/exit and touchdown events (once per landing, not per resting step; tile events identify map/cell/tile); `entities_in_rect`/`entities_in_radius` run world-space region queries over tracked entities (AoE, aggro, selection boxes — Hidden entities included, tilemap carriers excluded); `vfx()`/`spawn_emitter()` run the deterministic particle framework inside the sim step; a scene `vfx` field auto-attaches a named emitter (VfxRef component — re-anchored each step, stopped when the entity dies, re-attached on save restore), and a document-level `emitters` array declares full `EmitterDefinition`s (rate/lifetime/velocity range/spread/gravity/over-life scale+opacity+tint curves/max/LOD) in JSON so particles need no game code at all; `on_spawn` fires per spawned scene entity so games attach custom components keyed off `name`/`data`; `set_camera`/`camera_x/y/zoom`/`viewport_width/height` provide a world-space 2D view transform (entities draw at (world - camera) * zoom, off-screen entities culled, HUD stays screen-space) so generated games can scroll/zoom — the starter demo centers the camera on the player from on_update; `world_width`/`world_height` (`--world-w`/`--world-h`/`--move-speed`/`--jump`/`--save`) bound the built-in wall bounce independently of the window so camera games can build levels larger than one screen; `content_resolver.hpp` gives hosts a single content-path API over cooked manifests (packaged `Content/` then dev `build/cooked/`) and loose `packages/<id>/content/` files, with package-qualified overloads (`"pkg:path"` or explicit package arguments) so mod packages resolve through the same API; `scene_components.hpp` ships the canonical scene component set (Transform2D, Velocity2D, Extent2D, Tint, EntityName, SpriteRef, Layer, Parallax, Label, GravityScale, Solid, Anim, Rotation, Lifetime, Flip, Hidden, Oneway, NoBounce, UserData, Opacity, Spin, Parent, Tilemap) with `register_scene_components` codecs, `spawn_scene`, `find_entity_by_name`, and file-backed `save_world_to_file`/`load_world_from_file` (atomic write, safe false on corrupt/missing) so hosts no longer hand-roll ECS spawn/persistence; the starter registers codecs for its components (transform, velocity, extent, tint, name, sprite path) and quicksaves via `World::snapshot()` to `saves/quicksave.stw` on F5, restoring on F9 (player handle re-resolved by name so no stale `EntityId` survives); starter templates: `windowed` (platform + ECS demo) or `blank` (console); positional hierarchy is name-keyed only (no parent rotation/scale propagation, no cascade destroy — use World::set_parent for structural grouping); no custom scene-field extensibility or debugger attach; SDK is Windows/Release-only |
| Native engine editor | PARTIALLY IMPLEMENTED | `app/editor_main.cpp` + `app/editor_project.cpp` (`stellar-editor.exe`) links engine + core + platform — galaxy workspace running the full authoritative world-assembly pipeline (catalog, planetary bodies, stellar physics, small-body fields, orbit init, activity) on a JobSystem worker (250/500/1000/2500 sizes, seed regen), pannable/zoomable class-colored star map, system orbit workspace (Kepler ring polylines via `analytic_orbit_position`, companion hosts, small-body bands, time-scrubbed body markers/labels), **body workspace** (per-body satellite view: moon orbit rings via `planetary_satellite_orbit`, positions via `satellite_relative_position`, km-scale camera, Galaxy→System→Body view descent), click-select system inspector and body inspector (physical properties, environment, orbit/exposure, flags), searchable virtualized systems list, **system- and body-level annotation layer** (display-name/note/bookmark per record, body rows clickable to a body inspector context, bookmark markers on list rows and orbit markers) with bounded undo/redo over the whole project document (engine `UndoHistory`, Ctrl+Z/Y), editable project name, atomic JSON project save/load (`write_file_atomically`, schemaVersion 1, all-or-nothing parse, additive `bodyEdits`/`name` keys); succeeds the stranded 0.1.9 WPF editor on `work/stellar-engine-editor` (PR #326) | `editor_project` document tests (round-trip incl. body edits + project name + malformed rejection), `undo_history` engine tests; generation path covered by `galaxy_catalog`/planetary/orbit family tests; manual launch for UI | Authoring foundation: named Save-As writes `<name>.json` into the projects directory with a picker-based open; trait-override mutation landed (per-system/body anomaly, rare-resource and pre-warp-civilization flags cycle AUTO→YES→NO through the annotation layer — overrides win over generated records, re-apply deterministically after regeneration, round-trip through the codec, and ride the shared undo history); **multi-file projects** landed (a `<slug>/` directory holds `project.json` + `assets/`; the picker lists both flat and directory forms, saving upgrades flat→directory when an `assets/` folder exists and retires the stale `.json`, and embedded pngs are discovered by scan — the folder is the manifest — with the first image previewed in the inspector); **numeric mutation**: a body `radiusEarth` override edits generated `radius_earth` and an `orbitAu` override edits the stellar orbit radius, and a `massEarth` override edits generated `mass_earth` — all run through the same annotation layer (validated positive input, empty restores AUTO, undo rides history, codec round-trips), and every consumer reads the effective value (radius: detail rows, body-view disc sizing, camera fit; orbit: Kepler ring, day-phased position via a patched `AnalyticOrbit`, inspector + list AU values — moons excluded since they ride satellite orbits; habitable-zone tagging stays generated); **derived consistency**: surface gravity everywhere the editor reports it (inspector row + system body rows) recomputes as effective_mass / effective_radius² — the same relationship generation applies — so radius or mass overrides never leave a stale gravity reading; further property coverage open |
| Mods/accessibility/editor | IMPLEMENTED / PARTIALLY IMPLEMENTED foundations | Package system (`PackageRegistry`, `mods/` scan, `write_save_package_manifest`/`verify_save_package_manifest` save attestation), input/settings, Developer tools/import CLI, standalone editor | `package_platform` (incl. manifest attestation cases), editor + settings tests | Mod loading is content-only: namespaced package ids, priority-based overrides, semver dependency constraints and protected base namespaces resolve through `PackageRegistry::resolve`; world saves record the resolved load plan in a `<save>.packages.json` sidecar and `RuntimeHost` verifies it on F9/`load_world_from_file` restores — missing or version-mismatched packages log through `RuntimeDiagnostics` (report-only; loading proceeds). Executable plugins stay untrusted by design. Accessibility/editor remain partial — see the roadmap |

## Implementation records (newest first)

## Native missions panel integration (2026-09-24)

- **Purpose:** wire the previously tested-but-uninstantiated
  `NativeMissionView` (missions/settlement board over authoritative
  campaign state) into the native client behind a rail affordance.
- **Modules:** `app/native_client/native_ui_layout.hpp` (new
  `UiAction::Missions` + rail rect via `secondary(5)`, hit test and
  `hud_actions()` ring entry), `app/native_client/main.cpp`
  (routing/event/render/refresh + command dispatch),
  `app/native_client/native_missions.*` (view — unchanged),
  `data/locale/{en,de}.json` (`NAV_MISSIONS`).
- **Public interfaces:** the MISSIONS rail button toggles the panel
  through the same `route_navigation` dispatch as the other workspaces
  (mutually exclusive, closes on Map/Home/system entry/menu). The view's
  commands route to authoritative paths: FocusFleet →
  `fleet_controller_.select` + camera center, OpenColony → the existing
  overview entry (`enter_system` + body select), LandColony → planetary
  surface entry via `open_colony_from_system`, CollectOutpostFreight →
  surface entry plus the outpost freight preview through
  `outpost_freight_controller_.preview` (pause + UI gesture capture).
- **Data feed:** `refresh_missions` rebuilds the mission board, active
  settlement fleet views and owned-colony rows each frame the panel is
  open, gated on the session cache generation.
- **Keyboard contract:** the panel rings its actionable controls in
  (y,x) order (close, tabs, enabled site pagers, select-ship, colony
  View/Land/Collect — display-only mission cards and disabled controls
  stay out); Return/Space replay the matched press/release dispatch so
  the emitted commands are identical to pointer clicks, Escape releases
  a live ring before the host closes the panel, `map_hud_visible()`
  excludes it so the map focus groups cannot preempt its keys, and
  `focused_label`/`focused_bounds` feed `announce_focus` as Button
  announcements.
- **Tests:** `native_missions` (view behavior + ring order/activation/
  release/reset), `native_ui_layout` (18-item HUD ring incl. the new
  rect's hit-test round-trip and (y,x) ordering), `localization`, and
  the `--navigation-smoke` end-to-end pass (exclusive open, rail toggle,
  ring arming, Escape layering).
- **Localization:** panel chrome (title, tabs, pager/select/row buttons,
  empty state, focus labels, phase labels — roles reuse `FLEET_ROLE_*`)
  resolves through `set_localization` like the other workspaces.
- **Limitations:** the rail affordance renders a text glyph because no
  missions navigation art asset exists; producer-built data strings
  (card summaries, site details, freight reasons) remain authored
  English — `build_mission_board`/`colony_site_selection`/
  `build_owned_colony_rows` are catalog-free builders.

## Galaxy map framework + engine-shell Galaxy tool (2026-09-24)

- **Purpose:** the pending engine-level galaxy model for space-strategy
  consumers — a reusable, game-agnostic star chart (systems positioned
  in light-year space, lane links, fleet/colony/outpost/anomaly markers)
  with deterministic queries for rendering, selection and debugger
  surfaces. Closes the "Galaxy debugger pending an engine-level galaxy
  model" gap.
- **Modules:** `engine/include/stellar/engine/galaxy_map.hpp`,
  `engine/src/galaxy_map.cpp` (`GalaxyMap`, `GalaxySystem`,
  `GalaxyLane`, `GalaxyMarker`). Core projection:
  `core/include/stellar/core/galaxy_projection.hpp`,
  `core/src/galaxy_projection.cpp` (`project_galaxy_map`). Shell:
  GALAXY tab in `app/engine_main.cpp`.
- **Public interfaces:** `add_system/add_lane/add_marker` (caller ids,
  duplicate rejection), `remove_system` (drops incident lanes, detaches
  anchored markers), `set_lane_enabled`, `update_marker_position`,
  `set_marker_system`/`set_marker_destination`; ascending-id accessors;
  `neighbors`/`lanes_for` over a lazily rebuilt enabled-lane adjacency;
  `systems_in_radius`, `nearest_system` (id tie-break),
  `markers_in_system`, `markers_for_owner`, `distance_light_years`
  (3D euclidean); `find_route`/`route_length_light_years` — weighted
  Dijkstra over enabled lanes with lowest-id tie-breaks, so non-Core
  games get deterministic routing without `InterstellarLaneNetwork`;
  versioned `capture_state`/`restore_state` (v1) that rejects duplicate
  ids and dangling lane endpoints atomically.
  `project_galaxy_map(const FreshCampaignState&)` maps systems (name,
  position, stellar-class label, habitable/anomaly/rare/pre-warp tags),
  `InterstellarLaneNetwork::build()` lanes, colonies (anchored markers)
  and fleets (anchored or free-floating, destination preserved) —
  read-only, full-authority view; player-facing maps must apply
  observer knowledge rules first.
- **Consumers:** engine-shell GALAXY tab (deterministic synthetic
  chart — golden-angle spiral, two-nearest-neighbor lanes deduplicated,
  colony markers every fifth system, three fleet travellers hopping the
  lane graph; left-click selects the nearest system, right-click sets a
  route target with the `find_route` path highlighted, wheel zooms,
  STEP DAY/RUN/RESET; detail panel lists class/tags/lane
  neighbors/markers-in-system plus the weighted route). `project_galaxy_map` is the Core-side
  adapter a game/map surface feeds through — first consumer wiring
  beyond tests is the shell demo + adapter tests.
- **Tests:** `galaxy_map` (topology, deterministic ordering, adjacency
  laziness, spatial queries, markers, persistence round-trip +
  corruption rejection), `galaxy_projection` (mapping, lanes from the
  authoritative network, anchored/transit fleets, orphan colony skip,
  determinism), `framework_state_codec` (GalaxyMap JSON codec),
  `engine_shell_tool_galaxy` smoke test.
- **Save/performance impact:** map state is a presentation/projection
  model — `capture_state`/`restore_state` exist for embedders that
  persist charts; the Core projection allocates per call and is not on
  the per-frame path.
- **Limitations:** routing is lane-length-weighted only (no
  fuel/range/policy constraints — those stay with
  `InterstellarLaneNetwork`'s RoutePolicy/FuelRouteRequest); markers are
  render data — no gameplay rules; the projection is omniscient,
  observer filtering stays a consumer responsibility; no native-client
  consumer yet (the client's own star map predates the framework —
  migration is a separate decision).
- **Future reuse:** 4X/strategy star charts, jump-lane editors,
  sector/region overlays (tags + radius queries already support them),
  and the pending galaxy debugger tool in the standalone editor.

## Engine-shell Simulation tool + Core executor adoption (2026-09-23)

- **Purpose:** space-strategy specialization milestones 1/12 — make the
  simulation LOD machinery (a) authoritative in Stellar Continuum and
  (b) inspectable in the standalone engine shell as a genre tool.
- **Core adoption:** `GalaxySimulationStepCoordinator::advance` runs all
  12 strategic phases (economy → strategic_ai → automatic_orders →
  industry_allocation → construction → shipbuilding → legacy_research →
  exploration → freight → combat → colonization → economy_storage) as
  Active-tier `SimulationExecutor` tasks, dependency-chained to preserve
  the exact original ordering. Phase bodies capture the coordinator
  through bound lambdas — the coordinator's move constructor rebinds
  them so moved instances keep running phases against live context.
  `phase_executor()` exposes the executor for diagnostics; the
  executor's per-domain run statistics make phase activity observable.
- **Tool:** the SIMULATION tab in `stellar-engine.exe` hosts a live
  executor driving real framework state — three `Population` cohort
  settlements, a power `FlowNetwork` and a `LogisticsNetwork` freight
  route across Active/Normal/Background/Dormant tiers. STEP advances
  one tick, RUN auto-advances with the frame delta, WAKE exercises the
  event-wakeup path, TIER live-promotes/demotes the selected task; the
  panel shows tick, last-step report (eligible/ran/deferred/wakes/
  wall), tier counts and per-domain statistics. The COLONY tab is a
  working settlement designer over `engine::Colony` — a spec catalog
  (2 district types, 6 structures) builds against a real `Inventory`
  stockpile, districts/structures are inspected and toggled/demolished,
  and ADV 1D/30D reports jobs, housing, completions, outputs, upkeep/
  input shortfalls and utility balance. The ECONOMY tab inspects the
  economy framework: `EconomyCatalog` validation (with an injectable
  dangling recipe), `to_runtime_recipe` bridging into a live
  `ResourceNetwork` (producers, transfer lane, shortages) and an
  `analyze_economy` bottleneck table rolled up from real network state.
  The WARFARE tab is a theater inspector over `WarfareModel` — three
  ship classes (line/escort/transport), two hostile fleets and an
  interdictor on a strategic plane; rows select a fleet, ORDER cycles
  Hold/Move/Interdict/Retreat (Move steers at the opposing fleet),
  ENGAGE runs deterministic Lanchester resolution between the two
  combatants, STEP 5D/RUN advance movement + supply burn, and the panel
  shows per-fleet aggregate reports, cohort detail and whether the
  hostile fleet's position is interdiction-gated. The MISSIONS tab is a
  `MissionRuntime` debugger over a real `EventBus` — two mission
  definitions parsed from JSON (trigger conditions, stage timers,
  timeout stages, choice effects), FIRE EVENT cycles canned domain
  events through `handle_event` (including a non-matching one), rows
  select an instance for CHOOSE, STEP 10D/RUN advance stage timers, and
  SAVE/LOAD exercise `serialize()`/`restore()`; every
  `MissionEffectEvent` the runtime publishes lands in the effect log —
  the first consumer of the previously unwired `mission_graph`
  framework. The PHYSICS tab is a `PhysicsWorld` inspector — a
  drifting mover crossing a trigger volume (enter/exit events logged
  from `advance()`), layer-masked bodies, RAYCAST/SWEEP queries from
  the selected body to the target, STEP/RUN integration and a body
  table — the first consumer of the previously unwired `physics`
  framework.
  Generated game projects scaffold a `SimulationExecutor` demo — new
  games start with deterministic LOD scheduling wired into the loop —
  plus a persistence example: the starter host captures/restores
  `SimulationExecutor::State` through `framework_state_json` codecs into
  `RuntimeHost::save_data`/`load_data` slots (F5/F9 alongside the world
  quicksave, corrupt blobs fall back to fresh state), so the
  capture→serialize→restore contract is demonstrated for game-defined
  state rather than left as an undocumented gap.
- **Consumers/tests:** `campaign_coordinator` tests assert all 12 phase
  domains execute through the executor, empty campaigns advance safely,
  and scheduler state survives coordinator moves; the 28-case
  coordinator parity matrix, fresh/persistable/integrated-adaptive
  campaign parity and player17 v17 parity all pass unchanged (behavior
  identical — only the dispatch mechanism changed). The shell tools are
  UI inspectors over real engine state; `--frames N` renders N frames
  and exits, and ctest `engine_shell_tool_*` smoke-runs every tool's
  init+render path (all tools pass; the GALAXY tab added later brings
  the count to 16).
- **Save/performance impact:** executor state (tick, task elapsed/
  deferred/dirty/wake flags) is runtime-only — coordinator saves were
  already driven by authoritative domain state; `capture_state`/
  `restore_state` exist for embedders that persist the executor itself.
  Per-phase dispatch adds one executor advance per strategic step —
  negligible vs. phase work.
- **Limitations:** phases remain sequential (dependencies form one
  chain — no intra-step parallelism yet); the Simulation tool scenario
  is fixed/synthetic, not a save-loaded game.

## Event history framework (2026-09-23)

- **Purpose:** space-strategy specialization milestone 14 — the
  authoritative strategic chronicle: recorded happenings with
  observer-filtered queries, feeding chronicles and the M15 news
  substrate. See [HISTORY_FRAMEWORK.md](HISTORY_FRAMEWORK.md).
- **Engine APIs/ownership:** `HistoryEvent` (day, category, summary,
  actors, location, significance, visible_to privacy list, tags);
  `EventHistory` bounded store with monotonic ids, `query()`
  (category/tag/actor/time/significance + observer + limit),
  `feed(observer, since, min_significance)`, `prune_before` with a
  significance floor, and versioned `capture_state`/`restore_state`
  (codec in `framework_state_json.hpp`). Privacy is a query projection
  — records keep full truth for developer/omniscient views.
- **Core consumer:** `core/campaign_event_history` maps each
  authoritative `IntegratedAdaptiveCampaignStepResult` onto records —
  stable category vocabulary (construction.project, shipbuilding.ship,
  research.legacy/adaptive, exploration.\*, war.\*, colony.founded),
  involved-civilization visibility widened to observers that know the
  event's system (`widen_history_visibility` over
  `CivilizationKnowledgeState`), entity ids as tags, at_day = step
  end day. Discrete diplomatic journal entries join the same record
  path: after each step's diplomacy phase the runtime pulls the new
  journal tail via `DiplomacyState::history_events_since(watermark)`
  (monotonic event-id watermark baselined at runtime construction —
  restored journals never re-record), mapped to `diplomacy.<kind>`
  categories with the entry's own journal timestamp
  (campaign-milli-days → day), per-kind significance (war 0.95,
  agreements 0.8, routine 0.3–0.5), `civ:`/`system:` tags and the
  journal's authoritative `known_to_civilization_ids` audience —
  exempt from knowledge widening so excluded observers never learn
  identities; summaries are generic kind text (the journal's raw
  phrasing is internal). `IntegratedAdaptiveCampaignRuntime` owns an
  `EventHistory`
  and records every completed advance automatically — all callers
  (CampaignFrame, tests, tools) get the chronicle for free; exposed as
  `runtime().history()` / `frame().history()`. The chronicle serializes
  into the v17 save payload (`"EventHistory"`, PascalCase, strict
  ordered decode, absent = empty in older saves) for both player and
  developer saves.
- **Consumers/tests:** `history` tests — id assignment/lookup,
  every filter axis, observer privacy (public vs allow-listed),
  news feed, capacity bound, significance-aware pruning, bit-equal
  determinism, 200k-event scale, persistence round-trip + malformed
  rejection. `campaign_event_history` tests — category mapping for
  every step event type plus diplomatic journal kinds,
  actor/visibility/tags/at_day, journal-watermark accessors,
  widening-exempt diplomacy audience, feed privacy.
  `framework_state_codec` covers the JSON codec.
  **Presentation consumers (M15):** `seed_chronicle_notifications` in
  `native_notification_events` seeds the player notification feed from
  `history().feed()` at campaign admission — recorded dates/summaries
  survive load, category labels mapped, observer filtering delegated to
  the chronicle; located seeded reports carry `system_id` and render a
  VIEW SYSTEM action navigating via `enter_system`
  (`native_notification_events`/`native_notification` tests).
  `native_chronicle` adds the scrollable chronicle browser —
  `snapshot()` projects the observer-safe `query()` (feed()'s
  projection plus before_day) newest-first (4000-entry cap applied
  after optional category-domain, significance-floor,
  involved-actor, tag, time-window and search filters, true filtered
  total reported) and
  `NativeChronicleView` renders it as an overlay opened from the
  notification panel's CHRONICLE button with on-demand refresh, domain
  cycling, a significance cycle (0.0 → 0.3 → 0.5 → 0.7), an actor
  cycle (all intel → MINE → each civ appearing in the visible feed,
  names resolved from campaign state), a recency window (all → 30d →
  1y → 10y off a live campaign-day source) with ◀ ▶ window paging
  (`since_day` + `before_day` as a closed range — the whole timeline
  is pageable) and a header search field (case-insensitive substring
  over summary/category/tags, gated by `wants_text_input()`);
  clicking a located entry
  navigates the map to its system
  via `navigation()` → `enter_system` (the workspace re-applies the
  observation check) and single-foreign-actor entries expose a DIP
  action opening the diplomacy workspace on that contact; recorded
  reference tags render as clickable chips — clicking one applies
  `HistoryQuery::tag`'s exact-match focus ("everything fleet:12 did
  that we can see"), toggleable via the chip or an X focus button
  (`native_chronicle` tests). Admission
  seeding applies a fixed 0.35 report floor so high-volume trivia
  (damage ticks, detections) stays out of the transient feed. Voice
  announcement of the same step events already runs through
  `NativeGameplayVoiceBridge::route_events`.
- **Save/performance impact:** `EventHistory` field on the v17 payload
  (optional — absent in pre-chronicle saves); strict ordered decode
  with 1M-event bound and id-ordering validation on restore. Recording
  is O(1) append per emitted event — zero cost when a step emits none;
  queries are O(n) scans with sorted output.
- **Limitations:** opaque summary strings (no structured localization
  binding). Visibility widens at known-system granularity — no
  delayed/degraded intel or sensor-range falloff. Aggregate phase
  counters (sensor-contact recordings, diplomacy maintenance) are not
  discrete events and are not recorded. History capacity is fixed at
  100k records; retention (`maintain_chronicle`) prunes routine records
  older than 365 days once 90% full so capacity eviction cannot discard
  majors, but sustained record volume above ~0.35 significance still
  evicts oldest-first.

## Combined simulation scale benchmark (2026-09-23)

- **Purpose:** space-strategy specialization milestone 13 (partial) —
  prove the specialization frameworks compose deterministically under
  one scheduler.
- **What it exercises:** `combined_scale` ctest — 400 settlements each
  running `Population` + `Colony`, 8 regional `FlowNetwork` power
  grids (400 nodes), a `LogisticsNetwork` freight web (400 waypoints,
  400 routes, 2000 shipments), 200 `WarfareModel` fleets under move
  orders, 50 `StrategicMind` factions — six task domains at Active/
  Nearby/Normal/Background tiers through one `SimulationExecutor`.
- **Result:** 240 ticks, mean ~870µs/p50 ~560µs/p95 ~1.9ms on the dev
  machine; FNV-1a state checksums bit-identical across two full runs.
- **Limitations:** synthetic flat workload — real campaigns have
  heterogeneous settlement sizes and event spikes; parallel
  (advance_parallel) parity not exercised here (covered by
  simulation_scale_*); not yet wired to Core game state.

## Strategic warfare model (2026-09-23)

- **Purpose:** space-strategy specialization milestone 10 — fleets as
  cohort aggregates for thousands-of-fleets scale, with explicit
  deterministic engagement resolution and interdiction zones. See
  [WARFARE_FRAMEWORK.md](WARFARE_FRAMEWORK.md).
- **Engine APIs/ownership:** `ShipClass` templates, `ShipCohort`
  (class × count × condition × experience aggregates with weighted
  merge), `FleetState` (owner/position/order: Hold/Move/Interdict/
  Retreat). `report()` aggregates strength/speed/supply. `advance`
  moves fleets at slowest-cohort speed with arrival clamping.
  `interdicted()` gates hostile movement inside Interdict-order zones
  only — presence never blocks. `resolve()` is Lanchester-style
  attrition: aggregate attack distributed by hull share, net of
  per-ship defense; symmetric pre-resolution strengths.
- **Consumers/tests:** `warfare` tests — validation, cohort merge,
  movement/clamp, interdiction ownership and order gating, engagement
  attrition/destruction, defense absorption, bit-equal determinism,
  2000-fleet scale. **Core consumer:** `project_warfare_theater`
  (`campaign_warfare_projection`) reshapes authoritative
  fleets+systems into the engine theater — read-only, hulls
  preserved as cohorts — and `inspect_campaign_operations` emits
  `foreign_armed_presence` findings from it. Direct authority
  adoption still pending the DECISION_LOG graduation criteria.
- **Save/performance impact:** plain data with caller ids; O(fleets +
  cohorts) advance, O(cohorts²-free) aggregate resolve.
- **Limitations:** 2D plane, aggregate dps without range/arcs, no
  morale/retreat policy or supply settlement inside the model, no
  reinforcement semantics during engagement.

## Strategic AI decision machinery (2026-09-23)

- **Purpose:** space-strategy specialization milestone 9 — deterministic
  utility-based action selection for civilizations/factions; the
  decision machinery, not the policy. See
  [STRATEGIC_AI.md](STRATEGIC_AI.md).
- **Engine APIs/ownership:** `StrategicMind` — `UtilityAction`s
  (caller scorers + commit effects, cooldowns, weights, enable) grouped
  by domain; `decide(domain, day, min_utility, hysteresis)` evaluates
  in ascending id order, incumbent hysteresis prevents oscillation,
  argmax with smallest-id tie-break commits and journals a bounded
  `Decision` ring buffer (day/domain/action/utility/candidates/
  switched). Planning cadence is caller-owned — intended as
  `SimulationExecutor` domain tasks at different tiers.
- **Consumers/tests:** `strategic_ai` tests — argmax, id tie-break,
  hysteresis hold/switch, min-utility gate, cooldowns, domain
  independence, disabled actions, bounded journal, bit-equal
  determinism, 200-action × 5000-decision scale. **Core consumer:**
  `inspect_campaign_operations` runs a per-civ advisor spotlight each
  pass — the pass's own operational findings register as scored
  candidates (severity-weighted), `decide()` commits the argmax once,
  and the commit emits an `advisor_spotlight` record naming the top
  priority. A fresh mind per pass keeps the evaluation stateless —
  hysteresis/cooldowns are temporal semantics a one-shot ranking does
  not exercise. Core faction AI adoption still pending the
  DECISION_LOG graduation criteria.
- **Save/performance impact:** actions are code (re-register on load);
  persistent state is per-domain incumbent + per-action last-commit
  day. Decide cost is O(actions in domain).
- **Limitations:** flat scoring — no goal decomposition, opponent
  modeling or action budgets; fog-of-war/privacy filtering is the
  scorer's contract; journal is memory-only.

## Planetary habitability + terraforming (2026-09-23)

- **Purpose:** space-strategy specialization milestones 7–8 — adapter
  surface for authoritative Core planet state: physical environment
  description, species-relative habitability queries, staged
  terraforming projects. See
  [TERRAFORMING_FRAMEWORK.md](TERRAFORMING_FRAMEWORK.md).
- **Engine APIs/ownership:** `PlanetEnvironment` (temperature,
  atmosphere, gravity, water fraction, sorted environment tags);
  `HabitabilityProfile` (hard ranges + `tolerance` soft margins, water
  floor, required/forbidden tags); `evaluate_habitability` pure
  function → suitability/habitable/unmet reasons. `Terraforming` per
  planet owns the adapted environment; `TerraformProject` stages apply
  linear deltas over durations and discrete tag changes at completion;
  cancel preserves applied deltas; multi-stage completion per step.
- **Core adapter:** `stellar::core::to_engine_environment(PlanetaryBody)`
  (`core/planetary_adapter.*`) projects authoritative environment data
  into `PlanetEnvironment` — direct temperature/gravity, kPa→atm
  pressure, binary water-solvent presence, deterministic sorted tags
  (`atmosphere.*`, `solvent.*`, `high_radiation` at Core's >0.10
  threshold, `immersed`, `gas_giant`, `anomaly`, `cracked`,
  `rare_resource`, `native_civilization`). Core
  `assess_species_planet`/`species_environment` remains authoritative
  for campaign suitability; the engine evaluator serves reusable
  framework consumers (population needs, colony tags, terraforming).
- **Consumers/tests:** `planetary` tests — range scoring, soft margins,
  tag gates, water floor, determinism. `terraforming` tests — staged
  progression, interpolation, tag mutation, habitability improvement,
  cancel persistence, step-size invariance, determinism.
  `planetary_adapter` tests — field conversion, tag vocabulary,
  sorted-tag invariant, and `evaluate_habitability` over a projected
  real body.
- **Save/performance impact:** plain data everywhere; per-advance cost
  is O(stages completed), evaluation O(tags + params).
- **Limitations:** linear deltas only — no feedback loops, atmosphere
  composition, or cost/upkeep inside the framework; one active project
  per planet; tags are unvalidated free-form strings.

## Strategic logistics framework (2026-09-23)

- **Purpose:** space-strategy specialization milestone 6 — freight
  moving between settlements over explicit multi-leg routes with
  transit time and route capacity. See
  [LOGISTICS_FRAMEWORK.md](LOGISTICS_FRAMEWORK.md).
- **Engine APIs/ownership:** `LogisticsNetwork` — caller-owned waypoint
  nodes, `FreightRoute` explicit paths with per-leg transit days and
  in-flight capacity, `dispatch`/`cancel` shipment queue draining in
  ascending id order as capacity frees, `advance` delivering in
  (eta, id) order. Cargo accounting stays with the owner (deliveries
  are records, not inventory mutation); pathfinding stays with the
  caller's routing engine.
- **Consumers/tests:** `logistics` tests — route validation, transit
  timing, capacity queueing, disabled-route hold/resume, cancellation
  bounds, delivery ordering, bit-equal determinism, 20k-shipment scale.
  **Core consumer:** `campaign_logistics_projection` — the authoritative
  `HomeSystemLogisticsNetwork` (links, daily-flow allocations) restores
  into a `LogisticsNetwork` snapshot where route capacity/in-flight are
  committed tonnage (`per_day × transit_days`), so
  `route_utilization()` reports per-corridor saturation; campaign
  diagnostics emits `logistics_link_saturated` findings for links at
  ≥99.9% committed capacity — per-link bottleneck detail the
  colony-level snapshots cannot express. `campaign_logistics_projection`
  tests cover utilization math, over-commit (>1.0), disabled and
  zero-capacity links, the in-transit manifest, and post-projection
  `advance` delivery ordering. Direct authority adoption still pending
  the DECISION_LOG graduation criteria.
- **Save/performance impact:** routes/queue/transit are plain
  caller-id data; `now()` plus the manifests serialize directly.
  Per-advance cost is O(queue + transit).
- **Limitations:** no pathfinding/rerouting; no per-leg positions,
  convoy composition, edge loss/latency, or interdiction hooks;
  `remove_route` loses in-flight cargo.

## Infrastructure flow networks (2026-09-23)

- **Purpose:** space-strategy specialization milestone 5 — reusable
  single-resource distribution graphs (power/water/data/freight) with
  dirty-tracked topology and flow diagnostics. See
  [INFRASTRUCTURE_FRAMEWORK.md](INFRASTRUCTURE_FRAMEWORK.md).
- **Engine APIs/ownership:** `FlowNetwork` — caller-supplied node/edge
  ids, per-node supply/demand/storage rates, directed capacity edges
  (duplicate direction pairs rejected), topology mutations dirty a lazy
  union-find `components()` cache, rate/storage updates do not.
  `advance(days)` is a three-pass deterministic transport: local
  serve+storage release, greedy edge rebalancing in ascending id order,
  surplus storage absorption; `FlowAdvanceResult` reports totals plus
  nonzero per-node unmet and per-edge flow.
- **Consumers/tests:** `flow_network` tests — local serve, edge
  transfer, capacity saturation, storage buffering, islanded demand,
  component caching on enable/disable, documented no-same-tick-chaining
  behavior, bit-equal determinism, 10k-node/20k-edge scale. Colony
  utility pools and Core grids adoption pending.
- **Save/performance impact:** nodes/edges are plain caller-id data;
  component cache is derived, never persisted. Per-advance cost is
  O(nodes + edges).
- **Limitations:** greedy single-pass transport, not max-flow — flow
  doesn't chain through relays within one step; no shared corridor
  capacity across resources, edge latency or loss factors; storage is
  per-node.

## Colony/settlement structural framework (2026-09-23)

- **Purpose:** space-strategy specialization milestone 4 — the reusable
  substrate a settlement is built from (districts host structures;
  structures draw utilities, offer jobs/housing, consume inputs and
  produce outputs). See [COLONY_FRAMEWORK.md](COLONY_FRAMEWORK.md).
- **Engine APIs/ownership:** `DistrictSpec`/`StructureSpec` (data-driven
  templates: slots, build cost/time, utility demand+supply, upkeep,
  inputs/outputs per day, jobs, housing, condition decay/repair,
  `required_tags`); `Colony` (caller-supplied instance ids, district
  slot enforcement, construction inside `advance`, shared utility
  satisfaction pools, uniform workforce scaling, `Inventory`-backed
  upkeep/input draws and output depositing, condition decay/repair,
  district gating of hosted structures). `ColonyDelta` reports jobs,
  housing, outputs, shortfalls, utility balance and completions.
- **Consumers/tests:** `colony` tests — spec validation, district slots,
  requirement tags, construction timing, utility/workforce/input gating,
  condition repair, bit-equal determinism, 1000-colony × 30-tick scale
  (~4µs per colony-tick). Core `Colony`/`surface_economy` remain
  authoritative; adoption pending.
- **Save/performance impact:** specs and instances are plain data with
  caller-supplied ids — serialize directly; per-advance cost is
  structures × elapsed-steps with two sorted passes.
- **Limitations:** utility supply ignores `operating` (no supply →
  operating circularity; disable structures to cut supply); mid-step
  completions produce for the whole step; no spatial adjacency or
  upgrade chains; workforce is one undifferentiated pool.

## Population cohort framework (2026-09-23)

- **Purpose:** space-strategy specialization milestone 3 — aggregate
  demographics for colonies/civilizations without per-citizen entities.
  See [POPULATION_FRAMEWORK.md](POPULATION_FRAMEWORK.md).
- **Engine APIs/ownership:** `DemographicProfile` (data-driven
  species/culture template: fertility/mortality/lifespan, consumption,
  workforce participation, migration tendency, education rate),
  `CohortKey`/`PopulationCohort` (species × culture × occupation ×
  education × wealth aggregate with health/happiness/morale, housing,
  employment, environment suitability, 8-bucket age distribution),
  `Population` (cohort set) advancing under `SettlementConditions`:
  births, attributed deaths (starvation/environmental/overcrowding/
  insecurity/unhoused, healthcare-relieved), aging, quality drift with
  a hard food cap on happiness, equal-share employment, education-level
  cohort migration, explicit `take_emigrants`/`take_immigrants` slices
  and `migration_pressure` reporting.
- **Consumers/tests:** `population` tests — growth/decline accounting,
  starvation and environmental mortality, cohort merge, workforce and
  unemployment, education progression, aging, migration slices,
  determinism, 17M-headcount scale run.
  `population_habitability.hpp` resolves `environment_needs` into a
  `HabitabilityProfile` (hard `required_tags`, open ranges) evaluated
  through `evaluate_habitability`, producing the suitability scalar +
  "requires:<tag>" unmet reasons callers feed into
  `SettlementConditions`/`Cohort::environment_suitability`;
  `population_habitability` tests cover satisfied/missing/no-need
  environments and profile passthrough.
  **Core consumer:** `campaign_population_projection` — each colony
  projects into a single-cohort `Population` plus the
  `SettlementConditions` its authoritative systems already compute
  (sustenance-capacity chain → food/goods/housing ratios and
  overcrowding, `colony_labor` → employment, `stability` → wellbeing,
  `colony_habitat_support` → natural habitability, species biology →
  demographic template). Campaign diagnostics emits `population_unrest`
  when `migration_pressure()` — a const query, no competing growth
  authority — reports ≥10%/year emigration pressure. Depopulated
  colonies project neutral fit (no evaluable habitat); unknown species
  still propagates as corrupt state. `campaign_population_projection`
  tests cover the condition mapping, threshold crossing, automation
  relief, species-template carry, invariant propagation, and the
  empty-colony edge. Direct authority adoption still pending the
  DECISION_LOG graduation criteria.
- **Save/performance impact:** plain data state, serializes directly;
  cost scales with cohort count (hundreds), not headcount.
- **Limitations:** uniform (not per-bucket) mortality draw; equal-share
  job allocation; culture/wealth mobility beyond education TBD.

## Generic resource + economy catalog framework (2026-09-23)

- **Purpose:** space-strategy specialization milestone 2 — a reusable,
  data-driven economy definition layer over the runtime
  `ResourceNetwork`. See [ECONOMY_FRAMEWORK.md](ECONOMY_FRAMEWORK.md).
- **Engine APIs/ownership:** `ResourceSpec` (id, localization key,
  category, physical flag, mass/volume, storage class, perishability,
  transportability, base valuation, substitution group, tags, unit) and
  `RecipeSpec` (inputs/outputs/catalysts/byproducts, labor, energy,
  facility tags, duration, efficiency, substitution). `EconomyCatalog`
  validates without throwing — malformed/duplicate ids, unknown
  references, nonpositive quantities, zero-output recipes, dependency
  cycles and unreachable production chains (fixpoint over chain
  categories). `EconomyGraph` answers producers/consumers/downstream/
  upstream/unproducible and exposes edges for editor graphs.
  `analyze_economy` turns demand + caller observations into per-resource
  bottleneck/unmet/reserve/utilization/import-dependence diagnostics.
  `to_runtime_recipe` bridges specs into `ResourceNetwork` recipes.
- **Consumers/tests:** `economy_catalog` tests — valid catalog clean,
  graph closures, every validation class, diagnostics math, live
  `ResourceNetwork` production through the bridge, deterministic issue
  ordering. **Core consumer:** `sustenance_economy_catalog()` +
  `analyze_colony_sustenance` (`campaign_economy_projection`) run
  `analyze_economy` over authoritative colony sustenance state —
  `inspect_campaign_operations` emits `sustenance_shortfall`/
  `power_shortfall` findings from it. Editor economy tool and direct
  authority adoption still pending the DECISION_LOG graduation
  criteria.
- **Save/performance impact:** pure data + derived immutable graph —
  catalog contents serialize through the package/data layer, nothing
  runtime-persistent. Validation and fixpoint are catalog-scale
  (resources × recipes), not per-tick.
- **Limitations:** substitution groups validated but not resolved at
  runtime; catalysts/labor/facility tags are metadata until the colony
  framework gates on them; diagnostics consume caller-supplied
  observations (no automatic rollup yet).

## Core campaign economy projection + sustenance diagnostics (2026-09-24)

- **Purpose:** make the engine economy-analysis framework reachable over
  authoritative campaign state without creating a second economy
  authority. Core's macro colony economy (surface production,
  sustenance capacity, reserves) stays authoritative; the projection
  reshapes that state into `ResourceObservation`s so `analyze_economy`
  produces per-resource bottleneck/reserve/unmet diagnostics.
- **Core adapter:** `core/campaign_economy_projection.*` —
  `sustenance_economy_catalog()` builds a small `EconomyCatalog`
  (`res.food`, `res.water`, `res.power`); `colony_resource_observations`
  maps `surface_colony_output`, `surface_sustenance_projection`,
  `colony_sustenance_capacity` and `preview_colony_reserves` (clamped
  reserve semantics preserved) into observations; the power axis maps
  installed grid supply vs staffed building demand, with
  `stored_power_days × demand` as stock so `reserve_days` equals Core's
  authoritative storage days exactly.
  `analyze_colony_sustenance` runs them through `analyze_economy` sorted
  deterministically by resource id. No engine `Population`/`Colony`
  objects are instantiated for campaign authority.
- **Consumers/tests:** `inspect_campaign_operations` emits
  `sustenance_shortfall` findings for food/water and `power_shortfall`
  findings for brownouts (entity/civilization/system identity +
  demand/supply/reserve values) for colonies whose demand outruns
  installed supply over a 30-day horizon — consumed by the
  campaign diagnostic monitor, developer diagnostic report and QA host.
  `campaign_economy_projection` tests — healthy/hostile bodies,
  reserve-day parity with `preview_colony_reserves`, building
  contribution, power-grid deficit + `stored_power_days` reserve parity,
  determinism, legacy body-less colonies, and the
  operations-finding path; `campaign_diagnostics` tests updated for the
  new finding class (seeded worlds legitimately contain under-provisioned
  colonies).
- **Save/performance impact:** read-only projection — zero new
  persistent state; scans colonies once per daily diagnostics check with
  a cached static catalog (O(colonies), tiny resource set).
- **Limitations:** sustenance + grid power only — broader Core resource
  flows (industry, trade goods, freight contents) are not projected;
  power has no generation-headroom analog so `capacity_per_day`/
  utilization stay 0; findings describe shortfalls, they do not
  prescribe fixes; engine-side economy framework adoption into Core
  authority remains future work.

## Core campaign warfare projection + presence diagnostics (2026-09-24)

- **Purpose:** make the engine strategic-warfare framework reachable
  over authoritative campaign fleets without a second combat authority.
  Core combat resolution, military orders and transit stay
  authoritative; `core/campaign_warfare_projection.*` projects fleets +
  `CombatProfileDefinition`s into a `WarfareModel` theater so
  `FleetReport` aggregates and `resolve()` Lanchester previews serve
  diagnostics and tooling on copies — never mutating campaign state.
- **Core adapter:** `project_warfare_theater(fleets, systems)` — one
  fleet → one engine fleet with a single-ship cohort; ShipClass per
  combat profile (attack = `sustained_damage_per_day`, hull =
  shields+armor+hull, defense/interdiction = 0 — no Core analog;
  interdiction is tactical-only). Order mapping: retreating → Retreat,
  transit/destination → Move at the destination system, Attack +
  resolvable target → Move at the target fleet, else Hold. Cohort
  condition = live hull fraction; experience = 0.15/battle fought
  (documented display scale).
- **Consumers/tests:** `inspect_campaign_operations` emits
  `foreign_armed_presence` findings when an armed fleet is stationed in
  a system whose colonies it does not own (projected attack/hull/speed
  in `values`); consumed by the diagnostic monitor, developer report
  and QA host. `campaign_warfare_projection` tests — stat fidelity,
  order mapping precedence, unarmed/inactive handling, deterministic
  `resolve` previews that leave campaign state untouched, and the
  consumer path (only stationed armed foreign fleets flagged).
- **Save/performance impact:** read-only projection — zero persistent
  state; theater build is O(fleets + systems) once per daily
  diagnostics pass.
- **Limitations:** one ship per cohort (Core fleets are single
  vessels — no squadron scale); class speed binds per profile id;
  engagement previews are what-if math, not committed combat outcomes;
  no strategic interdiction radius exists in Core to project.

## Core campaign colony projection + degraded-structure diagnostics (2026-09-24)

- **Purpose:** make the engine Colony settlement framework (M4:
  spec/structure/utility substrate) reachable over authoritative
  campaign colonies without a second settlement authority. Core surface
  economy (`surface_economy.hpp`) stays authoritative;
  `core/campaign_colony_projection.*` reshapes `SurfaceBuilding`s +
  `surface_building_catalog()` definitions + the authoritative
  power/staffing allocation into an `engine::Colony` so aggregate
  structure/utility/workforce accounting serves diagnostics and tooling
  on a copy — never mutating campaign state.
- **Core adapter:** `project_colony_settlement(colony)` — one
  `SurfaceBuilding` → one standalone `Structure` (Core has no
  districts); spec `surface.<type_id>` synthesized from the catalog row
  (power → `power` utility supply/demand, workforce → jobs, housing,
  per-day outputs, `credits` upkeep, `industry` build cost);
  unknown type ids fall back to a bare `surface.unknown` spec. Flags
  map exactly (`is_complete`/`is_enabled`/`condition`); in-progress
  buildings carry `construction_remaining` in Core industry units;
  `operating` mirrors the authoritative `surface_colony_output()`
  powered set (zero when the allocator cannot run on unknown types);
  `standalone_slots` = `surface_building_capacity`, floored to the
  structure count for hub-less colonies (engine 0 == unlimited).
- **Consumers/tests:** `inspect_campaign_operations` emits
  `degraded_structures` findings — complete+enabled structures at or
  below `minimum_operational_condition` silently contribute nothing to
  surface output; the finding reports count and worst condition per
  colony. The same pass gained authoritative logistics findings:
  `logistics_strained`/`logistics_critical` per colony the economy
  logistics model rates under-covered (import requirement + coverage
  ratio in `values`; severity stays Warning — Critical is reserved for
  invariants) and `freight_corridor_gap` when a civilization's
  external colonies import support no represented corridor carries
  (skipped for homebound civs — coverage is only meaningful with
  external systems), `logistics_link_saturated` for home-system
  corridors at ≥99.9% committed capacity via the
  `campaign_logistics_projection` utilization view,
  `population_unrest` for colonies whose projected cohort shows
  ≥10%/year emigration pressure via `campaign_population_projection`,
  `advisor_spotlight` naming each civ's single top-priority finding
  (the pass's findings scored through `StrategicMind`'s argmax commit),
  and `treasury_arrears`/`treasury_depleted` from
  the authoritative `assess_treasury` — all previously surfaced only
  in workspace view-models or a player-scoped voice event, never in
  developer diagnostics. Corrupt classifier inputs (non-finite/negative
  credits or arrears) are skipped rather than thrown; `inspect_campaign_invariants`
  now also covers colony `stability`/`stored_extracted_materials`,
  building `condition`/`stored_power_days`, and economy
  `operating_arrears` so such corruption is caught as a Critical
  invariant finding instead of escaping the operations pass.
  The same hardening applies to the reference surface the
  authoritative queries validate: the orphaned-body check is now
  system-scoped (a body id that exists in another system is still
  unresolvable for the colony), populated colonies flag
  `unknown_species`, surface buildings flag `unknown_building_type`,
  negative colony ids flag `invalid_nonnegative_value`, and fleet
  `strategic_speed` must be finite positive (`invalid_positive_value`
  — the warfare projection's class definitions reject `<= 0`). The
  remaining state surfaces are covered too: civilization
  `home_system_id` (`orphaned_home` — the loader already enforces it,
  the invariant catches hand-built/corrupt in-memory state),
  and civilization `species_id` must resolve in the species
  catalog (`unknown_species` — the loader's `require_species`
  rejects blank and uncatalogued ids), and leadership entries flag
  `invalid_character` when office, character id, display name,
  voice profile or portrait metadata violate the persisted
  blank/UTF-16-length bounds,
  research/construction/shipyard rows (duplicate civilization ids,
  `orphaned_research`/`orphaned_construction`/`orphaned_shipyard`,
  non-negative progress/authorization/reserved-population fields,
  `unknown_technology` for uncatalogued completed or active research,
  `queue_overflow` past `maximum_queued_construction_projects`,
  shipyard `unknown_species` and `orphaned_colony` reservation refs,
  plus the capture-time shipyard accounting checks: positive
  `next_order_sequence`, `inconsistent_build` for active accounting
  without a design, `unknown_ship_design` when an unknown design
  carries refund metadata, build progress bounded by the design's
  industry cost, `invalid_order_id`/`inconsistent_sequence` for
  malformed or non-monotonic order identities, and
  `invalid_reservation` via the guarded population-safety
  validator). Economy DTO bounds also flag non-negative
  `last_research_spending_per_day` and both funding fractions.
  Construction project collections mirror `validate_construction`:
  `unknown_project` for uncatalogued active/completed/queued ids,
  `inconsistent_project` for active-without-project state and
  active/queued/completed overlap, `duplicate_id` per collection,
  `out_of_range` for progress beyond the catalog build cost, and
  non-negative queued authorization credits.
  Fleet state is covered end-to-end: transit/settlement/reconnaissance
  progress, leg range, fuel and cargo capacities, embarked-species
  catalog refs, destination/settlement body refs, reconnaissance
  system refs, freight outpost/home-colony refs, and the combat
  block's shield/armor/hull/cooldown/retreat magnitudes plus
  `orphaned_target`/`orphaned_defense`/`orphaned_disengagement`
  order refs, plus local-transit vector finiteness, `sensor_range`,
  `mission_order_revision`, `orphaned_route_hop` for planned-route
  systems that no longer exist, and `route_overflow` past the 132-entry
  persisted path bound. Persisted observer/intelligence state is
  covered too: knowledge snapshot entries flag `orphaned_observer`/
  `orphaned_known_system`/`orphaned_known_civilization`, per-civ survey
  rows flag `orphaned_known_system` plus defensive level-range/
  progress-bound/`inconsistent_survey` checks (the writers and restore
  path clamp and normalize, so violations are memory/hand-built
  corruption), and `galactic_core_observers` flag `orphaned_observer`,
  combat
  intelligence flags `orphaned_observer`/`orphaned_observed_fleet`
  plus `observation_overflow` past the 2048 per-observer bound and
  non-negative power/day, and an active massive encounter flags
  `orphaned_encounter`/`orphaned_encounter_vessel` for absent
  systems and vessel-bound fleets. Planetary bodies now mirror
  `validate_planetary_body`'s authoritative bounds: strictly-positive
  radius/mass (`invalid_positive_value` — `nonnegative` accepted 0),
  `orbit_index`, `invalid_body_parent` for parentless Moons and
  parented primaries, eccentricity outside [0,1) and inclination
  outside [0,180] as `out_of_range`; `validate_environment` bounds
  (gravity/pressure non-negative, temperature above absolute zero,
  radiation hazard in [0,1]) and `validate_stellar_planet` exposure
  fields (finite-positive orbit AU, non-negative flux/approach);
  top-level state adds `stellar_activity_day`, galactic-core
  position/exclusion radius and `engulfed_planets`. The stellar
  catalog checks mirror the persistence validators too: stellar
  class range, companion ordering and the Sol singleton
  (`invalid_kind`/`invalid_companion`), region index bounds,
  guarded `validate_stellar_physics`/`validate_stellar_orbits`/
  `validate_stellar_activity` per system (`invalid_stellar`),
  plus a guarded `capture_galaxy_persistence_metadata` umbrella that
  flags `invalid_generation_metadata` when the generation record
  disagrees with the galaxy (seed, system count, configuration,
  phenomena or core-landmark agreement). Developer provenance mirrors
  `validate_developer_simulation_state` (fixed-tick speed/backlog
  consistency → `invalid_simulation`) and `validate_developer_coverage`
  (coverage version, forced-system refs, required stellar types →
  `invalid_coverage`). Diplomatic state — which lives on the campaign
  runtime, outside `FreshCampaignState` — is covered by the companion
  `inspect_diplomacy_invariants(diplomacy, world, …)` pass: it mirrors
  `DiplomacySnapshotInvariantValidator` as a guarded umbrella
  (`invalid_state`) and adds the campaign-entity refs the snapshot
  validator cannot see (`orphaned_observer`/`orphaned_civilization`/
  `orphaned_system` across contacts, relationships, access grants,
  claims/responses, agreements, proposals and history). Adaptive
  research state is covered by `inspect_research_invariants(research,
  runtime, world, …)`: captures through
  `AdaptiveResearchCampaignSnapshotCodec` and replays the codec's
  save-path `restore` validation so in-memory corruption surfaces before
  the next save/load cycle, plus `orphaned_civilization` for rows the
  codec maps to absent empires. A fourth pass,
  `inspect_continuation_invariants(runtime, …)`, captures
  `runtime.continuation()` — the strategic coordinator's cached plans
  and the diplomacy schedule — and replays
  `validate_campaign_runtime_continuation`, the same validator the
  save/restore path applies, emitting `invalid_continuation` plus a
  precise `orphaned_plan` when a cached plan references an absent or
  non-planning civilization. The monitor,
  developer report and QA host compose all four passes.
  `CampaignFrame::advance` also retains a `CampaignAdvanceFailure`
  on the frame when an authoritative step throws mid-step: every
  `runtime->advance` call passes the `IntegratedAdaptiveCampaignAdvanceTrace`,
  and `campaign_advance_failure_phase` attributes the throw to the first
  phase whose output is absent (`core`/`sensor`/`research`/`diplomacy`,
  `chronicle` when all four completed but post-step recording threw;
  `tactical` for the combat route and `stellar_activity` for the
  weather-clock advance) plus the exception message —
  `frame.last_advance_failure()` clears on the next attempt; the QA
  host records it as a `simulation/step_failure` critical finding before
  attempting the critical checkpoint, and the native client routes a
  developer-session step throw through
  `CampaignDiagnosticMonitor::observe_advance_failure` (critical record
  under the same first-critical capture as `observe`) into the fault
  capture's pause + diagnostic-bundle path instead of crashing — player
  sessions still throw. `campaign_frame_parity`'s
  StrategicFailure contract row asserts the record exists with an
  attributed phase on a real throw, and the moved-owner row asserts a
  successful advance leaves none,
  cross-system orbit bindings via `validate_stellar_orbit_catalog`
  (`invalid_orbit_binding`), `validate_central_black_hole` on the
  galactic-core metadata (`invalid_black_hole`), galactic-core
  metadata position/exclusion-radius magnitudes, the persisted
  activity-clock bound (`invalid_activity_clock`), and the whole
  small-body catalog (`invalid_small_body` via
  `validate_small_body_catalog` — region/profile bounds,
  composition sums, ids, parent refs and cycles). Loader-enforced
  enum bounds flag `invalid_kind` for fleet `transit_phase` and
  economy `industry_priority`. Semantic bounds are flagged as `out_of_range`:
  `transit_progress` beyond 1 (the loader enforces [0,1]),
  `fuel_remaining_light_years` beyond `fuel_capacity_light_years`
  (refuel caps at capacity×service), `cargo_materials` beyond
  `cargo_material_capacity` (freight loads clamp at capacity), and
  the economy `last_research_funding_fraction`/
  `last_base_operations_funding_fraction` beyond 1 (authoritative
  writes clamp to [0,1]). The pass now mirrors the remaining
  `validate_galaxy_references` surface: settlement `kind` validity
  (`invalid_kind`), `surface_hub_level` outside [0,3]
  (`out_of_range`), buildings past `surface_building_capacity`
  (`capacity_overflow`), buildings without an exact body
  (`missing_surface_body`) or on a body without solid ground
  (`invalid_surface_site`), and `missing_economy` when a civilization
  runs surface construction with no authoritative economy. Fleets
  mirror the same validator: strictly-positive leg range and fuel
  capacity, `settlement_days_completed`/`reconnaissance_days_completed`
  bounded by `establishment_days`/`scout_reconnaissance_days`,
  `unknown_ship_design`/`incompatible_design` catalog checks,
  `invalid_loadout`/`invalid_vessel`/`inconsistent_vessel` for
  tactical state, `invalid_freight` for freight state on
  non-logistics fleets or wrong-kind/foreign-owner targets,
  `inconsistent_route` for waypoints without a destination or
  routes not ending at the mission destination, `invalid_order` for
  civilian hold/return orders on military fleets,
  `inconsistent_order` for work progress without a matching order,
  and `invalid_settlement`/`invalid_reconnaissance`/
  `invalid_destination` for role- or system-mismatched work sites.
  Combat intelligence additionally flags `duplicate_id` observation
  pairs, `invalid_evidence` for blank evidence, and the 4096-entry
  persisted total bound via `observation_overflow`. Per-building
  checks mirror `validate_surface_construction`: `invalid_slot` for
  out-of-range or duplicate slot indices (the canonical allocator
  throws), `invalid_placement` via the authoritative
  `surface_placement_error` (boundary, hub clearance, slope,
  overlap — only fully-valid buildings join the overlap set),
  `invalid_positive_value` for missing building ids,
  `inconsistent_progress` when `is_complete` disagrees with
  `industry_progress` versus the catalog cost, `inconsistent_upgrade`
  for pending-upgrade/progress mismatches or hub expansion past the
  level cap, and `out_of_range` for `operating_priority` outside
  [0,1], `condition` above 1, `industry_progress` above the build
  cost, and `stored_power_days` above the catalog storage.
  Resource outposts bound `stored_extracted_materials` against the
  represented capacity and `remaining + stored` against the
  represented deposit via `resource_outpost_snapshot` (guarded —
  unresolved inputs already carry their own findings). The active
  massive encounter is validated on a clone through
  `validate_campaign_massive_encounter` (`invalid_encounter`) — the
  canonical validator mutates, so diagnostics stay read-only while
  still surfacing deep battle-state corruption. Every
  throwing call in the operations pass is now wrapped — sustenance
  analysis, the warfare theater projection, lane-network construction
  and reach assessment, the logistics snapshot/coverage/home-network
  queries, credit flow and the population projection — so a corrupt
  entity degrades to "invariant finding + skipped entity" instead of
  discarding the entire pass's findings (the monitor collects
  invariants and operations in one batch, so an escape previously
  lost both). Both passes also surface truncation honestly: when the
  `maximum_findings` bound is reached mid-scan a `findings_truncated`
  marker is appended (Critical for invariants with the dropped count,
  Warning for operations), so a capped pass cannot hide findings
  silently — callers may see `maximum`+1 records.
  `campaign_colony_projection`
  tests — spec synthesis, flag fidelity, remaining-industry accounting,
  powered-set operating flags, unknown-type fallback, hub-less capacity
  floor, and the consumer paths (only worn/under-covered colonies
  flagged; civs missing economy/construction rows are skipped, not
  thrown; corrupt stability/condition/arrears flag invariants).
- **Save/performance impact:** read-only projection — zero persistent
  state; settlement build is O(buildings) once per colony per daily
  diagnostics pass.
- **Limitations:** `utility_balance()` reports nameplate spec
  supply/demand — Core's effective supply is condition-efficiency
  scaled plus a base 2, so the balances differ by design (documented in
  the adapter contract); food/water capacity and credit/industry build
  *time* have no engine analog; the projection carries no districts —
  Core's flat building list maps to standalone slots only.

## Massive simulation scheduler + simulation LOD executor (2026-09-23)

- **Purpose:** the space-strategy specialization's foundation — drive
  thousands of systems/colonies/fleets at relevance-scaled cadences
  without per-frame cost or nondeterminism. See
  [SIMULATION_LOD.md](SIMULATION_LOD.md) and
  [SPACE_STRATEGY_ENGINE.md](SPACE_STRATEGY_ENGINE.md).
- **Engine APIs/ownership:** `SimulationScheduler`
  (simulation_scheduler.hpp) — the existing tier-cadence core, now with
  deterministic sorted due lists and a peek/mark split
  (`begin_tick`/`collect_due`/`mark_ran`/`elapsed_since_run`/
  `dormant_keys`) so deferred and event-woken execution keeps exact
  elapsed accounting. `SimulationExecutor` (simulation_executor.hpp) —
  registers `SimulationTask`s (callback + `SimulationTier` +
  `JobPriority` + domain tag + `depends_on` ordering edges) and runs
  authoritative ticks: eligibility = cadence-due + `mark_dirty` +
  `wake`/`wake_domain`/`wake_all` (Dormant items are the event-driven
  tier — a wake runs them once with accumulated dormant elapsed);
  ordering = topo over due dependencies, ties by priority then **aging**
  (largest elapsed first — sustained budgets cannot starve keys) then
  key; `SimulationBudget{max_wall_time, max_tasks}` defers overflow with
  `elapsed_ticks` catch-up; `advance_parallel(JobSystem&)` executes
  dependency waves as tagged/prioritized jobs; `set_paused` freezes the
  tick; `set_tier`/`evaluate_tiers` handle LOD promotion/demotion;
  `dormant_items()` feeds bulk analytic propagation. Reports/stats:
  `SimulationStepReport` (eligible/ran/deferred/wakeups/wall/jobs),
  per-domain run/ns stats, `tick_history` percentiles, `tier_counts`,
  `total_wakeups`. Persistence: `capture_state`/`restore_state` on both
  layers carry tick, tiers, last-run/dormant bookkeeping, pending
  dirty/event wakeups and pause — taskless snapshot keys are reported
  unmatched instead of scheduled. The same versioned
  capture/restore contract covers every specialization framework
  (`Population`, `Colony`, `FlowNetwork`, `LogisticsNetwork`,
  `WarfareModel`, `StrategicMind`, `ResourceNetwork`): runtime state
  round-trips, definitions (profiles/specs/recipes/classes/actions)
  are re-registered code-side, and unknown references throw.
  `framework_state_json.hpp` provides the byte-level layer —
  templated `to_json`/`from_json` codecs (nlohmann-compatible,
  matching `galaxy_phenomena_json.hpp`) for all of those State
  structs so campaign save codecs can embed framework state without
  hand-written field lists.
  `SimulationExecutor::consume_tick()` marks every task as having
  consumed the current tick — the aborted-step contract: a mid-pipeline
  exception followed by a retry of the same logical step must not let
  un-run tasks double-integrate the aborted span.
- **Consumers/tests:** `simulation_executor` functional tests (cadence,
  elapsed catch-up, dirty/event/dormant wakes, ordering, budgets,
  pause, promotion, parallel≡serial state, consume_tick abort/retry
  semantics),
  `stellar_simulation_persistence_tests` (struct round-trip),
  `stellar_framework_persistence_tests` (all frameworks),
  `stellar_framework_state_codec_tests` (JSON round-trip through
  dump→parse→restore→re-capture equality plus version/enum negative
  cases) and `stellar_simulation_scale_tests` benchmarks registered as
  ctest `simulation_scale_250/500/1000/2500/5000` — serial and parallel
  checksums verified identical. **Core consumer:** the 12
  `GalaxySimulationStepCoordinator` phases (economy → economy_storage)
  run as executor tasks dependency-chained in the historical
  order; `campaign_coordinator_parity` (28 step + 8 combat cases)
  verifies identical behavior. **Phase cadence policy** is now
  coordinator-owned data: `set_phase_tier`/`phase_tier`/`wake_phase`
  demote any phase (Dormant = event-driven), day-integrating phases
  scale their span by `elapsed_ticks` so coarse runs conserve simulated
  time, and `coordinator.advance()` consumes the tick on exception so a
  retried step cannot double-integrate. The **seeded parity oracle**
  (`campaign_phase_cadence_oracle` ctest) captures a GalaxyPayloadV16
  digest after every step and proves all-active determinism,
  byte-exact parity for phases inert across the trace, honest
  divergence when a live phase is demoted, and dormant→wake accounting
  — the gate any real demotion policy must pass before adoption.
- **Save/performance impact:** tasks are code — re-registered on load,
  never serialized; cadence bookkeeping is derivable. 5000-task
  registration ≈ 320 KB task-state in the benchmark model; serial
  5000-system tick ≈ 536 µs mean on the dev machine.
- **Limitations:** ordering-only dependencies (no cross-tick dataflow);
  wall budget checked between tasks/waves (a single oversized task is
  never preempted); parallel waves wait fully between levels; cadence is
  owner policy, not adaptive. The coordinator runs serial with all
  phases Active — tick-count-driven phases (automatic_orders,
  legacy_research, economy_storage) have no day parameter to scale, so
  their demotion accrues at run cadence only; no production phase is
  demoted yet — each adoption must clear the parity oracle.
- **Future reuse:** the executor is the intended host for economy,
  population, colony, logistics and civilization-AI cadences in
  milestones 2–9 of the space-strategy specialization.

Records below retain purpose, API, consumers, tests, save/performance impact and
limitations. Current [architecture](ENGINE_ARCHITECTURE.md) and
[celestial status](CELESTIAL_CONTENT_STATUS.md) resolve superseded descriptions.

## Reusable 3D scene mode for generated games (2026-09-23)

- **Purpose:** the same generated-project loop (author → cook → build →
  run → save) now supports 3D worlds — a `scene3d.json` document drives a
  world of mesh entities simulated and rendered by the engine's existing
  GPU `Scene3D` pipeline, composited under the 2D pass so scene entities
  remain usable as HUD/overlay.
- **Engine APIs/ownership:** `Scene3dDocument` (scene_document.hpp) is
  the 3D counterpart of `SceneDocument`: camera (pos + yaw/pitch deg +
  fov + near/far), directional key light (world-space dir + intensity),
  background, `gravity` (−Y), `groundY` rest plane, `bounds` XZ
  half-extent, `music`, and the shared `emitters` table — serialized as
  strict all-or-nothing JSON. `Scene3dEntity` covers name, mesh spec,
  pos/rot (yaw-pitch-roll deg)/scale, velocity, color/opacity/texture,
  `doubleSided`, `gravityScale`, `solid`, `ttl`, `data`, `parent`.
  ECS: `Transform3D` (pos + quaternion + scale), `Velocity3D`,
  `MeshRef`, `TextureRef`, `DoubleSided`, `Parent3D` — all snapshot-
  persisted via registered codecs alongside the existing 2D set.
  `spawn_scene3d`/`entities3d`/`scene3d_from_world`/`resolve_hierarchy3d`
  mirror the 2D helpers; `Mesh3D` now carries local AABB bounds
  (`bounds_min`/`bounds_max`) computed in `create()`.
- **Mesh sources:** `MeshRef::spec` accepts `box[:sx,sy,sz]` and
  `annulus:inner,outer[,segments]` primitives
  (native_geometry3d.hpp), `sphere[:cols,rows]` (`Mesh3D::uv_sphere`),
  or a content-relative `.obj` path loaded through `ContentResolver`
  (cooked bytes or loose file) by `load_obj_mesh` — a minimal Wavefront
  OBJ parser (v/vn/vt/f, fan triangulation, generated flat normals).
- **RuntimeHost --scene3d:** `RuntimeHostOptions::scene3d` /
  `scene3d_file` (`--scene3d`, `--scene3d-file`, `--fly-speed`). The host
  loads the document into the same World (3D entities form a separate
  tracked set), hot-reloads it with the 2D scene poll, flies the camera
  with the rebindable "game" context (WASD move, Space/C up/down,
  right-drag look, wheel fov), integrates gravity + velocity at the
  fixed timestep, rests entities on `groundY` by their mesh's scaled
  world-AABB bottom, clamps/bounces at `bounds` (`NoBounce` opts out),
  ticks `Lifetime`, resolves `Parent3D` follow, and runs contact events
  — solid movers push out along the least-penetrated axis and zero
  inward velocity; `on_collision`/`on_collision_exit`/`on_land` fire for
  the 3D set. Public API: `scene3d()`, `entities3d()`,
  `entities3d_in_radius`, `spawn_entity3d`, `on_spawn3d`,
  `set_camera3d` + getters, `gravity3d()`, `ground_y()`, `raycast3d`,
  `entity3d_at`. F5/F9 snapshots capture the 3D set —
  including a `Camera3DState` carrier that restores the fly camera — and
  `load_world` partitions it back out of the 2D list. Narrow-phase
  collision uses SAT over oriented bounding boxes (`ObBox3D` +
  `obb_separation` in physics3d.hpp — each entity's local mesh bounds
  transformed by its quaternion/scale): rotated boxes resolve on their
  true faces instead of the conservative world AABB, and the returned
  minimum translation vector drives push-out + landing. The world AABB
  is still computed alongside for the ground plane, `bounds` clamping,
  and broad-phase pair rejection. A `lights` document
  array (max 2) feeds `Material3D::additional_lights` as world-space
  directional fills. `create_project`'s windowed starter ships a ready
  `editor/scene3d.json` and documents `--scene3d` in the host comment.
  `raycast3d(origin, dir, max_distance)` casts a ray against actual
  mesh triangles — shared `raycast_world3d` (scene_components) +
  `resolve_mesh_spec` (mesh3d_loader) transform the ray into each
  mesh's local frame (rotation + scale aware) and test triangles via
  `intersect_mesh_segment` (bounding-sphere reject); the nearest hit
  returns `{entity, distance, world point}`. `entity3d_at(sx, sy)`
  builds the camera ray through a viewport pixel for mouse picking.
  Both shared functions are reusable by tools — e.g. a scratch world
  from `spawn_scene3d` gives document-level picking without a host.
  Verified live: vertical rays hit box tops exactly, a 45°-rolled plank
  reports its true rotated face (local y≈0.25), a sphere occludes the
  plank behind it, and a screen-center pick through the pitched camera
  lands at the analytically-correct floor point.
- **Consumers/tests:** any generated host passes `--scene3d`;
  `engine_project` tests cover document round-trip/malformed/save-load,
  `engine_world` covers spawn/components/codecs/hierarchy/export/
  box_mesh/OBJ. Verified live: a gravity ball falls and rests on the
  ground plane (AABB bottom), static solids stay put, the scene renders
  through `Scene3DView` under the 2D HUD.
- **Save/performance impact:** 3D components are POD/string codecs in
  the same snapshot stream; meshes/textures cache per spec; contact scan
  is O(n²) over the 3D set (small scene counts); rendering reuses the
  existing bounded `Scene3D` submission path.
- **Limitations:** collision is OBB over the mesh's local AABB (not
  per-triangle — a sphere mesh still collides as its box); solids are
  blockers, not full rigid-body dynamics (no stacking solver — `groundY`
  + the upward push-out cover landing); ground plane and `bounds` still
  use the world AABB; `physics3d` kinematics/`spatial_index3d` exist
  engine-side but are not wired into this mode (raycast uses
  `segment_triangle` via `intersect_mesh_segment`); the Scene3D editor
  tab covers entity + document fields with a live preview/pick but has
  no transform gizmos or light/emitter authoring UI; lighting is one key
  light + up to two directional fills per material; raycast is
  O(triangles) per entity with no spatial partition — fine for queries,
  not per-frame sweeps.

## Authored tilemap layers for generated 2D games (2026-09-21)

- **Purpose:** reusable grid terrain for generated projects — authored
  tilemaps render tileset images across cell grids, each drawing at its own
  layer between entities, and optionally participating in authoritative
  collision (side-blocking, top landing, grounded detection for jump).
- **Engine APIs/ownership:** `SceneDocument::tilemaps` is a vector of
  `SceneTilemap` (tileset path, `x`/`y` grid origin in world px —
  chunked/procedural maps place tiles at nonzero offsets, `tileW`/`tileH`
  cell size, `columns`, `layer`, `parallax`, `collide`, `cells` with `-1`
  empty) serialized as a
  `"tilemaps"` JSON array with strict per-entry validation (positive
  dimensions, cell count divisible by columns); legacy single-`"tilemap"`
  documents still parse as a one-element array. `spawn_scene` carries each
  tilemap into the world as a `Tilemap` component on its own dedicated
  entity in document order (`tilemap_entities()` lists them,
  `tilemap_entity()` returns the first, `scene_from_world` re-exports all),
  so cell state is authoritative and snapshots with F5/F9 quicksaves —
  runtime cell edits (destructible terrain) persist. The host resolves each
  map's tileset through `ContentResolver` (per-map image cache), renders
  cells via the `Image` source-rectangle path in layer-sorted order that
  interleaves with the entity pass, honors camera transform and per-map
  parallax, and runs tile collision against every `collide` map inside the
  same authoritative movement pass as solid/oneway entities — each probe
  uses that map's own tile geometry and cells. `RuntimeHost::tile_at`/
  `set_tile_at` take an optional document-order map index (default 0 =
  the primary grid) and `tilemap_count()` reports the layer count;
  `tilemap_entities()` exposes the carriers for direct component work.
  `spawn_tilemap(SceneTilemap)`/`destroy_tilemap` add and remove layers
  at runtime (procedural terrain) — they join the same tracked set, so
  they render, collide and snapshot identically to scene-authored maps
  (scene hot-reload rebuilds all layers, like respawned entities).
- **RuntimeHost input actions:** the host now feeds every platform event into
  an `InputMapper` — a built-in "game" context (move_left/right/up/down on
  WASD+arrows+D-pad, `move_x`/`move_y` analog Axis1D on the left stick with a
  0.18 deadzone folded into player velocity, jump on Space/W/Up/pad-South,
  fire on Space/LMB/pad-RB, mine on C/pad-West) drives the player, so
  `RuntimeHostOptions::input_map`/`--input-map` JSON stacks project contexts
  on top and `host.input()` exposes `pressed`/`just_pressed`/`axis`/`rebind`
  to game code. `InputMapper::context_names()` enumerates registered contexts
  so a loaded map activates without name plumbing.
- **Gamepad input:** the platform layer opens the first attached SDL gamepad
  (`SDL_INIT_GAMEPAD`, hot-plug add/remove), normalizes buttons and
  clamped -1..1 axis motion into `GamepadPressed`/`GamepadReleased`/
  `GamepadAxis` `InputEvent`s, and `RuntimeHost` converts them into mapper
  `RawInputEvent`s — so generated games read pad input through the same
  action names as keyboard/mouse. `InputMapper` keeps per-axis last-value
  state (`gamepad_axes_`) because devices only emit axis events on change;
  `axis()` folds live GamepadAxis bindings into the per-frame result.
  Covered by `input_actions` tests (context_names enumeration, pad button
  press/release edges, axis persistence/update across frames, plus the
  existing feed/axis/chord/rebind suite); verified `--input-map` loads and
  degrades to defaults on missing/malformed files. Remaining gap: no
  rebinding UI or pad-specific glyphs in the tools; only the first pad is
  used.
- **Scene music:** `SceneDocument::music` names a content-relative track the
  host plays when the scene loads — per-level music for `set_scene()`
  switching and hot reload (a change to the field restarts the new track;
  empty keeps the current/options track). The Scene tool exposes a doc-level
  `music` field; covered by `engine_project` round-trip tests.
- **Scene world bounds:** `SceneDocument::worldSize [w,h]` lets each level
  declare its playable extent — the runtime resolves bounce/camera bounds as
  `--world-w/--world-h` argv > scene `worldSize` > viewport. The Scene tool
  exposes a doc-level `worldsize` field; verified by snapshot runs (an entity
  clamps at 400 vs 4000-wide bounds).
- **Entity spin:** `SceneEntity::spin` (deg/s) becomes a `Spin` component that
  integrates into `Rotation` each sim step — rotating hazards/props without
  per-frame game code. Snapshot-verified (spin advances the serialized
  rotation); the Scene tool exposes a `spin` field and the preview shows
  live rotation.
- **Entity bounce opt-out:** `SceneEntity::bounce=false` adds the `NoBounce`
  marker — the entity clamps dead at world bounds instead of rebounding
  (projectiles, debris). Snapshot-verified: a `bounce:false` mover stops at
  `world_w - w`; the Scene tool exposes a `bounce` bool field.
- **Runtime query surface:** `host.find_entity(name)` locates a tracked
  entity by authored name (doors, waypoints, triggers — "player" is just the
  conventional one); `host.sim_time()` reports deterministic elapsed sim
  seconds; `host.world_width()`/`world_height()` expose the resolved level
  bounds for spawn limits, AI roam ranges and minimap math;
  `host.screen_to_world(sx, sy)` maps pointer positions into the world
  under the camera (aim, click-to-move) and `host.entity_at(sx, sy)`
  hit-tests drawn bounds topmost-first (layer order, doc-order ties) with
  per-entity parallax and zoom applied — HUD picks where it appears,
  hidden entities and tilemap carriers never match.
- **Named save blobs:** `host.save_data(key, bytes)`/`load_data(key)`
  persist arbitrary game state (quest flags, inventories, settings) under
  `saves/data/<key>.dat` — atomic writes through the same rotating `.bak`
  history chain as world snapshots, newest-first recovery on a corrupt
  primary, `[A-Za-z0-9._-]` key whitelist. Verified live: a generated host
  writes and reloads a blob across runs.
- **Consumers:** `RuntimeHost` generated hosts (multi-map rendering, gravity
  landing, wall blocking, grounded jumps, hot reload); the shell Scene tool
  (TILES + adds a grid layer, MAP k/n cycles which tilemap the fields and
  PAINT edit, TILES - removes the selected layer — all under the document
  undo history; tileset/tilesize/columns/collide/layer/parallax/cells/paint
  fields, preview rendering with the same layer-sorted interleave and a
  checkerboard fallback when no tileset is set, PAINT mode that writes cells
  into the selected map by click/drag with a grid overlay and one undo step
  per stroke).
- **Save/determinism/performance:** every tilemap is a `Tilemap` component on
  its own world entity, so quicksaves snapshot all maps' cell edits and
  `scene_from_world` re-exports them in spawn order; pre-tilemap saves simply
  lack the components (scene reload restores them). Hot reload respawns all
  carriers with the document. Cell scans are O(columns x rows) per map with
  viewport culling; collision probes sample a few cell points per moving
  entity per colliding map per fixed step. Grid order is row-major and both
  spawn and draw order are deterministic (stable layer sort, doc order within
  a layer).
- **Tests:** `engine_project` tests cover JSON round-trip of a two-tilemap
  document (independent tilesets, dims, layer, parallax, collide, cells),
  legacy single-`"tilemap"` parsing, and rejection of malformed maps in both
  forms; `engine_world` tests cover per-map dedicated-entity spawn, both
  carriers staying out of the gameplay list, codec round-trips through
  `snapshot()`/`restore()` including independent runtime cell edits on each
  map, and `scene_from_world` exporting both; live verification on a
  generated project: a ball lands on the SECOND map's platform (y=160 vs the
  first map's floor at y=672 — each map's own geometry applies) and rests on
  the first map's floor when the platform map is removed.
- **Limits/reuse:** `tile_at`/`set_tile_at` take a document-order map index
  (`tilemap_count()` reports the layer count) and `tilemap_entities()`
  exposes the carriers — but there is no named-map lookup; the editor
  selects but cannot reorder tilemap layers (edit `layer` for draw order);
  collision is cell-level solid only (no per-tile slopes/one-way flags);
  paint strokes fill single cells (no brush size or fill tool). Other
  RuntimeHost consumers (2D platformers, top-down maps, puzzle boards)
  reuse the same path.

## Cooked flare reservations, local crash reports and small updates (2026-09-20)

- **Purpose:** fix the installed star-map zoom crash and retain actionable local
  evidence for future failures; distribute only changed release files.
- **Engine APIs/ownership:** `image_decode_output_bytes` in `native_image.cpp`
  reads mounted registry metadata to reserve the decoder's complete retained
  output, including selected mip tails and compressed-texture CPU fallback.
  `ImagePreparationQueue` still owns bounded asynchronous preparation. Its error
  now reports reserved/actual bytes and dimensions. `RuntimeDiagnostics` owns
  local stream mirroring, `fatal`, view `context`, retention and Windows fault /
  C++ termination reporting. The application supplies version, renderer, view,
  selected object, zoom and simulation time; diagnostics never reads Core state.
- **Maintenance APIs/consumers:** `Release::{parse,includes_payload}` and the
  existing `make_plan/execute` transaction accept a full target inventory with
  an exact `updateFrom` baseline and smaller `payloadPaths` download.
  `build-update-installer.ps1` hashes the full validated target before copying
  changed files. Setup verifies every retained installed file, rejects wrong or
  damaged bases before mutation, and uses existing repair/rollback rules.
- **Consumers:** packaged flare loading in both star-map and solar-system views;
  native client startup/error handling; installed and developer launches;
  update Setup, existing release builder and future offline updates.
- **Save/determinism/performance:** no gameplay, clock, RNG or save schema change.
  No texture resolution, mip, filtering, lighting or artwork changes. Reservation
  uses metadata, without decoding or allocating pixel data. Log context is a
  fixed 1 KiB buffer; disk context updates are limited to once per second.
  Normal session logs cap at 4 MiB; retain eight files of each report type.
  Installer hashes stream in bounded buffers and unchanged files keep timestamps.
- **Tests:** before-fix failure reproduced against the installed cooked packages;
  140 class/type/quality asynchronous zoom transitions retain supplied mip chains.
  Metadata estimates match actual retained bytes for sampled shipped formats,
  both decode usages and three resolution limits, without package reads.
  Diagnostics tests cover caught errors, Windows faults, worker termination,
  valid minidumps, stream mirroring, clean exit and retention. Maintenance tests
  cover exact-base checks, damaged retained files, malformed partial manifests,
  partial update rollback/repair and existing transaction regressions.
- **Limits/reuse:** crash dumps are best effort for faults that reach the process
  handlers; forced termination/power loss cannot guarantee a dump. No uploads.
  Development update is unsigned, offline and requires the named installed base;
  omitted damaged content needs base repair. Changed files are copied whole, not
  binary patched. Other image preparation consumers can reuse the estimator;
  other native clients can reuse process diagnostics. See
  [the crash investigation](STAR_MAP_ZOOM_CRASH_FIX_20260920.md).

## Windows release maintenance (2026-09-20)

- **Purpose:** install, update, repair, block downgrades and recover failed or
  interrupted Windows installations of cooked Stellar Continuum releases.
- **Ownership/modules:** Engine `ProductVersion` owns numeric four-component
  version/channel precedence. `RuntimeDirectoryLease` supplies an OS-backed
  game/maintenance interlock. `stellar_maintenance` owns strict release parsing,
  managed-path validation, file plans, streaming SHA-256, durable transaction
  journals, recovery and uninstall. `stellar_windows_maintenance` owns per-user
  registry/shortcut integration, pending-install discovery and process locks.
- **Public interfaces:** `Release::parse`, `determine_mode`, `make_plan`,
  `execute`, `recover`, `uninstall`, `Platform`, `ProgressSink`,
  `ProductVersion::{parse,string,channel_name}`, `RuntimeDirectoryLease`.
- **Consumers:** native Setup/Uninstall windows, native game startup,
  `build-release-installer.ps1`, existing cooker packaging, generated application
  versions, diagnostics and save version metadata. The UI does not implement
  separate version or transaction rules.
- **Save/thread/performance impact:** no save schema or simulation change.
  Player paths retain the existing LocalAppData default; the developer default
  uses a separate subfolder. Worker-thread installation streams 1 MiB buffers;
  unchanged files retain timestamps and changed generation names can reuse
  verified local package bytes. Updates serialize per user and per directory.
  No maintenance loops run in gameplay. Current full-payload maintenance test
  processed a 4.29 GB release with about 8.55 MiB peak working set.
- **Tests:** version/channel/malformed-path checks; clean install, differential
  update, no-op and corruption repair, downgrade protection, cancellation,
  six failure boundaries, four abrupt process exits, launch/file locks,
  transactional uninstall, saves/mods/settings preservation; isolated actual
  Windows registration/ShellLink and metadata-repair tests; full shipping
  package install/repair/uninstall and native-window inspection.
- **Limits/reuse:** per-user offline installation only; one registered install
  per user. Authenticode hooks and a compiled manifest hash are provided, but
  the current development build is unsigned. No automatic online updates,
  binary deltas, machine-wide deployment or historical rollback after successful
  cleanup. Future remote updates require HTTPS plus signed metadata validation.
  See [installer design and release workflow](WINDOWS_INSTALLER.md).

## Scoped simulation lookups and lean image residency (2026-09-20)

- **Purpose:** remove repeated full-catalog scans from survey planning and
  colony economic updates, and avoid redundant image memory without changing
  simulation results, texture resolution, lighting or filtering.
- **Ownership/APIs:** Core `SurveyOperationsBatch::build` lazily builds one
  read-only profile table per exploration planning pass. Core
  `SettlementBodyIndex::bodies_for` resolves only the requested settlements,
  including the exact system/body pair and first-match behavior. Existing
  biology, sustenance and resource rules remain authoritative. Engine
  `ImageDecodeUsage::PixelsOnly` reads one selected cooked mip; immutable
  `RgbaImage::pixels` aliases the RGBA8 base mip instead of copying it.
- **Consumers:** exploration candidate evaluation, `economy_credit_flow`,
  `advance_colony_economies`, startup/navigation/research/ship/disc artwork and
  galaxy/system background loaders. 3D material loaders keep their mip tails.
  The 2D cache accounts for actual CPU storage plus its RGBA GPU upload.
- **Save/performance:** no schema, RNG or rule changes. Lookup lifetimes are
  one request/tick; rebuild after body storage or identity changes. Survey
  preparation is O(systems + bodies), then constant-time queries. Settlement
  preparation scans bodies once and retains at most one entry per requested
  body, followed by constant-time lookups; no all-body persistent cache.
- **Tests:** exact survey parity over 50,000 systems/350,000 bodies, duplicate
  IDs, missing parents, NaNs and fresh scopes; colony parent/first-match/error
  parity; 5,000-colony state hash against the pre-optimization executable;
  existing economy/biology/operations fixtures; 800-tick saved-campaign replay;
  selected-mip exact pixels/one-read and shared RGBA storage assertions;
  native GPU/cache tests and three cooked game launch scenarios.
- **Measured results:** the 5,000-colony/350,000-body economic update fell from
  11,814 ms to 13.85 ms with identical state. Startup peak RAM fell from
  502.4 MB to 412.8 MB; four static screenshots are byte-identical. These are
  scoped measurements, not whole-game speedups. The 500-system replay remains
  about 1.9 ms/tick. Details and known failures are in
  [the performance report](PERFORMANCE_AUDIT_20260920.md).
- **Cleanup:** `tools/clean-obsolete-runtime-assets.ps1` checks the runtime
  allowlist, rejection audit, original SHA-256 and confined absolute paths.
  It removed 130 rejected local image duplicates plus 16 superseded generated
  package/manifest files (8,616,185,815 bytes). Original artwork, saves, audit
  records and current/previous downloads remain intact.
- **Limits/reuse:** 50,000-system generation/routing/save/load passes, but large
  saves are still expensive. A historical generation fingerprint assertion
  does not match the expanded current payload and still needs a baseline
  audit; it was not silently replaced. Massive combat/full-AI fleet stress,
  automatic VRAM budgeting and incremental audio streaming remain unverified
  or unfinished. These lookups can support other request-scoped Core queries.

## Cooked assets, packages and release validation (2026-09-20)

- **Purpose/ownership:** Engine `asset_cooker`, `texture_cook`, `asset_registry`,
  `asset_audit` and `sha256` convert reviewed inputs into independently indexed
  Windows content packages. Original masters and rejected images remain intact.
- **Public APIs:** `cook_asset_repository`, `audit_asset_repository`,
  `cook_texture`, `select_texture_mip`, `AssetRegistry::{find,read,validate_all,
  diagnostics}`, `mount_asset_registry`, `resource_exists`, `resource_stream`,
  `read_resource`, `RgbaImage::create_cooked`, and bounded codec helpers.
- **Consumers:** native image/GPU loaders, fonts, audio, research/catalog readers,
  planet materials and sky/VFX preparation queues. Native diagnostics/support
  bundles expose package generations and load failures. `StellarCooker` plus
  `tools/build-cooked-game.ps1` automate build, validation, symbols, manifests,
  optional launch verification and ZIP creation. CI entry is manual and requires
  a Windows graphics runner; local validation is separate evidence.
- **Data/performance:** stable IDs and aliases preserve saves; source/dependency
  hashes and implementation fingerprints invalidate cache products. Immutable
  registry records allow concurrent reads. Per-mip indexed reads avoid unused
  high LODs. Exact identical chunks share storage. BC7/BC5/BC4 support uses GPU
  capability checks and RGBA fallback; strict quality gates retain sensitive
  pixels losslessly. Existing CPU/GPU cache limits remain in force.
  The 2D galaxy loader discards unused cooked mip storage before admission;
  lossless local skies retain exact RGBA with the same residency as loose input.
- **Tests:** clean/incremental fixture, deterministic workers, cache repair,
  dependencies, states, imported master versus runtime-raster selection, partial
  mips, source-fallback rejection, and damaged/missing/truncated headers and
  checksums. GPU tests compare compressed color/alpha/normals to originals;
  source/cooked 1:1 reviews cover fourteen asset categories. Real packaged launch
  checks and measurements are recorded in `work/cooker/` and the cooker report.
  Optional cooked-root background tests compare every source pixel and exercise
  asynchronous queue limits. Research errors preserve loose-directory behavior
  while allowing virtual packaged catalogs.
- **Limits/reuse:** full-canvas VFX still require origin-aware crop integration;
  true incremental audio decoding, imported model-buffer cooking, HDR/BC6H and
  automatic VRAM budgeting remain unfinished. Current music remains compressed
  on disk and bounded PCM in memory; current geometry uses procedural LODs.
  Optional mods/updater/hot reload are not implemented. The complete requested
  optimization scope is not implied complete. See [cooker guide](ASSET_COOKER.md).
  Actual sizes and acceptance evidence: [cooked release report](ASSET_COOKER_REPORT.md).

## Package codec escalation and BC7 quality retries (2026-10-08)

- **Purpose/ownership:** shrink cooked packages without relaxing the texture
  quality gates. Engine `texture_cook` retries quality-gate failures at maximum
  BC7 encoder effort before declaring a lossless RGBA8 fallback; engine
  `asset_registry` selects the smallest result across `None`, `XpressHuff`,
  `XpressRgbaDelta`, `Lzms` and `LzmsRgbaDelta` per chunk. Failed compressor
  output is never tagged as compressed.
- **Public APIs:** `AssetCodec` gains `Lzms`/`LzmsRgbaDelta` (values 3/4);
  `compress_asset_bytes`, `decompress_asset_bytes`, chunk headers and manifest
  codec validation accept the expanded bounded range. BC4/BC5 paths are
  unaffected because encoder effort is a BC7-only parameter.
- **Consumers:** every cooked-package reader (image/GPU loaders, fonts, audio,
  catalogs) transparently decodes the new codecs through `decompress_asset_bytes`.
  Package readers and manifests produced before this change remain readable:
  codec 0–2 data is unchanged and old loaders reject unknown tags safely.
- **Data/performance:** measured on the four fallback-heavy categories (vfx,
  properties, critical, background): cooked unique bytes 3.88 GB → 3.42 GB
  (−11.9%) with identical fallback counts and unchanged quality metrics.
  LZMS encoding roughly doubles per-chunk cook time on those categories;
  decompression cost stays in the same class as XPRESS. BC7 effort escalation
  rescued 2 of 1,013 fallbacks and is retained mainly for diagnostic quality
  reporting; remaining fallbacks are legitimate gate failures.
- **Tests:** `engine_asset_cooker` covers codec-tag bounds, predictor round
  trips, LZMS and LZMS+delta round trips, empty-input handling, damaged-size
  rejection, and failed-compressor fallback to `None`. Deterministic-cache,
  integrity and maintenance suites unchanged.
- **Limits/reuse:** LZMS is Windows Compression API only; portable cooks would
  need another codec id. No adaptive streaming yet — storage saving only.

## Canonical moons, stable axes and quiet skies (2026-09-20)

- **Purpose:** import all 18 supplied major Sol moons into fresh and saved games,
  provide shared nested orbits, keep selected planets trackable, and make local
  skies resemble the faint regional star map.
- **Ownership/public APIs:** Core `planetary_satellites` exposes immutable
  `SolMoonDefinition`, `SatelliteOrbit`, `planetary_satellite_orbit` and
  `satellite_relative_position`. Engine `framed_orbit_position` transforms mean
  ellipses; `rotation_frame` constructs validated orthonormal orientations.
  Configurable bounded spherical preparation preserves Iapetus's dark terrain.
  Core `system_background` selects only the audited faint reference; application
  `faint_starfield_tint` keeps map/system brightness consistent.
- **Consumers:** catalog generation/migration, observer-safe system snapshots,
  production system paths/meshes, planetary globe, lighting/locked pose, asset
  allowlists and developer background inspection. Original images are retained.
- **Save/determinism:** idempotent append of reserved Sol moon identities;
  existing planets and appearance records remain intact. Physical elements and
  deterministic epoch reconstruct without a second simulation. Dwarf planets
  may parent moons. No new save schema required. Cosmetic 8–14-minute rotation
  uses real time and a fixed axis; tidal facing follows authoritative orbits.
- **Performance:** closed-form nested positions, 96 MiB CPU material cache,
  at most two 2048-wide close system materials, existing GPU limits. Resolved
  moons use a readable minimum disc and per-family orbit separation. Background
  cache remains three textures and 4,096 profiles; only one sky source ships.
- **Verification:** `native_moons`, `system_background`, `stellar_orbits`,
  `planet_appearance`, native system workspace/view/material/planetary-screen,
  save/persistence/colony-entry and scene GPU suites. Production family captures
  complement individual material inspection; camera remains anchored through
  hourly and large time steps. See `MOON_IMPLEMENTATION_REPORT.md` and
  `STARFIELD_IMPLEMENTATION_REPORT.md` for evidence and ownership details.
- **Limits/reuse:** mean orbits are not dated ephemerides or n-body integration;
  hidden hemispheres and display spacing remain approximate. One analytic
  eclipse caster lacks finite-star penumbra and combined ring/moon shadows.
  The sky currently has one approved reference. These generic frame/geometry
  APIs can support additional satellites. The full asset cooker/package task
  remains in progress and is not implied complete by these changes.

## Clear Sol sky, source detail and exclusive new giants (2026-09-20)

- **Purpose/modules/interfaces:** Core `SystemBackgroundProfile::local_nebula`
  enforces canonical clear-sky presets across local views without deleting galaxy
  phenomena. `migrate_giant_appearance` now resolves all four gaseous classes and
  canonical Sol to approved new materials and clears extra cloud opacity.
  Small-body generation/reconciliation restores 2,048 representatives per Sol
  belt. Engine `Material3D::cubic_magnification` opts into bounded cubic sampling
  for magnification only; default materials and minification remain unchanged.
- **Consumers:** System entry, system/battle sky rendering, saved/fresh planetary
  appearance and shared body inspection. High/Ultra retain original RGBA skies
  at native resolution, with corrected exposure; map phenomena remain separate.
- **Save/performance:** no new persisted fields or simulation threads. Existing
  physical planets, valid rings, resources and belt orbits are retained. Obsolete
  cloud jobs cancel on clear-sky transitions; queue/cache/close-mesh bounds remain.
- **Tests:** full source pixel checks, 48,000 profiles, cloud-to-Sol transitions
  and queue reclamation, giant migration/fresh Sol/round-trip, belt rendering,
  GPU contrast/no-overshoot/minification, and a real 1,000-system saved replay.
- **Limits/reuse:** finite source detail, approximate hemisphere reconstruction,
  representative belts, schematic moons and incomplete ice reflection/refraction.
  Preset environment policy and optional sampling are reusable by other scenes.
  See [correction report](SOLAR_VISUAL_CORRECTIONS.md).

## Reviewed giants, finite rings and deterministic system skies (2026-09-20)

- **Purpose/modules:** Core `planetary_rings` owns approved ring families, seeded
  incidence, Roche/thermal eligibility and legacy giant migration. Planet
  appearance carries shared giant physics/art identity. `system_background`
  owns location-aware sky profiles using actual saved generation/phenomenon data.
  Engine owns source preparation, oblate/annular geometry, sampled analytic
  mutual shadows, celestial plate projection and optional compressed textures.
- **Interfaces:** `make_planet_ring`, `validate_planet_ring`,
  `planetary_roche_radius`, `planet_rotational_oblateness`,
  `migrate_giant_appearance`, `SystemBackgroundCatalog::{bind,profile,region_examples}`,
  `select_system_background`, `celestial_plate_mesh`, `prepare_celestial_plate`,
  `annulus_mesh(..., thickness)`, and `RgbaImage::create` optional BC1 mip chains.
  The GPU adapter checks format support, uploads all supplied mips and falls
  back to RGBA on unsupported devices. Ring materials retain RGBA and anisotropy.
- **Consumers:** System View, shared planet inspection, developer giant
  laboratory, system/battle backgrounds, video settings, audited runtime export.
  Core developer commands validate before changing their persisted laboratory
  record; sky overrides remain presentation-only. All consumers share profiles
  and material assembly instead of independently choosing art.
- **Save/thread/performance:** versioned optional giant/ring fields preserve
  identity; 131 retired giant records migrate deterministically and idempotently.
  Sky profiles reconstruct from unchanged saved inputs against the frozen v1
  manifest. No new simulation threads. Shared async preparation, three-entry
  sky image cache, 4,096-profile cap, 64-entry ring geometry cache and existing
  GPU/material budgets bound resources. Low/Medium skies use BC1; High/Ultra are
  source-limited RGBA. Entry gates wait for selected environment art. Owner-thread
  polling drains completed sky/cloud jobs even after leaving their view, so
  uncollected results cannot starve another consumer's queue admission.
- **Tests:** eleven final suites cover Core distributions/eligibility, 131 legacy
  records, 48,000 sky profiles, 250-system full save reconstruction, shared native
  views, closed slab winding, gap preservation, mutual shadows and Vulkan
  anisotropic/BC1 uploads. Fifteen packaging tests verify exact approved pools,
  rejected paths, original hashes and missing/tampered assets. Actual native
  captures and packaged campaign loading provide integration evidence.
  Both affected suites passed again after the inactive-view queue correction;
  the relocated build passed System/Galaxy replays with a 1,000-system save copy
  and 4,568 asset/manifest hash checks, preserving the original save.
- **Limits/future reuse:** single-image hemisphere and lighting recovery remain
  approximate. Rings have finite annular geometry and analytic shadows without
  particle dynamics or multiple scattering. Planet shape/thermal rules are
  bounded approximations. The sky uses rectilinear plates for the current chart
  camera and planar galaxy metadata; dark attenuation is not full volumetric
  transport. Geometry, filtering and compressed image support are reusable for
  other tilted surfaces and distant environments. Details and exact counts:
  [giant/ring report](GIANT_RING_IMPLEMENTATION_REPORT.md),
  [starfield report](STARFIELD_IMPLEMENTATION_REPORT.md),
  [ring sharpness report](RING_SHARPNESS_REPORT.md).

## Flare detail, independent timing and arrow baseline correction (2026-09-20)

- **Purpose/modules:** Native `EruptionArtwork` restores the Engine curved
  emissive surface path after visual comparison exposed blurred image-derived
  volume. High quality retains 1024-pixel prepared art; distortion is disabled
  and stage blending is limited to transitions. `native_system_travel` aligns
  names across the broad arrow base with matching bounds and upright rotation.
- **Interfaces/consumers:** Core runtime `stellar_activity_day` and
  `advance_stellar_activity(real_seconds)` own unscaled scheduling.
  CampaignFrame calls it once per running strategic frame. Galaxy/System views
  and developer controls consume the same epoch; CME outputs move to frame-level
  advancement. Strategic steps no longer generate accelerated visual eruptions.
- **Save/thread/performance:** optional Galaxy `StellarActivityDay` round-trips
  through player/developer payloads. Missing legacy clocks initialize at the
  saved epoch without rerolling events. No new threads, unbounded caches or
  per-star frame scans; due-event heap remains. High uses the Ultra cache budget.
- **Tests:** textured GPU filament contrast and five-family captures, four-quality
  coverage, eight-direction text captures, sixteen-bearing geometry, 1x/25x real
  time equivalence, pause/menu, persistence, legacy and malformed clock checks.
- **Limits/reuse:** full volumetric flare quality remains unfinished; curved
  authored surfaces restore crisp detail. Existing Engine volume support remains
  available but is not selected for flares. Moons remain schematic. Desktop glow
  is confirmed gone by the user. See [correction report](FLARE_SHARPNESS_AND_TIMING.md).

## Developer autosave class compatibility (2026-09-19)

- **Purpose/modules:** Core's `planet_appearance_json.hpp` accepts the numeric
  planet-class identifiers emitted by earlier ordered developer autosaves. A
  frozen 16-entry v1 mapping preserves each class, artwork and physical metadata.
- **Interfaces/consumers:** the shared `PlanetClass` JSON reader supports both
  canonical string IDs and legacy integer ordinals, including compatible-class
  lists. Native campaign loading and all shared galaxy persistence consumers use
  it; writers continue to emit canonical strings.
- **Save/performance:** no source-save edits, rerolls, extra state, threads or
  simulation changes. Migration is a constant-time validated lookup per class.
- **Tests:** all 16 legacy ordinals in regular/ordered JSON, canonical write-back,
  complete appearance preservation, and malformed/overflow/fractional rejection;
  campaign persistence suites and loading the copied developer campaign.
- **Limits/reuse:** only the known v1 ordinal table is accepted. Unknown class IDs
  remain errors rather than being silently assigned a different world type.

## Appearance completion for population-free generation (2026-09-21)

- **Purpose/modules:** Core `planet_appearance.cpp`'s
  `generate_planet_appearances` previously skipped bodies whose system had no
  stellar object (population-free generation), leaving the first save capture
  appearance-less. Restore then synthesized appearances, breaking save/load
  idempotence for those campaigns.
- **Interfaces/consumers:** starless-system bodies now receive
  `planet_appearance_for_existing` during generation, so every generated body
  carries appearance before the first capture. Player campaign save/load and
  all galaxy-payload consumers see idempotent round trips.
- **Save/determinism/performance:** deterministic (`visual_seed` derives from
  the campaign seed and body id); no schema change — saved payloads simply
  contain appearance records that restore already produced anyway.
- **Tests:** `native_research_controller` cancelled-research full-save round
  trip, `native_fresh_progression`, and the galaxy/player persistence parity
  suites.
- **Limits/reuse:** starless bodies use the preserve-existing-environment
  appearance path (no stellar class eligibility without a star); systems with
  stellar objects are unaffected.

## Combined-scenario benchmark instrumentation (2026-09-21)

- **Purpose/modules:** `app/adaptive_campaign_host.cpp` — the
  `--simulate-adaptive-campaign` benchmark measures combined late-game
  workload: `--autosave-every N` runs the real Player17
  capture/encode/atomic-write inside the running campaign every N ticks, and
  `--stress-fleets N` injects N active military fleets per spacefaring
  civilization (half in interstellar transit toward Sol, exercising movement
  and sensor/contact phases). The campaign seeds via
  `seed_persistable_fresh_campaign` so the world carries the authoritative
  galactic core and metadata the real save path requires.
- **Interfaces/consumers:** CLI flags on the headless executable; the JSON
  report gains `autosaveIntervalTicks/Count/MeanMs/P95Ms/PeakMs/Bytes`,
  `stressFleetsPerCivilization`, `finalStateCounts.fleets` and per-phase
  `phaseTimings` from the existing `CampaignPerformanceSample` counters.
- **Save/determinism/performance:** autosaves write to a benchmark temp file
  and are deleted afterwards; capture does not mutate campaign state, so
  repeat determinism checks still hold. Phase profiling is the existing
  counter path (~sub-microsecond per phase per tick).
- **Tests/verification:** measured scenario in
  `docs/PERFORMANCE_AUDIT_20260920.md` (2500 systems, 1,000 fleets, 4,000
  ticks, 8 autosaves).
- **Limits/reuse:** stress fleets are uniform military squadrons for load
  measurement, not gameplay content; organic combat engagement is not
  forced.

## Volumetric eruptions, shared visual spin and navigation (2026-09-19)

- **Purpose/modules:** `SurfaceEffect3D`, closed `surface_emission_volume` proxies,
  the GPU adapter and fragment shader add bounded object-space emission volumes.
  `measure_emissive_disc` aligns events with the visible photosphere; Core stellar
  activity samples the entire sphere. Native planet materials share slow rotation
  and parent-facing tidal locks; workspace tracking follows close moving bodies.
- **Interfaces/consumers:** optional volume parameters preserve the existing
  surface path; `Text::rotation_degrees` supports clipped cached text. Galaxy and
  System eruption renderers, below-object labels and angled travel labels use
  these shared interfaces. Core `planet_is_tidally_locked` resolves optional saved
  lock metadata for System and planetary views. Magnification is relative to Fit
  System, with a 0.05× minimum and the existing close-up camera limit retained.
- **Save/thread/performance:** optional boolean preserves legacy records and
  explicit free/locked states in regular and ordered campaign JSON. Cosmetic spin
  uses one paused-aware real-time clock; no time-scale multiplier or new worker.
  Shader integration is 8–64 samples with early exit; quality uses 16–48. Existing
  six-event visibility, image queues, resource caps and owner-thread GPU lifetime
  remain. Art-derived disc measurement is cached and does not alter pixels.
- **Tests:** volume winding/validation, GPU side-on depth and sample convergence,
  photosphere masking, viewport containment, rotated text clipping, padded disc
  measurement, uniform sites/replay, steady poles/retrograde spin, speed isolation,
  lock JSON compatibility, camera following, hourly orbit displacement, zoom
  limits, label collision and arrow bearing checks. Package results accompany the
  [implementation report](STELLAR_VISUAL_MOTION_UPDATE.md).
- **Limits/reuse:** image-inferred depth, SDR, no plasma or tidal-evolution solver,
  existing schematic moon orbits, future CME travel/hazards. The user subsequently
  confirmed desktop glow is gone. Volume is no longer used for flares; disc measurement
  and rotated text primitives are reusable without view-owned game simulation.

This supersedes the earlier fixed live-planet pose policy at the user's request;
static portraits and the policy against added cloud layers remain unchanged.

## Stable star-map labels at deep zoom (2026-09-19)

- **Purpose/modules:** `native_galaxy_labels` previously rejected valid distant
  object projections beyond one million pixels, closing the native game during
  star-map zoom. The shared layout now clips finite positive obstacle footprints
  to the viewport and discards fully offscreen obstacles.
- **Interfaces/consumers:** `layout_native_galaxy_labels` retains its signature;
  Galaxy star, central black-hole and HUD avoidance all share this behavior.
- **Save/thread/performance:** presentation-only, no save migration or simulation
  changes. Owner-thread clipping is linear in obstacle count, works in place and
  reduces collision work. Double-precision intersection avoids endpoint overflow.
- **Tests:** the original error was reproduced on the user's copied developer
  campaign and by the new regression before the fix. Coverage includes distant,
  partially visible and enclosing obstacles, finite float extremes, and invalid
  geometry. Galaxy labels, stellar artwork and stellar eruptions suites pass.
  Packaged replay loads the same campaign, exercises wheel zoom/system entry and
  maximum close-up zoom in both views with zero reported label collisions.
- **Limits/reuse:** viewport bounds and invalid-geometry checks remain enforced.
  This corrects label clipping; other renderer/resource limits are unchanged.
  Future map overlays can submit offscreen footprints through the same contract.

## Persistent surface-attached stellar eruptions (2026-09-19)

- **Purpose/modules:** Core `stellar_activity` owns per-component profiles,
  counter-seeded exponential schedules, event timelines and CME associations.
  Engine `stochastic_timeline`, `surface_attachment`, `emissive_image` and
  `SurfaceEffect3D` provide reusable timing, radial meshes, transparency,
  interpolation, UV flow and analytical photosphere occlusion.
- **Interfaces/consumers:** `StellarActivityScheduler`,
  `apply_developer_stellar_activity`, `TravelingCmeLaunch`, `EruptionArtwork`;
  close Galaxy and System views share one event stream and presentation cache.
  Native developer tools and General eruption-quality controls use these APIs.
- **Save/thread/performance:** optional activity DTO/JSON migrates old saves at
  the current epoch, retaining new seeds/counters/coordinates/events. Core uses
  a due-event heap, bounded per-star history; application has four image jobs,
  six visible effects, bounded image/playback caches, no render-thread source
  preprocessing and no graphics-quality dependence in simulation.
- **Tests:** deterministic rates/rarity/CME/25× chunking; 65% full-save replay;
  420 class/type/variant combinations at four qualities; LOD, alpha and surface
  normals; GPU far-side/limb masking; actual two-view live continuity; old flare
  loop absence and planet/star regressions. All 373 sources audited, including
  the 12 distinct replacement A-class rising images.
- **Limits/reuse:** inferred visual stage pairing, single-frame generic families,
  estimated rotation, image-inferred volume rather than physical plasma, SDR output,
  future CME travel/hazard consumers; see [full report](STELLAR_ERUPTIONS.md).
  Attachment, timeline and material primitives can support storms, exhaust,
  impacts and volcanic activity without view-owned simulation.

## Consistent Solar System reference orientation (2026-09-19)

- **Purpose/modules:** the canonical native planet material previously treated
  a saved random axis node as screen roll, inverting Earth and other Sol globes.
- **Interfaces/consumers:** `native_planets::orientation` now consumes the
  existing `planet_presentation_pose` for Sol. Both live views, survey projection,
  picking and CPU portraits share the result; imported authored poses remain.
- **Save/thread/performance:** presentation correction only; physical metadata,
  artwork, save format, immutable resources and bounded caches are unchanged.
  No new Engine API, simulation, worker or draw pass is required.
- **Tests:** ten Sol identities across saved roll/tilt/phase variations,
  north/south portrait markers, 65-subclass material coverage, four targeted
  suites and native visual replay of Sol and imported worlds in both views.
- **Limits/reuse:** reference viewing poses are not an observer-specific physical
  pole projection; Uranus remains intentionally sideways. Manual inspection and
  procedural fallback poses remain. Reuse the canonical orientation in future
  consumers. See the [orientation report](PLANET_ORIENTATION_FIX.md).

## Restored lighting, multiple-star orbits and hourly time (2026-09-19)

- **Purpose/modules:** Engine linear-light materials and spherical thumbnails
  restore readable planet illumination; Core owns saved hierarchical stellar
  dynamics, stable planetary hosts and configurable clock cadence. Application
  renders every surveyed component and removes static rocky-belt photo strips.
- **Interfaces/consumers:** `Material3D::linear_light`,
  `SphericalThumbnailOptions::linear_light`, `StellarOrbitArchitecture`,
  `stellar_positions`, `stellar_planet_position`, `stellar_host_physics`,
  `stellar_host_accepts_orbit`, `reconcile_stellar_orbit_clearance`,
  `StrategicClock::set_days_per_second`, `format_campaign_time`. Generation,
  restore, developer commands, both planet views, portraits and the HUD consume
  these shared interfaces. Native 1x is one game hour per second; other Core
  consumers keep their default cadence unless configured.
- **Save/thread/performance:** optional versioned orbit records; deterministic
  migration preserves artwork and established planetary families. Analytic
  visible-system evaluation, grouped load repair, existing GPU pass/ownership,
  bounded asset residency and the 768-solid belt budget remain. Cosmetic tumble
  is independent of game speed and not persisted.
- **Tests:** 16 targeted suites, GPU colour readback, native developer replay,
  eight imported classes in both views, both belt types moving/paused, triple
  viewport fit, hourly/midnight/fixed-step checks, malformed-save rejection and
  a read-only 7,173-body campaign audit (187 binaries and 44 triples).
- **Limits/reuse:** restricted hierarchical Kepler dynamics; aggregate close-pair
  lighting/climate; existing primary-centred fleet navigation and moon-local
  motion; schematic spacing and exposure; environment-mapped ice optics. These
  shared orbit/material/time boundaries are reusable by later consumers. See the
  [implementation and validation report](LIGHTING_ORBITS_HOURLY_CLOCK.md).

## Clear orbital fields and stable artwork presentation (2026-09-19)

- **Purpose/modules:** Core small-body fields and planet appearances repair
  overlapping bands and misplaced frozen primaries; native system projection
  shares one radial scale for eccentric planet paths, belt particles and guides.
  Static imported globes face the authored hemisphere. The developer index finds
  imported artwork and species-appropriate unsettled habitable worlds.
- **Interfaces/consumers:** `place_small_body_field`, `reconcile_small_body_orbits`,
  `reconcile_frozen_planet_orbits`, `DeveloperPlanetFilter`,
  `planet_appearance_display_name`, `projected_orbit_point/path`, `advance_tumble`.
  Generation, restore, developer insertion, system chart, portraits and planetary
  inspection consume these shared boundaries.
- **Save/thread/performance:** deterministic load-time orbital repair preserves
  IDs, art, colonies and mining ledgers without a format change. Grouped system
  catalogs bound repair work. Cosmetic asteroid spin runs only for the visible
  system using capped real frame time, independent of strategic speed; existing
  768-solid/immutable-resource budgets remain. Stellar detail admission remains
  stable across draw order and long frames within four-image/two-job limits.
- **Tests:** cold primary/family repair and idempotence, full saved-campaign audit,
  belt eccentric clearance and Sol projection, constant-speed/paused tumble,
  imported/habitable index filtering, static authored portraits, star crowding
  with reversed order and long frame delays; native visual replay and save reload.
- **Limits/reuse:** approximate climate compatibility repair, schematic orbital
  spacing, unsaved cosmetic phase, reconstructed hidden hemispheres, procedural
  fallbacks where no images are approved, environment-mapped ice optics. Read the
  [implementation and validation report](ORBIT_ART_TUMBLE_FIXES.md).

## Fixed planet artwork and gradual star zoom (2026-09-19)

- **Purpose/modules:** shared native planet materials, globe and disc consumers
  keep all planets static and remove every added cloud overlay/shadow. Existing
  imported surfaces and manual 3D inspection remain. This supersedes automatic
  planet/cloud motion described in the earlier planet implementation reports.
- **Public interfaces/consumers:** `orientation` provides the fixed reference
  pose; `Artwork::append` accepts an optional stable presentation key. Galaxy-map
  primaries and companions use independent gradual blends through 8–48 pixel
  radii, including cached zoom reversals.
- **Save/thread/performance:** Core saves, clock, generation and original art are
  unchanged. Cloud image loading/draws are removed. Star transition state stays
  on the owner thread in a 1,024-entry LRU; existing four-image/two-job limits
  remain. No new shader, GPU pass or simulation subsystem is needed.
- **Tests:** all 65 planet subtypes at three LODs, static transforms, absent cloud
  layers/shadows, source pixels and portraits, legacy Sol, manual inspection;
  bounded star opacity steps during repeated/reversed zoom and independent stars.
- **Limits/reuse:** painted clouds stay in the images; prior hidden-hemisphere
  reconstruction and unsupported-subtype fallbacks remain. Optional per-object
  star transitions can be reused in other stellar views. See the
  [correction report](PLANET_STATIC_ART_REPORT.md).

## Authored ring detail and anisotropic sampling (2026-09-20)

- **Purpose/modules:** Engine ring material preparation preserves direct 2D
  source samples instead of averaging narrow gaps into radial strips. Scene3D
  adds a reusable bounded 8x anisotropic sampler for unequal texture footprints.
- **Interfaces/consumers:** `RingSourceOptions`, `prepare_ring_material`,
  `Material3D::anisotropic_texture`; imported rings, shared live planet assembly,
  developer giant laboratory, System View and planet inspection.
- **Save/performance:** no save or simulation changes. A close 1024 × 2048 ring
  is 8 MiB base plus 10.67 MiB GPU mips, charged to existing budgets. Shared
  sampler adds no per-frame texture uploads. System close allocations capped at
  two; smaller views use lower LODs; close geometry uses 512 segments.
- **Tests:** `engine_ring_material`, `native_scene3d_gpu`, `native_giant_visual`,
  `native_planet_materials`, `native_system_workspace`. Actual GPU images confirm
  radial contrast, transparent gaps, tilted views, distant averaging and reuse.
  All 162 prepared previews reviewed; ten families rendered through live code.
- **Limits/reuse:** single-view source lighting/geometry remain approximate.
  Source-limited detail, textured annuli and analytic shadows; no particle-scale
  simulation or volume scattering. Other tilted textured geometry can opt into
  the same sampler. See [report](RING_SHARPNESS_REPORT.md).

## Filtered 3D texture levels (2026-09-19)

- **Purpose/modules:** Engine Scene3D and the SDL/Vulkan adapter generate cached
  mip chains and use trilinear minification for planets, rings, asteroids and ice.
  Cloud longitude and environment seams preserve the correct pixel footprint.
- **Public interface/consumers:** `TextureMipLayout3D`, `texture_mip_layout3d`.
  Individual scene validation, combined-frame admission and GPU cache accounting
  share exact RGBA8 chain sizes. All native Scene3D consumers inherit filtering.
- **Save/thread/performance:** no save, generation, asset or clock changes.
  GPU owner-thread mip generation runs once per upload. Full chains count within
  the existing 192 MiB/128-entry texture limit; targets remain single-level.
  A square image adds about 1/6 to combined CPU/GPU residency; 1D rings about 1/2.
- **Tests:** GPU minification/motion, transparent ring gap and mean alpha, close
  zoom, cloud/environment seams, unchanged depth/optics/shadows, cache reuse,
  exact NPOT/1D allocation, cross-role deduplication and rejection before upload.
- **Limits/reuse:** isotropic UNORM channel filtering, no compression, temporal
  antialiasing or alpha-weighted color mip generation. The shared path is also
  usable by ships and other 3D scenery. See the
  [filtering report](PLANET_TEXTURE_FILTERING_REPORT.md).

## Oriented planet portraits (2026-09-19)

- **Purpose/modules:** Engine's CPU spherical preview now represents an oriented
  ellipsoid and transparent annulus. Native planet icons preserve saved tilt,
  spin, flattening, ring gaps, depth ordering and mutual shadows.
- **Public interface/consumers:** `SphericalThumbnailOptions` and the new
  `spherical_material_thumbnail` overload. Shared application pose/radius helpers
  supply both live meshes and portraits. The canonical material cache serves
  System View sidebars, Planetary Screen and colony command HUD icons.
- **Save/thread/performance:** no schema, RNG, timing or imported-image changes.
  Existing single CPU worker and 80-entry/96 MiB cache; 36,864 bytes per 96-pixel
  portrait, no per-frame regeneration or extra scene views. Four subpixels and
  eight fixed radial taps; generic output bounded to 16–1024 pixels. Existing
  GPU image residency rules apply to the resulting immutable RGBA image.
- **Tests:** known ellipsoid silhouette/axis, front/rear ring depth, opacity and
  clear gaps, fitting, mutual shadows, edge-on rays, determinism and invalid
  inputs. Integration checks canonical pose/radii, all ringed-giant families,
  cache reuse and actual portrait submission in the packaged native replay.
- **Limits/reuse:** fixed reference pose/light, surface-composited clouds; no
  detailed normal/specular response or atmospheric rim. Thin analytic rings and
  shadows retain the live renderer's approximation. The API can prepare other
  spherical/ellipsoidal asset icons without Core dependencies. See
  [the portrait report](PLANET_PORTRAITS_REPORT.md).

## Mutual planet/ring shadows (2026-09-19)

- **Purpose/modules:** the shared native planet assembly casts rings onto the
  surface, clouds and atmospheric rim, and the oblate planet onto its rings.
  Engine owns generic intersections and has no planet/campaign dependency.
- **Public interfaces:** optional `Material3D::shadow`, `AnalyticShadow3D`
  (ellipsoid or annulus), and `prepare_shadow3d`. World position, rotation, scale
  and radial/azimuth alpha maps control directional-light occlusion. Direct
  diffuse/specular light is shadowed; ambient, emission and environment radiance
  remain. Edges receive pixel antialiasing; ring alpha is filtered across its
  radial pixel footprint. Opt-in `two_sided_diffuse` lights thin ring undersides
  without illuminating the night side of solid planets.
- **Consumers:** canonical System View and Planetary Screen use `append_instances`.
  Ring radii, transparency/gaps, flattening, viewer pose and star direction use
  existing inputs. Cloud spin never rotates the ring plane. The later portrait
  extension also includes analytic ring/globe shadows in reference icons.
- **Save/thread/performance:** no new save fields, RNG draws, simulation clock or
  worker. Double positions are subtracted before GPU narrowing. One blocker per
  material, no extra render pass/draw calls; existing ring alpha is reused. Shadow
  images count toward scene and combined-frame 128-entry/192 MiB image budgets.
  Uniforms are 192 vertex/208 fragment bytes; seven sampler bindings.
- **Tests:** CPU transforms, large coordinates and validation; actual GPU pixels
  for rotated ellipsoids, front/behind blockers, ring holes/gaps/partial opacity,
  scale, parallel light, emission, mutual shadows, cache and resource budgets.
  Native material tests cover all LODs and cloud-relative pose; packaged replay
  verifies the canonical ringed globe submission.
- **Limits/reuse:** one directional-light blocker per receiver; analytic ellipsoids
  and zero-thickness annuli, not arbitrary mesh/particle shadows. No finite-star
  penumbra, multi-star shadows, volume scattering or interplanetary eclipses.
  Receiver extent is limited to one million blocker-normalized units. These
  generic primitives can serve other solid/ring consumers. See
  [the ring shadow report](PLANET_RING_SHADOWS_REPORT.md).

## Resolved planet subclass registry (2026-09-19)

- **Purpose/modules:** Core `planet_appearance.hpp/.cpp` resolves all 65 subclasses
  from normalized type and art configuration. Each record exposes base/subclass,
  orbital bands, surface temperature/heat, atmosphere pressure/composition/retention,
  water/ice/volcanism permissions, generation weights/percentages and separate
  approved, compatible and rejected image pools. Rejection metadata retains reasons,
  hashes and duplicate references for all 422 excluded images.
- **Public APIs/consumers:** `planet_type_registry`, `planet_type_record`,
  `planet_orbital_zone_definitions`, `planet_orbital_zone` and
  `rejected_planet_art_definitions`. Fresh generation consumes declared zones,
  atmosphere parameters, permissions, weights and image pools; the native developer
  index's VIEW RULES and `stellar_planet_type_registry` export read the same records.
  Export allowlists reject pool/status conflicts before copying materials.
- **Save/thread/performance:** immutable function-local statics initialize once;
  pools contain IDs into the stable art catalog, with a cached ID lookup during
  selection. No new per-frame work, threads or save schema. Existing appearances
  are never rerolled; new generation remains seeded. Source images remain offline.
- **Tests:** planet appearance tests cover all 65 records/examples, weight totals,
  665/422 disjoint primary pools, 17 duplicates, 70 geography rejections, 2,400
  physical cases, rejected-save IDs and persistence. Native developer tests exercise
  rules/example switching at 720p/1080p/2160p; eight exporter tests cover manifests,
  status conflicts, missing rejection reasons and tampering. Native replay captures
  the actual rules panel.
- **Limitations/reuse:** percentages are pre-eligibility baselines; irradiation
  bands are broad, conditional game categories. Climate and retention remain bounded
  approximations; configuration changes need a rebuild. Legacy/Sol exceptions are
  explicit. Future content/import tools can consume this registry instead of
  implementing private classification tables. See [the registry guide](PLANET_TYPE_REGISTRY.md).

## Canonical 3D planets and constrained planet taxonomy (2026-09-19)

- **Purpose:** use one persisted visual identity in System View and Planetary
  Screen; turn the 47-folder art collection into reviewed materials selected only
  after physical eligibility. 1,087 audited images, 665 approved materials,
  16 classes and 65 subclasses.
- **Engine APIs:** `spherical_material_preparation.hpp` converts caller-described
  discs into spherical maps with illumination correction, source-patch or zonal
  synthesis, separate cloud/emission and packed material masks.
  `SurfaceResponse3D` adds normal/property maps, relief and rotating cloud shadows;
  `Material3D` adds colored/intensity-controlled illumination and rim-shell alpha.
  `blackbody_light_color` is a three-band Planck rendering approximation. Existing
  sphere, directional-solid, annulus and immutable image resources are reused.
- **Core APIs:** `planet_appearance.hpp` owns the taxonomy, weights, physical
  eligibility, stable approved-art selection, climate validation and spin phase.
  `developer_planet_index.hpp` lists/resolves every subtype and generates checked
  examples through authoritative campaign state. Cracked bodies generate real
  parent-linked small-body fields. Source art cannot override water, ice, heat or
  atmosphere constraints; generation weights are configurable build inputs.
- **Consumers:** fresh generation, legacy-save activation, planetary persistence,
  native system workspace, shared planet globe, developer controls/full coverage,
  C++ import/review tools and the native exporter. Small UI portraits reuse the
  Engine spherical thumbnail projector and the canonical material worker/cache;
  they are fixed reference poses, with cloud/emission response and no ring mesh.
  No game taxonomy enters Engine.
- **Save/determinism:** optional per-body `PlanetAppearance` stores stable source,
  material, seed, class/subclass, climate, atmosphere, clouds, ring and rotation
  parameters. Missing records migrate without changing saved physical state;
  subsequent loads never reroll them. RNG is derived from stable seed/body IDs.
  Generation only modifies new physical worlds; existing settlement environments
  are preserved. UI reads use observer-filtered body snapshots.
- **Threading/performance:** single background CPU material job, 64 pending requests,
  80-entry/96 MiB owner cache, 128/256/1024-pixel material LOD, shared oblate meshes
  and bounded ring geometry. GPU uploads remain on the renderer thread, validated
  against combined-frame 128-entry/192 MiB image admission limits. 3,990 prepared
  map files are allowlisted and hash-verified; original renders are not runtime data.
  The owner thread must call `NativePlanetMaterialCache::poll()` once per update
  frame (`native_client/main.cpp` `update()`); polling is no longer lazy inside
  `request()`, so completed material jobs drain and `ready()` resolves even when
  no active view requests more materials.
- **Tests:** `planet_appearance`, `native_planet_materials`,
  `engine_spherical_material`, `native_developer_index`, `native_scene3d_gpu`,
  fresh/persistable campaigns, legacy migration, body persistence, system/planetary
  screens, small bodies and exporter tamper/path rejection. Packaged 720p/1080p
  replays compare eight real material instances across both views and save/load.
- **Limitations/reuse:** hidden geography is synthesized; normal/height/material
  masks are estimates. Rim atmosphere and cloud shells are not volumetric weather.
  No full multi-star radiative transfer, planet n-body integration,
  dynamic fracture simulation or generalized alien-city night maps. PNG disk
  compression does not imply GPU block compression. Generic material preparation,
  shader response, illumination and async caches can serve moons and other worlds.
  See [the implementation and audit report](PLANET_ART_IMPLEMENTATION_REPORT.md).

## Dielectric optics for solid materials (2026-09-18)

- **Purpose/API:** optional `Material3D::dielectric`, `Dielectric3D` in
  `native_scene3d.hpp`; environment and optical surface maps, IOR, roughness,
  transmission, absorption, thickness and derivative surface relief.
- **Subsystem:** immutable Scene3D validation, SDL GPU texture/sampler cache,
  native 3D vertex/fragment shaders and reproducibly embedded SPIR-V.
- **Consumer:** native frozen small bodies; legacy planet/ship materials retain
  their default shading. Application owns frost maps and material selection.
- **Save/threading:** presentation only; no save changes or new clock. Immutable
  resource generation is deterministic; GPU submission stays on the owner thread.
- **Performance:** no additional targets or passes; optical maps share the
  existing 128-entry/192 MiB texture budget, checked across all views before upload.
  Optical pixels use bounded environment cone filtering; disabled materials skip it.
- **Tests:** actual `native_scene3d_gpu` checks Fresnel, Snell bending, absorption,
  frost, GGX roughness, invalid optics and reuse; small-body tests cover real
  material integration, elongated forms and inclined chart-band alignment.
  Developer replay exercises real running/paused asteroid motion and disk reload.
- **Limitations/reuse:** environment-mapped, approximate optical thickness;
  no nearby-object ray tracing, caustics or volumetric scattering. Reusable for
  glass, frozen terrain, transparent minerals and other dielectric mesh consumers.
  See [the full report](NATIVE_ICE_OPTICS_REPORT.md).

This registry covers reusable native capabilities extended during the current
interface and developer-QA integration. **The integration is in progress.**
An entry is not evidence that the whole requested feature or release is complete.
See `engine/ARCHITECTURE.md` for the Engine → Core → Application dependency rule.

**Current scale/3D status (2026-09-18):** the native client uses only the new
planetary screen; the retired surface screen and its fallback paths are deleted.
Campaign setup supports 50,000 systems, player/developer writes stream JSON, and
3D combat movement, tactical corvette geometry and procedural globe relief have
real consumers. See [the latest integration report](NATIVE_3D_SCALE_INTEGRATION_REPORT.md)
and the final section below. Earlier entries record historical baselines; their
10,000-system cap, whole-document writer and planet-only consumer limitations
are superseded by this follow-up. The subsequent
[large-campaign loading update](NATIVE_CAMPAIGN_LOADING_REPORT.md) validates
complete save syntax, then decodes system/body arrays one record at a time.
The [save preparation update](NATIVE_CAMPAIGN_CAPTURE_REPORT.md) cuts measured
50k snapshot capture from 266 ms to 146 ms with unchanged serialized output.
Whole-file input, whole-world capture/state and full physical contact simulation
remain outstanding costs.

## Implementation rules

For a required capability gap, extend its authoritative subsystem first, add
automated checks, connect the UI, regression-test consumers, and record the
interface here. Do not substitute static data, a permanently disabled control,
duplicate simulation, or a private UI implementation of a reusable engine rule.
Review ownership, save compatibility, threading, determinism and performance
before changing a subsystem. Prefer incremental extensions and preserve existing
callers. Remaining gaps below are work outstanding, not approved deferrals.

## Simulation calendar and duration presentation

- **Subsystem:** Engine time presentation; Core campaign date policy.
- **Purpose:** Derive dates and readable durations from saved simulation time.
- **Public interface:** `engine/simulation_calendar.hpp`: `calendar_date`,
  `format_calendar_date`, `format_calendar_date_short`, `format_duration`.
  Core's `campaign_calendar.hpp` sets the campaign epoch to **2050-03-21**.
- **Current users:** Native strategic HUD, diplomacy, notification history,
  research, shipbuilding, construction, settlement, travel and upgrade estimates.
- **Save compatibility:** No new clock or schema. Existing elapsed days remain
  authoritative; existing campaigns display those days relative to the new epoch.
  This intentionally changes their calendar label, not their simulated progress.
- **Determinism/threading/performance:** Pure bounded calculations, no wall-clock
  reads or mutable state. Constant work per displayed value.
- **Known limitations:** English labels. Compact duration months are 30 days,
  years are 12 such months; dates use the Gregorian calendar and leap years.
  Research still advances at its existing 365.25-day rate per research year.
- **Tests:** `native_campaign_calendar` covers thresholds, singular/plural,
  leap/month/year boundaries, pause and restored clock values. Diplomacy and
  notification tests verify consumers use the shared epoch.
- **Future users:** Timeline, event inspector, replay/checkpoint browsers, logs.

## Textured triangle submission

- **Subsystem:** Engine native render commands / SDL renderer.
- **Purpose:** Draw a rotating textured planetary sphere using the same render
  submission boundary as other world objects.
- **Public interface:** `native_triangle_mesh.hpp`, `native_map_platform.hpp`:
  texture reference, UVs and vertex colors on triangle mesh commands; validated
  submission through `engine/src/native_map_platform.cpp`.
- **Current users:** Native map decals, markers, surface artwork and UI.
  The planetary globe now uses the depth-tested 3D path documented below.
- **Save compatibility:** Render-only data; no authoritative world changes.
- **Performance:** Bounded mesh and texture/cache budget; visible globe only.
- **Tests:** `native_triangle_mesh`, `native_client_platform`, planetary screen.
- **Known limitations:** Planetary province/resource simulation is separate and
  still needs the authoritative capability described under remaining work.
- **Future users:** Terrain overlays, region shading, textured UI/world meshes.

## Atomic ship construction batches and waiting-order movement

- **Subsystem:** Core shipbuilding, using existing authorization, reservation,
  order identity, queue and cancellation rules.
- **Public interface:** `shipbuilding.hpp`: `start_ship_build_batch`,
  `move_queued_ship_build`, `assess_ship_build_batches`. Batch quotes are
  projected by the controller; the UI does not independently calculate costs.
- **Purpose/current users:** Shipyard quantity and queue controls; controller
  integration is present, redesigned workspace integration is in progress.
- **Save compatibility:** Existing canonical active/queued order records. No
  second queue or alternative ownership/cost model.
- **Determinism/threading:** Simulation-owner operation; staged admission commits
  all requested orders or none. Active construction is not reordered.
- **Performance:** Batch size bounded by existing queue capacity. Staging copies
  mutable shipyard/economy/colony state; profile before increasing that bound.
- **Tests:** Existing shipbuilding admission/cancellation/persistence tests;
  `shipbuilding_start_assessment` additionally verifies cumulative batch quotes,
  no partial credit/population/ID reservation, population floors, queue overflow,
  active/foreign-order guards, promotion order and cancellation after reordering.
- **Known limitations:** Current campaign model has one yard per civilization.
  Multiple yards, levels and production lines require Core/persistence extension.
- **Future users:** AI procurement, production templates, developer simulation.

## Controlled asset projection and navigator

- **Subsystem:** Native observer-filtered projection and UI; existing Core entity
  IDs and ownership remain authoritative.
- **Public interface:** `native_controlled_assets.hpp`: `build`, `Key`, `Row`,
  `Navigator`; general UI preferences store independent category collapse state.
- **Current users:** Galaxy/system HUD, body/fleet selection, orbital yard entry.
- **Save compatibility:** Canonical IDs unchanged. Panel visibility/collapse are
  UI preferences, not world state.
- **Performance:** Cached metadata refresh and visible-row draw submission;
  no per-frame sorting or portrait generation for offscreen rows.
- **Tests:** `native_controlled_assets`: ownership/generation guards, stable IDs,
  search/collapse, selection, responsive bounds and 1,500-row virtualization.
- **Known limitations:** No independent station/occupation registry exists yet.
  These are required entity-model extensions, not fake navigator entries.
- **Future users:** Fleet/station browsers and contextual command selection.

## Campaign-owned research planning and scheduling

- **Subsystem:** Core Adaptive Research campaign state, funded commands and
  simulation; native research workspace consumes observer-safe projections.
- **Purpose:** Queue research once and let the authoritative simulation start it
  even when the UI is closed, including after moving or restoring a save.
- **Public interface:** `AdaptiveResearchPlan`, `ResearchPlanCommand`,
  `AdaptiveResearchCampaignState::plan/edit_plan`, and
  `AdaptiveResearchCampaignCommands::start_queued_research`.
- **Current users:** Research queue, favorites, recommendation preference,
  integrated campaign ticks; reusable by AI/developer orchestration.
- **Rules:** Known technologies only; 128 entries per ordered list; no duplicate
  reservation; normal funding, prerequisites, facilities, slots and labs. A
  blocked head stays first. Starting or concluding an entry removes it from the
  waiting queue. Zero-day/paused ticks make no changes. Suggestions never start
  research automatically; only explicit queued intent does.
- **Save compatibility:** Research campaign schema 3 stores nondefault planning
  in the existing Player17 save. Default plans retain schema-2 encoding for
  legacy compatibility. Schemas 1/2 load with empty plans. No UI sidecar or second
  queue remains. Session revisions protect stale commands and rebuild on load.
- **Determinism/threading/performance:** Simulation owner only, stable list order,
  bounded 128-entry work, existing cost/eligibility APIs; no wall-clock state.
- **Tests:** `native_research_controller` exercises ordering, stale commands,
  secrecy, idempotency, civilization isolation, insufficient funds, full-save
  round trip, malformed/oversized queues, paused clocks and headless advancement.
  Workspace tests cover existing controls and clipping. Broader regression is a
  release gate.
- **Related capability:** Active cancellation/refund is now a canonical command
  described below; ordinary queue removal still edits waiting intent only.

## Research cancellation, refunds and retained scientific work

- **Subsystem:** Core Adaptive Research state, runtime/authority, campaign funding
  and snapshots; native controller and research workspace are consumers.
- **Interfaces:** `cancelled_projects/cancelled_project`, runtime/authority
  `cancel_directed_research`, funded `cancellation_refund/cancel_directed_research`.
  Existing start commands restart cancelled work through normal eligibility,
  capacity, readiness and funding checks. No separate UI simulation.
- **Rules:** Active or paused programs can be cancelled. Their labs/slots are
  released; unused milestone reservations return to the same civilization's
  treasury once. Paid authorization, consumed milestones and operations are not
  refundable. Waiting intent for the same program is removed. Completed stages,
  partial work, earned capabilities, original context and unresolved hypothesis
  gates persist. Cancelled programs neither progress nor consume labs. Restarts
  pay new authorization and a new full milestone commitment; they cannot change
  applicability context or bypass missing facilities/scientific requirements.
- **Save compatibility:** A sparse cancelled-program archive lives in canonical
  research state. Core snapshot schema 6 adds `cancelledProjects`; versions 2-5
  identify pre-existing outer envelopes. States without cancelled work keep the
  byte-compatible schema-1 core encoding. All outer codecs and Player17 typed
  validation support the new nested record. Duplicate/overlapping projects,
  invalid progress/context and fabricated funding are rejected. No UI sidecar.
- **Presentation:** Active-row and inspector cancellation controls show the
  actual refund policy; cancelled work remains available to restart with its
  retained progress. Capacity blockers no longer hide scientifically eligible
  research cards while another program occupies the current slot.
- **Tests:** Funding regressions cover partial/exhausted/no reservations, active
  and paused cancellation, no-treasury refusal, repeated cancellation after load,
  actual progress through all three stages, context retention, malformed saves
  and paid restart. Controller tests cover stale commands, real prices, retained
  progress, full campaign JSON round-trip and capacity-only card visibility.
  Workspace tests route enabled/disabled controls at 720p/1080p/ultrawide/4K.
- **Performance/future reuse:** Sparse archive bounded by catalog nodes; no
  per-tick archived-project processing. Commands run on the simulation owner.
  Reusable by AI, scripted scenarios and future research portfolio management.
- **Limitations:** Cancellation history is not a full financial transaction
  journal. Research duration quotes still describe a full program at full funding;
  continuous forecast refinement remains separate from authoritative progress.

## Canonical research establishment and developer save isolation

- **Subsystem:** Core Adaptive Research, campaign persistence/save controller,
  native campaign startup/session; Engine access gate and modifier-aware input.
- **Purpose:** Research testing must grant real capabilities and must never turn
  test unlocks into an ordinary player campaign.
- **Public interfaces:** `AdaptiveResearchRuntime::establish_technology`,
  `initialize_developer_research`, `DeveloperResearchOptions`,
  `capture_developer_campaign`, `encode/restore_developer_campaign_json`,
  `PreparedPlayerCampaignSave::capture_developer`, `CampaignSaveKind`,
  `load_existing_developer_campaign`, `engine::DeveloperAccess`.
- **Current users:** Hidden developer new-game research checkboxes and the same
  native session save/autosave/backup pipeline used by player campaigns.
- **Rules:** Eligibility (`--devtools` or `STELLAR_CONTINUUM_DEVTOOLS=1/true`) plus
  explicit Ctrl+Shift+F12 activation; no research overrides in player setup.
  Unchecked research preserves the ordinary founding state. Normal/special
  classification comes from the existing catalog's `public_normal_research`.
  Completion uses canonical stage grants, capabilities, deployment evidence,
  funding reconciliation and queue cleanup. Repeated completion is idempotent.
- **Save compatibility:** Normal Player17 saves remain unchanged. A distinct
  version-1 developer envelope wraps the shared world/research codecs and tags
  the nested payload too. Player loaders reject either form. Developer saves
  use `.dev17.json` under the separate developer directory and retain tools-used
  and research-completion provenance. Backups never cross modes. Detached DTOs
  are captured on the simulation owner; JSON encoding and atomic writes remain
  worker operations. No copied live-world references reach the writer.
- **Tests:** Research controller tests pass completion, real unlocks, idempotency,
  unmodified unchecked state, loader rejection, malformed tags and round trips.
  New session/setup/UI regressions cover hidden controls, viewport containment,
  explicit activation, background writes, backup recovery and destination guards.
- **Performance:** Completion visits the catalog in deterministic depth/ID order
  once during setup. Normal play does not scan the catalog for developer flags.
- **Known limitations:** Current content defines 370 public normal technologies;
  no special technologies are authored yet. The separate special classification
  path is supported but cannot invent that missing content. The remaining
  developer console, coverage controls and long simulation tooling are separate
  required work, listed below.
- **Future users:** Authored scenarios, research migration tools, QA checkpoints,
  controlled test unlocks and hidden diagnostics in other native hosts.

## Bounded fixed-tick developer acceleration

- **Subsystem:** Engine `FixedClock`; Core `CampaignFrame` and `StrategicClock`.
- **Interface:** Fixed-clock snapshot/restore, `set_developer_speed`,
  `developer_ticks_behind`, and explicit committed fixed advancement.
- **Rules:** 250 ms of base time advances 0.25 campaign day; requested speeds are
  1/2/5/10/25. At most eight strategic ticks execute per rendered frame. Pending
  work stays in an integer accumulator; pause and menus neither consume nor add
  time. Exceptions preserve unprocessed debt and pause simulation.
- **Persistence:** Developer envelope stores speed, completed ticks and pending
  nanoseconds. The ordinary player clock and save format are unchanged.
- **Tests:** `developer_fixed_simulation` passes 1x/25x state equivalence,
  bounded work, backlog draining, paused/menu behavior and save/reload with debt.
  A real 1080p developer setup/play capture also passed. Tactical simulation
  uses the same reusable clock at 100 ms, bounded to 32 ticks per frame. Pending
  tactical ticks survive checkpoints. `step_developer` advances exactly one
  paused strategic/tactical tick through the normal event/save boundary.

## Civilization control policy and runtime continuation

- **Subsystem:** Core civilization control, strategic planning, research,
  construction, shipbuilding, exploration, colonization and diplomacy schedules.
- **Interfaces:** `CivilizationControlQuery`, `civilization_uses_ai`, campaign
  control projection, `set_developer_ai_control`, `StrategicRuntimeSnapshot`,
  `DiplomacyRuntimeSchedule`, and `CampaignRuntimeContinuation`.
- **Rules:** AI ownership of decisions is separate from player identity and
  territory. The existing civilization AI handles the player's empire when
  enabled. Returning control clears its cached strategic priorities immediately;
  existing legitimate orders/projects continue. No second AI or identity swap.
- **UI:** Hidden developer panel toggles human/AI control and the persistent DEV
  indicator states when AI control is active.
- **Persistence:** Developer saves include optional versioned runtime
  continuation: cached strategic plans, review deadlines and diplomacy schedule.
  Detached capture remains on the simulation owner, encoding on the save worker.
  Older developer envelopes omit this field and retain their legacy startup
  behavior. Normal Player17 encoding is unchanged. General snapshot/restore
  interfaces are reusable by future checkpoint/player-format revisions.
- **Validation:** Clock, campaign seed, civilization references, bounded plans,
  finite priorities and duplicate fields/IDs are checked before activation.
- **Tests:** Real three-empire 45-day run matches at 1x versus 25x with a reload;
  real funded player research starts only under AI control, player identity
  stays intact, human control resumes and invalid continuation references fail.
- **Limits:** This reuses the current AI's supported actions; it does not add
  missing station AI, diplomacy strategy or spectator input restrictions.

## Fleet command presentation

- Controlled Assets is the live fleet list. A selected fleet opens a compact
  left-side command card using existing order/quote authority. Unselected fleets
  leave no legacy panel or invisible hit area over the navigator.
- Galaxy label avoidance now reserves the real navigator and selected command
  card bounds. Existing standalone outliner callers remain compatible.
- Tests cover map picking, hidden-panel input, 720p/1080p/4K containment,
  selected-only content and canonical travel confirmation.

## Structured diagnostics and headless campaign QA

- **Subsystems/modules:** Engine `diagnostic_log` and shared `sha256`; Core
  `campaign_diagnostics`, opt-in `CampaignFrame` profiling; Application
  `developer_qa_host`. Research fingerprints delegate to the same byte digest.
- **Interfaces:** `DiagnosticLog::append/flush`, `DiagnosticLogPolicy`, typed
  `DiagnosticRecord`, `inspect_campaign_invariants`,
  `inspect_diplomacy_invariants`, `inspect_research_invariants`,
  `campaign_step_diagnostics`,
  `CampaignFrame::set_profiling_enabled`, `tick_execution_nanoseconds`.
- **Purpose/users:** The developer headless host runs the authoritative campaign
  frame, AI and research implementation. It records real events and measurements
  without opening a window or changing simulation decisions based on timings.
- **Boundaries:** Explicit `--developer-qa --headless --devtools`; new output
  directory required. Ordinary player saves are refused. Fresh QA uses the same
  current population generator as native setup. Normal rarity remains unchanged.
- **Outputs:** Exact executable SHA-256, seed/generator version/arguments,
  initial/final/three rotating periodic `.dev17.json` slots and atomic backups,
  JSON/Markdown summaries, command metadata, celestial report, bounded JSONL logs.
  Invariant failures stop execution and attempt a separate critical checkpoint.
- **Storage/threading:** Logger is owner-thread confined; default eight 4 MiB
  segments and 16 KiB records. Rotation only touches writer-owned files in its
  new directory. Oversize records carry truncation metadata. Counts remain in
  the summary even when old segments rotate. Save DTO/atomic writer is reused.
- **Save compatibility:** No new player schema. Runtime scheduling/fixed-clock
  continuation is captured in the existing isolated developer envelope. Profiling
  is transient. Completed QA runs verify exact canonical checkpoint round-trip.
- **Tests:** `campaign_diagnostics` validates fresh-world invariants, deliberate
  duplicate/orphan/nonfinite corruption, typed Unicode JSON, owner-thread refusal,
  existing-directory refusal, filtering, bounded records and segment rotation.
  `developer_qa_host` exercises real generation, simulation, daily invariants,
  rotating checkpoints, resume, final partial batches and 1x/25x equivalence.
- **Measured soak:** `work/qa-current-natural-100years/summary.json`: current
  500-system population, six civilizations plus one ancient, AI player takeover,
  144,000 quarter-day ticks / 36,000 days, 36,002 invariant checks, zero critical
  findings, exact final reload. 43.97 s wall time; mean tick 0.220 ms, worst
  4.485 ms on this host. Sampled private memory peaked at 13.48 MB and ended
  at 10.49 MB. Research and construction events occurred; no shipbuilding or
  exploration events occurred, so this does not validate those scenarios.
- **Limits:** No graphical FPS claim. Top-level strategic phase timings are
  implemented; fine-grained AI decision/pathfinding profiling, a comprehensive
  AI-stall detector, live log viewer and diagnostic ZIP remain. Existing
  events without a typed event kind retain their canonical message/IDs; they are
  not reclassified by guessing from text. The researched-content run completed
  72,000 ticks/18,000 days with zero invariant failures and exact final reload;
  it emitted 3 shipbuilding and 234 exploration events. Its slow exploration
  phase and fuel-stranded scout exposed the reach-batching and return-reserve
  work documented below. Original
  summary memory peak excluded the final sample; this reporting bug is fixed
  for subsequent runs (historical evidence is retained unchanged).
- **Future reuse:** Crash reports, CI soaks, long-campaign comparison, native
  developer console, scripted scenarios, diagnostic bundles and replay analysis.

## Deterministic stellar content coverage and privileged inspection

- **Subsystems:** Core population/founding, developer provenance and save codec.
- **Interfaces:** `ensure_stellar_coverage`, `developer_full_coverage` generation
  option, `validate_developer_coverage`, `build_developer_celestial_index`,
  `central_black_hole_with_state`, `set_developer_central_black_hole_state`.
- **Behavior:** Run natural generation first. Replace only surplus procedural
  non-rare primaries where a required category is missing; retain positions,
  requested system count, measured anchors and all naturally rare objects.
  Young/massive types prefer arms/star-forming regions. Planets and civilizations
  are then generated through the normal founding and hazard pipeline.
- **Provenance:** Marked developer sessions persist a coverage version and forced
  stable system IDs. Normal player writers/readers refuse these campaigns. Old
  developer saves without coverage metadata remain readable.
- **Consumers:** Native developer new-game checkbox and headless
  `--full-celestial-coverage`; developer index/report distinguishes natural,
  forced and total counts. The index refuses ordinary player worlds and does
  not reveal discovery knowledge.
- **Central object:** Reuse the one central SMBH. State changes preserve its
  position/mass/identity and update both authoritative metadata copies and jet
  geometry. Never inject another central SMBH. Native
  `native_stellar_observation.hpp::observed_central_artwork` resolves the existing
  quiet/accreting/jet artwork only for an observer who has explored the center.
  Developer inspection or access research alone does not reveal it. Existing
  sprite caches/LOD are reused; no new physical object or per-frame image decode.
- **Performance:** Bounded generation-only scan over 23 categories; no per-frame
  population mutation. Index creation is a read-only on-demand projection.
- **Tests:** `stellar_developer_coverage` covers all four galaxy sizes, stable
  placement, repeat-seed identity, anchor/rare preservation, idempotence,
  complete category counts, save reload, player isolation and central-state
  changes. Setup tests exercise privilege and intent propagation. These passed
  in the 201-test integrated run. `native_developer_index` checks 720p/1080p/4K,
  actual central-state commands, cancelled input and unchanged canonical state.
  Rendered 720p and 1440p captures verify setup flags, text input and map
  centering. Added central-state presentation tests cover all three states,
  unexplored/access-only redaction and a different observer.
- **Native integration:** The developer panel opens an eight-row virtual list
  with search/category selection, natural/forced counts, physical properties,
  central-state control and map centering. Campaign replacement discards the
  inspector; opening it never changes normal discovery or survey knowledge.
- **Remaining:** Additional planetary hazard coverage and broader developer
  inspector controls remain required. These are not approved deferrals.

## Phase profiling and scoped operational reach

- **Subsystems/modules:** Engine `phase_timing.hpp`; Core `campaign_coordinator`,
  `integrated_adaptive_campaign`, `fleet_reach`, exploration/settlement planners.
- **Reason:** A real later-game checkpoint spent most of its tick repeatedly
  rebuilding identical geometry/refueling lookup tables while scanning targets.
- **Public interfaces:** `PerformanceCounter`, `PhaseTimer::finish`,
  `set_profiling_enabled`, `reset_performance_counters`, `performance_samples`,
  `OperationalReachBatch::assess`.
- **Users:** Headless QA emits aggregate phase timings. Exploration, colony and
  resource-outpost planning scope one reach batch to one read-only operation;
  single-target assessments delegate to the same canonical calculation. Custom
  reach providers retain their existing authority and call order.
- **Compatibility:** No save format change or cross-tick decision cache. Live
  fleet fuel/range is read on each assessment; discard batches before mutating
  their borrowed geometry/colony/lane data. No scheduling, RNG, route scoring,
  mission timing or funding rule is derived from timing measurements.
- **Performance:** Counters have bounded storage, no per-sample allocation and
  no clock reads while disabled. Batched lookups avoid rebuilding one system
  hash table per destination. Before/after checkpoint results and measurements
  are release evidence, not an unconditional frame-rate promise. Matched
  800-tick continuations of the day-18,000 checkpoint dropped from 7.436 to
  1.697 ms mean tick (77.2%); exploration dropped from 6.616 to 0.994 ms.
  Canonical outcomes matched exactly apart from the save timestamp. Evidence:
  `work/qa-reach-comparison.json`. This isolates batching before the intentional
  AI fuel-policy change below; it is not a benchmark of every later change.
- **Tests:** Reach oracle/parity suites, `operational_reach_batch` over 500
  destinations/multiple fuel states and custom providers; profiling on/off/reset
  canonical-state comparison in `stellar_developer_coverage`; exact phase counts
  in `developer_qa_host`.
- **Future users:** Native profiler, mission previews, large-galaxy route tools,
  fleet/settlement planners and CI performance comparisons. Per-phase counters
  currently describe the strategic route, not detailed tactical/pathfinding GPU
  execution.

## Return-fuel reservation and shared service routing

- **Subsystems/modules:** Core `fleet_reach`, `civilian_recovery`,
  `exploration_planning`, `exploration_advance`, `campaign_coordinator`;
  `campaign_diagnostics` and the headless QA consumer.
- **Reason:** A real 50-year researched soak left an automated scout with
  insufficient fuel to return. One-way reachability was being used as though
  it guaranteed a sustainable exploration mission.
- **Public interfaces:** `MissionFuelPolicy::RetainReturnToService`, canonical
  `MissionReachAssessment::arrival_fuel_light_years`,
  `OperationalReachBatch::nearest_refueling`, fuel-policy arguments on
  exploration planning/simulation, and `inspect_campaign_operations`.
- **Users:** Production campaign exploration requires a valid outward route
  and enough projected arrival fuel to reach an owned refuelling settlement.
  If no such survey target remains, it uses the existing civilian return command.
  Manual missions retain explicit one-way reach rules. Low-level legacy fixture
  and custom-provider consumers retain their specified reach policy; requesting
  reserve planning from an incompatible custom provider fails explicitly.
  Return previews and orders share the same batched base-selection calculation.
- **Authority:** Actual fuel capacity, leg range, lanes, ownership and full/half
  settlement service remain authoritative. No injected fuel or teleportation.
  Already-stranded saves expose the canonical failed-return reason. QA emits
  bounded final operational findings separately from critical state invariants;
  an idle fleet alone is not labeled an AI deadlock.
- **Save impact:** No new persisted fields. Existing fuel, route and recovery
  flags/reasons already round-trip. Future AI decisions intentionally improve
  after loading; the population seed and generated object identities do not
  change. Reach projections and timing counters remain transient.
- **Performance:** Service-to-target route trees are reused and reversed on
  undirected lanes, then validated by the same fuel evaluator. This avoids
  constructing a route tree per potential destination. No persistent decision
  cache is introduced; newly owned or lost settlements affect the next plan.
- **Tests:** `exploration_fuel_safety` covers one-way versus return-reserve reach,
  exact arrival fuel, foreign/owned outposts, route-cache bounds, actual scout
  travel/reconnaissance/return/refuelling, new-base expansion, exhausted old ships
  and human-control isolation. Existing reach/recovery oracle tests remain.
  Diagnostics tests distinguish warning-level operational failures from corrupt
  state and never mutate fuel.
- **Integrated evidence:** `work/qa-reserve-regression.log`: 201/201 passed in
  185.03 seconds. `work/qa-fuel-reserve-25years/summary.json`: 500 systems,
  complete normal research, forced celestial coverage and AI takeover;
  36,000 ticks / 9,000 days, 9,002 invariant checks, zero critical or final
  operational findings, exact final checkpoint round-trip. The scout produced
  499 reconnaissance completions and finished at its home system with 1,200 ly
  of fuel. Mean strategic tick 0.578 ms, maximum 73.646 ms, 25.99 s wall time;
  these are headless measurements, not rendered frame-rate guarantees.
  `work/qa-old-stranded-detection/summary.json` resumes the earlier depleted
  scout: one explicit return-route warning, unchanged real fuel and exact reload.
  The final native 720p smoke (`work/developer-captures/index-final-720.log`)
  passed setup, generation, isolated saving, index search/focus and discovery
  preservation, with its rendered central-index capture visually reviewed.
- **Limitations:** Uses the existing shortest lane routes; it does not search
  all alternative fuel-stop itineraries. A ship already unable to reach any
  owned service needs a future physical rescue/supply capability; it is reported,
  not repaired by a QA override. Final operational snapshots are not a complete
  historical AI-stall detector.
- **Future reuse:** Mission previews, logistics/patrol reserve policies, AI
  decision explanations, a live operational warning panel and rescue dispatch.

## Shared diagnostic archives and current developer snapshots

- **Subsystems/modules:** Engine `diagnostic_bundle`, `diagnostic_log`,
  `runtime_paths`; Application `developer_diagnostic_report`, native support
  service/panel, and headless QA host. The existing native stored-ZIP writer
  was promoted to Engine; there is one archive implementation, not two.
- **Public interfaces:** `DiagnosticBundleEntry`, `DiagnosticBundleLimits`,
  `write_diagnostic_bundle`, `read_diagnostic_file`, `diagnostic_record_json`,
  `executable_path`; app `capture_developer_report`, `export_qa_directory`.
- **Purpose/users:** A hidden developer export action captures the actual current
  isolated campaign. Native F8 routes developer sessions to the same action.
  Ordinary support exports retain their last-completed-player-save behavior.
  Headless QA exports its completed session automatically. Reports contain build
  hash/provenance, generator/seed, typed findings or retained event logs, real
  population coverage, measured phase aggregates, and loadable checkpoints.
- **Safety/compatibility:** Normal Player17 saves and paths are unchanged. Live
  developer capture requires marked provenance and writes `latest.dev17.json`.
  No world references cross to the single asynchronous native writer. No
  simulation tick, reveal, repair, funding or research grant occurs during
  capture. The engine knows no game IDs or serialization schemas.
- **Bounds/performance:** At most 128 entries, 64 MiB per file and 256 MiB total
  snapshot bytes by default. File reads reject symlinks/nonregular sources and
  changed lengths. Relative ZIP paths reject traversal, reserved names, unsafe
  characters, duplicates and file/directory collisions before creating files.
  Unique directories and owned partial-file publication preserve prior exports.
  Headless files come from an explicit allowlist; no recursive user-file crawl.
  On-demand owner-thread snapshot encoding has campaign-size-dependent cost;
  disk/CRC publication runs off-thread in the native client. No export work runs
  per frame. Developer-only phase counters are bounded and remain unsaved.
- **Tests:** `native_support` exercises CRC/binary/nested-file round trips,
  unique names, bounds, path rejection, and ordinary/developer filenames.
  `native_support_service` exercises immutable single-worker snapshots, capture
  and write failure reporting, and explicit retry. `developer_diagnostic_report`
  checks state immutability, real coverage and exact exported checkpoint restore;
  `developer_qa_host` compares ZIP contents to its actual completed run artifacts.
  `native_developer_index` checks button capture/cancellation/busy behavior.
- **Limitations:** Native exports contain current findings, bounded canonical event history
  and session messages, not a complete historical AI decision journal. Headless rotation
  retains recent log segments and three periodic checkpoints; overwrite counts
  are reported. Full interactive command replay is not implemented. Checkpoint
  continuation explicitly requires the same executable/assets. Archives use
  standard ZIP storage without compression; no external runtime dependency.
- **Future reuse:** Bug-report uploads, continuous-integration artifact capture,
  replay bundles when command recording exists, and other bounded diagnostic
  file collections using immutable snapshots.

## Bounded native history and live diagnostics window

- **Subsystems/modules:** Engine `DiagnosticBuffer` in `diagnostic_log`;
  app `CampaignDiagnosticMonitor`, `NativeDeveloperDiagnostics`, developer
  panel, native frame-result consumer and diagnostic report builder.
- **Purpose/public interfaces:** `DiagnosticBuffer::append/clear/set_detail`,
  read-only `records/bytes/overwritten_records/filtered_records`;
  `CampaignDiagnosticMonitor::observe/reset/history`. Disk and memory logs
  share their typed encoder, severity/detail filtering and truncation rules.
- **Consumers:** Hidden **PERFORMANCE & DIAGNOSTICS** window shows measured
  canonical phase counts/mean/maximum CPU times, a refreshable recent-event
  snapshot, recording-level dropdown and retention/check counters. Native
  exports include `native-events.jsonl` plus explicit history metadata.
- **Authority/save behavior:** Observation consumes completed `CampaignFrame`
  results and the existing invariant/operational inspectors. It never ticks,
  repairs, reveals, grants resources, changes AI or serializes into game saves.
  Native session admission enables the existing fixed developer clock. Ordinary
  sessions bypass the monitor. Campaign activation clears prior history and UI;
  restored campaigns start a new observational session, not fabricated history.
- **Performance/limits:** Default 512 records, 2 MiB serialized history, 16 KiB
  per record. Structured records are retained alongside those bounded strings.
  No disk IO per native frame. State scans occur on the initial observation and
  when a completed frame reaches another simulation day. Identical active
  findings are deduplicated; resolved-and-returning findings can be recorded
  again. A frame spanning several days is inspected at its completed boundary,
  not retroactively at states the observer cannot access. UI rows are virtual;
  event snapshots are copied only on open/refresh. Full record text is exported.
- **Tests:** Shared diagnostic tests cover rotation, byte/record bounds,
  Unicode/oversize records, recording levels and wrong-thread rejection.
  Report tests compare monitored and unmonitored deterministic continuations,
  normal-mode isolation, paused deduplication, real operational warnings, reset
  and immutable export snapshots. `native_developer_diagnostics` tests dropdowns,
  hidden-panel routing, read-only behavior and 720p/1080p/ultrawide/4K bounds.
- **Limitations:** No complete AI candidate/score journal, interactive command
  replay, GPU timings or comprehensive long-term stall detector. The native
  monitor now pauses and captures the first critical finding using the native
  service described below. Recording
  detail is transient developer UI state. `core_total` includes its child
  phases; samples are CPU observations and never imply a guaranteed frame rate.
- **Future reuse:** Live warning navigation, broader entity inspectors,
  bounded command/event journals and CI/runtime diagnostic comparisons.

## Native first-critical pause and immutable capture

- **Modules/interfaces:** `CampaignDiagnosticMonitor::first_critical`, app
  `NativeDeveloperFaultCapture::observe/poll/reset`, shared report builder and
  `NativeSupportService`. The first critical record is latched independently of
  history filtering/rotation.
- **Consumers:** Isolated native developer campaigns pause both clocks, open
  diagnostics and export one report per campaign activation. A banner and pause
  menu expose success, the report location or a concrete error. Ordinary player
  campaigns do not activate this behavior. Manual exports use a separate worker.
- **Authority/saves:** No automatic repair. Capture preserves the failing state
  when encodable; invalid checkpoints produce an explicit partial report with
  findings and encoding failure instead of a false resumable checkpoint. Existing
  save schemas and normal save locations are unchanged.
- **Bounds/threading:** One asynchronous write plus one immutable pending
  snapshot at most. Campaign replacement does not misattribute an older result.
  Capture occurs once on the owner thread; the writer receives no live world
  references. Failures are surfaced rather than automatically retried.
- **Validation:** `native_developer_fault_capture` covers real Core critical
  findings, pause/isolation, immutable ZIP/CRC data, repeated observations,
  generation replacement, backpressure, capture and disk failures. The native
  720p developer smoke exercised a real invalid-treasury finding, paused the
  simulation and produced a readable critical diagnostic ZIP.
- **Limits/reuse:** This is failure preservation, not historical command replay
  or a full AI decision journal. The immutable capture service can serve future
  runtime invariant observers without coupling Engine to game rules.

## Shared concept-art UI skin and responsive workspaces

- **Engine interfaces:** `native_ui_skin.hpp` supplies clipped chamfered gradient
  `surface`, `control` and `progress` drawing through the existing colored triangle
  submission API. `text_fit.hpp` supplies renderer-measured UTF-8-safe ellipsis.
  Neither owns input, gameplay, assets, campaign state or persistence.
- **Consumers:** Shared menu/UI styles, the top navigation header, planetary
  command, guided research and ship construction. Panels use dark blue glass,
  thin cyan borders, selected/hover lighting and the existing approved art.
  Research cards use cached 512-pixel images and crop without distortion;
  ship construction uses a responsive grid with up to four illustrated columns.
  Planetary command keeps the textured globe, real statistics and construction
  commands with an illustrated overview and large bottom actions.
- **Layout/input:** `native_workspace_top` reserves the navigation header across
  workspaces. Header input cannot reach the map. Existing modal controls still
  take precedence. Readable research sections scroll under fixed action controls;
  costs and authoritative reasons are not silently truncated.
- **System fit:** Connected travel-arrow labels retain their lane bearing and
  fixed pixel size. Collision resolution moves them only the distance necessary
  to clear another arrow/label, avoiding excessive outward jumps at 720p.
  Canonical warp-in/out positions and the orbital boundary remain unchanged.
- **Performance/saves:** Bounded vertex geometry per control, existing texture
  caches, no new disk reads per frame or simulation/save schema changes. Text
  fitting measures actual renderer widths; it is presentation-only.
- **Validation:** Native workspace/layout tests cover 720p through 4K bounds,
  header and modal input, reachable long research descriptions/costs, grid
  actions, globe/slot commands and crowded real-system lane geometry. Rendered
  Vulkan captures verify actual art, cropping and the shared skin. Such captures
  do not establish a universal 60 FPS guarantee.
- **Limits/reuse:** This visual integration does not supply missing planetary
  regions, research/shipyard mechanics or other gates below. Other native screens
  can reuse the skin without recreating private drawing conventions.

## Engine limitations remaining / integration gates

- Real planetary regions, per-region data and functional map layers beyond
  current planet-level state require Core/persistence work.
- Multi-yard construction, upgrades and production lines require extending the
  canonical shipyard model before controls can be completed.
- Developer access, research setup, isolated saves, strategic acceleration and
  shared civilization-AI takeover are implemented. The broader developer
  console, full inspector controls, spectator controls, resource
  and exploration overrides and full QA presets remain
  in progress. Tactical acceleration, bounded logs and headless soak execution
  are implemented. Full coverage generation and the celestial index are tested.
  The broad QA preset and comprehensive AI stall checks remain incomplete.
  Automatic exploration now preserves return fuel and uses normal refuelling;
  physical rescue for already-stranded ships still needs an authoritative
  supply/recovery capability. Passing numerical invariants is not AI-behavior
  completion.
- Cross-screen interaction and current 1080p/1440p captures, full regression and
  relocated Windows package checks remain release gates.

The completion report must include **ENGINE CAPABILITIES ADDED / EXTENDED** and
**ENGINE LIMITATIONS REMAINING**, with affected modules/APIs, consumers, tests,
save and performance impact, and future reuse. Do not ship or report this batch
as complete while its required capability gaps remain unresolved.

## Verification evidence — diagnostics integration (2026-09-17)

`work/developer-diagnostics-final-regression.log`: 203/203 checks passed in
183.74 seconds before the research-cancellation extension. Native diagnostics,
export and celestial-index flows captured at 720p and 1440p. The separate
2,500-system, 80-tick run in `work/qa-diagnostic-huge-2500` passed with zero critical
or final operational findings and exact checkpoint round-trip. Its 48,326,479-byte
ZIP (11 entries; 23,737,196-byte latest checkpoint) passed independent ZIP CRC
and byte comparison. These are bounded scenario checks, not full-playthrough
or universal FPS guarantees.

## Sandbox morphology, population and paired artwork (2026-09-17)

- Purpose: Sandbox now routes through a six-card, 3 × 2 morphology page and a
  separate population dropdown before species, seed, size and civilization
  settings. `native_galaxy_creation.inl` consumes Core configuration; it does not
  implement population sampling. Existing story-mode availability is unchanged.
- Core APIs: `GalaxyGenerationConfig`, `resolve_galaxy_configuration`,
  `galaxy_generation_stream`, `galaxy_visual_pair`, `galaxy_footprint_frame`,
  `galaxy_has_central_black_hole`, `galaxy_configuration_description`.
  Stable IDs, requested/resolved population, seed, size, civilization roster,
  species, developer coverage and generator/art/profile versions define a
  SHA-256 fingerprint. Configurable morphology weights resolve Random through
  a dedicated deterministic stream. Copy Setup and developer Galaxy Details
  expose reproducible settings and the fingerprint.
- Engine: immutable `DensityMask` samples a bounded scalar field using bilinear
  interpolation; invalid/out-of-bounds coordinates return zero. No image I/O or
  game rules reside in that Engine component. Future terrain/resource placement
  can reuse it.
- Import: `import_galaxy_assets.py` classifies normalized filenames, rejects
  duplicate/ambiguous identities, verifies complete same-morphology pairs,
  preserves source aspect, and bakes cached 96 × 96 masks. A deterministic
  artwork transform fits the measured home neighborhood without changing its
  coordinates. Galaxy extent grows with the square root of system count.
- Consumers: native setup uses only Stars Included previews; the galaxy
  renderer uses only the paired Gas-Dust Only layer behind actual systems.
  Existing procedural morphology, regional population modifiers, star physics,
  hazards, rare-object hooks and fog-of-war remain authoritative. Artwork masks
  reject empty margins before minimum-distance acceptance; they do not assign
  stellar classes.
- Persistence: optional canonical configuration extends existing generation
  metadata, preserving legacy save compatibility. Actual saved stars, rare
  objects, resolved population, pairing and central occupancy restore directly.
  Normal central occupancy is optional and deterministic. Full-coverage developer
  sessions deliberately force one eligible center for QA and retain the
  developer coverage flag in configuration identity; normal play does not.
- Performance: bounded masks are parsed once, placement is offline generation
  work, and aspect-preserving images are decoded/cached by the existing artwork
  loader. Source resolution is retained externally; runtime images are bounded
  to 1280 pixels on the longest side and stay below the preparation byte limit.
- Tests: `galaxy_configuration` covers morphology/state/size generation,
  deterministic reproduction, persistence, footprint/anchors, fingerprint
  validation and optional SMBHs. Native setup/startup tests exercise selection,
  six-option dropdown, Back/Next, reset, and 720p/1080p/1440p/ultrawide/4K layout.
  `native_galaxy_backdrop` exercises all six real map layers and alpha edges.
  `test_galaxy_asset_import.py` validates filename parsing, source mapping,
  hashes, aspect, masks and export inclusion.
- Art limitation: the supplied 12 files contain six generic pairs and no
  explicitly population-qualified pairs. All 30 morphology/state combinations
  use validated same-morphology generic fallback, logged in developer
  diagnostics; population still changes actual generation. See
  [exact mapping](GALAXY_ASSET_REPORT.md). No missing state variants were invented.
- This extension does not close the separately listed planetary, shipyard,
  developer-QA and release-package gaps above.

### Widescreen asset and overview follow-through

- Both Irregular and Spiral pairs now use reviewed landscape edits. Full edited
  sources are retained under `assets/source/galaxies-16x9`; the existing importer
  emits exact 1280 × 720 runtime images. `export/galaxy-asset-edits.json` validates
  edit paths and SHA-256 hashes so a later import cannot silently revert them.
  Original user files remain unchanged. Prompts, tool method and saved paths are
  recorded in [GALAXY_WIDESCREEN_ART.md](GALAXY_WIDESCREEN_ART.md).
- The offline footprint fit checks every bilinear mask cell intersecting the
  measured home neighborhood, including cell corners. This fixed one narrow
  Irregular dust-lane rejection without relocating measured stars. Runtime mask
  sampling and generation costs are unchanged.
- `galaxy_artwork_fit_camera` and `NativeGalaxyBackdrop::fit_camera` accept an
  optional content rectangle. Native overview framing reserves the header,
  navigation and Controlled Assets panel, keeping the whole galaxy visible.
  The existing whole-viewport default remains available to other consumers.
  At overview the map remains fixed; closer views retain camera panning.
- Actual fleet management inspection also exposed an invalidated snapshot
  iterator and an overlapping star inspector. `execute_asset(Fleet)` now
  captures navigation values before changing systems and clears star selection
  when opening fleet controls. No gameplay or save schema changed.
- Final evidence: `work/galaxy-final-regression.log` passed 205/205 native tests
  in 205.22 seconds; `work/galaxy-package-tests-final.log` passed 68/68 export
  tests; `work/galaxy-widescreen-assets-final.log` passed 3/3 asset-import tests.
  Six actual creation runs span 720p, 1080p, 1440p, ultrawide and 4K. Six actual
  paused reloads verify overview/regional/system transitions with unchanged
  campaign day. The fleet hold/resume, locate and illustrated shipyard smoke
  passed in `work/concept-fleet-recovery-reviewed.log`.
- Limits: no population-specific source artwork was supplied, so the documented
  generic fallback still applies. These bounded checks do not establish all
  gameplay completion, universal 60 FPS, or relocated release-package approval.

### User-supplied overview background (September 17 follow-up)

- The native overview now uses `assets/visual/space/deep-field-v3.png`, copied
  byte-for-byte from the user's 2944 × 1648 replacement. It remains the fixed
  screen-space layer behind the selected main galaxy and fades out at closer
  map zoom; solar-system backgrounds are unaffected.
- `NativeGalaxyBackdropAssets::ArtworkSource` carries a per-source decoded-byte
  budget. The full-resolution deep field has a 20 MiB bound; other galaxy layers
  retain the 8 MiB bound. Preparation reserves that amount on the existing
  32 MiB worker queue, respects backpressure, and caches the result once. This
  keeps the supplied sharp detail without increasing the shared queue limit.
- Export allowlists, SHA-256 checks and packaged provenance select the new file.
  Existing queue/cancellation tests now exercise both memory bounds and preserve
  full source resolution. No Core generation, simulation, seed or save changed.
- The galaxy manifest is now a CMake configure dependency, so existing build
  trees refresh asset copy commands when a background is replaced. Source and
  built-copy hashes match. Follow-up evidence: 2/2 image preparation/backdrop
  tests, 68/68 export checks, all six actual 1080p overview/regional/system runs,
  and a 3840 × 2160 Spiral run passed (`work/galaxy-background-*.log`). The earlier
  205-test complete regression predates this presentation-only follow-up.

## Procedural galaxy phenomena and inherited system environments

Historical simulation foundation. The supplied-artwork extension below replaces
the procedural visual material, generation tuning and presentation budgets in
this entry. Organic geometry, overlap authority and gameplay remain shared.

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: generated nebulae and other spatial environments now belong to the
  saved C++ galaxy, with shared membership, appearance and initial exploration
  effects. Detailed implementation and file inventory:
  [PROCEDURAL_PHENOMENA_IMPLEMENTATION.md](PROCEDURAL_PHENOMENA_IMPLEMENTATION.md).
- Engine module: `organic_region.hpp` supplies reusable seeded noise, six organic
  silhouettes, broad-phase rejection and signed radial edge sampling, including
  the inner hollow boundary of remnant shells. It contains no game identifiers.
- Presentation modules: `procedural_gas.hpp` supplies warped filaments and dust
  pockets; `texture_coverage_mesh.hpp` applies a world coverage callback with
  screen-space texture coordinates so camera zoom never enlarges the gas detail.
  The existing textured `TriangleMesh` can now be submitted in ordered world
  commands. Consumers include region clouds and the translucent central veil.
- Core interfaces: `generate_galaxy_phenomena`, `validate_galaxy_phenomena`,
  `phenomenon_context`, `nearest_phenomenon`, `phenomena_diagnostics` and
  `phenomenon_footprint_density`. Definitions are embedded from
  `data/stellar/phenomena-v1.json`. Ten types reuse `StellarDiscoveryHooks`.
  Canonical generation weights counts/types/anchors by size, morphology,
  population, stellar regions, young stars and remnants; the existing artwork
  footprint and central exclusion constrain the complete sampled cloud bounds.
- Consumers: fresh-campaign generation, payload validation/save/load, exploration
  advancement, galaxy rendering, system backgrounds and tactical backgrounds.
  Sensor discovery range and survey work now consume bounded regional modifiers;
  seeded anomaly/resource opportunities augment real systems and bodies.
- Native interfaces: `NativePhenomena` owns cached immutable presentation data,
  a shared map gas material, one current system texture and optional debug atlas.
  Map/local images are 1536 x 864; the system image stays viewport-sized at all
  zoom levels. Local alpha is capped at 0.28. Shared `ImagePreparationQueue` handles
  preparation, cancellation and bounded reservations. `local_visual_multiplier`
  applies visual-density, camera and combat readability rules. Low/Medium/High
  preferences persist in General Settings; developer Ctrl+Alt+N exposes the saved
  regions, masks, membership, affinities, effects and visual overrides.
- Persistence: `GalaxyGenerationMetadata.Phenomena` saves all region geometry,
  seeds, colors, effects, hooks and system membership. New configuration v3
  includes the feature; v2 fingerprints and saves remain accepted, without
  backfilling old galaxies or rerolling their stars. Save validation rejects
  malformed fields, unsupported enums and geometry/membership disagreements.
- Performance: maximum 160 regions; circle broad phase and cached view contexts;
  no per-frame texture generation. Normal map plus local pixels occupy 10.125 MiB;
  the adaptive debug atlas has a separate 24 MiB bound. Equal-type local overlaps
  combine into at most ten gas families. The cached central veil uses 4 MiB.
  Coverage meshes are clipped before sampling and share texture identities. Timings and
  complete regression/visual evidence are recorded in the implementation report.
  High-detail central fog also uses worker preparation, with bounded capacity,
  cancellation and retry; it no longer generates during scene drawing.
- Tests: `galaxy_phenomena` covers deterministic generation, morphology/population
  tendencies, all ten types in the sampled seeds, zero-region worlds, save/load,
  legacy behavior, invalid data, real survey advancement, overlap caps, shared
  color, weaker edges, clear systems, seed-stable local masks, transparent atlas
  borders, layer order, combat/zoom attenuation, fixed texture scale across zoom,
  and query cost. Central fog detail/transparency are covered by backdrop tests. Existing
  `galaxy_configuration` tests cover every supported size/type/state combination;
  `native_general_settings` exercises density dropdown, Save/Cancel and persistence.
- Final evidence: full native build and 206/206 CTests passed (208.82 seconds);
  seven directly affected tests passed after the final fog-worker extension.
  Actual 720p/1080p/4K overview/regional/system captures passed and were reviewed.
  1080p steady mean was 16.692 ms, p95 19.230 ms. Full logs and timing qualifications
  are recorded in the implementation report; there is no universal FPS claim.
- Future reuse: organic region sampling is suitable for other bounded spatial
  effects. Core phenomenon contexts and existing discovery hooks allow later
  mechanics to consume the same saved region authority rather than inventing
  independent UI or simulation clouds.

### ENGINE LIMITATIONS REMAINING

- Radiation/attrition, movement, shield stress, colonization, research interest,
  combat concealment and special-event execution remain explicit data hooks.
  This restrained pass activates exploration penalties and discovery biases.
- Rendering uses cached layered 2D masks. Radial edge distances are not exact
  shortest distances to arbitrary contours, and footprint containment is sampled.
  Debug heatmaps show actual cloud density; affinity labels show placement bias,
  rather than simulating a second probabilistic spawn map.
- Host measurements do not certify universal 60 FPS. The separately recorded
  planetary, shipyard, full developer-QA and release-package gates remain open.

## Supplied phenomenon artwork, distribution and local inheritance

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: use all 48 supplied nebula files as an available art library while
  keeping procedural simulation authoritative. Full inventory, exact file/family
  mappings and 20-part completion report:
  [PHENOMENON_ART_IMPLEMENTATION.md](PHENOMENON_ART_IMPLEMENTATION.md).
- Generic Engine APIs in `texture_decal.hpp/.cpp`: `DecalMapping::uv`,
  `prepare_decal_texture`, `DecalBlendProfile`, `decal_lod_width`,
  `masked_decal_mesh` and `append_decal_batch`. These provide world-coordinate
  art mapping, aspect preservation, cached luminous/obscuring alpha, perimeter
  feathering, area-filtered LODs and ordered compatible batching. They use the
  existing native image/mesh contracts and have no Core dependency.
- `SpatialRegionIndex` in `spatial_region_index.hpp` supplies reusable immutable
  AABB broad-phase queries with stable vector-index payloads. Consumers use it
  for visible-region culling and local overlap candidates; occupied-cell scans
  avoid huge empty overview-grid traversals.
- Core interfaces: `PhenomenonVisualAsset`, `phenomenon_art_catalog`,
  `phenomenon_art`, `assign_phenomenon_art`, `phenomenon_art_role`,
  `phenomenon_art_inventory`, `phenomenon_art_usage`, `phenomenon_weights` and
  `natural_phenomenon_count`. The manifest is embedded from
  `data/stellar/phenomenon-art-v1.json`; distribution tuning from
  `data/stellar/phenomena-v2.json`. Original v1 definitions remain for old saves.
- New generation uses nine functional families, configurable system-count bands,
  population count/composition modifiers, morphology count/composition modifiers,
  regional affinities and existing footprint containment. Natural games do not
  force rare types. Explicit developer Full Content Coverage may force nine.
- Native consumers: `NativePhenomena` renders saved artwork on the actual galaxy
  map and derives local crops from the same source and system world coordinates.
  Eligible backgrounds render behind system/tactical objects. Supernova remnants
  and rare energetic features are map-only. The central undiscovered veil uses
  a supplied diffuse asset selected by manifest role, preserving discovery rules.
- Map detail now grows with world zoom and remains visible at overview; local
  system backgrounds keep a fixed apparent texture scale. Uniform mappings
  preserve source aspect and hue. Dark profiles retain obscuring dust. Local
  alpha is capped at 0.28; map combined coverage strength at 0.68. No normal
  phenomenon or core veil uses the former procedural gas artwork.
- Performance: 256/768/2944-pixel LODs, lazy worker preparation, 256-pixel fallback,
  at most three full-detail map regions, a 96 MiB prepared-art cache, and the
  existing 32 MiB outstanding image queue. Local composites are 1536 × 864 and
  use the strongest three eligible layers. All simulation overlaps remain.
  Decode/filter scratch and GPU copies are additional transient/resident memory.
  Core queries retain circle rejection, native contexts are cached per system,
  and adjacent shared textures batch without disturbing dark/light layer order.
- Save impact: configuration v4/phenomena v2 persist asset IDs, mirroring and
  generation-weight diagnostics alongside geometry, seeds, effects and membership.
  Legacy enum values are stable; appended entries add Diffuse, Mixed and Rare.
  v3/v1 fields retain their simulation and use a deterministic visual fallback.
  v2 worlds are not backfilled. Save/load never rerolls artwork.
- Build/export: `NativePhenomenonArtAssets.cmake` validates checksums and copies
  all originals. `import_phenomenon_assets.py` validates filename classification,
  categories, uniqueness, dimensions and eligibility. Native export packaging
  consumes that same manifest rather than maintaining another texture list.
- Developer controls: Ctrl+Alt+N exposes bounds/types/density/overlap/filename,
  affinity labels, visual override, Previous/Next/Go To and full usage reports.
  The persistent player control is Space phenomena density, default Medium.
- Verification: full native build, 206/206 regression checks, 3 importer checks,
  68 export/runtime checks and 3 existing galaxy import checks passed. Affected
  tests were rerun after the final developer overlay/region-table corrections.
  Tests cover all 48 decoded assets, edge masks, dark blending, rarity/count
  trends, deterministic save/load, legacy compatibility, overview/world UVs,
  local eligibility/readability, navigation and unchanged gameplay preferences.
  Actual 720p/1080p/4K overview/regional/system captures were reviewed; a legacy
  save also rendered the supplied central veil. The final 1080p steady sample
  averaged 17.150 ms, p95 19.745 ms. Detailed logs and caveats are in the report.
- Future reuse: these generic decal/mask/LOD and spatial-index interfaces can
  support dust fields, environmental overlays, territory masks and other bounded
  world artwork. New gameplay may consume the existing saved overlap contexts.

### ENGINE LIMITATIONS REMAINING

- 2D decals retain stars baked into the supplied images. There is no volumetric
  simulation; footprint fit is sampled and organic edge distance is radial.
- Local presentation uses three eligible layers and full-resolution map detail
  has a three-region cap. Cold preparation is asynchronous and can take seconds.
- This artwork pass initially retained the 2,500-system UI limit; the subsequent
  larger-campaign extension below raises it to 10,000. Count formulas cover the
  requested 100,000+ bands, without certifying campaigns at that size.
- Exploration modifiers and discovery biases are active; other existing
  radiation/attrition, movement, combat, colonization and research-interest values
  remain data hooks where consuming mechanics are still absent.
- No universal FPS or full release-export claim. Existing unrelated migration
  and release gates remain separate from this completed artwork integration.

## Larger campaigns, route performance and separate star-map scenery

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: support selectable 5,000/10,000-system native campaigns and accelerate
  their generation, navigation and marker rendering. Measurements, compatibility
  evidence and captures: [LARGE_GALAXY_ENGINE_REPORT.md](LARGE_GALAXY_ENGINE_REPORT.md).
- Core's `galaxy_catalog.hpp` owns `full_galaxy_system_counts`,
  `maximum_full_galaxy_system_count` and `supported_full_galaxy_system_count`.
  Generation/configuration validation, native setup and territory input consume
  this contract. Six size buttons fit the existing 720p new-game workspace.
- Generic Engine `SpatialRegionIndex::query` adaptively visits local cells or
  occupied cells, retains stable exact-filtered results and rejects invalid
  bounds. Consumers include stellar placement and phenomenon visibility and
  overlap queries. Huge empty queries never iterate unbounded cell ranges.
- Core indexes compatibility/diversity targets and first bodies once per
  generation. Spatial random draws are explicitly sequenced to retain the
  existing canonical result. Procedural naming uses deterministic suffixes past
  5,000 names instead of exhausting the finite combination pool.
- `InterstellarLaneNetwork` keeps its public API, exact backbone, three-neighbor
  topology, permissions, tie rules and owner-thread cache. Compact indexed
  backbone data, duplicate-edge lookup, partial neighbor sorting and a distance/
  ID priority queue accelerate build and route traversal. No topology migration.
- `NativeGalaxyStarMarkerRenderer::append_neutral_batch` consumes the existing
  Engine `TriangleMesh` interface with a small padded atlas. It retains every
  visible neutral star, tint/selection/contrast, clipping and primitive order;
  ten thousand neutral stars split into four meshes under 65,536 vertices each.
  The atlas remains inside the 14-resource marker cache budget.
- `NativeGalaxyBackdropAssets::{star_background,request_star_background}` adds
  a distinct cached, asynchronously prepared regional background. Overview
  retains the original `deep-field-v3.png`. The supplied `star-background.png`
  fades in only with regional map zoom, stays behind nebulae/objects, and uses
  an aspect-preserving screen cover independent of world pan/zoom. One-pixel
  overscan prevents seams. Neither map background enters system/planet views.
- Build/export preserves the supplied image byte-for-byte and validates both
  background assets and their credits through the reviewed asset manifest.
  Each full-resolution background has a 20 MiB preparation bound; admissions
  still obey the shared 32 MiB outstanding queue and retry after capacity frees.
- Save impact: no schema bump or reroll of loaded campaigns. Tests pin the
  pre-change 2,500-system payload and round-trip full 5,000/10,000 catalogs.
  Native smoke waits for actual save completion rather than assuming large
  saves finish after a fixed number of rendered frames.
- Validation: background coverage/transition/async tests; 10,000-marker geometry
  tests; adaptive spatial results against brute force; all-six-morphology larger
  generation; lane and existing campaign parity; new-game UI, real 8X simulation,
  save completion and paused reload. Full native build, 209/209 native tests
  and 88/88 export/runtime tests pass. Measurements are in the linked report.
- Future reuse: adaptive spatial queries support other bounded overlays;
  immutable navigation catalogs reuse the route improvements; existing Engine
  mesh/texture contracts support additional adjacent compatible sprite batches.

### ENGINE LIMITATIONS REMAINING

- Supported campaigns stop at 10,000 systems. Exact lane backbone construction
  remains quadratic; this does not certify 100,000-system worlds.
- Persistence still materializes whole JSON/world snapshots. Large saves take
  seconds and can cause capture/update spikes; streaming persistence is absent.
- The 4,096 territory anchor/claim cutoff from this pass is removed by the
  follow-up below. Fresh-campaign tests do not certify late-game scale, every
  GPU or sustained universal 60 FPS.
- Supplied nebulae remain 2D decals with three local overlaps and three full-
  detail map regions. Cold artwork preparation and other previously documented
  missing gameplay consumers and release gates remain open.

## Territory and campaign-save scaling follow-up

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: remove the 4,096 visible-anchor/claim rejection for supported large
  galaxies and eliminate redundant whole-document save conversion. Full design,
  measurements and evidence: [TERRITORY_AND_SAVE_SCALING_REPORT.md](TERRITORY_AND_SAVE_SCALING_REPORT.md).
- Generic Engine `SpatialPointIndex::{nearest,maximum_influence}` provides
  immutable 2D nearest-site and weighted radial-field queries, stable input-order
  ties, self/partition exclusions, optional candidate counts, finite range
  validation and concurrent allocation-free reads. It has no Core dependencies.
- Native territory's direct and worker projection paths share observer-filtered
  capture. Friendly radii, ownership, continuous borders, owner preservation and
  fog consume the index. All captured anchors and competing claims remain;
  the shared Core catalog cap, fixed grid, image cache and async publication
  rules remain. Source fingerprints avoid repeated DTO copies on unchanged
  frames and include known-system bits as well as survey levels.
- Core `detail::encode_galaxy_payload_v16_document` and private player document
  composition let player/developer writers extend an ordered document directly.
  Public encoder signatures, strict UTF-8/numeric/date validation, JSON bytes,
  schema and save/backup/recovery behavior remain compatible. Normal saves,
  immutable background saves and developer checkpoints use the same encoders.
- Tests: exhaustive versus indexed float queries; pre-change geometry/fog
  fingerprints; observer and async invalidation/failure regressions; a worker
  fixture with 10,000 anchors and 20,000 claims; old/new player byte comparisons,
  Unicode/invalid-text/null sections and developer continuation composition.
- Validation completed: full native build, 210/210 native regressions and 17/17
  profile-tool checks. A real 10,000-system 8X campaign advances from day 85 to
  165, completes mid/final saves and separately reloads paused. Its 1,200-frame
  active sample averages 16.766 ms with p95 17.079 ms; this has one visible
  territory and does not certify late-game load. Report links include reviewed
  overview/regional captures and machine-validated save/time evidence.
- Performance: the 2,500-system/500-colony cold territory update falls from
  2,903.880 ms to 462.452 ms; 10,000 anchors complete in about one second with
  1.83 MiB of retained imagery. Unchanged async checks in the settled 10,000-site
  fixture fall from 5.140 ms to 1.340 ms. Identical 88 MB player-save encoding
  falls from median 2,967.08 ms to 1,584.74 ms across three trials on this host.
- Future reuse: generic nearest-site and radial-field consumers can share the
  Engine index; other Core persistence envelopes can share validated document
  composition. No authoritative simulation rules or campaign state are moved
  into presentation or Engine.

### ENGINE LIMITATIONS REMAINING

- 10,000-system campaign cap and quadratic lane backbone remain. Whole-world
  capture, full JSON memory, disk flush costs and streaming persistence remain
  unresolved. No peak-memory reduction has been measured.
- Territory still samples a fixed grid per owner; thousands of separate owners
  and pathological coincident geometry are not performance-certified. Synthetic
  settled-map checks do not certify a long late-game simulation or universal FPS.
- Existing nebula detail budgets, missing modifier consumers and unrelated
  migration/release gates remain open.

## Native C++ 3D rendering and persistent GPU resources

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: render real 3D geometry alongside existing 2D scenes and move repeated
  planetary mesh construction, transforms and lighting onto the GPU. Design,
  measurements and evidence: [NATIVE_3D_ENGINE_REPORT.md](NATIVE_3D_ENGINE_REPORT.md).
- Generic Engine `native_scene3d.hpp` / `native_scene3d.cpp` provide immutable
  `Mesh3D` and `Scene3D`, indexed position/normal/UV geometry, quaternion and
  uniform-scale instances, materials, perspective/orthographic cameras and
  `prepare_instance3d`. Camera-relative double subtraction precedes GPU float
  conversion; conservative sphere/frustum culling rejects offscreen work.
  Geometry validates once; scene construction rejects malformed or excessive
  input, including tiny scales/projections that could underflow or overflow.
- Native Engine `native_scene3d_gpu.cpp` extends the existing SDL/Vulkan device
  with cached vertex/index buffers, cached textures, D32 depth, opaque and sorted
  transparent passes, directional lighting and optional double-sided materials.
  `Scene3DView` composes in either ordered 2D world or overlay command list;
  independent viewport targets preserve ordering without CPU readback. Backend
  resources initialize only when needed and stay on the window owner thread.
- `Window::scene3d_statistics()` exposes uploads, draws, culling, cache occupancy
  and estimated resident/target bytes. RAII releases resources on exceptions;
  bounded caches retain immutable owners. Combined frame budgets are checked
  before allocation, preventing multiple individually valid views bypassing
  resource limits. Failure is explicit rather than silent scene truncation.
- First consumer: `NativePlanetGlobe` shares one 128-by-64 sphere across surface,
  city-light and cloud materials. Existing real-body inputs, region projection,
  selection, drag/zoom, day/night controls and observer filtering remain. No
  Core or simulation dependency is added to Engine, and save schemas are unchanged.
- Build: maintained GLSL sources and embedded SPIR-V, regenerated by
  `tools/compile_scene3d_shaders.py`. `cmake/NativeScene3D.cmake` verifies source
  and generated-header hashes during native configuration. The reviewed compiler
  provenance is recorded; players and normal builds need no shader SDK/compiler.
- Performance: the same 1,000-frame three-layer globe preparation sample falls
  from 0.691710 ms to 0.021616 ms mean CPU time (about 32 times faster). This
  excludes GPU execution/present and is not a whole-game FPS claim. Repeated
  frame tests prove unchanged geometry/textures do not upload again.
- Validation: full native build and 212/212 regression tests pass. New CPU tests
  cover geometry, winding, cameras, clipping, culling, rotations, large-coordinate
  precision and invalid inputs; actual Vulkan pixel tests cover depth ordering,
  transparency, UVs, lighting, clipping/culling, world/UI composition, independent
  views, resizing and resource limits. Planetary tests check matrix alignment
  and existing interaction/layouts through 4K. The actual game passes construction,
  cancellation, save and paused reload at 720p, 1080p and 1440p, with reviewed
  captures. Final focused checks and log paths are recorded in the report.
- Future reuse: terrain, ship inspection and tactical views can share these
  mesh/material/camera/depth interfaces; they do not need private CPU-projected
  substitutes. Existing 2D maps, nebulae, backgrounds and UI remain supported.

### ENGINE LIMITATIONS REMAINING

- The planetary globe is the integrated 3D consumer. This does not convert all
  gameplay views to 3D or add physics/collision, asset import, skeletal animation,
  shadows, PBR, nonuniform transforms or a hierarchical scene graph.
- Transparency uses center-based sorting; intersecting transparent surfaces
  are not order-independent. One camera-space directional light is supported;
  mipmaps and dedicated 3D MSAA remain absent.
- Bounds: 262,144 vertices / 786,432 indices per mesh, 4,096 instances per scene,
  8 views per frame, 128 unique meshes and textures each across a frame, 64 MiB
  geometry cache, 192 MiB 3D image cache, 128 MiB combined color/depth targets.
  Cache estimates include retained CPU payload plus GPU data, not all allocator,
  staging, in-flight or driver overhead. Existing 2D caches remain separate.
- The 10,000-system campaign cap, quadratic lane backbone, whole-document save
  memory costs, nebula detail limits and other documented migration/release
  gates remain. No universal frame-rate or late-game performance certification.

## 3D gameplay consumers, sole planetary interface and 50,000-system campaigns

### ENGINE CAPABILITIES ADDED / EXTENDED

- Purpose: integrate 3D ships, terrain and motion/query primitives into the
  existing native application, extend catalog scale, and reduce save memory.
  Full ownership, measurements and evidence are in
  [NATIVE_3D_SCALE_INTEGRATION_REPORT.md](NATIVE_3D_SCALE_INTEGRATION_REPORT.md).
- Engine `physics3d.hpp` exposes finite-checked 3D vectors, acceleration-limited
  kinematics and continuous segment/sphere and segment/triangle queries. Core
  fixed-step combat consumes motion in XYZ; the planetary globe consumes mesh
  collision queries for picking. Planar compatibility paths preserve old golden
  results. Core owns combat rules, authorization and persisted state.
- Engine `native_geometry3d.hpp` constructs extruded convex hulls, heightfields
  and radial terrain meshes and finds the nearest mesh/segment intersection.
  Native tactical corvettes submit hull/deck instances to the existing GPU 3D
  renderer. The new globe displaces known generated solid planets, shares cached
  geometry and projects selections onto its relief. Sol artwork without height
  data, gas giants and unknown planets remain smooth. Heightfields are a reusable
  tested primitive without an active ground-screen consumer.
- Retired native surface UI/source, building/relief presentation, fallback
  routing, build targets, packaged-art declaration and runtime probes are removed.
  `NativeColonyWorkspace` hosts only `NativePlanetaryScreen` and its freight
  confirmation. Build/manage/cancel still use the canonical construction
  controller and Core costs/state. Navigation and speed shortcuts respect both
  planetary and freight confirmations.
- Engine `SpatialIndex3D` supports immutable 3D nearest queries and partition
  exclusions. Core uses it for a deterministic Boruvka backbone and nearest
  links above 10,000 systems, preserving the prior graph algorithm below that
  threshold. The native knowledge frontier shares the index. Lane storage holds
  only IDs/positions; route-cache capacity adapts to catalog size. Setup/content
  support 25,000 and 50,000 across the existing morphology choices.
- Large-catalog distant unvisited markers use compact cores with four vertices
  instead of 26. All systems remain rendered/selectable, while known and selected
  systems retain their detailed markers. The actual final 50,000-system map run
  averages 17.277 ms/frame (p95 18.061 ms) on this host, including cold/save/view
  transitions; a 249.494 ms save-service spike remains. This is not a universal
  FPS or hitch-free claim.
- Engine `AtomicTextSink`, `AtomicTextProducer` and
  `write_file_atomically_stream` add bounded buffered output with existing atomic
  replacement/backup guarantees. Core `stream_player_campaign_v17_json` and
  `stream_developer_campaign_json` serialize validated records for real manual,
  automatic and developer saves. Schema stays compatible; optional combat Z
  defaults to zero and zero is omitted. Snapshot capture stays on its owner;
  immutable prepared saves write on the existing worker.
- Engine `clip_image_to_viewport` crops unrotated image destinations and source
  coordinates before submission. The territory consumer now survives a local
  zoom that projects its galaxy atlas past 65,536 pixels, without relaxing backend
  validation or moving the territory. The fixed regional star background and
  distinct overview artwork remain in their established layers.
- Tests: full native build and 209/209 native regressions; final nine focused
  checks; 68 export/tool checks; indexed versus exhaustive neighbors, reordered
  lane determinism, 25k/50k generation/routes/save/reload, byte-identical player
  and developer encoders, failed-write preservation, depth/order round trips,
  legacy combat goldens, mesh normals/hits, extreme image clipping and input
  ownership. Actual game runs cover planetary construction/save/reload at three
  resolutions, freight/navigation, tactical order/reload, and 50,000-system
  creation plus overview/regional/system navigation after reload.
- Measurements: 50,000 systems / 355,519 bodies generate in 33.55 s, first lane
  build 1.57 s, capture plus streamed write 9.07 s, reload/checks 6.68 s in one
  headless fixture. A separate 10,000-system serialization benchmark lowers
  process peak working set from 403.12 MiB to 88.73 MiB (about 78%) while writing
  the same 87,945,352 bytes. These are measured fixture results, not general FPS
  or late-game guarantees.
- Future reuse: other tactical models, surface queries, spatial navigation and
  persistence envelopes can consume the Engine primitives and Core serializers
  without restoring the removed surface interface or duplicating game rules.

### ENGINE LIMITATIONS REMAINING

- Current supported maximum is 50,000, not unlimited. Generation and full-catalog
  presentation still have costs. Long late-game and universal 60 FPS remain
  uncertified.
- Full DTO capture and whole-file input remain; saves are still large,
  uncompressed JSON. The loading follow-up below avoids materializing complete
  system/body JSON arrays. Streaming writes do not make every save/capture instant.
- The physics integration is 3D motion and continuous queries, not rigid-body
  contacts, ground vehicles, physical terrain dynamics or an N-body solver.
  Triangle picking scans bounded meshes and caches unchanged pointer/view hits.
- Detailed authored 3D models/elevation for every ship/planet remain absent;
  corvette art stays capped at 32 and other formations keep their current symbols.
  Generated relief is presentation-only. Existing province-resource and ground
  combat gaps remain.
- Existing scene/GPU budgets, transparency ordering, lighting/shadow/PBR gaps,
  nebula overlap/detail limits and unrelated migration/release gates remain.


## Alpha 0.1.13: indexed nearby-world planning and release validation

The local alpha is packaged and validated; see
[the final report](releases/0.1.13-alpha-validation.md) for the exact artifact,
210 native tests, 405 tooling tests, 27 exported gameplay validators, final 50k
save/reload/navigation replay and extracted-archive launch. This remains an alpha
with the limitations below.

**Purpose / ownership:** Core's nearby habitable-world guarantee planner now
builds a single immutable index of eligible solid planets by system. Both its
greedy pass and constrained fallback use that index. `apply_nearby_habitable_guarantees`
remains the public interface; candidate order, seeded fallback selection and
species viability still follow the canonical rules. Native new campaigns and
headless generation consume this path through `create_founding_catalog`.

**Determinism / threading / saves:** the index borrows input bodies only for the
synchronous call, orders each system's planets by stable ID and never relies on
hash-map iteration to select a world. Viability reads staged state after previous
assignments. There is no new worker, state schema or save field. Memory is one
pointer per eligible planet plus bucket overhead, released after planning.

**Tests / performance:** `engine_generation_50000` fingerprints the complete
440,690,994-byte baseline campaign encoding (seed 8057, six ordinary civilizations,
one ancient). Existing civilization/founding parity fixtures cover both assignment
paths. The isolated generation benchmark fell from 36,229.4 ms to 3,588.75 ms on
this machine, with the same byte count and fingerprint. This is generation time,
not total startup time or a rendered-frame guarantee. The suite also retains real
25k/50k lane, routing and streamed save/reload checks.

**Save reliability:** the internal `JsonStreamWriter` now owns its output callback
instead of borrowing a possibly temporary `std::function`. This prevents dangling
callback access after construction and rejects empty sinks immediately. A regression
constructs the writer directly from a temporary lambda and verifies complete JSON.
All existing atomic-failure and whole-save roundtrips remain required.

`is_developer_campaign_save_path` classifies the ASCII suffix using UTF-8,
without converting Unicode filenames through the Windows ANSI code page. This
fixes New Game activation and save-controller configuration in non-English
folders while retaining case-insensitive player/developer isolation. Real file
write and boundary regressions cover Unicode names; the packaged New Game replay
creates, saves and reloads a Unicode sibling without overwriting its anchor.
Automated startup failures now exit with their cause instead of waiting forever
on the human-facing error screen. Interactive recovery remains available.

**Application / export:** Alpha 0.1.13 uses engine 0.1.62. Export includes a player
README and versioned release notes, identifies the native game executable, and
writes a ZIP SHA-256 sidecar after validation. Native galaxy checks require the
supplied star image only in regional view, preserve overview art, reject retired
synthetic points and ensure system-view isolation. Their failure regressions are
maintained. The expanded complete CTest suite has a 900-second exporter deadline;
individual runtime/test deadlines are unchanged. The reviewed celestial asset
allowlist includes the new planetary portraits/panorama and all three Earth globe
maps plus provenance; missing or changed files block export. Fleet input replays
use the new Controlled Assets Manage arrow and avoid HUD-covered route targets.
Versioned battle evidence appends extensions without truncating dotted versions.
Selected fleet portrait evidence counts the existing command-card image as well
as legacy outliner rows, with tests for selected and hidden command cards.

**Limitations / future reuse:** candidate-system scans and species assessments
remain; indexing is local to this planner, not a new global cache. Large save
capture and whole-file loading still retain whole-world memory. Campaigns
remain bounded at 50,000 systems, and collision queries do not yet provide a
rigid-body/contact solver. Future planners can reuse this grouping strategy;
new caches must preserve ownership, stable selection and invalidation. Full native
parity/Steam presets remain blocked. Local alpha packaging does not certify a
separate clean Windows device or publish a hosted release.

## Validated deferred JSON arrays and large campaign loading (2026-09-18)

### ENGINE CAPABILITIES ADDED / EXTENDED

- **Purpose / ownership:** move the existing ordered JSON parser from Core into
  reusable Engine infrastructure, with opt-in deferred arrays. Engine owns UTF-8,
  syntax, nesting and source positions; Core retains all campaign schemas,
  duplicate-property policy, diplomacy/research validation and activation rules.
- **Interfaces:** `engine/json_document.hpp` exposes `json::Value`,
  `ParseOptions::deferred_array_pointers`, `parse_ordered_json`, `array_size` and
  `visit_array`. Selected arrays borrow immutable input; the complete document is
  syntax-checked before returning, and a later visit materializes only one array
  element at a time. Default parsing remains fully owned. Ordered duplicate
  members, numeric lexemes, strict UTF-8, 64-level depth limit and original
  byte/line locations are preserved. There is no global mutable parser state.
- **Consumers:** Galaxy16, Player17 and developer campaign restoration select
  system and planetary-body arrays. Native startup and primary/backup recovery
  consume the same loaders. Developer restoration passes its already parsed
  campaign directly to the shared restoration routine, removing the intermediate
  whole-campaign serialization and second root parse. The galaxy DTO is released
  before research-runtime restoration. No alternate simulation or UI-only state.
- **Save compatibility:** no schema/version or emitted-save changes. Developer
  nested conversion errors now retain original-file byte offsets instead of
  offsets in a reserialized inner campaign. Existing stage order, developer/player
  isolation, callback exception identity and primary/backup behavior remain.
- **Performance:** for the existing 50,000-system, 353,781-body Player save,
  process peak working set fell from 2,545,926,144 to 821,202,944 bytes (67.7%).
  Measured restoration was 6,159.58 ms before and 6,151.73 ms after. Both
  recaptures emitted 439,218,730 bytes with FNV-1a `15530417222981827440`.
  These are one-process local load-and-recapture measurements, not general
  startup, FPS or late-game guarantees.
- **Tests:** `engine_json_document` covers eager/deferred equivalence, duplicate
  keys, escaped selectors, original locations, invalid UTF-8/escapes/numbers,
  exact depth boundaries, visitor exception identity, repeat traversal and
  10,000 records. Existing Galaxy/Player JSON parity and backup-recovery checks
  exercise the new path. `developer_fixed_simulation` additionally checks a
  malformed deferred system reports its exact source-file position.
  All **211 native tests passed** after rebuilding; an actual 50k native
  load/save and overview/regional/system navigation replay passed, and a 50k
  developer load peaked at 821,288,960 bytes. See the loading report for logs.
- **Future reuse:** large ordered catalogs, replay arrays and inspection tools
  can use the Engine API without depending on game rules. A consumer must retain
  immutable input storage until its last deferred visit.

### ENGINE LIMITATIONS REMAINING

Input is still a complete in-memory UTF-8 file; only selected arrays are deferred.
Each deferred record is parsed during validation and again during conversion.
One unusually large record, research state, fleets or unknown unselected fields
can still require substantial memory. Whole-world DTO capture/restoration and
large uncompressed saves remain. This does not change the 50,000-system ceiling,
save-capture hitch, physical terrain/rigid-body limitations or full-migration
gates. See the [loading report](NATIVE_CAMPAIGN_LOADING_REPORT.md) for evidence.

## Linear parent-chain validation and leaner save capture (2026-09-18)

### ENGINE CAPABILITIES ADDED / EXTENDED

- **Purpose / module / interface:** Engine `parent_chain_index.hpp` exposes
  `ParentChainIndex(span<const optional<size_t>>)`, `size()` and
  `reaches_cycle(index)`. It classifies cycles and all descendants leading into
  them in O(nodes) construction time, with O(1) queries and one owned byte per
  node. Missing parents terminate chains; out-of-range input/query indices throw.
  Construction uses no recursion or per-chain visited allocations.
- **Consumers / ownership:** Core's `capture_planetary_bodies` and
  `restore_planetary_bodies` map external identities once and consume the Engine
  analysis while preserving their canonical validation order. Capture projects
  directly into final DTO storage; restore validates optional DTOs in place.
  Galaxy reference maps reserve catalog capacity. Player/developer persistence,
  manual/autosave controllers and native load/recovery use these same paths.
- **Determinism / threading / saves:** results own their state, retain no input
  references, have no mutable global data, and permit concurrent const queries.
  Core still iterates catalog order and owns all game-specific rules and errors.
  Snapshot capture remains on the campaign's owning thread, followed by immutable
  background writing. No schema/version change, skipped validation, altered
  output ordering, live-state worker access or backup-policy change.
- **Performance:** seven captures of the same 50,000-system, 353,781-body
  campaign had a median of 266.185 ms before and 145.603 ms after (45.3% lower).
  Both emitted 439,218,730 bytes with FNV-1a `15530417222981827440`. This is
  local capture time excluding destruction and writing, not an FPS guarantee.
- **Tests:** exhaustive independent-walk comparison for 8,477 small graphs,
  owned lifetime/bounds checks and 250,000-node chains in
  `engine_parent_chain_index`; 32 planetary golden cases plus sparse/shuffled
  identity, cycle, missing-parent and error-precedence regressions; existing
  galaxy reference and campaign save-controller tests. All **212 native tests
  passed** after rebuilding every linked consumer, including 25k/50k campaigns
  and GPU checks. The actual native 50k load/save/navigation replay passed
  (153.109 ms save-service update frame); a 50k developer save also retained its
  prior serialized size and fingerprint. See the
  [capture report](NATIVE_CAMPAIGN_CAPTURE_REPORT.md) for validation evidence.
- **Future reuse:** scene hierarchies and single-parent catalogs can share the
  Engine index while retaining their domain rules. It is immutable analysis,
  not an incremental cache or general multi-edge dependency graph.

### ENGINE LIMITATIONS REMAINING

Whole-world capture still pauses the owning thread (about 146 ms in the measured
50k case); it requires a complete detached DTO and final live state. Whole-file
load input, uncompressed saves, the 50,000-system ceiling, full physical terrain
and rigid-body contacts, and broader native migration gates remain outstanding.

## Supplied stellar artwork at close zoom (2026-09-18)

### ENGINE CAPABILITIES ADDED / EXTENDED

- **Purpose / ownership:** App presentation extends the existing Engine image
  queue, immutable image resources and ordered Vulkan drawing; no parallel
  simulation or Engine-to-Core dependency is introduced.
- **Interfaces / consumers:** `observed_stellar_artwork` in
  `native_stellar_observation.hpp` admits only fully surveyed identities and
  resolves legacy spectral classes when physical records are absent. Galaxy
  primaries/companions and `NativeSystemWorkspace` consume the same resolution.
  `star_screen_radius(scale, visual_scale)` limits system stars to source-detail
  size without lowering planet navigation zoom. `append_selection` draws a rim
  rather than tinting the photosphere. Earlier LOD transition and larger deep
  map sizing expose the supplied surface.
- **Saves / performance / threading:** no campaign mutation, schema change,
  reclassification or new per-system texture. The four-close-image cache,
  two-pending-request bound and immutable background preparation remain.
  Overview/neighborhood marker sizes through 64x are unchanged. The star radius
  cap prevents extreme texture magnification; it is not a new FPS guarantee.
- **Validation / reuse:** artwork, galaxy marker, system workspace and observer
  tests plus real Vulkan captures cover legacy Sol, physical precedence,
  redaction, resolved source detail and maximum zoom. The expanded galaxy-art
  replay checks close-ups as well as existing layering/navigation. Shared
  observer resolution can serve future stellar inspection views. See the
  [close-up report](NATIVE_STELLAR_CLOSEUP_REPORT.md).
  All 16 related tests and packaged 720p/1080p close-up replays passed, including
  spectral-only Sol; packaged developer campaign creation also passed.
- **Developer entry:** App `--dev-game` opts into the existing Engine
  `DeveloperAccess` gate and activates it before the startup host is constructed.
  The Developer Game launcher therefore opens an already marked developer menu;
  New Game > Sandbox uses the existing authoritative developer campaign setup
  and separate save directory. The `--devtools` shortcut path remains available.
  No simulation, persistence schema or ordinary-launch eligibility changes.
  Four startup/developer regressions and a packaged `--dev-game` creation/save
  replay passed with no key chord or automation-driven developer activation.

### ENGINE LIMITATIONS REMAINING

Supplied resolution limits fine detail; no physical 3D stellar surface or new
protostar asset is added. Legacy classes lacking finer measurements use their
representative class artwork. Four distinct close texture types per frame and
existing simulation/50k-scale limitations remain.

## Developer full exploration and empire monitoring (2026-09-18)

### ENGINE CAPABILITIES ADDED / EXTENDED

- **Purpose / modules / interfaces:** Core `fully_explore_developer_galaxy`
  grants existing system survey, civilization and core knowledge to the developer
  player only. `DiplomacyState::territorial_claims(optional observer)` returns
  owned claim-only snapshots in stable claim order; `campaign_territorial_claims`
  enforces ordinary filtering or an explicitly fully explored developer view.
  `developer_empire_summaries` and `developer_empire_research` are guarded,
  read-only projections of real civilization, colony, fleet, economy, diplomacy
  and adaptive research state. The existing research authority owns percentages.
- **Consumers:** native setup/async generation, current developer campaign reveal
  control, territory projection worker, Empire Monitor, core artwork and map
  labels. Clickable DEV CONTROLS provides access without a keyboard chord. The
  monitor can focus an alien home, and the celestial index can focus the core.
- **Central object presentation:** shared `central_black_hole_map_radius` uses
  three times the prior world scale, a prominent overview minimum and a viewport
  bound at close zoom. Its collision-aware label has priority over empire and
  system names. This changes no physical properties or ordinary black-hole sizes.
- **Saves / determinism:** optional `FullExploration` in the isolated developer
  envelope, omitted when false; legacy saves default false. Existing knowledge
  serialization persists surveys and core discovery. No random draws, generated
  content, AI observer knowledge, treaties, claims or research are altered by
  exploration. Inspection and navigation are read-only. Player saves stay separate.
- **Performance / threading:** reveal is linear in systems and civilizations
  aside from knowledge set insertion. Empire summaries visit current colonies,
  fleets, economies, claims and research nodes; only the selected empire builds
  a detailed research view. Monitor refresh is capped at four per second while
  visible, on the campaign owner. It retains one opening baseline per empire,
  no time-series log. Claim-only snapshots avoid copying diplomatic history.
  Detached territory work keeps its existing generation/fingerprint safeguards.
- **Tests / reuse:** setup/generation tests cover independent switches, ordinary
  rejection, knowledge isolation, idempotence and JSON round trips. Developer UI
  tests cover foreign hidden claims, real population changes, ordinary rejection,
  read-only monitoring/navigation and 720p through 4K bounds. Runtime replay uses
  actual controls to reveal, monitor aliens and capture the supplied central
  black hole above fog. These projections can support developer scenario review
  and future history sampling without a duplicate simulation.
  Validation passed: 12 focused tests and 23 overlapping regressions; packaged
  720p/1080p Vulkan replays with inspected setup, monitor and map captures.
  Independent direct-launch exploration preserves research/coverage choices.
  Save anchors and final packaged executable/asset hashes were checked.

### ENGINE LIMITATIONS REMAINING

The monitor does not persist history or define a fictional overall development
percentage; displayed progress is actual research-stage progress. Reveal does
not fabricate diplomacy contacts, treaties or missing celestial objects. Some
morphology/size/seed combinations intentionally have no central black hole.
Normal player fog remains in force outside developer campaigns. Existing 50k
campaign, save-memory and physical-simulation limits remain unchanged. See
[NATIVE_DEVELOPER_EXPLORATION_REPORT.md](NATIVE_DEVELOPER_EXPLORATION_REPORT.md).

## Developer observation and planetary environmental classification (2026-09-18)

- **Purpose/modules:** Core `campaign_observation.hpp` grants developer-only read
  access from existing campaign provenance; `planetary_classification.hpp` derives
  environmental classes from canonical body records. Engine remains game-agnostic.
- **Interfaces:** `developer_observation`, `observation_survey_level`,
  `can_inspect_settlement`, `classify_planetary_world`, `planetary_world_class_name`.
- **Consumers:** native system/body inspectors, colony projection and roster,
  planetary screen, local travel projection and fleet controller/workspace.
  Alien colony output, currency, funding, buildings and research use their owner;
  fleet reconnaissance uses the fleet owner's actual knowledge. Foreign objects
  remain read-only, with existing Core command ownership validation retained.
- **Determinism/save/performance:** derived, owner-thread snapshots; no schema,
  persisted world, knowledge or AI changes. Classification is constant work per
  displayed body. Fleet duplicate detection uses one linear hash count instead
  of a full fleet scan for each row. Developer fleet combat status is collected
  per civilization; crowded developer fleet views still need profiling at scale.
- **Tests:** native colony/fleet controller developer and revoked-access cases,
  serialized-state equality, normal observer regression, system classification
  boundaries and canonical Sol classes, planetary layout/commands at 720/1080p,
  existing system/travel/body/roster regressions and real graphical replay.
  Foreign armed fleets also reject clicks on the hidden engagement control.
  Validation passed: 15 focused tests, 20 additional consumer regressions and
  packaged 720p/1080p Vulkan replays with alien colony/economy and Earth captures.
  Ordinary save anchors and packaged executable/file hashes were checked.
- **Limitations/reuse:** Continental is an environmental category, not measured
  coastline coverage. No regional population, climate, terraforming or military
  surface simulation is implied. The same policy can serve further debug views;
  map revelation and diplomatic command channels retain their separate semantics.
  See [the inspection report](NATIVE_DEVELOPER_INSPECTION_REPORT.md).


## Authored planet maps across system and 3D planetary views

- **Purpose/consumers:** Jupiter and Mars share their supplied-art-derived full
  maps in solar-system discs, inspection portraits and the native 3D screen.
- **Ownership/APIs:** Application `native_planet_surface_assets.hpp` provides the
  fixed `planet_surface_assets` catalog and `planet_surface_asset_index(key,layer)`;
  `NativePlanetGlobe::set_maps` receives a body key and layer. Existing Engine
  `RgbaImage`, image preparation jobs and `Scene3D` material APIs render them.
- **Compatibility:** No save or simulation changes. Canonical Sol identity and
  observer-filtered discovery select assets. Earth night/cloud layers remain.
- **Performance:** Five lazy cached globe layers; each authored PNG decodes to
  about 6 MiB. Albedo under 4096 wide keeps its bytes without an extra resize.
  Existing worker-generated system-disc cache retains its 16 MiB bound.
- **Tests:** `native_planet_disc_assets` and `native_planetary_screen` check real
  maps, worker parity, dynamic lighting, key/discovery invalidation, rotation,
  geometry and Earth regressions. Developer smoke captures both views and
  verifies read-only campaign state; package allowlists check exact file hashes.
- **Limits/future reuse:** Only Jupiter and Mars are newly unified. Inferred far
  sides, residual fine shading and no measured terrain maps remain. First globe
  decode is still synchronous; source-map sharing across worker requests can be
  extended later. No galaxy simulation/save limits change.
- **Report:** [Native authored planet trial](NATIVE_AUTHORED_PLANET_REPORT.md).

## Complete supplied Sol artwork and ring geometry

- **Purpose:** all eight Sol planets and Luna use the supplied-art-derived full
  maps in system discs, portraits and the rotatable planetary screen.
- **Modules/APIs:** Engine `native_geometry3d.hpp::annulus_mesh(inner,outer,segments)`
  validates finite ordered radii and bounded tessellation. Application
  `native_planet_surface_assets.hpp` owns the eleven-layer catalog and display
  poses; `native_planet_rings.hpp` shares the Saturn/Uranus band profiles across
  2D depth halves and immutable 3D instances. No celestial rules enter Engine.
- **Consumers:** planet disc preparation, system workspace, NativePlanetGlobe,
  developer graphical replay and export content allowlists.
- **Determinism/save/performance:** presentation only, observer-gated canonical
  identity; no save/schema changes. Nine 1774x887 color maps total about 54 MiB
  decoded; Earth's extra layers remain lazy. Existing 16 MiB disc cache and GPU
  budgets apply. Annuli are cached immutable resources; only visible system
  rings are projected. Initial globe decode remains synchronous.
- **Tests:** real-image worker parity and lighting for all nine bodies; discovery,
  immutable texture/mesh reuse, separate Earth layers, ring holes/winding, 2D
  back/body/front ordering, tilted 3D survey projection/picking; real packaged
  Sol navigation and front/far-side captures; export manifest/hash checks.
- **Limits/reuse:** generic annuli can serve further rings. Ring profiles and
  unseen surfaces are artistic; no particle or ring-shadow physics, measured
  terrain, UV Venus mode or other moon coverage is implied. Galaxy/save limits
  remain unchanged. See [the Sol artwork report](NATIVE_SOL_ARTWORK_REPORT.md).

## Seeded small-body fields, analytic motion and solid body rendering

- **Purpose:** nine persistent asteroid/ice/debris types, Sol baseline regions,
  cracked-world fragments, named source-art variants, orbital motion and spin.
- **Ownership/APIs:** Engine `analytic_orbit.hpp` supplies Kepler position,
  quaternion spin and display-radius evaluation; `billboard_batch.hpp` appends
  indexed cards within mesh budgets; `texture_decal` adds opaque cutouts that
  preserve enclosed shadows. Core `small_body_fields` and `small_body_commands`
  own generation, configuration, validation, resource depletion, environment
  queries and authorized developer spawning. Application owns cached image jobs,
  observer-filtered snapshots, LOD, picking, panels and shared orbit projection.
- **Consumers:** fresh campaigns, native legacy-session activation, galaxy JSON,
  system workspace, developer commands, resource/scanning inspection and exports.
  Mining/supply callers can reuse `extract_small_body_resource` and
  `harvest_small_body_supply`; path/scan consumers can query field environments.
- **Determinism/save:** independent seeded streams, version-1 compact field
  records, stable per-index reconstruction, saved epoch/canonical clock and sparse
  depletion ledger. Optional fields preserve legacy compatibility; explicit empty
  catalogs never reroll. Parent ownership/cycles, array lengths and scalar ranges
  are validated. Physical AU/radius values are separate from display coordinates.
- **Performance:** 32 fields/system, up to 2,048 reconstructed visual samples per
  field with viewport culling. All indices participate at overview zoom; projected
  size selects unresolved markers or real 3D meshes. A single scene draws at most
  768 solids, prioritizing the largest. Fixed source-image cache remains bounded;
  immutable solid meshes and albedo maps are separately cached. No per-frame
  authoritative integration.
- **Tests:** 12,000-seed rarity and 2,000-seed cracked trials; all types, Sol,
  orbit/spin, variants, depletion/supply, invalid saves, JSON round-trip, cutouts,
  LOD/picking/culling/discovery, zoom/layout regressions, export hashes and packaged
  graphical replays. Full generation/save checks pass through 10,000 systems.
- **Limits/reuse:** diffuse surface lighting, coarse environment hooks and fixed
  schematic planet phases. No GPU instancing, n-body physics, rock collision
  or automatic fleet mining/logistics consumption. Compiled configuration requires
  rebuilding. Comets, satellites and fragment effects can reuse Engine primitives.
  Read-only inspection: `NativeSmallBodyRenderer::last_scene()` exposes the exact
  solids `Scene3D` submitted to the current frame (the frame also carries the
  sky dome and planet globe views, so tests must not guess by list order), and
  `NativeSystemWorkspace::focused_small_body()`/`small_body_scene()` forward the
  focused seeded instance and submitted scene for validation tools.
  See [the complete implementation report](NATIVE_SMALL_BODY_FIELDS_REPORT.md).

## Directional solids, per-object lighting and system zoom

- **Purpose:** volume, moving surface illumination and a broad apparent mass range
  for small bodies; extended planetary magnification with a live camera readout.
- **Engine interfaces:** `native_solid_mesh.hpp::directional_solid_mesh` converts
  a caller-owned directional surface into immutable triangles with derivative
  normals. `Material3D::light_direction` optionally supplies validated, normalized
  camera-space lighting per object, using the existing fragment uniform. Unset
  values preserve the shared scene light used by existing globe/battle consumers.
- **Application consumers:** `NativeSmallBodyGeometry` caches 24 shapes at two
  LODs and 32 seamless surface maps. `NativeSmallBodyRenderer` submits one shared
  depth-tested scene. Ice haze/cluster cards are removed; rocky haze fades out at
  close range. `NativeSmallBodyPanel` browses large objects and identifies size
  classes. `NativeSystemWorkspace` zooms both planets and small bodies through
  55x with cursor anchoring and a lower-right magnification readout.
- **Save/determinism:** the wider 1.3–78 presentation-radius distribution derives
  from the existing version-one scale quantile. No RNG consumption, authoritative
  scale, material, orbit, spin, resource quantity, depletion or save schema changes.
- **Budgets:** 768 solid draws, approximately 7 MiB of CPU mesh data and 16 MiB of
  CPU albedo maps at full cache residency. Engine GPU/target budgets still apply.
  24x12 geometry below 28 screen pixels; 96x48 above; markers below 2.2 pixels.
- **Tests:** Engine solid-normal/validation tests and GPU light inheritance/override
  tests, plus native volume, spin, pause, cache, UV seam, size-tail, ice-haze,
  picking, culling and observer tests. System workspace covers 55x outer-planet
  anchoring, Fit System and indicator updates; graphical packaged replays cover
  planets, rocky/icy huge-body views and save/load at 720p and 1080p.
- **Limits/future reuse:** diffuse lighting without ice refraction/specular physics;
  finite shape library, discrete LOD and conservative bound picking. No collision
  or automatic mining loop. Directional solids and per-object light can also serve
  moons, debris and terrain props without Engine knowing their gameplay identity.
  See [the current 3D and zoom report](NATIVE_SMALL_BODY_3D_REPORT.md).

---

## Foundation expansion 1–30 registry (`engine/foundation-expansion-1-30`)

Baseline: `fbb3165b` (merged Developer-mode + design-system line, 172/172 CTest,
425 Python, sealed export `6ad1650b` green). Work branch:
`engine/foundation-expansion-1-30` — latest `1c2df367`, 188/188 CTest green.

Status vocabulary: **MISSING** (greenfield), **PARTIAL** (exists but does not
meet the requirement), **PRESENT** (meets the requirement), **EXTERNAL**
(engine side complete, outside dependency pending).

| # | Capability | Prior state | Current state | Files | Tests |
|---|---|---|---|---|---|
| 1 | Unified entity/world | PARTIAL — `EntityId`/`EntityRegistry` only | **ENGINE-COMPLETE + BRIDGE** — `World` store: components, hierarchy, queries, binary snapshot/restore, legacy ID map. `campaign_world_projection` adapts authoritative `FreshCampaignState` containers into the typed store as a read model — namespaced legacy ids (`campaign_legacy_id`, no cross-domain collisions), POD tag components per domain, honest containment hierarchy (moons→host bodies, civ-scoped economy/technology/construction/shipyard rows→their civilization, colonies→occupied body or system, fleets→current system; unresolved/cyclic refs stay unparented, never fabricated). Invariant pass adds `cyclic_parent` for body parent chains that revisit a body. Save identity rides `bind_legacy` + world snapshots; generational `EntityId`s invalidate stale handles on destroy. `sync_campaign_world` reconciles a projected world incrementally — rows matched by legacy id keep stable `EntityId`s across refreshes (created/updated/destroyed/reparented counts returned), removed rows retire their entity, and only campaign-namespaced bindings are swept (consumer-owned entities untouched). Core containers remain authoritative — no blanket rewrite; `campaign_world_projection_census` feeds the developer report's `session.json`. | `engine/…/world.hpp`, `engine/src/world.cpp`, `core/…/campaign_world_projection.hpp`, `core/src/campaign_world_projection.cpp`, `core/src/campaign_diagnostics.cpp`, `app/developer_diagnostic_report.hpp` | `engine_world`, `campaign_world_projection`, `campaign_diagnostics`, `developer_diagnostic_report` |
| 2 | Simulation scheduler + LOD | PARTIAL — `StrategicClock`, frame routing | **ENGINE-COMPLETE + LIVE CONSUMER** — `SimulationScheduler`/`SimulationExecutor`: tier policies (ACTIVE/NEARBY/DORMANT…), cadence, deterministic ordering, dormant analytic skip, budget-capped deferred execution, event/dirty wakes, versioned capture/restore. `GalaxySimulationStepCoordinator` runs the 12 step phases as Active-tier executor tasks dependency-chained in historical order (`campaign_coordinator_parity` verifies identical behavior). Phase tier demotion and parallel waves are future tuning, not yet enabled. | `engine/…/simulation_scheduler.hpp`, `simulation_executor.hpp` | `simulation_scheduler`, `simulation_executor`, `campaign_coordinator_parity` |
| 3 | Job/threading system | PARTIAL — FIFO+futures | **ENGINE-COMPLETE + LIVE CONSUMERS** — priorities, cooperative cancellation, dependency graphs, named workers, per-tag stats, error propagation. All app/core/engine `std::async` sites migrated: save-writer (`PlayerCampaignSaveController`), image preparation, audio director, territory overlay, planet-material decode queue (`MaterialCache` — persistent tagged worker replacing a fresh thread per decode; `job_stats()` exposes per-tag counts), support-bundle export, and campaign-session load. Voice synthesis keeps a dedicated COM-initialized thread (SAPI apartment requirement). | `engine/…/foundation.hpp`, `foundation.cpp` | `job_system` |
| 4 | Render graph | MISSING | **ENGINE-COMPLETE + LIVE CONSUMER** — `RenderGraph`: pass/resource declarations, single-writer validation, dependency+ordering edges, deterministic topological order. `native_scene3d_gpu` declares its per-frame resources (HDR scene target, color, depth) and passes (scene3d, tonemap — disabled without float-target support), compiles the DAG and executes passes in graph order; compile failure aborts the frame. | `engine/…/render_graph.hpp`, `native_scene3d_gpu.cpp` | `render_pipeline`, `native_scene3d_gpu` |
| 5 | GPU-driven rendering | MISSING | **PARTIAL + LIVE CONSUMER** — `DrawBatcher`: stable opaque (layer,material,mesh) batching, back-to-front transparent sort, culling hooks. `native_scene3d_gpu` interns each draw's full GPU binding key (mesh + all eight bound textures + sampler/pipeline flags) into `material_id`/`mesh_id`, submits `DrawItem`s, and executes the emitted `DrawBatch` runs as instanced `SDL_DrawGPUIndexedPrimitives` calls with `first_instance` indexing SSBO uniform arrays — identical objects merge into one draw call. True indirect draw still requires an SDL_GPU follow-on. | `engine/…/draw_batcher.hpp`, `native_scene3d_gpu.cpp` | `batcher_ui`, `native_scene3d_gpu` (instanced-draw + pixel assertions) |
| 6 | Texture streaming | PARTIAL — bounded LRU caches, sync decode | **ENGINE-COMPLETE + LIVE CONSUMER** — `TextureStreamer`: mip residency, priorities, VRAM budget, pin/evict, per-frame load queue. `native_scene3d_gpu` registers each bound texture with real per-mip bytes (cooked/BC1/RGBA paths), declares the frame's demand with camera-distance priority during `prepare()`, applies the streamer's residency changes by granularity (demotions/promotions re-upload the new resident tail; denials evict), and uploads lazily on re-admission; `Window::set_scene3d_texture_budget` retunes the budget at runtime. **Per-mip partial residency + screen-footprint LOD are live**: requests carry a desired mip derived from the projected bounding-sphere footprint (perspective/ortho focal × mesh radius — the sampler never reaches finer levels, so tails upload only what this frame needs; `anisotropic_texture` materials keep full chains since the flag declares high-frequency content), a denied request degrades to the coarsest mip tail that fits (`streamed_partial_binds` counts tail binds; RGBA tails CPU-box-downsample the base, cooked/BC1 tails upload level ranges), and only a fully denied bind serves the pinned white fallback (`streamed_fallbacks`). Environment/optics/shadow maps inherit the surface footprint estimate — a heuristic, not a per-UV density analysis. | `engine/…/texture_streaming.hpp`, `native_scene3d_gpu.cpp`, `native_map_platform.*` | `render_pipeline`, `native_scene3d_gpu` (budget-pressure eviction + degraded-tail + fallback pixel assertions) |
| 7 | Shader library + cache | MISSING — SDL built-ins only | **ENGINE-COMPLETE (management layer)** — `ShaderLibrary`: families, canonical variant keys, artifact hashes, version invalidation, diagnostics. Consumption pending SDL_GPU pipeline. | `engine/…/shader_library.hpp` | `render_pipeline` |
| 8 | Particle/VFX framework | MISSING — procedural flares | **ENGINE-COMPLETE** — `VfxSystem`: data-driven emitters, deterministic per-instance RNG pools, gravity/integration, LOD rate scaling, curve-driven scale/opacity/tint. RuntimeHost steps it in sim time and renders particles as camera-transformed tinted rects; host.spawn_emitter supports entity attachment with auto-stop on death (generated starter trails embers from its spark). Flare migration pending. | `engine/…/vfx.hpp`, `vfx.cpp` | `render_pipeline` |
| 9 | Physics layer | MISSING — combat-only grid | **ENGINE-COMPLETE** — `PhysicsWorld`: circle/AABB/segment primitives, broadphase over SpatialGrid, overlap/raycast/sweep, trigger enter/stay/exit events; versioned `capture_state`/`restore_state` round-trips bodies, id counter and the live overlap set (no phantom ENTER on restore) with `framework_state_json` codecs. Consumed by the shell PHYSICS inspector; game-path adoption pending. | `engine/…/physics.hpp`, `physics.cpp` | `spatial_physics`, `framework_persistence`, `framework_state_codec` |
| 10 | Spatial query framework | PARTIAL — private combat index | **ENGINE-COMPLETE + LIVE CONSUMER** — `SpatialGrid`: deterministic cell order, insert/remove/update, radius/AABB/ray queries, broadphase candidates. Galaxy-map `system_hit` uses a lazily rebuilt world-space `SpatialGrid<int>` (invalidated on session cache generation) for pointer hit-testing — O(cells touched) instead of scanning every system per event. Parity review concluded the massive-combat `SpatialIndex` stays private: it is a 3D Chebyshev cell-box scan returning *all* occupants (callers distance-filter), while `SpatialGrid` is 2D and `SpatialIndex3D` is k-nearest k-d — neither reproduces the exact candidate set/order combat determinism requires. | `engine/…/spatial_index.hpp`, `spatial_index3d.hpp` | `spatial_physics` |
| 11 | Route engine | PRESENT-PARTIAL | **EXTENDED** — `RoutePolicy` (blocked sets, per-system traversal cost = hostile-territory penalties) + `find_fuel_feasible_route` waypoint insertion with refuel callbacks. | `core/lane_network.*` | `route_policy` |
| 12 | Knowledge/FoW | PRESENT-PARTIAL | **UNCHANGED** — `CivilizationKnowledgeState` covers observer filtering; per-callsite discipline retained. | `core/knowledge.*` | `settlement_knowledge_parity` |
| 13 | Generic economy/resources | MISSING — per-resource fields | **ENGINE-COMPLETE** — `ResourceDefinition`/`Inventory`/`Recipe`/`Producer`/`TransferOrder`/`ResourceNetwork` with shortage reporting and bounded transfers. | `engine/…/resource_economy.hpp` | `economy_animation` |
| 14 | Event bus | PARTIAL — `EventQueue<T>` | **ENGINE-COMPLETE** — `EventBus`: typed subscribe, RAII `Subscription`, deferred tick-ordered queue, owner-thread enforcement. Core event-flow adoption pending. | `engine/…/event_bus.hpp` | `event_bus` |
| 15 | Mission/event framework | MISSING | **ENGINE-COMPLETE** — `MissionGraph`: JSON-defined triggers/conditions/stages/choices/timers, persistent instances, serialize/restore, effects emitted via EventBus. | `engine/…/mission_graph.hpp` | `mission_graph` |
| 16 | Advanced saves | MOSTLY PRESENT | **EXTENDED** — fnv1a64 integrity sidecars (atomic, incl. `.bak`), rolling history `.bak.2`..`.bak.4` with loader fallback, `read_player_campaign_preview` metadata reader (player + developer envelopes). Existing: v17 schema, migrations, autosave scheduler, async writer. | `engine/…/save_integrity.hpp`, `save_history.hpp`, `core/save_preview.*`, `core/player_campaign_*.cpp` | `save_integrity`, `save_history` |
| 17 | Deterministic replay | MISSING | **ENGINE-COMPLETE** — `ReplayRecorder`/`ReplayPlayer`: ordered command stream, FNV checkpoints, JSON round-trip. Session-journal integration pending. | `engine/…/replay.hpp` | `economy_animation` |
| 18 | Crash reporter | PARTIAL — support bundle only | **INTEGRATED VIA CODEX PATH** — `RuntimeDiagnostics` owns the real capture: unhandled-exception filter, terminate/abort handlers, session log, minidump and rolling context at client startup. The expansion `CrashReporter` is a parallel implementation kept library-only — installing it would displace the richer codex filter (it does not chain). Its context/event-bundle API remains available if a second consumer needs a non-fatal bundle writer. | `engine/…/runtime_diagnostics.*`, `engine/…/crash_reporter.hpp` | `runtime_diagnostics`, `crash_reporter` |
| 19 | Profiler | MISSING | **ENGINE-COMPLETE + CLIENT-CONSUMED** — `Profiler`: scoped spans, per-frame counters, thread-buffer drain, JSON export. Client frames bracketed in `scene()` (drains the preceding `update` spans), `update`/`simulation`/`scene` spans recorded, gated by developer session; aggregates render as `client/*` rows in the diagnostics LIVE PERFORMANCE table. **Frame capture + scenario comparison landed**: `ProfileCapture::parse`/`to_json` round-trips `export_json`, `compare_captures` diffs two captures into per-span mean deltas, and the engine shell's Profiler tool captures/saves/loads A+B slots (`<exe>/profiler_captures/`) and renders the largest deltas. **Lower-perturbation recording landed**: spans and call aggregates accumulate in per-thread buffers under each buffer's own lock — the global mutex is no longer taken on the recording path; per-thread aggregates merge at frame boundaries and thread exit, and `aggregates()`/`export_json` merge un-drained buffers on read so the real-time contract holds (multi-threaded recording covered in `profiler`). GPU timeline pending. | `engine/…/profiler.hpp`, `app/engine_main.cpp`, `app/native_client/native_developer_diagnostics.hpp` | `profiler`, `engine_diagnostics`, `native_developer_diagnostics` |
| 20 | Memory tracking | MISSING | **ENGINE-COMPLETE + CLIENT-CONSUMED** — `MemoryTracker`: subsystem registry, high-water marks, `TrackedAllocator` adapter, JSON export. Client reports planet-material cache residency (`used`/`reserved` against its 96 MiB budget) each update; developer diagnostic bundles include `memory.json`. Broader tagged-allocation and VRAM attribution pending. | `engine/…/memory_tracker.hpp`, `app/developer_diagnostic_report.hpp` | `engine_diagnostics`, `developer_diagnostic_report` |
| 21 | Input actions | PARTIAL — raw events | **ADOPTED** — `InputMapper` drives the client's galaxy keyboard shortcuts: `NativeCampaign` loads a data-driven `GALAXY` JSON context (pause, speeds 1–5 incl. Developer-only Demo, research/construction candidates, new campaign, F6 save, F8 support bundle) and dispatches `KeyPressed` events through `feed`/`just_pressed`. Stacked contexts, axes, chords and runtime rebinding ship in the engine for future UI. | `engine/…/input_actions.hpp`, `app/native_client/main.cpp` | `input_actions`, galaxy smoke key check |
| 22 | Audio engine | PARTIAL — CPU mixer | **EXTENDED** — the event-driven gameplay voice pipeline is now the live client path: `NativeGameplayVoiceBridge` observes `CampaignFrameResult`s after each authoritative advance (fleet/hull/diplomacy/economy/logistics + event routes, observer-safe), `NativeVoiceRouter` resolves cues from `Data/voice_profiles/events.json`, and `NativeVoicePlayback` (8-deep priority queue, dedupe, subtitle fallback) plays through `NativeAudioDirector::play_dialogue_pcm` on the engine `AudioOutput` voice channel — no parallel audio stack. `prerecordedPath`/`subtitleText` cue fields are now honored: the three approved scientist WAVs play recorded; all other events synthesize via SAPI with subtitle fallback. Legacy `VoiceCue` remains as the fallback when voice data is absent. Minted `localization_key`s now resolve through the live `LocalizationTable` at subtitle presentation — a catalogued `voice.<dialogue>.<variant>` entry overrides authored cue text; shipped catalogs carry no voice keys yet, so authored English remains the baseline. Music is incrementally streamed: `AudioStreamDecoder`/`open_audio_stream` pull-decodes 48 kHz stereo F32 on demand (lazy MF reader, `rewind()` loop, `AudioStreamError` permanent-failure classification, no 16 MiB/96 MiB caps), `AudioOutput::play_music` feeds it through the same bounded SDL queue, and the director streams its music track (`stats().music_streaming`) — effects and voice stay on whole-file clips. | `native_audio*`, `native_voice*` | `native_audio`, `native_voice` |
| 23 | Animation | MISSING | **ENGINE-COMPLETE** — `FloatCurve` (5 easings), `Timeline` tracks + loop modes (Once/Loop/PingPong) + crossed events. Skeletal blending out of scope. | `engine/…/animation.hpp` | `economy_animation` |
| 24 | Advanced UI | PARTIAL — theme helpers | **PARTIAL** — `VirtualizedList`, `TableModel` (sort/filter), `TreeModel` (expand/flatten), `UndoHistory` (bounded snapshot undo/redo) in engine; `VirtualizedList` is the colony roster's and the editor systems list's scroll model (stride row_height, `scroll_to`/`max_scroll` clamps, wheel input) **and the diagnostics panel's shared scroll model** — its four views configure the same instance per frame (33/57/61·s strides), wheel deltas scroll whole rows, `ensure_visible` drives the entities keyboard-follow, and `scroll_to` re-clamps every render so collapsing a tree or refreshing a list can never leave a stale offset past the tail, **and the remaining developer panels share it too** — the celestial index, planet-type index, empire monitor (both the empire and active-research lists), and the phenomena debug dump all scroll through the model (the phenomena dump's previous unbounded offset could even scroll past the end into blank space); the shared configure/clamp/snap step those consumers repeated is now the engine API itself — `VirtualizedList::sync_rows(row_count, row_height, viewport_height)` applies the new geometry, re-bounds a stale offset, snaps to a whole-row edge, and returns the first visible row, so every row-snapped consumer calls it per frame instead of duplicating the arithmetic; fractional-scroll consumers (colony roster, editor system/detail/picker lists, engine-shell asset/key/project/entity/scene lists, construction project/order lists) use the non-snapping `configure(...)`/`set_row_count(...)` pair, and the startup save-slot list is a `sync_rows` consumer, which closes the latent stale-offset gap their direct field assignments had — a filtered or shrunk row set could previously leave `scroll_offset` past `max_scroll` and render blank space; **`ScrollView` is the pixel-offset model for variable-height content** — `sync(content, viewport)` re-clamps on reflow, `scroll_by`/`scroll_to` bound wheel and drag deltas (non-finite input resets to the head), and `thumb(track, min_size)` reports proportional scrollbar geometry — consumed by every variable-height scrollable surface — chronicle browser, notification feed, body-inspection panel, economy workspace, research inspector/guided-card grid/active-program strip (the strip is a horizontal scroll, same contract), fleet outliner, colony freight review, new-game species and detail panes, diplomacy contact and detail panes, system-inspection card, supply-network workspace, controlled-assets navigator, planetary screen (facts/slots/details/queue panes) and shipyard designs/orders/details — replacing per-surface copies of the same clamp/thumb arithmetic; surfaces that used the inverted negative-offset convention (fleet, colony, construction, planetary, shipyard designs/orders) now hold positive offsets and subtract them from row geometry; `UndoHistory` backs the editor's annotation layer (Ctrl+Z/Y); **`TableModel` is the colony roster's sort and filter model** — column headers cycle ascending/descending over name/world/population (numeric population sort, direction marker), and a pointer-focused search field drives `TableModel::refilter` (case-insensitive contains over all cells; localized placeholder + caret, UTF-8-safe backspace, Escape blurs instead of closing); sort and filter both survive live refresh through `set_rows`, and the display-position→source-row mapping keeps filtered opens on the right colony; **`TableModel` is also the diagnostics performance table's sort model** — the Phase/Samples/Mean ms/Maximum ms headers cycle ascending→descending on click (roster `^`/`v` marker convention, scroll resets), and the per-frame `set_rows` rebuild keeps the sort live as samples refresh; **`TreeModel` is the editor's system list** — filtered systems are roots with bodies as children (›/▾ toggles, search auto-expands, body rows jump to system context); **`TreeModel` is also the diagnostics ENTITIES inspector** — the native client's first tree consumer: the read-only projected campaign hierarchy renders depth-indented rows with ·/›/▾ glyphs (roots open by default, deeper levels closed), a matched press+release on a row toggles its subtree, and toggles persist across `sync_campaign_world` rebuilds by node id (root collapses and child expansions remembered separately); **`TreeModel` owns an id-stable selection** (`select`/`selected`/`move_selection` over the flattened view — clears when the id is absent or hidden by a collapse) which the inspector drives from the keyboard — arrows/Home/End move a highlighted selection with the scroll window following, Left collapses an expanded parent or jumps to the parent row, Right expands or descends to the first child, Return/Space toggles, clicking a row selects it, and the selection survives sync rebuilds like the expansion sets; the selected row also drives a read-only detail pane beside the rows (entity index/generation, namespaced legacy ref, parent name, child count, and the full projected tag fields for each campaign domain); **`TreeModel` is also the Controlled Assets navigator** — the first player-surface consumer: the five category headers (Planets/Outposts/Fleets/Stations/Shipyards) are parent nodes over their asset rows, `flattened()` produces the visible entry list, and the persisted `Preferences::collapsed` plus search/temporary-reveal policy drive each header's expanded flag; the ENTITIES inspector also gained a pointer-focused search field (navigator contract — typed text filters the rebuild to matching entities plus their expanded ancestor chain, the census reports the kept count, Tab/Return commit out, Escape blurs instead of closing, and `wants_text_input` joins the client's text-input gate), and the RECENT EVENTS view shares the same field contract over its 512-record retained ring (a render-time index filter keeps hit-testing, scroll bounds and rendered cards on the same visible set). Remaining: `UndoHistory` adoption beyond the editor. | `engine/…/ui_viewmodels.hpp`, `engine/…/undo_history.hpp`, `app/native_client/native_colony_roster.cpp`, `app/native_client/native_controlled_assets.cpp`, `app/native_client/native_developer_diagnostics.hpp`, `app/editor_main.cpp` | `batcher_ui`, `native_colony_roster`, `native_controlled_assets`, `native_developer_diagnostics`, `undo_history` |
| 25 | Localization | MISSING | **ADOPTED (menus, settings, setup wizard + voice cues)** — `LocalizationTable`/`LocalizationService` (JSON locales, fallback chain, positional+named formatting, plurals, runtime reload). The startup screens, pause menu, settings hub, all four settings panels (General, Audio, Video, Voice & Subtitles), and the new-game setup wizard (mode/species cards, galaxy-type and population pages, seed/size/dev controls, environment tolerance details) resolve their labels through `data/locale/en.json` (packaged to `Data/locale/en.json` via `resource_stream`; missing keys fall back to literals). The diplomacy workspace resolves its chrome the same way — title, filter/tab labels, relationship meters, action buttons, modal text, empty states, and secrecy-preserving placeholders (`THE UNDISCOVERED`, `UNRESOLVED INFORMATION`) — while controller-produced values (political status, agreement/history entries) stay authoritative data. `NativeVoicePlayback` resolves minted `voice.<dialogue>.<variant>` cue keys the same way, so a locale pack can override subtitle text. Earlier claims of a developer-tools embedded catalog were wrong — no such consumer existed. Gameplay HUD strings are still literal; only English ships. | `engine/…/localization.hpp`, `data/locale/en.json`, `app/native_client/native_general_settings.*`, `app/native_client/native_audio_settings.*`, `app/native_client/native_video_settings.*`, `app/native_client/native_voice_settings.*` | `localization`, `native_general_settings`, `native_audio_settings`, `native_video_settings`, `native_voice_settings` |
| 26 | Accessibility | PARTIAL — subtitle size | **ENGINE-COMPLETE (settings layer), PARTIALLY CONSUMED** — `AccessibilitySettings`: ui/text scale, high contrast, color-blind modes, reduced motion/flashing, subtitles; sanitize + JSON round-trip. Client adoption: General Settings persists `reduceMotion` (gates decorative motion — system tumble/planet spin, eruption animation — without touching simulation) and `interfaceScale` (Compact/Standard/Large/Huge → 0.85/1.0/1.2/1.45 multiplier inside the engine clamp, applied as `NativeUiLayout::user_scale` so every `for_viewport` consumer plus stellar-observation scaling follows; draft/cancel semantics, persisted), and `reduceFlashing` (gates the stellar artwork polar-pulse to steady mean luminance via `Artwork::set_reduce_flashing`), and `highContrast` (`native_ui::apply_high_contrast` runs a global DrawList pass in `scene()` — low-luminance text snaps to primary ink across every surface without per-screen palette plumbing), and `colorBlind` (Off/Protanopia/Deuteranopia/Tritanopia → `native_ui::apply_color_blind` runs a global DrawList daltonization in `scene()`: Machado severity-1 simulation measures lost contrast, error redistribution pushes it into the channels the mode still perceives — covers text, primitives, image tints and mesh tints; GPU-rendered 3D scene content is out of scope until a post-process pass exists). The client's `GeneralPreferences` embeds the engine `AccessibilitySettings` struct as the canonical substrate (`accessibility` member carrying reduce-motion/flashing, high-contrast and the typed `ColorBlindMode`; the persisted JSON shape is unchanged and `effective()` folds the interface-scale preset into `ui_scale` with `sanitize()` for consumers). Keyboard focus contract (settings layer complete): the settings hub and General/Audio/Video/Voice panels ring their controls on Tab/arrows (Home/End jump to first/last) with Return/Space activation routed through the same dispatch as pointer clicks; focused sliders take Left/Right/Home/End as value adjustments; activation keeps focus (repeated Space toggles) while pointer presses, close paths and modal transitions reset it; modal states narrow the ring (Controls help view, pending folder browser, display-rollback confirm); open dropdowns own their keys. Focus changes play the hover cue via `HoverFeedback::cue()` (non-mutating — pointer state stays authoritative). The startup flow also adopts the contract end to end: `NativeStartupWorkspace` rings per-screen focusables across Entry/ModeSelection/LoadSlots/Busy/Failure/Development with transitions resetting via `reset_pointer()`, and the delegated `NativeNewGameWorkspace` covers all three setup pages — galaxy cards + Back/Next, population-state picker (open dropdown owns its keys), and the Configuration form (species rows, size presets, rival/ancient pickers, seed field, footer actions) in (y,x)-sorted order; the seed field enters edit mode on Return/Space, captures typing/arrows, and commits on Tab/Return. In-game workspaces adopt the same contract: settlement (choice rows), logistics (refresh/close), colony freight-review modal, economy (refresh/close/industry priorities), shipyard (search edit mode, sort/filter dropdowns, dynamic design + order cards, quantity/favorite/build controls, cancel-confirmation narrowing), construction (project + status rows, two-stage cancel), research (domain tabs, render-registered interface hits, guided/tree cards, inspector action), fleet (outliner rows, release-gated order/locate controls activated through a matched press+release pair, recovery rail, engage, preview confirm), battle (chrome + order grid; field selection stays pointer-spatial by design), and the chronicle browser (header controls and intro-row cyclers including the conditional page/focus buttons, located cards, DIP actions and tag chips — each clipped to the list viewport, activated through the same replayed press/release dispatch; the search field owns its keys while editing, Tab/Return commit out), and the notification feed (RECENT EVENTS rings the CHRONICLE/close header buttons and each card's explicit action buttons — card bodies stay inert and only viewport-contained actions focus, the same gate pointer activation applies — with keyboard activation replaying the press/release dispatch so Contact emits OpenDiplomaticContact and located cards emit OpenSystem unchanged), and the diplomacy workspace (RELATIONS rings the close control, the nine-button filter grid, contact rows clipped to their viewport, the conditional transmission/negotiate/war action stack, the five-tab strip and the detail region's proposal/intelligence actions; an open modal narrows the ring to its terms or confirm/cancel pair; activation replays the click dispatch so SelectContact, Action, ProposalAction and FocusSystem commands emit unchanged), and the colony roster (search field, refresh/close, the sort-column headers and every clipped-visible row ring in order — the search field enters edit mode on activation and owns its keys until Tab/Return commit out — with row activation replaying the matched press/release pair so generation-guarded open-colony commands emit unchanged), and the controlled-assets navigator (hide/search/clear plus every clipped-visible tree entry — category headers and rows — ring in order with scroll-follow keeping the focused entry fully visible; activation replays the press/release pair so headers toggle persisted collapse and rows select/manage unchanged; hidden mode narrows the ring to the restore control; the search field owns its keys while editing), and the system workspace's small-body survey panel (the always-on launcher/motion chrome plus the panel's close/field/body/focus controls and developer debug/spawn buttons ring in (y,x) order; activation replays the press dispatch so toggle_motion, field/body cycling, focus_small_body and spawn commands emit unchanged), and the planetary colony screen (the render-registered hit registry rings every enabled button, layer/view-mode control, tab strip, action row and clipped-visible slot cell in (y,x) order — keyboard activation replays the exact same hit dispatch a matched pointer press+release takes, the confirmation modal narrows the ring automatically by clearing the registry before registering Cancel/Confirm, Escape releases the ring before the modal-cancel/deselect/Back chain, and globe region picking stays pointer-spatial by design; `NativeColonyWorkspace` delegates `focus()`/`focused_label`/`focused_bounds`/`focused_control` to the planetary screen outside the freight modal so announcements cover both), and the developer celestial index (search field as Edit, category filter, close, every rendered row with scroll-follow via `VirtualizedList::ensure_visible`, the conditional central-state dropdown and center-map action — activation replays the shared hit dispatch so dropdown opens, row selection and focus requests emit unchanged; search editing owns its keys until Tab/Return commit out and Escape releases edit, then the ring, then the panel), and the developer planet-type index (header actions, class filter, rendered rows and the conditional rules/go/generate footer — activation replays the press/release dispatch so `force_developer_planet_type` and the focus request emit unchanged). Focus-ring suppression: `wants_keyboard_focus()` joins the galaxy input-mapper guard alongside `wants_text_input()` — while any post-mapper surface (workspaces, roster, system panel) holds a focused control, bound galaxy actions like Space→toggle_pause no longer preempt Return/Space activation. The pause menu itself adopts the contract: `menu_focus_` rings the seven stacked actions (Continue/Save/Load/Settings/Support/New Game/Exit) with hover-cue playback, Return/Space dispatch through the same `activate_menu_action` path pointer clicks take, and pointer presses, `toggle_menu`, and new-game pending/cancel transitions resetting it (the pending menu narrows to Continue; Return/Space there cancel like Escape/click). The map inspection card rings its close control (single-target ring; Return/Space replay the press dispatch that clears it, Escape now dismisses the card before falling through to the menu). Always-on map chrome is chained through a dispatcher focus-group policy: `map_focus_group_` orders the assets navigator, fleet outliner and HUD chrome, nav keys that would wrap a group's boundary release the ring (uncaptured) so the same key lands in the next group, pointer presses/cancels and `map_hud_visible()` transitions reset it, Escape releases a live ring before reaching the pause-menu toggle, and `NativeUiLayout::hud_actions()` exposes the 17 HUD actions (top strip, nav bar, rail; EVENTS only while the feed is available) for the ring — activation replays the pointer dispatch paths (pause/resume, speed cycle, feed toggle, menu, `route_navigation`). This also fixed the fleet outliner's keyboard block being unreachable (the navigator claimed every nav key first). Screen-reader substrate lands its first slice: `AccessibilityAnnouncer` is a bounded live-region queue (polite/assertive priorities — assertive preempts queued polite, consecutive duplicates collapse, capacity evicts oldest polite first, monotonically increasing sequence numbers) that a platform AT bridge will eventually drain; the client already consumes it two ways — `publish_notification` announces every feed item, and the pause menu announces each focused action's localized label on navigation — with pending announcements rendered through the existing voice-caption channel (`render_voice_caption` accepts a UI-announcement fallback, shown only while subtitles are enabled). Focused controls carry announcement labels: the assets navigator and fleet outliner expose `focused_label()` (control names, category headers, fleet/asset names), the HUD ring and pause menu announce localized action labels on every focus move, and the notification feed announces arrivals — all routed through the announcer. Every focus-bearing surface now exposes `focused_label()` and the dispatcher announces it on focus moves (chronicle, roster, freight modal, notifications, diplomacy, battle, economy, construction, shipyard, research, settlement, supply, inspection, startup/settings). Speech playback of announcements lands opt-in: the Voice & Subtitles panel gains a persisted "Speak interface announcements" toggle (`interfaceAnnouncements` in `voice-settings.json`; the strict schema accepts the optional key so legacy 10-key files load with it defaulting off). While enabled, each drained announcement submits a `NativeSpeechRequest` to `NativeVoicePlayback::speak` — Important priority (so chatter gating cannot swallow it), `interface` category with `ReplaceCategory` queueing (rapid navigation collapses queued speech), a per-announcement dedupe key (identical labels re-focused within the dedupe window still speak), 10-second expiry, and `interruptible` honoring `no_interruptions`; captions continue independently under the subtitles preference. Platform AT bridging lands its first slice on Windows: `NativeAccessibilityBridge` subclasses the game HWND's window procedure (the SDL message hook can only observe messages, and WM_GETOBJECT needs a return value), answers `UiaRootObjectId` requests with a minimal server-side `IRawElementProviderSimple` (pane control type, application name, absent from the control tree), and `announce()` raises `UiaRaiseNotificationEvent` (ImportantMostRecent processing) per drained announcement whenever `UiaClientsAreListening()` — the startup and campaign announcer drains both feed it; `Window::native_window_handle()` exposes the HWND read-only. Announcements carry a `Kind` (`Status`/`Focus`) — every `focused_label` site routes through `announce_focus`, and `Focus` items raise a real `UIA_AutomationFocusChangedEventId` on a synthetic `IRawElementProviderFragment` child (custom control, HasKeyboardFocus, name = focused label, Parent/FragmentRoot navigation back to the window provider, reported through `GetFocus`) instead of a live-region notification. Every focus-bearing surface also exposes focused_bounds() — the focus announcement carries the control client-pixel rect and the fragment reports it as its BoundingRectangle (client to screen projected), so magnifier/tracking AT sees real geometry. Slider focus announcements also carry a normalized AnnouncementRange — the fragment exposes a read-only IRangeValueProvider (GetPatternProvider on the raw provider; IsReadOnly, SetValue fails) so AT reports the sliders position in range. Focus announcements also carry an AnnouncementControl kind — the fragment maps it to a real UIA ControlType (Button/CheckBox/Edit/Slider/Group; a valid range implies Slider) so AT names the widget class, not just "custom". Audio/voice settings, the pause-menu ring and HUD chrome classify their controls; the text-field surfaces (colony-roster, chronicle, controlled-assets, research and shipyard searches plus the new-campaign seed field via the startup-workspace delegation) announce Edit, and the startup settings route forwards range/control as well as label+bounds. The previously reserved text/subtitle fields now carry live controls and consumers: General Settings gains a third accessibility row — SUBTITLES (master caption gate), SUBTITLE SIZE (0.75..2.0 preset cycle), TEXT SIZE (same presets) — persisted as `subtitlesEnabled`/`subtitleScale`/`textScale`; `render_voice_caption` takes the effective `AccessibilitySettings`, gates all caption output on `subtitles_enabled`, and multiplies the voice-preferred pixel size by `subtitle_scale` for both the in-game and startup call sites; `NativeUiLayout::set_text_scale` scales only the shared font metrics (`control/metric/heading_font_pixels`) inside `for_viewport`, enlarging text without growing chrome geometry. Remaining open: AT-SPI/non-Windows backends and writable/interactive UIA patterns. Startup-flow announcements stay caption-only for speech (the voice pipeline starts with the campaign session; the UIA bridge covers them). | `engine/…/accessibility.hpp`, `app/native_client/native_general_settings.*`, `app/native_client/native_audio_settings.*`, `app/native_client/native_video_settings.*`, `app/native_client/native_voice_settings.*`, `app/native_client/native_settings_hub.hpp`, `app/native_client/native_menu_hover.hpp`, `app/native_client/native_voice_caption.hpp`, `app/native_client/native_startup_workspace.*`, `app/native_client/native_new_game_workspace.*`, `app/native_client/native_accessibility_bridge.*` | `economy_animation`, `native_general_settings`, `native_audio_settings`, `native_video_settings`, `native_voice_settings`, `native_startup_workspace`, `native_new_game_workspace`, `native_settlement_workspace`, `native_logistics_workspace`, `native_colony_workspace`, `native_economy_workspace`, `native_shipyard_workspace`, `native_construction_workspace`, `native_research_workspace`, `native_fleet_workspace`, `native_battle_workspace`, `native_chronicle`, `native_notifications`, `native_diplomacy_workspace`, `native_colony_roster`, `native_controlled_assets`, `native_system_workspace`, `native_inspection`, `native_accessibility_bridge`, `native_developer_index` |
| 27 | Platform layer | PARTIAL — Win32+SDL+GDI | **UNCHANGED-PARTIAL** — existing paths/atomic-write/image layer retained; `PlatformServices` (Req 28) adds the services seam. Full OS abstraction documented as follow-on. | `engine/*` | — |
| 28 | Steam layer | MISSING | **ENGINE-COMPLETE — EXTERNAL** — `PlatformServices` facade + `NullPlatformBackend`; feature gating, user identity, achievement/presence/cloud calls. Live Steamworks SDK backend pending credentials. | `engine/…/platform_services.hpp` | `package_platform` |
| 29 | Mod architecture | MISSING | **ENGINE-COMPLETE** — `PackageManifest` (semver, deps, provides), `PackageRegistry` (protected namespaces, deterministic topo load order, conflict reporting), `scan_packages` directory discovery. | `engine/…/package.hpp` | `package_platform` |
| 30 | Stellar Tools | PARTIAL — dev submenu + panel | **EXTENDED** — tabbed tools host: Commands / Diagnostics (live Profiler + MemoryTracker overlays, session stats) / Saves (rolling-chain slots with integrity + preview fields). Profiler `begin/end_frame` actually wired: one frame per `scene()` call, `update`/`simulation`/`scene` spans, aggregates shown in LIVE PERFORMANCE. The diagnostics panel gained an ENTITIES inspector — a read-only `project_campaign_world` view over the authoritative campaign (rebuilt on open/refresh, not per-frame) listing every projected entity as `domain id · tag fields ← parent` (system position, body/colony/fleet and civ-scoped refs), plus entity/parented counts and the World's `estimated_memory_bytes` container footprint. The inspector keeps one projected world and reconciles it through `sync_campaign_world` on refresh — the incremental path's first live consumer, with created/updated/destroyed/reparented drift counts in the header. | `native_developer_tools.*`, `native_developer_diagnostics.hpp`, `main.cpp` | `native_developer_tools`, `native_developer_diagnostics` |

## Foundation expansion integration into the codex native line (2026-09-20)

Branch `work/foundation-1-30-codex-integration` merges
`engine/foundation-expansion-1-30` (`aa90d0e6`, 13 commits over `fbb3165b`)
onto `cpp/codex-native-architecture-integration` tip `e20e83a9`
(game `0.1.14.2-dev`, engine `0.1.64`). The codex architecture won every
overlapping subsystem — renderer, artwork policy, campaign session, installer
and packaging — and expansion work was kept only where additive.

### ENGINE CAPABILITIES ADDED / EXTENDED

- **Save history and recovery (req 16):** rolling `.bak`, `.bak.2` …
  `.bak.N` slots rotate on each save and the loader walks the chain.
  `PlayerCampaignLoadOrigin::History` recoveries now publish a distinct
  "Recovered campaign from an older autosave" notice in the session layer.
- **Deterministic replay (req 17):** `ReplayRecorder`/`ReplayPlayer` are wired
  into `NativeCampaign` (`--record`/`--replay`, fixed-step playback, FNV
  checkpoints). Verified: recorded checkpoint replayed with
  `verified_checkpoints:1`, `diverged:false`.
- **Input actions (req 21):** `InputMapper` drives galaxy keyboard shortcuts
  from a data-driven context, and `SDL_EVENT_KEY_UP` now emits
  `InputEventType::KeyReleased` (Escape/Backspace excluded) for correct
  pause/release behavior — verified by the galaxy-art smoke `paused:true`.
  The mapper's `begin_frame()` is called once per `update()` and its feed
  runs before workspace handlers so releases always clear held state;
  `--navigation-smoke` verifies speed/pause/save shortcuts in both galaxy
  and system views plus four blocked contexts.
- **Developer tools host (req 30):** codex's developer panel/diagnostics/
  empire-monitor subsystem retained; the expansion's parallel development-menu
  machinery was omitted as superseded. `--developer-smoke` runs through the
  codex path (SYNC-006 planet-map assertion still open).
- **Fleet overview:** `native_fleet_workspace` gained the overview/council
  rows (`OverviewRowKind`, colony navigation) adapted to codex's controller
  APIs and `selected_changed` lambda.
- **Localization (req 25):** `LocalizationTable`/`LocalizationService` ship in
  the engine; the startup screens, pause menu, settings hub, all four settings
  panels (General, Audio, Video, Voice), and the new-game setup wizard consume
  the cooked `Data/locale/en.json` catalog, and minted voice-cue localization
  keys resolve through the live table at subtitle presentation.
- **Engine libraries present, verified by unit tests:** World store,
  SimulationScheduler, JobSystem priorities, RenderGraph policy layer,
  DrawBatcher, TextureStreamer, ShaderLibrary, VfxSystem, PhysicsWorld,
  SpatialGrid, ResourceNetwork, EventBus, MissionGraph, CrashReporter
  (library-only — codex `RuntimeDiagnostics` owns the installed handlers),
  Profiler, MemoryTracker, FloatCurve/Timeline,
  UI view models, AccessibilitySettings, PlatformServices facade and
  PackageManifest/Registry.

### ENGINE LIMITATIONS REMAINING

- Engine libraries that are compiled and unit-tested but not yet consumed by
  the live game remain library-only per the table above (render graph backend,
  GPU-driven submission, texture streaming consumption, mission graph runtime,
  Steam backend). They are not claimed as in-game features.
- The expansion's duplicate developer-menu, audio-settings and session types
  were dropped; codex's wired implementations are authoritative.
- SYNC-001/002/005/010/011 CTest baseline failures are unchanged by the merge
  (21 tests, see the validation receipt). SYNC-006's developer-smoke planet-map
  assertion persists on the integrated build.
- Python exporter suite matches the codex baseline: 36 documented unsuccessful
  tests (fixture roots without current planet manifests, review-only source
  images absent from checkout); zero new regressions, one baseline test now
  passes (`test_galaxy_loads_assets_relative_to_executable`).
- Packaging scripts previously required PowerShell 7; they now also run on
  Windows PowerShell 5.1 with identical output bytes.

## Notes

- `engine/foundation.hpp` primitives are scaffolding: `EntityRegistry`,
  `FixedClock`, `DeterministicRandom`, `EventQueue` are used only by
  `headless_main` + tests; `JobSystem` now serves the save writer and the
  planet-material decode queue. A major
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
