#include "native_battle_art.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

core::MassiveObservedVessel vessel(std::int64_t id,
                                   std::string design = "patrol_corvette") {
  core::MassiveObservedVessel result;
  result.vessel_id = id;
  result.design_id = std::move(design);
  return result;
}

core::MassiveObservedFormation formation(std::int64_t id, int civilization,
                                         core::MassivePoint position = {}) {
  core::MassiveObservedFormation result;
  result.formation_id = id;
  result.civilization_id = civilization;
  result.position = position;
  result.is_exact = true;
  return result;
}

native_fleet::NativeOwnFleet fleet(int id,
                                   std::string design = "patrol_corvette") {
  native_fleet::NativeOwnFleet result;
  result.id = id;
  result.role = core::FleetRole::Military;
  result.design_id = std::move(design);
  return result;
}

struct Fixture {
  core::MassiveCombatSnapshot snapshot;
  core::CampaignMassiveEncounter encounter;
  native_fleet::NativeFleetMapView fleets;

  Fixture() {
    snapshot.battle_id = {1, 3, 5, 7};
    encounter.battle.battle_id = snapshot.battle_id;
    fleets.player_civilization_id = 4;
    auto own = formation(10, 4, {12, -5});
    own.velocity = {3, 4};
    own.heading_radians = -.75F;
    own.important_vessels.push_back(vessel(99, "science_cutter"));
    own.important_vessels.push_back(
        vessel(core::campaign_vessel_id_for_fleet(0)));
    own.important_vessels.push_back(
        vessel(core::campaign_vessel_id_for_fleet(7)));
    snapshot.formations.push_back(std::move(own));
    encounter.vessels = {{0, 10}, {7, 10}};
    fleets.own_fleets = {fleet(0), fleet(7)};
  }
};

void binding_contract() {
  Fixture fixture;
  const auto bound = native_battle_art::bind_owned_battle_art(
      fixture.snapshot, fixture.encounter, 4, fixture.fleets);
  require(bound.size() == 2, "exact owned vessels did not bind");
  const auto zero = std::ranges::find(
      bound, core::campaign_zero_fleet_vessel_id,
      &native_battle_art::BattleArtBinding::vessel_id);
  const auto positive = std::ranges::find(
      bound, 7, &native_battle_art::BattleArtBinding::vessel_id);
  require(zero != bound.end() && positive != bound.end(),
          "fleet zero did not use its reserved tactical identity");
  require(zero->vessel_index == 1 && positive->vessel_index == 2,
          "observed vessel offsets were not retained stably");
  require(zero->position.x == 12 && zero->velocity.y == 4 &&
              zero->heading_radians == -.75F,
          "observed motion was not copied");

  fixture.snapshot.battle_id[0] = 9;
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).empty(),
          "another battle's snapshot bound art");
  fixture.snapshot.battle_id = fixture.encounter.battle.battle_id;
  fixture.snapshot.battle_id = {};
  fixture.encounter.battle.battle_id = {};
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).empty(),
          "an unset battle identity bound art");
  fixture.snapshot.battle_id = {1, 3, 5, 7};
  fixture.encounter.battle.battle_id = fixture.snapshot.battle_id;
  fixture.encounter.reconciled = true;
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).empty(),
          "a reconciled encounter bound art");
  fixture.encounter.reconciled = false;
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 3, fixture.fleets).empty(),
          "a non-player observer received owned art");
}

