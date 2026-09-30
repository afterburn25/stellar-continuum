#include <stellar/core/fleet_composition.hpp>

#include <stellar/core/massive_combat_persistence.hpp>

#include <algorithm>

namespace stellar::core {

FleetComposition fleet_composition(const FleetState &fleet) {
  FleetComposition result;
  result.fleet_id = fleet.id;
  FleetCompositionMember member;
  member.fleet_id = fleet.id;
  member.vessel_id = campaign_vessel_id_for_fleet(fleet.id);
  member.name = fleet.tactical_vessel && !fleet.tactical_vessel->name.empty()
                    ? fleet.tactical_vessel->name
                    : fleet.name;
  if (fleet.tactical_vessel) {
    const auto &vessel = *fleet.tactical_vessel;
    member.has_vessel_state = true;
    if (!vessel.design_id.empty()) member.design_id = vessel.design_id;
    else if (fleet.design_id) member.design_id = fleet.design_id;
    member.is_flagship = vessel.is_flagship;
    member.is_carrier = vessel.is_carrier;
    member.is_interdictor = vessel.is_interdictor;
    member.is_story_ship = vessel.is_story_ship;
    member.hull_fraction = vessel.hull_fraction;
    member.engine_fraction = vessel.engine_fraction;
    member.sensor_fraction = vessel.sensor_fraction;
    member.warp_drive_fraction = vessel.warp_drive_fraction;
    member.reactor_fraction = vessel.reactor_fraction;
    member.interdictor_fraction = vessel.interdictor_fraction;
    member.battles_fought = vessel.battles_fought;
    member.confirmed_kills = vessel.confirmed_kills;
    member.destroyed = vessel.destroyed;
    member.escaped = vessel.escaped;
  } else {
    member.design_id = fleet.design_id;
  }
  if (fleet.combat) member.combat_profile_id = fleet.combat->profile_id;
  member.embarked_population_millions = fleet.embarked_population_millions;
  member.embarked_population_species_id = fleet.embarked_population_species_id;
  member.cargo_materials = fleet.cargo_materials;
  member.cargo_material_capacity = fleet.cargo_material_capacity;
  result.members.push_back(std::move(member));
  result.vessel_count = static_cast<int>(result.members.size());
  for (const auto &item : result.members) {
    if (!item.destroyed && !item.escaped) ++result.operational_vessel_count;
    result.total_embarked_population_millions +=
        item.embarked_population_millions;
    result.total_cargo_materials += item.cargo_materials;
    result.total_cargo_material_capacity += item.cargo_material_capacity;
  }
  return result;
}

} // namespace stellar::core
