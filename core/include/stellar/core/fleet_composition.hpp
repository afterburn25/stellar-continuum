#pragma once

#include <stellar/core/fleet_state.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stellar::core {

// One commissioned vessel inside a fleet. Canonical fleets field exactly one
// vessel today (the fidelity contract in campaign_warfare_projection.hpp), so
// every list produced here is a single member — but the member carries the
// full tactical-vessel record (role flags, subsystem fractions, battle
// history) plus the fleet's embarked payload, which the fleet view-model
// previously surfaced only as a hull scalar.
struct FleetCompositionMember {
  int fleet_id{};
  // Stable tactical identity shared with campaign combat bindings.
  std::int64_t vessel_id{};
  std::string name;
  std::optional<std::string> design_id;
  std::optional<std::string> combat_profile_id;
  bool is_flagship{}, is_carrier{}, is_interdictor{}, is_story_ship{};
  float hull_fraction{1}, engine_fraction{1}, sensor_fraction{1},
      warp_drive_fraction{1}, reactor_fraction{1}, interdictor_fraction{1};
  int battles_fought{}, confirmed_kills{};
  bool destroyed{}, escaped{};
  // True when the vessel's state comes from a retained MassiveVesselState
  // (battle-tested record); false when synthesized from the fleet's design.
  bool has_vessel_state{};
  // Embarked payload attributed to this member — the whole fleet payload
  // while fleets are single-vessel.
  double embarked_population_millions{};
  std::optional<std::string> embarked_population_species_id;
  double cargo_materials{}, cargo_material_capacity{};
};

struct FleetComposition {
  int fleet_id{};
  // Member vessels in deterministic order (vessel id ascending). Bounded by
  // the single-vessel fidelity contract; consumers must not assume it is
  // always one.
  std::vector<FleetCompositionMember> members;
  int vessel_count{};
  int operational_vessel_count{};
  double total_embarked_population_millions{};
  double total_cargo_materials{}, total_cargo_material_capacity{};
};

// Read-only projection over authoritative fleet state. Callers are
// responsible for observer gating — the projection reports whatever the
// caller is already permitted to see of `fleet`.
[[nodiscard]] FleetComposition fleet_composition(const FleetState &fleet);

} // namespace stellar::core
