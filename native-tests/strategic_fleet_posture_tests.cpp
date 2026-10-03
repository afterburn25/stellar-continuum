#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/strategic_fleet_posture.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <string>

using namespace stellar::core;

namespace {
int failures{};

void check(bool condition, std::string message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

FirstContactOpportunity contact(int observer, int target, std::int64_t tick) {
  return {observer,
          "contact-" + std::to_string(observer) + "-" + std::to_string(target),
          target,
          tick,
          7,
          ContactAwareness::communication_available,
          ContactCondition::active,
          true,
          1};
}

void mutual_contact(DiplomacyState &state, int a, int b, std::int64_t tick) {
  DiplomacySimulation simulation(state);
  (void)simulation.process_contact_opportunity(contact(a, b, tick));
  (void)simulation.process_contact_opportunity(contact(b, a, tick));
}

FreshCampaignState campaign() {
  FreshCampaignState result;
  Civilization one;
  one.id = 1;
  one.name = "Admiral";
  result.civilizations.push_back(one);
  FleetState military;
  military.id = 11;
  military.civilization_id = 1;
  military.role = FleetRole::Military;
  military.name = "First Fleet";
  result.fleets.push_back(military);
  FleetState scouts;
  scouts.id = 12;
  scouts.civilization_id = 1;
  scouts.role = FleetRole::Scout;
  scouts.name = "Pathfinder";
  result.fleets.push_back(scouts);
  FleetState foreign;
  foreign.id = 13;
  foreign.civilization_id = 2;
  foreign.role = FleetRole::Military;
  foreign.name = "Intruder";
  result.fleets.push_back(foreign);
  FleetState reserve;
  reserve.id = 14;
  reserve.civilization_id = 1;
  reserve.role = FleetRole::Military;
  reserve.name = "Reserve";
  reserve.is_active = false;
  result.fleets.push_back(reserve);
  return result;
}

CivilizationTraits traits() {
  CivilizationTraits result{};
  result.survival_priority = 0.5;
  return result;
}

CivilizationStrategicReview review(
    StrategicPriorityType primary = StrategicPriorityType::BuildFleet) {
  CivilizationStrategicReview result{};
  result.plan.civilization_id = 1;
  result.plan.priorities.push_back({primary, 0.9, "test"});
  return result;
}

FleetState *find_fleet(FreshCampaignState &campaign, int id) {
  for (auto &fleet : campaign.fleets)
    if (fleet.id == id) return &fleet;
  return nullptr;
}

void war_holds_weapons_free() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation(state).declare_war(1, 2, 20, {});
  const auto result = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(), review(), state.build_view_for(1), 60);
  const auto *fleet = find_fleet(state_campaign, 11);
  check(result.doctrines_changed == 1 &&
            fleet->doctrine.posture == FleetDoctrinePosture::EngageAtWill,
        "an active war holds weapons free on the military fleet");
  check(std::abs(fleet->doctrine.auto_retreat_hull_fraction - 0.30) < 1e-9,
        "survival-driven fleets keep a retreat floor");
  check(find_fleet(state_campaign, 12)->doctrine.posture ==
                FleetDoctrinePosture::HoldFast &&
            find_fleet(state_campaign, 13)->doctrine.posture ==
                FleetDoctrinePosture::HoldFast &&
            find_fleet(state_campaign, 14)->doctrine.posture ==
                FleetDoctrinePosture::HoldFast,
        "civilian, foreign, and inactive fleets are untouched");
}

void honor_bound_fleets_never_retreat() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation(state).declare_war(1, 2, 20, {});
  CivilizationTraits honorable = traits();
  honorable.honor_bound = true;
  const auto result = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, honorable, review(), state.build_view_for(1), 60);
  const auto *fleet = find_fleet(state_campaign, 11);
  check(result.doctrines_changed == 1 &&
            fleet->doctrine.posture == FleetDoctrinePosture::EngageAtWill &&
            fleet->doctrine.auto_retreat_hull_fraction == 0.0,
        "honor-bound fleets fight to the last hull");
}

void defense_priority_holds_weapons_free() {
  auto state_campaign = campaign();
  DiplomacyState state;
  const auto result = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(),
      review(StrategicPriorityType::Defend), state.build_view_for(1), 60);
  check(result.doctrines_changed == 1 &&
            find_fleet(state_campaign, 11)->doctrine.posture ==
                FleetDoctrinePosture::EngageAtWill,
        "a defense-led plan holds weapons free without a war");
}

void peace_holds_fire_and_is_idempotent() {
  auto state_campaign = campaign();
  DiplomacyState state;
  const auto view = state.build_view_for(1);
  const auto first = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(), review(), view, 60);
  check(first.doctrines_changed == 0 &&
            find_fleet(state_campaign, 11)->doctrine.posture ==
                FleetDoctrinePosture::HoldFast &&
            find_fleet(state_campaign, 11)
                    ->doctrine.auto_retreat_hull_fraction == 0.0,
        "peacetime fleets hold fire on the factory doctrine");
  const auto second = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(), review(), view, 61);
  check(second.doctrines_changed == 0,
        "repeating the same review changes nothing");
}

void doctrine_tracks_the_war_state() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  {
    DiplomacySimulation simulation(state);
    simulation.declare_war(1, 2, 20, {});
  }
  const auto first = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(), review(), state.build_view_for(1), 60);
  check(first.doctrines_changed == 1 &&
            find_fleet(state_campaign, 11)->doctrine.posture ==
                FleetDoctrinePosture::EngageAtWill,
        "the fleet goes weapons free when the war starts");
  {
    DiplomacySimulation simulation(state);
    const auto proposal = simulation.send_proposal(
        1, 2, DiplomaticProposalKind::peace_offer, 40, "Stand down.");
    simulation.respond_to_proposal(proposal, 2, true, 45);
  }
  const auto second = StrategicFleetPostureExecutor{}.execute(
      state_campaign, 1, traits(), review(), state.build_view_for(1), 61);
  const auto *fleet = find_fleet(state_campaign, 11);
  check(second.doctrines_changed == 1 &&
            fleet->doctrine.posture == FleetDoctrinePosture::HoldFast &&
            fleet->doctrine.auto_retreat_hull_fraction == 0.0,
        "the fleet stands down when the war resolves");
}
} // namespace

int main() {
  const auto run = [](const char *name, void (*fn)()) {
    std::cerr << "-- " << name << '\n';
    try {
      fn();
    } catch (const std::exception &e) {
      std::cerr << "UNCAUGHT in " << name << ": " << e.what() << '\n';
      ++failures;
    }
  };
  run("war_holds_weapons_free", war_holds_weapons_free);
  run("honor_bound_fleets_never_retreat", honor_bound_fleets_never_retreat);
  run("defense_priority_holds_weapons_free",
      defense_priority_holds_weapons_free);
  run("peace_holds_fire_and_is_idempotent",
      peace_holds_fire_and_is_idempotent);
  run("doctrine_tracks_the_war_state", doctrine_tracks_the_war_state);
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "strategic_fleet_posture: all checks passed\n";
  return 0;
}
