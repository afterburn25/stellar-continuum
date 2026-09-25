#include "native_outpost_freight_controller.hpp"

#include <stellar/core/colony_operations.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace stellar::native_colony {
namespace {
using namespace stellar::core;

struct Context {
  IntegratedAdaptiveCampaignRuntime &runtime;
  CampaignSimulationState &simulation;
  FreshCampaignState &world;
  int player_id{};
};

Context context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &simulation = runtime.world();
  auto &world = simulation.campaign();
  const auto id = world.player_civilization_id;
  if (std::ranges::count(world.civilizations, id, &Civilization::id) != 1)
    throw std::runtime_error("The campaign has no unique player civilization.");
  const auto player =
      std::ranges::find(world.civilizations, id, &Civilization::id);
  if (player == world.civilizations.end() || !player->is_player ||
      std::ranges::count(world.civilizations, true, &Civilization::is_player) !=
          1)
    throw std::runtime_error("The campaign has no valid player civilization.");
  return {runtime, simulation, world, id};
}

template <class Range, class Member>
typename Range::value_type *unique_by(Range &range, const int id,
                                      Member member) {
  if (std::ranges::count(range, id, member) != 1)
    return nullptr;
  const auto found = std::ranges::find(range, id, member);
  return found == range.end() ? nullptr : &*found;
}

bool finite(const Vec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool valid_fleet_numbers(const FleetState &fleet) {
  return finite(fleet.position) && std::isfinite(fleet.transit_progress) &&
         finite(fleet.local_transit_start) &&
         finite(fleet.local_transit_position) &&
         finite(fleet.local_transit_target) &&
         std::isfinite(fleet.settlement_days_completed) &&
         std::isfinite(fleet.reconnaissance_days_completed) &&
         std::isfinite(fleet.cargo_material_capacity) &&
         fleet.cargo_material_capacity > 0.0 &&
         std::isfinite(fleet.cargo_materials) && fleet.cargo_materials == 0.0 &&
         std::isfinite(fleet.strategic_speed) && fleet.strategic_speed > 0.0 &&
         std::isfinite(fleet.maximum_leg_range_light_years) &&
         fleet.maximum_leg_range_light_years >= 0.0 &&
         std::isfinite(fleet.fuel_capacity_light_years) &&
         fleet.fuel_capacity_light_years >= 0.0 &&
         std::isfinite(fleet.fuel_remaining_light_years) &&
         fleet.fuel_remaining_light_years >= 0.0 &&
         fleet.fuel_remaining_light_years <= fleet.fuel_capacity_light_years &&
         std::isfinite(fleet.sensor_range) && fleet.sensor_range >= 0.0f &&
         std::isfinite(fleet.embarked_population_millions) &&
         fleet.embarked_population_millions >= 0.0;
}

bool idle_freighter(const FleetState &fleet, const int player_id) {
  return fleet.is_active && fleet.civilization_id == player_id &&
         fleet.role == FleetRole::Logistics &&
         fleet.design_id == "bulk_freighter" && fleet.current_system_id &&
         !fleet.destination_system_id &&
         fleet.transit_phase == FleetTransitPhase::None &&
         !fleet.transit_origin_system_id && !fleet.transit_target_system_id &&
         fleet.planned_route_system_ids.empty() &&
         !fleet.destination_planetary_body_id && !fleet.settlement_body_id &&
         !fleet.reconnaissance_system_id && !fleet.freight_target_outpost_id &&
         !fleet.freight_home_colony_id && valid_fleet_numbers(fleet);
}

const Colony *unique_home(const FreshCampaignState &world,
                          const FleetState &fleet, const int player_id) {
  const auto found = std::ranges::find_if(world.colonies, [&](const Colony &c) {
    return c.civilization_id == player_id &&
           c.system_id == *fleet.current_system_id &&
           c.kind == SettlementKind::Colony;
  });
  if (found == world.colonies.end() ||
      std::ranges::count(world.colonies, found->id, &Colony::id) != 1)
    return nullptr;
  return &*found;
}

bool same_vec(const Vec2 a, const Vec2 b) { return a.x == b.x && a.y == b.y; }

bool same_decision_fleet(const FleetState &a, const FleetState &b) {
  return a.id == b.id && a.civilization_id == b.civilization_id &&
         a.name == b.name && a.role == b.role && a.design_id == b.design_id &&
         same_vec(a.position, b.position) &&
         a.current_system_id == b.current_system_id &&
         a.destination_system_id == b.destination_system_id &&
         a.transit_phase == b.transit_phase &&
         a.transit_origin_system_id == b.transit_origin_system_id &&
         a.transit_target_system_id == b.transit_target_system_id &&
         a.transit_progress == b.transit_progress &&
         same_vec(a.local_transit_start, b.local_transit_start) &&
         same_vec(a.local_transit_position, b.local_transit_position) &&
         same_vec(a.local_transit_target, b.local_transit_target) &&
         a.planned_route_system_ids == b.planned_route_system_ids &&
         a.hold_requested == b.hold_requested &&
         a.return_to_base_requested == b.return_to_base_requested &&
         a.return_to_base_failure_reason == b.return_to_base_failure_reason &&
         a.mission_order_revision == b.mission_order_revision &&
         a.destination_planetary_body_id == b.destination_planetary_body_id &&
         a.prevent_automatic_settlement == b.prevent_automatic_settlement &&
         a.settlement_body_id == b.settlement_body_id &&
         a.settlement_days_completed == b.settlement_days_completed &&
         a.reconnaissance_system_id == b.reconnaissance_system_id &&
         a.reconnaissance_days_completed == b.reconnaissance_days_completed &&
         a.freight_target_outpost_id == b.freight_target_outpost_id &&
         a.freight_home_colony_id == b.freight_home_colony_id &&
         a.cargo_material_capacity == b.cargo_material_capacity &&
         a.cargo_materials == b.cargo_materials &&
         a.strategic_speed == b.strategic_speed &&
         a.maximum_leg_range_light_years == b.maximum_leg_range_light_years &&
         a.fuel_capacity_light_years == b.fuel_capacity_light_years &&
         a.fuel_remaining_light_years == b.fuel_remaining_light_years &&
         a.sensor_range == b.sensor_range && a.is_active == b.is_active &&
         a.embarked_population_millions == b.embarked_population_millions &&
         a.embarked_population_species_id == b.embarked_population_species_id;
}

bool valid_operations(const ResourceOutpostOperationsSnapshot &operations) {
  return operations.is_resource_outpost &&
         std::isfinite(operations.extraction_per_day) &&
         operations.extraction_per_day >= 0.0 &&
         std::isfinite(operations.stored_materials) &&
         operations.stored_materials >= 0.0 &&
         std::isfinite(operations.storage_capacity) &&
         operations.storage_capacity >= 0.0 &&
         std::isfinite(operations.remaining_deposit_materials) &&
         operations.remaining_deposit_materials >= 0.0;
}

} // namespace

