#include "stellar/engine/runtime_host.hpp"

#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/broadphase.hpp"
#include "stellar/engine/mesh3d_loader.hpp"
#include "stellar/engine/native_audio.hpp"
#include "stellar/engine/native_geometry3d.hpp"
#include "stellar/engine/native_map_platform.hpp"
#include "stellar/engine/native_scene3d.hpp"
#include "stellar/engine/package.hpp"
#include "stellar/engine/replay.hpp"
#include "stellar/engine/runtime_diagnostics.hpp"
#include "stellar/engine/runtime_paths.hpp"
#include "stellar/engine/save_history.hpp"
#include "stellar/engine/texture_cook.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stellar::engine {

using namespace stellar::native_map;

namespace {
using native_map::rotate_vec;
// Hamilton product — a∘b applies b's rotation first.
Quaternion quat_mul(Quaternion a, Quaternion b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// InputEvent <-> journal payload. Positional CSV of every field that
// affects dispatch, then the TextEntered payload hex-encoded (commas are
// legal text) as the trailing field.
std::string encode_input_event(const InputEvent &e) {
  static const char hex[] = "0123456789abcdef";
  std::string text_hex;
  text_hex.reserve(e.text.size() * 2);
  for (const unsigned char ch : e.text) {
    text_hex += hex[ch >> 4];
    text_hex += hex[ch & 15];
  }
  char buf[256];
  std::snprintf(buf, sizeof buf, "%d,%u,%g,%g,%g,%g,%g,%u,%d,%d,%d,%u,%u,%u,%g",
                static_cast<int>(e.type), e.key, e.position.x, e.position.y,
                e.delta.x, e.delta.y, e.wheel_y, e.click_count,
                e.control ? 1 : 0, e.shift ? 1 : 0, e.alt ? 1 : 0,
                e.gamepad_button, e.gamepad_device, e.gamepad_axis,
                e.gamepad_axis_value);
  std::string out{buf};
  out += ',';
  out += text_hex;
  return out;
}

std::optional<InputEvent> decode_input_event(std::string_view payload) {
  InputEvent e;
  // Split into at most 16 fields; the last field is the hex text.
  std::vector<std::string_view> fields;
  std::size_t pos = 0;
  while (fields.size() < 15) {
    const auto comma = payload.find(',', pos);
    if (comma == std::string_view::npos) return std::nullopt;
    fields.push_back(payload.substr(pos, comma - pos));
    pos = comma + 1;
  }
  fields.push_back(payload.substr(pos));
  const auto num = [](std::string_view s, double &v) {
    try {
      v = std::stod(std::string(s));
      return true;
    } catch (const std::exception &) {
      return false;
    }
  };
  double v[15];
  for (int i = 0; i < 15; ++i)
    if (!num(fields[i], v[i])) return std::nullopt;
  e.type = static_cast<InputEventType>(static_cast<int>(v[0]));
  e.key = static_cast<std::uint32_t>(v[1]);
  e.position = {static_cast<float>(v[2]), static_cast<float>(v[3])};
  e.delta = {static_cast<float>(v[4]), static_cast<float>(v[5])};
  e.wheel_y = static_cast<float>(v[6]);
  e.click_count = static_cast<std::uint8_t>(v[7]);
  e.control = v[8] != 0;
  e.shift = v[9] != 0;
  e.alt = v[10] != 0;
  e.gamepad_button = static_cast<std::uint8_t>(v[11]);
  e.gamepad_device = static_cast<std::uint8_t>(v[12]);
  e.gamepad_axis = static_cast<std::uint8_t>(v[13]);
  e.gamepad_axis_value = static_cast<float>(v[14]);
  const std::string_view text_hex = fields[15];
  if (text_hex.size() % 2 != 0) return std::nullopt;
  for (std::size_t i = 0; i < text_hex.size(); i += 2) {
    const auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    const int hi = nib(text_hex[i]), lo = nib(text_hex[i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    e.text += static_cast<char>((hi << 4) | lo);
  }
  return e;
}
} // namespace

struct RuntimeHost::Impl {
  RuntimeHostOptions options;
  World world;
  std::unique_ptr<ContentResolver> content;
  // Points at run()'s scoped AudioOutput — the device must shut down before
  // the Window quits SDL, so it cannot be a member.
  audio::AudioOutput *audio = nullptr;
  std::vector<EntityId> entities;
  std::vector<std::shared_ptr<const RgbaImage>> sprites;
  std::optional<EntityId> player;
  // Rebindable action layer; built-in "game" context feeds player movement.
  InputMapper input;
  bool quit_requested = false;
  bool paused = false;
  double time_scale = 1.0;
  // Assigned inside run(); applies a scene switch immediately.
  std::function<void(const std::string &)> switch_scene;
  // Assigned inside run(); runtime entity spawn/destroy entry points.
  std::function<EntityId(const SceneEntity &)> spawn_fn;
  std::function<bool(EntityId)> destroy_fn;
  std::function<EntityId(const SceneTilemap &)> spawn_tilemap_fn;
  std::function<bool(EntityId)> destroy_tilemap_fn;
  // AABB pairs currently overlapping — collision-enter events only fire
  // on the transition into this set.
  std::set<std::pair<std::uint64_t, std::uint64_t>> overlapping;
  float cam_x = 0.f, cam_y = 0.f, cam_zoom = 1.f;
  int view_w = 0, view_h = 0;
  // Clear color from the active scene document (defaults when absent).
  std::uint8_t bg_r = 8, bg_g = 16, bg_b = 26;
  // Scene gravity in px/s² (document-level sim setting, like the bg color).
  float gravity = 0.f;
  // Resolved bounce bounds — viewport-sized unless world_width/height set.
  float world_w = 0.f, world_h = 0.f;
  // Scene-declared bounds (SceneDocument::world_w/h); argv options win.
  float scene_world_w = 0.f, scene_world_h = 0.f;
  // Entities resting on the floor or a solid — jump requires groundedness.
  // prev_grounded is last step's set: landing events fire once per
  // touchdown, not every resting step (gravity keeps dy>0 while settled).
  std::set<std::uint64_t> grounded;
  std::set<std::uint64_t> prev_grounded;
  // Accumulated simulation seconds — drives sprite-strip animation so
  // playback is deterministic under --fixed-hz.
  double sim_time = 0.0;
  // Entities carrying the scene's Tilemap components — one per document
  // tilemap layer (cell state is authoritative world data — runtime edits
  // snapshot with quicksaves) — plus each map's decoded tileset image.
  std::vector<EntityId> tilemap_es;
  std::vector<std::shared_ptr<const RgbaImage>> tileset_imgs;
  std::vector<Tilemap *> tilemaps() {
    std::vector<Tilemap *> out;
    for (const auto e : tilemap_es)
      if (auto *tm = world.get<Tilemap>(e)) out.push_back(tm);
    return out;
  }
  std::vector<const Tilemap *> tilemaps() const {
    std::vector<const Tilemap *> out;
    for (const auto e : tilemap_es)
      if (const auto *tm = world.get<Tilemap>(e)) out.push_back(tm);
    return out;
  }

  // Deterministic particle system stepped inside simulate() and rendered
  // as tinted rects. The host tracks every spawned instance so it can
  // re-anchor attachments, stop emitters whose entity died, and render.
  // The global budget keeps runaway scenes bounded; consumers opt out via
  // vfx().set_particle_budget(0).
  VfxSystem vfx{default_particle_budget};
  struct VfxTrack {
    VfxInstanceId instance;
    std::string definition_id;
    EntityId attached;
  };
  std::vector<VfxTrack> vfx_tracks;

  // Scene-declared entity animations (document `animations` array):
  // shared Timelines keyed by clip id — each referencing entity steps
  // its own AnimTimeline player against these.
  struct AnimRuntime {
    Timeline timeline;
    LoopMode mode{LoopMode::Loop};
  };
  std::unordered_map<std::string, AnimRuntime> anims;

  // --- 3D scene mode (--scene3d) ---
  // The 3D tracked set lives beside the 2D one: scene-spawned and
  // runtime-spawned mesh entities. Meshes/textures cache by spec/path.
  std::vector<EntityId> entities3d;
  std::unordered_map<std::string,
                     std::shared_ptr<const native_map::Mesh3D>>
      mesh_cache;
  std::unordered_map<std::string, std::shared_ptr<const RgbaImage>>
      tex3d_cache;
  // Resolves a MeshRef spec via the shared resolve_mesh_spec (primitive
  // or content-relative .obj through the resolver). Cached per spec.
  std::shared_ptr<const native_map::Mesh3D>
  mesh_of(const std::string &spec) {
    if (spec.empty() || content == nullptr) return nullptr;
    if (const auto it = mesh_cache.find(spec); it != mesh_cache.end())
      return it->second;
    auto mesh = resolve_mesh_spec(spec, content.get());
    mesh_cache.emplace(spec, mesh);
    return mesh;
  }
  // Fly camera — the document seeds it; input mutates yaw/pitch/pos.
  double cam3_x = 0, cam3_y = 0, cam3_z = 3;
  float cam3_yaw = 0.f, cam3_pitch = 0.f; // degrees
  float cam3_fov = 60.f, cam3_near = .01f, cam3_far = 1000.f;
  native_map::Vec3 light3{.42f, .2f, .87f};
  float light3_intensity = 1.f;
  // Up to two extra world-space directional lights from the document.
  std::vector<Scene3dLight> lights3;
  float gravity3 = 0.f, ground_y3 = 0.f, bounds3 = 0.f;
  bool look_held = false; // right-button mouse-look
  // Input journaling: --record fills `recorder` with frame-indexed input
  // commands + periodic world-hash checkpoints; --replay parses a
  // recording into `replay` and injects its commands ahead of live input.
  std::optional<ReplayRecorder> recorder;
  ReplayRecorder replay{ReplayHeader{}};
  std::size_t replay_cursor = 0, replay_cp_cursor = 0;
  bool replaying = false, replay_diverged = false,
       replay_verified_printed = false;
  // 3D contact/ground tracking — separate sets: a pair can be in contact
  // in one dimensionality and not the other.
  std::set<std::pair<std::uint64_t, std::uint64_t>> overlapping3d;
  std::set<std::uint64_t> grounded3d, prev_grounded3d;
  std::function<EntityId(const Scene3dEntity &)> spawn3_fn;
  std::function<void(const std::string &)> switch_scene3d;
};

RuntimeHost::RuntimeHost(RuntimeHostOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->options = std::move(options);
  register_scene_components(impl_->world);
  // The deterministic RNG state is a world component so quicksaves capture
  // it — DeterministicRandom is a single trivially-copyable uint64 state.
  impl_->world.register_component<DeterministicRandom>(
      "rng",
      [](const DeterministicRandom &r) {
        std::vector<std::uint8_t> b(sizeof r);
        std::memcpy(b.data(), &r, sizeof r);
        return b;
      },
      [](const std::vector<std::uint8_t> &b) {
        DeterministicRandom r{0};
        if (b.size() == sizeof r) std::memcpy(&r, b.data(), sizeof r);
        return r;
      });
}
RuntimeHost::~RuntimeHost() = default;
RuntimeHost::RuntimeHost(RuntimeHost &&) noexcept = default;
RuntimeHost &RuntimeHost::operator=(RuntimeHost &&) noexcept = default;

World &RuntimeHost::world() { return impl_->world; }
const ContentResolver &RuntimeHost::content() const { return *impl_->content; }
audio::AudioOutput &RuntimeHost::audio() { return *impl_->audio; }
DeterministicRandom &RuntimeHost::rng() {
  // Resolved lazily each call — restore regenerates entity ids, so the
  // carrier is found by component rather than a cached handle.
  for (const auto e : impl_->world.entities())
    if (auto *r = impl_->world.get<DeterministicRandom>(e)) return *r;
  const auto e = impl_->world.create();
  impl_->world.add(e, DeterministicRandom{impl_->options.seed});
  return *impl_->world.get<DeterministicRandom>(e);
}
std::optional<EntityId> RuntimeHost::player() const { return impl_->player; }
std::optional<EntityId>
RuntimeHost::find_entity(std::string_view name) const {
  for (const auto entity : impl_->entities)
    if (const auto *n = impl_->world.get<EntityName>(entity);
        n != nullptr && n->value == name)
      return entity;
  return std::nullopt;
}
double RuntimeHost::sim_time() const { return impl_->sim_time; }
InputMapper &RuntimeHost::input() { return impl_->input; }
std::optional<EntityId> RuntimeHost::tilemap_entity() const {
  return impl_->tilemap_es.empty()
             ? std::nullopt
             : std::optional<EntityId>{impl_->tilemap_es.front()};
}
std::vector<EntityId> RuntimeHost::tilemap_entities() const {
  return impl_->tilemap_es;
}
std::size_t RuntimeHost::tilemap_count() const {
  return impl_->tilemap_es.size();
}
std::optional<std::size_t>
RuntimeHost::tilemap_index(std::string_view name) const {
  return engine::tilemap_index(impl_->world, name);
}
int RuntimeHost::tile_at(std::string_view map_name, float world_x,
                         float world_y) const {
  const auto index = tilemap_index(map_name);
  return index ? tile_at(*index, world_x, world_y) : -1;
}
bool RuntimeHost::set_tile_at(std::string_view map_name, float world_x,
                              float world_y, int value) {
  const auto index = tilemap_index(map_name);
  return index ? set_tile_at(*index, world_x, world_y, value) : false;
}
int RuntimeHost::tile_at(float world_x, float world_y) const {
  return tile_at(0, world_x, world_y);
}
int RuntimeHost::tile_at(std::size_t map, float world_x,
                         float world_y) const {
  if (map >= impl_->tilemap_es.size()) return -1;
  const auto *tm = impl_->world.get<Tilemap>(impl_->tilemap_es[map]);
  if (!tm || tm->tile_w <= 0 || tm->tile_h <= 0 || tm->columns <= 0)
    return -1;
  const int cx =
      static_cast<int>(std::floor((world_x - tm->x) / tm->tile_w));
  const int cy =
      static_cast<int>(std::floor((world_y - tm->y) / tm->tile_h));
  if (cx < 0 || cy < 0 || cx >= tm->columns) return -1;
  const auto idx = static_cast<std::size_t>(cy * tm->columns + cx);
  return idx < tm->cells.size() ? tm->cells[idx] : -1;
}
bool RuntimeHost::set_tile_at(float world_x, float world_y, int value) {
  return set_tile_at(0, world_x, world_y, value);
}
bool RuntimeHost::set_tile_at(std::size_t map, float world_x,
                              float world_y, int value) {
  if (map >= impl_->tilemap_es.size()) return false;
  auto *tm = impl_->world.get<Tilemap>(impl_->tilemap_es[map]);
  if (!tm || tm->tile_w <= 0 || tm->tile_h <= 0 || tm->columns <= 0)
    return false;
  const int cx =
      static_cast<int>(std::floor((world_x - tm->x) / tm->tile_w));
  const int cy =
      static_cast<int>(std::floor((world_y - tm->y) / tm->tile_h));
  if (cx < 0 || cy < 0 || cx >= tm->columns) return false;
  const auto idx = static_cast<std::size_t>(cy * tm->columns + cx);
  if (idx >= tm->cells.size())
    tm->cells.resize(static_cast<std::size_t>(cy + 1) * tm->columns, -1);
  tm->cells[idx] = value;
  return true;
}
VfxSystem &RuntimeHost::vfx() { return impl_->vfx; }
VfxInstanceId RuntimeHost::spawn_emitter(std::string_view definition_id,
                                       float x, float y,
                                       EntityId attached) {
  const auto id = impl_->vfx.spawn(definition_id, {x, y, 0.f}, attached);
  if (id != invalid_vfx_instance)
    impl_->vfx_tracks.push_back(
        {id, std::string{definition_id}, attached});
  return id;
}
void RuntimeHost::request_quit() { impl_->quit_requested = true; }
void RuntimeHost::set_paused(bool paused) { impl_->paused = paused; }
bool RuntimeHost::paused() const { return impl_->paused; }
void RuntimeHost::set_time_scale(double scale) {
  impl_->time_scale = scale > 0.0 ? scale : 1.0;
}
double RuntimeHost::time_scale() const { return impl_->time_scale; }
void RuntimeHost::set_scene(std::string scene_file) {
  if (impl_->switch_scene)
    impl_->switch_scene(scene_file);
  else
    impl_->options.scene_file = std::move(scene_file);
}
EntityId RuntimeHost::spawn_entity(const SceneEntity &entity) {
  return impl_->spawn_fn ? impl_->spawn_fn(entity) : EntityId{};
}
bool RuntimeHost::destroy_entity(EntityId id) {
  return impl_->destroy_fn && impl_->destroy_fn(id);
}
EntityId RuntimeHost::spawn_tilemap(const SceneTilemap &map) {
  return impl_->spawn_tilemap_fn ? impl_->spawn_tilemap_fn(map)
                                 : EntityId{};
}
bool RuntimeHost::destroy_tilemap(EntityId id) {
  return impl_->destroy_tilemap_fn && impl_->destroy_tilemap_fn(id);
}
bool RuntimeHost::scene3d() const { return impl_->options.scene3d; }
EntityId RuntimeHost::spawn_entity3d(const Scene3dEntity &entity) {
  return impl_->spawn3_fn ? impl_->spawn3_fn(entity) : EntityId{};
}
std::vector<EntityId> RuntimeHost::entities3d() const {
  return impl_->entities3d;
}
std::vector<EntityId>
RuntimeHost::entities3d_in_radius(float x, float y, float z,
                                  float radius) const {
  std::vector<EntityId> out;
  const float r2 = radius * radius;
  for (const auto e : impl_->entities3d) {
    const auto *t = impl_->world.get<Transform3D>(e);
    if (!t) continue;
    const float dx = t->x - x, dy = t->y - y, dz = t->z - z;
    if (dx * dx + dy * dy + dz * dz <= r2) out.push_back(e);
  }
  return out;
}
std::vector<EntityId>
RuntimeHost::entities3d_in_box(float x, float y, float z, float half_w,
                               float half_h, float half_d) const {
  std::vector<EntityId> out;
  for (const auto e : impl_->entities3d) {
    const auto *t = impl_->world.get<Transform3D>(e);
    if (!t) continue;
    if (std::fabs(t->x - x) <= half_w && std::fabs(t->y - y) <= half_h &&
        std::fabs(t->z - z) <= half_d)
      out.push_back(e);
  }
  return out;
}
void RuntimeHost::set_camera3d(double x, double y, double z,
                               float yaw_deg, float pitch_deg) {
  impl_->cam3_x = x;
  impl_->cam3_y = y;
  impl_->cam3_z = z;
  impl_->cam3_yaw = yaw_deg;
  impl_->cam3_pitch = std::clamp(pitch_deg, -89.f, 89.f);
}
double RuntimeHost::camera3d_x() const { return impl_->cam3_x; }
double RuntimeHost::camera3d_y() const { return impl_->cam3_y; }
double RuntimeHost::camera3d_z() const { return impl_->cam3_z; }
float RuntimeHost::camera3d_yaw() const { return impl_->cam3_yaw; }
float RuntimeHost::camera3d_pitch() const { return impl_->cam3_pitch; }
float RuntimeHost::camera3d_fov() const { return impl_->cam3_fov; }
void RuntimeHost::set_camera3d_fov(float fov_deg) {
  impl_->cam3_fov = std::clamp(fov_deg, 1.f, 175.f);
}
float RuntimeHost::gravity3d() const { return impl_->gravity3; }
float RuntimeHost::ground_y() const { return impl_->ground_y3; }
std::optional<RuntimeHost::RaycastHit3D>
RuntimeHost::raycast3d(double ox, double oy, double oz, float dx,
                       float dy, float dz, float max_distance) const {
  const auto resolve = [this](const std::string &spec) {
    return impl_->mesh_of(spec);
  };
  const auto hit =
      raycast_world3d(impl_->world, impl_->entities3d, resolve, ox, oy,
                      oz, dx, dy, dz, max_distance);
  if (!hit) return std::nullopt;
  return RaycastHit3D{hit->entity, hit->distance, hit->x, hit->y,
                      hit->z};
}
std::optional<RuntimeHost::RaycastHit3D>
RuntimeHost::entity3d_at(float screen_x, float screen_y) const {
  if (impl_->view_w <= 0 || impl_->view_h <= 0) return std::nullopt;
  const float nx = (screen_x / impl_->view_w) * 2.f - 1.f;
  const float ny = 1.f - (screen_y / impl_->view_h) * 2.f;
  constexpr float kDeg = 3.14159265f / 180.f;
  const float half_tan = std::tan(impl_->cam3_fov * kDeg * .5f);
  const float aspect =
      static_cast<float>(impl_->view_w) / impl_->view_h;
  Vec3 dc{nx * half_tan * aspect, ny * half_tan, -1.f};
  const float l = std::sqrt(dc.x * dc.x + dc.y * dc.y + dc.z * dc.z);
  dc = {dc.x / l, dc.y / l, dc.z / l};
  const Quaternion cam_q =
      quat_mul(rotation_axis_angle({0.f, 1.f, 0.f},
                                   impl_->cam3_yaw * kDeg),
               rotation_axis_angle({1.f, 0.f, 0.f},
                                   impl_->cam3_pitch * kDeg));
  const Vec3 dir = rotate_vec(cam_q, dc);
  return raycast3d(impl_->cam3_x, impl_->cam3_y, impl_->cam3_z, dir.x,
                   dir.y, dir.z, impl_->cam3_far);
}

namespace {
std::optional<std::filesystem::path>
data_slot_path(const std::filesystem::path &root, std::string_view key) {
  if (key.empty() || key.size() > 64) return std::nullopt;
  for (const char c : key)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
      return std::nullopt;
  return root / "saves" / "data" / (std::string(key) + ".dat");
}
} // namespace

bool RuntimeHost::save_data(std::string_view key,
                            std::span<const std::byte> bytes) {
  const auto path = data_slot_path(impl_->options.project_root, key);
  if (!path) return false;
  try {
    std::filesystem::create_directories(path->parent_path());
    rotate_save_history(*path);
    write_file_atomically(*path, bytes);
    write_history_sidecars(*path);
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

std::optional<std::vector<std::uint8_t>>
RuntimeHost::load_data(std::string_view key) const {
  const auto path = data_slot_path(impl_->options.project_root, key);
  if (!path) return std::nullopt;
  const auto read = [](const std::filesystem::path &p)
      -> std::optional<std::vector<std::uint8_t>> {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>()};
  };
  if (auto bytes = read(*path)) return bytes;
  for (std::size_t slot = 1; slot <= k_default_save_history_depth; ++slot)
    if (auto bytes = read(history_slot_path(*path, slot))) return bytes;
  return std::nullopt;
}
void RuntimeHost::set_camera(float x, float y, float zoom) {
  impl_->cam_x = x;
  impl_->cam_y = y;
  impl_->cam_zoom = zoom > 0.f ? zoom : 1.f;
  if (clamp_camera && impl_->world_w > 0.f && impl_->world_h > 0.f) {
    const float span_x = impl_->view_w / impl_->cam_zoom;
    const float span_y = impl_->view_h / impl_->cam_zoom;
    impl_->cam_x = std::clamp(
        impl_->cam_x, 0.f, std::max(0.f, impl_->world_w - span_x));
    impl_->cam_y = std::clamp(
        impl_->cam_y, 0.f, std::max(0.f, impl_->world_h - span_y));
  }
}
float RuntimeHost::camera_x() const { return impl_->cam_x; }
float RuntimeHost::camera_y() const { return impl_->cam_y; }
float RuntimeHost::camera_zoom() const { return impl_->cam_zoom; }
int RuntimeHost::viewport_width() const {
  return impl_->view_w > 0 ? impl_->view_w : impl_->options.width;
}
int RuntimeHost::viewport_height() const {
  return impl_->view_h > 0 ? impl_->view_h : impl_->options.height;
}
float RuntimeHost::world_width() const { return impl_->world_w; }
float RuntimeHost::world_height() const { return impl_->world_h; }
std::pair<float, float> RuntimeHost::screen_to_world(float sx,
                                                     float sy) const {
  const float zoom = impl_->cam_zoom != 0.f ? impl_->cam_zoom : 1.f;
  return {impl_->cam_x + sx / zoom, impl_->cam_y + sy / zoom};
}
std::optional<EntityId> RuntimeHost::entity_at(float screen_x,
                                               float screen_y) const {
  std::optional<EntityId> best;
  int best_layer = 0;
  for (const auto e : impl_->entities) {
    const auto *t = impl_->world.get<Transform2D>(e);
    const auto *ext = impl_->world.get<Extent2D>(e);
    if (!t || !ext || impl_->world.get<Hidden>(e) != nullptr) continue;
    // Hit-test the DRAWN rect: per-entity parallax + zoom apply, so
    // screen-pinned HUD elements pick where they appear.
    const auto *px = impl_->world.get<Parallax>(e);
    const float parallax = px ? px->value : 1.f;
    const float dx = (t->x - impl_->cam_x * parallax) * impl_->cam_zoom;
    const float dy = (t->y - impl_->cam_y * parallax) * impl_->cam_zoom;
    const float dw = ext->w * impl_->cam_zoom;
    const float dh = ext->h * impl_->cam_zoom;
    if (screen_x < dx || screen_x > dx + dw || screen_y < dy ||
        screen_y > dy + dh)
      continue;
    // Iterating in document order means a later (on-top) entity within
    // the same layer always replaces an earlier hit.
    const auto *l = impl_->world.get<Layer>(e);
    const int layer = l ? l->value : 0;
    if (!best || layer >= best_layer) {
      best = e;
      best_layer = layer;
    }
  }
  return best;
}

std::vector<EntityId> RuntimeHost::entities_in_rect(float x, float y,
                                                    float w, float h) const {
  std::vector<EntityId> out;
  for (const auto e : impl_->entities) {
    const auto *t = impl_->world.get<Transform2D>(e);
    const auto *ext = impl_->world.get<Extent2D>(e);
    if (!t || !ext) continue;
    if (t->x < x + w && x < t->x + ext->w && t->y < y + h &&
        y < t->y + ext->h)
      out.push_back(e);
  }
  return out;
}

std::vector<EntityId> RuntimeHost::entities_in_radius(float x, float y,
                                                      float radius) const {
  std::vector<EntityId> out;
  const float r2 = radius * radius;
  for (const auto e : impl_->entities) {
    const auto *t = impl_->world.get<Transform2D>(e);
    const auto *ext = impl_->world.get<Extent2D>(e);
    if (!t || !ext) continue;
    const float cx = t->x + ext->w * .5f - x;
    const float cy = t->y + ext->h * .5f - y;
    if (cx * cx + cy * cy <= r2) out.push_back(e);
  }
  return out;
}

int RuntimeHost::run() {
  RuntimeDiagnostics::context("runtime:package-scan");
  auto &impl = *impl_;
  const auto &options = impl.options;
  auto &world = impl.world;

  // The base package registers first; its namespace is then protected so
  // mod packages under mods/ cannot override it.
  PackageRegistry registry;
  scan_packages(registry,
                (options.project_root / "packages").generic_string());
  registry.protect_namespace(options.package_id);
  scan_packages(registry, (options.project_root / "mods").generic_string());
  const auto plan = registry.resolve();

  const auto exe_dir = executable_directory();
  impl.time_scale = options.time_scale;
  impl.content = std::make_unique<ContentResolver>(
      options.package_id, options.project_root, exe_dir);
  const std::size_t cooked_assets = impl.content->cooked_count();

  // --replay parses before the window opens so a corrupt recording fails
  // fast instead of looking like a game hang.
  if (!options.replay_file.empty()) {
    std::ifstream in(options.replay_file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string error;
    auto parsed = ReplayRecorder::parse(ss.str(), &error);
    if (!parsed) {
      std::fprintf(stderr, "replay: cannot parse %s: %s\n",
                   options.replay_file.generic_string().c_str(),
                   error.c_str());
      return 1;
    }
    impl.replay = std::move(*parsed);
    impl.replaying = true;
    if (impl.replay.truncated())
      std::fprintf(stderr,
                   "replay: recording truncated — entries past the "
                   "retained prefix are absent\n");
    if (const auto &h = impl.replay.header();
        h.window_width != 0 &&
        (h.window_width != static_cast<std::uint32_t>(options.width) ||
         h.window_height != static_cast<std::uint32_t>(options.height)))
      std::fprintf(stderr,
                   "replay: recorded drawable %ux%u differs from this "
                   "window — pointer positions may not land identically\n",
                   h.window_width, h.window_height);
  }
  if (!options.record_file.empty()) {
    ReplayHeader header;
    header.seed = options.seed;
    header.window_width = static_cast<std::uint32_t>(options.width);
    header.window_height = static_cast<std::uint32_t>(options.height);
    impl.recorder.emplace(header);
  }
  // Writes the finished journal; false on failure.
  const auto flush_recorder = [&]() -> bool {
    if (!impl.recorder) return true;
    try {
      const auto text = impl.recorder->serialize();
      write_file_atomically(
          options.record_file,
          std::as_bytes(std::span{text.data(), text.size()}));
      return true;
    } catch (const std::exception &) {
      std::fprintf(stderr, "record: cannot write %s\n",
                   options.record_file.generic_string().c_str());
      return false;
    }
  };

  RuntimeDiagnostics::context("runtime:window-ctor");
  Window window(options.window_title, options.width, options.height,
                options.fullscreen, exe_dir / "engine-default-font.ttf");
  RuntimeDiagnostics::context("runtime:window-init");
  window.set_auto_frame_cap();
  // F12/PrintScreen captures land in the project's screenshots/ dir.
  window.set_screenshot_directory(
      std::filesystem::absolute(options.project_root / "screenshots"));

  RuntimeDiagnostics::context("runtime:audio-init");
  // Scoped to run() so its SDL audio teardown precedes ~Window's SDL_Quit.
  audio::AudioOutput audio_output;
  impl.audio = &audio_output;
  auto &audio = audio_output;

  auto load_clip = [&](const std::string &name)
      -> std::shared_ptr<const audio::AudioClip> {
    if (const auto bytes = impl.content->read_bytes(name)) {
      try {
        return audio::decode_audio_clip(*bytes, name);
      } catch (const std::exception &) {
      }
    }
    return {};
  };
  std::shared_ptr<const audio::AudioClip> bounce_clip;
  if (!options.bounce_clip.empty())
    bounce_clip = load_clip(options.bounce_clip);
  if (!bounce_clip && !options.bounce_clip_alt.empty())
    bounce_clip = load_clip(options.bounce_clip_alt);
  if (!options.music_clip.empty()) {
    if (const auto music = load_clip(options.music_clip))
      audio.play_music(music);
    else if (!options.music_clip_alt.empty())
      if (const auto mp3 = load_clip(options.music_clip_alt))
        audio.play_music(mp3);
  }

  // Decodes a content-relative sprite. Prefers the cooked package (BC7 mip
  // chains, one chunk per level); falls back to the loose source file.
  auto decode_sprite = [&](const std::string &sprite)
      -> std::shared_ptr<const RgbaImage> {
    if (const auto *rec = impl.content->find_cooked(sprite)) {
      try {
        if (rec->type == "texture" && rec->format == "BC7") {
          std::vector<Bc1MipLevel> mips;
          for (std::size_t c = 0; c < rec->chunks.size(); ++c) {
            const auto &chunk = rec->chunks[c];
            if (auto bytes = impl.content->read_cooked(sprite, c))
              mips.push_back(
                  Bc1MipLevel{chunk.width, chunk.height, std::move(*bytes)});
          }
          return RgbaImage::create_cooked(TextureFormat::Bc7, std::move(mips));
        }
      } catch (const std::exception &) {
      }
    }
    try {
      return decode_rgba_image(impl.content->loose_path(sprite), 2048);
    } catch (const std::exception &) {
      return nullptr;
    }
  };

  // Attaches an entity's VfxRef-named emitter, anchored at its center —
  // games register definitions via host.vfx().define(); the track table
  // re-anchors the emitter each step and stops it when the entity dies.
  const auto attach_vfx = [&](EntityId e) {
    const auto *vr = world.get<VfxRef>(e);
    if (!vr || vr->name.empty() || impl.vfx.definition(vr->name) == nullptr)
      return;
    const auto *t = world.get<Transform2D>(e);
    const auto *x = world.get<Extent2D>(e);
    const auto id = impl.vfx.spawn(
        vr->name,
        {t ? t->x + (x ? x->w * .5f : 0.f) : 0.f,
         t ? t->y + (x ? x->h * .5f : 0.f) : 0.f, 0.f},
        e);
    if (id != invalid_vfx_instance)
      impl.vfx_tracks.push_back({id, vr->name, e});
  };

  // Scene-declared emitter definitions register into the VfxSystem on
  // load, converting the document's degrees/key-list form to the engine's
  // radians/FloatCurve form — entity `vfx` fields then attach with no
  // game code at all.
  const auto register_emitters = [&](const std::vector<SceneEmitterDef> &emitters) {
    const auto curve_of = [](const auto &keys) {
      FloatCurve c;
      for (const auto &[t, v] : keys) c.add_key(t, v);
      return c;
    };
    for (const auto &em : emitters) {
      EmitterDefinition def;
      def.id = em.id;
      def.sprite = em.sprite;
      def.spawn_rate_per_second = em.rate;
      def.particle_lifetime_seconds = em.lifetime;
      def.velocity_min = {em.vx_min, em.vy_min, 0.f};
      def.velocity_max = {em.vx_max, em.vy_max, 0.f};
      def.spread_radians = em.spread_deg * (3.14159265f / 180.f);
      def.gravity = {em.gx, em.gy, 0.f};
      def.scale_over_life = curve_of(em.scale_keys);
      def.opacity_over_life = curve_of(em.opacity_keys);
      def.tint_r = curve_of(em.tint_r);
      def.tint_g = curve_of(em.tint_g);
      def.tint_b = curve_of(em.tint_b);
      def.max_particles = em.max_particles;
      def.lod_fade_distance = em.lod_fade_distance;
      def.lod_min_rate_scale = em.lod_min_rate_scale;
      impl.vfx.define(std::move(def));
    }
  };

  // Scene-declared entity animations: one shared Timeline per def;
  // channels feed the AnimTimeline players stepped inside simulate().
  const auto register_animations =
      [&](const std::vector<SceneAnimationDef> &defs) {
        impl.anims.clear();
        for (const auto &def : defs) {
          Impl::AnimRuntime runtime;
          float duration = 0.f;
          for (const auto &[channel, keys] : def.tracks) {
            auto &curve = runtime.timeline.track(channel);
            for (const auto &[t, v] : keys) {
              curve.add_key(t, v);
              duration = std::max(duration, t);
            }
          }
          runtime.timeline.duration = duration;
          for (const auto &[t, name] : def.events)
            runtime.timeline.add_event(t, name);
          runtime.mode = def.loop == "once"    ? LoopMode::Once
                         : def.loop == "pingpong" ? LoopMode::PingPong
                                                  : LoopMode::Loop;
          impl.anims.emplace(def.id, std::move(runtime));
        }
      };

  // Attaches a spawned or restored entity's AnimTimeline to its shared
  // Timeline — restores seek the saved playhead (codec wrote saved_*).
  const auto attach_anim = [&](EntityId e) {
    auto *at = world.get<AnimTimeline>(e);
    if (!at || at->id.empty()) return;
    const auto it = impl.anims.find(at->id);
    if (it == impl.anims.end()) return;
    at->player.play(&it->second.timeline, it->second.mode);
    at->player.seek(at->saved_time);
    at->player.set_paused(!at->saved_playing);
  };

  // (Re)spawns World entities from a scene document; sprite decode stays
  // host-side since it depends on this project's content roots.
  std::string scene_music;
  auto spawn_entities = [&](const SceneDocument &doc) {
    register_emitters(doc.emitters);
    register_animations(doc.animations);
    impl.bg_r = doc.bg_r;
    impl.bg_g = doc.bg_g;
    impl.bg_b = doc.bg_b;
    impl.gravity = doc.gravity;
    impl.scene_world_w = doc.world_w;
    impl.scene_world_h = doc.world_h;
    // Scene music: a set track swaps in on load; empty keeps whatever is
    // already playing so levels can share the options/default track.
    if (!doc.music.empty() && doc.music != scene_music)
      if (const auto clip = load_clip(doc.music)) {
        audio.play_music(clip);
        scene_music = doc.music;
      }
    for (const auto e : impl.entities) world.destroy(e);
    for (const auto e : impl.tilemap_es) world.destroy(e);
    impl.entities = spawn_scene(world, doc);
    impl.player = find_entity_by_name(world, "player");
    impl.overlapping.clear();
    impl.grounded.clear();
    impl.prev_grounded.clear();
    engine::resolve_hierarchy(world);
    impl.tilemap_es = engine::tilemap_entities(world);
    impl.tileset_imgs.clear();
    for (const auto e : impl.tilemap_es) {
      const auto *tm = world.get<Tilemap>(e);
      impl.tileset_imgs.push_back(
          tm && !tm->tileset.empty() ? decode_sprite(tm->tileset)
                                     : nullptr);
    }
    impl.sprites.assign(impl.entities.size(), {});
    for (std::size_t i = 0; i < impl.entities.size(); ++i) {
      if (const auto *sp = world.get<SpriteRef>(impl.entities[i]);
          sp != nullptr && !sp->value.empty())
        impl.sprites[i] = decode_sprite(sp->value);
      attach_vfx(impl.entities[i]);
      attach_anim(impl.entities[i]);
      // Game-defined component attach: zip the spawned id with its
      // authored record (spawn order matches document order).
      if (on_spawn && i < doc.entities.size())
        on_spawn(world, impl.entities[i], doc.entities[i]);
    }
  };

  // Authored entities come from the scene file (the engine tools' Scene
  // tool); it is polled so saved edits apply live to the running game. One
  // demo entity when the document is absent.
  auto scene_file = options.project_root / options.scene_file;
  auto scene_stamp = std::filesystem::file_time_type{};
  auto reload_scene = [&] {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(scene_file, ec);
    if (ec || stamp == scene_stamp) return;
    if (const auto doc = SceneDocument::load(scene_file)) {
      scene_stamp = stamp;
      spawn_entities(*doc);
    }
  };
  impl.switch_scene = [&](const std::string &file) {
    scene_file = options.project_root / file;
    scene_stamp = {};
    reload_scene();
  };

  // Runtime spawn/destroy: joins the tracked set so the entity integrates,
  // bounces, renders and collides like a scene-spawned one.
  impl.spawn_fn = [&](const SceneEntity &entity) -> EntityId {
    const auto ids = spawn_scene(world, SceneDocument{{entity}});
    if (ids.empty()) return {};
    impl.entities.push_back(ids.front());
    impl.sprites.push_back(
        entity.sprite.empty() ? nullptr : decode_sprite(entity.sprite));
    attach_vfx(ids.front());
    attach_anim(ids.front());
    if (on_spawn) on_spawn(world, ids.front(), entity);
    return ids.front();
  };
  impl.destroy_fn = [&](EntityId id) -> bool {
    const auto it =
        std::find(impl.entities.begin(), impl.entities.end(), id);
    if (it == impl.entities.end()) return false;
    impl.sprites.erase(
        impl.sprites.begin() + (it - impl.entities.begin()));
    impl.entities.erase(it);
    if (impl.player && *impl.player == id) impl.player.reset();
    for (auto p = impl.overlapping.begin(); p != impl.overlapping.end();)
      if (p->first == id.value() || p->second == id.value())
        p = impl.overlapping.erase(p);
      else
        ++p;
    world.destroy(id);
    return true;
  };
  // Runtime-spawned tilemaps (procedural terrain) join the same tracked
  // set as scene-authored layers — render, collision and snapshots all
  // treat them identically.
  impl.spawn_tilemap_fn = [&](const SceneTilemap &s) -> EntityId {
    const auto e = world.create();
    world.add(e, Tilemap{s.tileset, s.x, s.y, s.tile_w, s.tile_h,
                         s.columns, s.layer, s.parallax, s.collide,
                         s.cells});
    if (!s.name.empty()) world.add(e, EntityName{s.name});
    impl.tilemap_es.push_back(e);
    impl.tileset_imgs.push_back(
        s.tileset.empty() ? nullptr : decode_sprite(s.tileset));
    return e;
  };
  impl.destroy_tilemap_fn = [&](EntityId id) -> bool {
    const auto it = std::find(impl.tilemap_es.begin(),
                              impl.tilemap_es.end(), id);
    if (it == impl.tilemap_es.end()) return false;
    impl.tileset_imgs.erase(impl.tileset_imgs.begin() +
                            (it - impl.tilemap_es.begin()));
    impl.tilemap_es.erase(it);
    world.destroy(id);
    return true;
  };

  // --- 3D scene mode -------------------------------------------------
  // Mesh resolution lives on Impl so public raycast queries share it.
  const auto mesh_of = [&](const std::string &spec) {
    return impl.mesh_of(spec);
  };
  const auto tex3d_of = [&](const std::string &path)
      -> std::shared_ptr<const RgbaImage> {
    if (path.empty()) return nullptr;
    if (const auto it = impl.tex3d_cache.find(path);
        it != impl.tex3d_cache.end())
      return it->second;
    auto img = decode_sprite(path);
    impl.tex3d_cache.emplace(path, img);
    return img;
  };

  // (Re)spawns the 3D entity set from a Scene3dDocument. 3D entities are
  // a separate tracked list — they never mix into the 2D gameplay set.
  auto spawn_entities3d = [&](const Scene3dDocument &doc) {
    register_emitters(doc.emitters);
    impl.cam3_x = doc.cam_x;
    impl.cam3_y = doc.cam_y;
    impl.cam3_z = doc.cam_z;
    impl.cam3_yaw = doc.cam_yaw_deg;
    impl.cam3_pitch = std::clamp(doc.cam_pitch_deg, -89.f, 89.f);
    impl.cam3_fov = std::clamp(doc.fov_deg, 1.f, 175.f);
    impl.cam3_near = doc.near_plane;
    impl.cam3_far = doc.far_plane;
    impl.light3 = {doc.light_x, doc.light_y, doc.light_z};
    impl.light3_intensity = doc.light_intensity;
    impl.lights3 = doc.lights;
    impl.gravity3 = doc.gravity;
    impl.ground_y3 = doc.ground_y;
    impl.bounds3 = doc.bounds;
    for (const auto e : impl.entities3d) world.destroy(e);
    impl.entities3d = spawn_scene3d(world, doc);
    for (std::size_t i = 0; i < impl.entities3d.size(); ++i) {
      // Pre-resolve meshes/textures so a bad spec surfaces at load, and
      // so spawn3_fn shares the warm caches.
      const auto *mr = world.get<MeshRef>(impl.entities3d[i]);
      if (mr) mesh_of(mr->spec);
      const auto *tr = world.get<TextureRef>(impl.entities3d[i]);
      if (tr) tex3d_of(tr->value);
      if (on_spawn3d && i < doc.entities.size())
        on_spawn3d(world, impl.entities3d[i], doc.entities[i]);
    }
  };
  auto scene3d_file = options.project_root / options.scene3d_file;
  auto scene3d_stamp = std::filesystem::file_time_type{};
  auto reload_scene3d = [&] {
    if (!options.scene3d) return;
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(scene3d_file, ec);
    if (ec || stamp == scene3d_stamp) return;
    if (const auto doc = Scene3dDocument::load(scene3d_file)) {
      scene3d_stamp = stamp;
      spawn_entities3d(*doc);
    }
  };
  impl.switch_scene3d = [&](const std::string &file) {
    scene3d_file = options.project_root / file;
    scene3d_stamp = {};
    reload_scene3d();
  };
  impl.spawn3_fn = [&](const Scene3dEntity &entity) -> EntityId {
    const auto ids = spawn_scene3d(world, Scene3dDocument{{entity}});
    if (ids.empty()) return {};
    impl.entities3d.push_back(ids.front());
    if (on_spawn3d) on_spawn3d(world, ids.front(), entity);
    return ids.front();
  };
  RuntimeDiagnostics::context("runtime:scene-init");
  reload_scene();
  reload_scene3d();
  if (impl.entities.empty() && impl.entities3d.empty())
    spawn_entities(SceneDocument{{SceneEntity{"demo", 120.f, 160.f, 96.f,
                                             96.f, 240.f, 150.f}}});

  // F5/F9 quicksave: World snapshots registered components to a checksummed
  // binary file; restore rebuilds entities, sprites and the player handle.
  const auto save_path = options.project_root / options.save_file;
  auto save_world = [&] {
    try {
      // Persist the fly camera with the world — carried on a lazily
      // resolved entity so restores recover it like the RNG stream.
      if (options.scene3d) {
        EntityId carrier{};
        for (const auto e : world.entities())
          if (world.get<Camera3DState>(e)) {
            carrier = e;
            break;
          }
        if (carrier == EntityId{}) {
          carrier = world.create();
          world.add(carrier, Camera3DState{});
        }
        *world.get<Camera3DState>(carrier) =
            Camera3DState{impl.cam3_x, impl.cam3_y, impl.cam3_z,
                          impl.cam3_yaw, impl.cam3_pitch, impl.cam3_fov};
      }
      save_world_to_file(world, save_path);
      // Record the resolved content set beside the save so a later load
      // under a different package/mod set attests rather than silently
      // binding stale content.
      write_save_package_manifest(save_path, plan);
    } catch (const std::exception &) {
    }
  };
  auto load_world = [&] {
    if (!load_world_from_file(world, save_path)) return;
    const auto pkg_report = verify_save_package_manifest(save_path, plan);
    if (!pkg_report.compatible()) {
      std::string note{"world save package mismatch:"};
      for (const auto &id : pkg_report.missing_packages)
        note += " missing " + id;
      for (const auto &m : pkg_report.version_mismatches)
        note += " " + m;
      RuntimeDiagnostics::context(note);
    }
    impl.entities = world.entities();
    // 3D entities restore with the snapshot too — split them out of the
    // 2D tracked set before the tilemap partition below.
    impl.entities3d = engine::entities3d(world);
    impl.entities.erase(
        std::remove_if(impl.entities.begin(), impl.entities.end(),
                       [&](EntityId e) {
                         return std::find(impl.entities3d.begin(),
                                          impl.entities3d.end(),
                                          e) != impl.entities3d.end();
                       }),
        impl.entities.end());
    impl.overlapping3d.clear();
    impl.grounded3d.clear();
    impl.prev_grounded3d.clear();
    for (const auto e : world.entities())
      if (const auto *cam = world.get<Camera3DState>(e)) {
        impl.cam3_x = cam->x;
        impl.cam3_y = cam->y;
        impl.cam3_z = cam->z;
        impl.cam3_yaw = cam->yaw_deg;
        impl.cam3_pitch = cam->pitch_deg;
        impl.cam3_fov = cam->fov_deg;
        break;
      }
    // Tilemap entities restore with the snapshot — pull them out of the
    // tracked set and re-resolve each tileset image.
    impl.tilemap_es = engine::tilemap_entities(world);
    impl.entities.erase(
        std::remove_if(impl.entities.begin(), impl.entities.end(),
                       [&](EntityId e) {
                         return std::find(impl.tilemap_es.begin(),
                                          impl.tilemap_es.end(),
                                          e) != impl.tilemap_es.end();
                       }),
        impl.entities.end());
    impl.tileset_imgs.clear();
    for (const auto e : impl.tilemap_es) {
      const auto *tm = world.get<Tilemap>(e);
      impl.tileset_imgs.push_back(
          tm && !tm->tileset.empty() ? decode_sprite(tm->tileset)
                                     : nullptr);
    }
    impl.player = find_entity_by_name(world, "player");
    impl.overlapping.clear();
    impl.grounded.clear();
    impl.prev_grounded.clear();
    impl.sprites.assign(impl.entities.size(), {});
    for (std::size_t i = 0; i < impl.entities.size(); ++i) {
      if (const auto *sp = world.get<SpriteRef>(impl.entities[i]);
          sp != nullptr && !sp->value.empty())
        impl.sprites[i] = decode_sprite(sp->value);
      attach_vfx(impl.entities[i]);
      attach_anim(impl.entities[i]);
    }
  };

  // Materialize the RNG carrier and (re)seed it from options — argv
  // overrides apply by now, and a game that rolled before run() still gets
  // the configured stream.
  rng() = DeterministicRandom{options.seed};

  // Default input context — the player-control actions. A project input
  // map JSON replaces/extends these via input_mapper.load_contexts.
  {
    InputContext game{"game"};
    auto key = [](int code) {
      return InputBinding{RawInputEvent::Kind::KeyPress, code, 1.f, {}};
    };
    auto pad = [](int button) {
      return InputBinding{RawInputEvent::Kind::GamepadButton, button, 1.f, {}};
    };
    // SDL_GamepadButton: SOUTH=0, WEST=2, DPAD_UP=11..DPAD_RIGHT=14;
    // axes: LEFTX=0, LEFTY=1.
    game.actions.push_back(
        InputAction{"move_left", InputAction::Type::Button,
                    {key('a'), key(0x40000050), pad(13)}});
    game.actions.push_back(
        InputAction{"move_right", InputAction::Type::Button,
                    {key('d'), key(0x4000004f), pad(14)}});
    game.actions.push_back(
        InputAction{"move_up", InputAction::Type::Button,
                    {key('w'), key(0x40000052), pad(11)}});
    game.actions.push_back(
        InputAction{"move_down", InputAction::Type::Button,
                    {key('s'), key(0x40000051), pad(12)}});
    game.actions.push_back(
        InputAction{"move_x", InputAction::Type::Axis1D,
                    {{RawInputEvent::Kind::GamepadAxis, 0, 1.f, {}}}});
    game.actions.push_back(
        InputAction{"move_y", InputAction::Type::Axis1D,
                    {{RawInputEvent::Kind::GamepadAxis, 1, 1.f, {}}}});
    game.actions.push_back(
        InputAction{"jump", InputAction::Type::Button,
                    {key(' '), key('w'), key(0x40000052), pad(0)}});
    game.actions.push_back(
        InputAction{"fire", InputAction::Type::Button,
                    {key(' '), pad(10),
                     {RawInputEvent::Kind::MouseButton, 1, 1.f, {}}}});
    game.actions.push_back(
        InputAction{"mine", InputAction::Type::Button,
                    {key('c'), pad(2)}});
    impl.input.add_context(std::move(game));
    impl.input.push_context("game");
    if (!options.input_map.empty()) {
      const auto path = options.project_root / options.input_map;
      std::ifstream in(path);
      if (in) {
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        // Loaded contexts stack on top of "game": non-exclusive ones fall
        // through to the defaults, exclusive ones take over.
        if (impl.input.load_contexts(text))
          for (const auto &name : impl.input.context_names())
            if (name != "game") impl.input.push_context(name);
      }
    }
  }
  float accumulator = 0.f;
  int rendered = 0;
  auto last = std::chrono::steady_clock::now();
  auto scene_poll = last;
  // Resolve world bounds up front too so the jump handler works before the
  // first rendered frame.
  // Bound priority: --world-w/--world-h > scene worldSize > viewport.
  impl.world_w = options.world_width > 0.f   ? options.world_width
                 : impl.scene_world_w > 0.f  ? impl.scene_world_w
                                             : static_cast<float>(options.width);
  impl.world_h = options.world_height > 0.f  ? options.world_height
                 : impl.scene_world_h > 0.f  ? impl.scene_world_h
                                             : static_cast<float>(options.height);
  RuntimeDiagnostics::context("runtime:loop");
  for (;;) {
    const auto snapshot = window.poll();
    if (snapshot.quit_requested || impl.quit_requested) break;
    impl.input.begin_frame();
    // The frame index is the journal tick — a replayed command lands in
    // the same frame it was recorded on, ahead of this frame's live
    // input (a live Escape still quits).
    const auto frame_tick = static_cast<std::uint64_t>(rendered);
    std::vector<InputEvent> frame_events;
    if (impl.replaying) {
      const auto &cmds = impl.replay.commands();
      while (impl.replay_cursor < cmds.size() &&
             cmds[impl.replay_cursor].tick <= frame_tick) {
        if (cmds[impl.replay_cursor].name == "input")
          if (auto decoded =
                  decode_input_event(cmds[impl.replay_cursor].payload))
            frame_events.push_back(*decoded);
        ++impl.replay_cursor;
      }
    }
    for (const auto &live : snapshot.events) frame_events.push_back(live);
    for (const auto &event : frame_events) {
      if (impl.recorder)
        impl.recorder->record(frame_tick, "input",
                              encode_input_event(event));
      if (event.type == InputEventType::EscapePressed)
        return flush_recorder() ? 0 : 1;
      // Feed the action mapper with a normalized raw event so game
      // actions (and the built-in player controls) see every input.
      RawInputEvent raw{};
      bool feed_raw = true;
      switch (event.type) {
      case InputEventType::KeyPressed:
        raw.kind = RawInputEvent::Kind::KeyPress;
        raw.code = static_cast<int>(event.key);
        break;
      case InputEventType::KeyReleased:
        raw.kind = RawInputEvent::Kind::KeyRelease;
        raw.code = static_cast<int>(event.key);
        break;
      case InputEventType::LeftPressed:
      case InputEventType::RightPressed:
      case InputEventType::LeftReleased:
      case InputEventType::RightReleased:
        raw.kind = RawInputEvent::Kind::MouseButton;
        raw.code = (event.type == InputEventType::LeftPressed ||
                    event.type == InputEventType::LeftReleased)
                       ? 1
                       : 3;
        raw.pressed = (event.type == InputEventType::LeftPressed ||
                       event.type == InputEventType::RightPressed);
        break;
      case InputEventType::PointerMove:
        raw.kind = RawInputEvent::Kind::MouseMotion;
        raw.x = event.delta.x;
        raw.y = event.delta.y;
        break;
      case InputEventType::Wheel:
        raw.kind = RawInputEvent::Kind::MouseWheel;
        raw.y = event.wheel_y;
        break;
      case InputEventType::BackspacePressed:
        raw.kind = RawInputEvent::Kind::KeyPress;
        raw.code = 8;
        break;
      case InputEventType::GamepadPressed:
      case InputEventType::GamepadReleased:
        raw.kind = RawInputEvent::Kind::GamepadButton;
        raw.code = event.gamepad_button;
        raw.device = event.gamepad_device;
        raw.pressed = event.type == InputEventType::GamepadPressed;
        break;
      case InputEventType::GamepadAxis:
        raw.kind = RawInputEvent::Kind::GamepadAxis;
        raw.code = event.gamepad_axis;
        raw.device = event.gamepad_device;
        raw.value = event.gamepad_axis_value;
        break;
      default:
        feed_raw = false;
        break;
      }
      if (feed_raw) {
        impl.input.feed(raw);
        // Instantaneous key events clear held state immediately.
        if (event.type == InputEventType::BackspacePressed) {
          raw.kind = RawInputEvent::Kind::KeyRelease;
          impl.input.feed(raw);
        }
      }
      if (event.type == InputEventType::KeyPressed) {
        if (event.key == 0x4000003e) save_world();   // F5
        if (event.key == 0x40000042) load_world();   // F9
        if (event.key == 'p') impl.paused = !impl.paused;  // P pauses the sim
      }
      // 3D camera look: right-drag turns, the wheel adjusts fov.
      if (options.scene3d) {
        if (event.type == InputEventType::RightPressed)
          impl.look_held = true;
        else if (event.type == InputEventType::RightReleased)
          impl.look_held = false;
        else if (event.type == InputEventType::PointerMove &&
                 impl.look_held) {
          impl.cam3_yaw += event.delta.x * .2f;
          impl.cam3_pitch = std::clamp(
              impl.cam3_pitch - event.delta.y * .2f, -89.f, 89.f);
        } else if (event.type == InputEventType::Wheel)
          impl.cam3_fov =
              std::clamp(impl.cam3_fov - event.wheel_y * 2.f, 1.f, 175.f);
      }
      if (on_event) on_event(event);
    }
    // Platformer jump: with gravity on, the jump action gives a grounded
    // player an impulse instead of held-key velocity.
    if (impl.gravity != 0.f && impl.player &&
        impl.input.just_pressed("jump")) {
      auto *v = world.get<Velocity2D>(*impl.player);
      if (v && impl.grounded.count(impl.player->value()))
        v->dy = -options.player_jump_impulse;
    }
    if (!snapshot.renderable()) continue;

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (now - scene_poll > std::chrono::milliseconds(500)) {
      scene_poll = now;
      reload_scene();
      reload_scene3d();
    }
    const float w = static_cast<float>(snapshot.drawable_width);
    const float h = static_cast<float>(snapshot.drawable_height);
    impl.view_w = static_cast<int>(w);
    impl.view_h = static_cast<int>(h);
    // World bounds default to the viewport (single-screen world); camera
    // games set world_width/height for larger levels.
    impl.world_w = options.world_width > 0.f  ? options.world_width
                   : impl.scene_world_w > 0.f ? impl.scene_world_w : w;
    impl.world_h = options.world_height > 0.f ? options.world_height
                   : impl.scene_world_h > 0.f ? impl.scene_world_h : h;
    const float world_w = impl.world_w;
    const float world_h = impl.world_h;

    // Input system: the "game" context's move actions drive the entity
    // named "player" — rebindable via the project input map.
    if (impl.player) {
      auto *v = world.get<Velocity2D>(*impl.player);
      if (v) {
        // Digital buttons plus the analog stick (deadzone on the stick).
        float dx = (impl.input.pressed("move_right") ? 1.f : 0.f) -
                   (impl.input.pressed("move_left") ? 1.f : 0.f);
        const float stick_x = impl.input.axis("move_x");
        if (std::abs(stick_x) > 0.18f) dx += stick_x;
        v->dx = std::clamp(dx, -1.f, 1.f) * options.player_move_speed;
        // With scene gravity active the player is a platformer: dy is
        // owned by gravity/jump, not held-key velocity.
        if (impl.gravity == 0.f) {
          float dy = (impl.input.pressed("move_down") ? 1.f : 0.f) -
                     (impl.input.pressed("move_up") ? 1.f : 0.f);
          const float stick_y = impl.input.axis("move_y");
          if (std::abs(stick_y) > 0.18f) dy += stick_y;
          v->dy = std::clamp(dy, -1.f, 1.f) * options.player_move_speed;
        }
      }
    }

    // Simulation step: fixed-timestep mode accumulates real time and steps
    // at a constant rate so gameplay is frame-rate independent.
    const float step = options.fixed_timestep_hz > 0.0
                           ? static_cast<float>(1.0 / options.fixed_timestep_hz)
                           : 0.f;
    auto simulate = [&](float dt_step) {
      dt_step *= static_cast<float>(impl.time_scale);
      impl.sim_time += dt_step;
      if (on_update) on_update(world, dt_step);
      impl.prev_grounded = std::move(impl.grounded);
      impl.grounded.clear();
      // One map's cell test — per-map geometry means each layer needs
      // its own grid lookup (not the any-layer solid_cell).
      const auto cell_of = [](const Tilemap &tm, float px, float py) {
        const int cx =
            static_cast<int>(std::floor((px - tm.x) / tm.tile_w));
        const int cy =
            static_cast<int>(std::floor((py - tm.y) / tm.tile_h));
        if (cx < 0 || cy < 0 || cx >= tm.columns) return false;
        const auto idx = static_cast<std::size_t>(cy * tm.columns + cx);
        return idx < tm.cells.size() && tm.cells[idx] >= 0;
      };
      // Tilemap lookup: which colliding layer holds a solid cell at this
      // world point (each map has its own tile size/grid), if any.
      const auto blocking_map = [&](float px, float py) -> const Tilemap * {
        for (const auto *tmap : impl.tilemaps()) {
          if (!tmap->collide || tmap->tile_w <= 0 || tmap->tile_h <= 0)
            continue;
          const auto &tm = *tmap;
          const int cx = static_cast<int>(std::floor(px / tm.tile_w));
          const int cy = static_cast<int>(std::floor(py / tm.tile_h));
          if (cx < 0 || cy < 0 || cx >= tm.columns) continue;
          const auto idx = static_cast<std::size_t>(cy * tm.columns + cx);
          if (idx < tm.cells.size() && tm.cells[idx] >= 0) return tmap;
        }
        return nullptr;
      };
      const auto solid_cell = [&](float px, float py) {
        return blocking_map(px, py) != nullptr;
      };
      // Kinematic solids: platforms with velocity integrate (no gravity)
      // and carry riders standing on their tops.
      for (const auto entity : impl.entities) {
        if (!world.get<Solid>(entity)) continue;
        auto *t = world.get<Transform2D>(entity);
        auto *v = world.get<Velocity2D>(entity);
        const auto *ext = world.get<Extent2D>(entity);
        if (!t || !v || !ext) continue;
        const float dx = v->dx * dt_step, dy = v->dy * dt_step;
        if (dx == 0.f && dy == 0.f) continue;
        const float prev_top = t->y, prev_left = t->x;
        t->x += dx;
        t->y += dy;
        // Platforms stop at the world bounds instead of bouncing.
        const float nx = std::clamp(t->x, 0.f, world_w - ext->w);
        const float ny = std::clamp(t->y, 0.f, world_h - ext->h);
        if (nx != t->x) v->dx = 0.f;
        if (ny != t->y) v->dy = 0.f;
        t->x = nx;
        t->y = ny;
        const float moved_x = t->x - prev_left, moved_y = t->y - prev_top;
        if (moved_x == 0.f && moved_y == 0.f) continue;
        // Carry riders: non-solid entities whose feet rest on this
        // platform's pre-move top get displaced with it.
        for (const auto rider : impl.entities) {
          if (rider == entity || world.get<Solid>(rider)) continue;
          auto *rt = world.get<Transform2D>(rider);
          const auto *re = world.get<Extent2D>(rider);
          if (!rt || !re) continue;
          const bool atop = rt->x < prev_left + ext->w &&
                            rt->x + re->w > prev_left &&
                            std::abs(rt->y + re->h - prev_top) <= 1.5f;
          if (atop) {
            rt->x += moved_x;
            rt->y += moved_y;
          }
        }
      }
      for (const auto entity : impl.entities) {
        auto *t = world.get<Transform2D>(entity);
        auto *v = world.get<Velocity2D>(entity);
        const auto *ext = world.get<Extent2D>(entity);
        if (!t || !v || !ext) continue;
        if (world.get<Solid>(entity)) continue;  // kinematic — moved above
        const auto *gs = world.get<GravityScale>(entity);
        const float gscale = impl.gravity != 0.f ? (gs ? gs->value : 1.f)
                                                 : 0.f;
        if (gscale != 0.f) v->dy += impl.gravity * gscale * dt_step;
        const float prev_right = t->x + ext->w;
        const float prev_bottom = t->y + ext->h;
        t->x += v->dx * dt_step;
        // Horizontal blocking: a moving entity whose side crosses a
        // solid's side stops against it (walls) — applies in top-down
        // games (no gravity) as well as platformers. One-way platforms
        // never side-block.
        if (v->dx != 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity || !world.get<Solid>(other) ||
                world.get<Oneway>(other))
              continue;
            const auto *st = world.get<Transform2D>(other);
            const auto *se = world.get<Extent2D>(other);
            if (!st || !se) continue;
            const bool overlap_y =
                t->y < st->y + se->h && t->y + ext->h > st->y;
            if (!overlap_y) continue;
            if (v->dx > 0.f && prev_right <= st->x + 1.f &&
                t->x + ext->w > st->x) {
              t->x = st->x - ext->w;
              v->dx = 0.f;
            } else if (v->dx < 0.f &&
                       prev_right - ext->w >= st->x + se->w - 1.f &&
                       t->x < st->x + se->w) {
              t->x = st->x + se->w;
              v->dx = 0.f;
            }
          }
        }
        // Tilemap side-blocking: the leading edge's corner cells stop
        // horizontal motion (mirrors the solid-entity rule, in top-down
        // games too). Whichever colliding layer blocks supplies the cell
        // geometry.
        if (v->dx != 0.f) {
          const float lead = v->dx > 0.f ? t->x + ext->w : t->x;
          const Tilemap *blocker = nullptr;
          for (const auto *candidate : impl.tilemaps()) {
            if (!candidate->collide || candidate->tile_w <= 0 ||
                candidate->tile_h <= 0)
              continue;
            const auto &tm = *candidate;
            const float step_y = std::max(1.f, tm.tile_h - 1.f);
            bool blocked = false;
            for (float cy = t->y + 1.f;
                 cy <= t->y + ext->h - 1.f && !blocked;
                 cy += step_y)
              blocked = cell_of(tm, lead, cy);
            blocked = blocked || cell_of(tm, lead, t->y + ext->h - 1.f);
            if (blocked) {
              blocker = candidate;
              break;
            }
          }
          if (blocker) {
            const auto &tm = *blocker;
            const int col = static_cast<int>(
                std::floor((lead - tm.x) / tm.tile_w));
            t->x = v->dx > 0.f
                       ? tm.x + col * tm.tile_w - ext->w
                       : tm.x + (col + 1) * static_cast<float>(tm.tile_w);
            v->dx = 0.f;
          }
        }
        const float prev_top = t->y;
        t->y += v->dy * dt_step;
        // Platform landings: a downward-moving entity whose bottom
        // crossed a solid's top this step lands on it — gravity-affected
        // falls and top-down motion alike. One-way platforms land
        // identically (crossing is only detected when moving down, so
        // rising/horizontal motion passes through).
        if (v->dy > 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity ||
                (!world.get<Solid>(other) && !world.get<Oneway>(other)))
              continue;
            const auto *st = world.get<Transform2D>(other);
            const auto *se = world.get<Extent2D>(other);
            if (!st || !se) continue;
            const bool overlap_x =
                t->x < st->x + se->w && t->x + ext->w > st->x;
            if (overlap_x && prev_bottom <= st->y + 1.f &&
                t->y + ext->h >= st->y) {
              t->y = st->y - ext->h;
              v->dy = 0.f;
              if (on_land && !impl.prev_grounded.count(entity.value()))
                on_land(entity, other);
            }
          }
          // Tilemap landing: the bottom edge's cells catch a fall —
          // each colliding layer lands on its own row geometry.
          for (std::size_t mi = 0; mi < impl.tilemap_es.size(); ++mi) {
            const auto *land_tm = world.get<Tilemap>(impl.tilemap_es[mi]);
            if (!land_tm || !land_tm->collide || land_tm->tile_w <= 0 ||
                land_tm->tile_h <= 0)
              continue;
            const auto &tm = *land_tm;
            const float step_x = std::max(1.f, tm.tile_w - 1.f);
            for (float px = t->x + 1.f; px <= t->x + ext->w - 1.f;
                 px += step_x) {
              const int row = static_cast<int>(
                  std::floor((t->y + ext->h - tm.y) / tm.tile_h));
              if (cell_of(tm, px, t->y + ext->h) &&
                  prev_bottom <= tm.y + row * tm.tile_h + 1.f) {
                t->y = tm.y + row * static_cast<float>(tm.tile_h) -
                       ext->h;
                v->dy = 0.f;
                if (on_tile_land &&
                    !impl.prev_grounded.count(entity.value())) {
                  const int cx = static_cast<int>(
                      std::floor((px - tm.x) / tm.tile_w));
                  const auto ci =
                      static_cast<std::size_t>(row * tm.columns + cx);
                  on_tile_land(entity, mi, cx, row,
                               ci < tm.cells.size() ? tm.cells[ci] : -1);
                }
                break;
              }
            }
          }
        }
        // Ceilings: an upward-moving entity whose top crossed a solid's
        // bottom stops under it (jump head-bump / top-down walls).
        // One-way platforms never block from below.
        if (v->dy < 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity || !world.get<Solid>(other) ||
                world.get<Oneway>(other))
              continue;
            const auto *st = world.get<Transform2D>(other);
            const auto *se = world.get<Extent2D>(other);
            if (!st || !se) continue;
            if (t->x < st->x + se->w && t->x + ext->w > st->x &&
                prev_top >= st->y + se->h - 1.f && t->y < st->y + se->h) {
              t->y = st->y + se->h;
              v->dy = 0.f;
            }
          }
          // Tilemap ceiling: the top edge's cells stop the rise on the
          // blocking layer's own row geometry.
          for (const auto *ceil_tm : impl.tilemaps()) {
            if (!ceil_tm->collide || ceil_tm->tile_w <= 0 ||
                ceil_tm->tile_h <= 0)
              continue;
            const auto &tm = *ceil_tm;
            const float step_x = std::max(1.f, tm.tile_w - 1.f);
            bool blocked = false;
            for (float px = t->x + 1.f; px <= t->x + ext->w - 1.f && !blocked;
                 px += step_x) {
              const int row = static_cast<int>(
                  std::floor((t->y - tm.y) / tm.tile_h));
              if (cell_of(tm, px, t->y) &&
                  prev_top >= tm.y + (row + 1) * tm.tile_h - 1.f) {
                t->y = tm.y + (row + 1) * static_cast<float>(tm.tile_h);
                v->dy = 0.f;
                blocked = true;
              }
            }
            if (!blocked && cell_of(tm, t->x + ext->w - 1.f, t->y)) {
              const int row = static_cast<int>(
                  std::floor((t->y - tm.y) / tm.tile_h));
              if (prev_top >= tm.y + (row + 1) * tm.tile_h - 1.f) {
                t->y = tm.y + (row + 1) * static_cast<float>(tm.tile_h);
                v->dy = 0.f;
              }
            }
            if (v->dy == 0.f) break;
          }
        }
        // The Bounce marker opts out of wall rebound — projectiles and
        // debris stop dead at the level edge instead of returning.
        const bool rebound = world.get<NoBounce>(entity) == nullptr;
        bool bounced = false;
        if (t->x < 0 || t->x > world_w - ext->w) {
          v->dx = rebound ? -v->dx : 0.f;
          bounced = rebound;
          t->x = std::clamp(t->x, 0.f, world_w - ext->w);
        }
        if (t->y < 0 || t->y > world_h - ext->h) {
          // Gravity-affected entities come to rest on the floor instead of
          // bouncing forever; the ceiling still deflects them downward.
          const bool rests =
              !rebound || (gscale != 0.f && t->y > world_h - ext->h);
          v->dy = rests ? 0.f : -v->dy;
          bounced = !rests;
          t->y = std::clamp(t->y, 0.f, world_h - ext->h);
        }
        // Grounded when resting: at the floor (or platform top) with no
        // downward velocity remaining.
        if (v->dy == 0.f && t->y + ext->h >= world_h - 1.f)
          impl.grounded.insert(entity.value());
        else if (v->dy == 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity ||
                (!world.get<Solid>(other) && !world.get<Oneway>(other)))
              continue;
            const auto *st = world.get<Transform2D>(other);
            const auto *se = world.get<Extent2D>(other);
            if (st && se && t->x < st->x + se->w && t->x + ext->w > st->x &&
                std::abs(t->y + ext->h - st->y) <= 1.5f) {
              impl.grounded.insert(entity.value());
              break;
            }
          }
          // Resting on a tile row counts as grounded too — any
          // colliding layer can hold the entity up.
          if (!impl.grounded.count(entity.value()))
            for (const auto *ground_tm : impl.tilemaps()) {
              if (!ground_tm->collide || ground_tm->tile_w <= 0 ||
                  ground_tm->tile_h <= 0)
                continue;
              const auto &tm = *ground_tm;
              const float below = t->y + ext->h + 0.5f;
              const int row = static_cast<int>(
                  std::floor((below - tm.y) / tm.tile_h));
              if (std::abs(t->y + ext->h - (tm.y + row * tm.tile_h)) >
                  1.5f)
                continue;
              const float step_x = std::max(1.f, tm.tile_w - 1.f);
              for (float px = t->x + 1.f; px <= t->x + ext->w - 1.f;
                   px += step_x)
                if (cell_of(tm, px, below)) {
                  impl.grounded.insert(entity.value());
                  break;
                }
              if (impl.grounded.count(entity.value())) break;
            }
        }
        if (bounced && impl.player && entity == *impl.player && bounce_clip) {
          // Pan the impact cue by the entity's screen position and
          // attenuate by normalized distance from the view center — the
          // first engine-side consumers of positional effects.
          const float view = impl.view_w > 0 ? static_cast<float>(impl.view_w)
                                             : world_w * impl.cam_zoom;
          const float view_h = impl.view_h > 0 ? static_cast<float>(impl.view_h)
                                               : world_h * impl.cam_zoom;
          const float screen_x =
              (t->x + ext->w * .5f - impl.cam_x) * impl.cam_zoom;
          const float screen_y =
              (t->y + ext->h * .5f - impl.cam_y) * impl.cam_zoom;
          const float pan =
              view > 0.f ? std::clamp(screen_x / (view * .5f) - 1.f, -1.f, 1.f)
                         : 0.f;
          // Corner of the view reads as distance 1 — the bounce still
          // plays at half gain so off-center impacts stay audible.
          const float norm_x = view > 0.f ? screen_x / (view * .5f) - 1.f : 0.f;
          const float norm_y = view_h > 0.f ? screen_y / (view_h * .5f) - 1.f : 0.f;
          const float distance =
              std::clamp(std::hypot(std::clamp(norm_x, -1.f, 1.f),
                                    std::clamp(norm_y, -1.f, 1.f)) /
                             1.4142135623730951f,
                         0.f, 1.f);
          audio.play_effect(bounce_clip, pan, 1.f - 0.5f * distance);
        }
      }
      // Lifetimes tick down in sim time; expired entities self-destruct
      // (collected first so destruction doesn't disturb the scan).
      {
        std::vector<EntityId> expired;
        for (const auto entity : impl.entities) {
          if (auto *lt = world.get<Lifetime>(entity);
              lt != nullptr && (lt->remaining -= dt_step) <= 0.f)
            expired.push_back(entity);
          // Angular velocity: Spin integrates into Rotation each step.
          if (const auto *sp = world.get<Spin>(entity)) {
            auto *rot = world.get<Rotation>(entity);
            if (rot == nullptr)
              world.add(entity, Rotation{sp->value * dt_step});
            else
              rot->value += sp->value * dt_step;
          }
        }
        for (const auto id : expired) impl.destroy_fn(id);
      }
      // Keyframed entity animations: each AnimTimeline steps its playhead
      // in sim time and the evaluated tracks own their channels — a clip
      // writing "x" sets Transform2D.x every step (velocity/collisions
      // still resolve around it). Events fire per crossing.
      for (const auto entity : impl.entities) {
        auto *at = world.get<AnimTimeline>(entity);
        if (!at || at->player.timeline() == nullptr) continue;
        const auto step = at->player.advance(dt_step);
        at->saved_time = at->player.time();
        at->saved_playing = !at->player.paused();
        if (on_anim_event)
          for (const auto &event : step.events)
            on_anim_event(event.name, entity);
        const auto channel = [&step](const char *name) -> const float * {
          const auto it = step.values.find(name);
          return it == step.values.end() ? nullptr : &it->second;
        };
        if (auto *t = world.get<Transform2D>(entity)) {
          if (const float *v = channel("x")) t->x = *v;
          if (const float *v = channel("y")) t->y = *v;
        }
        if (auto *x = world.get<Extent2D>(entity)) {
          if (const float *v = channel("w")) x->w = *v;
          if (const float *v = channel("h")) x->h = *v;
        }
        if (auto *vel = world.get<Velocity2D>(entity)) {
          if (const float *v = channel("vx")) vel->dx = *v;
          if (const float *v = channel("vy")) vel->dy = *v;
        }
        if (const float *v = channel("opacity"))
          if (auto *o = world.get<Opacity>(entity)) o->value = *v;
        if (const float *v = channel("rotation"))
          if (auto *r = world.get<Rotation>(entity)) r->value = *v;
        if (const float *v = channel("spin"))
          if (auto *sp = world.get<Spin>(entity)) sp->value = *v;
        if (auto *tint = world.get<Tint>(entity)) {
          const auto byte_of = [](float v) {
            return static_cast<std::uint8_t>(
                std::clamp(std::lround(v * 255.f), 0l, 255l));
          };
          if (const float *v = channel("tintR")) tint->r = byte_of(*v);
          if (const float *v = channel("tintG")) tint->g = byte_of(*v);
          if (const float *v = channel("tintB")) tint->b = byte_of(*v);
        }
      }
      // Hierarchy: parented entities snap to parent+offset, folding their
      // own world-space drift into the offset — runs before contacts so
      // overlap events see final positions.
      engine::resolve_hierarchy(world);
      // --- 3D scene mode sim ----------------------------------------
      // Fly camera: WASD strafe/forward in the yaw plane (pitch applies
      // to forward flight), Space/C up/down — all through the rebindable
      // "game" context so input maps can remap them.
      if (options.scene3d) {
        constexpr float kDeg = 3.14159265f / 180.f;
        const float sy = std::sin(impl.cam3_yaw * kDeg),
                    cy = std::cos(impl.cam3_yaw * kDeg);
        const float sp = std::sin(impl.cam3_pitch * kDeg),
                    cp = std::cos(impl.cam3_pitch * kDeg);
        const Vec3 fwd{-cp * sy, sp, -cp * cy};
        const Vec3 right{cy, 0.f, -sy};
        float mx = (impl.input.pressed("move_right") ? 1.f : 0.f) -
                   (impl.input.pressed("move_left") ? 1.f : 0.f);
        if (const float s = impl.input.axis("move_x");
            std::abs(s) > 0.18f)
          mx += s;
        float mz = (impl.input.pressed("move_up") ? 1.f : 0.f) -
                   (impl.input.pressed("move_down") ? 1.f : 0.f);
        if (const float s = impl.input.axis("move_y");
            std::abs(s) > 0.18f)
          mz -= s;
        const float my = (impl.input.pressed("jump") ? 1.f : 0.f) -
                         (impl.input.pressed("mine") ? 1.f : 0.f);
        const float spd = options.fly_speed * dt_step;
        impl.cam3_x += (fwd.x * mz + right.x * mx) * spd;
        impl.cam3_y += (fwd.y * mz + my) * spd;
        impl.cam3_z += (fwd.z * mz + right.z * mx) * spd;

        // An entity's collision geometry: the world AABB of its rotated
        // + scaled local bounds (cheap broad-phase, ground rest, bounds
        // clamp) plus the true OBB (narrow-phase SAT + MTV push-out).
        struct EntityBox3D {
          float cx{}, cy{}, cz{}, hx{}, hy{}, hz{};
          ObBox3D obb;
        };
        const auto entity_box3d = [&](EntityId e) {
          const auto *t = world.get<Transform3D>(e);
          const auto *m = world.get<MeshRef>(e);
          const auto mesh = m ? mesh_of(m->spec) : nullptr;
          const float s = t ? t->scale : 1.f;
          const Vec3 lo = mesh ? mesh->bounds_min()
                               : Vec3{-.5f, -.5f, -.5f};
          const Vec3 hi = mesh ? mesh->bounds_max()
                               : Vec3{.5f, .5f, .5f};
          const float lx = (hi.x - lo.x) * .5f * s,
                      ly = (hi.y - lo.y) * .5f * s,
                      lz = (hi.z - lo.z) * .5f * s;
          const Vec3 lc{(lo.x + hi.x) * .5f * s, (lo.y + hi.y) * .5f * s,
                        (lo.z + hi.z) * .5f * s};
          EntityBox3D box;
          box.cx = t ? t->x : 0.f;
          box.cy = t ? t->y : 0.f;
          box.cz = t ? t->z : 0.f;
          box.obb.half[0] = lx;
          box.obb.half[1] = ly;
          box.obb.half[2] = lz;
          // Unit-quaternion rotation matrix rows (same convention as
          // native_scene3d.cpp's column-major rotation_matrix); the
          // quaternion is normalized defensively.
          float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
          if (t) {
            const float ql = std::sqrt(t->qx * t->qx + t->qy * t->qy +
                                       t->qz * t->qz + t->qw * t->qw);
            if (ql > 1e-8f) {
              qx = t->qx / ql;
              qy = t->qy / ql;
              qz = t->qz / ql;
              qw = t->qw / ql;
            }
          }
          const float r00 = 1.f - 2.f * (qy * qy + qz * qz),
                      r01 = 2.f * (qx * qy - qz * qw),
                      r02 = 2.f * (qx * qz + qy * qw);
          const float r10 = 2.f * (qx * qy + qz * qw),
                      r11 = 1.f - 2.f * (qx * qx + qz * qz),
                      r12 = 2.f * (qy * qz - qx * qw);
          const float r20 = 2.f * (qx * qz - qy * qw),
                      r21 = 2.f * (qy * qz + qx * qw),
                      r22 = 1.f - 2.f * (qx * qx + qy * qy);
          box.cx += r00 * lc.x + r01 * lc.y + r02 * lc.z;
          box.cy += r10 * lc.x + r11 * lc.y + r12 * lc.z;
          box.cz += r20 * lc.x + r21 * lc.y + r22 * lc.z;
          box.hx = std::abs(r00) * lx + std::abs(r01) * ly +
                   std::abs(r02) * lz;
          box.hy = std::abs(r10) * lx + std::abs(r11) * ly +
                   std::abs(r12) * lz;
          box.hz = std::abs(r20) * lx + std::abs(r21) * ly +
                   std::abs(r22) * lz;
          // OBB: axes are the rotation matrix's columns (R·ê_i).
          box.obb.center = {box.cx, box.cy, box.cz};
          box.obb.axis[0] = {r00, r10, r20};
          box.obb.axis[1] = {r01, r11, r21};
          box.obb.axis[2] = {r02, r12, r22};
          return box;
        };

        // Entity integration: gravity pulls -Y; the ground plane rests
        // at ground_y under the entity's world-AABB bottom.
        impl.prev_grounded3d = std::move(impl.grounded3d);
        impl.grounded3d.clear();
        std::vector<EntityId> expired3d;
        for (const auto e : impl.entities3d) {
          auto *t = world.get<Transform3D>(e);
          if (!t) continue;
          auto *v = world.get<Velocity3D>(e);
          const auto *gs = world.get<GravityScale>(e);
          const float gscale = gs ? gs->value : 1.f;
          const auto box = entity_box3d(e);
          // Origin-relative AABB offsets (constant for a fixed
          // rotation/scale): entity-origin y that puts the world-AABB
          // bottom on ground_y, and the center's horizontal offset.
          const float rest_y =
              impl.ground_y3 + box.hy - (box.cy - t->y);
          const float cx_off = box.cx - t->x, cz_off = box.cz - t->z;
          if (v && gscale != 0.f) v->dy -= impl.gravity3 * gscale * dt_step;
          if (v && (v->dx != 0.f || v->dy != 0.f || v->dz != 0.f)) {
            t->x += v->dx * dt_step;
            t->y += v->dy * dt_step;
            t->z += v->dz * dt_step;
            // Ground plane: rest with the world-AABB bottom on ground_y,
            // fire on_land on the touchdown transition like the 2D path.
            if (t->y <= rest_y && v->dy <= 0.f) {
              t->y = rest_y;
              v->dy = 0.f;
              impl.grounded3d.insert(e.value());
              if (on_land &&
                  !impl.prev_grounded3d.count(e.value()))
                on_land(e, EntityId{});
            }
            // XZ bounds: bounce the world-AABB edges unless NoBounce.
            if (impl.bounds3 > 0.f) {
              const bool nb = world.get<NoBounce>(e) != nullptr;
              const float rx = std::min(box.hx, impl.bounds3),
                          rz = std::min(box.hz, impl.bounds3);
              if (t->x + cx_off < -impl.bounds3 + rx ||
                  t->x + cx_off > impl.bounds3 - rx) {
                t->x = std::clamp(t->x + cx_off, -impl.bounds3 + rx,
                                  impl.bounds3 - rx) - cx_off;
                v->dx = nb ? 0.f : -v->dx;
              }
              if (t->z + cz_off < -impl.bounds3 + rz ||
                  t->z + cz_off > impl.bounds3 - rz) {
                t->z = std::clamp(t->z + cz_off, -impl.bounds3 + rz,
                                  impl.bounds3 - rz) - cz_off;
                v->dz = nb ? 0.f : -v->dz;
              }
            }
          }
          if (auto *life = world.get<Lifetime>(e)) {
            life->remaining -= dt_step;
            if (life->remaining <= 0.f) expired3d.push_back(e);
          }
        }
        for (const auto id : expired3d) {
          const auto it = std::find(impl.entities3d.begin(),
                                    impl.entities3d.end(), id);
          if (it != impl.entities3d.end()) impl.entities3d.erase(it);
          world.destroy(id);
        }
        engine::resolve_hierarchy3d(world);
        // Contacts on the 3D set: world-AABB broad-phase, then SAT
        // narrow-phase over the true OBBs — rotated meshes stop
        // false-positive overlaps. Solid resolution pushes the mover
        // out along the SAT minimum-translation vector (arbitrary axis,
        // not just world axes). The scan always runs — solid resolution
        // is needed even when no callbacks are registered.
        {
          // Broad-phase: a uniform grid over the world AABBs emits
          // candidate pairs in sorted order — O(n + cells) instead of the
          // pairwise scan, deterministic, and no true overlap is missed.
          Broadphase3D grid;
          std::unordered_map<std::uint64_t, EntityBox3D> boxes;
          std::unordered_map<std::uint64_t, EntityId> by_key;
          float cell = 1.f;
          boxes.reserve(impl.entities3d.size());
          for (const auto e : impl.entities3d) {
            if (!world.get<Transform3D>(e)) continue;
            auto box = entity_box3d(e);
            cell = std::max({cell, box.hx, box.hy, box.hz});
            const auto key = e.value();
            by_key.emplace(key, e);
            boxes.emplace(key, std::move(box));
          }
          grid.reset(cell * 2.f);
          for (const auto &[key, box] : boxes)
            grid.insert(key,
                        {box.cx - box.hx, box.cy - box.hy, box.cz - box.hz},
                        {box.cx + box.hx, box.cy + box.hy, box.cz + box.hz});
          std::set<std::pair<std::uint64_t, std::uint64_t>> now;
          std::vector<std::pair<EntityId, EntityId>> entered;
          for (const auto &[ka, kb] : grid.pairs()) {
            const auto &abox = boxes.at(ka), &bbox = boxes.at(kb);
            const auto ea = by_key.at(ka), eb = by_key.at(kb);
            {
              // Broad-phase: world-AABB reject before the SAT scan.
              if (abox.hx + bbox.hx - std::abs(abox.cx - bbox.cx) <= 0.f ||
                  abox.hy + bbox.hy - std::abs(abox.cy - bbox.cy) <= 0.f ||
                  abox.hz + bbox.hz - std::abs(abox.cz - bbox.cz) <= 0.f)
                continue;
              // Narrow-phase: true oriented boxes — a separating axis
              // means the AABBs overlapped but the meshes do not.
              const auto mtv_ab =
                  obb_separation(abox.obb, bbox.obb);
              if (!mtv_ab) continue;
              const auto a = ka, b = kb;
              now.insert(std::minmax(a, b));
              if (!impl.overlapping3d.count(std::minmax(a, b)))
                entered.emplace_back(ea, eb);
              // Solid blocker resolution: the non-solid entity is pushed
              // out along the MTV and loses inward velocity along it. A
              // dominantly-upward push counts as a landing surface.
              const bool sa = world.get<Solid>(ea) != nullptr;
              const bool sb = world.get<Solid>(eb) != nullptr;
              if (sa != sb) {
                const auto mover = sa ? eb : ea;
                auto *tm = world.get<Transform3D>(mover);
                auto *vm = world.get<Velocity3D>(mover);
                // obb_separation(a,b) returns the vector that pushes b
                // out of a — call with the solid first.
                const CollisionVector3 mtv =
                    sa ? *mtv_ab : negate(*mtv_ab);
                tm->x += static_cast<float>(mtv.x);
                tm->y += static_cast<float>(mtv.y);
                tm->z += static_cast<float>(mtv.z);
                if (vm) {
                  const double ml =
                      std::sqrt(mtv.x * mtv.x + mtv.y * mtv.y +
                                mtv.z * mtv.z);
                  if (ml > 1e-9) {
                    const double nx = mtv.x / ml, ny = mtv.y / ml,
                                 nz = mtv.z / ml;
                    const double inward =
                        vm->dx * nx + vm->dy * ny + vm->dz * nz;
                    if (inward < 0.) {
                      vm->dx -= static_cast<float>(inward * nx);
                      vm->dy -= static_cast<float>(inward * ny);
                      vm->dz -= static_cast<float>(inward * nz);
                    }
                  }
                }
                // Push is mostly +Y → the mover rests on top: grounded,
                // and fire on_land on the touchdown transition.
                if (mtv.y > std::abs(mtv.x) &&
                    mtv.y > std::abs(mtv.z)) {
                  impl.grounded3d.insert(mover.value());
                  if (on_land &&
                      !impl.prev_grounded3d.count(mover.value()))
                    on_land(mover, sa ? ea : eb);
                }
              }
            }
          }
          if (on_collision_exit) {
            const auto unpack = [](std::uint64_t v) {
              return EntityId{static_cast<std::uint32_t>(v & 0xffffffffu),
                              static_cast<std::uint32_t>(v >> 32)};
            };
            for (const auto &key : impl.overlapping3d)
              if (!now.count(key))
                on_collision_exit(unpack(key.first), unpack(key.second));
          }
          impl.overlapping3d = std::move(now);
          if (on_collision)
            for (const auto &[a, b] : entered) on_collision(a, b);
        }
      }
      // AABB contact events: collect overlaps during the scan, then fire
      // callbacks afterwards so handlers may spawn/destroy entities safely.
      if (on_collision || on_collision_exit) {
        // Uniform-grid broad-phase: candidate pairs in sorted order —
        // O(n + cells) instead of the pairwise scan, deterministic.
        Broadphase2D grid;
        std::unordered_map<std::uint64_t, EntityId> by_key;
        float cell = 1.f;
        by_key.reserve(impl.entities.size());
        for (const auto e : impl.entities) {
          const auto *ta = world.get<Transform2D>(e);
          const auto *ea = world.get<Extent2D>(e);
          if (!ta || !ea) continue;
          cell = std::max({cell, ea->w, ea->h});
          by_key.emplace(e.value(), e);
        }
        grid.reset(cell);
        for (const auto &[key, e] : by_key) {
          const auto *ta = world.get<Transform2D>(e);
          const auto *ea = world.get<Extent2D>(e);
          grid.insert(key, {ta->x, ta->y},
                      {ta->x + ea->w, ta->y + ea->h});
        }
        std::set<std::pair<std::uint64_t, std::uint64_t>> now;
        std::vector<std::pair<EntityId, EntityId>> entered;
        for (const auto &[ka, kb] : grid.pairs()) {
          const auto ea_e = by_key.at(ka), eb_e = by_key.at(kb);
          const auto *ta = world.get<Transform2D>(ea_e);
          const auto *ea = world.get<Extent2D>(ea_e);
          const auto *tb = world.get<Transform2D>(eb_e);
          const auto *eb = world.get<Extent2D>(eb_e);
          if (ta->x < tb->x + eb->w && tb->x < ta->x + ea->w &&
              ta->y < tb->y + eb->h && tb->y < ta->y + ea->h) {
            const auto key = std::minmax(ka, kb);
            now.insert(key);
            if (!impl.overlapping.count(key))
              entered.emplace_back(ea_e, eb_e);
          }
        }
        if (on_collision_exit) {
          // Pairs present last step but absent now ended their contact —
          // entity destruction also ends it (the id is simply stale).
          const auto unpack = [](std::uint64_t v) {
            return EntityId{static_cast<std::uint32_t>(v & 0xffffffffu),
                            static_cast<std::uint32_t>(v >> 32)};
          };
          for (const auto &key : impl.overlapping)
            if (!now.count(key))
              on_collision_exit(unpack(key.first), unpack(key.second));
        }
        impl.overlapping = std::move(now);
        if (on_collision)
          for (const auto &[a, b] : entered) on_collision(a, b);
      }
      // Particles: re-anchor attached emitters to their entity's center
      // (dead entities stop their emitters), retire finished instances,
      // then advance the deterministic pools in sim time.
      for (auto it = impl.vfx_tracks.begin(); it != impl.vfx_tracks.end();) {
        bool keep = impl.vfx.alive(it->instance);
        if (keep && it->attached != EntityId{}) {
          const auto *t = world.get<Transform2D>(it->attached);
          const auto *e = world.get<Extent2D>(it->attached);
          if (t) {
            impl.vfx.set_position(
                it->instance,
                {t->x + (e ? e->w * .5f : 0.f),
                 t->y + (e ? e->h * .5f : 0.f), 0.f});
          } else {
            impl.vfx.stop(it->instance);
            keep = false;
          }
        }
        it = keep ? std::next(it) : impl.vfx_tracks.erase(it);
      }
      impl.vfx.advance(dt_step);
    };
    if (impl.paused) {
      // Rendering continues; the sim does not advance.
    } else if (step > 0.f) {
      // Frame-limited runs step once per rendered frame so --frames N
      // always produces exactly N simulation steps — byte-identical
      // snapshots across runs for determinism checks.
      if (options.frame_limit > 0) {
        simulate(step);
      } else {
        accumulator += dt;
        // Cap catch-up work so a suspended frame cannot spiral.
        for (int n = 0; n < 8 && accumulator >= step; ++n) {
          simulate(step);
          accumulator -= step;
        }
        if (accumulator >= step) accumulator = 0.f;
      }
    } else {
      simulate(dt);
    }
    audio.service();

    // World checkpoints: --record hashes the post-step world snapshot
    // every 30 frames; --replay consumes each recorded checkpoint once
    // the frame reaches its tick (a frozen minimized window keeps the
    // world identical, so a late consume still verifies).
    if (impl.recorder && rendered % 30 == 0) {
      const auto bytes = world.snapshot();
      impl.recorder->checkpoint(
          frame_tick,
          fnv1a64({reinterpret_cast<const char *>(bytes.data()),
                   bytes.size()}),
          "world");
    }
    if (impl.replaying) {
      const auto &cps = impl.replay.checkpoints();
      while (impl.replay_cp_cursor < cps.size() &&
             cps[impl.replay_cp_cursor].tick <= frame_tick) {
        const auto &cp = cps[impl.replay_cp_cursor++];
        const auto bytes = world.snapshot();
        const auto actual = fnv1a64(
            {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
        if (actual != cp.hash) {
          std::fprintf(stderr,
                       "replay_diverged={\"frame\":%llu,\"label\":\"%s\"}\n",
                       static_cast<unsigned long long>(cp.tick),
                       cp.label.c_str());
          impl.replay_diverged = true;
        }
      }
      // The recording is fully verified once its command stream and every
      // retained checkpoint have been consumed without a divergence.
      if (!impl.replay_verified_printed && !impl.replay_diverged &&
          impl.replay_cursor >= impl.replay.commands().size() &&
          impl.replay_cp_cursor >= cps.size()) {
        impl.replay_verified_printed = true;
        std::printf("replay_verified={\"commands\":%llu,\"checkpoints\":%llu}"
                    "\n",
                    static_cast<unsigned long long>(
                        impl.replay.commands().size()),
                    static_cast<unsigned long long>(cps.size()));
        std::fflush(stdout);
      }
    }

    DrawList draw;
    draw.overlay.push_back(
        FilledRectangle{{0, 0, w, h}, {impl.bg_r, impl.bg_g, impl.bg_b, 255}});
    // 3D scene mode: build this frame's scene graph and composite it
    // under the 2D pass — 2D entities/HUD still draw on top.
    if (options.scene3d && !impl.entities3d.empty()) {
      // Camera orientation = yaw about +Y then pitch about local +X.
      constexpr float kDeg = 3.14159265f / 180.f;
      const Quaternion q_yaw =
          rotation_axis_angle({0.f, 1.f, 0.f}, impl.cam3_yaw * kDeg);
      const Quaternion q_pitch =
          rotation_axis_angle({1.f, 0.f, 0.f}, impl.cam3_pitch * kDeg);
      const Quaternion cam_q = quat_mul(q_yaw, q_pitch);
      Camera3D cam;
      cam.position = {impl.cam3_x, impl.cam3_y, impl.cam3_z};
      cam.orientation = cam_q;
      cam.vertical_fov_radians = impl.cam3_fov * kDeg;
      cam.near_plane = std::max(impl.cam3_near, 1e-6f);
      cam.far_plane = std::clamp(impl.cam3_far, cam.near_plane + 1e-3f,
                                 1e7f);
      std::vector<MeshInstance3D> instances;
      instances.reserve(impl.entities3d.size());
      for (const auto e : impl.entities3d) {
        const auto *t = world.get<Transform3D>(e);
        const auto *mr = world.get<MeshRef>(e);
        if (!t || !mr) continue;
        const auto mesh = mesh_of(mr->spec);
        if (!mesh) continue;
        MeshInstance3D inst;
        inst.mesh = mesh;
        inst.position = {t->x, t->y, t->z};
        inst.rotation = {t->qx, t->qy, t->qz, t->qw};
        inst.scale = t->scale;
        const auto *tint = world.get<Tint>(e);
        const auto *op = world.get<Opacity>(e);
        const auto *tex = world.get<TextureRef>(e);
        inst.material.tint = tint ? Color{tint->r, tint->g, tint->b, 255}
                                : Color{255, 255, 255, 255};
        inst.material.opacity = op ? op->value : 1.f;
        inst.material.transparent = inst.material.opacity < 1.f;
        inst.material.texture = tex ? tex3d_of(tex->value) : nullptr;
        inst.material.double_sided =
            world.get<DoubleSided>(e) != nullptr;
        inst.material.light_intensity = impl.light3_intensity;
        inst.material.linear_light = true;
        instances.push_back(std::move(inst));
      }
      // The pipeline expects a camera-space light direction — rotate the
      // document's world-space dir by the camera's inverse orientation.
      const Quaternion inv{-cam_q.x, -cam_q.y, -cam_q.z, cam_q.w};
      const Vec3 light_cam = rotate_vec(inv, impl.light3);
      // Extra directional lights — same world→camera rotation; the
      // material pipeline evaluates at most two per instance.
      for (auto &inst : instances)
        for (std::size_t li = 0; li < impl.lights3.size() && li < 2;
             ++li) {
          const auto &l = impl.lights3[li];
          inst.material.additional_lights[li] = DirectionalLight3D{
              rotate_vec(inv, {l.dir_x, l.dir_y, l.dir_z}),
              {l.r, l.g, l.b}, l.intensity};
        }
      if (auto scene =
              Scene3D::create(cam, std::move(instances), light_cam))
        draw.overlay.insert(
            draw.overlay.begin() + 1,
            Scene3DView{std::move(scene), {0, 0, w, h}});
    }
    // Draw in layer order (stable — same-layer entities keep spawn order).
    std::vector<std::size_t> order(impl.entities.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
      const auto *la = world.get<Layer>(impl.entities[a]);
      const auto *lb = world.get<Layer>(impl.entities[b]);
      return (la ? la->value : 0) < (lb ? lb->value : 0);
    });
    // Tilemap layers paint at their own `layer` values, splitting the
    // entity pass — sorted once so each lands between the right entities.
    std::vector<std::size_t> tm_order(impl.tilemap_es.size());
    for (std::size_t i = 0; i < tm_order.size(); ++i) tm_order[i] = i;
    std::stable_sort(tm_order.begin(), tm_order.end(), [&](auto a, auto b) {
      const auto *ta = world.get<Tilemap>(impl.tilemap_es[a]);
      const auto *tb = world.get<Tilemap>(impl.tilemap_es[b]);
      return (ta ? ta->layer : 0) < (tb ? tb->layer : 0);
    });
    std::size_t next_tm = 0;
    auto draw_tilemap = [&](std::size_t which) {
      const auto *tmap = world.get<Tilemap>(impl.tilemap_es[which]);
      if (!tmap || which >= impl.tileset_imgs.size() ||
          !impl.tileset_imgs[which])
        return;
      const auto &tm = *tmap;
      if (tm.columns <= 0 || tm.tile_w <= 0 || tm.tile_h <= 0) return;
      const auto &res = *impl.tileset_imgs[which];
      const int set_cols = res.width() / tm.tile_w;
      if (set_cols <= 0) return;
      const float px = impl.cam_x * tm.parallax;
      const float py = impl.cam_y * tm.parallax;
      const int rows =
          static_cast<int>(tm.cells.size() / tm.columns);
      for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < tm.columns; ++cx) {
          const int cell = tm.cells[cy * tm.columns + cx];
          if (cell < 0) continue;
          const UiRect dest{(tm.x + cx * tm.tile_w - px) * impl.cam_zoom,
                            (tm.y + cy * tm.tile_h - py) * impl.cam_zoom,
                            tm.tile_w * impl.cam_zoom,
                            tm.tile_h * impl.cam_zoom};
          if (dest.x + dest.width < 0 || dest.y + dest.height < 0 ||
              dest.x > w || dest.y > h)
            continue;
          const UiRect src{
              static_cast<float>((cell % set_cols) * tm.tile_w),
              static_cast<float>((cell / set_cols) * tm.tile_h),
              static_cast<float>(tm.tile_w),
              static_cast<float>(tm.tile_h)};
          draw.overlay.push_back(
              Image{impl.tileset_imgs[which], dest, src});
        }
    };
    // Every tilemap at or below this entity layer draws before it.
    auto draw_tilemaps_below = [&](int layer) {
      while (next_tm < tm_order.size()) {
        const auto *tm = world.get<Tilemap>(impl.tilemap_es[tm_order[next_tm]]);
        if (tm && tm->layer > layer) break;
        draw_tilemap(tm_order[next_tm]);
        ++next_tm;
      }
    };
    for (const auto i : order) {
      const auto *t = world.get<Transform2D>(impl.entities[i]);
      const auto *ext = world.get<Extent2D>(impl.entities[i]);
      const auto *tint = world.get<Tint>(impl.entities[i]);
      if (!t || !ext || !tint) continue;
      const auto *el = world.get<Layer>(impl.entities[i]);
      draw_tilemaps_below(el ? el->value : 0);
      const auto *px = world.get<Parallax>(impl.entities[i]);
      const float parallax = px ? px->value : 1.0f;
      const UiRect rect{(t->x - impl.cam_x * parallax) * impl.cam_zoom,
                        (t->y - impl.cam_y * parallax) * impl.cam_zoom,
                        ext->w * impl.cam_zoom, ext->h * impl.cam_zoom};
      // View culling: skip entities fully outside the window, and
      // Hidden-marked entities entirely (they still simulate/collide).
      if (world.get<Hidden>(impl.entities[i]) != nullptr ||
          rect.x + rect.width < 0 || rect.y + rect.height < 0 ||
          rect.x > w || rect.y > h)
        continue;
      const auto *op = world.get<Opacity>(impl.entities[i]);
      const auto alpha = static_cast<std::uint8_t>(
          std::clamp(op ? op->value : 1.f, 0.f, 1.f) * 255.f);
      if (i < impl.sprites.size() && impl.sprites[i]) {
        Image img{impl.sprites[i], rect};
        // The entity color tints the sprite (default white = unchanged);
        // opacity modulates alpha.
        img.tint = {tint->r, tint->g, tint->b, alpha};
        if (const auto *anim = world.get<Anim>(impl.entities[i]);
            anim != nullptr && anim->frames > 1) {
          // Current cell from accumulated sim time (deterministic in
          // fixed-step). cols>0 slices a grid sheet (frame -> col,row);
          // 0 treats the sheet as one horizontal strip.
          const auto &res = *impl.sprites[i];
          const int cols =
              anim->cols > 0 ? std::min(anim->cols, anim->frames)
                             : anim->frames;
          const int rows = (anim->frames + cols - 1) / cols;
          const float cell_w =
              static_cast<float>(res.width()) / cols;
          const float cell_h =
              static_cast<float>(res.height()) / rows;
          const int elapsed =
              static_cast<int>(impl.sim_time * anim->fps);
          const int frame =
              anim->fps > 0.f
                  ? (anim->loop ? elapsed % anim->frames
                                : std::min(elapsed, anim->frames - 1))
                  : 0;
          img.source = UiRect{(frame % cols) * cell_w,
                              (frame / cols) * cell_h, cell_w, cell_h};
        }
        if (const auto *rot = world.get<Rotation>(impl.entities[i]))
          img.rotation_degrees = rot->value;
        if (const auto *fl = world.get<Flip>(impl.entities[i])) {
          img.flip_horizontal = fl->x;
          img.flip_vertical = fl->y;
        }
        draw.overlay.push_back(std::move(img));
      } else {
        draw.overlay.push_back(
            FilledRectangle{rect, {tint->r, tint->g, tint->b, alpha}});
      }
      if (const auto *label = world.get<Label>(impl.entities[i]);
          label != nullptr && !label->value.empty()) {
        const int font_px =
            std::max(8, static_cast<int>(rect.height * .5f));
        draw.overlay.push_back(Text{
            {rect.x + rect.width * .5f,
             rect.y + (rect.height - font_px) * .5f},
            label->value,
            {255, 255, 255, 255},
            font_px,
            rect.width,
            std::nullopt,
            TextAlign::Center});
      }
    }
    // Tilemaps layered above every entity draw last (foreground grids).
    while (next_tm < tm_order.size()) draw_tilemap(tm_order[next_tm++]);
    // Particles render above scene entities, in world space (camera
    // transform applies; no per-particle parallax — attach emitters to
    // parallax-scaled entities if layered depth is needed).
    for (const auto &track : impl.vfx_tracks) {
      const auto *def = impl.vfx.definition(track.definition_id);
      if (!def) continue;
      for (const auto &p : impl.vfx.particles(track.instance)) {
        const auto vis = impl.vfx.visual_for(
            *def, p.lifetime > 0.f ? p.age / p.lifetime : 1.f);
        const float sz = 6.f * vis.scale * impl.cam_zoom;
        const float sx =
            (p.position.x - impl.cam_x) * impl.cam_zoom - sz * .5f;
        const float sy =
            (p.position.y - impl.cam_y) * impl.cam_zoom - sz * .5f;
        const auto ch = [](float v) {
          return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f);
        };
        draw.overlay.push_back(FilledRectangle{
            {sx, sy, sz, sz},
            {ch(vis.r), ch(vis.g), ch(vis.b), ch(vis.opacity)}});
      }
    }
    draw.overlay.push_back(Text{{w * .5f, h * .5f - 80.f},
                                options.window_title, {86, 196, 255, 255}, 42,
                                0, std::nullopt, TextAlign::Center,
                                FontFace::Heading});
    draw.overlay.push_back(Text{
        {w * .5f, h * .5f + 12.f},
        std::to_string(plan.order.size()) + " content package(s), " +
            std::to_string(cooked_assets) + " cooked asset(s) - " +
            (plan.ok ? std::string("load plan ok")
                     : std::string("load plan FAILED")),
        {210, 230, 244, 255}, 16, 0, std::nullopt, TextAlign::Center});
    std::string help = "drop content into packages/" + options.package_id +
                       "/content/ and cook; an entity named 'player' follows "
                       "WASD/arrow keys; F5 saves, F9 loads " +
                       options.save_file;
    draw.overlay.push_back(Text{{w * .5f, h * .5f + 40.f}, help,
                                {122, 170, 190, 255}, 14, 0, std::nullopt,
                                TextAlign::Center});
    if (on_status) {
      if (auto line = on_status(); !line.empty())
        draw.overlay.push_back(Text{{w * .5f, h * .5f + 64.f}, std::move(line),
                                    {160, 200, 220, 255}, 14, 0, std::nullopt,
                                    TextAlign::Center});
    }
    if (on_draw) on_draw(draw, w, h);
    window.draw(draw);
    if (options.frame_limit > 0 && ++rendered >= options.frame_limit)
      break;
  }
  RuntimeDiagnostics::context("runtime:teardown");
  if (!flush_recorder()) return 1;
  if (impl.replaying && impl.replay_diverged) return 1;
  if (!options.snapshot_out.empty()) {
    try {
      save_world_to_file(world, options.snapshot_out);
      write_save_package_manifest(options.snapshot_out, plan);
    } catch (const std::exception &) {
      return 1;
    }
  }
  return plan.ok ? 0 : 1;
}

