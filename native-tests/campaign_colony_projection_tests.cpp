#include <stellar/core/campaign_colony_projection.hpp>
#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

// Colony projection tests — the read-only adapter that reshapes an
// authoritative Core colony (surface buildings + catalog definitions +
// the power/staffing allocation) into an engine Colony settlement for
// aggregate utility/workforce accounting and diagnostics.

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

using namespace stellar::core;

SurfaceBuilding building(int id, std::string type, bool complete = true,
                         bool enabled = true, double condition = 1.0,
                         double progress = 0.0) {
  SurfaceBuilding b;
  b.id = id;
  b.type_id = std::move(type);
  b.is_complete = complete;
  b.is_enabled = enabled;
  b.condition = condition;
  b.industry_progress = progress;
  return b;
}

Colony make_colony(int id = 1, int civ = 1, int hub = 1) {
  Colony colony;
  colony.id = id;
  colony.civilization_id = civ;
  colony.system_id = 7;
  colony.kind = SettlementKind::Colony;
  colony.surface_hub_level = hub;
  colony.population_millions = 1.0; // 0.45 workforce at participation rate
  return colony;
}

double balance(const stellar::engine::Colony &settlement,
               const char *utility, bool supply) {
  for (const auto &[id, pair] : settlement.utility_balance())
    if (id == utility) return supply ? pair.first : pair.second;
  return 0.0;
}

} // namespace

