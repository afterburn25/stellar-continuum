#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_economy_projection.hpp>
#include <stellar/core/surface_economy.hpp>

#include <cmath>
#include <iostream>
#include <string>

// Economy projection tests — the read-only adapter that re-shapes
// authoritative colony sustenance state for the engine's
// analyze_economy diagnostics (reserve days, bottlenecks, aggregation).

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::core;

const stellar::engine::EconomyDiagnostic *
row_for(const std::vector<stellar::engine::EconomyDiagnostic> &rows,
        const std::string &resource) {
  for (const auto &row : rows)
    if (row.resource == resource) return &row;
  return nullptr;
}

} // namespace

int main() {
  const auto catalog = sustenance_economy_catalog();

  // Catalog sanity: the sustenance vocabulary validates clean and every
  // produced resource resolves a producer recipe.
  {
    check(catalog.validate().empty(), "catalog validates without issues");
    check(!catalog.graph().producers_of("res.food").empty(),
          "food has a producer recipe");
    check(!catalog.graph().producers_of("res.water").empty(),
          "water has a producer recipe");
  }

  Civilization civ;
  civ.id = 7;
  civ.name = "Projection Test";
  civ.home_system_id = 1;
  civ.species_id = "terran_baseline";
  const Civilization civs[1] = {civ};

  // Oversized colony on a lifeless body: sealed habitat capacity (500M
  // at infrastructure 1) plus zero natural biosphere -> demand unmet on
  // both reserves -> bottleneck rows.
  {
    PlanetaryBody rock;
    rock.id = 4;
    rock.system_id = 1;
    rock.name = "Barren";
    rock.kind = PlanetaryBodyKind::Planet;
    rock.radius_earth = 0.5;
    rock.mass_earth = 0.3;
    rock.environment.temperature_kelvin = 40.0;
    rock.environment.pressure_kpa = 0.0;
    rock.environment.gravity_g = 0.3;
    rock.environment.atmosphere = PlanetaryAtmosphereRegime::Vacuum;
    rock.environment.available_solvent = PlanetarySolventRegime::None;
    rock.environment.has_solid_surface = true;
    const PlanetaryBody bodies[1] = {rock};

    auto colonies = seed_legacy_colonies(civs);
    auto &colony = colonies.front();
    colony.system_id = 1;
    colony.planetary_body_id = 4;
    colony.population_millions = 2000.0;
    colony.stored_food_population_days_millions = 3000.0;
    colony.stored_water_population_days_millions = 700.0;
    const auto rows =
        analyze_colony_sustenance(colony, bodies, 30.0, catalog);
    const auto *food = row_for(rows, "res.food");
    const auto *water = row_for(rows, "res.water");
    check(food != nullptr && water != nullptr,
          "food and water diagnostics emitted");
    if (food) {
      check(food->bottleneck, "undersupplied colony food bottleneck");
      check(std::abs(food->demand_per_day - 2000.0) < 1e-9,
            "food demand equals population");
      check(std::abs(food->unmet_per_day -
                     std::max(0.0, 2000.0 - food->supply_per_day)) < 1e-9,
            "food unmet is demand minus supply");
      // Reserve days match the authoritative reserve preview exactly.
      const auto sustenance = colony_sustenance_capacity(
          bodies, colony, surface_sustenance_projection(
                              surface_colony_output(colony, 30.0)));
      const auto reserves =
          preview_colony_reserves(colony, sustenance, 30.0);
      check(std::abs(food->reserve_days - reserves.food_reserve_days) <
                1e-6,
            "food reserve days match preview_colony_reserves");
    }
    if (water)
      check(water->bottleneck, "undersupplied colony water bottleneck");
    // Every demanded resource has a producer recipe in the catalog ->
    // not import dependent (the colony lacks capacity, not a recipe).
    if (food) check(!food->import_dependent, "food not import dependent");
  }

  // Supplied colony: an agriculture complex covers demand -> no food
  // bottleneck; water still unmet without reclamation.
  {
    Colony colony;
    colony.id = 1;
    colony.civilization_id = 7;
    colony.population_millions = 100.0;
    colony.stored_food_population_days_millions = 100.0 * 30.0;
    colony.stored_water_population_days_millions = 100.0 * 7.0;
    SurfaceBuilding farm;
    farm.id = 1;
    farm.type_id = "controlled_agriculture";
    farm.is_complete = true;
    farm.condition = 1.0;
    farm.stored_power_days = 30.0;
    colony.surface_buildings.push_back(farm);

    const auto rows =
        analyze_colony_sustenance(colony, {}, 30.0, catalog);
    const auto *food = row_for(rows, "res.food");
    const auto *water = row_for(rows, "res.water");
    check(food != nullptr, "food row present");
    if (food) {
      check(food->supply_per_day >= 100.0,
            "farm supplies at least demand");
      check(!food->bottleneck, "supplied colony has no food bottleneck");
      const auto sustenance = colony_sustenance_capacity(
          {}, colony, surface_sustenance_projection(
                          surface_colony_output(colony, 30.0)));
      const auto expected =
          preview_colony_reserves(colony, sustenance, 30.0);
      check(std::abs(food->reserve_days - expected.food_reserve_days) <
                1e-6,
            "food reserve days match preview_colony_reserves");
    }
    // Sealed habitat capacity (500M) covers water for this population.
    check(water != nullptr && !water->bottleneck,
          "sealed capacity covers water demand");
  }

  // Civilization rollup: demand/production/stock aggregate over the
  // civ's colonies, sorted order, bit-equal across runs.
  {
    Colony a;
    a.id = 1;
    a.civilization_id = 7;
    a.population_millions = 100.0;
    a.stored_food_population_days_millions = 3000.0;
    a.stored_water_population_days_millions = 700.0;
    Colony b = a;
    b.id = 2;
    b.population_millions = 50.0;
    Colony foreign = a;
    foreign.id = 3;
    foreign.civilization_id = 99;
    const Colony all[3] = {a, b, foreign};
    const SettlementBodyIndex index(all, {});

    const auto first = analyze_civilization_sustenance(
        all, 7, index, 30.0, catalog);
    const auto second = analyze_civilization_sustenance(
        all, 7, index, 30.0, catalog);
    const auto *food = row_for(first, "res.food");
    check(food != nullptr, "rollup food row present");
    if (food)
      check(std::abs(food->demand_per_day - 150.0) < 1e-9,
            "rollup demand sums owned colonies only");
    check(first.size() == second.size(), "rollup deterministic size");
    if (first.size() == second.size())
      for (std::size_t i = 0; i < first.size(); ++i)
        check(first[i].resource == second[i].resource &&
                  first[i].demand_per_day == second[i].demand_per_day &&
                  first[i].supply_per_day == second[i].supply_per_day &&
                  first[i].unmet_per_day == second[i].unmet_per_day &&
                  first[i].reserve_days == second[i].reserve_days,
              "rollup diagnostics bit-equal");
  }

  // Consumer: inspect_campaign_operations surfaces the bottleneck as a
  // sustenance_shortfall finding on the operations diagnostics channel.
  {
    PlanetaryBody rock;
    rock.id = 4;
    rock.system_id = 1;
    rock.name = "Barren";
    rock.kind = PlanetaryBodyKind::Planet;
    rock.radius_earth = 0.5;
    rock.mass_earth = 0.3;
    rock.environment.temperature_kelvin = 40.0;
    rock.environment.pressure_kpa = 0.0;
    rock.environment.gravity_g = 0.3;
    rock.environment.atmosphere = PlanetaryAtmosphereRegime::Vacuum;
    rock.environment.available_solvent = PlanetarySolventRegime::None;
    rock.environment.has_solid_surface = true;

    FreshCampaignState world;
    world.bodies.push_back(rock);
    auto colonies = seed_legacy_colonies(civs);
    auto &colony = colonies.front();
    colony.system_id = 1;
    colony.planetary_body_id = 4;
    colony.population_millions = 2000.0;
    world.colonies = std::move(colonies);
    // A small body-less colony stays covered by the sealed fallback —
    // it must not produce a finding.
    Colony fine;
    fine.id = 50;
    fine.civilization_id = 7;
    fine.population_millions = 100.0;
    world.colonies.push_back(fine);

    const auto findings =
        inspect_campaign_operations(world, 0, 100.0, 128);
    int food_findings = 0;
    int water_findings = 0;
    for (const auto &finding : findings) {
      if (finding.event_type != "sustenance_shortfall") continue;
      check(finding.entity_id && *finding.entity_id == 0,
            "finding names the starving colony");
      check(finding.civilization_id && *finding.civilization_id == 7,
            "finding names the owning civilization");
      if (finding.message.find("res.food") != std::string::npos)
        ++food_findings;
      if (finding.message.find("res.water") != std::string::npos)
        ++water_findings;
    }
    check(food_findings == 1, "one food shortfall finding");
    check(water_findings == 1, "one water shortfall finding");
  }

  if (failures == 0)
    std::cout << "campaign economy projection tests passed\n";
  return failures;
}
