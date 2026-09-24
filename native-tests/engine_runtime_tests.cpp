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
    host.on_update = [&](World &, float) {
      ++updates;
      rect_hits = host.entities_in_rect(0.f, 0.f, 2000.f, 2000.f).size();
      // The demo entity starts at (120,160) 96x96 — center near (168,208)
      // before it integrates far (240,150 u/s at 60 Hz).
      radius_hits =
          host.entities_in_radius(175.f, 215.f, 20.f).size();
      far_hits = host.entities_in_radius(-500.f, -500.f, 10.f).size();
    };
    check(host.run() == 0, "headless run exits cleanly");
    check(updates == 4, "headless steps once per frame under --frames");
    check(rect_hits == 1, "entities_in_rect finds the demo entity");
    check(radius_hits == 1, "entities_in_radius finds the demo entity");
    check(far_hits == 0, "entities_in_radius excludes far entities");
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
