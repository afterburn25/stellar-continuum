#include <stellar/core/planetary_adapter.hpp>

#include <algorithm>

namespace stellar::core {

namespace {

constexpr double kpa_per_atm = 101.325;
// Same hazard threshold as surface construction's radiation surcharge.
constexpr double high_radiation_threshold = 0.10;

std::string_view atmosphere_tag(PlanetaryAtmosphereRegime regime) {
  switch (regime) {
  case PlanetaryAtmosphereRegime::Vacuum:
    return "atmosphere.vacuum";
  case PlanetaryAtmosphereRegime::OxygenNitrogen:
    return "atmosphere.oxygen_nitrogen";
  case PlanetaryAtmosphereRegime::OxygenRich:
    return "atmosphere.oxygen_rich";
  case PlanetaryAtmosphereRegime::CarbonDioxideRich:
    return "atmosphere.carbon_dioxide";
  case PlanetaryAtmosphereRegime::Reducing:
    return "atmosphere.reducing";
  case PlanetaryAtmosphereRegime::Inert:
    return "atmosphere.inert";
  case PlanetaryAtmosphereRegime::Other:
    return "atmosphere.other";
  }
  return "atmosphere.other";
}

} // namespace

engine::PlanetEnvironment to_engine_environment(const PlanetaryBody &body) {
  const auto &env = body.environment;
  engine::PlanetEnvironment out;
  out.temperature_k = env.temperature_kelvin;
  out.atmosphere_atm = env.pressure_kpa / kpa_per_atm;
  out.gravity_g = env.gravity_g;
  out.water_fraction =
      env.available_solvent == PlanetarySolventRegime::Water ? 1.0 : 0.0;

  out.tags.emplace_back(atmosphere_tag(env.atmosphere));
  switch (env.available_solvent) {
  case PlanetarySolventRegime::Water:
    out.tags.emplace_back("solvent.water");
    break;
  case PlanetarySolventRegime::Ammonia:
    out.tags.emplace_back("solvent.ammonia");
    break;
  case PlanetarySolventRegime::Hydrocarbon:
    out.tags.emplace_back("solvent.hydrocarbon");
    break;
  case PlanetarySolventRegime::Other:
    out.tags.emplace_back("solvent.other");
    break;
  case PlanetarySolventRegime::None:
    break;
  }
  if (env.is_immersed_environment) out.tags.emplace_back("immersed");
  if (!env.has_solid_surface) out.tags.emplace_back("gas_giant");
  if (env.radiation_hazard > high_radiation_threshold)
    out.tags.emplace_back("high_radiation");
  if (body.cracked_world) out.tags.emplace_back("cracked");
  if (body.has_anomaly) out.tags.emplace_back("anomaly");
  if (body.has_rare_resource) out.tags.emplace_back("rare_resource");
  if (body.has_pre_warp_civilization)
    out.tags.emplace_back("native_civilization");

  std::sort(out.tags.begin(), out.tags.end());
  out.tags.erase(std::unique(out.tags.begin(), out.tags.end()),
                 out.tags.end());
  return out;
}

} // namespace stellar::core
