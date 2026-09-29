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

void seed_mutual_communication(DiplomacyState &state, int first, int second) {
  DiplomacySimulation simulation(state);
  for (const auto pair : {std::pair{first, second}, {second, first}}) {
    FirstContactOpportunity opportunity;
    opportunity.observer_civilization_id = pair.first;
    opportunity.contact_id = "contact-" + std::to_string(pair.first) + "-" +
                             std::to_string(pair.second);
    opportunity.target_civilization_id = pair.second;
    opportunity.observed_at_tick = 0;
    opportunity.awareness = ContactAwareness::communication_available;
    opportunity.condition = ContactCondition::active;
    opportunity.communication_available = true;
    opportunity.confidence = 1.0;
    (void)simulation.process_contact_opportunity(opportunity);
  }
}

// Pushes enough journal entries to scroll earlier events (such as a war
// declaration) off the bounded recent-history window.
void scroll_history(DiplomacyState &state, int territorial, int intruder,
                    int system_id, std::int64_t count) {
  DiplomacySimulation simulation(state);
  for (std::int64_t tick = 1; tick <= count; ++tick)
    simulation.record_trespass(territorial, intruder, system_id, tick);
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

// A weak, survival-driven civilization in a long war sends a settlement
// offer through the canonical proposal command.
void weary_losing_war_offers_settlement() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  auto weak = civilization(0, 0.05, 0.05);
  weak.traits.survival_priority = 0.8;
  fixture.civilizations = {weak, civilization(1, 0.5, 0.5)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  // Civilization 0 fields no armed fleet; civilization 1 does.
  fixture.fleets = {military_fleet(0, 1, 1)};
  seed_mutual_communication(fixture.diplomacy, 0, 1);
  DiplomacySimulation(fixture.diplomacy).declare_war(1, 0, 0);
  // The declaration scrolls off the bounded journal — the war reads as long.
  scroll_history(fixture.diplomacy, 1, 0, 1, 300);

  WarfareCoordinator coordinator;
  // Civilization 0's phase is 0; the first review boundary is 7000.
  const auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.peace_offers_sent >= 1,
          "weary losing civilization sent no settlement offer");
  const auto offers = fixture.diplomacy.snapshot().proposals;
  require(std::ranges::any_of(offers,
                              [](const auto &proposal) {
                                return proposal.proposer_civilization_id == 0 &&
                                       proposal.recipient_civilization_id ==
                                           1 &&
                                       proposal.status ==
                                           DiplomaticProposalStatus::pending;
                              }),
          "settlement offer did not reach the counterpart as a pending "
          "proposal");
}

// A weary losing civilization accepts an incoming peace offer, ending the
// war through the canonical proposal response.
void weary_war_accepts_incoming_peace() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  auto weak = civilization(0, 0.05, 0.05);
  weak.traits.survival_priority = 0.8;
  fixture.civilizations = {weak, civilization(1, 0.5, 0.5)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 1, 1)};
  seed_mutual_communication(fixture.diplomacy, 0, 1);
  DiplomacySimulation(fixture.diplomacy).declare_war(1, 0, 0);
  scroll_history(fixture.diplomacy, 1, 0, 1, 300);
  (void)DiplomacySimulation(fixture.diplomacy)
      .send_proposal(1, 0, DiplomaticProposalKind::peace_offer, 7000,
                     "Offers peace to end the war.");

  WarfareCoordinator coordinator;
  const auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.peace_offers_accepted == 1,
          "weary civilization did not accept the incoming peace offer");
  const auto relationship = fixture.diplomacy.get_relationship(0, 1);
  require(relationship && relationship->political_state ==
                              DiplomaticPoliticalState::peace,
          "accepting peace did not transition the relationship to peace");
}

// A winning civilization early in a war offers nothing.
void fresh_winning_war_offers_no_peace() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.5, 0.5),
                           civilization(1, 0.5, 0.5)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 0, 0)};
  seed_mutual_communication(fixture.diplomacy, 0, 1);
  DiplomacySimulation(fixture.diplomacy).declare_war(0, 1, 7000);

  WarfareCoordinator coordinator;
  const auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.peace_offers_sent == 0,
          "fresh winning war produced a settlement offer");
  require(at_war(fixture.diplomacy, 0, 1),
          "fresh war unexpectedly left the at_war state");
}

// An identified counterpart without a channel gets one on review so
// proposals can flow.
void identified_contact_opens_communication() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.1, 0.1),
                           civilization(1, 0.1, 0.1)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);

  WarfareCoordinator coordinator;
  // Civilization 0's phase is 0; review crosses the 7000 boundary.
  const auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.communications_established >= 1,
          "identified contact produced no communication channel");
  const auto view = fixture.diplomacy.build_view_for(0);
  require(std::ranges::any_of(view.contacts,
                              [](const auto &contact) {
                                return contact.target_civilization_id == 1 &&
                                       contact.communication_available;
                              }),
          "communication channel was not established on the contact");
}

