// RuntimeHost headless mode: a generated-game host runs its full loop
// without a window or audio device, stepping once per frame so frame
// counts are deterministic. Covers the region-query APIs
// (entities_in_rect/radius, entities3d_in_radius/box) that otherwise
// need a windowed run(), plus headless record→replay verification.

#include <stellar/engine/replay.hpp>
#include <stellar/engine/runtime_host.hpp>
#include <stellar/engine/world.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::engine;

RuntimeHostOptions headless_options(const std::filesystem::path &root) {
  RuntimeHostOptions options;
  options.package_id = "test.runtime";
  options.project_root = root;
  options.width = 640;
  options.height = 480;
  options.headless = true;
  options.fixed_timestep_hz = 60.0;
  options.frame_limit = 4;
  return options;
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "stellar-runtime-tests";
  std::error_code ec;
  std::filesystem::create_directories(root, ec);

  // Headless run: the demo entity spawns, on_update fires once per frame
  // and the 2D region queries resolve against the tracked set.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    std::size_t rect_hits = 0, radius_hits = 0, far_hits = 0;
    bool picked = false, picked_miss = false, picked_hidden = false;
    EntityId ghost_id{};
    host.on_update = [&](World &, float) {
      ++updates;
      // Capture on update 1 — the ghost spawned below would join later
      // queries otherwise.
      if (updates == 1) {
        rect_hits =
            host.entities_in_rect(0.f, 0.f, 2000.f, 2000.f).size();
        // The demo entity starts at (120,160) 96x96 — center near
        // (168,208) before it integrates far (240,150 u/s at 60 Hz).
        radius_hits =
            host.entities_in_radius(175.f, 215.f, 20.f).size();
        far_hits = host.entities_in_radius(-500.f, -500.f, 10.f).size();
      }
      // Screen-space picking: the demo rect sits at ~(120,160)-(216,256)
      // under the identity camera on update 1; empty space misses and a
      // hidden entity never picks.
      if (updates == 1) {
        picked = host.entity_at(150.f, 180.f).has_value();
        picked_miss = !host.entity_at(5.f, 5.f).has_value();
        SceneEntity ghost{};
        ghost.name = "ghost";
        ghost.x = 700.f;
        ghost.y = 700.f;
        ghost.w = 60.f;
        ghost.h = 60.f;
        ghost.visible = false;
        ghost_id = host.spawn_entity(ghost);
      }
      if (updates == 2)
        picked_hidden = !host.entity_at(710.f, 710.f).has_value();
    };
    check(host.run() == 0, "headless run exits cleanly");
    check(updates == 4, "headless steps once per frame under --frames");
    check(rect_hits == 1, "entities_in_rect finds the demo entity");
    check(radius_hits == 1, "entities_in_radius finds the demo entity");
    check(far_hits == 0, "entities_in_radius excludes far entities");
    check(picked, "entity_at hits the demo rect");
    check(picked_miss, "entity_at misses empty space");
    check(picked_hidden, "entity_at skips Hidden entities");
  }

  // Headless runs are deterministic: identical options produce
  // byte-identical world snapshots at the same frame.
  {
    std::vector<std::uint8_t> first, second;
    for (auto *out : {&first, &second}) {
      RuntimeHost host{headless_options(root)};
      int updates = 0;
      host.on_update = [&](World &world, float) {
        if (++updates == 3) *out = world.snapshot();
      };
      check(host.run() == 0, "determinism probe run exits cleanly");
    }
    check(!first.empty() && first == second,
          "headless snapshots are byte-identical across runs");
  }

  // 3D region queries: runtime-spawned mesh entities join the tracked
  // 3D set and answer radius/box queries from inside on_update.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    std::size_t box_hits = 0, sphere_hits = 0, list_size = 0;
    host.on_update = [&](World &, float) {
      if (++updates == 1) {
        Scene3dEntity box{};
        box.name = "probe";
        box.x = 5.f;
        box.y = -2.f;
        box.z = 7.f;
        host.spawn_entity3d(box);
      }
      box_hits = host.entities3d_in_box(5.f, -2.f, 7.f, 1.f, 1.f, 1.f)
                     .size();
      sphere_hits = host.entities3d_in_radius(5.f, -2.f, 7.f, 0.5f).size();
      list_size = host.entities3d().size();
    };
    check(host.run() == 0, "headless 3D run exits cleanly");
    check(list_size == 1, "spawn_entity3d joins the tracked 3D set");
    check(box_hits == 1, "entities3d_in_box finds the spawned entity");
    check(sphere_hits == 1, "entities3d_in_radius finds the spawned entity");
  }

  // Headless record → replay: a 60-frame recording checkpoints at ticks
  // 0 and 30; replaying it verifies every checkpoint and exits 0 under
  // --replay-exit semantics (run() returns nonzero on divergence).
  const auto recording = root / "host.rec";
  {
    auto options = headless_options(root);
    options.frame_limit = 60;
    options.record_file = recording;
    RuntimeHost host{options};
    check(host.run() == 0, "headless recording run exits cleanly");
    std::ifstream in(recording, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    const auto parsed = ReplayRecorder::parse(text);
    check(parsed && parsed->checkpoints().size() == 2,
          "headless recording journals world checkpoints");
  }
  {
    auto options = headless_options(root);
    options.frame_limit = 0;
    options.replay_file = recording;
    options.replay_exit = true;
    RuntimeHost host{options};
    check(host.run() == 0, "headless replay verifies checkpoints");
  }
  {
    // A forged checkpoint hash must diverge on frame 0 and exit nonzero.
    ReplayRecorder forged{ReplayHeader{}};
    forged.checkpoint(0, 0xDEADBEEFull, "world");
    const auto bad = root / "host-bad.rec";
    std::ofstream out(bad, std::ios::binary);
    out << forged.serialize();
    out.close();
    auto options = headless_options(root);
    options.frame_limit = 0;
    options.replay_file = bad;
    options.replay_exit = true;
    RuntimeHost host{options};
    check(host.run() == 1, "forged checkpoint diverges and exits nonzero");
  }

  // Runtime spawn/destroy, camera transform, tilemap queries and the
  // control surface (request_quit, set_paused, sim_time, rng) — all only
  // reachable inside run(), so headless mode is what makes them testable.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    EntityId spawned{};
    bool destroyed_ok = false, found_spawned = false;
    double time_at_two = -1.0;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 1) {
        SceneEntity entity{};
        entity.name = "runtime-spawned";
        entity.x = 400.f;
        entity.y = 300.f;
        spawned = host.spawn_entity(entity);
        found_spawned =
            host.find_entity("runtime-spawned") == spawned;
        // Camera: pan + zoom, then screen_to_world round-trips a point.
        host.set_camera(100.f, 50.f, 2.f);
      }
      if (updates == 2) {
        destroyed_ok = host.destroy_entity(spawned);
        time_at_two = host.sim_time();
      }
    };
    check(host.run() == 0, "control-surface run exits cleanly");
    check(found_spawned, "spawn_entity + find_entity resolve by name");
    check(destroyed_ok, "destroy_entity removes the runtime entity");
    check(std::abs(host.camera_x() - 100.f) < 1e-4f &&
              std::abs(host.camera_y() - 50.f) < 1e-4f &&
              std::abs(host.camera_zoom() - 2.f) < 1e-4f,
          "set_camera getters round-trip");
    const auto [wx, wy] = host.screen_to_world(200.f, 150.f);
    check(std::abs(wx - 200.f) < 1e-3f && std::abs(wy - 125.f) < 1e-3f,
          "screen_to_world applies camera + zoom");
    check(time_at_two > 0.03 && time_at_two < 0.05,
          "sim_time advances one fixed step per frame");
  }

  // request_quit ends the loop cleanly before --frames is exhausted.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    host.on_update = [&](World &, float) {
      if (++updates == 2) host.request_quit();
    };
    check(host.run() == 0, "request_quit exits cleanly");
    check(updates == 2, "request_quit stops before the frame budget");
  }

  // set_paused halts the sim step while the frame loop keeps running.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    host.set_paused(true);
    host.on_update = [&](World &, float) { ++updates; };
    check(host.run() == 0, "paused run exits on --frames");
    check(updates == 0, "set_paused suppresses the sim step");
    check(host.paused(), "paused() reports the set state");
  }

  // rng() is a world-carried deterministic stream — same seed, same
  // draws across separate hosts.
  {
    std::uint64_t draws[2]{};
    for (int i = 0; i < 2; ++i) {
      RuntimeHost host{headless_options(root)};
      bool drawn = false;
      host.on_update = [&](World &, float) {
        if (!drawn) {
          draws[i] = host.rng().next_u64();
          drawn = true;
        }
      };
      check(host.run() == 0, "rng probe run exits cleanly");
    }
    check(draws[0] == draws[1] && draws[0] != 0,
          "rng() reproduces the same stream for the same seed");
  }

  // Runtime tilemap spawn + cell queries by index and name.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    int read_back = -2;
    bool wrote = false;
    std::size_t map_count = 0;
    host.on_update = [&](World &, float) {
      if (++updates != 1) return;
      SceneTilemap map{};
      map.name = "runtime-ground";
      map.columns = 4;
      map.cells = {1, -1, 1, -1, -1, 1, -1, 1};
      const auto id = host.spawn_tilemap(map);
      map_count = host.tilemap_count();
      wrote = host.set_tile_at("runtime-ground", 5.f, 5.f, 7);
      read_back = host.tile_at("runtime-ground", 5.f, 5.f);
      check(host.tilemap_index("runtime-ground").has_value(),
            "tilemap_index resolves the runtime map by name");
      check(host.world().get<Tilemap>(id) != nullptr,
            "spawn_tilemap returns a carrier with a Tilemap");
    };
    check(host.run() == 0, "tilemap run exits cleanly");
    check(map_count == 1, "spawn_tilemap joins the map list");
    check(wrote && read_back == 7,
          "set_tile_at/tile_at round-trip by name");
  }

  // Contact events: two spawned overlapping entities fire on_collision
  // on the entering step and on_collision_exit once separated.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    int enters = 0, exits = 0;
    EntityId mover{};
    host.on_collision = [&](EntityId, EntityId) { ++enters; };
    host.on_collision_exit = [&](EntityId, EntityId) { ++exits; };
    host.on_update = [&](World &world, float) {
      ++updates;
      if (updates == 1) {
        SceneEntity a{};
        a.name = "contact-a";
        a.x = 500.f;
        a.y = 500.f;
        SceneEntity b{};
        b.name = "contact-b";
        b.x = 505.f;
        b.y = 505.f;
        host.spawn_entity(a);
        mover = host.spawn_entity(b);
      }
      if (updates == 3)
        if (auto *t = world.get<Transform2D>(mover)) t->x = 3000.f;
    };
    check(host.run() == 0, "contact run exits cleanly");
    check(enters >= 1, "on_collision fires on overlap entry");
    check(exits >= 1, "on_collision_exit fires on separation");
  }

  // Tilemap collision: a gravity-affected entity falls onto a colliding
  // row of cells — on_tile_land reports the exact cell once and the
  // entity rests on the tile top.
  {
    std::filesystem::create_directories(root / "editor", ec);
    {
      std::ofstream out(root / "editor" / "scene.json");
      out << R"({"gravity":900,
"entities":[{"name":"faller","x":40,"y":0,"w":32,"h":32}],
"tilemaps":[{"name":"ground","y":128,"tileW":32,"tileH":32,
"columns":8,"collide":true,"cells":[1,1,1,1,1,1,1,1]}]})";
    }
    auto options = headless_options(root);
    options.frame_limit = 90;
    RuntimeHost host{options};
    int landings = 0;
    int land_cx = -1, land_tile = -1;
    float rest_y = -1.f;
    host.on_tile_land = [&](EntityId, std::size_t, int cx, int cy,
                            int tile) {
      ++landings;
      land_cx = cx;
      land_tile = tile;
      check(cy == 0, "tile_land reports the landed row");
    };
    host.on_update = [&](World &world, float) {
      for (const auto e : world.entities())
        if (const auto *n = world.get<EntityName>(e);
            n && n->value == "faller")
          if (const auto *t = world.get<Transform2D>(e))
            rest_y = t->y;
    };
    check(host.run() == 0, "tilemap-landing run exits cleanly");
    check(landings == 1, "on_tile_land fires once per touchdown");
    check(land_cx == 1 && land_tile == 1,
          "on_tile_land reports the cell and tile value");
    check(std::abs(rest_y - 96.f) < 1.f,
          "the faller rests on the tile top (128 - 32)");
  }

  // The host steps VfxSystem in sim time — a spawned emitter produces
  // particles deterministically.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    VfxInstanceId instance = invalid_vfx_instance;
    std::size_t particles = 0, particles_late = 0;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 1) {
        EmitterDefinition def;
        def.id = "test-emitter";
        def.spawn_rate_per_second = 120.f;
        def.particle_lifetime_seconds = 1.0f;
        host.vfx().define(std::move(def));
        instance = host.spawn_emitter("test-emitter", 300.f, 300.f);
      }
      if (updates == 3)
        particles = host.vfx().particles(instance).size();
      if (updates == 4)
        particles_late = host.vfx().particles(instance).size();
    };
    check(host.run() == 0, "vfx run exits cleanly");
    check(instance != invalid_vfx_instance,
          "spawn_emitter returns a live instance");
    check(host.vfx().alive(instance), "the emitter stays alive");
    check(particles > 0 && particles_late > particles,
          "particles accumulate in sim time");
  }

  // spawn_entity3d + the 3D region queries track a separate entity set.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    EntityId spawned{};
    std::size_t in_box = 0, in_radius = 0, out_of_radius = 0;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 1) {
        Scene3dEntity e{};
        e.name = "crate";
        e.mesh = "box";
        e.x = 50.f;
        e.y = 0.f;
        e.z = -30.f;
        spawned = host.spawn_entity3d(e);
        in_box = host.entities3d_in_box(50.f, 0.f, -30.f, 5.f, 5.f, 5.f)
                     .size();
        in_radius =
            host.entities3d_in_radius(50.f, 0.f, -30.f, 10.f).size();
        out_of_radius =
            host.entities3d_in_radius(-500.f, 0.f, 0.f, 10.f).size();
      }
    };
    check(host.run() == 0, "3D spawn run exits cleanly");
    check(spawned != EntityId{}, "spawn_entity3d returns an id");
    check(host.entities3d().size() == 1,
          "the spawned entity joins the tracked 3D set");
    check(in_box == 1, "entities3d_in_box finds the crate");
    check(in_radius == 1, "entities3d_in_radius finds the crate");
    check(out_of_radius == 0, "entities3d_in_radius excludes far entities");
  }

  // set_scene swaps the spawned set mid-run — level switching.
  {
    std::filesystem::create_directories(root / "editor", ec);
    {
      std::ofstream out(root / "editor" / "scene.json");
      out << R"({"entities":[{"name":"alpha","x":10,"y":20}]})";
    }
    {
      std::ofstream out(root / "editor" / "scene-b.json");
      out << R"({"entities":[{"name":"beta","x":50,"y":60}]})";
    }
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    bool saw_alpha = false, saw_beta = false;
    host.on_update = [&](World &world, float) {
      ++updates;
      for (const auto e : world.entities())
        if (const auto *n = world.get<EntityName>(e)) {
          if (n->value == "alpha") saw_alpha = true;
          if (n->value == "beta") saw_beta = true;
        }
      if (updates == 2) host.set_scene("editor/scene-b.json");
    };
    check(host.run() == 0, "set_scene run exits cleanly");
    check(saw_alpha && saw_beta,
          "set_scene swaps the spawned entity set");
  }

  // --dump-bindings prints the resolved action map and exits before the
  // loop — the sim never ticks.
  {
    auto options = headless_options(root);
    options.dump_bindings = true;
    RuntimeHost host{options};
    bool updated = false;
    host.on_update = [&](World &, float) { updated = true; };
    check(host.run() == 0, "dump-bindings exits cleanly");
    check(!updated, "dump-bindings exits before the sim loop");
  }

  std::filesystem::remove_all(root, ec);
  if (failures == 0)
    std::cout << "engine runtime host tests passed\n";
  return failures == 0 ? 0 : 1;
}
