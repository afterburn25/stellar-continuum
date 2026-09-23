// e2e-tile - Stellar Engine game host.
// RuntimeHost owns the window, ECS world, scene hot-reload,
// WASD player input, sprites, audio and F5/F9 quicksave -
// this file only declares the project and your game logic.
#include <stellar/engine/runtime_host.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/runtime_diagnostics.hpp>

namespace engine = stellar::engine;

int main(int argc, char **argv) {
  // Session log + crash minidumps land in the project's logs/ dir.
  engine::RuntimeDiagnostics diagnostics{"e2e-tile",
                                         "dev", "logs"};
  engine::RuntimeHost host{
      {.package_id = "game.e2e-tile",
       .window_title = "e2e-tile"}};

  // Game logic lives here: it runs each frame after input,
  // before the built-in velocity/bounce integration. The ECS
  // world carries the canonical scene components (Transform2D,
  // Velocity2D, Extent2D, Tint, EntityName, SpriteRef, Layer).
  // This demo keeps the camera centered on the player entity.
  host.on_update = [&host](engine::World &world, float dt) {
    (void)world; (void)dt;
    if (!host.player()) return;
    const auto *t =
        host.world().get<engine::Transform2D>(*host.player());
    const auto *ext =
        host.world().get<engine::Extent2D>(*host.player());
    if (t)
      host.set_camera(
          t->x + (ext ? ext->w : 0.f) * .5f -
              host.viewport_width() * .5f,
          t->y + (ext ? ext->h : 0.f) * .5f -
              host.viewport_height() * .5f);
  };

  // The status line under the built-in help text; shows the live
  // entity count and pause state. host.paused()/request_quit()/
  // set_scene() give game code the same control the keys do.
  host.on_status = [&host] {
    return std::to_string(host.world().entities().size()) +
           (host.paused() ? " entities | PAUSED (P)"
                          : " entities");
  };

  // A deterministic ember trail definition (VfxSystem runs in
  // sim time, so --fixed-hz runs stay reproducible).
  engine::EmitterDefinition ember{};
  ember.id = "ember";
  ember.spawn_rate_per_second = 40.f;
  ember.particle_lifetime_seconds = 0.5f;
  ember.velocity_min = {-30.f, -60.f, 0.f};
  ember.velocity_max = {30.f, -20.f, 0.f};
  ember.gravity = {0.f, 140.f, 0.f};
  ember.tint_r.add_key(0.f, 1.f);
  ember.tint_r.add_key(1.f, 0.9f);
  ember.tint_g.add_key(0.f, 0.85f);
  ember.tint_g.add_key(1.f, 0.2f);
  ember.tint_b.add_key(0.f, 0.3f);
  ember.tint_b.add_key(1.f, 0.05f);
  ember.opacity_over_life.add_key(0.f, 1.f);
  ember.opacity_over_life.add_key(1.f, 0.f);
  host.vfx().define(ember);

  // Space fires a 'spark' entity from the player; contact events
  // destroy sparks. spawn_entity/destroy_entity join and leave the
  // tracked set (integration, bounce, render, collisions).
  host.on_event = [&host](
      const stellar::native_map::InputEvent &event) {
    using stellar::native_map::InputEventType;
    if (event.type != InputEventType::KeyPressed ||
        !host.player())
      return;
    const auto *t =
        host.world().get<engine::Transform2D>(*host.player());
    const auto *ext =
        host.world().get<engine::Extent2D>(*host.player());
    // C mines the tile under the player: tilemap cells are
    // authoritative world state, so edits persist via F5 saves.
    if (event.key == 'c') {
      if (const auto te = host.tilemap_entity()) {
        if (auto *tm = host.world().get<engine::Tilemap>(*te);
            tm && tm->columns > 0 && tm->tile_w > 0 &&
            tm->tile_h > 0) {
          const int cx = static_cast<int>(
              (t->x + (ext ? ext->w : 0.f) * .5f) / tm->tile_w);
          const int cy = static_cast<int>(
              (t->y + (ext ? ext->h : 0.f) * .5f) / tm->tile_h);
          const auto idx =
              static_cast<std::size_t>(cy * tm->columns + cx);
          if (cx >= 0 && cy >= 0 && cx < tm->columns &&
              idx < tm->cells.size())
            tm->cells[idx] = -1;
        }
      }
      return;
    }
    if (event.key != ' ')
      return;
    engine::SceneEntity spark{};
    spark.name = "spark";
    // Spawn above the player so it does not instantly overlap its
    // own firer (contact events destroy sparks).
    spark.x = t->x; spark.y = t->y - (ext ? ext->h : 64.f) - 8.f;
    spark.w = spark.h = 12.f;
    spark.vx = 420.f; spark.vy = -420.f;
    spark.r = 255; spark.g = 220; spark.b = 80;
    // Sparks expire on their own if they never hit anything.
    spark.ttl = 3.f;
    const auto spark_id = host.spawn_entity(spark);
    // Attach a deterministic ember emitter to the spark - it
    // follows the entity and stops when the spark dies.
    host.spawn_emitter("ember", spark.x + 6.f,
                       spark.y + 6.f, spark_id);
  };
  host.on_collision = [&host](engine::EntityId a,
                              engine::EntityId b) {
    for (const auto id : {a, b})
      if (const auto *n =
              host.world().get<engine::EntityName>(id);
          n != nullptr && n->value == "spark")
        host.destroy_entity(id);
  };

  // '--frames N' renders N frames then exits (CI smoke tests);
  // '--fixed-hz N' runs deterministic fixed-timestep simulation;
  // '--scene <path>' picks a different editor scene document.
  try {
    return host.run(argc, argv);
  } catch (const std::exception &error) {
    diagnostics.fatal(error.what());
    return 1;
  }
}
