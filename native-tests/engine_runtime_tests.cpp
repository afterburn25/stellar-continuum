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

  std::filesystem::remove_all(root, ec);
  if (failures == 0)
    std::cout << "engine runtime host tests passed\n";
  return failures == 0 ? 0 : 1;
}
