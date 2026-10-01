#include <stellar/core/freight.hpp>

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/colony_operations.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

double source_min(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}

double source_max(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}

std::vector<EconomyConstructionState>
construction_projection(std::span<const ConstructionState> construction) {
  std::vector<EconomyConstructionState> result;
  result.reserve(construction.size());
  for (const auto &state : construction)
    result.push_back({state.civilization_id, state.completed_project_ids});
  return result;
}

EconomyWorldView
economy_view(FreightWorldView world,
             std::span<const EconomyConstructionState> construction,
             std::span<const EconomyFleetState> fleets) {
  return {world.civilizations, world.bodies, construction, fleets};
}

} // namespace

FreightSimulation::FreightSimulation(FreightReachAssessor reach)
    : reach_(std::move(reach)) {
  if (!reach_)
    reach_ = [](OperationalReachWorldView world, int civilization_id,
                const FleetState &fleet, int target_system_id,
                InterstellarMissionKind mission) {
      return assess_operational_reach(world, civilization_id, fleet,
                                      target_system_id, mission);
    };
}

FreightOrderResult
FreightSimulation::issue_transit_order(FreightWorldView world,
                                       int civilization_id, int fleet_id,
                                       int target_system_id) const {
  const auto fleet = std::find_if(
      world.fleets.begin(), world.fleets.end(), [=](const auto &candidate) {
        return candidate.id == fleet_id && candidate.is_active &&
               candidate.civilization_id == civilization_id &&
               candidate.role == FleetRole::Logistics;
      });
  if (fleet == world.fleets.end())
    return {false, "Select an owned freighter first."};
  if (fleet->freight_home_colony_id || fleet->freight_target_outpost_id)
    return {false, "Finish the current cargo collection and delivery before "
                   "assigning another course."};

  const auto assessment =
      reach_(world.reach(), civilization_id, *fleet, target_system_id,
             InterstellarMissionKind::Logistics);
  if (!assessment.is_supported)
    return {false, assessment.reason};
  assign_fleet_route(world.reach(), *fleet, target_system_id, assessment);
  return {true, fleet->name + ": course set. " + assessment.reason};
}

FreightOrderResult
FreightSimulation::issue_collection_order(FreightWorldView world,
                                          int civilization_id, int fleet_id,
                                          int outpost_id) const {
  const auto fleet = std::find_if(
      world.fleets.begin(), world.fleets.end(), [=](const auto &candidate) {
        return candidate.id == fleet_id && candidate.is_active &&
               candidate.civilization_id == civilization_id &&
               candidate.role == FleetRole::Logistics &&
               candidate.design_id == "bulk_freighter";
      });
  if (fleet == world.fleets.end())
    return {false, "No controllable bulk freighter with that fleet ID is "
                   "available."};
  if (fleet->destination_system_id || fleet->freight_home_colony_id ||
      fleet->freight_target_outpost_id || fleet->cargo_materials > 0.0)
    return {false, fleet->name + " is already assigned to a freight run."};
  if (!fleet->current_system_id)
    return {false, fleet->name + " must finish its current lane leg before "
                                 "receiving a freight order."};

  const auto home = std::find_if(
      world.colonies.begin(), world.colonies.end(), [&](const auto &colony) {
        return colony.civilization_id == civilization_id &&
               colony.system_id == *fleet->current_system_id &&
               colony.kind == SettlementKind::Colony;
      });
  if (home == world.colonies.end())
    return {false,
            "A freight run must depart from one of your developed colonies."};
  const auto outpost = std::find_if(
      world.colonies.begin(), world.colonies.end(), [=](const auto &colony) {
        return colony.id == outpost_id &&
               colony.civilization_id == civilization_id &&
               colony.kind == SettlementKind::ResourceOutpost;
      });
  if (outpost == world.colonies.end())
    return {false, "That staffed resource outpost is unavailable."};

  const auto body =
      outpost->planetary_body_id
          ? std::find_if(world.bodies.begin(), world.bodies.end(),
                         [&](const auto &value) {
                           return value.id == *outpost->planetary_body_id &&
                                  value.system_id == outpost->system_id;
                         })
          : world.bodies.end();
  if (body != world.bodies.end() && body->has_rare_resource) {
    const auto remaining = outpost->remaining_extractable_materials.value_or(
        source_max(0.0, initial_deposit_reserve(*body) -
                            outpost->stored_extracted_materials));
    if (!std::isfinite(remaining) || remaining < 0.0)
      throw std::runtime_error("Resource outpost " +
                               std::to_string(outpost->id) +
                               " has an invalid remaining deposit reserve.");
  }

  ResourceOutpostOperationsSnapshot operations;
  try {
    operations =
        resource_outpost_snapshot(world.bodies, world.economies, *outpost);
  } catch (const std::out_of_range &) {
    auto funding = 1.0;
    if (const auto economy = std::find_if(
            world.economies.begin(), world.economies.end(),
            [&](const auto &value) {
              return value.civilization_id == outpost->civilization_id;
            });
        economy != world.economies.end())
      funding = economy->last_base_operations_funding_fraction;
    if (!std::isfinite(funding) || funding < 0.0 || funding > 1.0)
      throw std::out_of_range(
          "Operating funding fraction must be finite and between zero and one. "
          "(Parameter 'operatingFundingFraction')");
    throw;
  }
  if (operations.stored_materials <= 0.0 &&
      operations.extraction_per_day <= 0.0)
    return {false, operations.status};

  const auto assessment =
      reach_(world.reach(), civilization_id, *fleet, outpost->system_id,
             InterstellarMissionKind::Logistics);
  if (!assessment.is_supported)
    return {false, assessment.reason};

  // Source intentionally records the freight run before route assignment.
  fleet->freight_home_colony_id = home->id;
  fleet->freight_target_outpost_id = outpost->id;
  assign_fleet_route(world.reach(), *fleet, outpost->system_id, assessment);
  return {true, fleet->name + " dispatched to collect up to " +
                    detail::legacy_custom_fixed(fleet->cargo_material_capacity,
                                                0, 1) +
                    " material units from " + outpost->name + ". " +
                    assessment.reason};
}

