#include <stellar/core/fleet_composition.hpp>
#include <stellar/core/massive_combat_persistence.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::core;

void require(const bool value, const std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

void synthesized_member_falls_back_to_fleet_fields() {
  FleetState fleet;
  fleet.id = 7;
  fleet.name = "Pathfinder Nine";
  fleet.design_id = "scout_mk2";
  fleet.embarked_population_millions = 1.25;
  fleet.embarked_population_species_id = "human";
  fleet.cargo_materials = 12.5;
  fleet.cargo_material_capacity = 40.;

  const auto composition = fleet_composition(fleet);
  require(composition.fleet_id == 7, "composition lost the fleet id");
  require(composition.vessel_count == 1 && composition.members.size() == 1,
          "single-vessel fleet must project exactly one member");
  require(composition.operational_vessel_count == 1,
          "a healthy vessel must count as operational");

  const auto &member = composition.members.front();
  require(member.fleet_id == 7 && member.vessel_id == 7,
          "member identity must reuse the campaign vessel namespace");
  require(member.name == "Pathfinder Nine" && !member.has_vessel_state,
          "member without vessel state must fall back to the fleet name");
  require(member.design_id && *member.design_id == "scout_mk2",
          "member dropped the fleet design id");
  require(member.hull_fraction == 1.f && member.engine_fraction == 1.f &&
              member.sensor_fraction == 1.f && member.warp_drive_fraction == 1.f &&
              member.reactor_fraction == 1.f,
          "synthesized member must report intact subsystems");
  require(!member.destroyed && !member.escaped && member.battles_fought == 0 &&
              member.confirmed_kills == 0,
          "synthesized member must not invent a battle record");
  require(member.embarked_population_millions == 1.25 &&
              member.embarked_population_species_id &&
              *member.embarked_population_species_id == "human" &&
              member.cargo_materials == 12.5 &&
              member.cargo_material_capacity == 40.,
          "member must carry the fleet's embarked payload");
  require(composition.total_embarked_population_millions == 1.25 &&
              composition.total_cargo_materials == 12.5 &&
              composition.total_cargo_material_capacity == 40.,
          "composition totals diverged from member payload");
}

void tactical_vessel_state_overrides_the_lead_record() {
  FleetState fleet;
  fleet.id = 3;
  fleet.name = "Task Force Anchor";
  fleet.design_id = "destroyer_mk1";
  fleet.combat = create_initial_fleet_combat_state("patrol_corvette_mk1",
                                                   FleetRole::Military);

  MassiveVesselState vessel;
  vessel.id = campaign_vessel_id_for_fleet(3);
  vessel.name = "ISS Resolute";
  vessel.design_id = "destroyer_mk2";
  vessel.is_flagship = true;
  vessel.is_interdictor = true;
  vessel.hull_fraction = .62f;
  vessel.engine_fraction = .5f;
  vessel.sensor_fraction = .9f;
  vessel.warp_drive_fraction = .8f;
  vessel.reactor_fraction = .7f;
  vessel.interdictor_fraction = .95f;
  vessel.battles_fought = 4;
  vessel.confirmed_kills = 2;
  fleet.tactical_vessel = vessel;

  const auto composition = fleet_composition(fleet);
  require(composition.vessel_count == 1, "battle-tested fleet lost its member");
  const auto &member = composition.members.front();
  require(member.has_vessel_state && member.name == "ISS Resolute",
          "member must prefer the retained vessel record");
  require(member.design_id && *member.design_id == "destroyer_mk2",
          "member must prefer the vessel's own design id");
  require(member.is_flagship && member.is_interdictor && !member.is_carrier &&
              !member.is_story_ship,
          "member role flags were not sealed faithfully");
  require(member.hull_fraction == .62f && member.engine_fraction == .5f &&
              member.sensor_fraction == .9f && member.warp_drive_fraction == .8f &&
              member.reactor_fraction == .7f,
          "member subsystem fractions were not sealed faithfully");
  require(member.battles_fought == 4 && member.confirmed_kills == 2,
          "member battle record was dropped");
  require(member.combat_profile_id &&
              *member.combat_profile_id == fleet.combat->profile_id,
          "member dropped the fleet combat profile");
}

void destroyed_and_escaped_members_leave_the_operational_count() {
  FleetState fleet;
  fleet.id = 9;
  fleet.name = "Lost Cause";
  MassiveVesselState vessel;
  vessel.id = campaign_vessel_id_for_fleet(9);
  vessel.name = "ISS Ember";
  vessel.destroyed = true;
  fleet.tactical_vessel = vessel;

  const auto destroyed = fleet_composition(fleet);
  require(destroyed.vessel_count == 1 &&
              destroyed.operational_vessel_count == 0,
          "a destroyed member must stay listed but leave the operational count");

  vessel.destroyed = false;
  vessel.escaped = true;
  fleet.tactical_vessel = vessel;
  const auto escaped = fleet_composition(fleet);
  require(escaped.vessel_count == 1 && escaped.operational_vessel_count == 0,
          "an escaped member must stay listed but leave the operational count");
}

void vessel_identity_survives_fleet_zero() {
  FleetState fleet;
  fleet.id = 0;
  fleet.name = "Anchor Fleet";
  const auto composition = fleet_composition(fleet);
  require(composition.members.front().vessel_id == campaign_zero_fleet_vessel_id,
          "fleet zero must map to the reserved vessel identity");
}

void projection_is_deterministic() {
  FleetState fleet;
  fleet.id = 12;
  fleet.name = "Repeatable";
  MassiveVesselState vessel;
  vessel.id = campaign_vessel_id_for_fleet(12);
  vessel.name = "ISS Meridian";
  vessel.hull_fraction = .4f;
  vessel.battles_fought = 9;
  fleet.tactical_vessel = vessel;

  const auto first = fleet_composition(fleet);
  const auto second = fleet_composition(fleet);
  require(first.vessel_count == second.vessel_count &&
              first.members.size() == second.members.size() &&
              first.members.front().name == second.members.front().name &&
              first.members.front().hull_fraction ==
                  second.members.front().hull_fraction &&
              first.total_cargo_materials == second.total_cargo_materials,
          "composition projection must be deterministic over identical input");
}

} // namespace

int main() {
  synthesized_member_falls_back_to_fleet_fields();
  tactical_vessel_state_overrides_the_lead_record();
  destroyed_and_escaped_members_leave_the_operational_count();
  vessel_identity_survives_fleet_zero();
  projection_is_deterministic();
  std::cout << "fleet composition tests passed\n";
  return 0;
}
