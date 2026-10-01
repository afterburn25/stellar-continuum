#include "native_territory_projection.hpp"
#include "native_territory_overlay.hpp"
#include <stellar/engine/image_clipping.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace stellar::core;
using namespace stellar::native_territory;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

StellarSystem make_system(int id, float x, float y) {
  StellarSystem system;
  system.id = id;
  system.name = "System " + std::to_string(id);
  system.position = {x, y};
  return system;
}

Civilization make_civilization(int id, std::string name, int home) {
  Civilization civilization;
  civilization.id = id;
  civilization.name = std::move(name);
  civilization.home_system_id = home;
  return civilization;
}

Colony make_colony(int id, int civilization, int system) {
  Colony colony;
  colony.id = id;
  colony.civilization_id = civilization;
  colony.system_id = system;
  colony.name = "Colony " + std::to_string(id);
  return colony;
}

// Two known civilizations on a small cluster plus a third civilization and a
// fourth system the observer has never seen.
FreshCampaignState make_world() {
  FreshCampaignState world;
  world.player_civilization_id = 0;
  world.systems = {make_system(0, 0.f, 0.f), make_system(1, 40.f, 0.f),
                   make_system(2, 80.f, 0.f), make_system(3, 400.f, 400.f)};
  world.civilizations = {make_civilization(0, "Terran Accord", 0),
                         make_civilization(1, "Pelagic League", 1),
                         make_civilization(2, "Hidden Assembly", 3)};
  world.colonies = {make_colony(0, 1, 2)};
  world.knowledge = CivilizationKnowledgeState::create_initial(
      world.systems, world.civilizations, 0.f);
  for (const int system : {0, 1, 2}) {
    world.knowledge.reveal_system(0, system);
    world.knowledge.mark_system_fully_surveyed(0, system);
  }
  world.knowledge.reveal_civilization(0, 1);
  return world;
}

TerritorialClaimSnapshot make_claim(std::int64_t id, int claimant, int system,
                                    bool active = true) {
  TerritorialClaimSnapshot claim;
  claim.claim_id = id;
  claim.claimant_civilization_id = claimant;
  claim.system_id = system;
  claim.active = active;
  return claim;
}

const NativeTerritoryRegion *region_of(const NativeTerritoryProjection &view,
                                       int civilization_id) {
  for (const auto &region : view.territories)
    if (region.civilization_id == civilization_id) return &region;
  return nullptr;
}

void anchors_and_visibility() {
  auto world = make_world();
  const auto view =
      build_native_territory_projection(world, 0, {}, 14.f);
  const auto *player = region_of(view, 0);
  const auto *foreign = region_of(view, 1);
  require(player && foreign, "known civilizations did not project regions");
  require(player->civilization_name == "Terran Accord",
          "region did not carry the observer-known civilization name");
  require(foreign->anchors.size() == 2,
          "foreign home plus settlement did not produce two anchors");
  require(region_of(view, 2) == nullptr,
          "an unknown civilization leaked a territory anchor");
  require(view.unexplored_system_ids.contains(3) &&
              !view.unexplored_system_ids.contains(0),
          "unexplored set did not match observer system knowledge");
  require(!player->fill_runs.empty() && !player->contours.empty(),
          "visible territory produced no fill or boundary geometry");
  require(player->contours.front().size() >= 3,
          "territory contour was not a closed loop");
}

void settlement_supersedes_foreign_home() {
  auto world = make_world();
  // Colonizing a foreign home replaces its natal marker with the owner.
  world.colonies.push_back(make_colony(1, 0, 1));
  const auto view =
      build_native_territory_projection(world, 0, {}, 14.f);
  const auto *player = region_of(view, 0);
  const auto *foreign = region_of(view, 1);
  require(player && player->anchors.size() == 2,
          "player settlement over a foreign home did not anchor");
  require(foreign && foreign->anchors.size() == 1,
          "displaced foreign home anchor was not superseded");
}

