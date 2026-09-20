#include <stellar/engine/render_graph.hpp>
#include <stellar/engine/shader_library.hpp>
#include <stellar/engine/texture_streaming.hpp>
#include <stellar/engine/vfx.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
bool order_before(const std::vector<std::string> &order, std::string_view a,
                  std::string_view b) {
  const auto ia = std::find(order.begin(), order.end(), a);
  const auto ib = std::find(order.begin(), order.end(), b);
  return ia != order.end() && ib != order.end() && ia < ib;
}
} // namespace

int main() {
  // --- Render graph ---
  RenderGraph graph;
  const auto scene = graph.add_resource(
      {RenderResourceDesc::Kind::Texture2D, "scene", 1920,
       1080, "rgba16f"});
  const auto depth = graph.add_resource(
      {RenderResourceDesc::Kind::Attachment, "depth", 1920,
       1080, "d32"});
  const auto bloom = graph.add_resource(
      {RenderResourceDesc::Kind::Texture2D, "bloom", 960, 540,
       "rgba16f"});
  const auto post_out = graph.add_resource(
      {RenderResourceDesc::Kind::Texture2D, "post_out", 1920, 1080,
       "rgba16f"});
  const auto backbuffer = graph.add_resource(
      {RenderResourceDesc::Kind::Texture2D, "backbuffer", 0,
       0, "rgba8", false});

  RenderPass stars;
  stars.name = "stars";
  stars.writes = {scene, depth};
  graph.add_pass(stars);
  RenderPass bloom_pass;
  bloom_pass.name = "bloom";
  bloom_pass.reads = {scene};
  bloom_pass.writes = {bloom};
  graph.add_pass(bloom_pass);
  RenderPass post;
  post.name = "post";
  post.reads = {scene, bloom};
  post.writes = {post_out};
  graph.add_pass(post);
  RenderPass ui;
  ui.name = "ui";
  ui.reads = {post_out};
  ui.writes = {backbuffer};
  graph.add_pass(ui);

  std::vector<std::string> order;
  std::vector<RenderGraphDiagnostic> diagnostics;
  check(graph.compile(&order, &diagnostics), "graph compiles");
  check(order_before(order, "stars", "bloom"), "bloom after stars");
  check(order_before(order, "bloom", "post"), "post after bloom");
  check(order_before(order, "post", "ui"), "ui after post");
  check(graph.producers(scene).size() == 1, "single scene producer");
  check(graph.consumers(scene).size() == 2, "scene consumed twice");

  // Double-writer is an error.
  RenderGraph bad;
  const auto res = bad.add_resource(
      {RenderResourceDesc::Kind::Texture2D, "x", 1, 1, "rgba8"});
  RenderPass pa, pb;
  pa.name = "a";
  pa.writes = {res};
  pb.name = "b";
  pb.writes = {res};
  bad.add_pass(pa);
  bad.add_pass(pb);
  check(!bad.compile(nullptr), "two writers to one resource fails");

  // Cycle detection via ordering constraints.
  RenderGraph cyclic;
  RenderPass c1, c2;
  c1.name = "c1";
  c1.ordering_constraints = {"c2"};
  c2.name = "c2";
  c2.ordering_constraints = {"c1"};
  cyclic.add_pass(c1);
  cyclic.add_pass(c2);
  check(!cyclic.compile(nullptr), "ordering cycle rejected");

  // --- Texture streaming ---
  TextureStreamer streamer(1000);
  const auto planet = streamer.register_texture(
      {"planet_earth", {600, 300, 150, 50}}); // 1100 total
  const auto nebula = streamer.register_texture({"nebula", {200, 100}});
  streamer.request(planet, 0, 10.0f); // wants full res, high priority
  streamer.request(nebula, 0, 1.0f);
  auto changes = streamer.advance_frame(1);
  // Budget 1000: planet needs 1100 (over budget!), nebula 300.
  // Neither fits? planet 1100 > 1000 alone -> skipped; nebula 300 fits.
  check(streamer.finest_resident_mip(nebula).has_value(),
        "nebula resident under budget");
  check(!streamer.finest_resident_mip(planet).has_value(),
        "over-budget texture stays unloaded");
  check(streamer.resident_bytes() == 300, "resident bytes tracked");

  // Next frame: request planet at mip 2 (150+50=200) — fits.
  streamer.request(planet, 2, 10.0f);
  streamer.request(nebula, 0, 1.0f);
  changes = streamer.advance_frame(2);
  check(streamer.finest_resident_mip(planet) == 2u,
        "planet resident at mip 2");
  check(streamer.resident_bytes() == 500, "budget: 300 nebula + 200 planet");

  // No request next frame -> nebula evicted.
  streamer.request(planet, 2, 10.0f);
  changes = streamer.advance_frame(3);
  check(!streamer.finest_resident_mip(nebula).has_value(),
        "unrequested texture evicted");

  // Pinned texture survives without requests.
  streamer.set_pinned(nebula, true);
  changes = streamer.advance_frame(4);
  check(streamer.finest_resident_mip(nebula) == 0u,
        "pinned texture stays fully resident");

  // --- Shader library ---
  ShaderLibrary shaders;
  const auto planet_family =
      shaders.add_family({"planet", "shaders/planet.hlsl", 1});
  const auto key1 = shaders.variant_key(planet_family,
                                        {{"ATMOSPHERE", "1"},
                                         {"RINGS", "0"}});
  const auto key2 = shaders.variant_key(planet_family,
                                        {{"RINGS", "0"},
                                         {"ATMOSPHERE", "1"}});
  check(key1 == key2, "variant key canonicalizes define order");
  auto &variant =
      shaders.get_or_create_variant(planet_family,
                                    {{"ATMOSPHERE", "1"}, {"RINGS", "0"}});
  check(!variant.compiled, "new variant starts uncompiled");
  shaders.mark_compiled(key1, 0xDEADBEEF);
  check(shaders.compiled_variant_count() == 1, "compiled count");
  shaders.bump_family_version(planet_family);
  check(shaders.variant_count() == 0,
        "version bump invalidates variants");
  check(shaders.family(planet_family)->version == 2, "version bumped");

  // --- VFX ---
  VfxSystem vfx;
  EmitterDefinition flare;
  flare.id = "stellar_flare";
  flare.sprite = "flare_sprite";
  flare.spawn_rate_per_second = 20.0f;
  flare.particle_lifetime_seconds = 2.0f;
  flare.velocity_min = {0, 0, 0};
  flare.velocity_max = {1, 1, 0};
  flare.max_particles = 64;
  flare.opacity_over_life.add_key(0.0f, 1.0f);
  flare.opacity_over_life.add_key(1.0f, 0.0f);
  vfx.define(flare);

  const auto emitter = vfx.spawn("stellar_flare", {100, 50, 0});
  check(emitter != invalid_vfx_instance, "emitter spawned");
  vfx.advance(0.5); // ~10 particles
  const auto count = vfx.particles(emitter).size();
  check(count >= 9 && count <= 11, "spawn rate over 0.5s");

  // Determinism: a second system with the same spawn order matches.
  VfxSystem vfx2;
  vfx2.define(flare);
  const auto emitter2 = vfx2.spawn("stellar_flare", {100, 50, 0});
  vfx2.advance(0.5);
  const auto a = vfx.particles(emitter);
  const auto b = vfx2.particles(emitter2);
  check(a.size() == b.size(), "deterministic particle count");
  bool identical = a.size() == b.size();
  for (std::size_t i = 0; identical && i < a.size(); ++i)
    identical = a[i].velocity.x == b[i].velocity.x &&
                a[i].position.x == b[i].position.x;
  check(identical, "deterministic particle state");

  // LOD: far distance shrinks the spawn rate.
  EmitterDefinition lod_def = flare;
  lod_def.lod_fade_distance = 100.0f;
  lod_def.lod_min_rate_scale = 0.0f;
  VfxSystem vfx_lod;
  vfx_lod.define(lod_def);
  const auto lod = vfx_lod.spawn("stellar_flare", {0, 0, 0});
  vfx_lod.set_lod_distance(lod, 100.0f); // fully faded
  vfx_lod.advance(0.5);
  check(vfx_lod.particles(lod).empty(), "fully faded LOD spawns nothing");

  // Expiry: particles die; stopped emitter with empty pool is reclaimed.
  vfx.stop(emitter);
  vfx.advance(3.0); // exceeds 2s lifetime
  check(!vfx.alive(emitter) || vfx.particles(emitter).empty(),
        "particles expire");
  check(vfx.live_instance_count() == 0,
        "stopped drained instance reclaimed");

  // Curve-driven opacity.
  const auto visual =
      vfx.visual_for(flare, flare.particle_lifetime_seconds * 0.5f);
  check(std::abs(visual.opacity - 0.5f) < 1e-5,
        "opacity curve midpoint");

  if (failures == 0)
    std::cout << "Render graph, streaming, shader and VFX tests passed\n";
  return failures == 0 ? 0 : 1;
}
