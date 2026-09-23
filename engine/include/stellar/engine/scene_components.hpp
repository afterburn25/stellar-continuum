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

// File-backed snapshot helpers: save_world_to_file snapshots the world and
// writes the checksummed binary atomically (throws on failure);
// load_world_from_file returns false instead of throwing when the file is
// absent, truncated, corrupt or version-mismatched, leaving the world
// untouched.
void save_world_to_file(const World &world,
                        const std::filesystem::path &path);
bool load_world_from_file(World &world, const std::filesystem::path &path);

} // namespace stellar::engine
