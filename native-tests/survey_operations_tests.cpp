#include <stellar/core/survey_operations.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("Unsupported named number: " + text);
}
void equal_number(double actual, double expected, const std::string &field) {
  if (std::isnan(expected)) {
    check(std::isnan(actual), field + ": expected NaN");
    return;
  }
  if (std::isinf(expected)) {
    check(actual == expected, field + ": infinity mismatch");
    return;
  }
  const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
  check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-12 * scale,
        field + ": number mismatch");
}
template <typename T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = {static_cast<float>(number(value.at("Position").at("X"))),
                     static_cast<float>(number(value.at("Position").at("Y"))),
                     optional<double>(value.at("GalacticDepthLightYears"))};
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  result.catalog_preset_id = optional<std::string>(value.at("CatalogPresetId"));
  if (!value.at("StellarClass").is_null())
    result.primary =
        static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary =
        static_cast<StellarClass>(value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary =
        static_cast<StellarClass>(value.at("TertiaryStellarClass").get<int>());
  result.stellar_catalog_id =
      optional<std::string>(value.at("StellarCatalogId"));
  return result;
}

PlanetaryBody parse_body(const Json &value) {
  PlanetaryBody result;
  result.id = value.at("Id").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.parent_body_id = optional<int>(value.at("ParentBodyId"));
  result.orbit_index = value.at("OrbitIndex").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.kind = static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>());
  result.radius_earth = number(value.at("RadiusEarth"));
  result.mass_earth = number(value.at("MassEarth"));
  const auto &environment = value.at("Environment");
  result.environment = {number(environment.at("GravityG")),
                        number(environment.at("TemperatureKelvin")),
                        number(environment.at("PressureKPa")),
                        static_cast<PlanetaryAtmosphereRegime>(
                            environment.at("Atmosphere").get<int>()),
                        static_cast<PlanetarySolventRegime>(
                            environment.at("AvailableSolvent").get<int>()),
                        number(environment.at("RadiationHazard")),
                        environment.at("IsImmersedEnvironment").get<bool>(),
                        environment.at("HasSolidSurface").get<bool>()};
  result.legacy_colonization_candidate =
      value.at("LegacyColonizationCandidate").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  result.orbital_eccentricity = value.contains("OrbitalEccentricity")
                                    ? number(value.at("OrbitalEccentricity"))
                                    : 0.0;
  result.orbital_inclination_degrees =
      value.contains("OrbitalInclinationDegrees")
          ? number(value.at("OrbitalInclinationDegrees"))
          : 0.0;
  return result;
}

void equal_system(const StellarSystem &actual, const StellarSystem &expected,
                  const std::string &field) {
  check(actual.id == expected.id && actual.name == expected.name &&
            actual.archetype == expected.archetype &&
            actual.primary == expected.primary &&
            actual.secondary == expected.secondary &&
            actual.tertiary == expected.tertiary &&
            actual.catalog_preset_id == expected.catalog_preset_id &&
            actual.stellar_catalog_id == expected.stellar_catalog_id &&
            actual.has_habitable_world == expected.has_habitable_world &&
            actual.has_anomaly == expected.has_anomaly &&
            actual.has_rare_resource == expected.has_rare_resource &&
            actual.has_pre_warp_civilization ==
                expected.has_pre_warp_civilization &&
            actual.position.depth_light_years ==
                expected.position.depth_light_years,
        field + ": system mutation");
  equal_number(actual.position.x, expected.position.x, field + ".X");
  equal_number(actual.position.y, expected.position.y, field + ".Y");
}

void equal_body(const PlanetaryBody &actual, const PlanetaryBody &expected,
                const std::string &field) {
  check(actual.id == expected.id && actual.system_id == expected.system_id &&
            actual.parent_body_id == expected.parent_body_id &&
            actual.orbit_index == expected.orbit_index &&
            actual.name == expected.name && actual.kind == expected.kind &&
            actual.environment.atmosphere == expected.environment.atmosphere &&
            actual.environment.available_solvent ==
                expected.environment.available_solvent &&
            actual.environment.is_immersed_environment ==
                expected.environment.is_immersed_environment &&
            actual.environment.has_solid_surface ==
                expected.environment.has_solid_surface &&
            actual.legacy_colonization_candidate ==
                expected.legacy_colonization_candidate &&
            actual.has_rare_resource == expected.has_rare_resource &&
            actual.has_anomaly == expected.has_anomaly &&
            actual.has_pre_warp_civilization ==
                expected.has_pre_warp_civilization,
        field + ": body mutation");
  equal_number(actual.radius_earth, expected.radius_earth,
               field + ".RadiusEarth");
  equal_number(actual.mass_earth, expected.mass_earth, field + ".MassEarth");
  equal_number(actual.environment.gravity_g, expected.environment.gravity_g,
               field + ".GravityG");
  equal_number(actual.environment.temperature_kelvin,
               expected.environment.temperature_kelvin,
               field + ".TemperatureKelvin");
  equal_number(actual.environment.pressure_kpa,
               expected.environment.pressure_kpa, field + ".PressureKPa");
  equal_number(actual.environment.radiation_hazard,
               expected.environment.radiation_hazard,
               field + ".RadiationHazard");
  equal_number(actual.orbital_eccentricity, expected.orbital_eccentricity,
               field + ".OrbitalEccentricity");
  equal_number(actual.orbital_inclination_degrees,
               expected.orbital_inclination_degrees,
               field + ".OrbitalInclinationDegrees");
}

