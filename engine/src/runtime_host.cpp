#include "stellar/engine/runtime_host.hpp"

#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/native_audio.hpp"
#include "stellar/engine/native_map_platform.hpp"
#include "stellar/engine/package.hpp"
#include "stellar/engine/runtime_diagnostics.hpp"
#include "stellar/engine/runtime_paths.hpp"
#include "stellar/engine/save_history.hpp"
#include "stellar/engine/texture_cook.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace stellar::engine {

using namespace stellar::native_map;

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
  std::set<std::uint64_t> grounded;
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
  VfxSystem vfx;
  struct VfxTrack {
    VfxInstanceId instance;
    std::string definition_id;
    EntityId attached;
  };
  std::vector<VfxTrack> vfx_tracks;
};

RuntimeHost::RuntimeHost(RuntimeHostOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->options = std::move(options);
  register_scene_components(impl_->world);
}
RuntimeHost::~RuntimeHost() = default;
RuntimeHost::RuntimeHost(RuntimeHost &&) noexcept = default;
RuntimeHost &RuntimeHost::operator=(RuntimeHost &&) noexcept = default;

World &RuntimeHost::world() { return impl_->world; }
const ContentResolver &RuntimeHost::content() const { return *impl_->content; }
audio::AudioOutput &RuntimeHost::audio() { return *impl_->audio; }
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

  // (Re)spawns World entities from a scene document; sprite decode stays
  // host-side since it depends on this project's content roots.
  std::string scene_music;
  auto spawn_entities = [&](const SceneDocument &doc) {
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
  RuntimeDiagnostics::context("runtime:scene-init");
  reload_scene();
  if (impl.entities.empty())
    spawn_entities(SceneDocument{{SceneEntity{"demo", 120.f, 160.f, 96.f,
                                             96.f, 240.f, 150.f}}});

  // F5/F9 quicksave: World snapshots registered components to a checksummed
  // binary file; restore rebuilds entities, sprites and the player handle.
  const auto save_path = options.project_root / options.save_file;
  auto save_world = [&] {
    try {
      save_world_to_file(world, save_path);
    } catch (const std::exception &) {
    }
  };
  auto load_world = [&] {
    if (!load_world_from_file(world, save_path)) return;
    impl.entities = world.entities();
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
    impl.sprites.assign(impl.entities.size(), {});
    for (std::size_t i = 0; i < impl.entities.size(); ++i) {
      if (const auto *sp = world.get<SpriteRef>(impl.entities[i]);
          sp != nullptr && !sp->value.empty())
        impl.sprites[i] = decode_sprite(sp->value);
    }
  };

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
    for (const auto &event : snapshot.events) {
      if (event.type == InputEventType::EscapePressed) return 0;
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
        raw.pressed = event.type == InputEventType::GamepadPressed;
        break;
      case InputEventType::GamepadAxis:
        raw.kind = RawInputEvent::Kind::GamepadAxis;
        raw.code = event.gamepad_axis;
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
        // Horizontal blocking: a moving gravity-affected entity whose side
        // crosses a solid's side stops against it (walls). One-way
        // platforms never side-block.
        if (gscale != 0.f && v->dx != 0.f) {
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
        // horizontal motion (mirrors the solid-entity rule). Whichever
        // colliding layer blocks supplies the cell geometry.
        if (gscale != 0.f && v->dx != 0.f) {
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
        t->y += v->dy * dt_step;
        // Platform landings: a falling, gravity-affected entity whose
        // bottom crossed a solid's top this step lands on it. One-way
        // platforms land identically (crossing is only detected when
        // falling, so rising/horizontal motion passes through).
        if (gscale != 0.f && v->dy > 0.f) {
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
            }
          }
          // Tilemap landing: the bottom edge's cells catch a fall —
          // each colliding layer lands on its own row geometry.
          for (const auto *land_tm : impl.tilemaps()) {
            if (!land_tm->collide || land_tm->tile_w <= 0 ||
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
                break;
              }
            }
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
        if (bounced && impl.player && entity == *impl.player && bounce_clip)
          audio.play_effect(bounce_clip);
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
      // AABB contact events: collect overlaps during the scan, then fire
      // callbacks afterwards so handlers may spawn/destroy entities safely.
      if (on_collision || on_collision_exit) {
        std::set<std::pair<std::uint64_t, std::uint64_t>> now;
        std::vector<std::pair<EntityId, EntityId>> entered;
        for (std::size_t i = 0; i < impl.entities.size(); ++i) {
          const auto *ta = world.get<Transform2D>(impl.entities[i]);
          const auto *ea = world.get<Extent2D>(impl.entities[i]);
          if (!ta || !ea) continue;
          for (std::size_t j = i + 1; j < impl.entities.size(); ++j) {
            const auto *tb = world.get<Transform2D>(impl.entities[j]);
            const auto *eb = world.get<Extent2D>(impl.entities[j]);
            if (!tb || !eb) continue;
            if (ta->x < tb->x + eb->w && tb->x < ta->x + ea->w &&
                ta->y < tb->y + eb->h && tb->y < ta->y + ea->h) {
              const auto a = impl.entities[i].value();
              const auto b = impl.entities[j].value();
              const auto key = std::minmax(a, b);
              now.insert(key);
              if (!impl.overlapping.count(key))
                entered.emplace_back(impl.entities[i], impl.entities[j]);
            }
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

    DrawList draw;
    draw.overlay.push_back(
        FilledRectangle{{0, 0, w, h}, {impl.bg_r, impl.bg_g, impl.bg_b, 255}});
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
          // Horizontal strip: cell width = sprite width / frames, current
          // cell from accumulated sim time (deterministic in fixed-step).
          const auto &res = *impl.sprites[i];
          const float cell_w =
              static_cast<float>(res.width()) / anim->frames;
          const int frame = anim->fps > 0.f
                                ? static_cast<int>(impl.sim_time *
                                                   anim->fps) %
                                      anim->frames
                                : 0;
          img.source = UiRect{frame * cell_w, 0.f, cell_w,
                              static_cast<float>(res.height())};
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
  if (!options.snapshot_out.empty()) {
    try {
      save_world_to_file(world, options.snapshot_out);
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
  }
  return run();
}

} // namespace stellar::engine
