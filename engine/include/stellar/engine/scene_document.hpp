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
  // Sprite-sheet animation: `sprite` is a horizontal strip of `frames`
  // equal-sized cells played at `fps` frames per second of sim time.
  // 1/0 (defaults) draw the whole sprite.
  int frames{1};
  float fps{0.0f};
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
};

struct SceneDocument {
  std::vector<SceneEntity> entities;
  // Background clear color; defaults to the engine's dark space blue.
  std::uint8_t bg_r{8}, bg_g{16}, bg_b{26};
  // Downward acceleration in px/s² applied to entities' velocity each sim
  // step (scaled per entity by gravityScale). 0 disables gravity.
  float gravity{0.0f};

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
