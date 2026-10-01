#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_warfare_projection.hpp>
#include <stellar/core/combat_state.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// Warfare projection tests — the read-only adapter that reshapes
// authoritative campaign fleets + combat profiles into an engine
// WarfareModel theater for reports, order mapping and engagement
// previews, plus the operations-diagnostics consumer path.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool near(double a, double b, double eps = 1e-6) {
  return std::fabs(a - b) <= eps;
}

using namespace stellar::core;

FleetState make_fleet(int id, int civ, FleetRole role) {
  FleetState fleet;
  fleet.id = id;
  fleet.civilization_id = civ;
  fleet.role = role;
  fleet.is_active = true;
  return fleet;
}

} // namespace

int main() {
  using stellar::engine::FleetOrderKind;

  StellarSystem alpha;
  alpha.id = 1;
  alpha.name = "Alpha";
  alpha.position = {10.0f, 20.0f};
  StellarSystem beta;
  beta.id = 2;
  beta.name = "Beta";
  beta.position = {90.0f, 40.0f};
  const std::vector<StellarSystem> systems{alpha, beta};

  // Baseline projection: combat state drives class stats + condition.
  {
    auto fleet = make_fleet(7, 1, FleetRole::Military);
    fleet.position = {5.0f, 6.0f};
    FleetCombatState combat;
    combat.profile_id = "patrol_corvette_mk1";
    combat.hull = 47.5; // half of max_hull 95
    fleet.combat = combat;
    MassiveVesselState vessel;
    vessel.battles_fought = 4;
    fleet.tactical_vessel = vessel;

    const auto theater =
        project_warfare_theater(std::span{&fleet, 1}, systems);
    const auto *projected = theater.fleet(7);
    check(projected != nullptr, "fleet projected into theater");
    check(projected && projected->order.kind == FleetOrderKind::Hold,
          "stationed fleet holds");
    const auto *cohort = theater.cohort(7, "patrol_corvette_mk1");
    check(cohort != nullptr, "cohort uses authoritative profile id");
    check(cohort && near(cohort->count, 1.0), "one vessel per fleet");
    check(cohort && near(cohort->condition, 0.5),
          "cohort condition tracks hull fraction");
    check(cohort && near(cohort->experience, 0.6),
          "battle experience projects at documented scale");
    const auto report = theater.report(7);
    // patrol_corvette_mk1: 28 damage / 0.75 day interval, 35+45+95
    // pool, class speed 22 — report() scales effective stats by the
    // 0.5 cohort condition (engine semantics).
    check(near(report.attack,
               (28.0 / 0.75) * 0.5 * (1.0 + 0.5 * 0.6), 1e-4),
          "attack projects sustained damage per day");
    check(near(report.hull, (35 + 45 + 95) * 0.5),
          "hull pool projects shields+armor+hull");
    check(near(report.speed, 22.0 * 0.5),
          "speed projects fleet strategic speed");
    const auto *cls = theater.ship_class("patrol_corvette_mk1");
    check(cls && near(cls->attack, 28.0 / 0.75, 1e-4) &&
              near(cls->hull, 175.0) && near(cls->speed, 22.0),
          "class rows carry unscaled authoritative stats");
  }

  // Order mapping.
  {
    std::vector<FleetState> fleets;

    auto mover = make_fleet(1, 1, FleetRole::Military);
    mover.destination_system_id = 2;
    mover.transit_phase = FleetTransitPhase::InterstellarWarp;
    fleets.push_back(mover);

    auto retreater = make_fleet(2, 1, FleetRole::Military);
    retreater.return_to_base_requested = true;
    retreater.destination_system_id = 2; // retreat wins over transit
    fleets.push_back(retreater);

    auto target = make_fleet(3, 2, FleetRole::Military);
    target.position = {50.0f, 50.0f};
    fleets.push_back(target);

    auto attacker = make_fleet(4, 1, FleetRole::Military);
    FleetCombatState attack_state;
    attack_state.profile_id = "patrol_corvette_mk1";
    attack_state.order = MilitaryOrderType::Attack;
    attack_state.target_fleet_id = 3;
    attacker.combat = attack_state;
    fleets.push_back(attacker);

    const auto theater = project_warfare_theater(fleets, systems);
    const auto *mo = theater.fleet(1);
    check(mo && mo->order.kind == FleetOrderKind::Move &&
              near(mo->order.target_x, 90.0) &&
              near(mo->order.target_y, 40.0),
          "transiting fleet moves at destination system");
    const auto *ro = theater.fleet(2);
    check(ro && ro->order.kind == FleetOrderKind::Retreat,
          "retreat order wins over transit");
    const auto *ao = theater.fleet(4);
    check(ao && ao->order.kind == FleetOrderKind::Move &&
              near(ao->order.target_x, 50.0),
          "attack order steers at target fleet");
  }

  // Unarmed + missing-profile fleets still project safely; inactive
  // fleets are excluded.
  {
    auto civilian = make_fleet(1, 1, FleetRole::Science);
    auto dead = make_fleet(2, 1, FleetRole::Military);
    dead.is_active = false;
    const std::vector<FleetState> fleets{civilian, dead};
    const auto theater = project_warfare_theater(fleets, systems);
    check(theater.fleet(1) != nullptr, "unarmed fleet projects");
    check(theater.fleet(2) == nullptr, "inactive fleet excluded");
    check(near(theater.report(1).attack, 0.0),
          "unarmed fleet reports zero attack");
  }

  // Deterministic engagement preview on a projected theater.
  {
    auto a = make_fleet(1, 1, FleetRole::Military);
    FleetCombatState ca;
    ca.profile_id = "patrol_corvette_mk1";
    ca.hull = 95;
    a.combat = ca;
    auto b = make_fleet(2, 2, FleetRole::Military);
    b.combat = ca;
    b.position = {3.0f, 0.0f};
    const std::vector<FleetState> fleets{a, b};
    auto first = project_warfare_theater(fleets, systems);
    auto second = project_warfare_theater(fleets, systems);
    const auto ra = first.resolve(1, 2, 5.0);
    const auto rb = second.resolve(1, 2, 5.0);
    check(near(ra.a_ships_lost, rb.a_ships_lost) &&
              near(ra.b_ships_lost, rb.b_ships_lost),
          "engagement preview is deterministic");
    check(ra.a_ships_lost > 0.0 && ra.b_ships_lost > 0.0,
          "armed fleets trade attrition");
    // Campaign state is untouched: the projection is a copy.
    check(near(fleets[0].combat->hull, 95.0),
          "preview does not mutate campaign fleets");
  }

  // Consumer: inspect_campaign_operations surfaces foreign_armed_presence.
  {
    FreshCampaignState world;
    world.systems = systems;

    Colony colony;
    colony.id = 1;
    colony.civilization_id = 1;
    colony.system_id = 1;
    colony.kind = SettlementKind::Colony;
    colony.population_millions = 0.0;
    world.colonies.push_back(colony);

    auto intruder = make_fleet(10, 2, FleetRole::Military);
    intruder.current_system_id = 1;
    FleetCombatState combat;
    combat.profile_id = "patrol_corvette_mk1";
    combat.hull = 95;
    intruder.combat = combat;
    world.fleets.push_back(intruder);

    auto patrol = make_fleet(11, 1, FleetRole::Military);
    patrol.current_system_id = 1;
    patrol.combat = combat; // armed but owned by the colony's civ
    world.fleets.push_back(patrol);

    auto trader = make_fleet(12, 2, FleetRole::Logistics);
    trader.current_system_id = 1;
    FleetCombatState unarmed;
    unarmed.profile_id = "civilian_light_v1";
    trader.combat = unarmed; // foreign but unarmed
    world.fleets.push_back(trader);

    auto inbound = make_fleet(13, 2, FleetRole::Military);
    inbound.current_system_id = 1;
    inbound.transit_phase = FleetTransitPhase::InterstellarWarp;
    inbound.combat = combat; // armed but not stationed
    world.fleets.push_back(inbound);

    const auto findings = inspect_campaign_operations(world, 0, 100.0);
    int presence = 0;
    for (const auto &finding : findings) {
      if (finding.event_type != "foreign_armed_presence") continue;
      ++presence;
      check(finding.entity_id && *finding.entity_id == 10,
            "finding names the intruding fleet");
      check(finding.system_id && *finding.system_id == 1,
            "finding names the contested system");
      check(finding.civilization_id && *finding.civilization_id == 2,
            "finding names the fleet owner");
    }
    check(presence == 1,
          "only the stationed armed foreign fleet is flagged");
  }

  if (failures == 0)
    std::cout << "campaign warfare projection tests passed\n";
  return failures;
}