void claim_outlines_are_observer_safe() {
  auto world = make_world();
  const std::vector<TerritorialClaimSnapshot> claims{
      make_claim(1, 1, 2),           // known civ, surveyed system → drawn
      make_claim(2, 1, 3),           // known civ, unsurveyed system → hidden
      make_claim(3, 2, 2),           // unknown civ → hidden
      make_claim(4, 1, 1, false),    // inactive → hidden
      make_claim(5, 1, 99)};         // missing system → hidden
  const auto view =
      build_native_territory_projection(world, 0, claims, 14.f);
  require(view.claims.size() == 1,
          "claim outlines did not apply the observer-visibility filter");
  require(view.claims.front().civilization_id == 1 &&
              view.claims.front().system_id == 2 &&
              view.claims.front().radius >= 28.f,
          "visible claim lost its claimant, system, or minimum radius");
}

// A deterministic ordinary large-galaxy fixture: every catalog system is
// known, but only a small set of empires has a home and sparse settlements.
// It measures the observer-visible presentation work without copying Core
// state or modelling a pathological empire-per-system campaign.
FreshCampaignState make_benchmark_world(int system_count, int empire_count,
                                        int colony_count) {
  FreshCampaignState world;
  world.player_civilization_id = 0;
  const int side = static_cast<int>(std::ceil(std::sqrt(system_count)));
  world.systems.reserve(static_cast<std::size_t>(system_count));
  world.civilizations.reserve(static_cast<std::size_t>(empire_count));
  for (int id = 0; id < system_count; ++id) {
    const int x = id % side, y = id / side;
    world.systems.push_back(make_system(id, static_cast<float>(x * 19),
                                        static_cast<float>(y * 17)));
  }
  for (int id = 0; id < empire_count; ++id)
    world.civilizations.push_back(
        make_civilization(id, "Empire " + std::to_string(id), id));
  require(colony_count > 0 && colony_count <= system_count - empire_count,
          "benchmark colony count was outside the catalog capacity");
  int colony_id = 0;
  for (int index = 0; index < colony_count; ++index) {
    const int system = empire_count +
                       index * (system_count - empire_count) / colony_count;
    world.colonies.push_back(
        make_colony(colony_id++, system % empire_count, system));
  }
  world.knowledge = CivilizationKnowledgeState::create_initial(
      world.systems, world.civilizations, 0.f);
  for (const auto &system : world.systems) {
    world.knowledge.reveal_system(0, system.id);
    world.knowledge.mark_system_fully_surveyed(0, system.id);
  }
  for (int civilization = 1; civilization < empire_count; ++civilization)
    world.knowledge.reveal_civilization(0, civilization);
  return world;
}

void hidden_settlements_cannot_disclose_foreign_control() {
  auto world = make_world();
  // This colony occupies a known system, but its owner is unknown to the
  // observer. It must not create an anchor, region, label, or claim outline.
  world.colonies.push_back(make_colony(9, 2, 1));
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(9, 2, 1)};
  const auto view = build_native_territory_projection(world, 0, claims, 14.f);
  require(region_of(view, 2) == nullptr,
          "hidden settlement leaked a foreign territory region");
  require(view.claims.empty(),
          "hidden settlement leaked a foreign territorial claim");
  const auto *known = region_of(view, 1);
  require(known && std::ranges::any_of(
                       known->anchors, [](const auto &anchor) {
                         return anchor.kind == NativeTerritoryAnchorKind::home &&
                                anchor.system_id == 1;
                       }),
          "hidden settlement suppressed a known civilization's home anchor");
}