// A war whose contacts drifted stale mid-conflict: each belligerent
// reacquires the other through the canonical observation pipeline at its
// review, a channel opens once both sides are live, and a settlement offer
// flows through the canonical proposal path.
void stale_war_contacts_reacquire_and_settle() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  auto weak = civilization(0, 0.05, 0.05);
  weak.traits.survival_priority = 0.8;
  fixture.civilizations = {weak, civilization(1, 0.5, 0.5)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  // Civilization 0 fields no armed fleet; civilization 1 does.
  fixture.fleets = {military_fleet(0, 1, 1)};
  seed_mutual_identification(fixture.diplomacy, 0, 1);
  DiplomacySimulation simulation(fixture.diplomacy);
  // The declaration needs fresh identified contacts; war begins at tick 0.
  (void)simulation.declare_war(1, 0, 0);
  // Contacts then drift stale during the war — the canonical re-observation
  // path records the loss for both directions.
  for (const auto pair : {std::pair{0, 1}, {1, 0}}) {
    FirstContactOpportunity drift;
    drift.observer_civilization_id = pair.first;
    drift.contact_id = "contact-" + std::to_string(pair.first) + "-" +
                       std::to_string(pair.second);
    drift.target_civilization_id = pair.second;
    drift.observed_at_tick = 5000;
    drift.awareness = ContactAwareness::identified;
    drift.condition = ContactCondition::stale_or_lost;
    drift.confidence = 0.75;
    (void)simulation.process_contact_opportunity(drift);
  }
  // The declaration scrolls off the bounded journal — the war reads as long.
  scroll_history(fixture.diplomacy, 1, 0, 1, 300);

  WarfareCoordinator coordinator;
  // Civilization 0 reviews at the 7000 boundary: it reacquires its stale
  // contact on the belligerent; no channel yet — the other side is stale.
  auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.belligerent_contacts_reacquired == 1,
          "at-war stale contact was not reacquired");
  // Civilization 1 reviews at the 7997 boundary: it reacquires its side and
  // the mutual channel opens now that both contacts are live.
  result = coordinator.advance(fixture.world(), fixture.runtime, 8000, 500);
  require(result.belligerent_contacts_reacquired == 1,
          "counterpart's stale war contact was not reacquired");
  require(result.communications_established >= 1,
          "mutual war contacts did not open a channel");
  // Civilization 0 reviews again at the 14000 boundary: with the channel
  // live and the war long and losing, settlement flows — an incoming offer
  // accepted, its own offer sent, or both.
  result = coordinator.advance(fixture.world(), fixture.runtime, 14500, 6500);
  require(result.peace_offers_sent + result.peace_offers_accepted >= 1,
          "weary belligerent with a live channel produced no settlement "
          "action");
}

// A civilization that just accepted a ceasefire does not break it on the
// same cadence — the respect window suppresses redeclaration, and once it
// expires an aggressive civilization may resume the war.
void fresh_ceasefire_defers_redeclaration() {
  WarfareFixture fixture({system(0, 0, 0), system(1, 40, 0)});
  fixture.civilizations = {civilization(0, 0.95, 0.95),
                           civilization(1, 0.10, 0.10)};
  fixture.colonies = {colony(0, 0, 0), colony(1, 1, 1)};
  fixture.fleets = {military_fleet(0, 0, 0), military_fleet(1, 1, 1)};
  seed_mutual_communication(fixture.diplomacy, 0, 1);
  DiplomacySimulation simulation(fixture.diplomacy);
  (void)simulation.declare_war(0, 1, 0);
  // A ceasefire is proposed and accepted through the canonical proposal
  // path at tick 1000 — the agreement is barely six days old at review.
  const auto proposal = simulation.send_proposal(
      0, 1, DiplomaticProposalKind::ceasefire_offer, 1000,
      "Ceasefire to halt hostilities.");
  simulation.respond_to_proposal(proposal, 1, true, 1000);
  const auto relationship = fixture.diplomacy.get_relationship(0, 1);
  require(relationship && relationship->political_state ==
                              DiplomaticPoliticalState::ceasefire,
          "accepted ceasefire did not transition the relationship");

  WarfareCoordinator coordinator;
  // Civilization 0 reviews at the 7000 boundary: the ceasefire is fresh.
  auto result =
      coordinator.advance(fixture.world(), fixture.runtime, 7500, 1000);
  require(result.wars_declared == 0,
          "civilization broke a freshly accepted ceasefire");
  // At the 42000 boundary the respect window has expired — the aggressive
  // civilization is free to resume the war.
  result =
      coordinator.advance(fixture.world(), fixture.runtime, 43000, 35500);
  require(result.wars_declared == 1,
          "aggressive civilization never resumed after the ceasefire "
          "respect window expired");
  require(at_war(fixture.diplomacy, 0, 1),
          "resumed war did not transition the relationship to at_war");
}

} // namespace

int main() try {
  aggressive_border_contact_declares_war();
  identified_contact_opens_communication();
  passive_border_contact_keeps_peace();
  co_located_hostile_fleet_engages();
  idle_war_fleet_deploys_toward_enemy();
  foreign_presence_records_trespass();
  weary_losing_war_offers_settlement();
  weary_war_accepts_incoming_peace();
  fresh_winning_war_offers_no_peace();
  stale_war_contacts_reacquire_and_settle();
  fresh_ceasefire_defers_redeclaration();
  std::cout << "warfare coordination: 11/11 scenarios passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "warfare coordination failed: " << error.what() << '\n';
  return 1;
}
