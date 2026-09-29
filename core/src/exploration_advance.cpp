#include <stellar/core/exploration_advance.hpp>

#include <stellar/core/civilian_recovery.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/interstellar_distance.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace stellar::core {
namespace {

double min_preserving_nan(double first, double second) {
  return std::isnan(first) || std::isnan(second)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(first, second);
}

double max_preserving_nan(double first, double second) {
  return std::isnan(first) || std::isnan(second)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(first, second);
}

double clamp_preserving_nan(double value, double minimum, double maximum) {
  return std::isnan(value) ? value : std::clamp(value, minimum, maximum);
}

Vec2 chart_position(const StellarSystem &system) {
  return {system.position.x, system.position.y};
}

float chart_distance(Vec2 first, Vec2 second) {
  const float dx = first.x - second.x;
  const float dy = first.y - second.y;
  return std::sqrt(dx * dx + dy * dy);
}

Vec2 advance_towards(Vec2 position, Vec2 target, float distance) {
  const float dx = target.x - position.x;
  const float dy = target.y - position.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  return {position.x + dx / length * distance,
          position.y + dy / length * distance};
}

std::string target_ordinal(int system_id) {
  const auto wrapped = std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(system_id) + std::uint32_t{1});
  const auto magnitude = wrapped < 0 ? -static_cast<std::int64_t>(wrapped)
                                     : static_cast<std::int64_t>(wrapped);
  auto digits = std::to_string(magnitude);
  if (digits.size() < 3)
    digits.insert(digits.begin(), 3 - digits.size(), '0');
  return wrapped < 0 ? "-" + digits : digits;
}

// Per-advance lookup tables. The world view's flat system/body/colony spans
// are immutable for the duration of a step, so the id- and membership-scans
// the helpers below perform resolve in O(1) instead of O(systems) or
// O(bodies) per fleet per tick — identical results, just indexed.
class AdvanceIndex {
public:
  explicit AdvanceIndex(std::span<const StellarSystem> systems) {
    systems_by_id_.reserve(systems.size());
    for (const auto &system : systems)
      systems_by_id_.emplace(system.id, &system);
  }

  const StellarSystem *system(int id) const {
    const auto found = systems_by_id_.find(id);
    return found == systems_by_id_.end() ? nullptr : found->second;
  }

  const StellarSystem &required_system(int id) const {
    const auto *found = system(id);
    if (!found)
      throw std::runtime_error("Sequence contains no matching element");
    return *found;
  }

  const std::vector<const PlanetaryBody *> &
  bodies_for(std::span<const PlanetaryBody> bodies, int system_id) {
    if (!bodies_ready_) {
      for (const auto &body : bodies)
        bodies_by_system_[body.system_id].push_back(&body);
      bodies_ready_ = true;
    }
    static const std::vector<const PlanetaryBody *> empty;
    const auto found = bodies_by_system_.find(system_id);
    return found == bodies_by_system_.end() ? empty : found->second;
  }

  double refueling(std::span<const Colony> colonies, int civilization_id,
                   int system_id) {
    if (!refueling_ready_) {
      for (const auto &colony : colonies) {
        const double service =
            colony.kind == SettlementKind::Colony
                ? 1.0
                : colony.kind == SettlementKind::ResourceOutpost ? 0.5 : 0.0;
        auto &value = refueling_[key(colony.civilization_id,
                                     colony.system_id)];
        if (service > value)
          value = service;
      }
      refueling_ready_ = true;
    }
    const auto found = refueling_.find(key(civilization_id, system_id));
    return found == refueling_.end() ? 0.0 : found->second;
  }

  // Lazily sorted-by-x catalog for sensor-radius queries. Building it
  // validates every catalog position — the same validation the original
  // full-catalog distance scan applied to every system on every call.
  const std::vector<const StellarSystem *> &
  systems_by_x(std::span<const StellarSystem> systems) {
    if (!by_x_ready_) {
      by_x_.reserve(systems.size());
      for (const auto &system : systems) {
        const auto &position = system.position;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            (position.depth_light_years &&
             !std::isfinite(*position.depth_light_years)))
          throw std::invalid_argument(
              "Star position must contain finite light-year coordinates");
        by_x_.push_back(&system);
      }
      std::stable_sort(by_x_.begin(), by_x_.end(),
                       [](const auto *first, const auto *second) {
                         return first->position.x < second->position.x;
                       });
      by_x_ready_ = true;
    }
    return by_x_;
  }