void FreightSimulation::advance(FreightWorldView world,
                                double simulation_days) const {
  if (!std::isfinite(simulation_days) || simulation_days < 0.0)
    throw std::out_of_range(
        "Freight transfer time must be finite and nonnegative. (Parameter "
        "'simulationDays')");
  if (simulation_days <= 0.0)
    return;

  // Per-advance invariant caches (ADR 0002 option a): the transfer-rate
  // port capacity, storage-capacity projections and fleet/construction
  // projections depend only on inputs that cannot change inside this loop
  // (colony buildings/infrastructure, construction completion sets, fleet
  // roles/activity). Rebuilding them per freighter is O(fleets x world);
  // hoisting keeps identical arithmetic while paying it once.
  std::unordered_map<int, double> port_capacity_cache;
  const auto transfer_rate = [&](const FleetState &vessel,
                                 const Colony &colony) {
    const auto [entry, inserted] =
        port_capacity_cache.try_emplace(colony.id, 0.0);
    if (inserted)
      entry->second = port_transfer_capacity_per_day(colony);
    return source_min(cargo_transfer_rate_per_day(vessel), entry->second);
  };
  std::optional<std::vector<EconomyConstructionState>> construction_cache;
  std::optional<std::vector<EconomyFleetState>> fleets_cache;
  std::unordered_map<int, double> storage_capacity_cache;
  const auto free_storage_for = [&](int civilization_id,
                                    const CivilizationEconomy &economy) {
    const auto [entry, inserted] =
        storage_capacity_cache.try_emplace(civilization_id, 0.0);
    if (inserted) {
      if (!construction_cache)
        construction_cache = construction_projection(world.construction);
      if (!fleets_cache)
        fleets_cache = economic_fleet_projection(world.fleets);
      entry->second = industry_storage_capacity(
          economy_view(world, *construction_cache, *fleets_cache),
          world.colonies, civilization_id);
    }
    // `economy.industry` is mutated by unload transfers below — keep the
    // subtraction live rather than caching the free amount.
    return source_max(0.0, entry->second - economy.industry);
  };

  for (auto &fleet : world.fleets) {
    if (!fleet.is_active || fleet.role != FleetRole::Logistics ||
        fleet.transit_phase != FleetTransitPhase::None ||
        fleet.destination_system_id || !fleet.freight_home_colony_id)
      continue;
    if (civilization_operating_funding(world.economies,
                                       fleet.civilization_id) <= 0.0000001)
      continue;

    const auto home = std::find_if(
        world.colonies.begin(), world.colonies.end(), [&](const auto &colony) {
          return colony.id == *fleet.freight_home_colony_id &&
                 colony.civilization_id == fleet.civilization_id &&
                 colony.kind == SettlementKind::Colony;
        });
    if (home == world.colonies.end())
      continue;

    if (fleet.freight_target_outpost_id) {
      const auto outpost = std::find_if(
          world.colonies.begin(), world.colonies.end(),
          [&](const auto &colony) {
            return colony.id == *fleet.freight_target_outpost_id &&
                   colony.civilization_id == fleet.civilization_id &&
                   colony.kind == SettlementKind::ResourceOutpost;
          });
      if (outpost == world.colonies.end() ||
          fleet.current_system_id != outpost->system_id)
        continue;

      const auto loaded = source_min(
          transfer_rate(fleet, *outpost) * simulation_days,
          source_min(outpost->stored_extracted_materials,
                     fleet.cargo_material_capacity - fleet.cargo_materials));
      outpost->stored_extracted_materials -= loaded;
      fleet.cargo_materials += loaded;
      if (fleet.cargo_materials + 0.0000001 < fleet.cargo_material_capacity &&
          outpost->stored_extracted_materials > 0.0000001)
        continue;

      const auto assessment =
          reach_(world.reach(), fleet.civilization_id, fleet, home->system_id,
                 InterstellarMissionKind::Logistics);
      if (assessment.is_supported) {
        // Source clears the target before route assignment.
        fleet.freight_target_outpost_id.reset();
        assign_fleet_route(world.reach(), fleet, home->system_id, assessment);
      }
      continue;
    }

    if (fleet.current_system_id != home->system_id)
      continue;
    const auto economy = std::find_if(
        world.economies.begin(), world.economies.end(), [&](const auto &state) {
          return state.civilization_id == fleet.civilization_id;
        });
    if (economy == world.economies.end())
      throw std::runtime_error("Sequence contains no matching element");

    const auto civilization = std::find_if(
        world.civilizations.begin(), world.civilizations.end(),
        [&](const auto &value) { return value.id == fleet.civilization_id; });
    if (civilization == world.civilizations.end())
      throw std::runtime_error("Sequence contains no matching element");
    if (!civilization->is_seeded_ancient &&
        std::find_if(world.construction.begin(), world.construction.end(),
                     [&](const auto &value) {
                       return value.civilization_id == fleet.civilization_id;
                     }) == world.construction.end())
      throw std::runtime_error("Sequence contains no matching element");

    const auto free_storage =
        free_storage_for(fleet.civilization_id, *economy);
    const auto unloaded = source_min(
        transfer_rate(fleet, *home) * simulation_days,
        source_min(fleet.cargo_materials, free_storage));
    economy->industry += unloaded;
    fleet.cargo_materials -= unloaded;
    if (fleet.cargo_materials <= 0.0000001) {
      fleet.cargo_materials = 0.0;
      fleet.freight_home_colony_id.reset();
    }
  }
}

double FreightSimulation::cargo_transfer_rate_per_day(const FleetState &fleet) {
  const auto design =
      fleet.design_id ? find_ship_design(*fleet.design_id) : nullptr;
  return design && design->cargo_transfer_rate_per_day > 0.0
             ? design->cargo_transfer_rate_per_day
         : fleet.cargo_material_capacity > 0.0 ? 20.0
                                               : 0.0;
}

double FreightSimulation::port_transfer_capacity_per_day(const Colony &colony) {
  return basic_hub_transfer_capacity_per_day +
         surface_colony_output(colony).cargo_transfer_capacity_per_day;
}

double
FreightSimulation::effective_transfer_rate_per_day(const FleetState &fleet,
                                                   const Colony &colony) {
  return source_min(cargo_transfer_rate_per_day(fleet),
                    port_transfer_capacity_per_day(colony));
}

} // namespace stellar::core
