#pragma once

#include "stellar/engine/content_resolver.hpp"
#include "stellar/engine/input_actions.hpp"
#include "stellar/engine/scene_components.hpp"
#include "stellar/engine/vfx.hpp"
#include "stellar/engine/world.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stellar::native_map {
class Window;
struct DrawList;
struct InputEvent;
} // namespace stellar::native_map

namespace stellar::engine {
namespace audio {
class AudioOutput;
} // namespace audio

struct RuntimeHostOptions {
  // Base package namespace (e.g. "game.my-game"); its content/ directory is
  // the loose-content root and it is protected against mod overrides.
  std::string package_id;
  std::string window_title{"Stellar Game"};
  int width{1280}, height{720};
  bool fullscreen{false};
  // World-space bounds for the built-in wall bounce, independent of the
  // window size — pair with set_camera for worlds larger than one screen.
  // 0 (default) uses the viewport size (a single-screen world).
  float world_width{0.0f}, world_height{0.0f};
  // Working root for packages/, mods/, build/cooked/ and editor/.
  std::filesystem::path project_root{"."};
  // Polled for changes; saving it hot-reloads the running scene.
  std::string scene_file{"editor/scene.json"};
  std::string save_file{"saves/quicksave.stw"};
  // Optional clips resolved through the ContentResolver.
  std::string music_clip{"audio/music.wav"};
  std::string music_clip_alt{"audio/music.mp3"};
  std::string bounce_clip{"audio/bounce.wav"};
  std::string bounce_clip_alt{"audio/bounce.mp3"};
  // Simulation step rate: 0 (default) integrates per rendered frame with
  // wall-clock dt; >0 accumulates real time and steps on_update + movement
  // at a fixed rate so simulation is deterministic regardless of FPS.
  double fixed_timestep_hz{0.0};
  // 0 = run until quit; >0 exits after that many rendered frames — lets CI
  // and scripts smoke-test that a built game starts and ticks.
  int frame_limit{0};
  // Simulation speed multiplier — 1.0 default; 0.5 half-speed, 2.0 double.
  // Scales the dt each step sees, so fixed-step determinism is preserved.
  double time_scale{1.0};
  // When non-empty, the world snapshot is written here on exit — combine
  // with --fixed-hz/--frames to compare runs byte-for-byte.
  std::filesystem::path snapshot_out;
  // 'player' entity tuning: held-key velocity and the gravity-mode jump
  // impulse, both in world units/second.
  float player_move_speed{320.f};
  float player_jump_impulse{520.f};
  // Optional project-relative input map JSON (InputMapper contexts). When
  // absent/unreadable the built-in defaults apply: move_left/right/up/down
  // on WASD+arrows, jump on Space/W/Up.
  std::string input_map;
  // Seed for the host-owned deterministic RNG (rng()) — every generated
  // game defaults to the same stream so runs and replays match; --seed
  // varies it. The state lives on a world entity, so F5/F9 snapshots
  // capture it: restoring a save restores the RNG position too.
  std::uint64_t seed{0x9E3779B97F4A7C15ull};
  // 3D scene mode: when true the host loads `scene3d_file` (a
  // Scene3dDocument) into the same World — entities carry Transform3D/
  // Velocity3D/MeshRef components, integrate velocity + gravity, collide
  // as spheres (on_collision/on_land), and render through the engine's
  // GPU Scene3D pipeline composited under the 2D overlay (2D scene
  // entities still draw on top as HUD). The camera is a fly camera:
  // WASD move, Space/Ctrl up/down, right-drag mouse-look, arrows turn.
  bool scene3d{false};
  std::string scene3d_file{"editor/scene3d.json"};
  // Fly-camera move speed in 3D world units/second.
  float fly_speed{4.0f};
};

// A ready-made windowed 2D game host: owns the Window, package/content
// resolution, the ECS World, scene-document hot reload, WASD/arrow 'player'
// input, velocity integration + wall bounce, sprite rendering (cooked BC7 or
// loose images), audio playback and F5/F9 world quicksave/quickload. Saves
// record the resolved package load plan in a "<save>.packages.json" sidecar;
// loads verify it and report missing/version-mismatched packages through
// RuntimeDiagnostics (report-only — the load still proceeds). Games
// customize through the callbacks rather than reimplementing the loop — the
// same role Unreal's GameInstance plays for its projects.
class RuntimeHost {
public:
  explicit RuntimeHost(RuntimeHostOptions options);
  ~RuntimeHost();
  RuntimeHost(RuntimeHost &&) noexcept;
  RuntimeHost &operator=(RuntimeHost &&) noexcept;
  RuntimeHost(const RuntimeHost &) = delete;
  RuntimeHost &operator=(const RuntimeHost &) = delete;