void fog_covers_unexplored_space() {
  auto world = make_world();
  const auto view =
      build_native_territory_projection(world, 0, {}, 14.f);
  const auto &fog = view.fog;
  require(fog.width > 0 && fog.height > 0 &&
              fog.alpha.size() ==
                  static_cast<std::size_t>(fog.width) * fog.height,
          "fog mask dimensions did not match its alpha payload");
  const auto alpha_at = [&](int x, int y) {
    return fog.alpha[static_cast<std::size_t>(y) * fog.width + x];
  };
  // Cell containing system 3 (far corner) should be fully fogged near it,
  // the surveyed cluster should be clear, and the border row stays zero.
  bool any_opaque = false, any_clear = false;
  for (int y = 0; y < fog.height; ++y)
    for (int x = 0; x < fog.width; ++x) {
      if (alpha_at(x, y) > 200) any_opaque = true;
      if (alpha_at(x, y) == 0) any_clear = true;
    }
  require(any_opaque && any_clear,
          "fog mask did not separate explored from unexplored space");
  for (int index = 0; index < fog.width; ++index)
    require(alpha_at(index, 0) == 0 && alpha_at(index, fog.height - 1) == 0,
            "fog mask padding row was not transparent");
}

void fingerprint_tracks_inputs() {
  auto world = make_world();
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(1, 1, 2)};
  const auto base = native_territory_fingerprint(world, 0, claims, 14.f);
  require(native_territory_fingerprint(world, 0, claims, 14.f) == base,
          "territory fingerprint was not stable for identical inputs");
  world.colonies.push_back(make_colony(7, 0, 0));
  require(native_territory_fingerprint(world, 0, claims, 14.f) != base,
          "new settlement did not change the territory fingerprint");
  auto fewer = make_world();
  const auto inactive = std::vector<TerritorialClaimSnapshot>{
      make_claim(1, 1, 2, false)};
  require(native_territory_fingerprint(fewer, 0, inactive, 14.f) !=
              native_territory_fingerprint(fewer, 0, claims, 14.f),
          "claim activation did not change the territory fingerprint");
  require(native_territory_fingerprint(fewer, 0, claims, 7.f) !=
              native_territory_fingerprint(fewer, 0, claims, 14.f),
          "coordinate scale did not change the territory fingerprint");
  const auto before_rename = native_territory_fingerprint(fewer, 0, claims, 14.f);
  fewer.civilizations[1].name = "Renamed Pelagic League";
  require(native_territory_fingerprint(fewer, 0, claims, 14.f) != before_rename,
          "rendered civilization rename did not invalidate the projection");
}

void detail_ramp_and_palette() {
  // Overview blend falls as zoom approaches the regional band.
  const float fitted = .2f;
  require(native_territory_overview_blend(.2f, fitted) >
              native_territory_overview_blend(.6f, fitted),
          "overview blend did not fade with zoom");
  require(native_territory_detail(1.f) < native_territory_detail(0.f),
          "territory detail did not brighten into the regional view");
  const auto player = native_territory_color(0, 0);
  require(player.r == 0x58 && player.g == 0xcf && player.b == 0xfb,
          "player territory color drifted from the palette");
  const auto foreign = native_territory_color(4, 0);
  require(foreign.r == 0x64 && foreign.g == 0xd6 && foreign.b == 0xa5,
          "civilization palette slot drifted from the reference");
}