int RuntimeHost::run(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--frames")
      impl_->options.frame_limit = std::atoi(argv[++i]);
    else if (arg == "--fixed-hz")
      impl_->options.fixed_timestep_hz = std::atof(argv[++i]);
    else if (arg == "--snapshot-out")
      impl_->options.snapshot_out = argv[++i];
    else if (arg == "--scene")
      impl_->options.scene_file = argv[++i];
    else if (arg == "--save")
      impl_->options.save_file = argv[++i];
    else if (arg == "--width")
      impl_->options.width = std::atoi(argv[++i]);
    else if (arg == "--height")
      impl_->options.height = std::atoi(argv[++i]);
    else if (arg == "--fullscreen")
      impl_->options.fullscreen = std::atoi(argv[++i]) != 0;
    else if (arg == "--speed")
      impl_->options.time_scale = std::atof(argv[++i]);
    else if (arg == "--world-w")
      impl_->options.world_width =
          static_cast<float>(std::atof(argv[++i]));
    else if (arg == "--world-h")
      impl_->options.world_height =
          static_cast<float>(std::atof(argv[++i]));
    else if (arg == "--move-speed")
      impl_->options.player_move_speed =
          static_cast<float>(std::atof(argv[++i]));
    else if (arg == "--jump")
      impl_->options.player_jump_impulse =
          static_cast<float>(std::atof(argv[++i]));
    else if (arg == "--input-map")
      impl_->options.input_map = argv[++i];
    else if (arg == "--seed")
      impl_->options.seed = std::strtoull(argv[++i], nullptr, 10);
    else if (arg == "--record")
      impl_->options.record_file = argv[++i];
    else if (arg == "--replay")
      impl_->options.replay_file = argv[++i];
    else if (arg == "--scene3d")
      impl_->options.scene3d = true;
    else if (arg == "--scene3d-file")
      impl_->options.scene3d_file = argv[++i];
    else if (arg == "--fly-speed")
      impl_->options.fly_speed =
          static_cast<float>(std::atof(argv[++i]));
  }
  return run();
}

} // namespace stellar::engine
