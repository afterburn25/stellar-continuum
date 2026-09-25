// VfxSystem: deterministic particle emitters — spawn/lifecycle, per-
// emitter and global budgets, LOD rate scaling, lifetime expiry,
// presentation curves and replay-stable randomness.

#include <stellar/engine/vfx.hpp>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *label) {
  if (!condition) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}

stellar::engine::EmitterDefinition emitter(std::string id, float rate,
                                           float lifetime = 5.f) {
  stellar::engine::EmitterDefinition def;
  def.id = std::move(id);
  def.spawn_rate_per_second = rate;
  def.particle_lifetime_seconds = lifetime;
  def.velocity_min = {0.f, 1.f, 0.f};
  def.velocity_max = {0.f, 1.f, 0.f};
  return def;
}

} // namespace

int main() {
  using namespace stellar::engine;

  {
    VfxSystem vfx;
    check(vfx.spawn("missing", {}) == invalid_vfx_instance,
          "spawning an undefined emitter fails");
    check(vfx.definition("missing") == nullptr,
          "definition() reports unknown ids");

    vfx.define(emitter("steady", 60.f));
    const auto id = vfx.spawn("steady", {1.f, 2.f, 3.f});
    check(id != invalid_vfx_instance && vfx.alive(id),
          "spawn returns a live instance");
    vfx.advance(1.0);
    check(vfx.particles(id).size() == 60,
          "one second at 60/s emits 60 particles");
    const auto &first = vfx.particles(id).front();
    check(first.position.x == 1.f && first.position.y == 2.f,
          "particles spawn at the emitter anchor");

    // Stop drains live particles; once the pool empties the instance is
    // gone entirely.
    vfx.stop(id);
    check(!vfx.alive(id), "stop marks the instance inactive");
    vfx.advance(10.0);
    check(vfx.stats().live_instances == 0,
          "a stopped emitter is erased once drained");
    check(vfx.particles(id).empty(), "a drained instance has no pool");
  }

  {
    // Lifetime expiry: a short-lived particle dies on schedule even
    // while the emitter keeps running.
    VfxSystem vfx;
    vfx.define(emitter("brief", 0.f, 0.5f));
    const auto id = vfx.spawn("brief", {});
    vfx.advance(0.1);
    check(vfx.particles(id).empty(), "a zero-rate emitter stays empty");

    VfxSystem vfx2;
    auto def = emitter("burst", 1000.f, 0.5f);
    vfx2.define(def);
    const auto b = vfx2.spawn("burst", {});
    vfx2.advance(0.1);
    const auto spawned = vfx2.particles(b).size();
    check(spawned == 100, "a 0.1s step at 1000/s emits 100 particles");
    vfx2.stop(b);
    vfx2.advance(0.6);
    check(vfx2.particles(b).empty() && !vfx2.alive(b),
          "particles expire at their lifetime");
  }

  {
    // Per-emitter cap and global budget: the pool never exceeds
    // max_particles, and the global budget tapers spawn demand.
    VfxSystem vfx;
    auto def = emitter("capped", 10000.f, 60.f);
    def.max_particles = 8;
    vfx.define(def);
    const auto id = vfx.spawn("capped", {});
    vfx.advance(1.0);
    check(vfx.particles(id).size() == 8,
          "max_particles caps the emitter pool");

    VfxSystem tight{10};
    tight.define(emitter("a", 500.f, 60.f));
    tight.define(emitter("b", 500.f, 60.f));
    tight.spawn("a", {});
    tight.spawn("b", {});
    tight.advance(1.0);
    const auto stats = tight.stats();
    check(stats.live_particles <= 10,
          "the global budget bounds total live particles");
    check(stats.budget_scale < 1.0f,
          "the budget tapers spawn demand via budget_scale");
  }

  {
    // LOD: at/past the fade distance the emitter runs at min rate;
    // halfway it runs at the interpolated scale.
    VfxSystem vfx;
    auto def = emitter("distant", 100.f, 60.f);
    def.lod_fade_distance = 10.f;
    def.lod_min_rate_scale = 0.f;
    vfx.define(def);
    const auto far = vfx.spawn("distant", {});
    vfx.set_lod_distance(far, 20.f);
    vfx.advance(1.0);
    check(vfx.particles(far).empty(),
          "an emitter past the fade distance emits nothing");

    VfxSystem half;
    half.define(def);
    const auto mid = half.spawn("distant", {});
    half.set_lod_distance(mid, 5.f);
    half.advance(1.0);
    check(half.particles(mid).size() == 50,
          "half the fade distance runs the emitter at half rate");
  }

  {
    // Determinism: identical definition/spawn/advance sequences produce
    // identical particle streams.
    auto run = [] {
      VfxSystem vfx;
      auto def = emitter("rand", 200.f, 5.f);
      def.velocity_min = {-1.f, -1.f, -1.f};
      def.velocity_max = {1.f, 2.f, 1.f};
      def.spread_radians = 0.5f;
      vfx.define(def);
      const auto id = vfx.spawn("rand", {});
      vfx.advance(0.25);
      return vfx.particles(id).front().velocity;
    };
    const auto a = run(), b = run();
    check(a.x == b.x && a.y == b.y && a.z == b.z,
          "identical runs produce identical particles");
  }

  {
    // visual_for evaluates the presentation curves at normalized age.
    VfxSystem vfx;
    auto def = emitter("fade", 0.f, 2.f);
    def.opacity_over_life.add_key(0.f, 1.f);
    def.opacity_over_life.add_key(1.f, 0.f);
    def.scale_over_life.add_key(0.f, 0.5f);
    def.scale_over_life.add_key(1.f, 2.f);
    const auto mid = vfx.visual_for(def, 1.0f);
    check(std::abs(mid.opacity - 0.5f) < 1e-4f &&
              std::abs(mid.scale - 1.25f) < 1e-4f,
          "curves evaluate at mid-life");
    const auto end = vfx.visual_for(def, 99.f);
    check(end.opacity == 0.f && end.scale == 2.f,
          "curves clamp past their last key");
  }

  if (failures == 0) std::cout << "vfx tests passed\n";
  return failures == 0 ? 0 : 1;
}
