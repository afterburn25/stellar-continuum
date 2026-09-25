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
  // Name of a registered VfxSystem emitter to attach on spawn — the host
  // keeps it anchored to the entity and stops it when the entity dies.
  // Games register definitions via host.vfx().define(...).
  std::string vfx;
  // Name of another entity this one follows, keeping its authored offset —
  // riders on moving platforms, weapons on ships. Resolution folds the
  // child's own world-space motion (velocity, collisions) into the offset,
  // so children can also drift relative to their parent. Cycles and
  // missing parents are ignored (child keeps its last world position).
  std::string parent;
  // Id of a document `animations` clip driving this entity's channels each
  // sim step — keyframed patrols, door slides, opacity pulses. A track
  // owns its channel while the clip plays.
  std::string anim;
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
  // Optional map name ("ground", "decor") — attaches EntityName to the
  // spawned carrier so games can resolve layers with tilemap_index()
  // instead of tracking document-order indices.
  std::string name;
};

// Named scalar-track animation shared by entities — keyframed motion,
// sizing, opacity and tint authored once and referenced by `anim` on any
// number of entities. Track keys use the same (time,value) list shape as
// emitter curves; `events` are (time,name) markers the host forwards to
// the game as the playhead crosses them.
struct SceneAnimationDef {
  std::string id;
  std::string loop{"loop"}; // "once" | "loop" | "pingpong"
  // channel -> (time,value) keys. Channels: x y w h vx vy opacity
  // rotation spin tintR tintG tintB — each owned by the track while the
  // entity plays the clip (a track sets the field every sim step).
  std::vector<std::pair<std::string, std::vector<std::pair<float, float>>>>
      tracks;
  std::vector<std::pair<float, std::string>> events;
};

// Declarative particle emitter definition — registers into the runtime's
// VfxSystem on scene load so entity `vfx` fields need no game code.
// Curves are (normalized-age, value) key lists.
struct SceneEmitterDef {
  std::string id;
  std::string sprite;              // content-relative particle texture
  float rate{0.f};                 // particles spawned per sim second
  float lifetime{1.0f};            // per-particle lifetime in seconds
  float vx_min{}, vx_max{};        // initial velocity range (px/s)
  float vy_min{}, vy_max{};
  float spread_deg{0.f};           // cone spread around velocity, degrees
  float gx{}, gy{};                // per-particle gravity
  std::vector<std::pair<float, float>> scale_keys;    // over-life
  std::vector<std::pair<float, float>> opacity_keys;
  std::vector<std::pair<float, float>> tint_r, tint_g, tint_b;
  std::uint32_t max_particles{256};
  float lod_fade_distance{0.f};
  float lod_min_rate_scale{0.f};
};

// A 3D scene entity: a mesh instance placed in world space. `mesh` names a
// primitive ("box", "sphere", "annulus:inner,outer[,segments]") or a
// content-relative OBJ path ("models/ship.obj"). Rotation is authored as
// euler degrees (yaw Y, pitch X, roll Z — applied in that order).
struct Scene3dEntity {
  std::string name;
  std::string mesh{"box"};
  float x{}, y{}, z{};
  float yaw_deg{}, pitch_deg{}, roll_deg{};
  float scale{1.0f};
  float vx{}, vy{}, vz{};
  std::uint8_t r{255}, g{255}, b{255}, a{255};
  // Optional content-relative texture applied to the mesh.
  std::string texture;
  float opacity{1.0f};
  bool double_sided{false};
  // Multiplies the document's gravity (pulls -Y); 0 ignores it.
  float gravity_scale{1.0f};
  // Static blocker for sphere-collision events; solids never move.
  bool solid{false};
  // Seconds of sim time before self-destruct; 0 = immortal.
  float ttl{0.0f};
  // Freeform game data, carried verbatim into the UserData component.
  std::string data;
  // Name of another 3D entity to follow at its authored offset.
  std::string parent;
  // Named emitter from the document's `emitters` table, attached on
  // spawn — particles anchor to the entity's projected screen position.
  std::string vfx;
  // Metallic-workflow material. All members are optional — a document that
  // leaves them at defaults renders exactly the legacy diffuse path.
  float metallic{0.f}, roughness{0.55f};
  // Packed metallic/roughness map (glTF convention: G = roughness scale,
  // B = metallic), content-relative.
  std::string metallic_roughness;
  // Emissive radiance map (colony lights, engine glow), content-relative.
  std::string emissive;
  // 0 disables emission; scales emissive map × emissive tint.
  float emissive_strength{0.f};
  float emissive_r{1.f}, emissive_g{1.f}, emissive_b{1.f};
  // 0 = emit everywhere; 1 = emit only across the terminator (night side).
  float night_emissive{0.f};
  // Equirect radiance map feeding diffuse irradiance + specular
  // environment response for this material, content-relative.
  std::string environment;
  float environment_strength{0.f}; // 0 disables IBL
  // Alpha cutout: fragments below this discard (lattices, decals).
  float alpha_cutout{0.f};
  // Surface texture repeat, per axis; (1,1) disables tiling.
  float uv_tile_x{1.f}, uv_tile_y{1.f};
  // Single-scatter limb atmosphere: tinted rim weighted to the day side.
  // strength 0 leaves the body's authored art untouched.
  float atmo_strength{0.f}, atmo_power{3.f}, atmo_night{0.05f};
  float atmo_r{0.45f}, atmo_g{0.62f}, atmo_b{1.f};
  // Distance culling: hidden once the camera is farther than this many
  // world units from the bounding-sphere surface. 0 = always visible.
  float visible_range{0.f};
  // Opaque surface response — content-relative maps; any subset binds.
  // The cloud map's alpha self-shadows the surface (cloud_opacity) and
  // its RGB can composite as a visible deck (cloud_albedo), drifted by
  // cloud_offset. Packed properties are roughness / liquid / ice /
  // height; height also drives relief parallax.
  std::string normal_map, properties_map, cloud_map;
  float normal_strength{0.35f}, relief{0.f}, cloud_opacity{0.f};
  float cloud_albedo{0.f}, cloud_offset_x{0.f}, cloud_offset_y{0.f};
  // Wrap-diffuse terminator softening [0,1]; 0 keeps Lambert shading.
  float terminator_wrap{0.f};
};