void secrecy_and_integrity() {
  Fixture fixture;
  fixture.snapshot.formations.front().is_exact = false;
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).empty(),
          "an inexact formation exposed art");
  fixture.snapshot.formations.front().is_exact = true;
  fixture.snapshot.formations.front().civilization_id = 8;
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).empty(),
          "a foreign formation exposed art");
  fixture.snapshot.formations.front().civilization_id = 4;
  fixture.fleets.own_fleets.front().design_id = "science_cutter";
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).size() == 1,
          "a non-approved design received moving artwork");
  fixture.fleets.own_fleets.front().design_id = "patrol_corvette";
  fixture.snapshot.formations.front().important_vessels[1].design_id =
      "science_cutter";
  require(native_battle_art::bind_owned_battle_art(
              fixture.snapshot, fixture.encounter, 4, fixture.fleets).size() == 1,
          "a design mismatch received artwork");

  Fixture foreign_only;
  foreign_only.fleets.own_fleets.clear();
  foreign_only.fleets.foreign_contacts.push_back({0, 100, 2, "scan"});
  require(native_battle_art::bind_owned_battle_art(
              foreign_only.snapshot, foreign_only.encounter, 4,
              foreign_only.fleets).empty(),
          "foreign intelligence was treated as owned canonical state");
}

void duplicate_and_binding_budget_safety() {
  Fixture duplicate_fleet;
  duplicate_fleet.fleets.own_fleets.push_back(fleet(0));
  require(native_battle_art::bind_owned_battle_art(
              duplicate_fleet.snapshot, duplicate_fleet.encounter, 4,
              duplicate_fleet.fleets).empty(),
          "duplicate canonical fleets were accepted");
  Fixture duplicate_binding;
  duplicate_binding.encounter.vessels.push_back({0, 10});
  require(native_battle_art::bind_owned_battle_art(
              duplicate_binding.snapshot, duplicate_binding.encounter, 4,
              duplicate_binding.fleets).empty(),
          "duplicate campaign bindings were accepted");
  Fixture duplicate_vessel;
  duplicate_vessel.snapshot.formations.front().important_vessels.push_back(
      vessel(core::campaign_zero_fleet_vessel_id));
  require(native_battle_art::bind_owned_battle_art(
              duplicate_vessel.snapshot, duplicate_vessel.encounter, 4,
              duplicate_vessel.fleets).empty(),
          "duplicate observed vessels were accepted");
  Fixture duplicate_formation;
  duplicate_formation.snapshot.formations.push_back(
      duplicate_formation.snapshot.formations.front());
  require(native_battle_art::bind_owned_battle_art(
              duplicate_formation.snapshot, duplicate_formation.encounter, 4,
              duplicate_formation.fleets).empty(),
          "duplicate observed formations were accepted");

  Fixture many;
  many.snapshot.formations.clear();
  many.encounter.vessels.clear();
  many.fleets.own_fleets.clear();
  for (int id = 1; id <= 40; ++id) {
    auto item = formation(id, 4, {static_cast<float>(id), 0});
    item.important_vessels.push_back(vessel(id));
    many.snapshot.formations.push_back(std::move(item));
    many.encounter.vessels.push_back({id, id});
    many.fleets.own_fleets.push_back(fleet(id));
  }
  require(native_battle_art::bind_owned_battle_art(
              many.snapshot, many.encounter, 4, many.fleets).size() ==
              native_battle_art::maximum_battle_art_sprites,
          "binding did not enforce the hard artwork budget");
}

native_battle_art::BattleArtBinding plan_binding(std::int64_t id,
                                                  core::MassivePoint position) {
  return {id, id, "patrol_corvette", position, {0, 2}, 0, 0};
}

