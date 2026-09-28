#include <stellar/core/exploration_planning.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
ExplorationMissionPlan unavailable_plan(int fleet_id, std::string status) {
  return {fleet_id, {}, FleetRole::Scout, false, std::move(status), {}};
}
ExplorationMissionOrderAssessment
rejected(std::string message,
         std::optional<ExplorationMissionCandidate> candidate = std::nullopt) {
  return {false, false, std::move(message), std::move(candidate)};
}
ExplorationMissionOrderAssessment
approved(std::string message, ExplorationMissionCandidate candidate,
         bool local) {
  return {true, local, std::move(message), std::move(candidate)};
}
InterstellarMissionKind mission_kind(FleetRole role) {
  switch (role) {
  case FleetRole::Scout:
    return InterstellarMissionKind::ScoutReconnaissance;
  case FleetRole::Science:
    return InterstellarMissionKind::ScienceSurvey;
  case FleetRole::Military:
    return InterstellarMissionKind::MilitaryDeployment;
  case FleetRole::Logistics:
    return InterstellarMissionKind::Logistics;
  default:
    return InterstellarMissionKind::ScoutReconnaissance;
  }
}
std::string invariant_percent_zero(double value) {
  if (std::isnan(value))
    return "NaN";
  if (std::isinf(value))
    return std::signbit(value) ? "-Infinity %" : "Infinity %";
  const double scaled = value * 100.0;
  const double lower = std::floor(scaled);
  const double fraction = scaled - lower;
  double rounded = lower;
  if (fraction > 0.5)
    rounded = lower + 1.0;
  else if (fraction == 0.5) {
    const double product_error = std::fma(value, 100.0, -scaled);
    if (product_error > 0.0 ||
        (product_error == 0.0 && std::fmod(lower, 2.0) != 0.0))
      rounded = lower + 1.0;
  }
  return std::to_string(static_cast<long long>(rounded)) + " %";
}
std::string survey_reason(FleetRole role, SystemSurveyLevel level,
                          double progress,
                          const std::optional<double> &remaining_days,
                          const MissionReachAssessment &reach) {
  std::string work;
  if (role == FleetRole::Scout && level == SystemSurveyLevel::detected)
    work = "Detected target still needs a reconnaissance pass.";
  else if (role == FleetRole::Scout)
    work = "Catalog target can be reconnoitered on arrival.";
  else if (role == FleetRole::Science &&
           level == SystemSurveyLevel::partially_surveyed && remaining_days) {
    const auto percentage = invariant_percent_zero(progress);
    const auto days = std::isnan(*remaining_days)
                          ? std::string("NaN")
                          : detail::legacy_custom_fixed(*remaining_days, 0, 1);
    work = "Detailed survey is " + percentage + " complete; approximately " +
           days + " survey days remain.";
  } else if (role == FleetRole::Science && level == SystemSurveyLevel::detected)
    work = "Detected target needs a detailed science survey.";
  else if (role == FleetRole::Science)
    work = "Catalog target can receive a first detailed science survey on "
           "arrival.";
  else
    work = "Survey work is available.";
  return reach.is_supported ? work + " " + reach.reason
                            : work + " Blocked: " + reach.reason;
}
} // namespace

ExplorationMissionPlanner::ExplorationMissionPlanner(
    ExplorationReachAssessment operational_reach)
    : uses_canonical_reach_(!operational_reach),operational_reach_(std::move(operational_reach)) {
  if (!operational_reach_)
    operational_reach_ = [](OperationalReachWorldView world,
                            int civilization_id, const FleetState &fleet,
                            int target, InterstellarMissionKind kind) {
      return ::stellar::core::assess_operational_reach(world, civilization_id,
                                                       fleet, target, kind);
    };
}

bool ExplorationMissionPlanner::needs_survey_work(
    const CivilizationKnowledgeState &knowledge, const FleetState &fleet,
    int system_id) {
  const auto level =
      knowledge.system_survey_level(fleet.civilization_id, system_id);
  if (fleet.role == FleetRole::Scout)
    return level < SystemSurveyLevel::partially_surveyed;
  if (fleet.role == FleetRole::Science)
    return level < SystemSurveyLevel::fully_surveyed;
  return false;
}

int ExplorationMissionPlanner::survey_priority(FleetRole role,
                                               SystemSurveyLevel level) {
  if (role == FleetRole::Science) {
    if (level == SystemSurveyLevel::partially_surveyed)
      return 0;
    if (level == SystemSurveyLevel::detected)
      return 1;
    return 2;
  }
  return level == SystemSurveyLevel::detected ? 0 : 1;
}

