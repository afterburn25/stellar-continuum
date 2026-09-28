#include "native_body_inspection.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using stellar::native_system::NativePositiveSignature;
using stellar::native_system::NativeSystemBody;
using stellar::native_system::NativeSystemBodyDetails;
using stellar::native_system::NativeSystemSnapshot;
using stellar::native_system_ui::BodyInspection;
using stellar::native_system_ui::build_body_inspection;

void require(bool condition, std::string_view expression, int line) {
  if (!condition)
    throw std::runtime_error("Body inspection check failed at line " +
                             std::to_string(line) + ": " + std::string(expression));
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)

const std::string &fact(const BodyInspection &inspection, std::string_view label) {
  for (const auto &section : inspection.sections)
    if (const auto result = std::ranges::find(section.facts, label,
                                               &stellar::native_system_ui::BodyFact::label);
        result != section.facts.end())
      return result->value;
  throw std::runtime_error("Missing fact: " + std::string(label));
}

bool has_fact(const BodyInspection &inspection, std::string_view label) {
  return std::ranges::any_of(inspection.sections, [&](const auto &section) {
    return std::ranges::find(section.facts, label,
                             &stellar::native_system_ui::BodyFact::label) != section.facts.end();
  });
}

NativeSystemSnapshot earth_snapshot() {
  NativeSystemSnapshot snapshot;
  snapshot.survey_level = stellar::core::SystemSurveyLevel::fully_surveyed;
  NativeSystemBody earth;
  earth.id = 3;
  earth.name = "Earth";
  earth.kind = stellar::core::PlanetaryBodyKind::Planet;
  earth.radius_earth = 1.0;
  earth.orbital_eccentricity = .0167;
  earth.orbital_inclination_degrees = 0.0;
  earth.positive_signatures = {NativePositiveSignature::rare_resource,
                               NativePositiveSignature::activity};
  earth.details = NativeSystemBodyDetails{.mass_earth = 1.0,
                                          .gravity_g = 1.0,
                                          .temperature_kelvin = 288.0,
                                          .pressure_kpa = 101.3,
                                          .atmosphere = stellar::core::PlanetaryAtmosphereRegime::OxygenNitrogen};
  snapshot.bodies.push_back(std::move(earth));
  return snapshot;
}
} // namespace

int main() {
  try {
    auto snapshot = earth_snapshot();
    auto inspection = build_body_inspection(snapshot, 3);
    REQUIRE(inspection && inspection->confirmed);
    REQUIRE(inspection->survey_status == "SURVEY COMPLETE");
    REQUIRE(fact(*inspection, "Type") == "Planet");
    REQUIRE(fact(*inspection, "Radius") == "6,371 km");
    REQUIRE(fact(*inspection, "Mass") == "5.972 × 10²⁴ kg");
    REQUIRE(fact(*inspection, "Gravity") == "9.81 m/s²");
    REQUIRE(fact(*inspection, "Eccentricity") == "0.0167");
    REQUIRE(fact(*inspection, "Inclination") == "0°");
    REQUIRE(fact(*inspection, "Temperature") == "288 K");
    REQUIRE(fact(*inspection, "Pressure") == "101.3 kPa");
    REQUIRE(fact(*inspection, "Atmosphere") == "Oxygen / nitrogen");
    REQUIRE(fact(*inspection, "Known moons") == "0");
    REQUIRE(fact(*inspection, "Resources") == "Signal detected");
    REQUIRE(fact(*inspection, "Activity") == "Signal detected");
    REQUIRE(!has_fact(*inspection, "Anomaly"));

    auto partial = snapshot;
    partial.survey_level = stellar::core::SystemSurveyLevel::partially_surveyed;
    const auto redacted = build_body_inspection(partial, 3);
    REQUIRE(redacted && !redacted->confirmed);
    REQUIRE(redacted->survey_status == "DETAILED SURVEY NEEDED");
    REQUIRE(fact(*redacted, "Radius") == "Unconfirmed");
    REQUIRE(fact(*redacted, "Mass") == "Unconfirmed");
    REQUIRE(fact(*redacted, "Eccentricity") == "Unconfirmed");
    REQUIRE(fact(*redacted, "Atmosphere") == "Unconfirmed");
    auto mutated = partial;
    mutated.bodies.front().radius_earth = 99.0;
    mutated.bodies.front().orbital_eccentricity = .88;
    mutated.bodies.front().details->mass_earth = 99.0;
    mutated.bodies.front().details->temperature_kelvin = 9000.0;
    REQUIRE(build_body_inspection(mutated, 3) == redacted);

    auto below_partial = snapshot;
    below_partial.survey_level = stellar::core::SystemSurveyLevel::detected;
    REQUIRE(!build_body_inspection(below_partial, 3));
    REQUIRE(!build_body_inspection(snapshot, 404));

    auto parent_cases = snapshot;
    parent_cases.bodies.front().parent_body_id = 99;
    REQUIRE(!has_fact(*build_body_inspection(parent_cases, 3), "Orbits"));
    parent_cases.bodies.front().parent_body_id = 3;
    REQUIRE(!has_fact(*build_body_inspection(parent_cases, 3), "Orbits"));
    NativeSystemBody parent{.id = 99, .name = "Known Parent"};
    parent_cases.bodies.front().parent_body_id = 99;
    parent_cases.bodies.push_back(parent);
    REQUIRE(fact(*build_body_inspection(parent_cases, 3), "Orbits") == "Known Parent");
    parent_cases.bodies.pop_back();
    parent_cases.bodies.front().parent_body_id.reset();
    NativeSystemBody moon{.id = 4, .parent_body_id = 3, .name = "Moon", .kind = stellar::core::PlanetaryBodyKind::Moon};
    NativeSystemBody self{.id = 3, .parent_body_id = 3, .name = "Duplicate"};
    parent_cases.bodies.push_back(moon);
    parent_cases.bodies.push_back(self);
    parent_cases.bodies.push_back(NativeSystemBody{.id = 5, .parent_body_id = 3, .name = "Non-moon"});
    REQUIRE(fact(*build_body_inspection(parent_cases, 3), "Known moons") == "1");

    auto invalid = snapshot;
    invalid.bodies.front().radius_earth = std::numeric_limits<double>::infinity();
    invalid.bodies.front().orbital_eccentricity = std::numeric_limits<double>::quiet_NaN();
    invalid.bodies.front().orbital_inclination_degrees = -1.0;
    invalid.bodies.front().details->mass_earth = std::numeric_limits<double>::max();
    invalid.bodies.front().details->gravity_g = -1.0;
    invalid.bodies.front().details->temperature_kelvin = -1.0;
    invalid.bodies.front().details->pressure_kpa = std::numeric_limits<double>::infinity();
    const auto invalid_inspection = build_body_inspection(invalid, 3);
    REQUIRE(invalid_inspection && invalid_inspection->confirmed);
    REQUIRE(fact(*invalid_inspection, "Radius") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Mass") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Gravity") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Eccentricity") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Inclination") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Temperature") == "Unconfirmed");
    REQUIRE(fact(*invalid_inspection, "Pressure") == "Unconfirmed");
    std::cout << "native body inspection tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