  [[nodiscard]] World &world();
  [[nodiscard]] const ContentResolver &content() const;
  // Valid only while run() is on the stack (i.e. inside callbacks) — the
  // output device is scoped to the SDL loop so it tears down before the
  // window does.
  [[nodiscard]] audio::AudioOutput &audio();
  // The entity named "player" in the active scene, if any.
  [[nodiscard]] std::optional<EntityId> player() const;
  // A tracked entity by its authored scene name — doors, waypoints,
  // triggers. "player" is just the conventional movement-driven one.
  [[nodiscard]] std::optional<EntityId>
  find_entity(std::string_view name) const;
  // Seconds of simulated time elapsed since run() started — fixed-step
  // deterministic under --fixed-hz (scaled by --speed).
  [[nodiscard]] double sim_time() const;
  // The rebindable action layer: built-in "game" context
  // (move_left/right/up/down, jump) drives the player, and games can push
  // their own contexts or load a map file (RuntimeHostOptions::input_map).
  // Action state updates from the same events the callbacks observe.
  [[nodiscard]] InputMapper &input();
  // The entity carrying the scene's first Tilemap component (grid
  // terrain), if the document has one. Cell state is authoritative:
  // mutate it through world().get<Tilemap>(...) for destructible
  // terrain — it snapshots with F5/F9 quicksaves and hot-reloads with
  // the scene document.
  [[nodiscard]] std::optional<EntityId> tilemap_entity() const;
  // Every tilemap carrier, in document order — layered grids (background
  // decoration, collision ground, foreground overlay) each get one.
  [[nodiscard]] std::vector<EntityId> tilemap_entities() const;
  // World-space cell queries: tile value under a point (-1 = empty or no
  // tilemap), and write the cell under a point (false out of bounds).
  // The no-index overloads address the FIRST tilemap; the indexed forms
  // take a document-order map index into tilemap_entities().
  [[nodiscard]] int tile_at(float world_x, float world_y) const;
  bool set_tile_at(float world_x, float world_y, int value);
  [[nodiscard]] int tile_at(std::size_t map, float world_x,
                            float world_y) const;
  bool set_tile_at(std::size_t map, float world_x, float world_y,
                   int value);
  // Name overloads resolve through tilemap_index() each call — the right
  // shape for infrequent queries; hot loops should hoist the index.
  [[nodiscard]] int tile_at(std::string_view map_name, float world_x,
                            float world_y) const;
  bool set_tile_at(std::string_view map_name, float world_x,
                   float world_y, int value);
  // Number of tilemap layers in the loaded scene.
  [[nodiscard]] std::size_t tilemap_count() const;
  // Document-order index of the tilemap named in the scene document
  // (SceneTilemap::name attaches EntityName to the carrier), or nullopt —
  // feed the result to the indexed tile_at/set_tile_at overloads, or call
  // their name overloads directly for infrequent queries.
  [[nodiscard]] std::optional<std::size_t>
  tilemap_index(std::string_view name) const;
  // Spawns a new tilemap layer at runtime (procedural terrain): creates
  // a dedicated Tilemap-component entity that renders, collides and
  // snapshots like a scene-authored map, appended after the existing
  // layers. Returns a default (null) id when called outside run().
  EntityId spawn_tilemap(const SceneTilemap &map);
  // Destroys a tilemap carrier — scene-authored or runtime-spawned.
  bool destroy_tilemap(EntityId id);
  // The deterministic particle system — games define() emitters then
  // spawn_emitter() to run them; the host steps it in sim time and
  // renders particles as tinted rects above the scene.
  [[nodiscard]] VfxSystem &vfx();
  // Spawns a defined emitter at a world position. When `attached` names
  // a live entity the emitter re-anchors to its center each sim step and
  // stops automatically when the entity dies. Positions are world-space
  // (camera transform applies).
  VfxInstanceId spawn_emitter(std::string_view definition_id, float x,
                              float y, EntityId attached = {});

