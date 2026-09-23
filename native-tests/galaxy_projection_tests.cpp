#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_projection.hpp>
#include <stellar/core/lane_network.hpp>

#include <cmath>
#include <iostream>

// Galaxy projection tests — the read-only adapter that re-shapes
// authoritative campaign geography (systems, lanes, colonies, fleets)
// into the engine GalaxyMap model for map/debugger surfaces.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool near(double a, double b, double eps = 1e-4) {
  return std::fabs(a - b) <= eps;
}

using namespace stellar::core;

StellarSystem make_system(int id, const char *name, float x, float y) {
  StellarSystem s;
  s.id = id;
  s.name = name;
  s.position.x = x;
  s.position.y = y;
  return s;
}

} // namespace

int main() {
  FreshCampaignState state;
  state.seed = 7;
  auto alpha = make_system(1, "Alpha", 0.0f, 0.0f);
  alpha.primary = StellarClass::GYellowDwarf;
  alpha.has_habitable_world = true;
  auto beta = make_system(2, "Beta", 4.0f, 3.0f);
  beta.primary = StellarClass::MRedDwarf;
  beta.has_anomaly = true;
  auto gamma = make_system(3, "Gamma", 8.0f, 6.0f);
  state.systems = {alpha, beta, gamma};

  Colony colony;
  colony.id = 10;
  colony.civilization_id = 1;
  colony.system_id = 2;
  colony.name = "Haven";
  state.colonies = {colony};

  FleetState anchored;
  anchored.id = 20;
  anchored.civilization_id = 1;
  anchored.name = "Survey Wing";
  anchored.current_system_id = 1;
  anchored.destination_system_id = 3;
  FleetState transit;
  transit.id = 21;
  transit.civilization_id = 2;
  transit.position = {2.0f, 1.5f};
  transit.destination_system_id = 2;
  state.fleets = {anchored, transit};

  const auto map = project_galaxy_map(state);

  // Systems carry positions, class labels and flag tags.
  check(map.system_count() == 3, "system count");
  const auto *a = map.system(1);
  check(a && a->name == "Alpha" && near(a->x_light_years, 0.0) &&
            a->classification == "G yellow dwarf",
        "alpha mapping");
  bool habitable = false, anomaly = false;
  for (const auto &tag : a->tags)
    habitable = habitable || tag == "habitable";
  for (const auto &tag : map.system(2)->tags)
    anomaly = anomaly || tag == "anomaly";
  check(habitable && anomaly, "flag tags");

  // Lanes come from the authoritative lane network — every lane connects
  // mapped systems and preserves its length.
  InterstellarLaneNetwork expected(&state.systems);
  const auto expected_lanes = expected.build();
  check(map.lane_count() == expected_lanes.size(), "lane count");
  for (const auto lane_id : map.lane_ids()) {
    const auto *lane = map.lane(lane_id);
    check(lane && map.system(lane->first_system_id) &&
              map.system(lane->second_system_id),
          "lane endpoints mapped");
  }

  // Colony marker anchors to its system with civ ownership.
  const auto markers2 = map.markers_in_system(2);
  bool found_colony = false;
  for (const auto id : markers2) {
    const auto *m = map.marker(id);
    if (m->kind == stellar::engine::GalaxyMarker::Kind::Colony) {
      found_colony = true;
      check(m->label == "Haven" && m->owner_id == 1 &&
                near(m->x_light_years, 4.0),
            "colony marker mapping");
    }
  }
  check(found_colony, "colony marker present");

  // Anchored fleet rides its system; transit fleet keeps galactic
  // position and destination.
  const auto markers1 = map.markers_in_system(1);
  bool found_anchored = false;
  for (const auto id : markers1) {
    const auto *m = map.marker(id);
    if (m->kind == stellar::engine::GalaxyMarker::Kind::Fleet &&
        m->label == "Survey Wing") {
      found_anchored = true;
      check(m->destination_system_id.value_or(0) == 3,
            "fleet destination preserved");
    }
  }
  check(found_anchored, "anchored fleet marker");
  bool found_transit = false;
  for (const auto id : map.markers_for_owner(2)) {
    const auto *m = map.marker(id);
    if (m->kind == stellar::engine::GalaxyMarker::Kind::Fleet) {
      found_transit = true;
      check(!m->system_id && near(m->x_light_years, 2.0) &&
                near(m->y_light_years, 1.5),
            "transit fleet position");
    }
  }
  check(found_transit, "transit fleet marker");

  // A colony on an absent system is skipped rather than fabricating a
  // marker at the origin.
  {
    FreshCampaignState sparse;
    sparse.systems = {make_system(1, "Solo", 0.0f, 0.0f)};
    Colony orphan;
    orphan.id = 1;
    orphan.civilization_id = 1;
    orphan.system_id = 99;
    sparse.colonies = {orphan};
    const auto sparse_map = project_galaxy_map(sparse);
    check(sparse_map.system_count() == 1 && sparse_map.marker_count() == 0,
          "orphan colony skipped");
  }

  // Determinism: same input projects byte-equivalent state.
  {
    const auto first = project_galaxy_map(state).capture_state();
    const auto second = project_galaxy_map(state).capture_state();
    check(first.systems.size() == second.systems.size() &&
              first.lanes.size() == second.lanes.size() &&
              first.markers.size() == second.markers.size(),
          "deterministic projection sizes");
    bool identical = true;
    for (std::size_t i = 0; i < first.markers.size(); ++i)
      identical = identical && first.markers[i].id == second.markers[i].id &&
                  first.markers[i].label == second.markers[i].label;
    check(identical, "deterministic marker order");
  }

  if (failures == 0)
    std::cout << "galaxy_projection tests passed\n";
  return failures == 0 ? 0 : 1;
}
