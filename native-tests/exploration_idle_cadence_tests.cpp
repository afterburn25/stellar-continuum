#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/civilian_recovery.hpp>
#include <chrono>
#include <cmath>
#include <iostream>

// ADR 0002 phase-internal cadence coverage: the idle-survey verdict memo on
// ExplorationSimulation must reproduce the exact no-op a fresh planner
// evaluation would produce, must invalidate on every verdict-affecting
// input, and must never serve an injected reach provider.

using namespace stellar::core;
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

struct Scenario {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<CivilizationEconomy> economies;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  CivilizationKnowledgeState knowledge;
  Scenario() {
    for (int i = 0; i < 4; ++i) {
      StellarSystem s;
      s.id = i;
      s.name = "System " + std::to_string(i);
      // System 3 sits beyond the default 110 ly leg range so the science
      // vessel starts with no supported survey work.
      s.position = {i == 3 ? 500.f : i * 100.f, 0.f, {}};
      systems.push_back(s);
    }
    Civilization c;
    c.id = 0;
    c.home_system_id = 0;
    c.name = "Survey authority";
    c.development_stage = CivilizationDevelopmentStage::WarpCapable;
    civilizations.push_back(c);
    CivilizationEconomy economy;
    economy.civilization_id = 0;
    economies.push_back(economy);
    Colony base;
    base.id = 1;
    base.system_id = 0;
    base.civilization_id = 0;
    colonies.push_back(base);
    FleetState f;
    f.id = 1;
    f.civilization_id = 0;
    f.name = "Survey ship";
    f.role = FleetRole::Science;
    f.current_system_id = 0;
    f.maximum_leg_range_light_years = 110;
    f.fuel_capacity_light_years = 500;
    f.fuel_remaining_light_years = 500;
    fleets.push_back(f);
    for (int i = 0; i < 3; ++i)
      knowledge.mark_system_fully_surveyed(0, i);
  }
  ExplorationAdvanceWorldView view(InterstellarLaneNetwork &lanes) {
    return {systems, bodies, civilizations, fleets,
            colonies, economies, knowledge,  lanes};
  }
};

