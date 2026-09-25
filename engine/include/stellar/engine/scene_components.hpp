#pragma once

#include "stellar/engine/animation.hpp"
#include "stellar/engine/scene_document.hpp"
#include "stellar/engine/world.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_map {
class Mesh3D;
struct Quaternion;
}

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
  // Sprite-sheet animation: `frames` cells at `fps` per sim second.
  // `cols` is the sheet's columns per row — 0 means a horizontal strip.
  // `loop` false holds the last frame instead of wrapping (one-shots).
  int frames{1};
  float fps{0.0f};
  int cols{0};
  bool loop{true};
};
struct Rotation {
  // Degrees clockwise about the entity rect's center (sprites only).
  float value{0.0f};
};
struct Spin {
  // Angular velocity in deg/s — integrated into Rotation each sim step.
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
struct NoBounce {
  // Marker: clamps at world bounds instead of rebounding (projectiles,
  // debris — anything that should stop dead at the level edge).
};
struct UserData {
  // Freeform per-entity payload authored in the scene ("data" field).
  std::string value;
};
struct Opacity {
  // Draw alpha multiplier 0-1.
  float value{1.0f};
};
// Parent-child attachment: the entity follows the EntityName'd entity at a
// local offset. resolve_hierarchy folds any world-space edits since the
// last resolve (velocity integration, collision clamps, game writes) back
// into off_x/off_y, then snaps the transform to parent_pos + offset — so
// children follow AND can move locally. The string key survives snapshot
// id regeneration; `resolved` distinguishes a freshly attached component
// (use the offset verbatim) from a steady-state one (accumulate drift).
// Missing parents and cycles leave the child at its last position.
struct Parent {
  std::string name;
  float off_x{0.f}, off_y{0.f};     // local offset from the parent's origin
  float last_px{0.f}, last_py{0.f}; // parent's resolved pos at last resolve
  bool resolved{false};
};
struct VfxRef {
  // Named VfxSystem emitter to attach while the entity lives — the host
  // spawns it on scene load/runtime spawn and stops it on destroy.
  std::string name;
};
struct AnimTimeline {
  // Document animation clip driving this entity's channels each sim step.
  // `player` owns the live playhead; saved_* are restore scratch written by
  // the snapshot codec because the Timeline resolves by id only after the
  // world restores — the host re-attaches and seeks on load.
  std::string id;
  AnimationPlayer player;
  float saved_time{0.f};
  bool saved_playing{true};
};
// 3D scene components — the spatial counterparts spawned from a
// Scene3dDocument. Transforms carry a normalized quaternion orientation
// (authored euler degrees are converted at spawn) and uniform scale.
struct Transform3D {
  float x{}, y{}, z{};
  float qx{}, qy{}, qz{}, qw{1.f};
  float scale{1.f};
};
struct Velocity3D {
  float dx{}, dy{}, dz{};
};
struct MeshRef {
  // Mesh spec: "box", "sphere[:cols,rows]", "annulus:inner,outer[,seg]",
  // or a content-relative OBJ path ("models/ship.obj").
  std::string spec;
};
struct TextureRef {
  // Content-relative image applied as the 3D material's texture.
  std::string value;
};
struct DoubleSided {
  // Marker: render the mesh's back faces too (foliage, paper, debug).
};
// Metallic-workflow material for a 3D entity — the component counterpart
// of the entity document's metallic/roughness/emissive/environment
// fields. Map strings are content-relative paths the host resolves the
// same way as TextureRef. Scalars at defaults keep the legacy diffuse
// response; emission and IBL are strictly opt-in (strength 0).
struct MaterialPbr {
  float metallic{0.f}, roughness{0.55f};
  float emissive_strength{0.f}, night_emissive{0.f},
      environment_strength{0.f};
  float emissive_r{1.f}, emissive_g{1.f}, emissive_b{1.f};
  float alpha_cutout{0.f}, uv_tile_x{1.f}, uv_tile_y{1.f};
  std::string metallic_roughness, emissive, environment;
};
// Limb-scatter atmosphere shell on a 3D body — tinted (1-N.V)^power rim
// weighted to the day side with a nightside floor.
struct AtmosphereShell {
  float r{0.45f}, g{0.62f}, b{1.f};
  float strength{1.f}, power{3.f}, night_floor{0.05f};
};
// Host-owned fly-camera state for 3D scene mode, carried on a lazily
// resolved world entity so F5/F9 snapshots restore the camera too (the
// document seeds it only on scene load).
struct Camera3DState {
  // All-double members keep the layout padding-free — the snapshot codec
  // serializes members individually and requires the member list to cover
  // the whole struct, so mixed-width members would need explicit care.
  double x{}, y{}, z{3.0};
  double yaw_deg{}, pitch_deg{}, fov_deg{60.0};
};
// 3D positional attachment — same contract as Parent, with a z offset.
struct Parent3D {
  std::string name;
  float off_x{}, off_y{}, off_z{};
  float last_px{}, last_py{}, last_pz{};
  bool resolved{false};
};

// Grid terrain state, carried on dedicated world entities — one per
// document tilemap, absent from spawn_scene's return list (locate via
// tilemap_entities). Holding it as a component makes runtime cell edits
// (destructible terrain) part of the authoritative world snapshot.
struct Tilemap {
  std::string tileset;    // content-relative tile sheet image
  float x{0.f}, y{0.f};   // grid origin in world px
  int tile_w{32}, tile_h{32};
  int columns{0};         // cells per row; rows = cells.size()/columns
  int layer{-100};        // draw order vs entities
  float parallax{1.0f};
  bool collide{false};    // non-empty cells block/catch entities
  std::vector<int> cells; // row-major; <0 = empty
};

// Registers codecs for all scene components on `world`. Must run before
// snapshot()/restore() if those components are in use.
void register_scene_components(World &world);

// Spawns every entity in `doc` into `world` with the full component set
// (name/sprite only when non-empty) and returns the created ids. Each
// document tilemap spawns on a separate dedicated entity (Tilemap component
// only), in document order, NOT part of the returned list — find them with
// tilemap_entities(). The caller owns the ids' lifecycle — destroying
// previously spawned entities before a respawn is the caller's policy.
std::vector<EntityId> spawn_scene(World &world, const SceneDocument &doc);

// The live entity carrying the scene's FIRST Tilemap component, or nullopt.
std::optional<EntityId> tilemap_entity(const World &world);
// All entities carrying a Tilemap component, in spawn order.
std::vector<EntityId> tilemap_entities(const World &world);
// Document-order index of the tilemap whose EntityName matches, or nullopt
// — composes find_entity_by_name with tilemap_entities so games address
// authored layers ("ground", "decor") by name instead of position.
std::optional<std::size_t> tilemap_index(const World &world,
                                         std::string_view name);

// The inverse of spawn_scene: every live entity carrying EntityName (or, when
// unnamed, every entity with a Transform2D) becomes a SceneEntity built from
// its components, and every Tilemap component exports to doc.tilemaps in
// spawn order. Lets tools export live world state back to an editable,
// diffable document.
SceneDocument scene_from_world(const World &world);

// First live entity whose EntityName matches, or nullopt.
std::optional<EntityId> find_entity_by_name(const World &world,
                                            std::string_view name);

// Applies every Parent attachment: resolves each chain root-first (cycles
// ignored), folds the child's world-space drift since the previous resolve
// into its stored offset, and snaps its transform to parent+offset. Hosts
// call it once after spawning and each sim step after velocity/collision
// so contacts and rendering see final positions.
void resolve_hierarchy(World &world);

// Scene3dDocument counterparts: spawn/export/hierarchy over the 3D
// component set. spawn_scene3d returns the spawned ids in document order;
// scene3d_from_world exports every entity carrying a Transform3D.
// Authored 3D rotation is euler degrees (yaw about +Y, pitch about +X,
// roll about +Z, applied roll→pitch→yaw). These convert to/from the
// quaternion form the renderer, hierarchy and raycast consume — tools
// building previews from a document need the same convention.
native_map::Quaternion euler_to_quat3(float yaw_deg, float pitch_deg,
                                      float roll_deg);
void quat_to_euler3(const native_map::Quaternion &q, float &yaw_deg,
                    float &pitch_deg, float &roll_deg);

std::vector<EntityId> spawn_scene3d(World &world, const Scene3dDocument &doc);
Scene3dDocument scene3d_from_world(const World &world);
std::vector<EntityId> entities3d(const World &world);
void resolve_hierarchy3d(World &world);

// Nearest hit from raycast_world3d: the entity, the world-space
// distance along the ray, and the world-space hit point.
struct WorldRayHit3D {
  EntityId entity{};
  float distance{};
  float x{}, y{}, z{};
};
// Casts a ray (origin + dir*max_distance, dir need not be normalized)
// against every entity in `set` carrying Transform3D + MeshRef. Each ray
// transforms into the mesh's local frame (rotation + scale aware) and
// tests actual triangles via intersect_mesh_segment (bounding-sphere
// reject first). `resolve` maps a MeshRef spec to a mesh — see
// resolve_mesh_spec in mesh3d_loader.hpp. O(triangles) per hit entity —
// a query, not a per-frame sweep. Shared by RuntimeHost::raycast3d and
// tools that pick through a scratch world (spawn_scene3d on a document).
std::optional<WorldRayHit3D>
raycast_world3d(const World &world, std::span<const EntityId> set,
                const std::function<std::shared_ptr<
                    const native_map::Mesh3D>(const std::string &)>
                    &resolve,
                double ox, double oy, double oz, double dx, double dy,
                double dz, float max_distance);

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
