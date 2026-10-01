#pragma once

#include <stellar/core/exploration_planning.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/civilization_control.hpp>
#include <stellar/core/galaxy_phenomena.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::core {

enum class ExplorationEventType {
  SystemDetected,
  SystemReconnoitered,
  SystemSurveyStarted,
  SystemSurveyed,
  ResourceSignatureDetected,
  AnomalySignatureDetected,
  ActivitySignatureDetected,
  ResourceSurveyed,
  AnomalySurveyed,
  NativeCivilizationSurveyed,
  SensorContact,
  FirstContact,
};

struct ExplorationEvent {
  ExplorationEventType type{};
  int civilization_id{};
  int fleet_id{};
  int system_id{};
  std::string message;
  std::optional<int> target_civilization_id;
  std::optional<int> planetary_body_id;
};

struct ExplorationAdvanceWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::span<FleetState> fleets;
  std::span<const Colony> colonies;
  std::span<const CivilizationEconomy> economies;
  CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;
  CivilizationControlQuery control;
  const GalaxyPhenomena* phenomena{};
};

struct ExplorationOrderWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<FleetState> fleets;
  std::span<const Colony> colonies;
  const CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;

  [[nodiscard]] ExplorationPlanningWorldView planning() const noexcept {
    return {systems, bodies, fleets, colonies, knowledge, lanes};
  }
  [[nodiscard]] OperationalReachWorldView reach() const noexcept {
    return {systems, colonies, lanes};
  }
};

class ExplorationSimulation {
public:
  static constexpr double science_survey_progress_per_day = 0.08;
  static constexpr double scout_reconnaissance_progress = 0.35;
  static constexpr double scout_reconnaissance_days = 2.0;

  explicit ExplorationSimulation(
      ExplorationReachAssessment operational_reach = {},
      MissionFuelPolicy ai_fuel_policy=MissionFuelPolicy::ReachDestination);

  [[nodiscard]] MissionReachAssessment
  assess_operational_reach(ExplorationPlanningWorldView world, int fleet_id,
                           int destination_system_id) const;
  [[nodiscard]] ExplorationMissionOrderAssessment
  issue_travel_order(ExplorationOrderWorldView world, int fleet_id,
                     int destination_system_id) const;
  [[nodiscard]] ExplorationMissionOrderAssessment
  issue_survey_order(ExplorationOrderWorldView world, int fleet_id,
                     int destination_system_id) const;

  std::vector<ExplorationEvent>
  advance(ExplorationAdvanceWorldView world, double simulation_delta) const;

  // Diagnostics for the ADR 0002 phase-internal cadence layer: how many
  // idle-AI evaluations reused a retained "no supported survey work"
  // verdict versus ran the full planner.
  struct IdleVerdictStats {
    std::uint64_t hits{}, misses{}, stored{};
  };
  [[nodiscard]] IdleVerdictStats idle_verdict_stats() const noexcept;

private:
  [[nodiscard]] ExplorationMissionOrderAssessment
  issue_order(ExplorationOrderWorldView world, int fleet_id,
              int destination_system_id, bool require_survey_work) const;
  // Phase-internal cadence (ADR 0002): a runtime-only memo of the last
  // "no supported survey work" verdict per idle AI survey fleet, keyed on
  // every input that can flip the verdict — knowledge survey revisions,
  // lane-network and colony-set signatures, and the fleet's own
  // reach-affecting fields. A hit reproduces the exact no-op the full
  // planner would produce; any changed input falls through to fresh
  // evaluation. Never serialized; a cold memo simply re-plans.
  struct IdleSurveyVerdict {
    const StellarSystem *systems_data{};
    std::size_t systems_size{};
    const CivilizationKnowledgeState *knowledge{};
    std::uint64_t knowledge_nonce{};
    std::uint64_t survey_revision{};
    const InterstellarLaneNetwork *lanes{};
    std::uint64_t lanes_nonce{};
    std::uint64_t colonies_signature{};
    int civilization_id{};
    FleetRole role{};
    std::optional<int> current_system_id{};
    double fuel_remaining_light_years{};
    double fuel_capacity_light_years{};
    double maximum_leg_range_light_years{};
    bool return_to_base_requested{};

    friend bool operator==(const IdleSurveyVerdict &,
                           const IdleSurveyVerdict &) = default;
  };
  ExplorationMissionPlanner mission_planner_;
  MissionFuelPolicy ai_fuel_policy_;
  SurveyOperationsProfiler survey_profiler_;
  mutable std::unordered_map<int, IdleSurveyVerdict> idle_survey_verdicts_;
  mutable IdleVerdictStats idle_verdict_stats_{};
};

} // namespace stellar::core
