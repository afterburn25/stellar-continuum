#include <stellar/core/combat_command_runtime.hpp>

#include <algorithm>
#include <set>

namespace stellar::core {
namespace {
const CombatProfileDefinition &read_profile(const FleetState &fleet) {
  if (fleet.combat && !fleet.combat->profile_id.empty())
    if (const auto *profile = find_combat_profile(fleet.combat->profile_id))
      return *profile;
  return get_combat_profile(default_combat_profile_id(fleet.role));
}

bool disengaged_here(const FleetState &fleet, int system_id) {
  if (!fleet.combat || !find_combat_profile(fleet.combat->profile_id))
    return false;
  return fleet.combat->is_disengaged &&
         fleet.combat->disengaged_system_id == system_id;
}

std::vector<int> unique_sorted(std::span<const int> ids) {
  std::set<int> sorted(ids.begin(), ids.end());
  return {sorted.begin(), sorted.end()};
}
} // namespace

bool CombatBatchOrderPreview::all_accepted() const {
  return requested_fleet_count > 0 && rejected_count == 0;
}
bool CombatBatchOrderPreview::any_accepted() const {
  return accepted_count > 0;
}
bool CombatBatchOrderResult::all_accepted() const {
  return requested_fleet_count > 0 && rejected_count == 0;
}
bool CombatBatchOrderResult::any_accepted() const { return accepted_count > 0; }

CombatBatchOrderResult
issue_combat_batch(CombatSimulation &simulation, CombatWorldView world,
                   int civilization_id, std::span<const int> fleet_ids,
                   const MilitaryOrder &order) {
  CombatBatchOrderResult batch;
  const auto ids = unique_sorted(fleet_ids);
  batch.requested_fleet_count = static_cast<int>(ids.size());
  batch.fleet_results.reserve(ids.size());
  for (const int fleet_id : ids) {
    const auto result =
        simulation.issue_order(world, civilization_id, fleet_id, order);
    batch.fleet_results.push_back({fleet_id, result.accepted, result.message});
    result.accepted ? ++batch.accepted_count : ++batch.rejected_count;
  }
  return batch;
}

CombatCommandRuntime::CombatCommandRuntime(CombatHostilityView hostility)
    : hostility_(std::make_shared<CombatHostilityView>(
          hostility ? std::move(hostility)
                    : CombatHostilityView{[](int, int) { return false; }})),
      simulation_([policy = hostility_](int first, int second) {
        return (*policy)(first, second);
      }) {}

CombatOrderPreview
CombatCommandRuntime::preview_order(CombatWorldView world, int civilization_id,
                                    int fleet_id,
                                    const MilitaryOrder &order) const {
  const auto fleet_it = std::find_if(
      world.fleets.begin(), world.fleets.end(), [&](const FleetState &fleet) {
        return fleet.id == fleet_id &&
               fleet.civilization_id == civilization_id && fleet.is_active;
      });
  auto result = CombatOrderPreview{civilization_id, fleet_id, order, false, {}};
  if (fleet_it == world.fleets.end()) {
    result.message =
        "No active fleet with that identity belongs to the civilization.";
    return result;
  }

  const auto &fleet = *fleet_it;
  const auto &profile = read_profile(fleet);
  switch (order.type) {
  case MilitaryOrderType::Hold:
    result.accepted = true;
    result.message = fleet.name + " can hold position.";
    return result;
  case MilitaryOrderType::Defend: {
    if (!profile.has_weapon()) {
      result.message = fleet.name + " has no combat-capable weapon system.";
      return result;
    }
    const auto system_id = order.defend_system_id ? order.defend_system_id
                                                  : fleet.current_system_id;
    if (!system_id || fleet.current_system_id != system_id ||
        std::none_of(world.systems.begin(), world.systems.end(),
                     [&](const StellarSystem &system) {
                       return system.id == *system_id;
                     })) {
      result.message = "A defend order currently requires the fleet to be "
                       "present in the defended system.";
      return result;
    }
    result.accepted = true;
    result.message =
        fleet.name + " can defend system " + std::to_string(*system_id) + ".";
    return result;
  }
  case MilitaryOrderType::Attack: {
    if (!profile.has_weapon()) {
      result.message = fleet.name + " has no combat-capable weapon system.";
      return result;
    }
    if (!order.target_fleet_id) {
      result.message = "An attack order requires a target fleet.";
      return result;
    }
    const auto target = std::find_if(
        world.fleets.begin(), world.fleets.end(),
        [&](const FleetState &candidate) {
          return candidate.id == *order.target_fleet_id && candidate.is_active;
        });
    if (target == world.fleets.end() ||
        target->civilization_id == civilization_id) {
      result.message = "The requested target is not a valid hostile fleet.";
      return result;
    }
    if (!fleet.current_system_id ||
        fleet.current_system_id != target->current_system_id) {
      result.message =
          "The target must be in the same system before combat can begin.";
      return result;
    }
    if (disengaged_here(*target, *fleet.current_system_id)) {
      result.message = "The target has tactically disengaged from this "
                       "system-level engagement.";
      return result;
    }
    if (!(*hostility_)(civilization_id, target->civilization_id)) {
      result.message = "Diplomatic/political state does not currently permit "
                       "a hostile engagement.";
      return result;
    }
    result.accepted = true;
    result.message = fleet.name + " can attack " + target->name + ".";
    return result;
  }
  case MilitaryOrderType::Retreat:
    result.accepted = true;
    result.message = fleet.name + " can attempt to disengage.";
    return result;
  default:
    result.message = "Unknown military order.";
    return result;
  }
}

CombatBatchOrderPreview
CombatCommandRuntime::preview_orders(CombatWorldView world, int civilization_id,
                                     std::span<const int> fleet_ids,
                                     const MilitaryOrder &order) const {
  CombatBatchOrderPreview batch;
  const auto ids = unique_sorted(fleet_ids);
  batch.requested_fleet_count = static_cast<int>(ids.size());
  batch.fleet_results.reserve(ids.size());
  for (const int fleet_id : ids) {
    const auto preview = preview_order(world, civilization_id, fleet_id, order);
    batch.fleet_results.push_back(
        {fleet_id, preview.accepted, preview.message});
    preview.accepted ? ++batch.accepted_count : ++batch.rejected_count;
  }
  return batch;
}

CombatOrderResult
CombatCommandRuntime::issue_order(CombatWorldView world, int civilization_id,
                                  int fleet_id, const MilitaryOrder &order) {
  return simulation_.issue_order(world, civilization_id, fleet_id, order);
}

CombatBatchOrderResult
CombatCommandRuntime::issue_orders(CombatWorldView world, int civilization_id,
                                   std::span<const int> fleet_ids,
                                   const MilitaryOrder &order) {
  return issue_combat_batch(simulation_, world, civilization_id, fleet_ids,
                            order);
}

CombatOrderResult
CombatCommandRuntime::issue_engage_hostiles(CombatWorldView world,
                                            int civilization_id, int fleet_id) {
  const auto actor = std::find_if(
      world.fleets.begin(), world.fleets.end(), [&](const FleetState &fleet) {
        return fleet.id == fleet_id && fleet.is_active &&
               fleet.civilization_id == civilization_id;
      });
  if (actor == world.fleets.end() || !actor->current_system_id)
    return {false, "The selected fleet must be present in a star system to "
                   "engage hostiles."};

  std::vector<const FleetState *> candidates;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id != civilization_id &&
        fleet.current_system_id == actor->current_system_id)
      candidates.push_back(&fleet);
  std::stable_sort(
      candidates.begin(), candidates.end(),
      [](const auto *left, const auto *right) { return left->id < right->id; });
  for (const auto *candidate : candidates) {
    const MilitaryOrder order{MilitaryOrderType::Attack, candidate->id,
                              std::nullopt};
    if (preview_order(world, civilization_id, fleet_id, order).accepted)
      return issue_order(world, civilization_id, fleet_id, order);
  }
  return {false, "No attackable hostile fleet is detected in this fleet's "
                 "current system."};
}

CombatSimulation &CombatCommandRuntime::simulation() noexcept {
  return simulation_;
}
const CombatSimulation &CombatCommandRuntime::simulation() const noexcept {
  return simulation_;
}

} // namespace stellar::core
