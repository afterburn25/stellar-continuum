#pragma once

#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/knowledge.hpp>
#include <stellar/core/survey_operations.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// Shared scratch index for one read-only planning pass. Scope it to a
// single advance (or any span where the system catalog is immutable):
// systems_by_id is populated lazily once, and the per-(civilization, fleet
// role) survey-work lists memoize the needs_survey_work + priority-band
// filter keyed on the knowledge survey-level revision, so a mid-advance
// survey level change rebuilds exactly once on the next query — never
// stale. reach_batches memoizes the per-civilization O(systems + colonies)
// OperationalReachBatch::prepare() so every fleet mission selection does
// not rebuild it. Callers that omit it keep the original per-call scans.
struct ExplorationPlanningSharedIndex {
  SurveyCatalogIndex catalog;
  struct SurveyWorkList {
    std::uint64_t level_revision{};
    bool valid{};
    // (system, priority band) pairs in catalog order — identical to the
    // per-call filter output.
    std::vector<std::pair<const StellarSystem *, int>> entries;
  };
  std::unordered_map<std::int64_t, SurveyWorkList> survey_work;
  // One lazily-prepared OperationalReachBatch per civilization. Colonies
  // cannot change while a planning index is alive, so the batch's
  // refueling snapshot stays correct for its scope.
  OperationalReachBatch &reach_batch(const OperationalReachWorldView &world,
                                     int civilization_id) {
    return reach_batches_
        .try_emplace(civilization_id, world, civilization_id)
        .first->second;
  }

private:
  std::unordered_map<int, OperationalReachBatch> reach_batches_;
};

class ExplorationMissionPlanner {
public:
  static constexpr int default_maximum_candidates = 32;
  static constexpr int hard_maximum_candidates = 64;

  explicit ExplorationMissionPlanner(
      ExplorationReachAssessment operational_reach = {});

  ExplorationMissionPlan
  build_plan(ExplorationPlanningWorldView world, int fleet_id,
             int maximum_candidates = default_maximum_candidates,
             MissionFuelPolicy fuel_policy = MissionFuelPolicy::ReachDestination) const;
  [[nodiscard]] bool uses_canonical_reach() const noexcept { return uses_canonical_reach_; }
  ExplorationMissionOrderAssessment
  assess_order(ExplorationPlanningWorldView world, int fleet_id,
               int destination_system_id,
               bool require_survey_work = true) const;
  MissionReachAssessment
  assess_operational_reach(ExplorationPlanningWorldView world,
                           const FleetState &fleet,
                           int destination_system_id) const;

  // AI mission selection fast path: ranks survey targets in the same order
  // build_plan uses, but defers the route assessment until the selection is
  // decided. Returns the candidate select_mission would pick from the full
  // plan — first supported target not present in reservation_set, or the
  // first supported target overall with used_shared_fallback set when every
  // supported target is reserved. Null when nothing is supported.
  std::optional<ExplorationMissionCandidate>
  select_supported_candidate(ExplorationPlanningWorldView world,
                             const FleetState &fleet,
                             MissionFuelPolicy fuel_policy,
                             const std::unordered_set<int> &reservation_set,
                             bool &used_shared_fallback,
                             ExplorationPlanningSharedIndex *shared =
                                 nullptr) const;

  // Cheaper existence probe matching "the full plan has a supported
  // candidate": supported entries always sort first in build_plan, so any
  // supported target is present in every planning window regardless of the
  // candidate cap. Assesses targets in plan order and stops at the first
  // supported one instead of building every candidate.
  [[nodiscard]] bool has_supported_mission_target(
      ExplorationPlanningWorldView world, int fleet_id,
      MissionFuelPolicy fuel_policy =
          MissionFuelPolicy::ReachDestination,
      ExplorationPlanningSharedIndex *shared = nullptr) const;

  static bool needs_survey_work(const CivilizationKnowledgeState &knowledge,
                                const FleetState &fleet, int system_id);
  static int survey_priority(FleetRole role, SystemSurveyLevel level);

private:
  ExplorationMissionCandidate
  build_candidate(ExplorationPlanningWorldView world, const FleetState &fleet,
                  const StellarSystem &system,OperationalReachBatch *batch=nullptr,
                  MissionFuelPolicy fuel_policy=MissionFuelPolicy::ReachDestination,
                  SurveyOperationsBatch *surveys=nullptr,
                  const std::unordered_map<int, const StellarSystem *>
                      *systems_index = nullptr) const;
  bool uses_canonical_reach_{};
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
                 const FleetState &fleet,
                 MissionFuelPolicy fuel_policy=MissionFuelPolicy::ReachDestination,
                 ExplorationPlanningSharedIndex *shared = nullptr) const;

private:
  const ExplorationMissionPlanner &mission_planner_;
};

} // namespace stellar::core