int main() {
  // Baseline projection: spec synthesis, flags, construction remaining,
  // authoritative operating results, capacity bound.
  {
    auto colony = make_colony();
    colony.surface_buildings = {
        building(1, "power_generator"),
        building(2, "science_lab"),
        building(3, "habitat_complex"),
        building(4, "fabricator", /*complete=*/false, true, 1.0,
                 /*progress=*/100.0),
        building(5, "cargo_terminal", true, /*enabled=*/false),
        building(6, "trade_hub", true, true, /*condition=*/0.10),
    };

    const auto settlement = project_colony_settlement(colony);
    check(settlement.structure_count() == 6,
          "every surface building projects a structure");
    check(settlement.standalone_slots() == 16,
          "hub level 1 capacity becomes the standalone slot bound");

    const auto *spec = settlement.structure_spec("surface.power_generator");
    check(spec && spec->category == "power_generator" &&
              near(spec->utility_supply_per_day.front().amount, 4.0) &&
              near(spec->jobs, 0.020),
          "generator spec carries catalog power supply and workforce");

    const auto *s1 = settlement.structure(1);
    const auto *s2 = settlement.structure(2);
    const auto *s3 = settlement.structure(3);
    const auto *s4 = settlement.structure(4);
    const auto *s5 = settlement.structure(5);
    const auto *s6 = settlement.structure(6);
    check(s1 && s1->complete && s1->enabled &&
              near(s1->condition, 1.0) && near(s1->operating, 1.0),
          "complete generator projects complete/enabled/operating");
    check(s4 && !s4->complete && near(s4->construction_remaining, 350.0),
          "in-progress fabricator carries remaining industry (450-100)");
    check(s5 && s5->complete && !s5->enabled,
          "disabled building stays disabled");
    check(s6 && s6->enabled && !near(s6->operating, 1.0),
          "degraded building is not operating under Core allocation");

    // Powered allocation: supply 2 base + 4 generator = 6 grants all
    // three demands (science 2, habitat 2 -> trade is not operational).
    check(s2 && near(s2->operating, 1.0) && s3 && near(s3->operating, 1.0),
          "staffed+powered buildings report operating");

    // Nameplate utility balance counts complete+enabled specs — the
    // degraded trade hub still demands 2 (nameplate), disabled cargo
    // and in-progress fabricator are excluded.
    check(near(balance(settlement, "power", true), 4.0),
          "nameplate power supply is the generator's 4");
    check(near(balance(settlement, "power", false), 6.0),
          "nameplate power demand is science+habitat+trade = 6");
    check(near(settlement.jobs_total(), 0.020 + 0.050 + 0.015 + 0.030),
          "jobs_total sums complete+enabled workforce requirements");
    check(near(settlement.housing_capacity(), 1000.0),
          "housing capacity projects the habitat's 1000M support");
  }

  // Unknown type ids project under the fallback spec; the authoritative
  // allocator cannot run so nothing claims an operating result.
  {
    auto colony = make_colony();
    colony.surface_buildings = {building(1, "alien_relic"),
                                building(2, "power_generator")};
    const auto settlement = project_colony_settlement(colony);
    check(settlement.structure_count() == 2,
          "unknown type id still projects a structure");
    const auto *relic = settlement.structure(1);
    check(relic && relic->spec_id == "surface.unknown",
          "unknown type projects under the fallback spec");
    check(relic && near(relic->operating, 0.0),
          "unknown type blocks the allocator: operating is zero");
    check(settlement.structure_spec("surface.unknown") != nullptr,
          "fallback spec is defined");
  }

  // Hub-less colonies have capacity 0; the slot bound rises to the
  // structure count so presence is preserved (engine 0 == unlimited).
  {
    auto colony = make_colony(2, 1, /*hub=*/0);
    colony.surface_buildings = {building(1, "power_generator"),
                                building(2, "science_lab")};
    const auto settlement = project_colony_settlement(colony);
    check(settlement.standalone_slots() == 2,
          "zero-capacity colony raises the bound to structure count");
  }

  // Consumer: inspect_campaign_operations surfaces degraded_structures.
  {
    FreshCampaignState world;
    StellarSystem system;
    system.id = 7;
    system.name = "Test";
    system.position = {0.0f, 0.0f};
    world.systems.push_back(system);

    auto worn = make_colony(10, 1);
    worn.surface_buildings = {
        building(1, "power_generator"),
        building(2, "trade_hub", true, true, /*condition=*/0.10),
        building(3, "science_lab", true, true, /*condition=*/0.05),
    };
    world.colonies.push_back(worn);

    auto healthy = make_colony(11, 1);
    healthy.surface_buildings = {building(1, "power_generator"),
                                 building(2, "science_lab")};
    world.colonies.push_back(healthy);

    const auto findings = inspect_campaign_operations(world, 0, 100.0);
    int degraded = 0;
    for (const auto &finding : findings) {
      if (finding.event_type != "degraded_structures") continue;
      ++degraded;
      check(finding.entity_id && *finding.entity_id == 10,
            "finding names the degraded colony");
      check(finding.civilization_id && *finding.civilization_id == 1 &&
                finding.system_id && *finding.system_id == 7,
            "finding carries owner and system context");
      check(finding.values.count("degradedCount") &&
                near(std::get<double>(finding.values.at("degradedCount")),
                     2.0),
            "finding counts both degraded structures");
      check(finding.values.count("worstCondition") &&
                near(std::get<double>(finding.values.at("worstCondition")),
                     0.05),
            "finding reports the worst condition");
    }
    check(degraded == 1, "only the worn colony is flagged");
  }

  // Consumer: inspect_campaign_operations surfaces logistics supply
  // findings and the unrepresented freight-corridor gap, and skips
  // civilizations whose economy/construction rows are absent rather
  // than failing the pass.
  {
    FreshCampaignState world;
    StellarSystem home;
    home.id = 7;
    home.name = "Home";
    home.position = {0.0f, 0.0f};
    world.systems.push_back(home);
    StellarSystem far;
    far.id = 8;
    far.name = "Far";
    far.position = {30.0f, 0.0f};
    world.systems.push_back(far);

    Civilization civ;
    civ.id = 1;
    civ.name = "Strained Republic";
    civ.home_system_id = 7;
    world.civilizations.push_back(civ);
    Civilization healthy;
    healthy.id = 2;
    healthy.name = "Green Compact";
    healthy.home_system_id = 7;
    world.civilizations.push_back(healthy);
    Civilization sparse;
    sparse.id = 3;
    sparse.name = "Unstated Rim";
    sparse.home_system_id = 7;
    world.civilizations.push_back(sparse);

    world.economies.push_back({/*civilization_id=*/1});
    world.economies.push_back({/*civilization_id=*/2});
    ConstructionState con1;
    con1.civilization_id = 1;
    world.construction.push_back(con1);
    ConstructionState con2;
    con2.civilization_id = 2;
    world.construction.push_back(con2);
    // civ 3 deliberately has no economy or construction rows.
    Civilization overdrawn;
    overdrawn.id = 4;
    overdrawn.name = "Overdrawn League";
    overdrawn.home_system_id = 7;
    world.civilizations.push_back(overdrawn);
    CivilizationEconomy broke;
    broke.civilization_id = 4;
    broke.operating_arrears = 25.0;
    world.economies.push_back(broke);
    ConstructionState con4;
    con4.civilization_id = 4;
    world.construction.push_back(con4);

    Colony critical = make_colony(20, 1);
    critical.population_millions = 1000.0; // demand 1.25, local .72 -> .58
    world.colonies.push_back(critical);
    Colony strained = make_colony(21, 1);
    strained.system_id = 8; // external system -> corridor gap
    strained.population_millions = 1000.0;
    strained.infrastructure = 1.2; // coverage .72 -> Strained
    world.colonies.push_back(strained);
    Colony fine = make_colony(22, 2);
    fine.population_millions = 1000.0;
    fine.infrastructure = 2.0; // coverage ~1.31 -> Healthy
    world.colonies.push_back(fine);
    world.colonies.push_back(make_colony(23, 3));
    auto solvent = make_colony(24, 4);
    solvent.infrastructure = 2.0; // healthy colony; the arrears is treasury-side
    world.colonies.push_back(solvent);

    const auto findings = inspect_campaign_operations(world, 0, 100.0);
    int critical_n = 0, strained_n = 0, gap_n = 0, arrears_n = 0;
    for (const auto &finding : findings) {
      if (finding.event_type == "logistics_critical") {
        ++critical_n;
        check(finding.entity_id && *finding.entity_id == 20 &&
                  finding.severity == stellar::engine::DiagnosticSeverity::Warning,
              "critical finding names the under-covered colony");
      } else if (finding.event_type == "logistics_strained") {
        ++strained_n;
        check(finding.entity_id && *finding.entity_id == 21,
              "strained finding names the marginal colony");
      } else if (finding.event_type == "freight_corridor_gap") {
        ++gap_n;
        check(finding.civilization_id && *finding.civilization_id == 1,
              "corridor gap names the importing civilization");
      } else if (finding.event_type == "treasury_arrears") {
        ++arrears_n;
        check(finding.civilization_id && *finding.civilization_id == 4,
              "arrears finding names the overdrawn civilization");
        check(finding.values.count("operatingArrears") &&
                  near(std::get<double>(
                           finding.values.at("operatingArrears")),
                       25.0),
              "arrears finding reports the unpaid amount");
      }
    }
    check(critical_n == 1 && strained_n == 1 && gap_n == 1 &&
              arrears_n == 1,
          "logistics conditions, corridor gap and arrears each fire once");
  }

  // Invariants: corrupt stability/condition/arrears surface as
  // invalid_nonnegative_value findings (the ops pass skips those
  // inputs rather than throwing).
  {
    FreshCampaignState world;
    StellarSystem system;
    system.id = 7;
    system.name = "Test";
    system.position = {0.0f, 0.0f};
    world.systems.push_back(system);
    Civilization civ;
    civ.id = 1;
    civ.name = "Corrupt Holdings";
    civ.home_system_id = 7;
    world.civilizations.push_back(civ);

    auto bad = make_colony(30, 1);
    bad.stability = -1.0;
    bad.population_species_id = "voidborne"; // uncatalogued
    bad.planetary_body_id = 9; // body 9 exists but lives in system 8
    bad.surface_buildings = {
        building(1, "power_generator", true, true, /*condition=*/-0.5),
        building(2, "xeno_relic")}; // uncatalogued type
    world.colonies.push_back(bad);
    PlanetaryBody foreign;
    foreign.id = 9;
    foreign.system_id = 8;
    foreign.radius_earth = 1.0;
    foreign.mass_earth = 1.0;
    world.bodies.push_back(foreign);
    CivilizationEconomy broken;
    broken.civilization_id = 1;
    broken.operating_arrears = -3.0;
    world.economies.push_back(broken);
    ConstructionState con;
    con.civilization_id = 1;
    world.construction.push_back(con);
    // Zero strategic speed makes the warfare projection's class
    // definition throw; a duplicate system id makes lane construction
    // throw. Both must degrade to invariant findings, not kill the pass.
    FleetState dead_stick;
    dead_stick.id = 1;
    dead_stick.civilization_id = 1;
    dead_stick.is_active = true;
    dead_stick.current_system_id = 7;
    dead_stick.strategic_speed = 0.0;
    world.fleets.push_back(dead_stick);
    StellarSystem dup;
    dup.id = 7;
    dup.name = "Duplicate";
    world.systems.push_back(dup);

    const auto findings = inspect_campaign_invariants(world, 0, 100.0);
    int invalid = 0, species = 0, type = 0, orphan = 0, positive = 0,
        duplicate = 0;
    for (const auto &finding : findings) {
      if (finding.event_type == "invalid_nonnegative_value") ++invalid;
      else if (finding.event_type == "invalid_positive_value") ++positive;
      else if (finding.event_type == "unknown_species") ++species;
      else if (finding.event_type == "unknown_building_type") ++type;
      else if (finding.event_type == "orphaned_colony") ++orphan;
      else if (finding.event_type == "duplicate_id") ++duplicate;
    }
    check(invalid == 3,
          "stability, condition and arrears each flag invalid values");
    check(species == 1 && type == 1 && orphan == 1,
          "uncatalogued species/type and cross-system body are flagged");
    check(positive == 1 && duplicate == 1,
          "zero strategic speed and duplicate system are flagged");
    // The ops pass skips the corrupt entities rather than throwing —
    // before the guards, any of these escaped the whole pass.
    (void)inspect_campaign_operations(world, 0, 100.0);
  }

  if (failures == 0)
    std::cout << "campaign colony projection tests passed\n";
  return failures;
}
