#pragma once

#include <stellar/core/planetary_catalog.hpp>
#include <string_view>

namespace stellar::core {
// Derived environmental descriptions, independent of artwork and species
// suitability. Continental is a temperate, non-immersed water-world class;
// coastlines, moisture coverage and geological activity are not measured here.
enum class PlanetaryWorldClass {
  Barren, Continental, Ocean, HighPressureOcean, Desert, Arid, Frozen,
  Greenhouse, Molten, Hydrocarbon, Ammonia, Reducing, Exotic,
  GasGiant, IceGiant, Cracked
};

[[nodiscard]] inline PlanetaryWorldClass classify_planetary_world(
    const PlanetaryBody& body, bool canonical_sol = false) noexcept {
  using enum PlanetaryWorldClass;
  if(body.cracked_world)return Cracked;
  const auto& e = body.environment;
  const bool airless = e.atmosphere == PlanetaryAtmosphereRegime::Vacuum ||
                       e.pressure_kpa < .1;
  if (!e.has_solid_surface) {
    if (canonical_sol && (body.name == "Uranus" || body.name == "Neptune")) return IceGiant;
    if (canonical_sol && (body.name == "Jupiter" || body.name == "Saturn")) return GasGiant;
    return e.temperature_kelvin < 170 && body.mass_earth < 50 && body.radius_earth < 6
               ? IceGiant : GasGiant;
  }
  if (e.temperature_kelvin >= 1000) return Molten;
  if (!airless && e.temperature_kelvin >= 450 && e.pressure_kpa >= 1000) return Greenhouse;
  if (e.available_solvent == PlanetarySolventRegime::Hydrocarbon && e.temperature_kelvin <= 180)
    return Hydrocarbon;
  if (airless) return Barren;
  if (e.temperature_kelvin < 200) return Frozen;
  if (e.is_immersed_environment && e.available_solvent == PlanetarySolventRegime::Water)
    return e.pressure_kpa >= 250 ? HighPressureOcean : Ocean;
  if (e.available_solvent == PlanetarySolventRegime::Ammonia) return Ammonia;
  if (e.available_solvent == PlanetarySolventRegime::Other || e.is_immersed_environment) return Exotic;
  if (e.atmosphere == PlanetaryAtmosphereRegime::Reducing) return Reducing;
  if (e.available_solvent == PlanetarySolventRegime::Water &&
      e.temperature_kelvin >= 260 && e.temperature_kelvin <= 320 &&
      e.pressure_kpa >= 40 && e.pressure_kpa <= 200 &&
      (e.atmosphere == PlanetaryAtmosphereRegime::OxygenNitrogen ||
       e.atmosphere == PlanetaryAtmosphereRegime::OxygenRich)) return Continental;
  if (e.available_solvent == PlanetarySolventRegime::None)
    return e.temperature_kelvin >= 285 ? Desert : Barren;
  return Arid;
}

[[nodiscard]] inline std::string_view planetary_world_class_name(PlanetaryWorldClass value) noexcept {
  using enum PlanetaryWorldClass;
  switch (value) {
  case Barren: return "Barren";
  case Continental: return "Continental";
  case Ocean: return "Ocean";
  case HighPressureOcean: return "High-pressure ocean";
  case Desert: return "Desert";
  case Arid: return "Arid";
  case Frozen: return "Frozen";
  case Greenhouse: return "Greenhouse";
  case Molten: return "Molten";
  case Hydrocarbon: return "Hydrocarbon";
  case Ammonia: return "Ammonia";
  case Reducing: return "Reducing atmosphere";
  case Exotic: return "Exotic";
  case GasGiant: return "Gas giant";
  case IceGiant: return "Ice giant";
  case Cracked: return "Cracked world";
  }
  return "Unknown";
}
}
