#pragma once

#include <stellar/core/planetary_catalog.hpp>
#include <stellar/engine/planetary.hpp>

namespace stellar::core {

// Planetary adapter — projects authoritative Core planet state into the
// engine's `PlanetEnvironment` so engine frameworks (Population
// environment_needs, Colony required_tags, Terraforming mutation) can
// evaluate it without depending on game types. Pure, deterministic,
// allocation-light.
//
// Field mapping:
//   temperature_kelvin  -> temperature_k (direct)
//   pressure_kpa        -> atmosphere_atm (kPa / 101.325)
//   gravity_g           -> gravity_g (direct)
//   available_solvent   -> water_fraction: 1.0 when Water, else 0.0 —
//                          categorical presence, NOT coverage fraction
//   tags                -> derived vocabulary:
//     "atmosphere.<regime>"   — vacuum, oxygen_nitrogen, oxygen_rich,
//                             carbon_dioxide, reducing, inert, other
//     "solvent.<regime>"      — water, ammonia, hydrocarbon, other
//     "immersed"              — is_immersed_environment
//     "gas_giant"             — !has_solid_surface
//     "high_radiation"        — radiation_hazard > 0.10 (matches the
//                             surface-construction hazard threshold)
//     "cracked"               — cracked_world
//     "anomaly"               — has_anomaly
//     "rare_resource"         — has_rare_resource
//     "native_civilization"   — has_pre_warp_civilization
// Tags are sorted+unique per the engine contract.

[[nodiscard]] engine::PlanetEnvironment
to_engine_environment(const PlanetaryBody &body);

} // namespace stellar::core
