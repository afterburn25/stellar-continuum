#include "stellar/engine/runtime_host.hpp"

#include "stellar/engine/native_audio.hpp"
#include "stellar/engine/native_map_platform.hpp"
#include "stellar/engine/package.hpp"
#include "stellar/engine/runtime_diagnostics.hpp"
#include "stellar/engine/runtime_paths.hpp"
#include "stellar/engine/texture_cook.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
  bool quit_requested = false;
  bool paused = false;
  double time_scale = 1.0;
  // Assigned inside run(); applies a scene switch immediately.
  std::function<void(const std::string &)> switch_scene;
  // Assigned inside run(); runtime entity spawn/destroy entry points.
  std::function<EntityId(const SceneEntity &)> spawn_fn;
  std::function<bool(EntityId)> destroy_fn;
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
  // Entities resting on the floor or a solid — jump requires groundedness.
  std::set<std::uint64_t> grounded;
  // Accumulated simulation seconds — drives sprite-strip animation so
  // playback is deterministic under --fixed-hz.
  double sim_time = 0.0;
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
void RuntimeHost::set_camera(float x, float y, float zoom) {
  impl_->cam_x = x;
  impl_->cam_y = y;
  impl_->cam_zoom = zoom > 0.f ? zoom : 1.f;
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
  auto spawn_entities = [&](const SceneDocument &doc) {
    impl.bg_r = doc.bg_r;
    impl.bg_g = doc.bg_g;
    impl.bg_b = doc.bg_b;
    impl.gravity = doc.gravity;
    for (const auto e : impl.entities) world.destroy(e);
    impl.entities = spawn_scene(world, doc);
    impl.player = find_entity_by_name(world, "player");
    impl.overlapping.clear();
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
    impl.player = find_entity_by_name(world, "player");
    impl.overlapping.clear();
    impl.sprites.assign(impl.entities.size(), {});
    for (std::size_t i = 0; i < impl.entities.size(); ++i) {
      if (const auto *sp = world.get<SpriteRef>(impl.entities[i]);
          sp != nullptr && !sp->value.empty())
        impl.sprites[i] = decode_sprite(sp->value);
    }
  };

  std::unordered_set<std::uint32_t> held_keys;
  float accumulator = 0.f;
  int rendered = 0;
  auto last = std::chrono::steady_clock::now();
  auto scene_poll = last;
  // Resolve world bounds up front too so the jump handler works before the
  // first rendered frame.
  impl.world_w = options.world_width > 0.f
                     ? options.world_width
                     : static_cast<float>(options.width);
  impl.world_h = options.world_height > 0.f
                     ? options.world_height
                     : static_cast<float>(options.height);
  RuntimeDiagnostics::context("runtime:loop");
  for (;;) {
    const auto snapshot = window.poll();
    if (snapshot.quit_requested || impl.quit_requested) break;
    for (const auto &event : snapshot.events) {
      if (event.type == InputEventType::EscapePressed) return 0;
      if (event.type == InputEventType::KeyPressed) {
        held_keys.insert(event.key);
        if (event.key == 0x4000003e) save_world();   // F5
        if (event.key == 0x40000042) load_world();   // F9
        if (event.key == 'p') impl.paused = !impl.paused;  // P pauses the sim
        // Platformer jump: with gravity on, up/W gives a grounded player an
        // impulse instead of held-key velocity.
        if (impl.gravity != 0.f && impl.player &&
            (event.key == 'w' || event.key == 0x40000052)) {
          auto *v = world.get<Velocity2D>(*impl.player);
          if (v && impl.grounded.count(impl.player->value()))
            v->dy = -520.f;
        }
      }
      if (event.type == InputEventType::KeyReleased)
        held_keys.erase(event.key);
      if (on_event) on_event(event);
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
    impl.world_w = options.world_width > 0.f ? options.world_width : w;
    impl.world_h = options.world_height > 0.f ? options.world_height : h;
    const float world_w = impl.world_w;
    const float world_h = impl.world_h;

    // Input system: WASD/arrow keys drive the entity named "player"
    // (SDL3 keycodes: arrows are 0x4000004f-0x40000052).
    if (impl.player) {
      auto *v = world.get<Velocity2D>(*impl.player);
      if (v) {
        const auto held = [&](std::uint32_t k) {
          return held_keys.count(k) != 0;
        };
        const float dx = (held('d') || held(0x4000004f) ? 1.f : 0.f) -
                         (held('a') || held(0x40000050) ? 1.f : 0.f);
        v->dx = dx * 320.f;
        // With scene gravity active the player is a platformer: dy is
        // owned by gravity/jump, not held-key velocity.
        if (impl.gravity == 0.f) {
          const float dy = (held('s') || held(0x40000051) ? 1.f : 0.f) -
                           (held('w') || held(0x40000052) ? 1.f : 0.f);
          v->dy = dy * 320.f;
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
      for (const auto entity : impl.entities) {
        auto *t = world.get<Transform2D>(entity);
        auto *v = world.get<Velocity2D>(entity);
        const auto *ext = world.get<Extent2D>(entity);
        if (!t || !v || !ext) continue;
        if (world.get<Solid>(entity)) continue;  // solids never move
        const auto *gs = world.get<GravityScale>(entity);
        const float gscale = impl.gravity != 0.f ? (gs ? gs->value : 1.f)
                                                 : 0.f;
        if (gscale != 0.f) v->dy += impl.gravity * gscale * dt_step;
        const float prev_right = t->x + ext->w;
        const float prev_bottom = t->y + ext->h;
        t->x += v->dx * dt_step;
        // Horizontal blocking: a moving gravity-affected entity whose side
        // crosses a solid's side stops against it (walls).
        if (gscale != 0.f && v->dx != 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity || !world.get<Solid>(other)) continue;
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
        t->y += v->dy * dt_step;
        // Platform landings: a falling, gravity-affected entity whose
        // bottom crossed a solid's top this step lands on it.
        if (gscale != 0.f && v->dy > 0.f) {
          for (const auto other : impl.entities) {
            if (other == entity || !world.get<Solid>(other)) continue;
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
        }
        bool bounced = false;
        if (t->x < 0 || t->x > world_w - ext->w) {
          v->dx = -v->dx;
          bounced = true;
          t->x = std::clamp(t->x, 0.f, world_w - ext->w);
        }
        if (t->y < 0 || t->y > world_h - ext->h) {
          // Gravity-affected entities come to rest on the floor instead of
          // bouncing forever; the ceiling still deflects them downward.
          const bool rests = gscale != 0.f && t->y > world_h - ext->h;
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
            if (other == entity || !world.get<Solid>(other)) continue;
            const auto *st = world.get<Transform2D>(other);
            const auto *se = world.get<Extent2D>(other);
            if (st && se && t->x < st->x + se->w && t->x + ext->w > st->x &&
                std::abs(t->y + ext->h - st->y) <= 1.5f) {
              impl.grounded.insert(entity.value());
              break;
            }
          }
        }
        if (bounced && impl.player && entity == *impl.player && bounce_clip)
          audio.play_effect(bounce_clip);
      }
      // Lifetimes tick down in sim time; expired entities self-destruct
      // (collected first so destruction doesn't disturb the scan).
      {
        std::vector<EntityId> expired;
        for (const auto entity : impl.entities)
          if (auto *lt = world.get<Lifetime>(entity);
              lt != nullptr && (lt->remaining -= dt_step) <= 0.f)
            expired.push_back(entity);
        for (const auto id : expired) impl.destroy_fn(id);
      }
      // AABB contact events: collect overlaps during the scan, then fire
      // callbacks afterwards so handlers may spawn/destroy entities safely.
      if (on_collision) {
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
        impl.overlapping = std::move(now);
        for (const auto &[a, b] : entered) on_collision(a, b);
      }
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
    for (const auto i : order) {
      const auto *t = world.get<Transform2D>(impl.entities[i]);
      const auto *ext = world.get<Extent2D>(impl.entities[i]);
      const auto *tint = world.get<Tint>(impl.entities[i]);
      if (!t || !ext || !tint) continue;
      const auto *px = world.get<Parallax>(impl.entities[i]);
      const float parallax = px ? px->value : 1.0f;
      const UiRect rect{(t->x - impl.cam_x * parallax) * impl.cam_zoom,
                        (t->y - impl.cam_y * parallax) * impl.cam_zoom,
                        ext->w * impl.cam_zoom, ext->h * impl.cam_zoom};
      // View culling: skip entities fully outside the window.
      if (rect.x + rect.width < 0 || rect.y + rect.height < 0 ||
          rect.x > w || rect.y > h)
        continue;
      if (i < impl.sprites.size() && impl.sprites[i]) {
        Image img{impl.sprites[i], rect};
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
            FilledRectangle{rect, {tint->r, tint->g, tint->b, 255}});
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
  }
  return run();
}

} // namespace stellar::engine
