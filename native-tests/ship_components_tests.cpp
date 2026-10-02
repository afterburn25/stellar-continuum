#include <stellar/core/fleet_persistence.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/legacy_technology.hpp>
#include <stellar/core/ship_components.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/shipyard_state.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool condition, const std::string &message) {
  if (!condition) fail(message);
}
void equal_number(double actual, double expected, const std::string &label) {
  const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
  check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-9 * scale,
        label);
}
bool has_issue(const AuthoredShipDesignValidation &result,
               const std::string &fragment) {
  return std::ranges::any_of(result.issues, [&](const std::string &issue) {
    return issue.find(fragment) != std::string::npos;
  });
}
template <typename Fn>
void check_throws_persistence(Fn &&fn, const std::string &label) {
  try {
    fn();
  } catch (const ShipDesignPersistenceError &) {
    return;
  }
  fail(label);
}
struct WorldFixture {
  std::vector<ConstructionState> construction{
      ConstructionState{.civilization_id = 7,
                        .completed_project_ids = {"orbital_shipyard"}}};
  std::vector<ShipbuildingCapabilities> capabilities;
  std::vector<AuthoredShipDesign> authored;
  void grant(std::vector<std::string> capability_ids) {
    capabilities.clear();
    capabilities.push_back(
        ShipbuildingCapabilities{7, std::move(capability_ids)});
  }
  ShipDesignReadView view() {
    return {construction, capabilities, {}, authored};
  }
};
const std::vector<std::string> all_capabilities{
    "spacecraft_construction", "experimental_interstellar_transit",
    "reliable_ftl", "extended_ftl_range"};
const std::vector<std::string> baseline_capabilities{
    "spacecraft_construction", "experimental_interstellar_transit"};
} // namespace