// An extra directional light — the material pipeline evaluates at most
// two of these in addition to the scene's key light.
struct Scene3dLight {
  float dir_x{0.f}, dir_y{0.f}, dir_z{1.f};
  float r{1.f}, g{1.f}, b{1.f};
  float intensity{0.5f};
};

// A world-space point light — station floods, engine glow, muzzle light.
// The material pipeline evaluates at most four per scene; range 0 keeps
// pure inverse-square falloff instead of a hard window.
struct Scene3dPointLight {
  float x{}, y{}, z{};
  float r{1.f}, g{1.f}, b{1.f};
  float intensity{1.f};
  float range{0.f};
};

// A 3D scene: camera, key light, and mesh entities — the 3D counterpart of
// SceneDocument, authored by tools and consumed by RuntimeHost's --scene3d
// mode. Same contract: diffable JSON, all-or-nothing parse.
struct Scene3dDocument {
  std::vector<Scene3dEntity> entities;
  // Camera: world position + yaw/pitch (degrees; 0,0 looks down -Z),
  // vertical fov and clip planes.
  float cam_x{0.f}, cam_y{0.f}, cam_z{3.f};
  float cam_yaw_deg{0.f}, cam_pitch_deg{0.f};
  float fov_deg{60.f};
  float near_plane{0.01f}, far_plane{1000.f};
  // Key light direction (world-space; the host rotates it into camera
  // space at render time) + intensity multiplier.
  float light_x{0.42f}, light_y{0.2f}, light_z{0.87f};
  float light_intensity{1.0f};
  // Up to two additional world-space directional lights (fill/rim).
  std::vector<Scene3dLight> lights;
  // World-space point lights; at most four reach the fragment pipeline.
  std::vector<Scene3dPointLight> point_lights;
  // Post-processing applied to the 3D view's HDR resolve. Exposure is a
  // linear pre-tonemap multiplier (1 = neutral), bloom is an additive mip
  // halo above its luminance threshold, contrast pivots about 0.18.
  float exposure{1.f};
  float bloom{0.f}, bloom_threshold{1.f};
  float contrast{1.f}, saturation{1.f}, sharpen{0.f};
  // Quality tier for expensive per-view effects: low|medium|high|ultra.
  std::string quality{"high"};
  // Diagnostic shading override for the 3D view:
  // lit|unlit|albedo|normals|roughness|metallic|emissive|lighting.
  std::string debug_view{"lit"};
  // Key-light directional shadow map: an ortho coverage volume centred
  // shadow_distance units along camera forward. shadow_extent<=0 disables;
  // resolution 0 picks the quality-tier default (1024/2048/4096).
  float shadow_extent{0.f}, shadow_distance{64.f}, shadow_depth{256.f};
  float shadow_strength{1.f}, shadow_bias{0.0005f};
  std::uint32_t shadow_resolution{0};
  // Background clear color.
  std::uint8_t bg_r{8}, bg_g{16}, bg_b{26};
  // Downward (-Y) acceleration in units/s²; 0 disables gravity.
  float gravity{0.0f};
  // Ground plane height — gravity entities rest at rest_height + their
  // mesh radius * scale. Defaults to 0 (the XZ plane).
  float ground_y{0.0f};
  // XZ play-bounds half-extent — 0 disables clamping.
  float bounds{0.0f};
  // Content-relative music track played when the scene loads.
  std::string music;
  // Reuses the 2D emitter table — attached emitters anchor to entity
  // centers in the 3D view's projected space.
  std::vector<SceneEmitterDef> emitters;

  static constexpr std::string_view filename{"scene3d.json"};

  std::string to_json() const;
  static std::optional<Scene3dDocument>
  from_json(std::string_view text, std::string *error = nullptr);
  void save(const std::filesystem::path &path) const;
  static std::optional<Scene3dDocument>
  load(const std::filesystem::path &path, std::string *error = nullptr);
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
  // Particle emitter definitions registered into the host's VfxSystem on
  // load — entity `vfx` fields reference these by id (game-registered
  // definitions via host.vfx().define() still work).
  std::vector<SceneEmitterDef> emitters;
  // Keyframed entity animations resolved by `anim` fields on load —
  // the host builds one shared Timeline per def and steps a playhead
  // per referencing entity.
  std::vector<SceneAnimationDef> animations;

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