struct ErrorInfo {
  std::string type;
  std::string message;
};
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: survey_operations_tests <fixture.json>");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture");
    const auto fixture = Json::parse(stream);
    check(fixture.at("Format") == "stellar-survey-operations-oracle-v1",
          "Unsupported fixture format");
    check(fixture.at("NativeBoundary").at("NullGalaxy") ==
              "C# throws ArgumentNullException; native typed spans cannot "
              "represent null.",
          "Unexpected null boundary documentation");
    check(fixture.at("NativeBoundary").at("NullSystemRecord") ==
              "C# reference lists can contain null; native StellarSystem spans "
              "cannot.",
          "Unexpected null-system boundary documentation");
    check(fixture.at("NativeBoundary").at("NullBodyRecord") ==
              "C# reference lists can contain null; native PlanetaryBody spans "
              "cannot.",
          "Unexpected null-body boundary documentation");
    check(fixture.at("NativeBoundary").at("NullEnvironment") ==
              "C# PlanetaryBodyState can be constructed with a null "
              "environment; native embeds it by value.",
          "Unexpected null-environment boundary documentation");

    int passed = 0;
    SurveyOperationsProfiler profiler;
    for (const auto &test : fixture.at("Cases")) {
      const auto name = test.at("Name").get<std::string>();
      const auto kind = test.at("Kind").get<std::string>();
      check(kind == "Profile", name + ": unknown kind " + kind);
      const auto &arguments = test.at("Arguments");
      const auto system_id = arguments.at("SystemId").get<int>();
      std::vector<StellarSystem> systems;
      std::vector<PlanetaryBody> bodies;
      for (const auto &value : arguments.at("Systems"))
        systems.push_back(parse_system(value));
      for (const auto &value : arguments.at("Bodies"))
        bodies.push_back(parse_body(value));
      const auto systems_before = systems;
      const auto bodies_before = bodies;

      const auto expects_error = !test.at("Error").is_null();
      std::optional<ErrorInfo> wanted_error;
      if (expects_error) {
        wanted_error =
            ErrorInfo{test.at("Error").at("Type").get<std::string>(),
                      test.at("Error").at("Message").get<std::string>()};
        check(wanted_error->type == "InvalidOperationException",
              name + ": unsupported expected error type");
      }

      std::optional<SurveyOperationsProfile> result;
      std::optional<ErrorInfo> actual_error;
      try {
        result = profiler.build(systems, bodies, system_id);
      } catch (const std::exception &error) {
        actual_error = ErrorInfo{"InvalidOperationException", error.what()};
      }

      check(actual_error.has_value() == expects_error,
            name + ": error presence mismatch");
      if (wanted_error) {
        check(actual_error->type == wanted_error->type,
              name + ": error type mismatch");
        check(actual_error->message == wanted_error->message,
              name + ": error message mismatch: " + actual_error->message);
        check(test.at("Result").is_null(),
              name + ": error result must be null");
      } else {
        check(result.has_value(), name + ": result missing");
        const auto &expected = test.at("Result");
        check(result->system_id == expected.at("SystemId").get<int>(),
              name + ".SystemId");
        check(result->planet_count == expected.at("PlanetCount").get<int>(),
              name + ".PlanetCount");
        check(result->moon_count == expected.at("MoonCount").get<int>(),
              name + ".MoonCount");
        equal_number(result->estimated_science_survey_days,
                     number(expected.at("EstimatedScienceSurveyDays")),
                     name + ".EstimatedScienceSurveyDays");
        check(static_cast<int>(result->operational_hazard) ==
                  expected.at("OperationalHazard").get<int>(),
              name + ".OperationalHazard");
        equal_number(result->progress_per_day(),
                     number(expected.at("ProgressPerDay")),
                     name + ".ProgressPerDay");
      }

      check(systems.size() == systems_before.size(),
            name + ": systems size mutation");
      for (std::size_t index = 0; index < systems.size(); ++index)
        equal_system(systems[index], systems_before[index],
                     name + ".Systems[" + std::to_string(index) + "]");
      check(bodies.size() == bodies_before.size(), name + ": bodies mutation");
      for (std::size_t index = 0; index < bodies.size(); ++index)
        equal_body(bodies[index], bodies_before[index],
                   name + ".Bodies[" + std::to_string(index) + "]");
      ++passed;
    }
    std::cout << "survey operations parity passed " << passed << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "survey operations parity failed: " << error.what() << '\n';
    return 1;
  }
}
