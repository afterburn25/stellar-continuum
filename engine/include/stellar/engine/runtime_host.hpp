#pragma once

#include "stellar/engine/content_resolver.hpp"
#include "stellar/engine/scene_components.hpp"
#include "stellar/engine/vfx.hpp"
#include "stellar/engine/world.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
};

// A ready-made windowed 2D game host: owns the Window, package/content
// resolution, the ECS World, scene-document hot reload, WASD/arrow 'player'
// input, velocity integration + wall bounce, sprite rendering (cooked BC7 or
// loose images), audio playback and F5/F9 world quicksave/quickload. Games
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
  // The entity carrying the scene's Tilemap component (grid terrain), if
  // the document has one. Cell state is authoritative: mutate it through
  // world().get<Tilemap>(...) for destructible terrain — it snapshots with
  // F5/F9 quicksaves and hot-reloads with the scene document.
  [[nodiscard]] std::optional<EntityId> tilemap_entity() const;
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
  void set_camera(float x, float y, float zoom = 1.0f);
  [[nodiscard]] float camera_x() const;
  [[nodiscard]] float camera_y() const;
  [[nodiscard]] float camera_zoom() const;
  // Current window size (post --width/--height/--fullscreen overrides) —
  // follow-cameras need it to center: set_camera(px - vw/2, py - vh/2).
  [[nodiscard]] int viewport_width() const;
  [[nodiscard]] int viewport_height() const;

  // Spawns one entity at runtime (bullets, pickups, effects) — it joins the
  // tracked set: velocity integration, wall bounce, rendering, collisions.
  // Returns a default (null) id when called outside run().
  EntityId spawn_entity(const SceneEntity &entity);
  // Destroys a tracked entity; false for untracked/stale ids.
  bool destroy_entity(EntityId id);

  // Runs each rendered frame after input handling and scene polling, before
  // the built-in velocity integration. The place for game logic.
  std::function<void(World &, float dt)> on_update;
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

  // Owns the SDL loop; returns the process exit code. The argv overload
  // applies `--frames N` / `--fixed-hz N` / `--snapshot-out <path>` /
  // `--scene <path>` / `--width` / `--height` / `--fullscreen` /
  // `--world-w` / `--world-h` / `--speed` / `--move-speed` / `--jump` /
  // `--save <path>` overrides.
  int run();
  int run(int argc, char **argv);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace stellar::engine