MissionReachAssessment ExplorationMissionPlanner::assess_operational_reach(
    ExplorationPlanningWorldView world, const FleetState &fleet,
    int destination_system_id) const {
  return operational_reach_({world.systems, world.colonies, world.lanes},
                            fleet.civilization_id, fleet, destination_system_id,
                            mission_kind(fleet.role));
}

namespace {
const StellarSystem *
indexed_system(const std::unordered_map<int, const StellarSystem *> &index,
               std::optional<int> id) {
  if (!id)
    return nullptr;
  const auto found = index.find(*id);
  return found == index.end() ? nullptr : found->second;
}

double indexed_distance_from_fleet(
    const std::unordered_map<int, const StellarSystem *> &index,
    const FleetState &fleet, const StellarSystem &target) {
  return interstellar_distance_from_fleet(
      indexed_system(index,
                     fleet.transit_phase == FleetTransitPhase::InterstellarWarp
                         ? fleet.transit_origin_system_id
                         : fleet.current_system_id),
      indexed_system(index,
                     fleet.transit_phase == FleetTransitPhase::InterstellarWarp
                         ? (!fleet.planned_route_system_ids.empty()
                                ? std::optional<int>(
                                      fleet.planned_route_system_ids.front())
                                : fleet.destination_system_id)
                         : std::nullopt),
      fleet, target);
}
} // namespace

ExplorationMissionCandidate
ExplorationMissionPlanner::build_candidate(ExplorationPlanningWorldView world,
                                           const FleetState &fleet,
                                           const StellarSystem &system,OperationalReachBatch *batch,MissionFuelPolicy fuel_policy,SurveyOperationsBatch *surveys,
                                           const std::unordered_map<int, const StellarSystem *> *systems_index) const {
  const auto level =
      world.knowledge.system_survey_level(fleet.civilization_id, system.id);
  const auto progress =
      world.knowledge.system_survey_progress(fleet.civilization_id, system.id);
  std::optional<SurveyOperationsProfile> profile;
  if (level >= SystemSurveyLevel::partially_surveyed)
    profile = surveys?surveys->build(system.id):survey_profiler_.build(world.systems, world.bodies, system.id);
  std::optional<double> remaining_days;
  if (fleet.role == FleetRole::Science && profile) {
    const auto remaining =
        profile->estimated_science_survey_days * (1.0 - progress);
    remaining_days =
        std::isnan(remaining) ? remaining : std::max(0.0, remaining);
  }
  const auto distance =
      systems_index
          ? indexed_distance_from_fleet(*systems_index, fleet, system)
          : interstellar_distance_from_fleet(world.systems, fleet, system);
  auto reach = batch?batch->assess(fleet,system.id,mission_kind(fleet.role),fuel_policy):assess_operational_reach(world, fleet, system.id);
  const auto priority = survey_priority(fleet.role, level);
  return {system.id,
          system.name,
          level,
          progress,
          priority,
          distance,
          remaining_days,
          profile ? std::optional(profile->operational_hazard) : std::nullopt,
          reach,
          survey_reason(fleet.role, level, progress, remaining_days, reach)};
}

ExplorationMissionPlan
ExplorationMissionPlanner::build_plan(ExplorationPlanningWorldView world,
                                      int fleet_id,
                                      int maximum_candidates,MissionFuelPolicy fuel_policy) const {
  if(fuel_policy!=MissionFuelPolicy::ReachDestination&&!uses_canonical_reach_)
    throw std::invalid_argument("Return fuel planning requires the canonical operational reach provider.");
  maximum_candidates =
      std::clamp(maximum_candidates, 1, hard_maximum_candidates);
  const auto fleet =
      std::find_if(world.fleets.begin(), world.fleets.end(),
                   [fleet_id](const auto &candidate) {
                     return candidate.id == fleet_id && candidate.is_active;
                   });
  if (fleet == world.fleets.end())
    return unavailable_plan(
        fleet_id,
        "No active exploration vessel with that fleet ID is available.");
  if (fleet->role != FleetRole::Scout && fleet->role != FleetRole::Science)
    return unavailable_plan(
        fleet_id, fleet->name + " is not a scout or science survey vessel.");

  std::vector<ExplorationMissionCandidate> candidates;
  SurveyOperationsBatch surveys(world.systems,world.bodies);
  // One id->system index per plan instead of a linear scan per candidate.
  std::unordered_map<int, const StellarSystem *> systems_index;
  systems_index.reserve(world.systems.size());
  for (const auto &system : world.systems)
    systems_index.emplace(system.id, &system);
  std::optional<OperationalReachBatch> batch;
  if(uses_canonical_reach_)batch.emplace(OperationalReachWorldView{world.systems,world.colonies,world.lanes},fleet->civilization_id);
  for (const auto &system : world.systems)
    if (needs_survey_work(world.knowledge, *fleet, system.id))
      candidates.push_back(build_candidate(world, *fleet, system,batch?&*batch:nullptr,fuel_policy,&surveys,&systems_index));
  std::stable_sort(
      candidates.begin(), candidates.end(),
      [](const auto &left, const auto &right) {
        const auto left_support = left.reach.is_supported ? 0 : 1;
        const auto right_support = right.reach.is_supported ? 0 : 1;
        if (left_support != right_support)
          return left_support < right_support;
        if (left.priority_band != right.priority_band)
          return left.priority_band < right.priority_band;
        const bool left_nan = std::isnan(left.distance_from_fleet);
        const bool right_nan = std::isnan(right.distance_from_fleet);
        if (left_nan != right_nan)
          return left_nan;
        if (!left_nan && left.distance_from_fleet != right.distance_from_fleet)
          return left.distance_from_fleet < right.distance_from_fleet;
        return left.system_id < right.system_id;
      });
  if (candidates.size() > static_cast<std::size_t>(maximum_candidates))
    candidates.resize(static_cast<std::size_t>(maximum_candidates));
  const auto count = candidates.size();
  auto status = count == 0
                    ? "No remaining survey work is available to this vessel."
                    : std::to_string(count) + " survey target" +
                          (count == 1 ? "" : "s") +
                          " available in the current planning window.";
  return {fleet->id, fleet->name,       fleet->role,
          true,      std::move(status), std::move(candidates)};
}

