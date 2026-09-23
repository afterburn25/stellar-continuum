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

std::string translate(const stellar::engine::LocalizationTable *locale,
                      std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string unit(const stellar::engine::LocalizationTable *locale,
                 std::string_view key, std::string_view fallback,
                 std::string value) {
  std::string pattern = translate(locale, key, fallback);
  if (const auto at = pattern.find("{0}"); at != std::string::npos)
    pattern.replace(at, 3, value);
  return pattern;
}

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

std::string physical_kind(stellar::core::PlanetaryBodyKind value,
                          const stellar::engine::LocalizationTable *locale) {
  switch (value) {
  case stellar::core::PlanetaryBodyKind::Moon: return translate(locale,"BODY_KIND_MOON","Moon");
  case stellar::core::PlanetaryBodyKind::DwarfPlanet: return translate(locale,"BODY_KIND_DWARF","Dwarf planet");
  case stellar::core::PlanetaryBodyKind::Planet: return translate(locale,"BODY_KIND_PLANET","Planet");
  }
  return translate(locale,"BODY_UNCONFIRMED","Unconfirmed");
}

std::string atmosphere(stellar::core::PlanetaryAtmosphereRegime value,
                       const stellar::engine::LocalizationTable *locale) {
  switch (value) {
  case stellar::core::PlanetaryAtmosphereRegime::OxygenNitrogen: return translate(locale,"BODY_ATMO_OXYGEN","Oxygen / nitrogen");
  case stellar::core::PlanetaryAtmosphereRegime::OxygenRich: return translate(locale,"BODY_ATMO_OXYGEN_RICH","Oxygen rich");
  case stellar::core::PlanetaryAtmosphereRegime::CarbonDioxideRich: return translate(locale,"BODY_ATMO_CO2","Carbon dioxide");
  case stellar::core::PlanetaryAtmosphereRegime::Reducing: return translate(locale,"BODY_ATMO_REDUCING","Reducing");
  case stellar::core::PlanetaryAtmosphereRegime::Inert: return translate(locale,"BODY_ATMO_INERT","Inert");
  case stellar::core::PlanetaryAtmosphereRegime::Vacuum: return translate(locale,"BODY_ATMO_VACUUM","Vacuum");
  case stellar::core::PlanetaryAtmosphereRegime::Other: return translate(locale,"BODY_ATMO_OTHER","Other");
  }
  return translate(locale,"BODY_UNCONFIRMED","Unconfirmed");
}

std::string confirmed_radius(double value,
                             const stellar::engine::LocalizationTable *locale) {
  const auto kilometres = value * earth_radius_kilometres;
  return std::isfinite(value) && value > 0.0 && std::isfinite(kilometres)
             ? unit(locale, "BODY_UNIT_KM", "{0} km", grouped_integer(kilometres))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_mass(double value,
                           const stellar::engine::LocalizationTable *locale) {
  const auto kilograms = value * earth_mass_kilograms;
  return std::isfinite(value) && value >= 0.0 && std::isfinite(kilograms)
             ? unit(locale, "BODY_UNIT_KG", "{0} kg", scientific(kilograms))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_gravity(double value,
                              const stellar::engine::LocalizationTable *locale) {
  const auto metric = value * standard_gravity_metres_per_second_squared;
  return std::isfinite(value) && value >= 0.0 && std::isfinite(metric)
             ? unit(locale, "BODY_UNIT_GRAVITY", "{0} m/s²", fixed(metric, 2))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_temperature(double value,
                                  const stellar::engine::LocalizationTable *locale) {
  return std::isfinite(value) && value >= 0.0
             ? unit(locale, "BODY_UNIT_KELVIN", "{0} K", fixed(value, 0))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_pressure(double value,
                               const stellar::engine::LocalizationTable *locale) {
  return std::isfinite(value) && value >= 0.0
             ? unit(locale, "BODY_UNIT_KPA", "{0} kPa", fixed(value, 2))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_eccentricity(double value,
                                   const stellar::engine::LocalizationTable *locale) {
  return std::isfinite(value) && value >= 0.0 && value < 1.0
             ? fixed(value, 4)
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
std::string confirmed_inclination(double value,
                                  const stellar::engine::LocalizationTable *locale) {
  return std::isfinite(value) && value >= 0.0 && value <= 180.0
             ? unit(locale, "BODY_UNIT_DEGREES", "{0}°", fixed(value, 2))
             : translate(locale, "BODY_UNCONFIRMED", "Unconfirmed");
}
} // namespace

std::optional<BodyInspection>
build_body_inspection(const native_system::NativeSystemSnapshot &snapshot,
                      const int body_id,
                      const stellar::engine::LocalizationTable *locale) {
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
  const auto hidden = [locale] { return translate(locale,"BODY_UNCONFIRMED","Unconfirmed"); };
  BodySection physical{translate(locale,"BODY_SECTION_PHYSICAL","Physical"), {{translate(locale,"BODY_FACT_TYPE","Type"), physical_kind(found->kind,locale)},
      {translate(locale,"BODY_FACT_CLASS","World class"), confirmed&&found->appearance&&!found->appearance->source_asset_id.starts_with("sol:")?stellar::core::planet_appearance_display_name(*found->appearance):confirmed && found->world_class ? std::string(stellar::core::planetary_world_class_name(*found->world_class)) : hidden()},
      {translate(locale,"BODY_FACT_RADIUS","Radius"), confirmed ? confirmed_radius(found->radius_earth,locale) : hidden()},
      {translate(locale,"BODY_FACT_MASS","Mass"), confirmed ? confirmed_mass(found->details->mass_earth,locale) : hidden()},
      {translate(locale,"BODY_FACT_GRAVITY","Gravity"), confirmed ? confirmed_gravity(found->details->gravity_g,locale) : hidden()},
      {translate(locale,"BODY_FACT_ECCENTRICITY","Eccentricity"), confirmed ? confirmed_eccentricity(found->orbital_eccentricity,locale) : hidden()},
      {translate(locale,"BODY_FACT_INCLINATION","Inclination"), confirmed ? confirmed_inclination(found->orbital_inclination_degrees,locale) : hidden()}}};
  BodySection environment{translate(locale,"BODY_SECTION_ENVIRONMENT","Environment"), {{translate(locale,"BODY_FACT_TEMPERATURE","Temperature"), confirmed ? confirmed_temperature(found->details->temperature_kelvin,locale) : hidden()},
      {translate(locale,"BODY_FACT_PRESSURE","Pressure"), confirmed ? confirmed_pressure(found->details->pressure_kpa,locale) : hidden()},
      {translate(locale,"BODY_FACT_ATMOSPHERE","Atmosphere"), confirmed ? atmosphere(found->details->atmosphere,locale) : hidden()}}};

  if(confirmed&&found->details->stellar_exposure) {
    const auto& e=*found->details->stellar_exposure;
    physical.facts.push_back({translate(locale,"BODY_FACT_DISTANCE","Stellar distance"),unit(locale,"BODY_UNIT_KM","{0} km",scientific(e.orbit_au*stellar::core::astronomical_unit_km))});
    environment.facts.push_back({translate(locale,"BODY_FACT_FLUX","Stellar flux"),unit(locale,"BODY_UNIT_FLUX","{0} Earth flux",fixed(e.incident_flux,3))});
    environment.facts.push_back({translate(locale,"BODY_FACT_ZONE","Thermal zone"),e.in_habitable_zone?translate(locale,"BODY_ZONE_TEMPERATE","Temperate flux (other hazards apply)"):translate(locale,"BODY_ZONE_OUTSIDE","Outside temperate zone")});
    if(e.baked)environment.facts.push_back({translate(locale,"BODY_IRRADIATION","EXTREME IRRADIATION"),translate(locale,"BODY_IRRADIATION_NOTE","Approach, settlement, surface work and mining prohibited")});
  }
  BodySection satellites{translate(locale,"BODY_SECTION_SATELLITES","Satellites & signals"), {}};
  bool has_valid_parent = false;
  if (found->parent_body_id && *found->parent_body_id != found->id) {
    const auto parent = std::ranges::find(snapshot.bodies, *found->parent_body_id,
                                          &native_system::NativeSystemBody::id);
    if (parent != snapshot.bodies.end() && parent->id != found->id) {
      satellites.facts.push_back({translate(locale,"BODY_FACT_ORBITS","Orbits"), parent->name});
      has_valid_parent = true;
    }
  }
  if (!has_valid_parent) {
    satellites.facts.push_back({translate(locale,"BODY_FACT_MOONS","Known moons"), std::to_string(std::ranges::count_if(
        snapshot.bodies, [&](const auto &candidate) {
          return candidate.id != found->id && candidate.kind == stellar::core::PlanetaryBodyKind::Moon &&
                 candidate.parent_body_id &&
                 *candidate.parent_body_id == found->id;
        }))});
  }
  const auto signal = [&](native_system::NativePositiveSignature kind, std::string label) {
    if (std::ranges::find(found->positive_signatures, kind) != found->positive_signatures.end())
      satellites.facts.push_back({std::move(label), translate(locale,"BODY_SIGNAL","Signal detected")});
  };
  signal(native_system::NativePositiveSignature::rare_resource, translate(locale,"BODY_SIG_RESOURCES","Resources"));
  signal(native_system::NativePositiveSignature::activity, translate(locale,"BODY_SIG_ACTIVITY","Activity"));
  signal(native_system::NativePositiveSignature::anomaly, translate(locale,"BODY_SIG_ANOMALY","Anomaly"));

  return BodyInspection{.body_id = found->id,
                        .name = found->name,
                        .survey_status = confirmed ? translate(locale,"BODY_SURVEY_COMPLETE","SURVEY COMPLETE") : translate(locale,"BODY_SURVEY_NEEDED","DETAILED SURVEY NEEDED"),
                        .confirmed = confirmed,
                        .sections = {std::move(physical), std::move(environment), std::move(satellites)}};
}

} // namespace stellar::native_system_ui