void overlay_cache_is_bounded_complete_and_camera_independent() {
  auto world = make_world();
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(1, 1, 2)};
  NativeTerritoryOverlay overlay;
  overlay.update(world, 0, claims);
  require(overlay.valid() && overlay.projection(),
          "overlay did not retain a completed projection");
  const auto initial_bytes = overlay.cached_image_bytes();
  require(initial_bytes > 0 &&
              initial_bytes <= NativeTerritoryOverlay::maximum_cached_image_bytes,
          "overlay retained images outside its fixed cache budget");

  stellar::native_map::Camera camera;
  camera.pixels_per_world = 4.;
  stellar::native_map::DrawList draw;
  const auto stats = overlay.append(draw, camera, 1280, 720, .2f);
  require(stats.fog_images == 1 && stats.fill_images == 1,
          "overlay did not emit its fog and complete fill atlas");
  require(stats.contour_segments > 0 && stats.claim_segments > 0,
          "overlay did not emit visible contour and claim geometry");

  const stellar::native_map::Image *fill = nullptr;
  for (const auto &command : draw.world) {
    const auto *image = std::get_if<stellar::native_map::Image>(&command);
    if (image && image->resource && image->tint.r == 255 &&
        image->tint.g == 255 && image->tint.b == 255) {
      fill = image;
      break;
    }
  }
  require(fill, "overlay did not expose a color-baked fill atlas");
  const auto player = native_territory_color(0, 0);
  const auto foreign = native_territory_color(1, 0);
  bool has_player = false, has_foreign = false;
  const auto &pixels = fill->resource->pixels();
  for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
    has_player = has_player ||
                 (pixels[index] == player.r && pixels[index + 1] == player.g &&
                  pixels[index + 2] == player.b && pixels[index + 3] == 255);
    has_foreign = has_foreign ||
                  (pixels[index] == foreign.r && pixels[index + 1] == foreign.g &&
                   pixels[index + 2] == foreign.b && pixels[index + 3] == 255);
  }
  require(has_player && has_foreign,
          "a projected territory was omitted from the shared fill atlas");

  camera.center = {160., -90.};
  camera.pixels_per_world = .6;
  stellar::native_map::DrawList resized_draw;
  const auto resized_stats = overlay.append(resized_draw, camera, 3840, 2160, .2f);
  require(resized_stats.fog_images == 1 && resized_stats.fill_images == 1 &&
              overlay.cached_image_bytes() == initial_bytes,
          "panning or resizing rebuilt the retained territory images");
  stellar::native_map::DrawList hidden;
  const auto hidden_stats=overlay.append(hidden,camera,1280,720,.2f,
      {.draw_labels=true,.emphasize_overview=false,.draw_ownership=false});
  require(hidden_stats.fog_images==1&&hidden_stats.fill_images==0&&
          hidden_stats.contour_segments==0&&hidden_stats.claim_segments==0,
          "Territory layer toggle must keep the fog shroud while hiding ownership marks");
  for(const auto& command:hidden.world)
    require(!std::holds_alternative<stellar::native_map::Text>(command),
            "Hidden territory layer still named its regions");

  camera.center={0,0};camera.pixels_per_world=1000.;
  stellar::native_map::DrawList magnified;
  const auto magnified_stats=overlay.append(magnified,camera,1280,720,.2f);
  require(magnified_stats.fog_images==1&&magnified_stats.fill_images==1,
          "Magnified territory disappeared instead of cropping.");
  for(const auto& command:magnified.world)if(const auto* image=std::get_if<stellar::native_map::Image>(&command))
    require(image->source&&image->destination.x>=0&&image->destination.y>=0&&
        image->destination.width<=1280&&image->destination.height<=720,
        "Magnified atlas exceeded drawable bounds.");
}

void projected_image_cropping_preserves_uvs(){
  using namespace stellar::native_map;
  const auto art=RgbaImage::create(100,100,std::vector<std::uint8_t>(40000,255));
  Image original{art,{-100000,-200000,400000,400000},UiRect{20,10,40,80}};
  auto clipped=clip_image_to_viewport(original,{0,0,1000,800});
  require(clipped&&clipped->source&&clipped->destination.width==1000&&
      clipped->destination.height==800&&clipped->source->x==30&&clipped->source->y==50&&
      std::abs(clipped->source->width-.1f)<.00001f&&std::abs(clipped->source->height-.16f)<.00001f,
      "Large image crop changed source coordinates or scale.");
  original.destination={2000,2000,100,100};
  require(!clip_image_to_viewport(original,{0,0,1000,800}),"Offscreen image was submitted.");
  original.rotation_degrees=30;
  bool rejected=false;try{(void)clip_image_to_viewport(original,{0,0,1000,800});}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"Axis-aligned crop silently distorted a rotated image.");
}