private:
  static std::int64_t key(int civilization_id, int system_id) {
    return (static_cast<std::int64_t>(civilization_id) << 32) |
           static_cast<std::uint32_t>(system_id);
  }

  std::unordered_map<int, const StellarSystem *> systems_by_id_;
  std::unordered_map<int, std::vector<const PlanetaryBody *>>
      bodies_by_system_;
  std::unordered_map<std::int64_t, double> refueling_;
  std::vector<const StellarSystem *> by_x_;
  bool bodies_ready_{};
  bool refueling_ready_{};
  bool by_x_ready_{};
};

const StellarSystem &first_system(const AdvanceIndex &index, int id) {
  return index.required_system(id);
}

const StellarSystem *first_system_or_null(const AdvanceIndex &index,
                                          std::optional<int> id) {
  return id ? index.system(*id) : nullptr;
}

const Civilization &first_civilization(
    std::span<const Civilization> civilizations, int id) {
  const auto found = std::find_if(civilizations.begin(), civilizations.end(),
                                  [=](const auto &value) {
                                    return value.id == id;
                                  });
  if (found == civilizations.end())
    throw std::runtime_error("Sequence contains no matching element");
  return *found;
}

bool is_survey_fleet(const FleetState &fleet) {
  return fleet.role == FleetRole::Scout || fleet.role == FleetRole::Science;
}

double refueling_service_level(AdvanceIndex &index,
                               ExplorationAdvanceWorldView world,
                               int civilization_id, int system_id) {
  return index.refueling(world.colonies, civilization_id, system_id);
}

std::vector<const PlanetaryBody *>
ordered_bodies(AdvanceIndex &index, ExplorationAdvanceWorldView world,
               int system_id) {
  const auto &matching = index.bodies_for(world.bodies, system_id);
  std::vector<const PlanetaryBody *> result(matching.begin(),
                                            matching.end());
  std::stable_sort(result.begin(), result.end(), [](const auto *first,
                                                    const auto *second) {
    return first->id < second->id;
  });
  return result;
}

void emit_reconnaissance_signatures(AdvanceIndex &index,
                                    ExplorationAdvanceWorldView world,
                                    const FleetState &fleet, int system_id,
                                    std::vector<ExplorationEvent> &events) {
  for (const auto *body : ordered_bodies(index, world, system_id)) {
    if (body->has_rare_resource)
      events.push_back({ExplorationEventType::ResourceSignatureDetected,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name +
                            " detected an unusual resource signature near " +
                            body->name +
                            "; detailed survey is required to confirm it.",
                        {}, body->id});
    if (body->has_anomaly)
      events.push_back({ExplorationEventType::AnomalySignatureDetected,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name + " detected an anomalous signature near " +
                            body->name + "; its nature remains unconfirmed.",
                        {}, body->id});
    if (body->has_pre_warp_civilization)
      events.push_back({ExplorationEventType::ActivitySignatureDetected,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name +
                            " detected unresolved activity signatures from " +
                            body->name +
                            "; detailed survey is required before classification.",
                        {}, body->id});
  }
}

void emit_confirmed_discoveries(AdvanceIndex &index,
                                ExplorationAdvanceWorldView world,
                                const FleetState &fleet, int system_id,
                                std::vector<ExplorationEvent> &events) {
  for (const auto *body : ordered_bodies(index, world, system_id)) {
    if (body->has_anomaly)
      events.push_back({ExplorationEventType::AnomalySurveyed,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name + " confirmed an anomaly on or near " +
                            body->name + ".",
                        {}, body->id});
    if (body->has_rare_resource)
      events.push_back({ExplorationEventType::ResourceSurveyed,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name +
                            " confirmed a rare-resource deposit or signature "
                            "associated with " +
                            body->name + ".",
                        {}, body->id});
    if (body->has_pre_warp_civilization)
      events.push_back({ExplorationEventType::NativeCivilizationSurveyed,
                        fleet.civilization_id, fleet.id, system_id,
                        fleet.name +
                            " confirmed a native pre-warp civilization on " +
                            body->name + ".",
                        {}, body->id});
  }
}

