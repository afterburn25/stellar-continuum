#pragma once

#include "stellar/engine/scene_document.hpp"
#include "stellar/engine/world.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Canonical 2D scene components shared by engine tools and game hosts.
// Registering these once (register_scene_components) gives every World a
// stable persisted identity for the entity data a SceneDocument carries, so
// World::snapshot()/restore() round-trips authored scenes end-to-end.
struct Transform2D {
  float x{}, y{};
};
struct Velocity2D {
  float dx{}, dy{};
};
struct Extent2D {
  float w{}, h{};
};
struct Tint {
  std::uint8_t r{255}, g{255}, b{255};
};
struct EntityName {
  std::string value;
};
struct SpriteRef {
  // Content-relative image path (e.g. "data/logo.png"); hosts resolve it
  // against their content roots or cooked packages.
  std::string value;
};
struct Layer {
  // Draw order — higher layers render above lower ones.
  int value{};
};
struct Parallax {
  // Camera scroll factor: 1.0 follows the world, 0.0 pins to the screen.
  float value{1.0f};
};
struct Label {
  // Centered text drawn inside the entity's rect (button/caption text).
  std::string value;
};
struct GravityScale {
  // Multiplies the scene's gravity on this entity (0 ignores gravity).
  float value{1.0f};
};
// Marker component: gravity-affected entities land on Solid tops; solids
// are exempt from integration (they never move or fall).
struct Solid {};
struct Anim {
  // Sprite-strip animation: `frames` cells at `fps` per sim second.
  int frames{1};
  float fps{0.0f};
};
struct Rotation {
  // Degrees clockwise about the entity rect's center (sprites only).
  float value{0.0f};
};
struct Lifetime {
  // Remaining sim seconds before the host destroys the entity; counts down
  // each sim step. Absent component = immortal.
  float remaining{0.0f};
};
struct Flip {
  // Sprite mirroring across the destination axes.
  bool x{false};
  bool y{false};
};
struct Hidden {
  // Marker: entity simulates and collides but is skipped by the renderer.
};
struct Oneway {
  // Marker: landable from above, pass-through from sides and below.
};
struct UserData {
  // Freeform per-entity payload authored in the scene ("data" field).
  std::string value;
};
struct Opacity {
  // Draw alpha multiplier 0-1.
  float value{1.0f};
};

// Registers codecs for all scene components on `world`. Must run before
// snapshot()/restore() if those components are in use.
void register_scene_components(World &world);

// Spawns every entity in `doc` into `world` with the full component set
// (name/sprite only when non-empty) and returns the created ids. The caller
// owns the ids' lifecycle — destroying previously spawned entities before a
// respawn is the caller's policy.
std::vector<EntityId> spawn_scene(World &world, const SceneDocument &doc);

// The inverse of spawn_scene: every live entity carrying EntityName (or, when
// unnamed, every entity with a Transform2D) becomes a SceneEntity built from
// its components. Lets tools export live world state back to an editable,
// diffable document.
SceneDocument scene_from_world(const World &world);

// First live entity whose EntityName matches, or nullopt.
std::optional<EntityId> find_entity_by_name(const World &world,
                                            std::string_view name);

// File-backed snapshot helpers: save_world_to_file snapshots the world,
// rotates the .bak history chain (save_history.hpp) and writes the
// checksummed binary atomically (throws on failure). load_world_from_file
// returns false instead of throwing when the primary is absent, truncated,
// corrupt or version-mismatched — and first falls back through the rotated
// history slots newest-to-oldest, leaving the world untouched when every
// candidate fails.
void save_world_to_file(const World &world,
                        const std::filesystem::path &path);
bool load_world_from_file(World &world, const std::filesystem::path &path);

} // namespace stellar::engine