std::string NativeOutpostFreightController::tr(
    std::string_view key, std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

NativeOutpostFreightOutcome NativeOutpostFreightController::stale() const {
  return {false, tr("FREIGHT_MSG_STALE", "The freight dispatch changed; review it again.")};
}

void NativeOutpostFreightController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native outpost freight control must run on the "
                           "simulation owner thread.");
}

void NativeOutpostFreightController::bind_generation(
    const std::uint64_t generation) {
  if (generation_ && generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace the freight controller.");
  if (generation_ && generation != *generation_)
    quote_.reset();
  generation_ = generation;
}

NativeOutpostFreightPreview
NativeOutpostFreightController::preview(CampaignFrame &frame,
                                        const std::uint64_t generation,
                                        const NativeColonyView &view) {
  require_owner();
  NativeOutpostFreightPreview result;
  result.campaign_generation = generation;
  result.player_civilization_id = view.player_civilization_id;
  result.colony_id = view.colony_id;
  result.body_id = view.body_id;
  result.system_id = view.system_id;
  try {
    bind_generation(generation);
    quote_.reset();
    auto current = context(frame);
    result.player_civilization_id = current.player_id;
    result.colony_id = view.colony_id;
    result.body_id = view.body_id;
    result.system_id = view.system_id;

    if (view.campaign_generation != generation ||
        view.player_civilization_id != current.player_id ||
        !view.resource_outpost) {
      result.message = tr("FREIGHT_MSG_VIEW_STALE", "That resource outpost view is stale.");
      return result;
    }
    const auto *outpost =
        unique_by(current.world.colonies, view.colony_id, &Colony::id);
    const auto *body =
        unique_by(current.world.bodies, view.body_id, &PlanetaryBody::id);
    const auto *system =
        unique_by(current.world.systems, view.system_id, &StellarSystem::id);
    if (!outpost || outpost->civilization_id != current.player_id ||
        outpost->kind != SettlementKind::ResourceOutpost ||
        outpost->system_id != view.system_id ||
        outpost->planetary_body_id != view.body_id || !body ||
        body->system_id != view.system_id || !system ||
        !current.world.knowledge.is_system_fully_surveyed(current.player_id,
                                                          view.system_id)) {
      result.message =
          tr("FREIGHT_MSG_OUTPOST_UNAVAILABLE", "That fully surveyed owned resource outpost is unavailable.");
      return result;
    }
    if (std::ranges::count(current.world.economies, current.player_id,
                           &CivilizationEconomy::civilization_id) != 1) {
      result.message = tr("FREIGHT_MSG_ECONOMY", "The player economy is unavailable or ambiguous.");
      return result;
    }

    ResourceOutpostOperationsSnapshot operations;
    try {
      operations = resource_outpost_snapshot(current.world.bodies,
                                             current.world.economies, *outpost);
    } catch (const std::exception &error) {
      result.message = error.what();
      return result;
    }
    if (!valid_operations(operations)) {
      result.message = tr("FREIGHT_MSG_INVALID_OPS", "The resource outpost has invalid operating values.");
      return result;
    }
    result.outpost_name = outpost->name;
    result.stored_materials = operations.stored_materials;
    result.extraction_per_day = operations.extraction_per_day;

    struct Candidate {
      const FleetState *fleet;
      const Colony *home;
    };
    std::vector<Candidate> candidates;
    for (const auto &candidate : current.world.fleets) {
      if (!idle_freighter(candidate, current.player_id) ||
          std::ranges::count(current.world.fleets, candidate.id,
                             &FleetState::id) != 1)
        continue;
      const auto *candidate_home =
          unique_home(current.world, candidate, current.player_id);
      if (!candidate_home || !candidate_home->planetary_body_id)
        continue;
      const auto *home_body =
          unique_by(current.world.bodies, *candidate_home->planetary_body_id,
                    &PlanetaryBody::id);
      if (!home_body || home_body->system_id != candidate_home->system_id ||
          !unique_by(current.world.systems, candidate_home->system_id,
                     &StellarSystem::id))
        continue;
      candidates.push_back({&candidate, candidate_home});
    }
    std::ranges::sort(candidates, {}, [](const Candidate &candidate) {
      return candidate.fleet->id;
    });
    if (candidates.empty()) {
      result.message =
          tr("FREIGHT_MSG_NO_FREIGHTER", "No idle owned bulk freighter is available at a developed colony.");
      return result;
    }

    const FleetState *fleet = nullptr;
    const Colony *home = nullptr;
    std::optional<FleetState> after_preflight;
    for (const auto candidate : candidates) {
      CampaignSimulationState copied{current.world};
      try {
        const auto preflight =
            current.runtime.core().issue_freight_collection_order(
                &copied, current.player_id, candidate.fleet->id, outpost->id);
        if (result.message.empty())
          result.message = preflight.message;
        if (!preflight.accepted)
          continue;
        const auto *after = unique_by(copied.campaign().fleets,
                                      candidate.fleet->id, &FleetState::id);
        if (!after) {
          if (result.message.empty())
            result.message =
                tr("FREIGHT_MSG_PREFLIGHT", "Freight preflight did not preserve the selected fleet.");
          continue;
        }
        fleet = candidate.fleet;
        home = candidate.home;
        after_preflight = *after;
        result.message = preflight.message;
        break;
      } catch (const std::exception &error) {
        if (result.message.empty())
          result.message = error.what();
      }
    }
    if (!fleet || !home || !after_preflight)
      return result;
    if (next_revision_ == 0)
      throw std::overflow_error("Freight quote revision exhausted.");
    result.revision = next_revision_++;
    result.fleet_id = fleet->id;
    result.home_colony_id = home->id;
    result.fleet_name = fleet->name;
    result.home_name = home->name;
    result.cargo_capacity = fleet->cargo_material_capacity;
    result.accepted = true;
    quote_ = HeldQuote{
        result, DecisionSnapshot{
                    *fleet, *after_preflight, *home->planetary_body_id,
                    home->name, outpost->name, operations.stored_materials,
                    operations.extraction_per_day, operations.storage_capacity,
                    operations.remaining_deposit_materials}};
    return result;
  } catch (const std::exception &error) {
    quote_.reset();
    result.accepted = false;
    result.message = error.what();
    return result;
  }
}

NativeOutpostFreightOutcome
NativeOutpostFreightController::issue(CampaignFrame &frame,
                                      const std::uint64_t generation,
                                      const std::uint64_t revision) {
  require_owner();
  if (!generation_ || *generation_ != generation || !quote_ ||
      quote_->preview.campaign_generation != generation ||
      quote_->preview.revision != revision)
    return stale();
  auto held = std::move(*quote_);
  quote_.reset();
  if (frame.clock().speed() != StrategicSpeed::Paused)
    return {false, tr("FREIGHT_MSG_PAUSED", "Pause the campaign before dispatching freight.")};

  std::optional<Context> current_holder;
  try {
    current_holder.emplace(context(frame));
  } catch (const std::exception &error) {
    return {false, error.what()};
  }
  auto &current = *current_holder;
  const auto &q = held.preview;
  if (current.player_id != q.player_civilization_id ||
      std::ranges::count(current.world.economies, current.player_id,
                         &CivilizationEconomy::civilization_id) != 1 ||
      !current.world.knowledge.is_system_fully_surveyed(current.player_id,
                                                        q.system_id))
    return stale();
  auto *fleet = unique_by(current.world.fleets, q.fleet_id, &FleetState::id);
  const auto *home =
      unique_by(current.world.colonies, q.home_colony_id, &Colony::id);
  const auto *outpost =
      unique_by(current.world.colonies, q.colony_id, &Colony::id);
  const auto *body =
      unique_by(current.world.bodies, q.body_id, &PlanetaryBody::id);
  const auto *home_body = unique_by(
      current.world.bodies, held.decision.home_body_id, &PlanetaryBody::id);
  if (!fleet || !same_decision_fleet(*fleet, held.decision.fleet_before) ||
      !idle_freighter(*fleet, current.player_id) || !home || !home_body ||
      home->civilization_id != current.player_id ||
      home->kind != SettlementKind::Colony ||
      home->system_id != fleet->current_system_id ||
      home->planetary_body_id != held.decision.home_body_id ||
      home_body->system_id != home->system_id ||
      home->name != held.decision.home_name || !outpost || !body ||
      outpost->civilization_id != current.player_id ||
      outpost->kind != SettlementKind::ResourceOutpost ||
      outpost->system_id != q.system_id ||
      outpost->planetary_body_id != q.body_id ||
      outpost->name != held.decision.outpost_name ||
      body->system_id != q.system_id ||
      !unique_by(current.world.systems, q.system_id, &StellarSystem::id) ||
      !unique_by(current.world.systems, home->system_id, &StellarSystem::id))
    return stale();

  ResourceOutpostOperationsSnapshot operations;
  try {
    operations = resource_outpost_snapshot(current.world.bodies,
                                           current.world.economies, *outpost);
  } catch (const std::exception &error) {
    return {false, error.what()};
  }
  if (!valid_operations(operations) ||
      operations.stored_materials != held.decision.stored_materials ||
      operations.extraction_per_day != held.decision.extraction_per_day ||
      operations.storage_capacity != held.decision.storage_capacity ||
      operations.remaining_deposit_materials !=
          held.decision.remaining_materials)
    return stale();

  CampaignSimulationState copied{current.world};
  FreightOrderResult preflight;
  try {
    preflight = current.runtime.core().issue_freight_collection_order(
        &copied, current.player_id, q.fleet_id, q.colony_id);
  } catch (const std::exception &error) {
    return {false, error.what()};
  }
  const auto *after =
      unique_by(copied.campaign().fleets, q.fleet_id, &FleetState::id);
  if (!preflight.accepted || !after ||
      !same_decision_fleet(*after, held.decision.fleet_after_preflight))
    return {false,
            preflight.message.empty() ? stale().message : preflight.message};

  try {
    const auto result = current.runtime.core().issue_freight_collection_order(
        &current.simulation, current.player_id, q.fleet_id, q.colony_id);
    return {result.accepted, result.message};
  } catch (const std::exception &error) {
    return {false, error.what()};
  }
}

void NativeOutpostFreightController::clear() noexcept {
  if (std::this_thread::get_id() != owner_)
    return;
  quote_.reset();
}
} // namespace stellar::native_colony
