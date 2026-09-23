#include <stellar/core/campaign_colony_projection.hpp>
#include <stellar/core/campaign_diagnostics.hpp>
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

  if (failures == 0)
    std::cout << "campaign colony projection tests passed\n";
  return failures;
}