void async_request_coalesces_and_clear_invalidates() {
  auto world = make_world();
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(1, 1, 2)};
  NativeTerritoryOverlay overlay;
  overlay.request_update(world, 0, claims, 11);
  require(overlay.pending() && !overlay.valid(),
          "async request synchronously published territory work");
  auto latest = world;
  latest.colonies.push_back(make_colony(22, 0, 2));
  overlay.request_update(latest, 0, claims, 12);
  while (overlay.pending()) {
    overlay.poll();
    std::this_thread::yield();
  }
  overlay.poll();
  require(overlay.valid() && !overlay.pending(),
          "latest async request was not collected");
  const auto *player = region_of(*overlay.projection(), 0);
  require(player && std::ranges::any_of(player->anchors, [](const auto &anchor) {
            return anchor.kind == NativeTerritoryAnchorKind::settlement &&
                   anchor.system_id == 2;
          }),
          "coalesced request published stale territory geometry");

  overlay.request_update(latest, 0, claims, 13);
  overlay.clear();
  require(!overlay.valid(), "clear retained an obsolete territory projection");
  while (overlay.pending()) {
    overlay.poll();
    std::this_thread::yield();
  }
  overlay.poll();
  require(!overlay.valid() && !overlay.pending(),
          "clear allowed an in-flight result to republish");

  NativeTerritoryOverlay snapshot_overlay;
  auto captured = make_world();
  snapshot_overlay.request_update(captured, 0, claims, 14);
  captured.colonies.push_back(make_colony(23, 0, 2));
  while (snapshot_overlay.pending()) {
    snapshot_overlay.poll();
    std::this_thread::yield();
  }
  const auto *captured_player = region_of(*snapshot_overlay.projection(), 0);
  require(captured_player && !std::ranges::any_of(
                                 captured_player->anchors, [](const auto &anchor) {
                                   return anchor.kind == NativeTerritoryAnchorKind::settlement &&
                                          anchor.system_id == 2;
                                 }),
          "worker observed Core state changed after DTO capture");
}

void async_failures_are_terminal_once_and_stale_failures_are_discarded() {
  const auto world = make_world();
  NativeTerritoryOverlay overlay;
  int attempts = 0;
  overlay.set_preparation_hook_for_tests([&] {
    ++attempts;
    throw std::runtime_error("deterministic preparation failure");
  });
  overlay.request_update(world, 0, {}, 20);
  overlay.request_update(world, 0, {}, 20);
  int reported = 0;
  while (overlay.pending()) {
    try {
      overlay.poll();
    } catch (const std::runtime_error &) {
      ++reported;
    }
    std::this_thread::yield();
  }
  require(reported == 1 && attempts == 1,
          "current worker failure was not surfaced exactly once");
  overlay.request_update(world, 0, {}, 20);
  require(!overlay.pending() && attempts == 1,
          "failed request was automatically retried");

  NativeTerritoryOverlay stale_overlay;
  std::atomic<bool> entered = false, release = false;
  stale_overlay.set_preparation_hook_for_tests([&] {
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
    throw std::runtime_error("stale preparation failure");
  });
  stale_overlay.request_update(world, 0, {}, 21);
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
  stale_overlay.clear();
  release.store(true, std::memory_order_release);
  while (stale_overlay.pending()) {
    stale_overlay.poll();
    std::this_thread::yield();
  }
  stale_overlay.poll();
  require(!stale_overlay.valid(),
          "stale worker failure or result republished after clear");
}

