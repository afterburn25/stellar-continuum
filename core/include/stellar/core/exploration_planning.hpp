#pragma once

#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/survey_operations.hpp>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {

struct ExplorationMissionCandidate {
  int system_id{};
  std::string catalog_name;
  SystemSurveyLevel survey_level{};
  double survey_progress{};
  int priority_band{};
  double distance_from_fleet{};
  std::optional<double> estimated_remaining_science_survey_days;
  std::optional<SurveyOperationalHazard> survey_operational_hazard;
  MissionReachAssessment reach;
  std::string reason;
};

struct ExplorationMissionPlan {
  int fleet_id{};
  std::string fleet_name;
  FleetRole fleet_role{FleetRole::Scout};
  bool can_receive_orders{};
  std::string status;
  std::vector<ExplorationMissionCandidate> candidates;
};

struct ExplorationMissionOrderAssessment {
  bool accepted{};
  bool is_local_survey{};
  std::string message;
  std::optional<ExplorationMissionCandidate> candidate;
};

struct ExplorationAiMissionSelection {
  int fleet_id{};
  std::optional<ExplorationMissionCandidate> candidate;
  bool used_shared_fallback{};
  std::vector<int> reserved_system_ids;
  std::string reason;
};

struct ExplorationPlanningWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const FleetState> fleets;
  std::span<const Colony> colonies;
  const CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;
};

using ExplorationReachAssessment = std::function<MissionReachAssessment(
    OperationalReachWorldView, int, const FleetState &, int,
    InterstellarMissionKind)>;

class ExplorationMissionPlanner {
public:
  static constexpr int default_maximum_candidates = 32;
  static constexpr int hard_maximum_candidates = 64;

  explicit ExplorationMissionPlanner(
      ExplorationReachAssessment operational_reach = {});

  ExplorationMissionPlan
  build_plan(ExplorationPlanningWorldView world, int fleet_id,
             int maximum_candidates = default_maximum_candidates) const;
  ExplorationMissionOrderAssessment
  assess_order(ExplorationPlanningWorldView world, int fleet_id,
               int destination_system_id,
               bool require_survey_work = true) const;
  MissionReachAssessment
  assess_operational_reach(ExplorationPlanningWorldView world,
                           const FleetState &fleet,
                           int destination_system_id) const;

  static bool needs_survey_work(const CivilizationKnowledgeState &knowledge,
                                const FleetState &fleet, int system_id);
  static int survey_priority(FleetRole role, SystemSurveyLevel level);

private:
  ExplorationMissionCandidate
  build_candidate(ExplorationPlanningWorldView world, const FleetState &fleet,
                  const StellarSystem &system) const;
  ExplorationReachAssessment operational_reach_;
  SurveyOperationsProfiler survey_profiler_;
};

class ExplorationAiMissionCoordinator {
public:
  explicit ExplorationAiMissionCoordinator(
      const ExplorationMissionPlanner &mission_planner);
  ExplorationAiMissionCoordinator(ExplorationMissionPlanner &&) = delete;
  ExplorationAiMissionCoordinator(const ExplorationMissionPlanner &&) = delete;
  ExplorationAiMissionSelection
  select_mission(ExplorationPlanningWorldView world,
                 const FleetState &fleet) const;

private:
  const ExplorationMissionPlanner &mission_planner_;
};

} // namespace stellar::core
