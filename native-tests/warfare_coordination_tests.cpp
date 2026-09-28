#include <stellar/core/colony_economy.hpp>
#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/warfare_coordination.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {

void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}

CivilizationControlQuery all_ai_control() {
  return [](int) { return true; };
}

struct WarfareFixture {
  std::vector<StellarSystem> systems;
  std::vector<Civilization> civilizations;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  std::vector<FleetPowerObservation> intelligence;
  InterstellarLaneNetwork lanes;
  DiplomacyState diplomacy;
  DiplomacyCampaignRuntimeCoordinator runtime;

  explicit WarfareFixture(std::vector<StellarSystem> seeded_systems)
      : systems(std::move(seeded_systems)), lanes(systems),
        runtime(diplomacy) {
    runtime.reset(0, true);
  }

  WarfareWorldView world() {
    return {systems,    civilizations, colonies,
            fleets,     intelligence,  lanes,
            all_ai_control()};
  }
};

StellarSystem system(int id, float x, float y) {
  StellarSystem value;
  value.id = id;
  value.position = {x, y, std::nullopt};
  return value;
}

Civilization civilization(int id, double aggression, double territoriality) {
  Civilization value;
  value.id = id;
  value.name = "Civilization " + std::to_string(id);
  value.home_system_id = id;
  value.species_id = "terran_baseline";
  value.traits.aggression = aggression;
  value.traits.territoriality = territoriality;
  value.traits.greed = 0.4;
  value.traits.risk_tolerance = 0.5;
  value.traits.survival_priority = 0.2;
  return value;
}

Colony colony(int id, int civilization_id, int system_id) {
  Colony value;
  value.id = id;
  value.civilization_id = civilization_id;
  value.system_id = system_id;
  value.name = "Colony " + std::to_string(id);
  return value;
}

FleetState military_fleet(int id, int civilization_id, int system_id) {
  FleetState value;
  value.id = id;
  value.civilization_id = civilization_id;
  value.name = "Patrol " + std::to_string(id);
  value.role = FleetRole::Military;
  value.design_id = "patrol_corvette";
  value.current_system_id = system_id;
  return value;
}

void seed_mutual_identification(DiplomacyState &state, int first, int second) {
  DiplomacySimulation simulation(state);
  for (const auto pair : {std::pair{first, second}, {second, first}}) {
    FirstContactOpportunity opportunity;
    opportunity.observer_civilization_id = pair.first;
    opportunity.contact_id = "contact-" + std::to_string(pair.first) + "-" +
                             std::to_string(pair.second);
    opportunity.target_civilization_id = pair.second;
    opportunity.observed_at_tick = 0;
    opportunity.awareness = ContactAwareness::contact_established;
    opportunity.condition = ContactCondition::active;
    opportunity.confidence = 1.0;
    (void)simulation.process_contact_opportunity(opportunity);
  }
}

bool at_war(const DiplomacyState &state, int first, int second) {
  const auto relationship = state.get_relationship(first, second);
  return relationship &&
         relationship->political_state == DiplomaticPoliticalState::at_war;
}

// An aggressive civilization bordering a weaker neighbour declares war
// through the canonical observer diplomacy command.
void aggressive_border_contact_declares_war() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.95, 0.95),
                           civilization(1, 0.10, 0.10)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 0, 0), military_fleet(1, 1, 1)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);

  WarfareCoordinator coordinator;
  const auto result = coordinator.advance(fixture.world(), fixture.runtime, 7, 1000);
  require(result.wars_declared == 1,
          "aggressive border contact produced no war declaration");
  require(at_war(fixture.diplomacy, 0, 1),
          "war declaration did not transition the relationship to at_war");
}

// A passive civilization under identical geometry refrains from declaring.
void passive_border_contact_keeps_peace() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.05, 0.05),
                           civilization(1, 0.05, 0.05)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 0, 0), military_fleet(1, 1, 1)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);

  WarfareCoordinator coordinator;
  const auto result = coordinator.advance(fixture.world(), fixture.runtime, 7, 1000);
  require(result.wars_declared == 0,
          "passive border contact produced a war declaration");
  require(!at_war(fixture.diplomacy, 0, 1),
          "passive civilizations ended up at war");
}

// A fleet co-located with an at-war hostile receives an Attack order through
// the canonical combat command path.
void co_located_hostile_fleet_engages() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.5, 0.5), civilization(1, 0.5, 0.5)};
  fixture.fleets = {military_fleet(0, 0, 0), military_fleet(1, 1, 0)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);
  DiplomacySimulation(fixture.diplomacy).declare_war(0, 1, 0);

  WarfareCoordinator coordinator;
  const auto result = coordinator.advance(fixture.world(), fixture.runtime, 0, 1000);
  require(result.engagement_orders >= 1,
          "co-located at-war fleets produced no engagement order");
  const auto &fleet = fixture.fleets.front();
  require(fleet.combat && fleet.combat->order == MilitaryOrderType::Attack,
          "engagement order did not reach the fleet combat state");
}

// An idle armed fleet at war routes toward the nearest hostile-occupied
// system through the canonical reach/route machinery.
void idle_war_fleet_deploys_toward_enemy() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.5, 0.5), civilization(1, 0.5, 0.5)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 0, 0)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);
  DiplomacySimulation(fixture.diplomacy).declare_war(0, 1, 0);

  WarfareCoordinator coordinator;
  const auto result = coordinator.advance(fixture.world(), fixture.runtime, 0, 1000);
  require(result.deployment_orders >= 1,
          "idle at-war fleet received no deployment order");
  const auto &fleet = fixture.fleets.front();
  require(fleet.destination_system_id &&
              *fleet.destination_system_id == 1,
          "deployment order did not route the fleet toward hostile space");
}

// Foreign military presence inside a colony system records a trespass event
// through the diplomacy simulation.
void foreign_presence_records_trespass() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.1, 0.1), civilization(1, 0.1, 0.1)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  // Intruder fleet sits inside civilization 1's colony system.
  fixture.fleets = {military_fleet(0, 0, 1)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);

  WarfareCoordinator coordinator;
  // Civilization 1's review phase is (1 * 997) % 7000 = 997, so it reviews
  // once the tick crosses 997 within the elapsed window.
  const auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 1500, 1000);
  require(result.trespasses_recorded >= 1,
          "foreign military presence produced no trespass record");
}

} // namespace

int main() try {
  aggressive_border_contact_declares_war();
  passive_border_contact_keeps_peace();
  co_located_hostile_fleet_engages();
  idle_war_fleet_deploys_toward_enemy();
  foreign_presence_records_trespass();
  std::cout << "warfare coordination: 5/5 scenarios passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "warfare coordination failed: " << error.what() << '\n';
  return 1;
}
