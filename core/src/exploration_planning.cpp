#include <stellar/core/exploration_planning.hpp>

#include "exploration_prof_internal.hpp"

#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <queue>
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

// Mirrors OperationalReachBatch::prepare's refueling projection: the
// civ's (system_id, max service factor) pairs, sorted for a
// content-exact compare in DrainVerdict::matches.
static std::vector<std::pair<int, double>>
drain_refuel_sites(ExplorationPlanningWorldView world, int civilization_id) {
  std::unordered_map<int, double> refueling;
  for (const auto &colony : world.colonies) {
    if (colony.civilization_id != civilization_id)
      continue;
    auto &service = refueling[colony.system_id];
    service = std::max(service,
                       colony.kind == SettlementKind::Colony ? 1.0 : 0.5);
  }
  std::vector<std::pair<int, double>> sites(refueling.begin(),
                                            refueling.end());
  std::sort(sites.begin(), sites.end());
  return sites;
}

bool ExplorationMissionPlanner::DrainVerdict::matches(
    ExplorationPlanningWorldView world, const FleetState &fleet,
    MissionFuelPolicy policy,
    const std::vector<std::pair<int, double>> &sites) const {
  return systems_data == world.systems.data() &&
         systems_size == world.systems.size() && lanes == &world.lanes &&
         civilization_id == fleet.civilization_id && role == fleet.role &&
         fleet.current_system_id.has_value() &&
         origin_system_id == *fleet.current_system_id &&
         fuel_remaining == fleet.fuel_remaining_light_years &&
         fuel_capacity == fleet.fuel_capacity_light_years &&
         leg_range == fleet.maximum_leg_range_light_years &&
         fuel_policy == policy &&
         survey_level_revision == world.knowledge.survey_level_revision() &&
         refuel_sites == sites;
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
    bool &used_shared_fallback,
    ExplorationPlanningSharedIndex *shared) const {
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
  std::unordered_map<int, const StellarSystem *> local_systems_index;
  const std::unordered_map<int, const StellarSystem *> *systems_index;
  if (shared) {
    if (shared->catalog.systems_by_id.empty())
      for (const auto &system : world.systems)
        shared->catalog.systems_by_id.emplace(system.id, &system);
    systems_index = &shared->catalog.systems_by_id;
  } else {
    local_systems_index.reserve(world.systems.size());
    for (const auto &system : world.systems)
      local_systems_index.emplace(system.id, &system);
    systems_index = &local_systems_index;
  }
  std::optional<OperationalReachBatch> local_batch;
  OperationalReachBatch *batch = nullptr;
  if (uses_canonical_reach_)
    batch = shared ? &shared->reach_batch(
                         OperationalReachWorldView{world.systems,
                                                   world.colonies,
                                                   world.lanes},
                         subject.civilization_id)
                   : &local_batch.emplace(
                         OperationalReachWorldView{world.systems,
                                                   world.colonies,
                                                   world.lanes},
                         subject.civilization_id);

  // Cross-call negative memo: a drained select is a pure function of the
  // fields DrainVerdict compares — reservations only veto supported
  // targets and can never create one. Canonical reach path only.
  std::vector<std::pair<int, double>> drain_sites;
  const bool memoize_drain =
      batch != nullptr && subject.current_system_id.has_value();
  if (memoize_drain) {
    drain_sites = drain_refuel_sites(world, subject.civilization_id);
    if (const auto found = drain_verdicts_.find(subject.id);
        found != drain_verdicts_.end() &&
        found->second.matches(world, subject, fuel_policy, drain_sites))
      return std::nullopt;
  }

  // Light ranking identical to the build_plan comparator restricted to the
  // supported subsequence: (priority band, distance with NaN last, id).
  struct RankedTarget {
    const StellarSystem *system;
    int priority_band;
    double distance;
    // Lane slot resolved once per candidate on the canonical path —
    // lets the pop loop probe by slot instead of re-hashing the id.
    int lane_slot;
  };
  const auto ranked_less = [](const RankedTarget &left,
                              const RankedTarget &right) {
    if (left.priority_band != right.priority_band)
      return left.priority_band < right.priority_band;
    const bool left_nan = std::isnan(left.distance);
    const bool right_nan = std::isnan(right.distance);
    if (left_nan != right_nan)
      return left_nan;
    if (!left_nan && left.distance != right.distance)
      return left.distance < right.distance;
    return left.system->id < right.system->id;
  };
  const auto ranked_greater = [&](const RankedTarget &left,
                                 const RankedTarget &right) {
    return ranked_less(right, left);
  };
  // The (system, band) filter depends only on the fleet's civilization and
  // role plus survey LEVELS — memoizable per shared index while the
  // knowledge survey-level revision is unchanged. Distance stays per-fleet.
  auto &prof = detail::expl_prof();
  const bool profiling = prof.enabled.load(std::memory_order_relaxed);
  std::optional<detail::ExplProfScope> prof_build;
  if (profiling)
    prof_build.emplace(prof.select_build_ns);
  std::vector<std::pair<const StellarSystem *, int>> local_work;
  const std::vector<std::pair<const StellarSystem *, int>> *work;
  if (shared) {
    const std::int64_t key =
        (static_cast<std::int64_t>(subject.civilization_id) << 32) |
        static_cast<std::uint32_t>(static_cast<int>(subject.role));
    auto &entry = shared->survey_work[key];
    if (!entry.valid ||
        entry.level_revision != world.knowledge.survey_level_revision()) {
      if (profiling)
        ++prof.work_rebuilds;
      entry.entries.clear();
      for (const auto &system : world.systems)
        if (needs_survey_work(world.knowledge, subject, system.id))
          entry.entries.push_back(
              {&system,
               survey_priority(subject.role,
                               world.knowledge.system_survey_level(
                                   subject.civilization_id, system.id))});
      entry.level_revision = world.knowledge.survey_level_revision();
      entry.valid = true;
    }
    work = &entry.entries;
  } else {
    for (const auto &system : world.systems)
      if (needs_survey_work(world.knowledge, subject, system.id))
        local_work.push_back(
            {&system,
             survey_priority(subject.role,
                             world.knowledge.system_survey_level(
                                 subject.civilization_id, system.id))});
    work = &local_work;
  }
  if (profiling)
    prof.work_entries += static_cast<long long>(work->size());
  // Canonical-reach selects can drop lane-unreachable targets outright:
  // batch->assess resolves them through find_shortest_route's component
  // prune, so they can never be supported — removing them shrinks both
  // the heapify and the pop/assess loop. The reach range is per-fleet
  // constant, so one leg-range component set covers the whole list; the
  // component-count gate keeps the common fully-connected graph at zero
  // per-candidate cost.
  const double prune_range = subject.maximum_leg_range_light_years;
  const bool prune_unreachable =
      batch != nullptr && subject.current_system_id.has_value() &&
      world.lanes.connected_component_count(prune_range) > 1;
  std::vector<RankedTarget> targets;
  targets.reserve(work->size());
  for (const auto &[system, band] : *work) {
    // has_system keeps lane-unknown candidates on the assess path so
    // find_shortest_route's out_of_range contract is preserved exactly.
    if (prune_unreachable && world.lanes.has_system(system->id) &&
        !world.lanes.systems_connected(*subject.current_system_id,
                                       system->id, prune_range))
      continue;
    targets.push_back({system, band,
                       indexed_distance_from_fleet(*systems_index, subject,
                                                   *system),
                       batch ? world.lanes.slot_of_system(system->id) : 0});
  }
  // The (band, distance-with-NaN-last, id) order is total — heap pops yield
  // exactly the stable_sort sequence, but only the handful of entries the
  // lazy assessment below actually consumes pay the ordering cost.
  std::priority_queue<RankedTarget, std::vector<RankedTarget>,
                      decltype(ranked_greater)>
      queue(ranked_greater, std::move(targets));
  if (profiling) {
    prof_build.reset();
    prof_build.emplace(prof.select_loop_ns);
  }

  // Assess lazily in plan order. The plan truncates to
  // hard_maximum_candidates entries with supported candidates first, so only
  // the first hard_maximum_candidates supported targets can be selected.
  const StellarSystem *first_supported = nullptr;
  const StellarSystem *chosen = nullptr;
  int supported_seen = 0;
  while (!queue.empty()) {
    if (supported_seen >= hard_maximum_candidates)
      break;
    const RankedTarget target = queue.top();
    queue.pop();
    if (profiling)
      ++prof.pops;
    // Verdict-only on the canonical path — the loop discards everything
    // but is_supported. Slot probes skip the id lookups entirely; the
    // lane-unknown fallback keeps find_shortest_route's out_of_range
    // contract reachable for non-catalog targets.
    const bool supported =
        batch ? (target.lane_slot >= 0
                     ? batch->probe_supported(subject, target.lane_slot,
                                              fuel_policy)
                     : batch->assess(subject, target.system->id,
                                     mission_kind(subject.role),
                                     fuel_policy, /*explain=*/false)
                         .is_supported)
              : assess_operational_reach(world, subject,
                                         target.system->id)
                    .is_supported;
    if (profiling)
      ++prof.assess_calls;
    if (!supported)
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
    if (!first_supported) {
      if (profiling)
        ++prof.drains;
      if (memoize_drain)
        drain_verdicts_[subject.id] = DrainVerdict{
            world.systems.data(),
            world.systems.size(),
            &world.lanes,
            subject.civilization_id,
            subject.role,
            *subject.current_system_id,
            subject.fuel_remaining_light_years,
            subject.fuel_capacity_light_years,
            subject.maximum_leg_range_light_years,
            fuel_policy,
            world.knowledge.survey_level_revision(),
            std::move(drain_sites)};
      return std::nullopt;
    }
    chosen = first_supported;
    used_shared_fallback = true;
  }
  if (profiling) {
    prof_build.reset();
    prof_build.emplace(prof.select_finish_ns);
  }
  SurveyOperationsBatch surveys(world.systems, world.bodies,
                                shared ? &shared->catalog : nullptr);
  return build_candidate(world, subject, *chosen, batch,
                         fuel_policy, &surveys, systems_index);
}