void detect_civilization_contacts(ExplorationAdvanceWorldView world,
                                  const FleetState &fleet,
                                  std::vector<ExplorationEvent> &events) {
  if (!fleet.current_system_id)
    return;
  for (const auto &other : world.civilizations) {
    if (other.id == fleet.civilization_id ||
        world.knowledge.is_civilization_known(fleet.civilization_id, other.id))
      continue;
    const bool colony_present =
        std::ranges::any_of(world.colonies, [&](const auto &colony) {
          return colony.civilization_id == other.id &&
                 colony.system_id == *fleet.current_system_id;
        });
    const bool fleet_present =
        std::ranges::any_of(world.fleets, [&](const auto &candidate) {
          return candidate.is_active && candidate.civilization_id == other.id &&
                 candidate.current_system_id == fleet.current_system_id;
        });
    if (!colony_present && !fleet_present)
      continue;
    world.knowledge.reveal_civilization(fleet.civilization_id, other.id);
    events.push_back({ExplorationEventType::FirstContact,
                      fleet.civilization_id, fleet.id,
                      *fleet.current_system_id,
                      "First contact: " + other.name + ".", other.id, {}});
  }
}

std::string hazard_name(SurveyOperationalHazard hazard) {
  switch (hazard) {
  case SurveyOperationalHazard::Routine:
    return "routine";
  case SurveyOperationalHazard::Elevated:
    return "elevated";
  case SurveyOperationalHazard::Severe:
    return "severe";
  }
  return std::to_string(static_cast<int>(hazard));
}

bool process_local_survey(AdvanceIndex &index,
                          ExplorationAdvanceWorldView world, FleetState &fleet,
                          int system_id, double simulation_delta,
                          SurveyOperationsBatch &surveys,
                          std::vector<ExplorationEvent> &events) {
  const auto& location=first_system(index,system_id);
  const auto scan_effort=phenomenon_context(world.phenomena,location.position.x,location.position.y,system_id).effects.scanning;
  simulation_delta/=scan_effort;
  if (fleet.role == FleetRole::Scout) {
    if (world.knowledge.system_survey_level(fleet.civilization_id, system_id) >=
        SystemSurveyLevel::partially_surveyed)
      return false;
    if (fleet.reconnaissance_system_id != system_id) {
      fleet.reconnaissance_system_id = system_id;
      fleet.reconnaissance_days_completed = 0;
    }
    fleet.reconnaissance_days_completed = min_preserving_nan(
        ExplorationSimulation::scout_reconnaissance_days,
        fleet.reconnaissance_days_completed + simulation_delta);
    if (fleet.reconnaissance_days_completed + 1e-9 <
        ExplorationSimulation::scout_reconnaissance_days)
      return true;
    if (!world.knowledge.record_reconnaissance(
            fleet.civilization_id, system_id,
            ExplorationSimulation::scout_reconnaissance_progress))
      return false;
    const auto &system = first_system(index, system_id);
    const auto profile = surveys.build(system_id);
    events.push_back({
        ExplorationEventType::SystemReconnoitered, fleet.civilization_id,
        fleet.id, system_id,
        fleet.name + " completed a rapid reconnaissance pass of " +
            system.name + "; estimated detailed survey effort is " +
            detail::legacy_custom_fixed(profile.estimated_science_survey_days*scan_effort,
                                        0, 1) +
            " days (" + hazard_name(profile.operational_hazard) +
            " survey conditions)."});
    emit_reconnaissance_signatures(index, world, fleet, system_id, events);
    return true;
  }

  if (fleet.role != FleetRole::Science ||
      world.knowledge.is_system_fully_surveyed(fleet.civilization_id,
                                               system_id))
    return false;
  const auto previous_level =
      world.knowledge.system_survey_level(fleet.civilization_id, system_id);
  const auto previous_progress =
      world.knowledge.system_survey_progress(fleet.civilization_id, system_id);
  const auto profile = surveys.build(system_id);
  const auto completed = world.knowledge.advance_system_survey(
      fleet.civilization_id, system_id,
      profile.progress_per_day() * simulation_delta);
  const auto current_progress =
      world.knowledge.system_survey_progress(fleet.civilization_id, system_id);
  const auto current_level =
      world.knowledge.system_survey_level(fleet.civilization_id, system_id);
  if (current_progress <= previous_progress + 0.0000001)
    return false;
  const auto &system = first_system(index, system_id);
  if (previous_level < SystemSurveyLevel::partially_surveyed) {
    events.push_back({ExplorationEventType::SystemSurveyStarted,
                      fleet.civilization_id, fleet.id, system_id,
                      fleet.name + " began a detailed science survey of " +
                          system.name + "; estimated total effort is " +
                          detail::legacy_custom_fixed(
                              profile.estimated_science_survey_days*scan_effort, 0, 1) +
                          " days."});
    if (current_level == SystemSurveyLevel::partially_surveyed)
      emit_reconnaissance_signatures(index, world, fleet, system_id, events);
  }
  if (completed) {
    events.push_back({ExplorationEventType::SystemSurveyed,
                      fleet.civilization_id, fleet.id, system_id,
                      fleet.name + " completed a detailed survey of " +
                          system.name + "."});
    emit_confirmed_discoveries(index, world, fleet, system_id, events);
  }
  return true;
}