  // Game-driven control, callable from the callbacks:
  // ends the run loop after the current frame (the clean-exit path —
  // window teardown, snapshot_out, exit code — all still run).
  void request_quit();
  // Pauses/resumes the simulation; rendering and callbacks continue.
  // The P key toggles the same flag.
  void set_paused(bool paused);
  [[nodiscard]] bool paused() const;
  // Runtime-adjustable sim speed (RuntimeHostOptions::time_scale).
  void set_time_scale(double scale);
  [[nodiscard]] double time_scale() const;
  // Switches the active scene document (project-relative path), respawning
  // entities — level switching. Before run() it sets the initial scene.
  void set_scene(std::string scene_file);
  // View transform in world units: entities draw at
  // (world_pos - camera) * zoom. A platformer follows its player by calling
  // this from on_update. Zoom 1 is the identity view; the built-in HUD text
  // stays screen-space.
  // When true, set_camera clamps to the resolved world bounds so the view
  // never scrolls past the level edges.
  bool clamp_camera{false};
  void set_camera(float x, float y, float zoom = 1.0f);
  [[nodiscard]] float camera_x() const;
  [[nodiscard]] float camera_y() const;
  [[nodiscard]] float camera_zoom() const;
  // Current window size (post --width/--height/--fullscreen overrides) —
  // follow-cameras need it to center: set_camera(px - vw/2, py - vh/2).
  [[nodiscard]] int viewport_width() const;
  [[nodiscard]] int viewport_height() const;
  // Resolved level bounds (--world-w/h argv > scene worldSize > viewport)
  // — spawn limits, AI roam ranges, minimap scaling.
  [[nodiscard]] float world_width() const;
  [[nodiscard]] float world_height() const;
  // Converts a window-space point (event position, pointer) into world
  // space under the current camera — click-to-move, aiming, picking.
  // Assumes the gameplay plane (parallax 1).
  [[nodiscard]] std::pair<float, float> screen_to_world(float sx,
                                                        float sy) const;
  // Topmost tracked entity whose drawn bounds contain a window-space
  // point — highest layer wins, later document order breaks ties.
  // Per-entity parallax and zoom are applied, so HUD (parallax 0) and
  // world entities pick correctly under a moving camera. Hidden
  // entities and tilemap carriers never match (query maps with
  // tile_at).
  [[nodiscard]] std::optional<EntityId> entity_at(float screen_x,
                                                  float screen_y) const;

  // World-space region queries — AoE damage, aggro ranges, selection
  // boxes, trigger zones. Every tracked entity is tested (Hidden
  // included: they still simulate and collide); tilemap carriers are
  // skipped (query maps with tile_at). Returned in document order.
  // entities_in_rect intersects each entity's bounds; entities_in_radius
  // tests center points.
  [[nodiscard]] std::vector<EntityId>
  entities_in_rect(float x, float y, float w, float h) const;
  [[nodiscard]] std::vector<EntityId>
  entities_in_radius(float x, float y, float radius) const;

  // Spawns one entity at runtime (bullets, pickups, effects) — it joins the
  // tracked set: velocity integration, wall bounce, rendering, collisions.
  // Returns a default (null) id when called outside run().
  EntityId spawn_entity(const SceneEntity &entity);
  // Destroys a tracked entity; false for untracked/stale ids.
  bool destroy_entity(EntityId id);

  // --- 3D scene mode (--scene3d) ---
  // True when a Scene3dDocument drives the world. spawn_entity3d adds a
  // mesh entity mid-game; entities3d lists the 3D tracked set (kept
  // separate from the 2D gameplay list); entities3d_in_radius runs a
  // sphere query (aggro/AoE/pick volumes).
  [[nodiscard]] bool scene3d() const;
  EntityId spawn_entity3d(const Scene3dEntity &entity);
  [[nodiscard]] std::vector<EntityId> entities3d() const;
  [[nodiscard]] std::vector<EntityId>
  entities3d_in_radius(float x, float y, float z, float radius) const;
  // Fly camera: world position + yaw/pitch degrees (0,0 looks down -Z).
  void set_camera3d(double x, double y, double z, float yaw_deg,
                    float pitch_deg);
  [[nodiscard]] double camera3d_x() const;
  [[nodiscard]] double camera3d_y() const;
  [[nodiscard]] double camera3d_z() const;
  [[nodiscard]] float camera3d_yaw() const;
  [[nodiscard]] float camera3d_pitch() const;
  [[nodiscard]] float camera3d_fov() const;
  void set_camera3d_fov(float fov_deg);
  // Scene-level 3D tuning the document owns (readable for game logic).
  [[nodiscard]] float gravity3d() const;
  [[nodiscard]] float ground_y() const;
  // Hit record from a 3D ray query.
  struct RaycastHit3D {
    EntityId entity{};
    float distance{};
    float x{}, y{}, z{}; // world-space hit point
  };
  // Casts a ray against the 3D set's actual mesh triangles (hitscan
  // weapons, LOS checks, mouse picking). The ray transforms into each
  // mesh's local space (rotation+scale aware); returns the nearest hit.
  // O(triangles) per entity — fine for queries, not per-frame sweeps.
  [[nodiscard]] std::optional<RaycastHit3D>
  raycast3d(double ox, double oy, double oz, float dx, float dy,
            float dz, float max_distance) const;
  // Screen-space pick: builds the camera ray through a viewport pixel
  // and raycasts it — the 3D counterpart of entity_at().
  [[nodiscard]] std::optional<RaycastHit3D>
  entity3d_at(float screen_x, float screen_y) const;