std::optional<ExplorationMissionCandidate>
ExplorationMissionPlanner::select_supported_candidate(
    ExplorationPlanningWorldView world, const FleetState &fleet,
    MissionFuelPolicy fuel_policy,
    const std::unordered_set<int> &reservation_set,
    bool &used_shared_fallback) const {
  used_shared_fallback = false;
  if(fuel_policy!=MissionFuelPolicy::ReachDestination&&!uses_canonical_reach_)
    throw std::invalid_argument("Return fuel planning requires the canonical operational reach provider.");
  // Match build_plan: the stored fleet — not the caller's reference — is the
  // planning subject, and a missing/inactive/wrong-role fleet has no plan.
  const auto stored =
      std::find_if(world.fleets.begin(), world.fleets.end(),
                   [&fleet](const auto &candidate) {
                     return candidate.id == fleet.id && candidate.is_active;
                   });
  if (stored == world.fleets.end() ||
      (stored->role != FleetRole::Scout && stored->role != FleetRole::Science))
    return std::nullopt;
  const auto &subject = *stored;
  std::unordered_map<int, const StellarSystem *> systems_index;
  systems_index.reserve(world.systems.size());
  for (const auto &system : world.systems)
    systems_index.emplace(system.id, &system);
  std::optional<OperationalReachBatch> batch;
  if(uses_canonical_reach_)batch.emplace(OperationalReachWorldView{world.systems,world.colonies,world.lanes},subject.civilization_id);

  // Light ranking identical to the build_plan comparator restricted to the
  // supported subsequence: (priority band, distance with NaN last, id).
  struct RankedTarget {
    const StellarSystem *system;
    int priority_band;
    double distance;
  };
  std::vector<RankedTarget> targets;
  targets.reserve(world.systems.size());
  for (const auto &system : world.systems) {
    if (!needs_survey_work(world.knowledge, subject, system.id))
      continue;
    targets.push_back(
        {&system,
         survey_priority(
             subject.role,
             world.knowledge.system_survey_level(subject.civilization_id,
                                                 system.id)),
         indexed_distance_from_fleet(systems_index, subject, system)});
  }
  std::stable_sort(targets.begin(), targets.end(),
                   [](const RankedTarget &left, const RankedTarget &right) {
                     if (left.priority_band != right.priority_band)
                       return left.priority_band < right.priority_band;
                     const bool left_nan = std::isnan(left.distance);
                     const bool right_nan = std::isnan(right.distance);
                     if (left_nan != right_nan)
                       return left_nan;
                     if (!left_nan && left.distance != right.distance)
                       return left.distance < right.distance;
                     return left.system->id < right.system->id;
                   });

  // Assess lazily in plan order. The plan truncates to
  // hard_maximum_candidates entries with supported candidates first, so only
  // the first hard_maximum_candidates supported targets can be selected.
  const StellarSystem *first_supported = nullptr;
  const StellarSystem *chosen = nullptr;
  int supported_seen = 0;
  for (const auto &target : targets) {
    if (supported_seen >= hard_maximum_candidates)
      break;
    const auto reach =
        batch ? batch->assess(subject, target.system->id,
                              mission_kind(subject.role), fuel_policy)
              : assess_operational_reach(world, subject, target.system->id);
    if (!reach.is_supported)
      continue;
    ++supported_seen;
    if (!first_supported)
      first_supported = target.system;
    if (!reservation_set.contains(target.system->id)) {
      chosen = target.system;
      break;
    }
  }
  if (!chosen) {
    if (!first_supported)
      return std::nullopt;
    chosen = first_supported;
    used_shared_fallback = true;
  }
  SurveyOperationsBatch surveys(world.systems, world.bodies);
  return build_candidate(world, subject, *chosen, batch ? &*batch : nullptr,
                         fuel_policy, &surveys, &systems_index);
}