bool handle_inbound(AdvanceIndex &index, ExplorationAdvanceWorldView world,
                    FleetState &fleet, const StellarSystem &target,
                    std::vector<ExplorationEvent> &events) {
  const auto service = refueling_service_level(
      index, world, fleet.civilization_id, target.id);
  if (service > 0)
    fleet.fuel_remaining_light_years = max_preserving_nan(
        fleet.fuel_remaining_light_years,
        fleet.fuel_capacity_light_years * service);
  const auto already_known =
      world.knowledge.is_system_known(fleet.civilization_id, target.id);
  const auto range =
      fleet.sensor_range*phenomenon_context(world.phenomena,target.position.x,target.position.y,target.id).effects.sensor;
  // Same contract as reveal_within_sensor_range: reveal every catalog
  // system whose charted distance is within range, count the new reveals.
  // The x-sorted index only prefilters candidates; acceptance still uses
  // squared_distance_light_years, so the reveal set is identical. The
  // band is widened slightly because the 2-D distance is evaluated in
  // float math and |dx| can round across the radius boundary.
  int revealed = 0;
  {
    const double range_squared = static_cast<double>(range) * range;
    const double band =
        std::fabs(static_cast<double>(range)) * 1.0001 + 0.001;
    const double low = static_cast<double>(target.position.x) - band;
    const double high = static_cast<double>(target.position.x) + band;
    const auto &sorted = index.systems_by_x(world.systems);
    const auto first = std::lower_bound(
        sorted.begin(), sorted.end(), low,
        [](const StellarSystem *system, double bound) {
          return static_cast<double>(system->position.x) < bound;
        });
    const auto last = std::upper_bound(
        first, sorted.end(), high,
        [](double bound, const StellarSystem *system) {
          return bound < static_cast<double>(system->position.x);
        });
    for (auto it = first; it != last; ++it)
      if (squared_distance_light_years(target.position, (*it)->position) <=
              range_squared &&
          world.knowledge.reveal_system(fleet.civilization_id, (*it)->id))
        ++revealed;
  }
  if (!already_known)
    events.push_back({ExplorationEventType::SystemDetected,
                      fleet.civilization_id, fleet.id, target.id,
                      fleet.name + " reached astronomical target " +
                          target_ordinal(target.id) +
                          "; detailed system data still requires survey work."});
  if (revealed > 0)
    events.push_back({ExplorationEventType::SensorContact,
                      fleet.civilization_id, fleet.id, target.id,
                      "Sensors added " + std::to_string(revealed) + " system" +
                          (revealed == 1 ? "" : "s") +
                          " to the local chart."});
  detect_civilization_contacts(world, fleet, events);
  if (!fleet.return_to_base_requested)
    return false;
  (void)activate_queued_civilian_return_at_system(
      {world.systems, world.colonies, world.fleets, world.lanes}, fleet);
  return true;
}

} // namespace