void async_a_b_a_reuses_the_active_request() {
  auto first = make_world();
  auto second = first;
  second.colonies.push_back(make_colony(28, 0, 2));
  NativeTerritoryOverlay overlay;
  std::atomic<bool> entered = false, release = false;
  std::atomic<int> preparations = 0;
  overlay.set_preparation_hook_for_tests([&] {
    preparations.fetch_add(1, std::memory_order_relaxed);
    entered.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
  });
  overlay.request_update(first, 0, {}, 30);
  while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
  overlay.request_update(second, 0, {}, 31);
  overlay.request_update(first, 0, {}, 30);
  release.store(true, std::memory_order_release);
  while (overlay.pending()) {
    overlay.poll();
    std::this_thread::yield();
  }
  const auto *player = region_of(*overlay.projection(), 0);
  require(preparations.load(std::memory_order_relaxed) == 1 && player &&
              !std::ranges::any_of(player->anchors, [](const auto &anchor) {
                return anchor.kind == NativeTerritoryAnchorKind::settlement &&
                       anchor.system_id == 2;
              }),
          "A-to-B-to-A submitted duplicate work or published B");
}

void benchmark_update_case(int system_count, int empire_count,
                           int colony_count) {
  const auto world =
      make_benchmark_world(system_count, empire_count, colony_count);
  NativeTerritoryOverlay overlay;
  const auto cold_start = std::chrono::steady_clock::now();
  overlay.update(world, 0, {});
  const auto cold_end = std::chrono::steady_clock::now();
  require(overlay.valid(), "cold benchmark update did not retain a projection");

  constexpr int unchanged_iterations = 20;
  const auto unchanged_start = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < unchanged_iterations; ++iteration)
    overlay.update(world, 0, {});
  const auto unchanged_end = std::chrono::steady_clock::now();
  const auto cold_ms = std::chrono::duration<double, std::milli>(
                           cold_end - cold_start)
                           .count();
  const auto unchanged_ms = std::chrono::duration<double, std::milli>(
                                unchanged_end - unchanged_start)
                                .count() /
                            unchanged_iterations;
  std::cout << "territory_benchmark systems=" << system_count
            << " empires=" << empire_count << " colony_anchors="
            << colony_count << " cold_ms=" << cold_ms
            << " unchanged_ms=" << unchanged_ms
            << " cache_bytes=" << overlay.cached_image_bytes()
            << " grid_cells=" << overlay.projection()->grid_cell_count << '\n';
}

void benchmark_async_update_case(int system_count, int empire_count,
                                 int colony_count) {
  const auto world =
      make_benchmark_world(system_count, empire_count, colony_count);
  NativeTerritoryOverlay overlay;
  const auto request_start = std::chrono::steady_clock::now();
  overlay.request_update(world, 0, {}, 1);
  const auto request_end = std::chrono::steady_clock::now();
  const auto unchanged_start = std::chrono::steady_clock::now();
  overlay.request_update(world, 0, {}, 1);
  const auto unchanged_end = std::chrono::steady_clock::now();
  while (overlay.pending()) {
    overlay.poll();
    std::this_thread::yield();
  }
  overlay.poll();
  const auto completion_end = std::chrono::steady_clock::now();
  const auto request_ms = std::chrono::duration<double, std::milli>(
                              request_end - request_start)
                              .count();
  const auto unchanged_ms = std::chrono::duration<double, std::milli>(
                                unchanged_end - unchanged_start)
                                .count();
  const auto worker_completion_ms = std::chrono::duration<double, std::milli>(
                                        completion_end - request_end)
                                        .count();
  std::cout << "territory_async_benchmark systems=" << system_count
            << " empires=" << empire_count << " colony_anchors="
            << colony_count << " request_ms=" << request_ms
            << " unchanged_request_ms=" << unchanged_ms
            << " worker_completion_ms=" << worker_completion_ms
            << " cache_bytes=" << overlay.cached_image_bytes() << '\n';
}

void run_update_benchmarks() {
  benchmark_update_case(500, 3, 6);
  benchmark_update_case(500, 3, 100);
  benchmark_update_case(2500, 6, 30);
  benchmark_update_case(2500, 6, 500);
  benchmark_update_case(10000, 6, 9994);
}