  // Named game-data blobs under <root>/saves/data/<key>.dat — quest flags,
  // inventories, settings, anything the world snapshot doesn't cover.
  // save_data writes atomically through the same rotating .bak history
  // chain as world snapshots; load_data walks the chain newest-first when
  // the primary is absent/corrupt. Keys accept [A-Za-z0-9._-] only —
  // anything else fails (false / nullopt).
  bool save_data(std::string_view key, std::span<const std::byte> bytes);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>>
  load_data(std::string_view key) const;

  // Runs each rendered frame after input handling and scene polling, before
  // the built-in velocity integration. The place for game logic.
  std::function<void(World &, float dt)> on_update;
  // The game's deterministic random stream — loot, spawns, AI rolls. Lives
  // on a dedicated world entity so the state snapshots and restores with
  // F5/F9 saves (never use std::rand — it breaks run-to-run determinism).
  [[nodiscard]] engine::DeterministicRandom &rng();
  // Observed after the host's own handling (Escape/F5/F9/held keys).
  std::function<void(const native_map::InputEvent &)> on_event;
  // Appended under the built-in status line when non-empty.
  std::function<std::string()> on_status;
  // Extra overlay primitives each frame, drawn above the scene entities.
  std::function<void(native_map::DrawList &, float w, float h)> on_draw;
  // Fires when two tracked entities' AABBs begin overlapping — once per
  // pair per contact, not every frame. Runs inside the sim step.
  std::function<void(EntityId a, EntityId b)> on_collision;
  // Fires when an overlapping pair separates or one member is destroyed —
  // the ids may already be stale. Runs inside the sim step.
  std::function<void(EntityId a, EntityId b)> on_collision_exit;
  // Fires when a falling entity lands on a solid/oneway entity — footsteps,
  // landing damage. Contact events don't cover this (resting is adjacent,
  // not overlapping). Runs inside the sim step.
  std::function<void(EntityId entity, EntityId ground)> on_land;
  // Fires when a falling entity lands on a colliding tilemap cell —
  // map/cx/cy/tile identify the exact cell (terrain damage, per-tile
  // sounds). Runs inside the sim step.
  std::function<void(EntityId entity, std::size_t map, int cx, int cy,
                     int tile)>
      on_tile_land;
  // Fires once per entity when a scene spawns (load, hot-reload, runtime
  // spawn_entity) — attach game-defined components keyed off the authored
  // `data`/`name` fields. Runs synchronously during the spawn call.
  std::function<void(World &, EntityId, const SceneEntity &)> on_spawn;
  // 3D counterpart — fires per spawned Scene3dEntity (scene3d mode).
  std::function<void(World &, EntityId, const Scene3dEntity &)> on_spawn3d;
  // Named Timeline events crossed while a scene-authored `anim` clip
  // advances — door-opened markers, patrol turnarounds. Fires once per
  // crossing inside the sim step.
  std::function<void(const std::string &event, EntityId entity)>
      on_anim_event;

  // Owns the SDL loop; returns the process exit code. The argv overload
  // applies `--frames N` / `--fixed-hz N` / `--snapshot-out <path>` /
  // `--scene <path>` / `--width` / `--height` / `--fullscreen` /
  // `--world-w` / `--world-h` / `--speed` / `--move-speed` / `--jump` /
  // `--save <path>` / `--input-map <path>` / `--seed` overrides.
  int run();
  int run(int argc, char **argv);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace stellar::engine