ExplorationSimulation::ExplorationSimulation(
    ExplorationReachAssessment operational_reach,MissionFuelPolicy ai_fuel_policy)
    : mission_planner_(std::move(operational_reach)),ai_fuel_policy_(ai_fuel_policy) {
  if(ai_fuel_policy_==MissionFuelPolicy::RetainReturnToService&&!mission_planner_.uses_canonical_reach())
    throw std::invalid_argument("Return fuel planning requires the canonical operational reach provider.");
}

MissionReachAssessment ExplorationSimulation::assess_operational_reach(
    ExplorationPlanningWorldView world, int fleet_id,
    int destination_system_id) const {
  const auto fleet = std::find_if(world.fleets.begin(), world.fleets.end(),
                                  [=](const auto &candidate) {
                                    return candidate.id == fleet_id &&
                                           candidate.is_active;
                                  });
  if (fleet == world.fleets.end())
    return unsupported_mission_reach(
        "No active fleet with that identity is available.");
  return mission_planner_.assess_operational_reach(world, *fleet,
                                                   destination_system_id);
}

ExplorationMissionOrderAssessment ExplorationSimulation::issue_travel_order(
    ExplorationOrderWorldView world, const int fleet_id,
    const int destination_system_id) const {
  return issue_order(world, fleet_id, destination_system_id, false);
}

ExplorationMissionOrderAssessment ExplorationSimulation::issue_survey_order(
    ExplorationOrderWorldView world, const int fleet_id,
    const int destination_system_id) const {
  return issue_order(world, fleet_id, destination_system_id, true);
}

ExplorationMissionOrderAssessment ExplorationSimulation::issue_order(
    ExplorationOrderWorldView world, const int fleet_id,
    const int destination_system_id, const bool require_survey_work) const {
  auto assessment = mission_planner_.assess_order(
      world.planning(), fleet_id, destination_system_id, require_survey_work);
  if (!assessment.accepted)
    return assessment;

  const auto fleet =
      std::find_if(world.fleets.begin(), world.fleets.end(),
                   [fleet_id](const auto &candidate) {
                     return candidate.id == fleet_id && candidate.is_active;
                   });
  if (fleet == world.fleets.end())
    throw std::logic_error(
        "Accepted exploration order no longer has an active fleet.");
  if (assessment.is_local_survey) {
    clear_fleet_route(*fleet);
    return assessment;
  }
  if (!assessment.candidate)
    throw std::logic_error(
        "Accepted exploration travel order has no route candidate.");
  assign_fleet_route(world.reach(), *fleet, destination_system_id,
                     assessment.candidate->reach);
  return assessment;
}

