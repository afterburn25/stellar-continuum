#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stellar/core/shipbuilding.hpp>
using Json = nlohmann::json;
using namespace stellar::core;
namespace {
void check(bool value, const std::string &label) {
  if (!value)
    throw std::runtime_error(label);
}
double read_number(const Json &value);
void number(double a, const Json &b, const std::string &label) {
  double e = read_number(b);
  double scale = std::max({1., std::abs(a), std::abs(e)});
  check((std::isnan(a) && std::isnan(e)) || (std::isinf(a) && a == e) ||
            (std::isfinite(a) && std::isfinite(e) &&
             std::abs(a - e) <= 1e-6 * scale),
        label);
}
double read_number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("Unknown named floating-point value");
}
template <class T>
void opt(const std::optional<T> &a, const Json &e, const std::string &l) {
  check(a.has_value() != e.is_null(), l + " presence");
  if (a)
    check(*a == e.get<T>(), l);
}
template <class T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(read_number(value.at("X"))),
          static_cast<float>(read_number(value.at("Y")))};
}
FleetCombatState parse_combat(const Json &v) {
  FleetCombatState r;
  r.profile_id = v.at("ProfileId");
  r.shields = read_number(v.at("Shields"));
  r.armor = read_number(v.at("Armor"));
  r.hull = read_number(v.at("Hull"));
  r.weapon_cooldown_remaining_days =
      read_number(v.at("WeaponCooldownRemainingDays"));
  r.order = static_cast<MilitaryOrderType>(v.at("Order").get<int>());
  r.target_fleet_id = optional_value<int>(v.at("TargetFleetId"));
  r.defend_system_id = optional_value<int>(v.at("DefendSystemId"));
  r.retreat_progress_days = read_number(v.at("RetreatProgressDays"));
  r.retreat_started = v.at("RetreatStarted");
  r.is_disengaged = v.at("IsDisengaged");
  r.disengaged_system_id = optional_value<int>(v.at("DisengagedSystemId"));
  return r;
}
MassiveCombatLoadout parse_loadout(const Json &v) {
  MassiveCombatLoadout r;
  r.mass_per_ship = static_cast<float>(read_number(v.at("MassPerShip")));
  r.acceleration = static_cast<float>(read_number(v.at("Acceleration")));
  r.maximum_speed = static_cast<float>(read_number(v.at("MaximumSpeed")));
  r.shield_per_ship = static_cast<float>(read_number(v.at("ShieldPerShip")));
  r.armor_per_ship = static_cast<float>(read_number(v.at("ArmorPerShip")));
  r.hull_per_ship = static_cast<float>(read_number(v.at("HullPerShip")));
  r.reactor_output_per_ship =
      static_cast<float>(read_number(v.at("ReactorOutputPerShip")));
  r.cooling_per_ship = static_cast<float>(read_number(v.at("CoolingPerShip")));
  r.warp_stabilization =
      static_cast<float>(read_number(v.at("WarpStabilization")));
  r.warp_spool_seconds =
      static_cast<float>(read_number(v.at("WarpSpoolSeconds")));
  r.module_slot_capacity = v.at("ModuleSlotCapacity");
  r.maximum_module_mass =
      static_cast<float>(read_number(v.at("MaximumModuleMass")));
  for (const auto &x : v.at("Weapons")) {
    MassiveWeaponGroup w;
    w.id = x.at("Id");
    w.kind = static_cast<MassiveWeaponKind>(x.at("Kind").get<int>());
    w.mounts_per_ship = x.at("MountsPerShip");
    w.damage_per_shot = static_cast<float>(read_number(x.at("DamagePerShot")));
    w.shots_per_second =
        static_cast<float>(read_number(x.at("ShotsPerSecond")));
    w.range = static_cast<float>(read_number(x.at("Range")));
    w.accuracy = static_cast<float>(read_number(x.at("Accuracy")));
    w.power_per_second =
        static_cast<float>(read_number(x.at("PowerPerSecond")));
    w.heat_per_second = static_cast<float>(read_number(x.at("HeatPerSecond")));
    r.weapons.push_back(w);
  }
  for (const auto &x : v.at("Modules")) {
    MassiveModuleState m;
    m.id = x.at("Id");
    m.kind = static_cast<MassiveModuleKind>(x.at("Kind").get<int>());
    m.installed_count = x.at("InstalledCount");
    m.mass_each = static_cast<float>(read_number(x.at("MassEach")));
    m.power_per_second_each =
        static_cast<float>(read_number(x.at("PowerPerSecondEach")));
    m.heat_per_second_each =
        static_cast<float>(read_number(x.at("HeatPerSecondEach")));
    m.condition = static_cast<float>(read_number(x.at("Condition")));
    m.enabled = x.at("Enabled");
    m.effective_range = static_cast<float>(read_number(x.at("EffectiveRange")));
    m.field_strength = static_cast<float>(read_number(x.at("FieldStrength")));
    m.detection_signature =
        static_cast<float>(read_number(x.at("DetectionSignature")));
    m.slots = x.at("Slots");
    r.modules.push_back(m);
  }
  return r;
}
MassiveVesselState parse_vessel(const Json &v) {
  MassiveVesselState r;
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.design_id = v.at("DesignId");
  r.is_flagship = v.at("IsFlagship");
  r.is_carrier = v.at("IsCarrier");
  r.is_interdictor = v.at("IsInterdictor");
  r.is_story_ship = v.at("IsStoryShip");
  r.hull_fraction = static_cast<float>(read_number(v.at("HullFraction")));
  r.engine_fraction = static_cast<float>(read_number(v.at("EngineFraction")));
  r.sensor_fraction = static_cast<float>(read_number(v.at("SensorFraction")));
  r.warp_drive_fraction =
      static_cast<float>(read_number(v.at("WarpDriveFraction")));
  r.reactor_fraction = static_cast<float>(read_number(v.at("ReactorFraction")));
  r.interdictor_fraction =
      static_cast<float>(read_number(v.at("InterdictorFraction")));
  r.battles_fought = v.at("BattlesFought");
  r.confirmed_kills = v.at("ConfirmedKills");
  r.destroyed = v.at("Destroyed");
  r.escaped = v.at("Escaped");
  return r;
}
FleetState parse_fleet(const Json &v) {
  FleetState r;
  r.id = v.at("Id");
  r.civilization_id = v.at("CivilizationId");
  r.name = v.at("Name");
  r.role = static_cast<FleetRole>(v.at("Role").get<int>());
  r.design_id = optional_value<std::string>(v.at("DesignId"));
  r.position = parse_vec2(v.at("Position"));
  r.current_system_id = optional_value<int>(v.at("CurrentSystemId"));
  r.destination_system_id = optional_value<int>(v.at("DestinationSystemId"));
  r.transit_phase =
      static_cast<FleetTransitPhase>(v.at("TransitPhase").get<int>());
  r.transit_origin_system_id =
      optional_value<int>(v.at("TransitOriginSystemId"));
  r.transit_target_system_id =
      optional_value<int>(v.at("TransitTargetSystemId"));
  r.transit_progress = read_number(v.at("TransitProgress"));
  r.local_transit_start = parse_vec2(v.at("LocalTransitStart"));
  r.local_transit_position = parse_vec2(v.at("LocalTransitPosition"));
  r.local_transit_target = parse_vec2(v.at("LocalTransitTarget"));
  r.planned_route_system_ids =
      v.at("PlannedRouteSystemIds").get<std::vector<int>>();
  r.hold_requested = v.at("HoldRequested");
  r.return_to_base_requested = v.at("ReturnToBaseRequested");
  r.return_to_base_failure_reason =
      optional_value<std::string>(v.at("ReturnToBaseFailureReason"));
  r.mission_order_revision = v.at("MissionOrderRevision");
  r.destination_planetary_body_id =
      optional_value<int>(v.at("DestinationPlanetaryBodyId"));
  r.prevent_automatic_settlement = v.at("PreventAutomaticSettlement");
  r.settlement_body_id = optional_value<int>(v.at("SettlementBodyId"));
  r.settlement_days_completed = read_number(v.at("SettlementDaysCompleted"));
  r.reconnaissance_system_id =
      optional_value<int>(v.at("ReconnaissanceSystemId"));
  r.reconnaissance_days_completed =
      read_number(v.at("ReconnaissanceDaysCompleted"));
  r.freight_target_outpost_id =
      optional_value<int>(v.at("FreightTargetOutpostId"));
  r.freight_home_colony_id = optional_value<int>(v.at("FreightHomeColonyId"));
  r.cargo_material_capacity = read_number(v.at("CargoMaterialCapacity"));
  r.cargo_materials = read_number(v.at("CargoMaterials"));
  r.strategic_speed = read_number(v.at("StrategicSpeed"));
  r.maximum_leg_range_light_years =
      read_number(v.at("MaximumLegRangeLightYears"));
  r.fuel_capacity_light_years = read_number(v.at("FuelCapacityLightYears"));
  r.fuel_remaining_light_years = read_number(v.at("FuelRemainingLightYears"));
  r.sensor_range = static_cast<float>(read_number(v.at("SensorRange")));
  r.is_active = v.at("IsActive");
  r.embarked_population_millions =
      read_number(v.at("EmbarkedPopulationMillions"));
  r.embarked_population_species_id =
      optional_value<std::string>(v.at("EmbarkedPopulationSpeciesId"));
  if (!v.at("Combat").is_null())
    r.combat = parse_combat(v.at("Combat"));
  if (!v.at("TacticalLoadout").is_null())
    r.tactical_loadout = parse_loadout(v.at("TacticalLoadout"));
  if (!v.at("TacticalVessel").is_null())
    r.tactical_vessel = parse_vessel(v.at("TacticalVessel"));
  return r;
}
struct FixtureWorld {
  std::vector<Civilization> civilizations;
  std::vector<StellarSystem> systems;
  std::vector<ConstructionState> construction;
  std::vector<ShipyardState> shipyards;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<FleetState> fleets;
  std::vector<ShipbuildingCapabilities> capabilities;
  std::vector<ShipbuildingStrategicPreference> preferences;
  ShipbuildingWorld world() {
    return {civilizations, systems, construction, shipyards,  colonies,
            economies,     fleets,  capabilities, preferences};
  }
};
FixtureWorld parse_world(const Json &snapshot) {
  FixtureWorld w;
  for (const auto &v : snapshot.at("Civilizations")) {
    Civilization c;
    c.id = v.at("Id");
    c.name = v.at("Name");
    c.home_system_id = v.at("HomeSystemId");
    c.archetype =
        static_cast<CivilizationArchetype>(v.at("Archetype").get<int>());
    const auto &t = v.at("Traits");
    c.traits = {read_number(t.at("Aggression")),
                read_number(t.at("Territoriality")),
                read_number(t.at("Greed")),
                read_number(t.at("ScientificCuriosity")),
                read_number(t.at("RiskTolerance")),
                read_number(t.at("SurvivalPriority")),
                t.at("HonorBound")};
    c.is_player = v.at("IsPlayer");
    c.development_stage = static_cast<CivilizationDevelopmentStage>(
        v.at("DevelopmentStage").get<int>());
    c.is_seeded_ancient = v.at("IsSeededAncient");
    c.expansion_allowed = v.at("ExpansionAllowed");
    c.neutral_unless_provoked = v.at("NeutralUnlessProvoked");
    c.species_id = v.at("SpeciesId");
    w.civilizations.push_back(c);
  }
  for (const auto &v : snapshot.at("Systems")) {
    StellarSystem s;
    s.id = v.at("Id");
    s.name = v.at("Name");
    auto p = parse_vec2(v.at("Position"));
    s.position = {p.x, p.y,
                  optional_value<double>(v.at("GalacticDepthLightYears"))};
    s.archetype = static_cast<StarArchetype>(v.at("Archetype").get<int>());
    s.has_habitable_world = v.at("HasHabitableWorld");
    s.has_anomaly = v.at("HasAnomaly");
    s.has_rare_resource = v.at("HasRareResource");
    s.has_pre_warp_civilization = v.at("HasPreWarpCivilization");
    s.catalog_preset_id = optional_value<std::string>(v.at("CatalogPresetId"));
    s.stellar_catalog_id =
        optional_value<std::string>(v.at("StellarCatalogId"));
    w.systems.push_back(s);
  }
  for (const auto &v : snapshot.at("ConstructionStates")) {
    ConstructionState s;
    s.civilization_id = v.at("CivilizationId");
    s.completed_project_ids =
        v.at("CompletedProjectIds").get<std::vector<std::string>>();
    s.active_project_id = optional_value<std::string>(v.at("ActiveProjectId"));
    s.active_project_progress = read_number(v.at("ActiveProjectProgress"));
    s.active_project_authorization_credits =
        read_number(v.at("ActiveProjectAuthorizationCredits"));
    for (const auto &q : v.at("QueuedProjects"))
      s.queued_projects.push_back(
          {q.at("ProjectId"), read_number(q.at("AuthorizationCredits"))});
    w.construction.push_back(s);
  }
  for (const auto &v : snapshot.at("ShipyardStates")) {
    ShipyardState s;
    s.civilization_id = v.at("CivilizationId");
    s.next_order_sequence = v.at("NextOrderSequence");
    s.active_design_id = optional_value<std::string>(v.at("ActiveDesignId"));
    s.active_order_id = optional_value<std::string>(v.at("ActiveOrderId"));
    s.active_build_progress = read_number(v.at("ActiveBuildProgress"));
    s.active_authorization_credits =
        read_number(v.at("ActiveAuthorizationCredits"));
    s.reserved_population_millions =
        read_number(v.at("ReservedPopulationMillions"));
    s.reserved_population_species_id =
        optional_value<std::string>(v.at("ReservedPopulationSpeciesId"));
    s.reserved_population_source_colony_id =
        optional_value<int>(v.at("ReservedPopulationSourceColonyId"));
    for (const auto &q : v.at("QueuedBuilds"))
      s.queued_builds.push_back(
          {q.at("OrderId"), q.at("DesignId"),
           read_number(q.at("AuthorizationCredits")),
           read_number(q.at("ReservedPopulationMillions")),
           optional_value<std::string>(q.at("ReservedPopulationSpeciesId")),
           optional_value<int>(q.at("ReservedPopulationSourceColonyId"))});
    w.shipyards.push_back(s);
  }
  for (const auto &v : snapshot.at("Colonies")) {
    Colony c;
    c.id = v.at("Id");
    c.civilization_id = v.at("CivilizationId");
    c.system_id = v.at("SystemId");
    c.planetary_body_id = optional_value<int>(v.at("PlanetaryBodyId"));
    c.name = v.at("Name");
    c.kind = static_cast<SettlementKind>(v.at("Kind").get<int>());
    c.population_species_id = v.at("PopulationSpeciesId");
    c.population_millions = read_number(v.at("PopulationMillions"));
    c.infrastructure = read_number(v.at("Infrastructure"));
    c.stability = read_number(v.at("Stability"));
    c.stored_food_population_days_millions =
        read_number(v.at("StoredFoodPopulationDaysMillions"));
    c.stored_water_population_days_millions =
        read_number(v.at("StoredWaterPopulationDaysMillions"));
    c.stored_extracted_materials =
        read_number(v.at("StoredExtractedMaterials"));
    c.remaining_extractable_materials =
        optional_value<double>(v.at("RemainingExtractableMaterials"));
    c.surface_hub_level = v.at("SurfaceHubLevel");
    c.surface_hub_upgrade_days_remaining =
        read_number(v.at("SurfaceHubUpgradeDaysRemaining"));
    w.colonies.push_back(c);
  }
  for (const auto &v : snapshot.at("Economies")) {
    CivilizationEconomy e;
    e.civilization_id = v.at("CivilizationId");
    e.credits = read_number(v.at("Credits"));
    e.industry = read_number(v.at("Industry"));
    e.science = read_number(v.at("Science"));
    e.last_credits_per_second = read_number(v.at("LastCreditsPerSecond"));
    e.last_industry_per_second = read_number(v.at("LastIndustryPerSecond"));
    e.last_science_per_second = read_number(v.at("LastSciencePerSecond"));
    e.last_research_spending_per_day =
        read_number(v.at("LastResearchSpendingPerDay"));
    e.last_research_funding_fraction =
        read_number(v.at("LastResearchFundingFraction"));
    e.operating_arrears = read_number(v.at("OperatingArrears"));
    e.last_base_operations_funding_fraction =
        read_number(v.at("LastBaseOperationsFundingFraction"));
    if (!v.at("IndustryPriority").is_null())
      e.industry_priority =
          static_cast<IndustryPriority>(v.at("IndustryPriority").get<int>());
    w.economies.push_back(e);
  }
  for (const auto &v : snapshot.at("Fleets"))
    w.fleets.push_back(parse_fleet(v));
  for (const auto &v : snapshot.at("Capabilities"))
    w.capabilities.push_back(
        {v.at("CivilizationId"),
         v.at("CapabilityIds").get<std::vector<std::string>>()});
  for (const auto &v : snapshot.at("Preferences")) {
    ShipbuildingStrategicPreference p;
    p.civilization_id = v.at("CivilizationId");
    if (!v.at("PreferredNewFleetRole").is_null())
      p.preferred_new_fleet_role =
          static_cast<FleetRole>(v.at("PreferredNewFleetRole").get<int>());
    p.defer_new_colonization = v.at("DeferNewColonization");
    w.preferences.push_back(p);
  }
  return w;
}
FixtureWorld make_world() {
  FixtureWorld w;
  Civilization c;
  c.id = 1;
  c.name = "Terran Union";
  c.home_system_id = 10;
  c.is_player = true;
  c.development_stage = CivilizationDevelopmentStage::WarpCapable;
  c.species_id = "terran_baseline";
  c.traits.scientific_curiosity = .8;
  w.civilizations.push_back(c);
  StellarSystem s;
  s.id = 10;
  s.name = "Sol";
  s.position = {2, 3, std::nullopt};
  s.archetype = StarArchetype::Standard;
  s.has_habitable_world = true;
  w.systems.push_back(s);
  ConstructionState x;
  x.civilization_id = 1;
  x.completed_project_ids = {"orbital_shipyard"};
  w.construction.push_back(x);
  w.shipyards.push_back({.civilization_id = 1});
  Colony colony;
  colony.id = 20;
  colony.civilization_id = 1;
  colony.system_id = 10;
  colony.name = "Earth";
  colony.population_species_id = "terran_baseline";
  colony.population_millions = 3000;
  w.colonies.push_back(colony);
  CivilizationEconomy e;
  e.civilization_id = 1;
  e.credits = 10000;
  e.industry = 10000;
  w.economies.push_back(e);
  w.capabilities.push_back(
      {1,
       {"spacecraft_construction", "experimental_interstellar_transit",
        "reliable_ftl", "extended_ftl_range"}});
  return w;
}
void compare_order(const ShipbuildingOrderResult &a, const Json &e,
                   const std::string &l) {
  check(a.accepted == e.at("Accepted"), l + " accepted");
  check(a.message == e.at("Message").get<std::string>(), l + " message");
}
void compare_shipyard(const ShipyardState &a, const Json &e,
                      const std::string &l) {
  check(a.civilization_id == e.at("CivilizationId"), l + " civ");
  check(a.next_order_sequence == e.at("NextOrderSequence"), l + " seq");
  opt(a.active_design_id, e.at("ActiveDesignId"), l + " design");
  opt(a.active_order_id, e.at("ActiveOrderId"), l + " order");
  number(a.active_build_progress, e.at("ActiveBuildProgress"), l + " progress");
  number(a.active_authorization_credits, e.at("ActiveAuthorizationCredits"),
         l + " quote");
  number(a.reserved_population_millions, e.at("ReservedPopulationMillions"),
         l + " pop");
  opt(a.reserved_population_species_id, e.at("ReservedPopulationSpeciesId"),
      l + " species");
  opt(a.reserved_population_source_colony_id,
      e.at("ReservedPopulationSourceColonyId"), l + " source");
  check(a.queued_builds.size() == e.at("QueuedBuilds").size(),
        l + " queued count");
  for (size_t i = 0; i < a.queued_builds.size(); ++i) {
    const auto &x = a.queued_builds[i];
    const auto &y = e.at("QueuedBuilds")[i];
    check(x.order_id == y.at("OrderId").get<std::string>() &&
              x.design_id == y.at("DesignId").get<std::string>(),
          l + " queued id");
    number(x.authorization_credits, y.at("AuthorizationCredits"),
           l + " queued quote");
    number(x.reserved_population_millions, y.at("ReservedPopulationMillions"),
           l + " queued pop");
    opt(x.reserved_population_species_id, y.at("ReservedPopulationSpeciesId"),
        l + " queued species");
    opt(x.reserved_population_source_colony_id,
        y.at("ReservedPopulationSourceColonyId"), l + " queued source");
  }
}
void compare_combat(const FleetCombatState &a, const Json &e,
                    const std::string &l) {
  check(a.profile_id == e.at("ProfileId").get<std::string>(), l + " profile");
  number(a.shields, e.at("Shields"), l + " shields");
  number(a.armor, e.at("Armor"), l + " armor");
  number(a.hull, e.at("Hull"), l + " hull");
  number(a.weapon_cooldown_remaining_days, e.at("WeaponCooldownRemainingDays"),
         l + " cooldown");
  check(static_cast<int>(a.order) == e.at("Order"), l + " combat order");
  opt(a.target_fleet_id, e.at("TargetFleetId"), l + " target");
  opt(a.defend_system_id, e.at("DefendSystemId"), l + " defend");
  number(a.retreat_progress_days, e.at("RetreatProgressDays"), l + " retreat");
  check(a.retreat_started == e.at("RetreatStarted") &&
            a.is_disengaged == e.at("IsDisengaged"),
        l + " flags");
  opt(a.disengaged_system_id, e.at("DisengagedSystemId"), l + " disengaged");
}
void compare_loadout(const MassiveCombatLoadout &a, const Json &e,
                     const std::string &l) {
  const auto b = parse_loadout(e);
  number(a.mass_per_ship, e.at("MassPerShip"), l + " mass");
  number(a.acceleration, e.at("Acceleration"), l + " acceleration");
  number(a.maximum_speed, e.at("MaximumSpeed"), l + " max speed");
  number(a.shield_per_ship, e.at("ShieldPerShip"), l + " shield");
  number(a.armor_per_ship, e.at("ArmorPerShip"), l + " armor");
  number(a.hull_per_ship, e.at("HullPerShip"), l + " hull");
  number(a.reactor_output_per_ship, e.at("ReactorOutputPerShip"),
         l + " reactor");
  number(a.cooling_per_ship, e.at("CoolingPerShip"), l + " cooling");
  number(a.warp_stabilization, e.at("WarpStabilization"), l + " stabilization");
  number(a.warp_spool_seconds, e.at("WarpSpoolSeconds"), l + " spool");
  check(a.module_slot_capacity == b.module_slot_capacity, l + " slots");
  number(a.maximum_module_mass, e.at("MaximumModuleMass"), l + " module mass");
  check(a.weapons.size() == b.weapons.size() &&
            a.modules.size() == b.modules.size(),
        l + " collections");
  for (size_t i = 0; i < a.weapons.size(); ++i) {
    const auto &x = a.weapons[i];
    const auto &y = b.weapons[i];
    check(x.id == y.id && x.kind == y.kind &&
              x.mounts_per_ship == y.mounts_per_ship,
          l + " weapon identity");
    number(x.damage_per_shot, e.at("Weapons")[i].at("DamagePerShot"),
           l + " weapon damage");
    number(x.shots_per_second, e.at("Weapons")[i].at("ShotsPerSecond"),
           l + " weapon shots");
    number(x.range, e.at("Weapons")[i].at("Range"), l + " weapon range");
    number(x.accuracy, e.at("Weapons")[i].at("Accuracy"),
           l + " weapon accuracy");
    number(x.power_per_second, e.at("Weapons")[i].at("PowerPerSecond"),
           l + " weapon power");
    number(x.heat_per_second, e.at("Weapons")[i].at("HeatPerSecond"),
           l + " weapon heat");
  }
  for (size_t i = 0; i < a.modules.size(); ++i) {
    const auto &x = a.modules[i];
    const auto &y = b.modules[i];
    check(x.id == y.id && x.kind == y.kind &&
              x.installed_count == y.installed_count &&
              x.enabled == y.enabled && x.slots == y.slots,
          l + " module identity");
    number(x.mass_each, e.at("Modules")[i].at("MassEach"),
           l + " module mass each");
    number(x.power_per_second_each, e.at("Modules")[i].at("PowerPerSecondEach"),
           l + " module power");
    number(x.heat_per_second_each, e.at("Modules")[i].at("HeatPerSecondEach"),
           l + " module heat");
    number(x.condition, e.at("Modules")[i].at("Condition"),
           l + " module condition");
    number(x.effective_range, e.at("Modules")[i].at("EffectiveRange"),
           l + " module range");
    number(x.field_strength, e.at("Modules")[i].at("FieldStrength"),
           l + " module field");
    number(x.detection_signature, e.at("Modules")[i].at("DetectionSignature"),
           l + " module signature");
  }
}
void compare_vessel(const MassiveVesselState &a, const Json &e,
                    const std::string &l) {
  const auto b = parse_vessel(e);
  check(a.id == b.id && a.name == b.name && a.design_id == b.design_id &&
            a.is_flagship == b.is_flagship && a.is_carrier == b.is_carrier &&
            a.is_interdictor == b.is_interdictor &&
            a.is_story_ship == b.is_story_ship &&
            a.battles_fought == b.battles_fought &&
            a.confirmed_kills == b.confirmed_kills &&
            a.destroyed == b.destroyed && a.escaped == b.escaped,
        l + " identity/history");
  number(a.hull_fraction, e.at("HullFraction"), l + " hull");
  number(a.engine_fraction, e.at("EngineFraction"), l + " engine");
  number(a.sensor_fraction, e.at("SensorFraction"), l + " sensor");
  number(a.warp_drive_fraction, e.at("WarpDriveFraction"), l + " warp");
  number(a.reactor_fraction, e.at("ReactorFraction"), l + " reactor");
  number(a.interdictor_fraction, e.at("InterdictorFraction"),
         l + " interdictor");
}
void compare_fleet(const FleetState &a, const Json &e, const std::string &l) {
  check(a.id == e.at("Id") && a.civilization_id == e.at("CivilizationId") &&
            a.name == e.at("Name").get<std::string>() &&
            static_cast<int>(a.role) == e.at("Role"),
        l + " identity");
  opt(a.design_id, e.at("DesignId"), l + " design");
  number(a.position.x, e.at("Position").at("X"), l + " x");
  number(a.position.y, e.at("Position").at("Y"), l + " y");
  opt(a.current_system_id, e.at("CurrentSystemId"), l + " current");
  opt(a.destination_system_id, e.at("DestinationSystemId"), l + " destination");
  check(static_cast<int>(a.transit_phase) == e.at("TransitPhase"),
        l + " phase");
  opt(a.transit_origin_system_id, e.at("TransitOriginSystemId"), l + " origin");
  opt(a.transit_target_system_id, e.at("TransitTargetSystemId"),
      l + " transit target");
  number(a.transit_progress, e.at("TransitProgress"), l + " transit progress");
  number(a.local_transit_start.x, e.at("LocalTransitStart").at("X"),
         l + " local start x");
  number(a.local_transit_start.y, e.at("LocalTransitStart").at("Y"),
         l + " local start y");
  number(a.local_transit_position.x, e.at("LocalTransitPosition").at("X"),
         l + " local position x");
  number(a.local_transit_position.y, e.at("LocalTransitPosition").at("Y"),
         l + " local position y");
  number(a.local_transit_target.x, e.at("LocalTransitTarget").at("X"),
         l + " local target x");
  number(a.local_transit_target.y, e.at("LocalTransitTarget").at("Y"),
         l + " local target y");
  check(a.planned_route_system_ids ==
            e.at("PlannedRouteSystemIds").get<std::vector<int>>(),
        l + " route");
  check(a.hold_requested == e.at("HoldRequested") &&
            a.return_to_base_requested == e.at("ReturnToBaseRequested"),
        l + " mission flags");
  opt(a.return_to_base_failure_reason, e.at("ReturnToBaseFailureReason"),
      l + " return reason");
  check(a.mission_order_revision == e.at("MissionOrderRevision"),
        l + " revision");
  opt(a.destination_planetary_body_id, e.at("DestinationPlanetaryBodyId"),
      l + " body");
  check(a.prevent_automatic_settlement == e.at("PreventAutomaticSettlement"),
        l + " prevent");
  opt(a.settlement_body_id, e.at("SettlementBodyId"), l + " settle body");
  number(a.settlement_days_completed, e.at("SettlementDaysCompleted"),
         l + " settle days");
  opt(a.reconnaissance_system_id, e.at("ReconnaissanceSystemId"), l + " recon");
  number(a.reconnaissance_days_completed, e.at("ReconnaissanceDaysCompleted"),
         l + " recon days");
  opt(a.freight_target_outpost_id, e.at("FreightTargetOutpostId"),
      l + " outpost");
  opt(a.freight_home_colony_id, e.at("FreightHomeColonyId"),
      l + " freight home");
  number(a.cargo_material_capacity, e.at("CargoMaterialCapacity"),
         l + " capacity");
  number(a.cargo_materials, e.at("CargoMaterials"), l + " cargo");
  number(a.strategic_speed, e.at("StrategicSpeed"), l + " speed");
  number(a.maximum_leg_range_light_years, e.at("MaximumLegRangeLightYears"),
         l + " range");
  number(a.fuel_capacity_light_years, e.at("FuelCapacityLightYears"),
         l + " fuel cap");
  number(a.fuel_remaining_light_years, e.at("FuelRemainingLightYears"),
         l + " fuel");
  number(a.sensor_range, e.at("SensorRange"), l + " sensor");
  check(a.is_active == e.at("IsActive"), l + " active");
  number(a.embarked_population_millions, e.at("EmbarkedPopulationMillions"),
         l + " population");
  opt(a.embarked_population_species_id, e.at("EmbarkedPopulationSpeciesId"),
      l + " species");
  check(a.combat.has_value() != e.at("Combat").is_null(),
        l + " combat presence");
  if (a.combat)
    compare_combat(*a.combat, e.at("Combat"), l + " combat");
  check(a.tactical_loadout.has_value() != e.at("TacticalLoadout").is_null(),
        l + " loadout presence");
  if (a.tactical_loadout)
    compare_loadout(*a.tactical_loadout, e.at("TacticalLoadout"),
                    l + " loadout");
  check(a.tactical_vessel.has_value() != e.at("TacticalVessel").is_null(),
        l + " vessel presence");
  if (a.tactical_vessel)
    compare_vessel(*a.tactical_vessel, e.at("TacticalVessel"), l + " vessel");
}
void compare_world(const FixtureWorld &w, const Json &e, const std::string &l) {
  const auto &ys = e.at("ShipyardStates");
  check(w.shipyards.size() == ys.size(), l + " shipyards");
  for (size_t i = 0; i < w.shipyards.size(); ++i)
    compare_shipyard(w.shipyards[i], ys[i], l + " shipyard");
  const auto &yc = e.at("Colonies");
  check(w.colonies.size() == yc.size(), l + " colonies");
  for (size_t i = 0; i < w.colonies.size(); ++i) {
    const auto &a = w.colonies[i];
    const auto &b = yc[i];
    check(a.id == b.at("Id") && a.civilization_id == b.at("CivilizationId") &&
              a.system_id == b.at("SystemId") &&
              a.name == b.at("Name").get<std::string>(),
          l + " colony identity");
    number(a.population_millions, b.at("PopulationMillions"),
           l + " colony pop");
    check(a.population_species_id ==
              b.at("PopulationSpeciesId").get<std::string>(),
          l + " colony species");
    check(a.planetary_body_id == optional_value<int>(b.at("PlanetaryBodyId")) &&
              static_cast<int>(a.kind) == b.at("Kind").get<int>() &&
              a.surface_hub_level == b.at("SurfaceHubLevel").get<int>(),
          l + " colony metadata");
    number(a.infrastructure, b.at("Infrastructure"), l + " infrastructure");
    number(a.stability, b.at("Stability"), l + " stability");
    number(a.stored_food_population_days_millions,
           b.at("StoredFoodPopulationDaysMillions"), l + " food");
    number(a.stored_water_population_days_millions,
           b.at("StoredWaterPopulationDaysMillions"), l + " water");
    number(a.stored_extracted_materials, b.at("StoredExtractedMaterials"),
           l + " materials");
    check(a.remaining_extractable_materials ==
              optional_value<double>(b.at("RemainingExtractableMaterials")),
          l + " extractable");
    number(a.surface_hub_upgrade_days_remaining,
           b.at("SurfaceHubUpgradeDaysRemaining"), l + " hub upgrade");
    check(a.surface_buildings.empty() && b.at("SurfaceBuildings").empty(),
          l + " surface buildings");
  }
  const auto &ye = e.at("Economies");
  check(w.economies.size() == ye.size(), l + " economies");
  for (size_t i = 0; i < w.economies.size(); ++i) {
    number(w.economies[i].credits, ye[i].at("Credits"), l + " credits");
    number(w.economies[i].industry, ye[i].at("Industry"), l + " industry");
    number(w.economies[i].science, ye[i].at("Science"), l + " science");
    const auto &a = w.economies[i];
    const auto &b = ye[i];
    number(a.last_credits_per_second, b.at("LastCreditsPerSecond"),
           l + " credit rate");
    number(a.last_industry_per_second, b.at("LastIndustryPerSecond"),
           l + " industry rate");
    number(a.last_science_per_second, b.at("LastSciencePerSecond"),
           l + " science rate");
    number(a.last_research_spending_per_day, b.at("LastResearchSpendingPerDay"),
           l + " research spend");
    number(a.last_research_funding_fraction,
           b.at("LastResearchFundingFraction"), l + " research funding");
    number(a.operating_arrears, b.at("OperatingArrears"), l + " arrears");
    number(a.last_base_operations_funding_fraction,
           b.at("LastBaseOperationsFundingFraction"),
           l + " operations funding");
    check((!a.industry_priority && b.at("IndustryPriority").is_null()) ||
              (a.industry_priority && !b.at("IndustryPriority").is_null() &&
               static_cast<int>(*a.industry_priority) ==
                   b.at("IndustryPriority").get<int>()),
          l + " industry priority");
  }
  const auto &yf = e.at("Fleets");
  check(w.fleets.size() == yf.size(), l + " fleets");
  for (size_t i = 0; i < w.fleets.size(); ++i)
    compare_fleet(w.fleets[i], yf[i], l + " fleet" + std::to_string(i));
}
void check_events(const std::vector<ShipbuildingEvent> &a, const Json &e,
                  const std::string &l) {
  check(a.size() == e.size(), l + " count");
  for (size_t i = 0; i < a.size(); ++i)
    check(a[i].civilization_id == e[i].at("CivilizationId").get<int>() &&
              a[i].fleet_id == e[i].at("FleetId").get<int>() &&
              a[i].design_id == e[i].at("DesignId").get<std::string>() &&
              a[i].message == e[i].at("Message").get<std::string>(),
          l + " item " + std::to_string(i));
}
void check_error(const std::exception_ptr &actual, const std::string &type,
                 const std::string &message, const std::string &name) {
  check(actual != nullptr, name + " expected error");
  try {
    std::rethrow_exception(actual);
  } catch (const std::out_of_range &e) {
    check(type == "InvalidOperationException" ||
              type == "ArgumentOutOfRangeException",
          name + " error type");
    check(e.what() == message, name + " error message");
  } catch (const std::invalid_argument &e) {
    check(type == "InvalidOperationException", name + " error type");
    check(e.what() == message, name + " error message");
  } catch (const std::runtime_error &e) {
    check(type == "InvalidOperationException", name + " error type");
    check(e.what() == message, name + " error message");
  }
}
ShipbuildingOrderResult expected_order(const Json &value) {
  return {value.at("Accepted").get<bool>(),
          value.at("Message").get<std::string>()};
}
ShipbuildingEvent expected_event(const Json &value) {
  return {value.at("CivilizationId").get<int>(), value.at("FleetId").get<int>(),
          value.at("DesignId").get<std::string>(),
          value.at("Message").get<std::string>()};
}
std::vector<ShipbuildingEvent> expected_events(const Json &value) {
  std::vector<ShipbuildingEvent> result;
  for (const auto &item : value)
    result.push_back(expected_event(item));
  return result;
}
void compare_events(const std::vector<ShipbuildingEvent> &actual,
                    const std::vector<ShipbuildingEvent> &expected,
                    const std::string &label) {
  check(actual.size() == expected.size(), label + " count");
  for (std::size_t i = 0; i < actual.size(); ++i)
    check(actual[i].civilization_id == expected[i].civilization_id &&
              actual[i].fleet_id == expected[i].fleet_id &&
              actual[i].design_id == expected[i].design_id &&
              actual[i].message == expected[i].message,
          label + " item " + std::to_string(i));
}
void run_sequence(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto &arguments = test.at("Arguments");
  const int civilization_id = arguments.at("CivilizationId").get<int>();
  const auto design_ids =
      arguments.at("DesignIds").get<std::vector<std::string>>();
  const double partial_budget =
      read_number(arguments.at("PartialIndustryBudget"));
  const double partial_days =
      read_number(arguments.at("PartialSimulationDays"));
  const auto cancellation_order_id =
      arguments.at("CancellationOrderId").get<std::string>();
  const double completion_budget =
      read_number(arguments.at("CompletionIndustryBudget"));
  const double completion_days =
      read_number(arguments.at("CompletionSimulationDays"));
  const int completion_count = arguments.at("CompletionCount").get<int>();
  const auto &result = test.at("Result");
  std::vector<ShipbuildingOrderResult> expected_orders;
  for (const auto &item : result.at("Orders"))
    expected_orders.push_back(expected_order(item));
  const auto expected_partial = expected_events(result.at("Partial"));
  ShipbuildingCancellationAssessment expected_assessment{
      result.at("Assessment").at("CanCancel").get<bool>(),
      result.at("Assessment").at("IsActive").get<bool>(),
      optional_value<std::string>(result.at("Assessment").at("DesignId")),
      read_number(result.at("Assessment").at("RefundCredits")),
      optional_value<std::string>(result.at("Assessment").at("Blocker"))};
  ShipbuildingCancellationResult expected_cancellation{
      result.at("Cancellation").at("Accepted").get<bool>(),
      result.at("Cancellation").at("Message").get<std::string>(),
      read_number(result.at("Cancellation").at("RefundedCredits"))};
  std::vector<std::vector<ShipbuildingEvent>> expected_completions;
  for (const auto &item : result.at("Completions"))
    expected_completions.push_back(expected_events(item));
  check(test.at("Error").is_null(), name + " fixture error");
  auto world = parse_world(test.at("Before"));
  compare_world(world, test.at("Before"), name + " parsed before");

  std::vector<ShipbuildingOrderResult> orders;
  std::vector<std::exception_ptr> operation_errors;
  for (const auto &design_id : design_ids) {
    try {
      orders.push_back(
          start_ship_build(world.world(), civilization_id, design_id));
      operation_errors.push_back(nullptr);
    } catch (...) {
      operation_errors.push_back(std::current_exception());
    }
  }
  std::vector<ShipbuildingEvent> partial;
  try {
    partial = advance_shipbuilding_for_civilization(
        world.world(), civilization_id, partial_budget, partial_days);
    operation_errors.push_back(nullptr);
  } catch (...) {
    operation_errors.push_back(std::current_exception());
  }
  ShipbuildingCancellationAssessment assessment;
  try {
    assessment = assess_ship_build_cancellation(
        world.world().read(), civilization_id, cancellation_order_id);
    operation_errors.push_back(nullptr);
  } catch (...) {
    operation_errors.push_back(std::current_exception());
  }
  ShipbuildingCancellationResult cancellation;
  try {
    cancellation = cancel_ship_build(world.world(), civilization_id,
                                     cancellation_order_id);
    operation_errors.push_back(nullptr);
  } catch (...) {
    operation_errors.push_back(std::current_exception());
  }
  std::vector<std::vector<ShipbuildingEvent>> completions;
  for (int index = 0; index < completion_count; ++index) {
    try {
      completions.push_back(advance_shipbuilding_for_civilization(
          world.world(), civilization_id, completion_budget, completion_days));
      operation_errors.push_back(nullptr);
    } catch (...) {
      operation_errors.push_back(std::current_exception());
    }
  }

  check(std::all_of(operation_errors.begin(), operation_errors.end(),
                    [](const auto &error) { return !error; }),
        name + " unexpected operation error");
  check(orders.size() == expected_orders.size(), name + " order count");
  for (std::size_t i = 0; i < orders.size(); ++i)
    check(orders[i].accepted == expected_orders[i].accepted &&
              orders[i].message == expected_orders[i].message,
          name + " order " + std::to_string(i));
  compare_events(partial, expected_partial, name + " partial");
  check(assessment.can_cancel == expected_assessment.can_cancel &&
            assessment.is_active == expected_assessment.is_active &&
            assessment.design_id == expected_assessment.design_id &&
            assessment.blocker == expected_assessment.blocker,
        name + " assessment");
  check(assessment.refund_credits == expected_assessment.refund_credits,
        name + " assessment refund");
  check(cancellation.accepted == expected_cancellation.accepted &&
            cancellation.message == expected_cancellation.message &&
            cancellation.refunded_credits ==
                expected_cancellation.refunded_credits,
        name + " cancellation");
  check(completions.size() == expected_completions.size(),
        name + " completion count");
  for (std::size_t i = 0; i < completions.size(); ++i)
    compare_events(completions[i], expected_completions[i],
                   name + " completion " + std::to_string(i));
  const auto projection = economic_fleet_projection(world.fleets);
  check(projection.size() == world.fleets.size() && projection.size() == 2 &&
            projection[0].role == EconomyFleetRole::Colony &&
            projection[1].role == EconomyFleetRole::Scout,
        name + " economic fleet projection");
  compare_world(world, test.at("After"), name + " after");
}
void run(const Json &t) {
  const auto name = t.at("Name").get<std::string>();
  const auto kind = t.at("Kind").get<std::string>();
  check(kind == "Start" || kind == "Assess" || kind == "Cancel" ||
            kind == "Demand" || kind == "Ensure" || kind == "Advance" ||
            kind == "AdvanceSelected",
        name + ": unknown fixture kind");
  auto w = parse_world(t.at("Before"));
  compare_world(w, t.at("Before"), name + " parsed before");
  const auto &args = t.at("Arguments");
  std::exception_ptr error;
  std::optional<ShipbuildingOrderResult> order;
  std::optional<ShipbuildingCancellationAssessment> assessment;
  std::optional<ShipbuildingCancellationResult> cancellation;
  std::optional<double> demand;
  std::vector<ShipbuildingEvent> events;
  int civilization{};
  std::string value;
  double days{}, budget{};
  std::optional<std::vector<ConstructionIndustryBudget>> budgets;
  std::optional<std::string> expected_error_type;
  std::optional<std::string> expected_error_message;
  if (!t.at("Error").is_null()) {
    expected_error_type = t.at("Error").at("Type").get<std::string>();
    expected_error_message = t.at("Error").at("Message").get<std::string>();
  }
  if (kind == "Start") {
    civilization = args.at("CivilizationId");
    value = args.at("DesignId");
  } else if (kind == "Assess" || kind == "Cancel") {
    civilization = args.at("CivilizationId");
    value = args.at("OrderId");
  } else if (kind == "Demand") {
    civilization = args.at("CivilizationId");
    days = read_number(args.at("SimulationDays"));
  } else if (kind == "AdvanceSelected") {
    civilization = args.at("CivilizationId");
    budget = read_number(args.at("IndustryBudget"));
    days = read_number(args.at("SimulationDays"));
  } else if (kind == "Advance") {
    days = read_number(args.at("SimulationDays"));
    if (!args.at("Budgets").is_null()) {
      budgets.emplace();
      for (const auto &[key, item] : args.at("Budgets").items())
        budgets->push_back({std::stoi(key), read_number(item)});
    }
  }
  try {
    if (kind == "Start")
      order = start_ship_build(w.world(), civilization, value);
    else if (kind == "Assess")
      assessment =
          assess_ship_build_cancellation(w.world().read(), civilization, value);
    else if (kind == "Cancel")
      cancellation = cancel_ship_build(w.world(), civilization, value);
    else if (kind == "Demand")
      demand =
          shipbuilding_industry_demand(w.world().read(), civilization, days);
    else if (kind == "Ensure")
      ensure_automatic_ship_orders(w.world());
    else if (kind == "Advance")
      events = advance_shipbuilding(
          w.world(),
          budgets ? std::optional<std::span<const ConstructionIndustryBudget>>(
                        *budgets)
                  : std::nullopt,
          days);
    else
      events = advance_shipbuilding_for_civilization(w.world(), civilization,
                                                     budget, days);
  } catch (...) {
    error = std::current_exception();
  }
  if (t.at("Error").is_null()) {
    check(!error, name + " unexpected error");
    const auto &r = t.at("Result");
    if (order)
      compare_order(*order, r, name);
    else if (assessment) {
      check(assessment->can_cancel == r.at("CanCancel").get<bool>() &&
                assessment->is_active == r.at("IsActive").get<bool>() &&
                assessment->design_id ==
                    optional_value<std::string>(r.at("DesignId")) &&
                assessment->blocker ==
                    optional_value<std::string>(r.at("Blocker")),
            name + " assessment result");
      number(assessment->refund_credits, r.at("RefundCredits"),
             name + " refund");
    } else if (cancellation) {
      check(cancellation->accepted == r.at("Accepted").get<bool>() &&
                cancellation->message == r.at("Message").get<std::string>(),
            name + " cancellation result");
      number(cancellation->refunded_credits, r.at("RefundedCredits"),
             name + " cancelled refund");
    } else if (demand)
      number(*demand, r, name + " demand");
    else if (kind == "Advance" || kind == "AdvanceSelected")
      check_events(events, r, name + " events");
    else
      check(r.is_null(), name + " ensure result");
  } else
    check_error(error, *expected_error_type, *expected_error_message, name);
  compare_world(w, t.at("After"), name + " after");
}
void direct_boundaries() {
  auto w = make_world();
  check(!start_ship_build(w.world(), 999, "warp_scout").accepted,
        "unknown civilization");
  const auto credits = w.economies[0].credits;
  w.shipyards[0].next_order_sequence = std::numeric_limits<std::int64_t>::max();
  check(!start_ship_build(w.world(), 1, "warp_scout").accepted,
        "order identity exhaustion");
  check(w.economies[0].credits == credits && !w.shipyards[0].active_design_id,
        "identity rejection mutation");

  w = make_world();
  check(start_ship_build(w.world(), 1, "warp_scout").accepted,
        "fleet exhaustion start");
  w.shipyards[0].active_build_progress =
      get_ship_design("warp_scout").industry_cost;
  FleetState maximum;
  maximum.id = std::numeric_limits<int>::max();
  w.fleets.push_back(maximum);
  bool overflow = false;
  try {
    (void)advance_shipbuilding_for_civilization(w.world(), 1, 0, 1);
  } catch (const std::overflow_error &) {
    overflow = true;
  }
  check(overflow && w.fleets.size() == 1 && w.shipyards[0].active_design_id,
        "fleet identity exhaustion boundary");

  w = make_world();
  check(start_ship_build(w.world(), 1, "warp_scout").accepted,
        "home ordering start");
  w.shipyards[0].active_build_progress =
      get_ship_design("warp_scout").industry_cost;
  w.fleets.push_back(maximum);
  w.systems.clear();
  bool missing_home = false;
  try {
    (void)advance_shipbuilding_for_civilization(w.world(), 1, 0, 1);
  } catch (const std::out_of_range &error) {
    missing_home = std::string_view(error.what()) ==
                   "Sequence contains no matching element";
  }
  check(missing_home, "home lookup precedes native fleet identity boundary");

  w = make_world();
  w.civilizations[0].is_player = false;
  w.civilizations[0].name = "Orion League";
  ensure_automatic_ship_orders(w.world());
  check(w.shipyards[0].active_design_id ==
            std::optional<std::string>("warp_scout"),
        "automatic scout fallback");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected shipbuilding fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open shipbuilding fixture");
    auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-shipbuilding-oracle-v2",
          "Unknown fixture format");
    for (const auto &t : fixture.at("Cases")) {
      const auto kind = t.at("Kind").get<std::string>();
      if (kind == "Sequence")
        run_sequence(t);
      else
        run(t);
    }
    direct_boundaries();
    std::cout << "shipbuilding_tests: passed " << fixture.at("Cases").size()
              << " cases\n";
  } catch (const std::exception &e) {
    std::cerr << "shipbuilding_tests: " << e.what() << "\n";
    return 1;
  }
}