void render_plan_contract() {
  const native_battle_art::BattleArtProjection project =
      [](core::MassivePoint point) {
        return native_map::Point{100 + point.x * 2, 80 + point.y * 2};
      };
  std::vector bindings{plan_binding(1, {0, 0})};
  const native_map::UiRect clip{0, 0, 200, 160};
  const auto low = native_battle_art::prepare_battle_art(
      bindings, project, clip, 2, 1);
  const auto high = native_battle_art::prepare_battle_art(
      bindings, project, clip, 5, 1);
  require(low.size() == 1 && high.size() == 1 &&
              high.front().size.x > low.front().size.x &&
              low.front().size.x == low.front().size.y,
          "zoom or square artwork sizing is incorrect");
  require(low.front().center.x > 100 && low.front().center.y == 80,
          "world projection and vessel offset were not used");
  require(low.front().moving &&
              std::abs(low.front().heading_degrees - 90) < .001F,
          "moving heading did not follow velocity");

  bindings.front().velocity = {};
  bindings.front().heading_radians = std::numbers::pi_v<float>;
  auto stationary = native_battle_art::prepare_battle_art(
      bindings, project, clip, 2, 1);
  require(stationary.size() == 1 && !stationary.front().moving &&
              std::abs(std::abs(stationary.front().heading_degrees) - 180) <
                  .001F,
          "stationary heading did not use observed orientation");
  bindings.front().velocity.x = std::numeric_limits<float>::quiet_NaN();
  stationary = native_battle_art::prepare_battle_art(bindings, project, clip,
                                                      2, 1);
  require(stationary.size() == 1 && !stationary.front().moving,
          "invalid velocity produced moving effects");

  auto offset = plan_binding(2, {0, 0});
  offset.vessel_index = 1;
  bindings = {plan_binding(1, {0, 0}), offset};
  const auto separated = native_battle_art::prepare_battle_art(
      bindings, project, clip, 2, 1);
  require(separated.size() == 2 &&
              std::hypot(separated[1].center.x - 100,
                         separated[1].center.y - 80) >=
                  separated[1].size.x * .65F &&
              std::hypot(separated[0].center.x - 100,
                         separated[0].center.y - 80) >=
                  separated[0].size.x * .65F,
          "independent vessels obscured the formation token");

  bindings = {plan_binding(1, {-90, 0}), plan_binding(2, {1000, 0})};
  const auto clipped = native_battle_art::prepare_battle_art(
      bindings, project, clip, 2, 1);
  require(clipped.size() == 1 &&
              clipped.front().clip.x == clip.x &&
              clipped.front().clip.width == clip.width,
          "partial clipping or offscreen rejection is incorrect");
}

void invalid_and_plan_budget_safety() {
  const native_map::UiRect clip{0, 0, 200, 160};
  const native_battle_art::BattleArtProjection project =
      [](core::MassivePoint point) {
        return native_map::Point{point.x + 100, point.y + 80};
      };
  std::vector<native_battle_art::BattleArtBinding> bindings;
  for (int id = 1; id <= 40; ++id)
    bindings.push_back(plan_binding(id, {0, 0}));
  require(native_battle_art::prepare_battle_art(bindings, project, clip, 2, 1,
                                                1000).size() ==
              native_battle_art::maximum_battle_art_sprites,
          "caller budget bypassed the hard sprite limit");
  require(native_battle_art::prepare_battle_art(bindings, project, clip, 2, 1,
                                                3).size() == 3,
          "caller sprite budget was not honored");
  require(native_battle_art::prepare_battle_art(bindings, {}, clip, 2, 1).empty(),
          "empty projection was accepted");
  require(native_battle_art::prepare_battle_art(bindings, project, clip, 0, 1)
              .empty(),
          "invalid zoom was accepted");
  require(native_battle_art::prepare_battle_art(
              bindings, project, {0, 0, -1, 20}, 2, 1).empty(),
          "invalid clipping rectangle was accepted");
  bindings.front().position.x = std::numeric_limits<float>::infinity();
  require(native_battle_art::prepare_battle_art(bindings, project, clip, 2, 1)
              .size() == 32,
          "invalid geometry consumed a valid sprite budget slot");
  const native_battle_art::BattleArtProjection invalid_projection =
      [](core::MassivePoint) {
        return native_map::Point{std::numeric_limits<float>::quiet_NaN(), 0};
      };
  require(native_battle_art::prepare_battle_art(
              bindings, invalid_projection, clip, 2, 1).empty(),
          "non-finite projection output was accepted");
}
} // namespace

int main() {
  try {
    binding_contract();
    secrecy_and_integrity();
    duplicate_and_binding_budget_safety();
    render_plan_contract();
    invalid_and_plan_budget_safety();
    std::cout << "Native battle art binding and render-plan tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native_battle_art_tests: " << error.what() << '\n';
    return 1;
  }
}
