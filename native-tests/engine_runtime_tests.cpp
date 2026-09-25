// RuntimeHost headless mode: a generated-game host runs its full loop
// without a window or audio device, stepping once per frame so frame
// counts are deterministic. Covers the region-query APIs
// (entities_in_rect/radius, entities3d_in_radius/box) that otherwise
// need a windowed run(), plus headless record→replay verification.

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/replay.hpp>
#include <stellar/engine/runtime_host.hpp>
#include <stellar/engine/scene_components.hpp>
#include <stellar/engine/world.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
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
    // on_draw/on_status still fire headless — the DrawList is built and
    // handed to the game; only GPU submission is skipped.
    int draws = 0, statuses = 0;
    host.on_draw = [&](stellar::native_map::DrawList &draw, float w,
                       float h) {
      ++draws;
      check(w == 640.f && h == 480.f, "headless on_draw gets the drawable size");
      check(!draw.overlay.empty(), "the HUD overlay is built headless");
    };
    host.on_status = [&] {
      ++statuses;
      return "status line";
    };
    check(host.run() == 0, "headless run exits cleanly");
    check(updates == 4, "headless steps once per frame under --frames");
    check(rect_hits == 1, "entities_in_rect finds the demo entity");
    check(radius_hits == 1, "entities_in_radius finds the demo entity");
    check(far_hits == 0, "entities_in_radius excludes far entities");
    check(picked, "entity_at hits the demo rect");
    check(picked_miss, "entity_at misses empty space");
    check(picked_hidden, "entity_at skips Hidden entities");
    check(draws == 4, "on_draw fires once per headless frame");
    check(statuses == 4, "on_status fires once per headless frame");
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
  {
    // --replay-until N: dumps the canonical world snapshot when journal
    // tick N completes, then exits 0 — the artifact for bisecting a
    // divergent replay.
    auto options = headless_options(root);
    options.frame_limit = 0;
    options.replay_file = recording;
    options.replay_until = 10;
    RuntimeHost host{options};
    std::vector<std::pair<float, float>> positions;
    host.on_update = [&](World &world, float) {
      const auto *t =
          world.get<Transform2D>(*find_entity_by_name(world, "demo"));
      positions.emplace_back(t->x, t->y);
    };
    check(host.run() == 0, "--replay-until exits cleanly");
    const auto dump =
        std::filesystem::path(recording.generic_string() + ".until-10.stw");
    check(std::filesystem::exists(dump), "--replay-until writes the dump");
    // Frame tick 10 is the 11th step — the dump must carry exactly the
    // post-step state observed at that update.
    check(positions.size() == 11, "--replay-until stops after tick 10");
    World restored;
    register_scene_components(restored);
    check(load_world_from_file(restored, dump),
          "--replay-until dump loads");
    const auto *t =
        restored.get<Transform2D>(*find_entity_by_name(restored, "demo"));
    check(std::abs(t->x - positions[10].first) < 1e-4f &&
              std::abs(t->y - positions[10].second) < 1e-4f,
          "the dump captures the tick-10 world state");
  }
  // control surface (request_quit, set_paused, sim_time, rng) — all only
  // reachable inside run(), so headless mode is what makes them testable.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    EntityId spawned{};
    bool destroyed_ok = false, found_spawned = false;
    bool spawn_cb = false;
    EntityId spawn_cb_id{};
    double time_at_two = -1.0;
    host.on_spawn = [&](World &, EntityId id, const SceneEntity &src) {
      if (src.name == "runtime-spawned") {
        spawn_cb = true;
        spawn_cb_id = id;
      }
    };
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
    check(spawn_cb && spawn_cb_id == spawned,
          "on_spawn fires with the new id");
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
    bool wrote = false, carrier_ok = false, destroyed = false;
    std::size_t map_count = 0, after_destroy = 1;
    host.on_update = [&](World &, float) {
      if (++updates != 1) return;
      SceneTilemap map{};
      map.name = "runtime-ground";
      map.columns = 4;
      map.cells = {1, -1, 1, -1, -1, 1, -1, 1};
      const auto id = host.spawn_tilemap(map);
      map_count = host.tilemap_count();
      carrier_ok =
          host.tilemap_entity() == id && host.tilemap_entities().size() == 1;
      wrote = host.set_tile_at("runtime-ground", 5.f, 5.f, 7);
      read_back = host.tile_at("runtime-ground", 5.f, 5.f);
      check(host.tilemap_index("runtime-ground").has_value(),
            "tilemap_index resolves the runtime map by name");
      check(host.world().get<Tilemap>(id) != nullptr,
            "spawn_tilemap returns a carrier with a Tilemap");
      destroyed = host.destroy_tilemap(id);
      after_destroy = host.tilemap_count();
    };
    check(host.run() == 0, "tilemap run exits cleanly");
    check(map_count == 1, "spawn_tilemap joins the map list");
    check(carrier_ok,
          "tilemap_entity/tilemap_entities enumerate the carrier");
    check(wrote && read_back == 7,
          "set_tile_at/tile_at round-trip by name");
    check(destroyed && after_destroy == 0,
          "destroy_tilemap removes the map");
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
    std::optional<RuntimeHost::RaycastHit3D> hit, miss;
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
        // Raycast straight at the crate's center from the -X side, and
        // a parallel ray that misses it entirely.
        hit = host.raycast3d(0.0, 0.0, -30.0, 1.f, 0.f, 0.f, 200.f);
        miss = host.raycast3d(0.0, 0.0, 200.0, 1.f, 0.f, 0.f, 200.f);
      }
    };
    check(host.run() == 0, "3D spawn run exits cleanly");
    check(spawned != EntityId{}, "spawn_entity3d returns an id");
    check(host.entities3d().size() == 1,
          "the spawned entity joins the tracked 3D set");
    check(in_box == 1, "entities3d_in_box finds the crate");
    check(in_radius == 1, "entities3d_in_radius finds the crate");
    check(out_of_radius == 0, "entities3d_in_radius excludes far entities");
    check(hit.has_value() && hit->entity == spawned,
          "raycast3d hits the crate");
    check(hit.has_value() && hit->distance > 40.f && hit->distance < 50.f,
          "raycast3d reports a sane hit distance");
    check(!miss.has_value(), "raycast3d misses off-axis");
  }

  // Injected F5/F9 drive the in-run quicksave/quicksave-load path: a
  // crafted journal presses F5 at tick 2, the world keeps simulating,
  // then F9 at tick 6 restores the saved snapshot — the observed
  // position jumps back to the tick-2 state.
  {
    ReplayRecorder journal;
    // Positional CSV: type 8 = KeyPressed; 0x4000003e = F5,
    // 0x40000042 = F9 (SDL keycodes).
    journal.record(2, "input",
                   "8,1073741886,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    journal.record(6, "input",
                   "8,1073741890,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    // A dedicated root keeps this block independent of the scene files
    // earlier blocks wrote under `root`.
    const auto sub_root = root / "save-load";
    std::filesystem::create_directories(sub_root);
    const auto journal_path = sub_root / "save_journal.json";
    {
      std::ofstream out(journal_path);
      out << journal.serialize();
    }
    auto opts = headless_options(sub_root);
    opts.frame_limit = 8;
    opts.replay_file = journal_path;
    RuntimeHost host{opts};
    int updates = 0;
    int events_seen = 0;
    bool demo_found = false;
    std::vector<Transform2D> positions;
    host.on_event = [&](const stellar::native_map::InputEvent &) {
      ++events_seen;
    };
    host.on_update = [&](World &world, float) {
      ++updates;
      const auto demo = host.find_entity("demo");
      if (demo) {
        demo_found = true;
        if (const auto *t = world.get<Transform2D>(*demo))
          positions.push_back(*t);
      }
    };
    check(host.run() == 0, "save/load replay exits cleanly");
    check(updates == 8 && demo_found,
          "the demo entity resolves every frame");
    check(events_seen == 2, "on_event receives injected input");
    check(positions.size() == 8, "every frame reports a position");
    if (positions.size() == 8) {
      // F5 at tick 2 saves the state update 2 observed; stepping from
      // that snapshot reproduces update 3's position, so update 7
      // (post-F9) must equal update 3 exactly — while update 6 had
      // already drifted.
      check(positions[5].x != positions[2].x ||
                positions[5].y != positions[2].y,
            "the world drifts between save and restore");
      check(positions[6].x == positions[2].x &&
                positions[6].y == positions[2].y,
            "F9 restores the F5 snapshot exactly");
    }
  }

  // 3D gravity + ground plane: a spawned box falls, lands on ground_y
  // once (on_land touchdown transition), and rests with its AABB bottom
  // on the plane. on_spawn3d fires for the document entity.
  {
    const auto sub = root / "scene3d-land";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[{"name":"ball","mesh":"box","pos":[0,4,0]},
                  {"name":"marker","mesh":"box","pos":[0,0,-5],
                   "gravityScale":0}],
                  "gravity":40.0,"groundY":0.0})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    opts.frame_limit = 90;
    RuntimeHost host{opts};
    int landings = 0;
    EntityId landed{}, ground_arg{42u, 7u};
    bool spawn3d_cb = false;
    bool pick_hit = false, pick_miss = false;
    float rest_y = -999.f;
    host.on_spawn3d = [&](World &, EntityId id, const Scene3dEntity &s) {
      if (s.name == "ball") spawn3d_cb = id != EntityId{};
    };
    host.on_land = [&](EntityId e, EntityId ground) {
      ++landings;
      landed = e;
      ground_arg = ground;
    };
    int pick_updates = 0;
    host.on_update = [&](World &world, float) {
      if (!host.entities3d().empty())
        if (const auto *t =
                world.get<Transform3D>(host.entities3d().front()))
          rest_y = t->y;
      // The marker sits dead-center on the -Z camera axis; the corner
      // of the viewport misses every entity.
      if (++pick_updates == 1 && host.entities3d().size() == 2) {
        const auto hit = host.entity3d_at(320.f, 240.f);
        pick_hit = hit.has_value() && hit->entity == host.entities3d()[1];
        pick_miss = !host.entity3d_at(5.f, 5.f).has_value();
      }
    };
    check(host.run() == 0, "3D land run exits cleanly");
    check(spawn3d_cb, "on_spawn3d fires for the document entity");
    check(pick_hit, "entity3d_at picks the centered marker");
    check(pick_miss, "entity3d_at misses the viewport corner");
    check(landings == 1 && landed == host.entities3d().front(),
          "on_land fires once on touchdown");
    check(ground_arg == EntityId{},
          "the ground plane passes an empty entity");
    check(std::abs(rest_y - 0.5f) < 0.05f,
          "the box rests with its AABB bottom on ground_y");
  }

  // 3D solid collision: a box falling onto a solid entity separates
  // along the MTV, grounds, and fires on_land with the solid as the
  // ground entity — the non-empty counterpart of the ground-plane case.
  {
    const auto sub = root / "scene3d-solid";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[
                   {"name":"crate","mesh":"box","pos":[0,3,0]},
                   {"name":"slab","mesh":"box","pos":[0,0,0],
                    "scale":3.0,"solid":true}],
                  "gravity":40.0,"groundY":-100.0})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    opts.frame_limit = 90;
    RuntimeHost host{opts};
    int landings = 0, contacts = 0;
    EntityId landed{}, ground_arg{};
    float rest_y = -999.f;
    host.on_collision = [&](EntityId, EntityId) { ++contacts; };
    host.on_land = [&](EntityId e, EntityId ground) {
      ++landings;
      landed = e;
      ground_arg = ground;
    };
    host.on_update = [&](World &world, float) {
      if (!host.entities3d().empty())
        if (const auto *t =
                world.get<Transform3D>(host.entities3d().front()))
          rest_y = t->y;
    };
    check(host.run() == 0, "3D solid-land run exits cleanly");
    check(contacts == 1, "on_collision fires once for the solid pair");
    check(landings == 1, "on_land fires once on the solid touchdown");
    check(landed == host.entities3d().front(),
          "on_land reports the falling crate");
    check(ground_arg != EntityId{},
          "on_land passes the solid as the ground entity");
    // A 3x box's top is y=1.5; the unit crate's AABB bottom rests there
    // → center y = 2.0.
    check(std::abs(rest_y - 2.0f) < 0.1f,
          "the crate rests on the solid's top face");
  }

  // 3D enter/exit: two overlapping non-solid boxes fire on_collision on
  // the first overlapping step, then on_collision_exit once the mover's
  // velocity carries it clear — no push-out without a solid.
  {
    const auto sub = root / "scene3d-exit";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[
                   {"name":"anchor","mesh":"box","pos":[0,0,0]},
                   {"name":"drifter","mesh":"box","pos":[0.5,0,0],
                    "vel":[2,0,0]}]})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    opts.frame_limit = 30;
    RuntimeHost host{opts};
    int enters = 0, exits = 0;
    EntityId entered_a{}, exited_a{};
    host.on_collision = [&](EntityId a, EntityId) {
      ++enters;
      entered_a = a;
    };
    host.on_collision_exit = [&](EntityId a, EntityId) {
      ++exits;
      exited_a = a;
    };
    check(host.run() == 0, "3D enter/exit run exits cleanly");
    check(enters == 1, "on_collision fires once while the boxes overlap");
    check(exits == 1, "on_collision_exit fires once they separate");
    check(entered_a == exited_a,
          "the exit pair matches the enter pair");
  }

  // 3D ttl / parent / bounds: "spark" self-destructs when its Lifetime
  // drains, "rider" follows its moving parent at the captured offset,
  // and "bouncer" reflects off the XZ bounds wall.
  {
    const auto sub = root / "scene3d-misc";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[
                   {"name":"carrier","mesh":"box","pos":[0,0,0],
                    "vel":[1,0,0]},
                   {"name":"rider","mesh":"box","pos":[0.5,0,0],
                    "parent":"carrier"},
                   {"name":"spark","mesh":"box","pos":[0,0,5],
                    "ttl":0.05},
                   {"name":"bouncer","mesh":"box","pos":[0,0,2],
                    "vel":[0,0,4]}],
                  "bounds":3.0})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    opts.frame_limit = 30;
    RuntimeHost host{opts};
    check(host.run() == 0, "3D ttl/parent/bounds run exits cleanly");
    float rider_x = -999.f, carrier_x = -999.f, bouncer_z = -999.f;
    bool spark_alive = false;
    for (const auto e : host.entities3d()) {
      const auto *n = host.world().get<EntityName>(e);
      const auto *t = host.world().get<Transform3D>(e);
      if (!n || !t) continue;
      if (n->value == "rider") rider_x = t->x;
      if (n->value == "carrier") carrier_x = t->x;
      if (n->value == "bouncer") bouncer_z = t->z;
      if (n->value == "spark") spark_alive = true;
    }
    check(!spark_alive, "ttl destroys the 3D entity after its lifetime");
    check(std::abs(rider_x - carrier_x - 0.5f) < 0.05f,
          "the 3D child follows its parent at the captured offset");
    check(bouncer_z > 0.0f && bouncer_z < 2.4f,
          "the bouncer reflects off the bounds wall");
    const auto carrier = [&]() -> EntityId {
      for (const auto e : host.entities3d())
        if (const auto *n = host.world().get<EntityName>(e);
            n && n->value == "carrier")
          return e;
      return {};
    }();
    check(host.destroy_entity(carrier),
          "destroy_entity removes a 3D entity");
    check(host.entities3d().size() == 2,
          "the destroyed 3D entity leaves the tracked set");
  }

  // 3D quicksave: F5/F9 in scene3d mode snapshots the 3D tracked set —
  // load_world partitions the restored entities back into entities3d
  // and the mover's Transform3D returns to the tick-2 state.
  {
    ReplayRecorder journal;
    journal.record(2, "input",
                   "8,1073741886,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    journal.record(6, "input",
                   "8,1073741890,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    const auto sub = root / "scene3d-save";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[
                   {"name":"mover","mesh":"box","pos":[0,0,0],
                    "vel":[1,0,0]}]})";
    }
    const auto journal_path = sub / "save_journal.json";
    {
      std::ofstream out(journal_path);
      out << journal.serialize();
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    opts.frame_limit = 8;
    opts.replay_file = journal_path;
    RuntimeHost host{opts};
    std::vector<float> xs;
    host.on_update = [&](World &world, float) {
      if (!host.entities3d().empty())
        if (const auto *t =
                world.get<Transform3D>(host.entities3d().front()))
          xs.push_back(t->x);
    };
    check(host.run() == 0, "3D save/load replay exits cleanly");
    check(xs.size() == 8, "the 3D mover reports every frame");
    check(host.entities3d().size() == 1,
          "load_world repartitions the 3D entity");
    if (xs.size() == 8) {
      check(xs[5] != xs[2],
            "the 3D world drifts between save and restore");
      check(xs[6] == xs[2],
            "F9 restores the 3D snapshot exactly");
    }
  }

  // scene3d record → replay: checkpoint hashing covers the 3D
  // component codecs — a recorded 3D run verifies clean end-to-end.
  {
    const auto sub = root / "scene3d-replay";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[
                   {"name":"mover","mesh":"box","pos":[0,0,0],
                    "vel":[1,0.5,0]}]})";
    }
    const auto rec = sub / "host3d.rec";
    {
      auto opts = headless_options(sub);
      opts.scene3d = true;
      opts.frame_limit = 40;
      opts.record_file = rec;
      RuntimeHost host{opts};
      check(host.run() == 0, "3D recording run exits cleanly");
    }
    {
      auto opts = headless_options(sub);
      opts.scene3d = true;
      opts.frame_limit = 0;
      opts.replay_file = rec;
      opts.replay_exit = true;
      RuntimeHost host{opts};
      check(host.run() == 0,
            "3D replay verifies world checkpoints over the 3D codecs");
    }
  }

  // save_data/load_data round-trip named blobs under saves/data/ —
  // no run() needed, and key validation rejects path escapes.
  {
    RuntimeHost host{headless_options(root)};
    const std::byte payload[] = {std::byte{0xde}, std::byte{0xad},
                                 std::byte{0xbe}, std::byte{0xef}};
    check(host.save_data("quest.flags", payload),
          "save_data accepts a valid key");
    const auto loaded = host.load_data("quest.flags");
    check(loaded.has_value() && loaded->size() == 4 &&
              (*loaded)[0] == 0xde && (*loaded)[3] == 0xef,
          "load_data returns the saved bytes");
    check(!host.save_data("../escape", payload),
          "save_data rejects path escapes");
    check(!host.load_data("never-written").has_value(),
          "load_data reports a missing key");
  }

  // Accessor surface: viewport/world dimensions, the fly camera,
  // time scale, and the player/3D getters on a 2D demo world.
  {
    RuntimeHost host{headless_options(root)};
    int updates = 0;
    host.on_update = [&](World &, float) {
      if (++updates == 1) {
        host.set_time_scale(2.5);
        host.set_camera3d(1.0, 2.0, 3.0, 45.f, -10.f);
      }
    };
    check(host.run() == 0, "accessor run exits cleanly");
    check(host.viewport_width() == 640 && host.viewport_height() == 480,
          "viewport getters report the configured drawable");
    check(host.world_width() == 640.f && host.world_height() == 480.f,
          "world bounds fall back to the viewport");
    check(std::abs(host.time_scale() - 2.5) < 1e-9,
          "set_time_scale round-trips");
    // Non-positive scales clamp to 1.0 on both paths — a --speed 0 or
    // negative would otherwise integrate entities backwards.
    host.set_time_scale(0.0);
    check(host.time_scale() == 1.0, "set_time_scale clamps non-positive");
    auto bad_speed = headless_options(root);
    bad_speed.time_scale = -3.0;
    RuntimeHost clamped{bad_speed};
    check(clamped.run() == 0 && clamped.time_scale() == 1.0,
          "options time_scale clamps at run");
    // Set inside update 1 — after that step's sim_time accumulation —
    // so only steps 2-4 run scaled: dt * (1 + 3 * 2.5).
    check(std::abs(host.sim_time() - (1.0 / 60.0) * 8.5) < 1e-6,
          "time_scale scales sim_time accumulation");
    check(std::abs(host.camera3d_x() - 1.0) < 1e-9 &&
              std::abs(host.camera3d_y() - 2.0) < 1e-9 &&
              std::abs(host.camera3d_z() - 3.0) < 1e-9 &&
              std::abs(host.camera3d_yaw() - 45.f) < 1e-4f &&
              std::abs(host.camera3d_pitch() + 10.f) < 1e-4f,
          "set_camera3d getters round-trip");
    check(!host.player().has_value(),
          "no player entity in the demo world");
    check(!host.scene3d() && host.gravity3d() == 0.f &&
              host.ground_y() == 0.f,
          "3D getters are inert in 2D mode");
    check(!host.has_audio(),
          "has_audio reports no device in a headless run");
  }

  // set_scene3d swaps the spawned 3D set mid-run — the level-switching
  // counterpart of set_scene.
  {
    const auto sub = root / "scene3d-switch";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream a(sub / "editor" / "scene3d.json");
      a << R"({"entities":[{"name":"first","pos":[0,0,0]}]})";
      std::ofstream b(sub / "editor" / "scene3d-b.json");
      b << R"({"entities":[{"name":"second","pos":[9,9,9]}]})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    RuntimeHost host{opts};
    int updates = 0;
    bool had_first = false, had_second = false;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 1) {
        had_first = host.entities3d().size() == 1;
        host.set_scene3d("editor/scene3d-b.json");
      }
      if (updates == 2)
        had_second = host.entities3d().size() == 1;
    };
    check(host.run() == 0, "set_scene3d run exits cleanly");
    check(had_first && had_second,
          "set_scene3d respawns the tracked 3D set");
    const auto box = host.entities3d_in_box(9.f, 9.f, 9.f, 1.f, 1.f, 1.f);
    check(box.size() == 1, "the switched scene's entity is live");

    // Called before run(), set_scene3d selects the initial document and
    // enables the mode — no --scene3d flag needed.
    RuntimeHost initial{headless_options(sub)};
    initial.set_scene3d("editor/scene3d-b.json");
    bool spawned_b = false;
    initial.on_update = [&](World &, float) {
      spawned_b = initial.entities3d_in_box(9.f, 9.f, 9.f, 1.f, 1.f, 1.f)
                      .size() == 1;
    };
    check(initial.run() == 0, "pre-run set_scene3d exits cleanly");
    check(initial.scene3d() && spawned_b,
          "pre-run set_scene3d selects the initial document");
  }

  // Hot reload: the host polls the scene file every 500ms of wall
  // time — overwriting it mid-run respawns the tracked set from the
  // new document (the live-edit path the scene tool relies on).
  {
    const auto sub = root / "hot-reload";
    std::filesystem::create_directories(sub / "editor");
    const auto scene_path = sub / "editor" / "scene.json";
    {
      std::ofstream out(scene_path);
      out << R"({"entities":[{"name":"alpha","x":10,"y":10}]})";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 4;
    RuntimeHost host{opts};
    int updates = 0;
    bool saw_alpha = false, saw_beta = false;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 1) {
        saw_alpha = host.find_entity("alpha").has_value();
        // Cross the 500ms scene-poll boundary before rewriting, so the
        // new file stamp can't share the initial load's tick.
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        std::ofstream out(scene_path);
        out << R"({"entities":[{"name":"beta","x":20,"y":20}]})";
      }
      if (updates >= 2) saw_beta = host.find_entity("beta").has_value();
    };
    check(host.run() == 0, "hot-reload run exits cleanly");
    check(saw_alpha && saw_beta,
          "the poll respawns the scene from the rewritten file");
    check(!host.find_entity("alpha").has_value(),
          "the replaced entity is destroyed on reload");
  }

  // set_scene on a missing or malformed file fails safe — the running
  // scene is kept instead of being torn down into an empty world.
  {
    const auto sub = root / "bad-switch";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[{"name":"kept","x":10,"y":10}]})";
      std::ofstream bad(sub / "editor" / "broken.json");
      bad << "{not json";
      std::ofstream bad3(sub / "editor" / "broken3d.json");
      bad3 << "{not json";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 6;
    RuntimeHost host{opts};
    int updates = 0;
    bool kept_after_missing = false, kept_after_broken = false;
    host.on_update = [&](World &, float) {
      ++updates;
      if (updates == 2) host.set_scene("editor/nonexistent.json");
      if (updates == 3)
        kept_after_missing = host.find_entity("kept").has_value();
      if (updates == 4) {
        host.set_scene("editor/broken.json");
        host.set_scene3d("editor/broken3d.json");
      }
      if (updates == 5)
        kept_after_broken = host.find_entity("kept").has_value() &&
                            host.entities3d().empty();
    };
    check(host.run() == 0, "bad-switch run exits cleanly");
    check(kept_after_missing,
          "a missing scene file keeps the running scene");
    check(kept_after_broken,
          "malformed 2D/3D scene files keep the running scene");
  }

  // Scene-authored animations: a clip's "x" track owns the entity's
  // Transform2D.x while playing, and a named event marker fires exactly
  // once when the playhead crosses it.
  {
    const auto sub = root / "anim";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[{"name":"door","x":10,"y":20,"anim":"slide"}],
                  "animations":[{"id":"slide","loop":"once",
                    "tracks":{"x":[[0,10],[0.2,110]]},
                    "events":[[0.1,"halfway"]]}]})";
    }
    auto opts = headless_options(sub);
    // 12 frames at 60 Hz reaches the clip's 0.2s end; the marker at
    // 0.1s crosses around frame 7.
    opts.frame_limit = 12;
    RuntimeHost host{opts};
    int updates = 0;
    int halfway_events = 0;
    float door_x = -1.f;
    host.on_anim_event = [&](const std::string &event, EntityId) {
      if (event == "halfway") ++halfway_events;
    };
    host.on_update = [&](World &world, float) {
      ++updates;
      const auto door = host.find_entity("door");
      if (door)
        if (const auto *t = world.get<Transform2D>(*door))
          door_x = t->x;
    };
    check(host.run() == 0, "anim run exits cleanly");
    check(halfway_events == 1,
          "on_anim_event fires once per marker crossing");
    check(door_x > 10.f && door_x <= 110.f,
          "the x track owns Transform2D.x while playing");
  }

  // 2D solid platform: a falling entity lands on the solid's top and
  // on_land fires once with the platform as the ground entity.
  {
    const auto sub = root / "solid-land";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"gravity":400.0,
                  "entities":[{"name":"faller","x":50,"y":100,"w":32,"h":32},
                   {"name":"platform","x":0,"y":200,"w":200,"h":32,
                    "solid":true,"gravityScale":0}]})";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 60;
    RuntimeHost host{opts};
    int landings = 0;
    EntityId landed{}, ground_arg{42u, 9u};
    float rest_y = -999.f;
    host.on_land = [&](EntityId e, EntityId ground) {
      ++landings;
      landed = e;
      ground_arg = ground;
    };
    host.on_update = [&](World &world, float) {
      const auto faller = host.find_entity("faller");
      if (faller)
        if (const auto *t = world.get<Transform2D>(*faller))
          rest_y = t->y;
    };
    check(host.run() == 0, "solid-land run exits cleanly");
    check(landings == 1, "on_land fires once on the platform touchdown");
    check(landed == host.find_entity("faller"),
          "on_land reports the falling entity");
    check(ground_arg == host.find_entity("platform"),
          "on_land passes the solid as the ground entity");
    // Rests with its bottom edge on the platform's top (200 - 32).
    check(std::abs(rest_y - 168.f) < 2.f,
          "the faller rests on the platform top");
  }

  // Oneway platforms: a falling entity lands on top, but a riser
  // moving up through the platform's cells passes straight through.
  {
    const auto sub = root / "oneway";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"gravity":400.0,
                  "entities":[
                   {"name":"faller","x":50,"y":100,"w":32,"h":32},
                   {"name":"riser","x":100,"y":300,"w":32,"h":32,
                    "vy":-250,"gravityScale":0},
                   {"name":"ledge","x":0,"y":200,"w":200,"h":16,
                    "oneway":true,"gravityScale":0}]})";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 60;
    RuntimeHost host{opts};
    float faller_y = -999.f, riser_y = 999.f;
    host.on_update = [&](World &world, float) {
      if (const auto f = host.find_entity("faller"))
        if (const auto *t = world.get<Transform2D>(*f))
          faller_y = t->y;
      if (const auto r = host.find_entity("riser"))
        if (const auto *t = world.get<Transform2D>(*r))
          riser_y = t->y;
    };
    check(host.run() == 0, "oneway run exits cleanly");
    check(std::abs(faller_y - 168.f) < 2.f,
          "the faller lands on the oneway platform's top");
    check(riser_y < 100.f,
          "the riser passes through the oneway platform");
  }

  // Injected input drives the "player" entity through the real
  // action-mapper path: a held 'd' (move_right) sets velocity while
  // held, release stops it — the same path live keyboard input takes.
  {
    const auto sub = root / "input-player";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[{"name":"player","x":100,"y":200}]})";
    }
    ReplayRecorder journal;
    // KeyPressed=8 / KeyReleased=9; 'd' = 100.
    journal.record(1, "input", "8,100,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    journal.record(5, "input", "9,100,0,0,0,0,0,0,0,0,0,0,0,0,0,");
    const auto journal_path = sub / "input_journal.json";
    {
      std::ofstream out(journal_path);
      out << journal.serialize();
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 8;
    opts.replay_file = journal_path;
    RuntimeHost host{opts};
    std::vector<float> xs;
    host.on_update = [&](World &world, float) {
      const auto player = host.player();
      if (player)
        if (const auto *t = world.get<Transform2D>(*player))
          xs.push_back(t->x);
    };
    check(host.run() == 0, "player-input replay exits cleanly");
    check(xs.size() == 8, "the player resolves every frame");
    if (xs.size() == 8) {
      check(xs[0] == 100.f, "the player starts at rest");
      check(xs[4] > xs[0], "held move_right advances the player");
      check(xs[7] == xs[5],
            "releasing move_right stops the player");
    }
  }

  // --input-map stacks a project context over the built-in "game" one:
  // an exclusive rebind of move_right to 'e' disables the default 'd'.
  {
    const auto sub = root / "input-map";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[{"name":"player","x":100,"y":200}]})";
      std::ofstream map(sub / "input-map.json");
      map << R"({"contexts":[{"name":"custom","exclusive":true,
                  "actions":[{"name":"move_right","type":"Button",
                    "bindings":[{"kind":"KeyPress","code":101}]}]}]})";
    }
    ReplayRecorder journal;
    journal.record(1, "input", "8,100,0,0,0,0,0,0,0,0,0,0,0,0,0,");  // 'd'
    journal.record(3, "input", "8,101,0,0,0,0,0,0,0,0,0,0,0,0,0,");  // 'e'
    const auto journal_path = sub / "input_journal.json";
    {
      std::ofstream out(journal_path);
      out << journal.serialize();
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 6;
    opts.input_map = "input-map.json";
    opts.replay_file = journal_path;
    RuntimeHost host{opts};
    std::vector<float> xs;
    host.on_update = [&](World &world, float) {
      const auto player = host.player();
      if (player)
        if (const auto *t = world.get<Transform2D>(*player))
          xs.push_back(t->x);
    };
    check(host.run() == 0, "input-map replay exits cleanly");
    check(xs.size() == 6, "the player resolves every frame");
    if (xs.size() == 6) {
      check(xs[2] == 100.f,
            "the exclusive context shadows the default 'd' binding");
      check(xs[5] > 100.f,
            "the project binding 'e' drives move_right");
    }
  }

  // Lifetime ttl self-destructs in sim time, and a name-keyed parent
  // attachment keeps the child at its authored offset as the parent
  // moves.
  {
    const auto sub = root / "ttl-parent";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[
                   {"name":"mover","x":0,"y":100,"vx":100},
                   {"name":"rider","x":50,"y":140,"parent":"mover"},
                   {"name":"spark","x":500,"y":500,"ttl":0.05}]})";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 20;
    RuntimeHost host{opts};
    float offset = 0.f;
    int rider_frames = 0;
    host.on_update = [&](World &world, float) {
      const auto mover = host.find_entity("mover");
      const auto rider = host.find_entity("rider");
      if (mover && rider) {
        const auto *mt = world.get<Transform2D>(*mover);
        const auto *rt = world.get<Transform2D>(*rider);
        if (mt && rt) {
          offset = rt->x - mt->x;
          ++rider_frames;
        }
      }
    };
    check(host.run() == 0, "ttl/parent run exits cleanly");
    check(rider_frames > 0 && std::abs(offset - 50.f) < 1.f,
          "the rider keeps its authored offset as the parent moves");
    check(!host.find_entity("spark").has_value(),
          "the ttl entity self-destructs in sim time");
  }

  // A scene `vfx` field auto-attaches a document-declared emitter on
  // spawn — particles flow with no game code.
  {
    const auto sub = root / "vfx-attach";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[{"name":"torch","x":100,"y":100,
                   "vfx":"embers"}],
                  "emitters":[{"id":"embers","rate":120,
                   "lifetime":0.5}]})";
    }
    RuntimeHost host{headless_options(sub)};
    int updates = 0;
    std::size_t live = 0, particles = 0;
    host.on_update = [&](World &, float) {
      ++updates;
      live = host.vfx().live_instance_count();
      if (updates == 4 && live == 1)
        particles = host.vfx().particles(1).size();
    };
    check(host.run() == 0, "vfx-attach run exits cleanly");
    check(live == 1, "the scene vfx field spawns an attached emitter");
    check(particles > 0, "the attached emitter produces particles");
  }

  // A scene3d `vfx` field attaches the same way — the emitter anchors at
  // the entity's camera-projected screen position and tracks its motion.
  {
    const auto sub = root / "vfx-attach-3d";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene3d.json");
      out << R"({"entities":[{"name":"comet","pos":[0,0,0],
                   "vel":[4,0,0],"vfx":"trail"}],
                  "emitters":[{"id":"trail","rate":240,
                   "lifetime":0.5}]})";
    }
    auto opts = headless_options(sub);
    opts.scene3d = true;
    RuntimeHost host{opts};
    int updates = 0;
    std::size_t live = 0;
    float mean_x = 0.f, max_x = 0.f;
    host.on_update = [&](World &, float) {
      ++updates;
      live = host.vfx().live_instance_count();
      if (updates == 4 && live == 1) {
        float sum = 0.f;
        std::size_t count = 0;
        for (const auto &p : host.vfx().particles(1)) {
          sum += p.position.x;
          max_x = std::max(max_x, p.position.x);
          ++count;
        }
        mean_x = count ? sum / count : 0.f;
      }
    };
    check(host.run() == 0, "vfx-attach-3d run exits cleanly");
    check(live == 1, "the scene3d vfx field spawns an attached emitter");
    // The default camera looks at the origin: the anchor starts at
    // screen center (320,240) and drifts right as the comet moves +X —
    // world-space anchoring would leave particles near x~0 instead.
    check(mean_x > 320.f, "particles anchor at the projected position");
    check(max_x > 325.f, "the anchor tracks the moving entity");
  }

  // Spin integrates into Rotation; wall bounce reflects movers but a
  // "bounce":false entity stops dead at the level edge.
  {
    const auto sub = root / "spin-bounce";
    std::filesystem::create_directories(sub / "editor");
    {
      std::ofstream out(sub / "editor" / "scene.json");
      out << R"({"entities":[
                   {"name":"spinner","x":100,"y":300,"spin":90},
                   {"name":"bouncer","x":600,"y":50,"vx":300},
                   {"name":"stopper","x":600,"y":150,"vx":300,
                    "bounce":false}]})";
    }
    auto opts = headless_options(sub);
    opts.frame_limit = 30;
    RuntimeHost host{opts};
    float spin_deg = 0.f, bouncer_dx = 0.f, stopper_x = -1.f,
          stopper_dx = 999.f;
    host.on_update = [&](World &world, float) {
      if (const auto e = host.find_entity("spinner"))
        if (const auto *r = world.get<Rotation>(*e))
          spin_deg = r->value;
      if (const auto e = host.find_entity("bouncer"))
        if (const auto *v = world.get<Velocity2D>(*e))
          bouncer_dx = v->dx;
      if (const auto e = host.find_entity("stopper")) {
        if (const auto *t = world.get<Transform2D>(*e))
          stopper_x = t->x;
        if (const auto *v = world.get<Velocity2D>(*e))
          stopper_dx = v->dx;
      }
    };
    check(host.run() == 0, "spin/bounce run exits cleanly");
    // on_update runs before each step, so 30 updates observe 29
    // integrations: ~43.5 degrees.
    check(spin_deg > 40.f && spin_deg < 50.f,
          "spin integrates ~90 deg/s into Rotation");
    check(bouncer_dx < 0.f, "the wall bounce reflects the mover");
    check(stopper_x == 608.f && stopper_dx == 0.f,
          "bounce:false stops dead at the level edge");
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

  // run(argc, argv) parses the CLI flags end-to-end: --headless selects
  // the windowless loop, --frames bounds the run, --width/--height size
  // the synthetic drawable, --fixed-hz fixes the step.
  {
    RuntimeHost host{headless_options(root)};
    const char *argv[] = {"game",   "--headless", "--frames", "3",
                          "--width", "320",       "--height", "240",
                          "--fixed-hz", "60"};
    int updates = 0;
    host.on_update = [&](World &, float) { ++updates; };
    check(host.run(9, const_cast<char **>(argv)) == 0,
          "argv-parsed headless run exits cleanly");
    check(updates == 3, "--frames bounds the argv-driven run");
    check(host.viewport_width() == 320 && host.viewport_height() == 240,
          "--width/--height set the synthetic drawable");

    // A trailing --scene3d (last argv slot) must still parse — the
    // value-taking scan stops at i+1 < argc, so valueless flags are
    // scanned separately.
    const auto sub3d = root / "argv-3d";
    std::filesystem::create_directories(sub3d / "editor");
    {
      std::ofstream out(sub3d / "editor" / "scene3d.json");
      out << R"({"entities":[{"name":"crate","pos":[0,0,0]}]})";
    }
    RuntimeHost host3d{headless_options(sub3d)};
    const char *argv3d[] = {"game", "--headless", "--frames", "2",
                            "--scene3d"};
    check(host3d.run(5, const_cast<char **>(argv3d)) == 0,
          "trailing --scene3d run exits cleanly");
    check(host3d.scene3d() && host3d.entities3d().size() == 1,
          "trailing --scene3d flag enables the 3D mode");
  }

  // --snapshot-out writes a world snapshot at teardown that
  // load_world_from_file restores bit-exact.
  {
    const auto sub = root / "snapshot-out";
    std::filesystem::create_directories(sub);
    const auto snap = sub / "end.stellar";
    auto opts = headless_options(sub);
    opts.snapshot_out = snap;
    RuntimeHost host{opts};
    float last_x = 0.f, last_vx = 0.f;
    host.on_update = [&](World &world, float) {
      if (const auto demo = host.find_entity("demo")) {
        if (const auto *t = world.get<Transform2D>(*demo)) last_x = t->x;
        if (const auto *v = world.get<Velocity2D>(*demo)) last_vx = v->dx;
      }
    };
    check(host.run() == 0, "snapshot-out run exits cleanly");
    check(std::filesystem::exists(snap), "the snapshot file is written");
    World restored;
    register_scene_components(restored);
    check(load_world_from_file(restored, snap),
          "the snapshot loads into a fresh world");
    if (const auto demo = find_entity_by_name(restored, "demo")) {
      const auto *t = restored.get<Transform2D>(*demo);
      // on_update observes the pre-integration state; the teardown
      // snapshot holds the position after the final step's integrate.
      check(t && std::abs(t->x - (last_x + last_vx / 60.f)) < 1e-5f,
            "the restored entity holds the final simulated state");
    } else {
      check(false, "the restored world contains the demo entity");
    }
  }

  std::filesystem::remove_all(root, ec);
  if (failures == 0)
    std::cout << "engine runtime host tests passed\n";
  return failures == 0 ? 0 : 1;
}
