#include <stellar/core/planetary_adapter.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

// Planetary adapter tests — the Core->engine environment projection
// that feeds Population environment_needs, Colony required_tags and
// Terraforming evaluation without engine dependence on game types.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::core;

bool has_tag(const stellar::engine::PlanetEnvironment &env,
             const std::string &tag) {
  return std::find(env.tags.begin(), env.tags.end(), tag) !=
         env.tags.end();
}

} // namespace

int main() {
  // Earth-like body: physical fields map directly, expected tag set.
  {
    PlanetaryBody body;
    body.id = 3;
    body.environment.gravity_g = 1.0;
    body.environment.temperature_kelvin = 288.0;
    body.environment.pressure_kpa = 101.325;
    body.environment.atmosphere =
        PlanetaryAtmosphereRegime::OxygenNitrogen;
    body.environment.available_solvent = PlanetarySolventRegime::Water;
    body.environment.has_solid_surface = true;

    const auto env = to_engine_environment(body);
    check(std::abs(env.temperature_k - 288.0) < 1e-9, "temperature direct");
    check(std::abs(env.atmosphere_atm - 1.0) < 1e-9,
          "pressure converted to atm");
    check(std::abs(env.gravity_g - 1.0) < 1e-9, "gravity direct");
    check(std::abs(env.water_fraction - 1.0) < 1e-9,
          "water solvent present");
    check(has_tag(env, "atmosphere.oxygen_nitrogen"), "atmosphere tag");
    check(has_tag(env, "solvent.water"), "solvent tag");
    check(!has_tag(env, "high_radiation"), "mild radiation untagged");
    check(std::is_sorted(env.tags.begin(), env.tags.end()),
          "tags sorted");
  }

  // Hostile body: vacuum gas giant, high radiation, anomaly.
  {
    PlanetaryBody body;
    body.environment.gravity_g = 0.0;
    body.environment.temperature_kelvin = 95.0;
    body.environment.pressure_kpa = 0.0;
    body.environment.atmosphere = PlanetaryAtmosphereRegime::Vacuum;
    body.environment.available_solvent = PlanetarySolventRegime::None;
    body.environment.radiation_hazard = 0.4;
    body.environment.has_solid_surface = false;
    body.has_anomaly = true;
    body.cracked_world = true;

    const auto env = to_engine_environment(body);
    check(std::abs(env.atmosphere_atm) < 1e-12, "vacuum pressure");
    check(env.water_fraction == 0.0, "no solvent -> no water");
    check(has_tag(env, "atmosphere.vacuum"), "vacuum tag");
    check(has_tag(env, "high_radiation"), "radiation tag at >0.10");
    check(has_tag(env, "gas_giant"), "no solid surface -> gas giant");
    check(has_tag(env, "anomaly") && has_tag(env, "cracked"),
          "body flags mapped");
  }

  // The projected environment feeds the engine habitability evaluator —
  // the adapter's purpose: engine frameworks consume Core data without
  // knowing Core types.
  {
    PlanetaryBody body;
    body.environment.gravity_g = 1.0;
    body.environment.temperature_kelvin = 288.0;
    body.environment.pressure_kpa = 101.325;
    body.environment.atmosphere =
        PlanetaryAtmosphereRegime::OxygenNitrogen;
    body.environment.available_solvent = PlanetarySolventRegime::Water;
    body.environment.has_solid_surface = true;

    stellar::engine::HabitabilityProfile terran;
    terran.id = "terran";
    terran.temperature_min_k = 240.0;
    terran.temperature_max_k = 320.0;
    terran.atmosphere_min = 0.5;
    terran.atmosphere_max = 2.0;
    terran.gravity_min_g = 0.5;
    terran.gravity_max_g = 1.5;
    terran.water_min = 0.2;
    terran.tolerance = 0.0; // hard bounds for a crisp check
    terran.required_tags = {"atmosphere.oxygen_nitrogen", "solvent.water"};
    terran.forbidden_tags = {"high_radiation"};

    const auto report = stellar::engine::evaluate_habitability(
        to_engine_environment(body), terran);
    check(report.habitable && report.suitability > 0.99,
          "projected earth is fully habitable to terrans");

    body.environment.radiation_hazard = 0.5;
    const auto irradiated = stellar::engine::evaluate_habitability(
        to_engine_environment(body), terran);
    check(!irradiated.habitable,
          "forbidden radiation tag fails habitability");
    check(std::find(irradiated.unmet.begin(), irradiated.unmet.end(),
                    std::string("forbidden:high_radiation")) !=
              irradiated.unmet.end(),
          "unmet reason records the forbidden tag");
  }

  if (failures == 0) {
    std::cout << "planetary adapter tests passed\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