int main(int argc, char **) try {
  const bool massive = argc > 1;
  // Survey-level revision: insertions and level transitions bump it,
  // progress-only updates do not.
  CivilizationKnowledgeState revisions;
  check(revisions.survey_level_revision(0) == 0,
        "Fresh knowledge reported a nonzero revision");
  revisions.reveal_system(0, 7);
  const auto after_reveal = revisions.survey_level_revision(0);
  check(after_reveal > 0, "Revealing a system did not bump the revision");
  revisions.record_reconnaissance(0, 7, 0.35);
  check(revisions.survey_level_revision(0) > after_reveal,
        "Level transition to partially_surveyed did not bump the revision");
  const auto before_progress = revisions.survey_level_revision(0);
  revisions.advance_system_survey(0, 7, 0.1);
  check(revisions.survey_level_revision(0) == before_progress,
        "Progress-only update must not bump the level revision");
  revisions.advance_system_survey(0, 7, 1.0);
  check(revisions.survey_level_revision(0) > before_progress,
        "Completing a survey did not bump the revision");
  check(revisions.survey_level_revision(9) == 0,
        "Revisions leaked across civilizations");

  // Retained verdict: an idle AI science vessel with all supported work
  // exhausted must reproduce the exact no-op on every later tick.
  Scenario idle;
  InterstellarLaneNetwork lanes(idle.systems);
  ExplorationSimulation simulation;
  for (int tick = 0; tick < 4; ++tick) {
    const auto events = simulation.advance(idle.view(lanes), 0.25);
    check(events.empty(), "Idle verdict manufactured exploration events");
    check(!idle.fleets[0].destination_system_id,
          "Idle verdict manufactured a route");
    check(!idle.fleets[0].return_to_base_failure_reason,
          "Idle verdict recorded a spurious return failure");
  }
  const auto stats = simulation.idle_verdict_stats();
  check(stats.stored == 1 && stats.misses == 1 && stats.hits == 3,
        "Idle verdict memo did not retain and reuse the no-work verdict");

  // Fleet-state invalidation: extending the vessel's reach must force fresh
  // planning that discovers the previously unsupported target.
  idle.fleets[0].maximum_leg_range_light_years = 500;
  auto events = simulation.advance(idle.view(lanes), 0.25);
  check(idle.fleets[0].destination_system_id == 3,
        "Fleet reach change did not invalidate the idle verdict");
  check(simulation.idle_verdict_stats().misses == 2,
        "Fleet reach change served a stale idle verdict");

  // Colony-set invalidation under the reserve policy: a stranded vessel must
  // rediscover newly established refuelling service.
  Scenario stranded;
  InterstellarLaneNetwork stranded_lanes(stranded.systems);
  ExplorationSimulation reserve({}, MissionFuelPolicy::RetainReturnToService);
  auto &ship = stranded.fleets[0];
  ship.current_system_id = 2;
  ship.position = {200, 0};
  ship.fuel_remaining_light_years = 8;
  stranded.knowledge.mark_system_fully_surveyed(0, 2);
  stranded.knowledge.mark_system_fully_surveyed(0, 3);
  (void)reserve.advance(stranded.view(stranded_lanes), 0.25);
  check(ship.return_to_base_failure_reason && !ship.destination_system_id,
        "Stranded vessel did not report its unrecoverable return");
  (void)reserve.advance(stranded.view(stranded_lanes), 0.25);
  check(reserve.idle_verdict_stats().hits == 1,
        "Second stranded evaluation did not reuse the retained verdict");
  Colony relief;
  relief.id = 9;
  relief.civilization_id = 0;
  relief.system_id = 2;
  stranded.colonies.push_back(relief);
  (void)reserve.advance(stranded.view(stranded_lanes), 0.25);
  check(reserve.idle_verdict_stats().misses == 2,
        "Colony-set change served a stale idle verdict");
  check(ship.fuel_remaining_light_years == 500,
        "Newly serviced colony was not observed on re-evaluation");
  check(!ship.destination_system_id,
        "Still-exhausted survey work manufactured a route");

  // Injected reach providers bypass the memo: they may consult state outside
  // the verdict key, so every tick must evaluate fresh.
  Scenario custom;
  InterstellarLaneNetwork custom_lanes(custom.systems);
  int reach_calls = 0;
  ExplorationSimulation injected(
      [&](OperationalReachWorldView, int, const FleetState &, int,
          InterstellarMissionKind) {
        ++reach_calls;
        return unsupported_mission_reach("stubbed reach");
      });
  for (int tick = 0; tick < 3; ++tick)
    (void)injected.advance(custom.view(custom_lanes), 0.25);
  check(reach_calls >= 3,
        "Injected reach provider was served a retained verdict");
  check(injected.idle_verdict_stats().hits == 0 &&
            injected.idle_verdict_stats().stored == 0,
        "Injected reach provider populated the canonical verdict memo");

  if (massive) {
    // Scale check: 400 idle vessels over 200 ticks must complete through
    // retained verdicts, not per-tick planner sweeps.
    Scenario scale;
    for (int i = 2; i <= 400; ++i) {
      FleetState extra = scale.fleets[0];
      extra.id = i;
      extra.name = "Survey ship " + std::to_string(i);
      scale.fleets.push_back(extra);
    }
    InterstellarLaneNetwork scale_lanes(scale.systems);
    ExplorationSimulation scale_sim;
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 200; ++tick)
      (void)scale_sim.advance(scale.view(scale_lanes), 0.25);
    const auto elapsed =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start)
            .count();
    const auto scale_stats = scale_sim.idle_verdict_stats();
    check(scale_stats.stored == 400 && scale_stats.hits == 79600,
          "Scale scenario did not fully retain idle verdicts");
    // Baseline: one fresh planner sweep per fleet through the same
    // coordinator path the advance loop would invoke without the memo.
    ExplorationMissionPlanner baseline_planner;
    ExplorationAiMissionCoordinator baseline_coordinator(baseline_planner);
    const auto plan_start = std::chrono::steady_clock::now();
    for (const auto &f : scale.fleets)
      (void)baseline_coordinator.select_mission(
          {scale.systems, scale.bodies, scale.fleets, scale.colonies,
           scale.knowledge, scale_lanes},
          f, MissionFuelPolicy::ReachDestination);
    const auto plan_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - plan_start)
            .count();
    std::cout << "{\"idleFleets\":400,\"ticks\":200,"
                 "\"milliseconds\":"
              << elapsed << ",\"hits\":" << scale_stats.hits
              << ",\"freshPlan400Milliseconds\":" << plan_ms
              << ",\"avoidedSweepEstimateMs\":"
              << plan_ms * static_cast<double>(scale_stats.hits) / 400.0
              << "}\n";
  }

  std::cout << "exploration idle cadence tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
