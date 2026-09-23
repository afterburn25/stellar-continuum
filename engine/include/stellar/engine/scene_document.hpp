#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// A simple authored scene: a flat list of entities with the properties a
// starter/game loop needs to spawn them. This is the project's shared data
// contract between engine tools (which author it) and the game host (which
// consumes it) — deliberately a plain JSON document rather than a binary
// World snapshot so it is diffable and hand-editable.
struct SceneEntity {
  std::string name;
  float x{}, y{}, w{32.f}, h{32.f};
  float vx{}, vy{};
  std::uint8_t r{86}, g{196}, b{255};
  // Optional content-relative image (e.g. "data/logo.png") rendered instead
  // of the tinted rect; resolved under the project's content roots.
  std::string sprite;
  // Draw order — higher layers render above lower ones; equal layers keep
  // document order.
  int layer{};
  // Camera scroll factor: 1.0 (default) moves with the world, 0.0 pins the
  // entity to the screen (HUD/backdrop), 0.5 drifts at half speed.
  float parallax{1.0f};
  // Optional centered text label drawn inside the entity's rect (after any
  // sprite) — turns a tinted rect into a button or caption.
  std::string text;
  // Multiplies the document's gravity on this entity: 0 ignores gravity
  // (HUD, static scenery), 1 is full strength, 2 falls twice as fast.
  float gravity_scale{1.0f};
  // Static blocker: gravity-affected entities falling onto a solid land on
  // its top surface (platforms, ground). Solids never move or fall.
  bool solid{false};
  // Sprite-sheet animation: `sprite` is a sheet of `frames` equal-sized
  // cells played at `fps` frames per second of sim time. `fcols` is the
  // sheet's columns per row — 0 (default) treats it as a horizontal strip
  // (cols = frames); set it for grid sheets (RPG walk cycles, effects).
  int frames{1};
  float fps{0.0f};
  int fcols{0};
  // false = play once and hold the last frame (explosions, effects) —
  // pair with ttl to despawn when the clip ends.
  bool anim_loop{true};
  // Clockwise rotation in degrees — sprites rotate about their rect's
  // center; tinted rects ignore it (no rotated-fill primitive).
  float rotation{0.0f};
  // Seconds of sim time before the entity self-destructs; 0 = immortal.
  // For spawned effects (sparks, pickups) that should not persist.
  float ttl{0.0f};
  // Mirror the sprite horizontally/vertically (sprites only).
  bool flip_x{false};
  bool flip_y{false};
  // Invisible entities simulate and collide normally but are not drawn.
  bool visible{true};
  // One-way platform: entities land on its top but pass through the
  // sides and bottom. Independent of `solid` (which also side-blocks).
  bool oneway{false};
  // Freeform game data — spawn tags, patrol notes, door ids. The engine
  // carries it verbatim; games interpret it via the UserData component.
  std::string data;
  // Draw opacity 0-1 — sprites tint-modulate, rects use it directly.
  float opacity{1.0f};
  // Angular velocity in deg/s — integrates `rotation` each sim step.
  float spin{0.0f};
  // false = clamp dead at world bounds instead of rebounding (projectiles).
  bool bounce{true};
  // Name of another entity this one follows, keeping its authored offset —
  // riders on moving platforms, weapons on ships. Resolution folds the
  // child's own world-space motion (velocity, collisions) into the offset,
  // so children can also drift relative to their parent. Cycles and
  // missing parents are ignored (child keeps its last world position).
  std::string parent;
};

// Grid terrain layer: a tileset image sliced into tile_w/tile_h cells
// (indexed left-to-right, top-to-bottom) painted at grid positions.
struct SceneTilemap {
  std::string tileset;      // content-relative image path
  float x{0.f}, y{0.f};     // grid origin in world px — chunked/procedural
                            // maps place tiles at nonzero offsets
  int tile_w{32}, tile_h{32};
  int columns{0};           // map cells per row (rows = cells.size()/columns)
  int layer{-100};          // draw order vs entities (default: behind all)
  float parallax{1.0f};     // same semantics as entity parallax
  // When true, every non-empty cell acts as a solid for landing and
  // side-blocking — platform terrain authored in the document.
  bool collide{false};
  // One tileset cell index per grid cell, row-major; <0 = empty.
  std::vector<int> cells;
};

struct SceneDocument {
  std::vector<SceneEntity> entities;
  // Grid terrain layers — empty in most scenes; each draws at its own
  // layer between entities (background grids, collision ground,
  // foreground overlays). Parses legacy single-"tilemap" documents too.
  std::vector<SceneTilemap> tilemaps;
  // Background clear color; defaults to the engine's dark space blue.
  std::uint8_t bg_r{8}, bg_g{16}, bg_b{26};
  // Downward acceleration in px/s² applied to entities' velocity each sim
  // step (scaled per entity by gravityScale). 0 disables gravity.
  float gravity{0.0f};
  // Content-relative music track played when the scene loads (empty keeps
  // whatever is playing — lets levels share a track or swap it).
  std::string music;
  // Level world bounds in px — 0 inherits the runtime's --world-w/--world-h
  // option or the viewport. Lets each scene own its playable extent.
  float world_w{0.0f}, world_h{0.0f};

  static constexpr std::string_view filename{"scene.json"};

  std::string to_json() const;
  // All-or-nothing parse: nullopt + `error` on any malformed field.
  static std::optional<SceneDocument> from_json(std::string_view text,
                                                std::string *error = nullptr);
  // Atomic write via write_file_atomically; throws on failure.
  void save(const std::filesystem::path &path) const;
  static std::optional<SceneDocument> load(const std::filesystem::path &path,
                                           std::string *error = nullptr);
};

} // namespace stellar::engine
