#include <stellar/core/campaign_population_projection.hpp>

#include <cmath>
#include <iostream>

// Population projection tests — the read-only adapter that re-shapes a
// Colony into a single-cohort engine Population plus authoritative
// SettlementConditions, so migration_pressure() describes the colony's
// emigration position without any competing growth authority.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool near(double a, double b, double eps = 1e-6) {
  return std::fabs(a - b) <= eps;
}

stellar::core::Colony colony(double population, double stability,
                             double infrastructure) {
  stellar::core::Colony c;
  c.id = 1;
  c.civilization_id = 7;
  c.system_id = 42;
  c.name = "Test Colony";
  c.kind = stellar::core::SettlementKind::Colony;
  c.population_species_id = "terran_baseline";
  c.population_millions = population;
  c.stability = stability;
  c.infrastructure = infrastructure;
  return c;
}

} // namespace

int main() try {
  using namespace stellar::core;

  // Healthy legacy colony: the authoritative sustenance math derives
  // natural capacity covering the population, so all ratios read 1.0;
  // stability 1.0 -> happiness 1.0, employment ~0.81 at infrastructure
  // 1.5 — emigration pressure stays low.
  {
    const auto projected =
        project_colony_population(colony(500.0, 1.0, 1.5), {}, 30.0, false);
    check(near(projected.population.total(), 500.0),
          "cohort carries the authoritative headcount");
    check(projected.population.cohort_count() == 1,
          "single cohort per colony");
    const auto *c = projected.population.cohort(projected.cohort);
    check(c != nullptr, "cohort resolves by key");
    check(c && c->key.profile == "terran_baseline",
          "cohort profile is the colony species");
    check(c && near(c->happiness, 1.0), "stable colony reads happy");
    check(c && c->employment_rate > 0.75 && c->employment_rate < 0.9,
          "employment reflects colony_labor capacity rate");
    check(near(projected.conditions.food_ratio, 1.0) &&
              near(projected.conditions.housing_ratio, 1.0) &&
              near(projected.conditions.overcrowding, 0.0),
          "legacy colony sustenance ratios read self-sufficient");
    check(near(projected.natural_habitability, 1.0),
          "no-body colony reads full habitability");
    const double pressure = projected.population.migration_pressure(
        projected.cohort, projected.conditions);
    check(pressure > 0.0 && pressure < 0.10,
          "healthy colony stays below unrest pressure");
  }

  // Stressed colony: low stability drags happiness; thin infrastructure
  // lowers the labor capacity rate — emigration pressure crosses the
  // diagnostics threshold.
  {
    const auto projected =
        project_colony_population(colony(500.0, 0.3, 0.3), {}, 30.0, false);
    const auto *c = projected.population.cohort(projected.cohort);
    check(c && near(c->happiness, 0.3), "unstable colony reads unhappy");
    const double pressure = projected.population.migration_pressure(
        projected.cohort, projected.conditions);
    check(pressure >= 0.10,
          "stressed colony crosses the unrest threshold");
    // The engine formula: tendency 0.5 x clamp01(0.4*(1-happy) +
    // 0.3*(1-employment) + 0.2*overcrowding + 0.1*(1-food)).
    const double expected =
        0.5 * std::clamp(0.4 * 0.7 + 0.3 * (1.0 - c->employment_rate),
                         0.0, 1.0);
    check(near(pressure, expected, 1e-9),
          "pressure matches the engine formula on projected inputs");
  }

  // Automation raises the labor capacity rate -> slightly less unrest.
  {
    const auto manual =
        project_colony_population(colony(500.0, 0.3, 0.3), {}, 30.0, false);
    const auto automated =
        project_colony_population(colony(500.0, 0.3, 0.3), {}, 30.0, true);
    const double p_manual = manual.population.migration_pressure(
        manual.cohort, manual.conditions);
    const double p_auto = automated.population.migration_pressure(
        automated.cohort, automated.conditions);
    check(p_auto < p_manual,
          "industrial automation eases emigration pressure");
  }

  // Species template: terran_baseline carries its authored lifespan and
  // metabolic demand into the demographic profile; an unknown species
  // still projects with framework defaults.
  {
    const auto projected =
        project_colony_population(colony(10.0, 1.0, 1.0), {}, 30.0, false);
    const auto *profile =
        projected.population.profile("terran_baseline");
    check(profile && near(profile->lifespan_years, 82.0),
          "authored species lifespan reaches the profile");
    check(profile && near(profile->food_per_capita_per_day, 1.0),
          "authored metabolic demand reaches the profile");

    // An unknown species is an authoritative invariant violation —
    // colony_habitat_support's validation propagates through the
    // projection rather than being silently tolerated.
    auto unknown = colony(10.0, 1.0, 1.0);
    unknown.population_species_id = "uncharted_species";
    bool threw = false;
    try {
      (void)project_colony_population(unknown, {}, 30.0, false);
    } catch (const std::out_of_range &) {
      threw = true;
    }
    check(threw, "unknown species surfaces as an invariant violation");
  }

  // Empty colony: zero headcount projects cleanly with neutral
  // conditions and zero pressure.
  {
    const auto projected =
        project_colony_population(colony(0.0, 1.0, 1.0), {}, 30.0, false);
    check(near(projected.population.total(), 0.0),
          "empty colony projects zero headcount");
    check(near(projected.conditions.food_ratio, 1.0) &&
              near(projected.conditions.overcrowding, 0.0),
          "empty colony reads neutral conditions");
    check(near(projected.population.migration_pressure(
                   projected.cohort, projected.conditions),
               0.0),
          "empty colony has no emigration pressure");
  }

  if (failures) {
    std::cerr << failures << " population projection test(s) failed.\n";
    return 1;
  }
  std::cout << "campaign population projection tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << "EXCEPTION: " << e.what() << '\n';
  return 2;
}
