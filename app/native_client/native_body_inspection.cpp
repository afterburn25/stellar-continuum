#include "native_body_inspection.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

namespace stellar::native_system_ui {
namespace {
constexpr double earth_radius_kilometres = 6'371.0;
constexpr double earth_mass_kilograms = 5.9722e24;
constexpr double standard_gravity_metres_per_second_squared = 9.80665;

std::string trim_decimal(std::string value) {
  const auto dot = value.find('.');
  if (dot == std::string::npos)
    return value;
  while (!value.empty() && value.back() == '0')
    value.pop_back();
  if (!value.empty() && value.back() == '.')
    value.pop_back();
  return value;
}

std::string fixed(double value, int precision) {
  std::ostringstream result;
  result.imbue(std::locale::classic());
  result << std::fixed << std::setprecision(precision) << value;
  return trim_decimal(result.str());
}

std::string grouped_integer(double value) {
  auto digits = fixed(value, 0);
  const auto first = digits[0] == '-' ? 1U : 0U;
  for (std::size_t position = digits.size(); position > first + 3U;
       position -= 3U)
    digits.insert(position - 3U, 1U, ',');
  return digits;
}

std::string superscript(int value) {
  constexpr std::string_view digits[] = {"⁰", "¹", "²", "³", "⁴",
                                         "⁵", "⁶", "⁷", "⁸", "⁹"};
  if (value == 0)
    return std::string(digits[0]);
  std::string result = value < 0 ? "⁻" : "";
  auto number = std::to_string(value < 0 ? -value : value);
  for (const auto digit : number)
    result += digits[static_cast<std::size_t>(digit - '0')];
  return result;
}

std::string scientific(double value) {
  if (value < 1'000'000.0)
    return grouped_integer(value);
  const auto exponent = static_cast<int>(std::floor(std::log10(value)));
  const auto coefficient = value / std::pow(10.0, exponent);
  return fixed(coefficient, 3) + " × 10" + superscript(exponent);
}

std::string physical_kind(stellar::core::PlanetaryBodyKind value) {
  switch (value) {
  case stellar::core::PlanetaryBodyKind::Moon: return "Moon";
  case stellar::core::PlanetaryBodyKind::DwarfPlanet: return "Dwarf planet";
  case stellar::core::PlanetaryBodyKind::Planet: return "Planet";
  }
  return "Unconfirmed";
}

std::string atmosphere(stellar::core::PlanetaryAtmosphereRegime value) {
  switch (value) {
  case stellar::core::PlanetaryAtmosphereRegime::OxygenNitrogen: return "Oxygen / nitrogen";
  case stellar::core::PlanetaryAtmosphereRegime::OxygenRich: return "Oxygen rich";
  case stellar::core::PlanetaryAtmosphereRegime::CarbonDioxideRich: return "Carbon dioxide";
  case stellar::core::PlanetaryAtmosphereRegime::Reducing: return "Reducing";
  case stellar::core::PlanetaryAtmosphereRegime::Inert: return "Inert";
  case stellar::core::PlanetaryAtmosphereRegime::Vacuum: return "Vacuum";
  case stellar::core::PlanetaryAtmosphereRegime::Other: return "Other";
  }
  return "Unconfirmed";
}

std::string confirmed_radius(double value) {
  const auto kilometres = value * earth_radius_kilometres;
  return std::isfinite(value) && value > 0.0 && std::isfinite(kilometres)
             ? grouped_integer(kilometres) + " km"
             : "Unconfirmed";
}
std::string confirmed_mass(double value) {
  const auto kilograms = value * earth_mass_kilograms;
  return std::isfinite(value) && value >= 0.0 && std::isfinite(kilograms)
             ? scientific(kilograms) + " kg"
             : "Unconfirmed";
}
std::string confirmed_gravity(double value) {
  const auto metric = value * standard_gravity_metres_per_second_squared;
  return std::isfinite(value) && value >= 0.0 && std::isfinite(metric)
             ? fixed(metric, 2) + " m/s²"
             : "Unconfirmed";
}
std::string confirmed_temperature(double value) {
  return std::isfinite(value) && value >= 0.0 ? fixed(value, 0) + " K" : "Unconfirmed";
}
std::string confirmed_pressure(double value) {
  return std::isfinite(value) && value >= 0.0 ? fixed(value, 2) + " kPa" : "Unconfirmed";
}
std::string confirmed_eccentricity(double value) {
  return std::isfinite(value) && value >= 0.0 && value < 1.0 ? fixed(value, 4) : "Unconfirmed";
}
std::string confirmed_inclination(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 180.0 ? fixed(value, 2) + "°" : "Unconfirmed";
}
} // namespace

std::optional<BodyInspection>
build_body_inspection(const native_system::NativeSystemSnapshot &snapshot,
                      const int body_id) {
  if (snapshot.survey_level < stellar::core::SystemSurveyLevel::partially_surveyed)
    return std::nullopt;
  const auto found = std::ranges::find(snapshot.bodies, body_id,
                                       &native_system::NativeSystemBody::id);
  if (found == snapshot.bodies.end())
    return std::nullopt;

  // Orbital drawing may retain approximate geometry after reconnaissance.
  // The inspector must not promote those values to confirmed measurements.
  const auto confirmed = snapshot.survey_level == stellar::core::SystemSurveyLevel::fully_surveyed &&
                         found->details.has_value();
  const auto hidden = [] { return std::string{"Unconfirmed"}; };
  BodySection physical{"Physical", {{"Type", physical_kind(found->kind)},
      {"World class", confirmed&&found->appearance&&!found->appearance->source_asset_id.starts_with("sol:")?stellar::core::planet_appearance_display_name(*found->appearance):confirmed && found->world_class ? std::string(stellar::core::planetary_world_class_name(*found->world_class)) : hidden()},
      {"Radius", confirmed ? confirmed_radius(found->radius_earth) : hidden()},
      {"Mass", confirmed ? confirmed_mass(found->details->mass_earth) : hidden()},
      {"Gravity", confirmed ? confirmed_gravity(found->details->gravity_g) : hidden()},
      {"Eccentricity", confirmed ? confirmed_eccentricity(found->orbital_eccentricity) : hidden()},
      {"Inclination", confirmed ? confirmed_inclination(found->orbital_inclination_degrees) : hidden()}}};
  BodySection environment{"Environment", {{"Temperature", confirmed ? confirmed_temperature(found->details->temperature_kelvin) : hidden()},
      {"Pressure", confirmed ? confirmed_pressure(found->details->pressure_kpa) : hidden()},
      {"Atmosphere", confirmed ? atmosphere(found->details->atmosphere) : hidden()}}};

  if(confirmed&&found->details->stellar_exposure) {
    const auto& e=*found->details->stellar_exposure;
    physical.facts.push_back({"Stellar distance",scientific(e.orbit_au*stellar::core::astronomical_unit_km)+" km"});
    environment.facts.push_back({"Stellar flux",fixed(e.incident_flux,3)+" Earth flux"});
    environment.facts.push_back({"Thermal zone",e.in_habitable_zone?"Temperate flux (other hazards apply)":"Outside temperate zone"});
    if(e.baked)environment.facts.push_back({"EXTREME IRRADIATION","Approach, settlement, surface work and mining prohibited"});
  }
  BodySection satellites{"Satellites & signals", {}};
  bool has_valid_parent = false;
  if (found->parent_body_id && *found->parent_body_id != found->id) {
    const auto parent = std::ranges::find(snapshot.bodies, *found->parent_body_id,
                                          &native_system::NativeSystemBody::id);
    if (parent != snapshot.bodies.end() && parent->id != found->id) {
      satellites.facts.push_back({"Orbits", parent->name});
      has_valid_parent = true;
    }
  }
  if (!has_valid_parent) {
    satellites.facts.push_back({"Known moons", std::to_string(std::ranges::count_if(
        snapshot.bodies, [&](const auto &candidate) {
          return candidate.id != found->id && candidate.kind == stellar::core::PlanetaryBodyKind::Moon &&
                 candidate.parent_body_id &&
                 *candidate.parent_body_id == found->id;
        }))});
  }
  const auto signal = [&](native_system::NativePositiveSignature kind, std::string label) {
    if (std::ranges::find(found->positive_signatures, kind) != found->positive_signatures.end())
      satellites.facts.push_back({std::move(label), "Signal detected"});
  };
  signal(native_system::NativePositiveSignature::rare_resource, "Resources");
  signal(native_system::NativePositiveSignature::activity, "Activity");
  signal(native_system::NativePositiveSignature::anomaly, "Anomaly");

  return BodyInspection{.body_id = found->id,
                        .name = found->name,
                        .survey_status = confirmed ? "SURVEY COMPLETE" : "DETAILED SURVEY NEEDED",
                        .confirmed = confirmed,
                        .sections = {std::move(physical), std::move(environment), std::move(satellites)}};
}

} // namespace stellar::native_system_ui
