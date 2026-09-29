#pragma once

#include <stellar/core/exploration_planning.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/civilization_control.hpp>
#include <stellar/core/galaxy_phenomena.hpp>

#include <optional>
#include <span>
#include <string>
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

private:
  [[nodiscard]] ExplorationMissionOrderAssessment
  issue_order(ExplorationOrderWorldView world, int fleet_id,
              int destination_system_id, bool require_survey_work) const;
  ExplorationMissionPlanner mission_planner_;
  MissionFuelPolicy ai_fuel_policy_;
  // Generation-static system/body catalog lookups shared across advances.
  // Revalidates on the source spans, so a reloaded world view rebuilds it.
  mutable SurveyCatalogIndex campaign_catalog_;
  // Generation-static per-system phenomenon contexts shared across
  // advances — pure in (regions, position, id); the (regions, systems
  // span) guard clears it on a reloaded world view.
  mutable PhenomenonContextIndex campaign_phenomena_;
  // Campaign-wide survey-work store: entries reference the systems
  // span (guarded on data + size like the catalog index) and patch
  // through the per-civilization dirty marks, so a tick with no level
  // changes for a civilization costs a revision compare instead of a
  // catalog rescan.
  mutable ExplorationPlanningSharedIndex::SurveyWorkStore
      campaign_survey_work_;
};

} // namespace stellar::core
