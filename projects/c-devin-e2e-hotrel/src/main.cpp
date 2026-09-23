// C:/Devin/e2e-hotrel - Stellar Engine game host.
// Opens a native window through stellar::platform, resolves the
// project's content packages, and renders. Escape quits.
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/package.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/engine/scene_document.hpp>
#include <stellar/engine/world.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace stellar::native_map;
namespace engine = stellar::engine;

// Game components live in your game's headers; World stores any type.
struct Transform { float x, y; };
struct Velocity { float dx, dy; };
struct Extent { float w, h; };
struct Tint { std::uint8_t r, g, b; };

int main() {
  // Content roots resolve relative to the working directory (the
  // project root when launched from the engine tools). The base
  // package registers first; the namespace is then protected so mod
  // packages under mods/ cannot override it.
  engine::PackageRegistry registry;
  engine::scan_packages(registry, "packages");
  registry.protect_namespace("game.c-devin-e2e-hotrel");
  engine::scan_packages(registry, "mods");
  const auto plan = registry.resolve();

  // Cooked content validates when present: Content/ beside the
  // executable (packaged layout from the tools' PACKAGE step) or
  // build/cooked/ under the project root (dev layout from COOK).
  // A local registry — not the global mount — so loose resources like
  // the bundled font keep resolving beside the executable.
  std::size_t cooked_assets = 0;
  const auto exe_dir = engine::executable_directory();
  for (const auto manifest :
       {exe_dir / "Content" / "runtime.stmanifest",
        std::filesystem::path("build") / "cooked" / "Content" /
            "runtime.stmanifest"}) {
    if (std::filesystem::is_regular_file(manifest)) {
      const engine::AssetRegistry cooked_registry(manifest);
      cooked_assets = cooked_registry.records().size();
      break;
    }
  }

  Window window("C:/Devin/e2e-hotrel", 1280, 720, false,
                exe_dir / "engine-default-font.ttf");
  window.set_auto_frame_cap();

  // The entity/component world: create entities, attach components,
  // tick systems each frame.
  engine::World world;
  // Authored entities come from editor/scene.json (written by the
  // engine tools' SCENE section). One demo entity when absent.
  std::vector<engine::SceneEntity> scene_entities;
  if (const auto doc =
          engine::SceneDocument::load("editor/scene.json"))
    scene_entities = doc->entities;
  if (scene_entities.empty())
    scene_entities.push_back(engine::SceneEntity{
        "demo", 120.f, 160.f, 96.f, 96.f, 240.f, 150.f});
  std::vector<engine::EntityId> entities;
  for (const auto &source : scene_entities) {
    const auto entity = world.create();
    world.add(entity, Transform{source.x, source.y});
    world.add(entity, Velocity{source.vx, source.vy});
    world.add(entity, Extent{source.w, source.h});
    world.add(entity, Tint{source.r, source.g, source.b});
    entities.push_back(entity);
  }
  // Optional sprite images decode once at startup from the base
  // package's content dir (sprite "data/logo.png" resolves to
  // packages/game.c-devin-e2e-hotrel/content/data/logo.png).
  std::vector<std::shared_ptr<const RgbaImage>> sprites(
      entities.size());
  for (std::size_t i = 0; i < scene_entities.size(); ++i) {
    if (scene_entities[i].sprite.empty()) continue;
    try {
      sprites[i] = decode_rgba_image(
          std::filesystem::path("packages") / "game.c-devin-e2e-hotrel" /
              "content" / scene_entities[i].sprite,
          2048);
    } catch (const std::exception &) {
    }
  }

  auto last = std::chrono::steady_clock::now();
  for (;;) {
    const auto snapshot = window.poll();
    if (snapshot.quit_requested) break;
    for (const auto &event : snapshot.events)
      if (event.type == InputEventType::EscapePressed) return 0;
    if (!snapshot.renderable()) continue;

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    const float w = static_cast<float>(snapshot.drawable_width);
    const float h = static_cast<float>(snapshot.drawable_height);

    // Movement system: integrate velocity, bounce off the frame.
    for (const auto entity : entities) {
      auto *t = world.get<Transform>(entity);
      auto *v = world.get<Velocity>(entity);
      const auto *ext = world.get<Extent>(entity);
      if (!t || !v || !ext) continue;
      t->x += v->dx * dt; t->y += v->dy * dt;
      if (t->x < 0 || t->x > w - ext->w) v->dx = -v->dx;
      if (t->y < 0 || t->y > h - ext->h) v->dy = -v->dy;
    }

    DrawList draw;
    draw.overlay.push_back(
        FilledRectangle{{0, 0, w, h}, {8, 16, 26, 255}});
    for (std::size_t i = 0; i < entities.size(); ++i) {
      const auto *t = world.get<Transform>(entities[i]);
      const auto *ext = world.get<Extent>(entities[i]);
      const auto *tint = world.get<Tint>(entities[i]);
      if (!t || !ext || !tint) continue;
      if (i < sprites.size() && sprites[i])
        draw.overlay.push_back(
            Image{sprites[i], {t->x, t->y, ext->w, ext->h}});
      else
        draw.overlay.push_back(FilledRectangle{
            {t->x, t->y, ext->w, ext->h},
            {tint->r, tint->g, tint->b, 255}});
    }
    draw.overlay.push_back(Text{
        {w * .5f, h * .5f - 80.f}, "C:/Devin/e2e-hotrel",
        {86, 196, 255, 255}, 42, 0, std::nullopt, TextAlign::Center,
        FontFace::Heading});
    draw.overlay.push_back(Text{
        {w * .5f, h * .5f + 12.f},
        std::to_string(plan.order.size()) +
            " content package(s), " +
            std::to_string(cooked_assets) + " cooked asset(s) - " +
            (plan.ok ? std::string("load plan ok")
                     : std::string("load plan FAILED")),
        {210, 230, 244, 255}, 16, 0, std::nullopt,
        TextAlign::Center});
    draw.overlay.push_back(Text{
        {w * .5f, h * .5f + 40.f},
        "drop content into packages/game.c-devin-e2e-hotrel/content/ and cook",
        {122, 170, 190, 255}, 14, 0, std::nullopt,
        TextAlign::Center});
    window.draw(draw);
  }
  return plan.ok ? 0 : 1;
}