std::vector<ExplorationEvent>
ExplorationSimulation::advance(ExplorationAdvanceWorldView world,
                               double simulation_delta) const {
  if (!std::isfinite(simulation_delta) || simulation_delta < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'simulationDelta')");
  if (simulation_delta <= 0)
    return {};

  std::vector<ExplorationEvent> events;
  AdvanceIndex index(world.systems);
  // One planning index for the whole advance: the catalog spans are
  // immutable here and its survey-work lists revalidate through the
  // knowledge survey-level revision when a survey completes mid-tick.
  ExplorationPlanningSharedIndex planning_shared;
  SurveyOperationsBatch surveys(world.systems, world.bodies,
                                &planning_shared.catalog);
  for (auto &fleet : world.fleets) {
    if (!fleet.is_active)
      continue;
    const auto &civilization =
        first_civilization(world.civilizations, fleet.civilization_id);
    const auto capacity =
        civilization_operating_funding(world.economies, fleet.civilization_id);
    if (capacity <= 0.0000001)
      continue;
    if (fleet.current_system_id) {
      const auto service = refueling_service_level(
          index, world, fleet.civilization_id, *fleet.current_system_id);
      if (service > 0)
        fleet.fuel_remaining_light_years = max_preserving_nan(
            fleet.fuel_remaining_light_years,
            fleet.fuel_capacity_light_years * service);
    }
    if (fleet.hold_requested && fleet.current_system_id &&
        fleet.transit_phase != FleetTransitPhase::InterstellarWarp)
      continue;

    if (fleet.transit_phase == FleetTransitPhase::None &&
        !fleet.destination_system_id && is_survey_fleet(fleet) &&
        fleet.current_system_id &&
        process_local_survey(index, world, fleet, *fleet.current_system_id,
                             simulation_delta * capacity, surveys,
                             events)) {
      detect_civilization_contacts(world, fleet, events);
      continue;
    }

    if (fleet.transit_phase == FleetTransitPhase::None &&
        !fleet.destination_system_id && civilization_uses_ai(civilization,world.control) &&
        is_survey_fleet(fleet)) {
      ExplorationAiMissionCoordinator coordinator(mission_planner_);
      const auto selection = coordinator.select_mission(
          {world.systems, world.bodies, world.fleets, world.colonies,
           world.knowledge, world.lanes},
          fleet,ai_fuel_policy_,&planning_shared);
      if (selection.candidate)
        assign_fleet_route({world.systems, world.colonies, world.lanes}, fleet,
                           selection.candidate->system_id,
                           selection.candidate->reach);
      else if(ai_fuel_policy_==MissionFuelPolicy::RetainReturnToService&&fleet.current_system_id&&
          refueling_service_level(index,world,fleet.civilization_id,*fleet.current_system_id)<=0){
        const auto recovery=request_civilian_fleet_return(
            {world.systems,world.colonies,world.fleets,world.lanes},fleet.civilization_id,fleet.id);
        // Do not manufacture fuel for old saves that are already stranded.
        // Persist an actionable explanation and retry when the world changes.
        if(!recovery.accepted)fleet.return_to_base_failure_reason=recovery.message;
      }
    }

    if (!fleet.destination_system_id &&
        fleet.transit_phase == FleetTransitPhase::LocalArrival &&
        fleet.current_system_id) {
      (void)advance_fleet_local_transit(fleet, simulation_delta * capacity);
      if (fleet_local_transit_complete(fleet)) {
        fleet.transit_phase = FleetTransitPhase::None;
        fleet.transit_origin_system_id.reset();
        fleet.transit_target_system_id.reset();
        fleet.transit_progress = 0;
        if(fleet.stellar_transit_path.empty())fleet.local_transit_position = {};
      }
      continue;
    }
    if (!fleet.destination_system_id)
      continue;

    if (!fleet.current_system_id &&
        fleet.transit_phase == FleetTransitPhase::None) {
      fleet.transit_phase = FleetTransitPhase::InterstellarWarp;
      fleet.transit_target_system_id =
          !fleet.planned_route_system_ids.empty()
              ? std::optional<int>(fleet.planned_route_system_ids.front())
              : fleet.destination_system_id;
    }

    auto remaining_days = simulation_delta * capacity;
    while (fleet.destination_system_id && remaining_days > 0.0000001) {
      const auto movement_target_id =
          !fleet.planned_route_system_ids.empty()
              ? fleet.planned_route_system_ids.front()
              : *fleet.destination_system_id;
      const auto &target = first_system(index, movement_target_id);
      if (fleet.transit_phase == FleetTransitPhase::None) {
        if (!fleet.current_system_id)
          break;
        const auto &origin =
            first_system(index, *fleet.current_system_id);
        fleet.position = chart_position(origin);
        fleet.transit_origin_system_id = origin.id;
        fleet.transit_target_system_id = target.id;
        begin_fleet_local_transit(
            fleet, FleetTransitPhase::LocalDeparture,
            finite_fleet_chart_position(fleet.local_transit_position)
                ? fleet.local_transit_position
                : Vec2{},
            fleet_gate_towards(chart_position(target), chart_position(origin)),
            origin.stellar_object?&*origin.stellar_object:nullptr);
      }

      if (fleet.transit_phase == FleetTransitPhase::LocalDeparture ||
          fleet.transit_phase == FleetTransitPhase::LocalArrival) {
        const auto spent =
            advance_fleet_local_transit(fleet, remaining_days);
        remaining_days -= spent;
        if (!fleet_local_transit_complete(fleet))
          break;
        if (fleet.transit_phase == FleetTransitPhase::LocalDeparture) {
          fleet.transit_phase = FleetTransitPhase::InterstellarWarp;
          fleet.transit_progress = 0;
          fleet.current_system_id.reset();
          continue;
        }
        const auto final_arrival =
            target.id == fleet.destination_system_id &&
            fleet.planned_route_system_ids.size() <= 1;
        if (!final_arrival) {
          if (!fleet.planned_route_system_ids.empty())
            fleet.planned_route_system_ids.erase(
                fleet.planned_route_system_ids.begin());
          fleet.transit_origin_system_id = target.id;
          fleet.transit_target_system_id =
              !fleet.planned_route_system_ids.empty()
                  ? std::optional<int>(fleet.planned_route_system_ids.front())
                  : fleet.destination_system_id;
          fleet.transit_phase = FleetTransitPhase::InterstellarWarp;
          fleet.transit_progress = 0;
          fleet.current_system_id.reset();
          continue;
        }
        fleet.transit_phase = FleetTransitPhase::None;
        fleet.transit_origin_system_id.reset();
        fleet.transit_target_system_id.reset();
        fleet.transit_progress = 0;
        fleet.position = chart_position(target);
        if (!fleet.planned_route_system_ids.empty())
          fleet.planned_route_system_ids.erase(
              fleet.planned_route_system_ids.begin());
        const auto reached_final = target.id == fleet.destination_system_id &&
                                   fleet.planned_route_system_ids.empty();
        if (reached_final && !fleet.hold_requested &&
            !fleet.return_to_base_requested)
          fleet.destination_system_id.reset();
        if (fleet.hold_requested)
          break;
        continue;
      }

      const auto *physical_origin = first_system_or_null(
          index, fleet.transit_origin_system_id);
      auto full_distance =
          physical_origin
              ? distance_light_years(physical_origin->position, target.position)
              : 0.0;
      const auto progress =
          clamp_preserving_nan(fleet.transit_progress, 0.0, 1.0);
      if (full_distance <= 0.000001 && physical_origin)
        full_distance = chart_distance(chart_position(*physical_origin),
                                       chart_position(target));
      const auto distance =
          physical_origin
              ? full_distance * (1.0 - progress)
              : static_cast<double>(
                    chart_distance(fleet.position, chart_position(target)));
      const auto available_distance = min_preserving_nan(
          fleet.strategic_speed * remaining_days,
          fleet.fuel_remaining_light_years);
      if (available_distance <= 0 && distance > 0.000001F)
        break;
      if (distance > available_distance) {
        const auto next_progress =
            !physical_origin || full_distance <= 0.000001
                ? progress
                : clamp_preserving_nan(
                      progress + available_distance / full_distance, 0.0, 1.0);
        fleet.position =
            !physical_origin
                ? advance_towards(fleet.position, chart_position(target),
                                  static_cast<float>(available_distance))
                : interpolate_chart_position(*physical_origin, target,
                                             next_progress);
        fleet.fuel_remaining_light_years -= available_distance;
        fleet.transit_progress =
            !physical_origin || full_distance <= 0.000001 ? 0 : next_progress;
        break;
      }

      const auto warp_days =
          distance / max_preserving_nan(0.1, fleet.strategic_speed);
      remaining_days -= warp_days;
      fleet.fuel_remaining_light_years = max_preserving_nan(
          0.0, fleet.fuel_remaining_light_years - distance);
      const auto arrival_approach = fleet.position;
      fleet.position = chart_position(target);
      fleet.current_system_id = target.id;
      const auto origin_position = physical_origin
                                       ? chart_position(*physical_origin)
                                       : arrival_approach;
      const auto inbound =
          fleet_gate_towards(origin_position, chart_position(target));
      Vec2 final_target{};
      if (!(target.id == fleet.destination_system_id &&
            fleet.planned_route_system_ids.size() <= 1)) {
        const auto next_id = fleet.planned_route_system_ids.size() > 1
                                 ? fleet.planned_route_system_ids[1]
                                 : *fleet.destination_system_id;
        final_target = fleet_gate_towards(
            chart_position(first_system(index, next_id)),
            chart_position(target));
      }
      begin_fleet_local_transit(fleet, FleetTransitPhase::LocalArrival, inbound,
                                final_target,target.stellar_object?&*target.stellar_object:nullptr);
      if (handle_inbound(index, world, fleet, target, events))
        break;
      if (fleet.hold_requested)
        break;
    }
  }
  return events;
}

} // namespace stellar::core
