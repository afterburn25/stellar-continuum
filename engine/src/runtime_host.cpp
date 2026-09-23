#include "stellar/engine/runtime_host.hpp"

#include "stellar/engine/native_audio.hpp"
#include "stellar/engine/native_map_platform.hpp"
#include "stellar/engine/package.hpp"
#include "stellar/engine/runtime_paths.hpp"
#include "stellar/engine/texture_cook.hpp"

#include <chrono>
#include <cstdlib>
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

int RuntimeHost::run() {
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
  impl.content = std::make_unique<ContentResolver>(
      options.package_id, options.project_root, exe_dir);
  const std::size_t cooked_assets = impl.content->cooked_count();

  Window window(options.window_title, options.width, options.height,
                options.fullscreen, exe_dir / "engine-default-font.ttf");
  window.set_auto_frame_cap();

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
    for (const auto e : impl.entities) world.destroy(e);
    impl.entities = spawn_scene(world, doc);
    impl.player = find_entity_by_name(world, "player");
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
  const auto scene_file = options.project_root / options.scene_file;
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
  for (;;) {
    const auto snapshot = window.poll();
    if (snapshot.quit_requested) break;
    for (const auto &event : snapshot.events) {
      if (event.type == InputEventType::EscapePressed) return 0;
      if (event.type == InputEventType::KeyPressed) {
        held_keys.insert(event.key);
        if (event.key == 0x4000003e) save_world();   // F5
        if (event.key == 0x40000042) load_world();   // F9
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
        const float dy = (held('s') || held(0x40000051) ? 1.f : 0.f) -
                         (held('w') || held(0x40000052) ? 1.f : 0.f);
        v->dx = dx * 320.f;
        v->dy = dy * 320.f;
      }
    }

    // Simulation step: fixed-timestep mode accumulates real time and steps
    // at a constant rate so gameplay is frame-rate independent.
    const float step = options.fixed_timestep_hz > 0.0
                           ? static_cast<float>(1.0 / options.fixed_timestep_hz)
                           : 0.f;
    auto simulate = [&](float dt_step) {
      if (on_update) on_update(world, dt_step);
      for (const auto entity : impl.entities) {
        auto *t = world.get<Transform2D>(entity);
        auto *v = world.get<Velocity2D>(entity);
        const auto *ext = world.get<Extent2D>(entity);
        if (!t || !v || !ext) continue;
        t->x += v->dx * dt_step;
        t->y += v->dy * dt_step;
        bool bounced = false;
        if (t->x < 0 || t->x > w - ext->w) {
          v->dx = -v->dx;
          bounced = true;
          t->x = std::clamp(t->x, 0.f, w - ext->w);
        }
        if (t->y < 0 || t->y > h - ext->h) {
          v->dy = -v->dy;
          bounced = true;
          t->y = std::clamp(t->y, 0.f, h - ext->h);
        }
        if (bounced && impl.player && entity == *impl.player && bounce_clip)
          audio.play_effect(bounce_clip);
      }
    };
    if (step > 0.f) {
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
    draw.overlay.push_back(FilledRectangle{{0, 0, w, h}, {8, 16, 26, 255}});
    for (std::size_t i = 0; i < impl.entities.size(); ++i) {
      const auto *t = world.get<Transform2D>(impl.entities[i]);
      const auto *ext = world.get<Extent2D>(impl.entities[i]);
      const auto *tint = world.get<Tint>(impl.entities[i]);
      if (!t || !ext || !tint) continue;
      if (i < impl.sprites.size() && impl.sprites[i])
        draw.overlay.push_back(
            Image{impl.sprites[i], {t->x, t->y, ext->w, ext->h}});
      else
        draw.overlay.push_back(FilledRectangle{{t->x, t->y, ext->w, ext->h},
                                               {tint->r, tint->g, tint->b,
                                                255}});
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
  }
  return run();
}

} // namespace stellar::engine