void async_source_key_tracks_visible_changes() {
  auto world = make_world();
  NativeTerritoryOverlay overlay;
  std::atomic<int> preparations = 0;
  overlay.set_preparation_hook_for_tests([&] { ++preparations; });
  const auto finish = [&] {
    while (overlay.pending()) { overlay.poll(); std::this_thread::yield(); }
  };
  overlay.request_update(world, 0, {}, 1);
  finish();
  for (int i = 0; i < 20; ++i) overlay.request_update(world, 0, {}, 1);
  require(!overlay.pending() && preparations == 1,
          "Unchanged source key rebuilt territory.");
  world.civilizations[1].name = "Updated visible empire";
  world.systems[1].position.x += 1.f;
  world.knowledge.reveal_system(0, 3);
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(1, 0, 3)};
  overlay.request_update(world, 0, claims, 1);
  finish();
  const auto &view = *overlay.projection();
  const auto *region = region_of(view, 1);
  require(preparations == 2 && region && region->civilization_name == world.civilizations[1].name &&
              !view.unexplored_system_ids.contains(3) && view.claims.size() == 1 &&
              region->anchors.front().position.x == world.systems[1].position.x * 14.f,
          "Source-key reuse hid a rename, coordinate, discovery or claim change.");
}

void fully_settled_large_galaxy_has_no_visibility_cutoff() {
  auto world = make_benchmark_world(10000, 6, 9994);
  std::vector<TerritorialClaimSnapshot> claims;
  for (int system = 0; system < 10000; ++system) {
    claims.push_back(make_claim(system * 2, system % 6, system));
    claims.push_back(make_claim(system * 2 + 1, (system + 1) % 6, system));
  }
  const auto input = capture_native_territory_input(world, 0, claims, 14.f);
  std::size_t captured_anchors = 0;
  for (const auto &region : input.regions) captured_anchors += region.anchors.size();
  require(captured_anchors == 10000 && input.claims.size() == 20000,
          "Large settled galaxy lost visible anchors or competing claims during capture.");
  NativeTerritoryOverlay overlay;
  overlay.request_update(world, 0, claims, 17);
  while (overlay.pending()) {
    overlay.poll();
    std::this_thread::yield();
  }
  require(overlay.valid() && overlay.projection(), "Large territory worker failed to publish.");
  const auto &view = *overlay.projection();
  std::size_t anchors = 0;
  for (const auto &region : view.territories) {
    anchors += region.anchors.size();
    require(!region.fill_runs.empty() || !region.fill_polygons.empty(),
            "Large galaxy owner lost its territory fill.");
    require(!region.contours.empty(), "Large galaxy owner lost its border.");
  }
  require(anchors == 10000 && view.territories.size() == 6 && view.claims.size() == 20000,
          "Large territory projection dropped an owner, anchor, or claim.");
  require(view.unexplored_system_ids.empty() && view.grid_cell_count <= 160 * 160,
          "Large territory projection changed known space or unbounded its grid.");
  require(overlay.cached_image_bytes() <= NativeTerritoryOverlay::maximum_cached_image_bytes,
          "Large galaxy territory exceeded its image cache budget.");
}

void malformed_coordinates_fail_before_grid_construction() {
  for (const float coordinate : {std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(), 1.e8f}) {
    auto input = capture_native_territory_input(make_world(), 0, {}, 14.f);
    input.systems.front().position.x = coordinate;
    bool rejected = false;
    try { (void)build_native_territory_projection(input); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "Malformed territory coordinates reached grid construction.");
  }
}