ExplorationMissionOrderAssessment
ExplorationMissionPlanner::assess_order(ExplorationPlanningWorldView world,
                                        int fleet_id, int destination_system_id,
                                        bool require_survey_work) const {
  const auto fleet =
      std::find_if(world.fleets.begin(), world.fleets.end(),
                   [fleet_id](const auto &candidate) {
                     return candidate.id == fleet_id && candidate.is_active;
                   });
  if (fleet == world.fleets.end())
    return rejected(
        "No active exploration vessel with that fleet ID is available.");
  if (fleet->role != FleetRole::Scout && fleet->role != FleetRole::Science)
    return rejected(fleet->name + " is not a scout or science survey vessel.");
  const auto system =
      std::find_if(world.systems.begin(), world.systems.end(),
                   [destination_system_id](const auto &candidate) {
                     return candidate.id == destination_system_id;
                   });
  if (system == world.systems.end())
    return rejected("Unknown astronomical target.");
  if (require_survey_work &&
      !needs_survey_work(world.knowledge, *fleet, destination_system_id)) {
    return rejected(
        fleet->role == FleetRole::Scout
            ? system->name + " already has reconnaissance-grade survey "
                             "coverage."
            : system->name + " already has a completed detailed science "
                             "survey.");
  }
  auto candidate = build_candidate(world, *fleet, *system);
  if (!candidate.reach.is_supported) {
    auto reason = candidate.reach.reason;
    return rejected(std::move(reason), std::move(candidate));
  }
  const bool local = fleet->current_system_id == destination_system_id &&
                     !fleet->destination_system_id;
  if (!needs_survey_work(world.knowledge, *fleet, destination_system_id)) {
    auto message =
        local ? fleet->name + " is already on station in " + system->name + "."
              : fleet->name + ": course set for " + system->name + ". " +
                    candidate.reach.reason;
    return approved(std::move(message), std::move(candidate), local);
  }
  std::string action;
  if (local)
    action = fleet->role == FleetRole::Scout ? "reconnaissance pass"
                                             : "detailed science survey";
  else
    action = fleet->role == FleetRole::Scout ? "reconnaissance mission"
                                             : "science-survey mission";
  auto message = local ? fleet->name + " is ready to begin the " + action +
                             " in " + system->name + "."
                       : fleet->name + ": " + action + " approved for " +
                             system->name + ". " + candidate.reach.reason;
  return approved(std::move(message), std::move(candidate), local);
}

ExplorationAiMissionCoordinator::ExplorationAiMissionCoordinator(
    const ExplorationMissionPlanner &mission_planner)
    : mission_planner_(mission_planner) {}

ExplorationAiMissionSelection ExplorationAiMissionCoordinator::select_mission(
    ExplorationPlanningWorldView world, const FleetState &fleet,MissionFuelPolicy fuel_policy) const {
  if (!fleet.is_active ||
      (fleet.role != FleetRole::Scout && fleet.role != FleetRole::Science))
    return {fleet.id,
            std::nullopt,
            false,
            {},
            "Only active scout/science fleets participate in AI exploration "
            "coordination."};
  std::unordered_set<int> reservation_set;
  for (const auto &other : world.fleets) {
    if (!other.is_active || other.id == fleet.id ||
        other.civilization_id != fleet.civilization_id ||
        (other.role != FleetRole::Scout && other.role != FleetRole::Science))
      continue;
    if (other.destination_system_id) {
      reservation_set.insert(*other.destination_system_id);
      continue;
    }
    if (other.current_system_id &&
        ExplorationMissionPlanner::needs_survey_work(world.knowledge, other,
                                                     *other.current_system_id))
      reservation_set.insert(*other.current_system_id);
  }
  bool shared = false;
  auto selected = mission_planner_.select_supported_candidate(
      world, fleet, fuel_policy, reservation_set, shared);
  if (!selected)
    return {fleet.id,
            std::nullopt,
            false,
            {},
            "No supported survey work is currently available."};
  std::vector<int> reservations(reservation_set.begin(), reservation_set.end());
  std::sort(reservations.begin(), reservations.end());
  return {fleet.id, *selected, shared, std::move(reservations),
          shared
              ? "All supported targets in the bounded planning window are "
                "already reserved; sharing " +
                    selected->catalog_name + "."
              : "Selected unreserved target " + selected->catalog_name + "."};
}

} // namespace stellar::core