int run();
int main() {
  try {
    return run();
  } catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << "\n";
    return 1;
  }
}
int run() {
  // Catalog sanity: hulls and components resolve and slot plans are coherent.
  check(ship_hull_catalog().size() == 6, "expected six hulls");
  check(ship_component_catalog().size() == 18, "expected eighteen components");
  for (const auto &hull : ship_hull_catalog()) {
    for (const auto required : hull.required_slots)
      check(std::ranges::count(hull.slots, required) > 0,
            hull.id + " requires a slot kind it does not provide");
  }
  check(get_ship_hull("escort_frame").role == FleetRole::Military,
        "escort frame is military");
  check(get_ship_component("light_lance").slot == ShipComponentSlot::Weapon,
        "light lance is a weapon");
  check(ship_component_slot_name(ShipComponentSlot::Habitation) ==
            "Habitation",
        "slot names");

  WorldFixture world;
  world.grant(all_capabilities);

  AuthoredShipDesign gunboat{.id = "design.7.1",
                             .name = "Picket Gunboat",
                             .owner_civilization_id = 7,
                             .hull_id = "escort_frame",
                             .component_ids = {"ion_drive", "survey_warp_core",
                                               "light_lance", "coilgun_battery",
                                               "deflector_screen",
                                               "survey_array"}};
  auto validation = validate_authored_ship_design(gunboat, world.view(), 7);
  if (!validation.valid()) {
    for (const auto &issue : validation.issues)
      std::cerr << "unexpected issue: " << issue << "\n";
    fail("gunboat must validate");
  }

  const auto resolved = resolve_authored_ship_design(gunboat);
  check(resolved.id == "design.7.1", "resolved id");
  check(resolved.name == "Picket Gunboat", "resolved name");
  check(resolved.role == FleetRole::Military, "resolved role");
  equal_number(resolved.industry_cost, 700 + 60 + 70 + 110 + 140 + 130 + 50,
               "resolved industry cost");
  equal_number(resolved.credit_cost, 80 + 8 + 10 + 16 + 20 + 24 + 6,
               "resolved credit cost");
  equal_number(resolved.strategic_speed, 21 + 4, "resolved speed");
  equal_number(resolved.maximum_leg_range_light_years, 340, "leg range");
  equal_number(resolved.fuel_endurance_light_years, 800, "fuel endurance");
  equal_number(resolved.sensor_range, 125 + 30, "sensor range");
  check(resolved.crew_complement_individuals == 45 + 0 + 0 + 8 + 10 + 6 + 2,
        "resolved crew");

  const auto profile = resolve_authored_combat_profile(gunboat);
  equal_number(profile.max_shields, 55, "profile shields");
  equal_number(profile.max_armor, 60, "profile armor");
  equal_number(profile.max_hull, 90, "profile hull");
  equal_number(profile.weapon_damage, 18 + 26, "profile damage");
  equal_number(profile.weapon_interval_days, 6.0, "profile interval is min");
  equal_number(profile.retreat_delay_days, 8, "profile retreat delay");
  check(profile.has_weapon(), "gunboat has weapon");

  const auto loadout = massive_loadout_from_authored(gunboat);
  check(loadout.module_slot_capacity == 6, "loadout slot capacity");
  check(loadout.weapons.size() == 2, "loadout has both weapon groups");
  check(std::ranges::any_of(loadout.weapons,
                            [](const auto &w) {
                              return w.kind == MassiveWeaponKind::Beam;
                            }),
        "loadout carries beam group");

  // Authored resolution through the view; ownership is civilization-scoped.
  world.authored.push_back(gunboat);
  const auto via_view = resolve_ship_design(world.view(), 7, "design.7.1");
  check(via_view && via_view->name == "Picket Gunboat",
        "authored design resolves through view");
  const auto catalog = resolve_ship_design(world.view(), 7, "warp_scout");
  check(catalog && catalog->name == "Pathfinder Scout",
        "catalog fallback resolves");
  check(!resolve_ship_design(world.view(), 8, "design.7.1"),
        "other civilization cannot resolve owned design");
  check(!resolve_ship_design(world.view(), 7, "design.7.99"),
        "unknown design resolves to nullopt");

  // Validation failure modes.
  AuthoredShipDesign unknown_hull = gunboat;
  unknown_hull.hull_id = "missing";
  check(has_issue(validate_authored_ship_design(unknown_hull, world.view(), 7),
                  "Unknown hull"),
        "unknown hull rejected");

  AuthoredShipDesign unknown_component = gunboat;
  unknown_component.component_ids.push_back("missing");
  check(has_issue(validate_authored_ship_design(unknown_component,
                                                world.view(), 7),
                  "Unknown component"),
        "unknown component rejected");

  AuthoredShipDesign overflow = gunboat;
  overflow.component_ids = {"ion_drive", "ion_drive", "survey_warp_core",
                            "light_lance"};
  check(has_issue(validate_authored_ship_design(overflow, world.view(), 7),
                  "Too many Engine"),
        "slot overflow rejected");

  AuthoredShipDesign no_engine = gunboat;
  no_engine.component_ids = {"survey_warp_core", "light_lance",
                             "coilgun_battery", "deflector_screen",
                             "survey_array"};
  check(has_issue(validate_authored_ship_design(no_engine, world.view(), 7),
                  "Missing required Engine"),
        "missing required slot rejected");

  AuthoredShipDesign no_name = gunboat;
  no_name.name.clear();
  check(has_issue(validate_authored_ship_design(no_name, world.view(), 7),
                  "requires a name"),
        "empty name rejected");

  // Component prerequisites: gravity_snare needs extended_ftl_range.
  AuthoredShipDesign snare{.id = "design.7.3",
                           .name = "Snare Picket",
                           .owner_civilization_id = 7,
                           .hull_id = "survey_frame",
                           .component_ids = {"ion_drive", "survey_warp_core",
                                             "survey_array", "gravity_snare"}};
  world.grant(baseline_capabilities);
  check(has_issue(validate_authored_ship_design(snare, world.view(), 7),
                  "Gravity Snare"),
        "component prerequisite enforced");
  world.grant(all_capabilities);
  check(validate_authored_ship_design(snare, world.view(), 7).valid(),
        "component prerequisite satisfied");

  const auto snare_loadout = massive_loadout_from_authored(snare);
  check(std::ranges::any_of(snare_loadout.modules,
                            [](const auto &m) {
                              return m.kind ==
                                     MassiveModuleKind::WarpInterdictor;
                            }),
        "gravity snare contributes interdictor module");

  // Id allocation is deterministic and per-civilization.
  check(allocate_ship_design_id(world.authored, 7) == "design.7.2",
        "next id for owner");
  check(allocate_ship_design_id(world.authored, 9) == "design.9.1",
        "first id for new civilization");
  world.authored.push_back(AuthoredShipDesign{.id = "design.7.9",
                                              .owner_civilization_id = 7,
                                              .hull_id = "survey_frame"});
  check(allocate_ship_design_id(world.authored, 7) == "design.7.10",
        "allocation skips taken sequence");

  // --- Authoritative design service over FreshCampaignState (P2-M2) ---
  FreshCampaignState campaign;
  campaign.player_civilization_id = 7;
  campaign.civilizations.push_back(Civilization{.id = 7,
                                              .name = "Test Imperium",
                                              .home_system_id = 1,
                                              .species_id = "terran_baseline"});
  TechnologyState technology{.civilization_id = 7};
  technology.completed_technology_ids.insert("orbital_industry");
  technology.completed_technology_ids.insert("prototype_warp_drive");
  campaign.technologies.push_back(std::move(technology));
  campaign.construction.push_back(
      ConstructionState{.civilization_id = 7,
                        .completed_project_ids = {"orbital_shipyard"}});
  campaign.shipyards.push_back(ShipyardState{.civilization_id = 7});

  AuthoredShipDesign spec{.name = "Picket Gunboat",
                          .hull_id = "escort_frame",
                          .component_ids = {"ion_drive", "survey_warp_core",
                                            "light_lance", "coilgun_battery",
                                            "deflector_screen",
                                            "survey_array"}};
  const auto created = create_ship_design(campaign, 7, spec);
  check(created.accepted && created.design_id == "design.7.1",
        "design service allocates and commits: " + created.message);
  check(campaign.authored_ship_designs.size() == 1 &&
            campaign.authored_ship_designs.front().owner_civilization_id == 7,
        "committed design is owned by the civilization");

  spec.id = "design.7.1";
  check(!create_ship_design(campaign, 7, spec).accepted,
        "duplicate id rejected");
  check(!create_ship_design(campaign, 42, AuthoredShipDesign{}).accepted,
        "unknown civilization rejected");
  AuthoredShipDesign broken{.name = "Broken", .hull_id = "escort_frame",
                            .component_ids = {"ion_drive"}};
  const auto broken_result = create_ship_design(campaign, 7, broken);
  check(!broken_result.accepted &&
            broken_result.message.find("invalid") != std::string::npos,
        "structurally invalid spec rejected with diagnostics");
  check(campaign.authored_ship_designs.size() == 1,
        "rejected designs are not committed");

  const auto renamed = update_ship_design_metadata(
      campaign, 7, "design.7.1", "Picket Gunboat II", "refit");
  check(renamed.accepted &&
            campaign.authored_ship_designs.front().name == "Picket Gunboat II",
        "metadata update applies");
  check(!update_ship_design_metadata(campaign, 7, "design.7.1", "", "x")
             .accepted,
        "empty rename rejected");

  // Retirement is blocked while a shipyard order references the design.
  campaign.shipyards.front().queued_builds.push_back(
      ShipBuildOrderState{.order_id = "SY-7-2", .design_id = "design.7.1"});
  check(!retire_ship_design(campaign, 7, "design.7.1").accepted,
        "queued build blocks retirement");
  campaign.shipyards.front().queued_builds.clear();
  check(!retire_ship_design(campaign, 7, "design.7.9").accepted,
        "unknown design retire rejected");

  // --- Persistence codec round-trip and rejection paths ---
  AuthoredShipDesign extra{.id = "design.7.5",
                           .name = "Survey Skiff",
                           .owner_civilization_id = 7,
                           .hull_id = "survey_frame",
                           .component_ids = {"ion_drive", "survey_warp_core",
                                             "survey_array", "fuel_scoop"}};
  campaign.authored_ship_designs.push_back(extra);
  const auto captured =
      capture_authored_ship_designs(campaign.authored_ship_designs);
  const auto restored =
      restore_authored_ship_designs(captured, campaign.civilizations);
  check(restored.size() == 2, "round-trip count");
  check(restored[0].id == "design.7.1" && restored[0].name == "Picket Gunboat II" &&
            restored[0].component_ids.size() == 6,
        "round-trip preserves fields");
  auto bad_owner = captured;
  bad_owner[0].owner_civilization_id = 99;
  check_throws_persistence([&] {
    restore_authored_ship_designs(bad_owner, campaign.civilizations);
  }, "unknown owner rejected");
  auto bad_hull = captured;
  bad_hull[0].hull_id = "ghost_hull";
  check_throws_persistence([&] {
    restore_authored_ship_designs(bad_hull, campaign.civilizations);
  }, "unknown hull rejected");
  auto dup = captured;
  dup.push_back(dup.front());
  check_throws_persistence([&] {
    restore_authored_ship_designs(dup, campaign.civilizations);
  }, "duplicate id rejected");

  // --- Shipbuilding consumes authored designs end to end (P2-M3) ---
  campaign.authored_ship_designs.pop_back();
  std::vector<StellarSystem> systems{StellarSystem{.id = 1, .name = "Home"}};
  std::vector<Colony> colonies{Colony{.id = 20,
                                    .civilization_id = 7,
                                    .system_id = 1,
                                    .name = "Homeworld",
                                    .population_species_id = "terran_baseline",
                                    .population_millions = 3000.}};
  std::vector<CivilizationEconomy> economies{
      CivilizationEconomy{.civilization_id = 7, .credits = 1e9,
                          .industry = 1e9}};
  std::vector<FleetState> fleets;
  std::vector<ShipbuildingCapabilities> capabilities{
      ShipbuildingCapabilities{7, all_capabilities}};
  std::vector<ShipbuildingStrategicPreference> preferences;
  ShipbuildingWorld build_world{
      campaign.civilizations, systems,  campaign.construction,
      campaign.shipyards,   colonies, economies,
      fleets,               capabilities,
      preferences,          {},       {},
      {},                   campaign.authored_ship_designs};
  const auto started = start_ship_build(build_world, 7, "design.7.1");
  check(started.accepted, "authored design starts: " + started.message);
  const auto rejected =
      start_ship_build(build_world, 7, "design.7.99");
  check(!rejected.accepted, "unknown authored id rejected");
  const auto foreign =
      start_ship_build(build_world, 9, "design.7.1");
  check(!foreign.accepted, "authored design not shared across civilizations");

  for (int day = 0; day < 400 && fleets.empty(); ++day)
    advance_shipbuilding_for_civilization(build_world, 7, 1e9, 1.0);
  check(fleets.size() == 1, "authored build completes into a fleet");
  const auto &built = fleets.front();
  check(built.design_id && *built.design_id == "design.7.1",
        "authored design id stamped on the fleet");
  check(built.combat && built.combat->profile_override,
        "authored combat profile stamped");
  check(built.tactical_loadout &&
            std::ranges::any_of(built.tactical_loadout->weapons,
                                [](const auto &group) {
                                  return group.id == "coilgun_battery";
                                }),
        "component weapon groups reach tactical loadout");
  const auto &fighting =
      resolve_fleet_combat_profile(*built.combat, built.role);
  check(&fighting == &*built.combat->profile_override,
        "combat resolver prefers the authored override");

  // --- Doctrine validation + persistence of override/doctrine (P2-M3) ---
  auto &mutable_built = fleets.front();
  check(set_fleet_doctrine(mutable_built,
                           FleetDoctrine{FleetDoctrinePosture::EngageAtWill,
                                         0.35}),
        "valid doctrine accepted");
  check(mutable_built.doctrine.posture == FleetDoctrinePosture::EngageAtWill,
        "doctrine applied");
  check(!set_fleet_doctrine(
            mutable_built,
            FleetDoctrine{FleetDoctrinePosture::HoldFast, 1.5}),
        "out-of-range retreat fraction rejected");
  check(!set_fleet_doctrine(
            mutable_built,
            FleetDoctrine{FleetDoctrinePosture::HoldFast,
                          std::numeric_limits<double>::quiet_NaN()}),
        "NaN retreat fraction rejected");
  check(mutable_built.doctrine.posture == FleetDoctrinePosture::EngageAtWill,
        "rejected doctrine leaves state untouched");

  const auto fleet_dtos = capture_fleet_dtos(fleets);
  check(fleet_dtos.size() == 1 && fleet_dtos.front().doctrine &&
            fleet_dtos.front().doctrine->posture ==
                FleetDoctrinePosture::EngageAtWill,
        "doctrine captured");
  check(fleet_dtos.front().combat && fleet_dtos.front().combat->profile_override,
        "profile override captured");
  const auto round_fleets = restore_fleet_dtos(fleet_dtos, campaign.civilizations,
                                               16, false);
  check(round_fleets.size() == 1, "fleet round-trip count");
  const auto &round = round_fleets.front();
  check(round.doctrine.posture == FleetDoctrinePosture::EngageAtWill &&
            std::abs(round.doctrine.auto_retreat_hull_fraction - 0.35) < 1e-9,
        "doctrine survives restore");
  check(round.combat && round.combat->profile_override &&
            round.combat->profile_override->max_hull ==
                fighting.max_hull,
        "profile override survives restore");
  check(round.design_id && *round.design_id == "design.7.1" &&
            round.tactical_loadout,
        "design id and tactical loadout survive restore");

  std::cout << "ship_components tests passed\n";
  return 0;
}
