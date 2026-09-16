#include "native_territory_projection.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

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

} // namespace

int main() {
  try {
    anchors_and_visibility();
    settlement_supersedes_foreign_home();
    claim_outlines_are_observer_safe();
    fog_covers_unexplored_space();
    fingerprint_tracks_inputs();
    detail_ramp_and_palette();
    std::cout << "native_territory_projection tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native_territory_projection tests failed: " << error.what()
              << "\n";
    return 1;
  }
}
