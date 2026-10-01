#include <stellar/core/freight.hpp>

#include <cmath>
#include <iostream>

// ADR 0002 option-(a) coverage for the freight phase: per-advance caches of
// the port transfer rate, industry storage capacity and the construction/
// fleet projections must preserve exact per-tick transfer arithmetic —
// including the clamp-binding case where a cached capacity meets a live
// (mutating) industry stockpile mid-loop.

using namespace stellar::core;
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
bool near(double a, double b, double eps = 1e-6) {
  return std::abs(a - b) < eps;
}

struct World {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<ConstructionState> construction;
  std::vector<CivilizationEconomy> economies;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  World() {
    for (int i = 0; i < 2; ++i) {
      StellarSystem s;
      s.id = i;
      s.name = "System " + std::to_string(i);
      s.position = {i * 50.f, 0.f, {}};
      systems.push_back(s);
    }
    Civilization c;
    c.id = 0;
    c.home_system_id = 0;
    c.name = "Haulage authority";
    c.development_stage = CivilizationDevelopmentStage::WarpCapable;
    civilizations.push_back(c);
    ConstructionState built;
    built.civilization_id = 0;
    construction.push_back(built);
    CivilizationEconomy economy;
    economy.civilization_id = 0;
    economies.push_back(economy);
    Colony home;
    home.id = 1;
    home.civilization_id = 0;
    home.system_id = 0;
    home.name = "Home";
    home.kind = SettlementKind::Colony;
    home.infrastructure = 1.0; // contributes 500 + 500*1.0 industry capacity
    colonies.push_back(home);
    Colony mine;
    mine.id = 2;
    mine.civilization_id = 0;
    mine.system_id = 1;
    mine.name = "Mine";
    mine.kind = SettlementKind::ResourceOutpost;
    mine.stored_extracted_materials = 500;
    colonies.push_back(mine);
  }
  FreightWorldView view(InterstellarLaneNetwork &lanes) {
    return {systems, civilizations, bodies, construction,
            fleets,  colonies,      economies, lanes};
  }
  FleetState &freighter(int id, int system) {
    fleets.emplace_back();
    auto &f = fleets.back();
    f.id = id;
    f.civilization_id = 0;
    f.name = "Freighter " + std::to_string(id);
    f.role = FleetRole::Logistics;
    f.current_system_id = system;
    f.cargo_material_capacity = 400;
    f.fuel_capacity_light_years = 500;
    f.fuel_remaining_light_years = 500;
    f.maximum_leg_range_light_years = 110;
    return f;
  }
};

int main() try {
  // Two freighters unloading at the same home colony: capacity 1500 (both
  // owned colonies contribute 500 + 500*infrastructure), opening industry
  // 1494, effective transfer 4/day (port-bound). The cached capacity must
  // combine with live industry so the clamp binds mid-loop exactly as
  // uncached arithmetic would.
  {
    World world;
    InterstellarLaneNetwork lanes(world.systems);
    // Both owned colonies contribute infrastructure capacity: 500 + 500 +
    // 500 = 1500 total. Industry 1494 leaves exactly six units of headroom.
    world.economies[0].industry = 1494;
    world.freighter(1, 0);
    world.freighter(2, 0);
    for (auto &f : world.fleets) {
      f.freight_home_colony_id = 1;
      f.cargo_materials = 100;
    }
    auto &first = world.fleets[0];
    auto &second = world.fleets[1];

    FreightSimulation freight;
    freight.advance(world.view(lanes), 1.0);
    check(near(first.cargo_materials, 96) && near(second.cargo_materials, 98),
          "Storage clamp did not bind on the second freighter mid-loop");
    check(near(world.economies[0].industry, 1500),
          "Industry overflowed the cached storage capacity");
    freight.advance(world.view(lanes), 1.0);
    check(near(first.cargo_materials, 96) && near(second.cargo_materials, 98) &&
              near(world.economies[0].industry, 1500),
          "Full storage accepted further transfers");
  }

  // Two freighters loading at the same outpost: the cached port rate must
  // keep per-fleet loads and the shared stored decrement exact.
  {
    World world;
    InterstellarLaneNetwork lanes(world.systems);
    world.freighter(3, 1);
    world.freighter(4, 1);
    for (auto &f : world.fleets) {
      f.freight_home_colony_id = 1;
      f.freight_target_outpost_id = 2;
    }
    auto &first = world.fleets[0];
    auto &second = world.fleets[1];

    FreightSimulation freight;
    freight.advance(world.view(lanes), 1.0);
    check(near(first.cargo_materials, 4) && near(second.cargo_materials, 4),
          "Outpost loads did not use the shared port rate");
    check(near(world.colonies[1].stored_extracted_materials, 492),
          "Cached transfer rate corrupted the outpost stockpile");
    freight.advance(world.view(lanes), 1.0);
    check(near(first.cargo_materials, 8) && near(second.cargo_materials, 8) &&
              near(world.colonies[1].stored_extracted_materials, 484),
          "Second loading tick diverged under the cached rate");
  }

  // Fleets outside the freight contract still skip cleanly — no caches are
  // consulted or populated for them.
  {
    World world;
    InterstellarLaneNetwork lanes(world.systems);
    world.freighter(5, 0);
    auto &idle = world.fleets[0];
    idle.cargo_materials = 10; // no freight run assigned
    const double opening_industry = world.economies[0].industry;
    FreightSimulation freight;
    freight.advance(world.view(lanes), 1.0);
    check(near(idle.cargo_materials, 10) &&
              near(world.economies[0].industry, opening_industry),
          "Idle logistics fleet entered the transfer path");
  }

  std::cout << "freight cadence tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