bool ExplorationMissionPlanner::has_supported_mission_target(
    ExplorationPlanningWorldView world, int fleet_id,
    MissionFuelPolicy fuel_policy,
    ExplorationPlanningSharedIndex *shared) const {
  if(fuel_policy!=MissionFuelPolicy::ReachDestination&&!uses_canonical_reach_)
    throw std::invalid_argument("Return fuel planning requires the canonical operational reach provider.");
  // Same subject selection as build_plan: the stored fleet, not a caller
  // reference, and unavailable for missing/inactive/non-survey vessels.
  const auto fleet =
      std::find_if(world.fleets.begin(), world.fleets.end(),
                   [fleet_id](const auto &candidate) {
                     return candidate.id == fleet_id && candidate.is_active;
                   });
  if (fleet == world.fleets.end() ||
      (fleet->role != FleetRole::Scout && fleet->role != FleetRole::Science))
    return false;
  const auto &subject = *fleet;
  std::optional<OperationalReachBatch> local_batch;
  OperationalReachBatch *batch = nullptr;
  if (uses_canonical_reach_)
    batch = shared ? &shared->reach_batch(
                         OperationalReachWorldView{world.systems,
                                                   world.colonies,
                                                   world.lanes},
                         subject.civilization_id)
                   : &local_batch.emplace(
                         OperationalReachWorldView{world.systems,
                                                   world.colonies,
                                                   world.lanes},
                         subject.civilization_id);
  // Same negative memo as select_supported_candidate — "no supported
  // target exists" is exactly the drain verdict, and it is
  // reservation-free by construction here.
  std::vector<std::pair<int, double>> drain_sites;
  const bool memoize_drain =
      batch != nullptr && subject.current_system_id.has_value();
  if (memoize_drain) {
    drain_sites = drain_refuel_sites(world, subject.civilization_id);
    if (const auto found = drain_verdicts_.find(subject.id);
        found != drain_verdicts_.end() &&
        found->second.matches(world, subject, fuel_policy, drain_sites))
      return false;
  }
  // The result is existence-only, so the assess order cannot change it —
  // reuse the memoized (civilization, role) survey-work list and skip the
  // plan-order sort entirely.
  std::vector<std::pair<const StellarSystem *, int>> local_work;
  const std::vector<std::pair<const StellarSystem *, int>> *work;
  if (shared) {
    const std::int64_t key =
        (static_cast<std::int64_t>(subject.civilization_id) << 32) |
        static_cast<std::uint32_t>(static_cast<int>(subject.role));
    auto &entry = shared->survey_work[key];
    if (!entry.valid ||
        entry.level_revision != world.knowledge.survey_level_revision()) {
      entry.entries.clear();
      for (const auto &system : world.systems)
        if (needs_survey_work(world.knowledge, subject, system.id))
          entry.entries.push_back(
              {&system,
               survey_priority(subject.role,
                               world.knowledge.system_survey_level(
                                   subject.civilization_id, system.id))});
      entry.level_revision = world.knowledge.survey_level_revision();
      entry.valid = true;
    }
    work = &entry.entries;
  } else {
    for (const auto &system : world.systems)
      if (needs_survey_work(world.knowledge, subject, system.id))
        local_work.push_back({&system, 0});
    work = &local_work;
  }
  // Same connectivity pre-prune as select_supported_candidate: on the
  // canonical reach path an unreachable candidate can never be supported,
  // so the existence answer is unchanged. The component-count gate keeps
  // fully-connected graphs at zero per-candidate cost.
  const double prune_range = subject.maximum_leg_range_light_years;
  const bool prune_unreachable =
      batch != nullptr && subject.current_system_id.has_value() &&
      world.lanes.connected_component_count(prune_range) > 1;
  for (const auto &[system, ignored_band] : *work) {
    if (prune_unreachable && world.lanes.has_system(system->id) &&
        !world.lanes.systems_connected(*subject.current_system_id,
                                       system->id, prune_range))
      continue;
    bool supported;
    if (batch) {
      const int slot = world.lanes.slot_of_system(system->id);
      supported =
          slot >= 0
              ? batch->probe_supported(subject, slot, fuel_policy)
              : batch->assess(subject, system->id,
                              mission_kind(subject.role), fuel_policy,
                              /*explain=*/false)
                    .is_supported;
    } else {
      supported = assess_operational_reach(world, subject, system->id)
                      .is_supported;
    }
    if (supported)
      return true;
  }
  if (memoize_drain)
    drain_verdicts_[subject.id] = DrainVerdict{
        world.systems.data(),
        world.systems.size(),
        &world.lanes,
        subject.civilization_id,
        subject.role,
        *subject.current_system_id,
        subject.fuel_remaining_light_years,
        subject.fuel_capacity_light_years,
        subject.maximum_leg_range_light_years,
        fuel_policy,
        world.knowledge.survey_level_revision(),
        std::move(drain_sites)};
  return false;
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
    ExplorationPlanningWorldView world, const FleetState &fleet,MissionFuelPolicy fuel_policy,
    ExplorationPlanningSharedIndex *shared) const {
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
  bool shared_fallback = false;
  auto selected = mission_planner_.select_supported_candidate(
      world, fleet, fuel_policy, reservation_set, shared_fallback, shared);
  if (!selected)
    return {fleet.id,
            std::nullopt,
            false,
            {},
            "No supported survey work is currently available."};
  std::vector<int> reservations(reservation_set.begin(), reservation_set.end());
  std::sort(reservations.begin(), reservations.end());
  return {fleet.id, *selected, shared_fallback, std::move(reservations),
          shared_fallback
              ? "All supported targets in the bounded planning window are "
                "already reserved; sharing " +
                    selected->catalog_name + "."
              : "Selected unreserved target " + selected->catalog_name + "."};
}

} // namespace stellar::core