// Bitwise projection fixtures recorded before spatial acceleration. Cover all
// fill/contour vertices, fog texels, labels, anchors, claims and unknown systems.
std::uint64_t projection_digest(const NativeTerritoryProjection &p) {
  std::uint64_t hash = 14695981039346656037ull;
  const auto mix = [&](std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      hash ^= (value >> (i * 8)) & 255;
      hash *= 1099511628211ull;
    }
  };
  const auto point = [&](stellar::native_map::Point v) {
    mix(std::bit_cast<std::uint32_t>(v.x));
    mix(std::bit_cast<std::uint32_t>(v.y));
  };
  mix(p.territories.size());
  for (const auto &r : p.territories) {
    mix(r.civilization_id);
    mix(r.civilization_name.size());
    for (auto c : r.civilization_name) mix(static_cast<unsigned char>(c));
    mix(r.anchors.size());
    for (const auto &a : r.anchors) {
      mix(a.civilization_id); mix(a.system_id);
      point(a.position); mix(static_cast<int>(a.kind));
    }
    point(r.label_position);
    mix(r.fill_runs.size());
    for (const auto &f : r.fill_runs) { point(f.position); point(f.size); }
    for (const auto *lines : {&r.fill_polygons, &r.contours}) {
      mix(lines->size());
      for (const auto &line : *lines) {
        mix(line.size());
        for (auto v : line) point(v);
      }
    }
  }
  mix(p.claims.size());
  for (const auto &c : p.claims) {
    mix(c.civilization_id); mix(c.system_id); point(c.position);
    mix(std::bit_cast<std::uint32_t>(c.radius));
  }
  point(p.fog.position); point(p.fog.size); mix(p.fog.width); mix(p.fog.height);
  for (auto a : p.fog.alpha) mix(a);
  std::vector<int> unknown(p.unexplored_system_ids.begin(), p.unexplored_system_ids.end());
  std::ranges::sort(unknown);
  for (auto id : unknown) mix(id);
  mix(p.unowned_cell_count); mix(p.grid_cell_count);
  return hash;
}

void projection_compatibility() {
  const std::vector<TerritorialClaimSnapshot> claims{make_claim(1, 1, 2)};
  const std::array worlds{make_world(), make_benchmark_world(500, 3, 100),
                          make_benchmark_world(2500, 6, 500)};
  constexpr std::array<std::uint64_t, 3> expected{
      4113548603923953201ull, 6287520029111258996ull, 11936461440158785964ull};
  for (std::size_t i = 0; i < worlds.size(); ++i) {
    auto input = capture_native_territory_input(worlds[i], 0, claims, 14.f);
    const auto digest = projection_digest(build_native_territory_projection(input));
    require(digest == expected[i], "Spatial acceleration changed existing territory geometry or fog");
    require(projection_digest(build_native_territory_projection(worlds[i], 0, claims, 14.f)) == digest,
            "Direct and detached territory paths disagree");
  }
}

void run_async_update_benchmarks() {
  benchmark_async_update_case(500, 3, 6);
  benchmark_async_update_case(500, 3, 100);
  benchmark_async_update_case(2500, 6, 30);
  benchmark_async_update_case(2500, 6, 500);
  benchmark_async_update_case(10000, 6, 9994);
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
      run_update_benchmarks();
      return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark-async") {
      run_async_update_benchmarks();
      return 0;
    }
    projection_compatibility();
    fully_settled_large_galaxy_has_no_visibility_cutoff();
    malformed_coordinates_fail_before_grid_construction();
    anchors_and_visibility();
    settlement_supersedes_foreign_home();
    claim_outlines_are_observer_safe();
    hidden_settlements_cannot_disclose_foreign_control();
    fog_covers_unexplored_space();
    fingerprint_tracks_inputs();
    detail_ramp_and_palette();
    overlay_cache_is_bounded_complete_and_camera_independent();
    projected_image_cropping_preserves_uvs();
    async_request_coalesces_and_clear_invalidates();
    async_source_key_tracks_visible_changes();
    async_failures_are_terminal_once_and_stale_failures_are_discarded();
    async_a_b_a_reuses_the_active_request();
    std::cout << "native_territory_projection tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native_territory_projection tests failed: " << error.what()
              << "\n";
    return 1;
  }
}
