#include <stellar/core/ship_components.hpp>

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/strategic_input_support.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {
namespace {

// Hull fields: role, industry, credits, population, speed, leg range, fuel,
// sensor, cargo capacity, cargo rate, crew, shields, armor, hull, weapon
// damage, weapon interval, retreat delay, prerequisites, slots, required.
const std::array<ShipHullDefinition, 6> hulls{{
    {"survey_frame", "Survey Frame",
     "Compact survey hull with room for light sensors and utilities.",
     FleetRole::Scout, 420, 40, 0, 24, 420, 1200, 140, 0, 0, 12,
     0, 30, 60, 0, 0, 4,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Sensor, ShipComponentSlot::Utility},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp}},
    {"research_frame", "Research Frame",
     "Long-endurance science hull with expanded sensor and laboratory volume.",
     FleetRole::Science, 560, 60, 0, 18, 400, 1100, 150, 0, 0, 40,
     0, 30, 70, 0, 0, 6,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Sensor, ShipComponentSlot::Sensor,
      ShipComponentSlot::Utility},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp}},
    {"escort_frame", "Escort Frame",
     "Armored patrol hull fitted for weapon mounts and defensive systems.",
     FleetRole::Military, 700, 80, 0, 21, 340, 800, 125, 0, 0, 45,
     0, 60, 90, 0, 0, 8,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Weapon, ShipComponentSlot::Weapon,
      ShipComponentSlot::Defense, ShipComponentSlot::Sensor},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp}},
    {"colony_frame", "Colony Frame",
     "Heavy settlement hull built around habitation and cargo volume.",
     FleetRole::Colony, 900, 120, 0, 13.5, 300, 750, 80, 0, 0, 60,
     0, 20, 110, 0, 0, 10,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Habitation, ShipComponentSlot::Cargo,
      ShipComponentSlot::Utility},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Habitation}},
    {"freight_frame", "Freight Frame",
     "Long-legged logistics hull optimized for bulk material transfer.",
     FleetRole::Logistics, 640, 70, 0, 17, 350, 1000, 85, 40, 8, 30,
     0, 20, 80, 0, 0, 8,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Cargo, ShipComponentSlot::Cargo,
      ShipComponentSlot::Utility},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp}},
    {"outpost_frame", "Outpost Frame",
     "Sealed specialist hull for permanent resource extraction posts.",
     FleetRole::Colony, 700, 90, 0, 16, 330, 900, 95, 0, 0, 60,
     0, 30, 80, 0, 0, 8,
     {{"spacecraft_construction", "experimental_interstellar_transit"}, {},
      {"orbital_shipyard"}},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Habitation, ShipComponentSlot::Sensor,
      ShipComponentSlot::Utility},
     {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
      ShipComponentSlot::Habitation}},
}};

MassiveWeaponGroup weapon_group(std::string_view id, MassiveWeaponKind kind,
                                float damage, float interval_seconds,
                                float range, float accuracy) {
  MassiveWeaponGroup group;
  group.id = std::string(id);
  group.kind = kind;
  group.damage_per_shot = damage;
  group.shots_per_second = 1.f / interval_seconds;
  group.range = range;
  group.accuracy = accuracy;
  return group;
}

const std::array<ShipComponentDefinition, 18> components{{
    {"ion_drive", "Ion Drive", "Efficient first-generation thrust assembly.",
     ShipComponentSlot::Engine, 60, 8, 4, 0, 0, 0.f, 0, 0, 0, 0,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"plasma_drive", "Plasma Drive",
     "High-output drive that improves cruise speed at extra cost.",
     ShipComponentSlot::Engine, 140, 22, 9, 20, 60, 0.f, 0, 0, 0, 6,
     0, 0, 0, 0, std::nullopt, {{"reliable_ftl"}, {}, {}}, std::nullopt,
     std::nullopt},
    {"survey_warp_core", "Survey Warp Core",
     "Baseline interstellar warp core for early hulls.",
     ShipComponentSlot::Warp, 70, 10, 0, 0, 0, 0.f, 0, 0, 0, 0,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"deep_warp_core", "Deep Warp Core",
     "Extended-range warp architecture for long expedition legs.",
     ShipComponentSlot::Warp, 160, 30, 2, 60, 250, 0.f, 0, 0, 0, 8,
     0, 0, 0, 0, std::nullopt, {{"extended_ftl_range"}, {}, {}},
     std::nullopt, std::nullopt},
    {"survey_array", "Survey Array",
     "Wide-field telescope suite tuned for system surveys.",
     ShipComponentSlot::Sensor, 50, 6, 0, 0, 0, 30.f, 0, 0, 0, 2,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"deep_scan_array", "Deep-Scan Array",
     "High-gain anomaly sensor with dedicated analysis compute.",
     ShipComponentSlot::Sensor, 120, 18, 0, 0, 0, 55.f, 0, 0, 0, 5,
     0, 0, 0, 0, std::nullopt, {{"reliable_ftl"}, {}, {}}, std::nullopt,
     std::nullopt},
    {"light_lance", "Light Lance Turret",
     "Rapid-fire beam turret for close defense.",
     ShipComponentSlot::Weapon, 110, 16, 0, 0, 0, 0.f, 0, 0, 0, 8,
     0, 0, 0, 18, 6.0, {},
     weapon_group("light_lance", MassiveWeaponKind::Beam, 6.f, 1.6f, 420.f,
                  .72f),
     std::nullopt},
    {"coilgun_battery", "Coilgun Battery",
     "Magnetic kinetic battery with sustained fire.",
     ShipComponentSlot::Weapon, 140, 20, 0, 0, 0, 0.f, 0, 0, 0, 10,
     0, 0, 0, 26, 9.0, {},
     weapon_group("coilgun_battery", MassiveWeaponKind::Kinetic, 14.f, 3.2f,
                  560.f, .62f),
     std::nullopt},
    {"missile_rack", "Missile Rack",
     "Stand-off missile tubes with deep magazines.",
     ShipComponentSlot::Weapon, 180, 30, 0, 0, 0, 0.f, 0, 0, 0, 12,
     0, 0, 0, 40, 16.0, {{"reliable_ftl"}, {}, {}},
     weapon_group("missile_rack", MassiveWeaponKind::Missile, 30.f, 6.5f,
                  980.f, .58f),
     std::nullopt},
    {"flak_screen", "Flak Screen",
     "Point-defense autocannon grid for missile interception.",
     ShipComponentSlot::Defense, 90, 12, 0, 0, 0, 0.f, 0, 0, 0, 6,
     0, 0, 0, 6, 8.0, {},
     weapon_group("flak_screen", MassiveWeaponKind::PointDefense, 3.f, .4f,
                  240.f, .85f),
     std::nullopt},
    {"deflector_screen", "Deflector Screen",
     "Projected shield emitter web around the hull.",
     ShipComponentSlot::Defense, 130, 24, 0, 0, 0, 0.f, 0, 0, 0, 6,
     55, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"armor_plating", "Armor Plating",
     "Layered composite plating over pressure sections.",
     ShipComponentSlot::Defense, 90, 10, 0, 0, 0, 0.f, 0, 0, 0, 4,
     0, 60, 25, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"fuel_scoop", "Fuel Scoop",
     "Ramscoop collectors that stretch cruise endurance.",
     ShipComponentSlot::Utility, 80, 12, 0, 30, 300, 0.f, 0, 0, 0, 3,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"repair_bay", "Repair Bay",
     "Machine shop and spares that harden the hull.",
     ShipComponentSlot::Utility, 100, 14, 0, 0, 0, 0.f, 0, 0, 0, 6,
     0, 10, 30, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"gravity_snare", "Gravity Snare",
     "Warp-interdiction field projector for system denial.",
     ShipComponentSlot::Utility, 240, 45, 0, 0, 0, 0.f, 0, 0, 0, 14,
     0, 0, 0, 0, std::nullopt, {{"extended_ftl_range"}, {}, {}},
     std::nullopt, warp_interdictor()},
    {"colony_module", "Colony Module",
     "Cryo bays and settlement seed equipment for founding colonists.",
     ShipComponentSlot::Habitation, 220, 60, 0, 0, 0, 0.f, 40, 6, 250, 90,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"outpost_module", "Outpost Module",
     "Compact pressure-sealed habitat for specialist crews.",
     ShipComponentSlot::Habitation, 130, 30, 0, 0, 0, 0.f, 20, 4, 8, 30,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
    {"cargo_pod", "Cargo Pod",
     "External pressurized freight racks and transfer gear.",
     ShipComponentSlot::Cargo, 70, 9, 0, 0, 0, 0.f, 60, 8, 0, 4,
     0, 0, 0, 0, std::nullopt, {}, std::nullopt, std::nullopt},
}};

std::size_t slot_count(std::span<const ShipComponentSlot> slots,
                       ShipComponentSlot kind) {
  return static_cast<std::size_t>(std::ranges::count(slots, kind));
}

void append_prerequisite_issues(std::vector<std::string> &issues,
                                ShipDesignReadView world, int civilization_id,
                                const ShipDesignPrerequisites &prerequisites,
                                std::string_view owner) {
  ShipDesignDefinition probe;
  probe.prerequisites = prerequisites;
  if (auto reason = ship_design_lock_reason(world, civilization_id, probe))
    issues.push_back(std::string(owner) + " " + *reason + ".");
}

} // namespace

std::string_view ship_component_slot_name(ShipComponentSlot slot) {
  switch (slot) {
  case ShipComponentSlot::Engine: return "Engine";
  case ShipComponentSlot::Warp: return "Warp";
  case ShipComponentSlot::Sensor: return "Sensor";
  case ShipComponentSlot::Weapon: return "Weapon";
  case ShipComponentSlot::Defense: return "Defense";
  case ShipComponentSlot::Utility: return "Utility";
  case ShipComponentSlot::Cargo: return "Cargo";
  case ShipComponentSlot::Habitation: return "Habitation";
  }
  return "Unknown";
}

std::span<const ShipHullDefinition> ship_hull_catalog() { return hulls; }
std::span<const ShipComponentDefinition> ship_component_catalog() {
  return components;
}
const ShipHullDefinition *find_ship_hull(std::string_view id) {
  const auto item = std::ranges::find_if(
      hulls, [=](const auto &hull) { return hull.id == id; });
  return item == hulls.end() ? nullptr : &*item;
}
const ShipHullDefinition &get_ship_hull(std::string_view id) {
  const auto *hull = find_ship_hull(id);
  if (!hull) throw std::out_of_range("Sequence contains no matching element");
  return *hull;
}
const ShipComponentDefinition *find_ship_component(std::string_view id) {
  const auto item = std::ranges::find_if(
      components, [=](const auto &component) { return component.id == id; });
  return item == components.end() ? nullptr : &*item;
}
const ShipComponentDefinition &get_ship_component(std::string_view id) {
  const auto *component = find_ship_component(id);
  if (!component)
    throw std::out_of_range("Sequence contains no matching element");
  return *component;
}
const AuthoredShipDesign *find_authored_ship_design(
    std::span<const AuthoredShipDesign> designs, std::string_view id) {
  const auto item = std::ranges::find_if(
      designs, [=](const auto &design) { return design.id == id; });
  return item == designs.end() ? nullptr : &*item;
}
const AuthoredShipDesign *find_authored_ship_design(
    std::span<const AuthoredShipDesign> designs, int civilization_id,
    std::string_view id) {
  const auto item = std::ranges::find_if(designs, [=](const auto &design) {
    return design.id == id && design.owner_civilization_id == civilization_id;
  });
  return item == designs.end() ? nullptr : &*item;
}

AuthoredShipDesignValidation validate_authored_ship_design(
    const AuthoredShipDesign &design, ShipDesignReadView world,
    int civilization_id) {
  AuthoredShipDesignValidation result;
  if (design.name.empty())
    result.issues.push_back("A design requires a name.");
  const auto *hull = find_ship_hull(design.hull_id);
  if (!hull) {
    result.issues.push_back("Unknown hull '" + design.hull_id + "'.");
    return result;
  }
  append_prerequisite_issues(result.issues, world, civilization_id,
                             hull->prerequisites, "Hull");
  std::vector<ShipComponentSlot> installed;
  installed.reserve(design.component_ids.size());
  for (const auto &component_id : design.component_ids) {
    const auto *component = find_ship_component(component_id);
    if (!component) {
      result.issues.push_back("Unknown component '" + component_id + "'.");
      continue;
    }
    installed.push_back(component->slot);
    append_prerequisite_issues(result.issues, world, civilization_id,
                               component->prerequisites, component->name);
  }
  constexpr ShipComponentSlot kinds[] = {
      ShipComponentSlot::Engine,   ShipComponentSlot::Warp,
      ShipComponentSlot::Sensor,   ShipComponentSlot::Weapon,
      ShipComponentSlot::Defense,  ShipComponentSlot::Utility,
      ShipComponentSlot::Cargo,    ShipComponentSlot::Habitation};
  for (const auto slot : kinds) {
    const auto capacity = slot_count(hull->slots, slot);
    const auto used = slot_count(installed, slot);
    if (used > capacity)
      result.issues.push_back(
          "Too many " + std::string(ship_component_slot_name(slot)) +
          " components: " + std::to_string(used) + " installed, " +
          std::to_string(capacity) + " slots.");
  }
  for (const auto slot : kinds) {
    if (slot_count(hull->required_slots, slot) == 0) continue;
    if (slot_count(installed, slot) == 0)
      result.issues.push_back("Missing required " +
                              std::string(ship_component_slot_name(slot)) +
                              " component.");
  }
  const auto resolved = resolve_authored_ship_design(design);
  if (resolved.strategic_speed <= 0.)
    result.issues.push_back("Resolved strategic speed must be positive.");
  return result;
}

ShipDesignDefinition resolve_authored_ship_design(
    const AuthoredShipDesign &design) {
  const auto &hull = get_ship_hull(design.hull_id);
  ShipDesignDefinition result;
  result.id = design.id;
  result.name = design.name.empty() ? hull.name : design.name;
  result.description =
      design.description.empty() ? hull.description : design.description;
  result.role = hull.role;
  result.industry_cost = hull.industry_cost;
  result.credit_cost = hull.credit_cost;
  result.population_cost_millions = hull.population_cost_millions;
  result.strategic_speed = hull.strategic_speed;
  result.maximum_leg_range_light_years = hull.maximum_leg_range_light_years;
  result.fuel_endurance_light_years = hull.fuel_endurance_light_years;
  result.sensor_range = hull.sensor_range;
  result.cargo_material_capacity = hull.cargo_material_capacity;
  result.cargo_transfer_rate_per_day = hull.cargo_transfer_rate_per_day;
  result.crew_complement_individuals = hull.crew_complement_individuals;
  result.prerequisites = hull.prerequisites;
  for (const auto &component_id : design.component_ids) {
    const auto *component = find_ship_component(component_id);
    if (!component) continue;
    result.industry_cost += component->industry_cost;
    result.credit_cost += component->credit_cost;
    result.strategic_speed += component->strategic_speed_delta;
    result.maximum_leg_range_light_years +=
        component->leg_range_delta_light_years;
    result.fuel_endurance_light_years +=
        component->fuel_endurance_delta_light_years;
    result.sensor_range += component->sensor_range_delta;
    result.cargo_material_capacity += component->cargo_capacity_delta;
    result.cargo_transfer_rate_per_day += component->cargo_transfer_rate_delta;
    result.population_cost_millions += component->population_cost_millions_delta;
    result.crew_complement_individuals += component->crew_delta;
  }
  return result;
}

CombatProfileDefinition resolve_authored_combat_profile(
    const AuthoredShipDesign &design) {
  const auto &hull = get_ship_hull(design.hull_id);
  CombatProfileDefinition result;
  result.id = "authored:" + design.id;
  result.max_shields = hull.max_shields;
  result.max_armor = hull.max_armor;
  result.max_hull = hull.max_hull;
  result.weapon_damage = hull.weapon_damage;
  result.weapon_interval_days = hull.weapon_interval_days;
  result.retreat_delay_days = hull.retreat_delay_days;
  for (const auto &component_id : design.component_ids) {
    const auto *component = find_ship_component(component_id);
    if (!component) continue;
    result.max_shields += component->max_shields_delta;
    result.max_armor += component->max_armor_delta;
    result.max_hull += component->max_hull_delta;
    result.weapon_damage += component->weapon_damage_delta;
    if (component->weapon_interval_days &&
        (result.weapon_interval_days <= 0. ||
         *component->weapon_interval_days < result.weapon_interval_days))
      result.weapon_interval_days = *component->weapon_interval_days;
  }
  if (result.weapon_interval_days <= 0.) result.weapon_interval_days = 8.;
  return result;
}

MassiveCombatLoadout massive_loadout_from_authored(
    const AuthoredShipDesign &design) {
  const auto &hull = get_ship_hull(design.hull_id);
  auto loadout =
      massive_loadout_from_legacy(resolve_authored_combat_profile(design));
  // The legacy bridge emits a generic beam group for armed profiles; authored
  // weapon fit comes from component weapon groups instead.
  loadout.weapons.clear();
  loadout.module_slot_capacity = static_cast<int>(hull.slots.size());
  for (const auto &component_id : design.component_ids) {
    const auto *component = find_ship_component(component_id);
    if (!component) continue;
    if (component->weapon_group) {
      auto group = *component->weapon_group;
      if (group.id.empty()) group.id = component->id;
      loadout.weapons.push_back(std::move(group));
    }
    if (component->module) {
      auto module = *component->module;
      if (module.id.empty()) module.id = component->id;
      loadout.modules.push_back(std::move(module));
    }
  }
  return loadout;
}

std::optional<ShipDesignDefinition> resolve_ship_design(
    ShipDesignReadView world, int civilization_id, std::string_view design_id) {
  if (const auto *authored = find_authored_ship_design(
          world.authored_designs, civilization_id, design_id))
    return resolve_authored_ship_design(*authored);
  if (const auto *catalog = find_ship_design(design_id)) return *catalog;
  return std::nullopt;
}

std::string allocate_ship_design_id(std::span<const AuthoredShipDesign> designs,
                                    int civilization_id) {
  const std::string prefix = "design." + std::to_string(civilization_id) + ".";
  std::int64_t next = 1;
  for (const auto &design : designs) {
    if (!design.id.starts_with(prefix)) continue;
    std::int64_t sequence{};
    const auto *begin = design.id.data() + prefix.size();
    const auto *end = design.id.data() + design.id.size();
    if (std::from_chars(begin, end, sequence).ec == std::errc{} &&
        sequence >= next)
      next = sequence + 1;
  }
  return prefix + std::to_string(next);
}

namespace {

ShipDesignReadView authored_view(const FreshCampaignState &campaign) {
  ShipDesignReadView view;
  view.construction = campaign.construction;
  view.authored_designs = campaign.authored_ship_designs;
  view.capability_query = [technologies = &campaign.technologies](
                              int civilization_id,
                              std::string_view capability_id) {
    return prototype_shipbuilding_has_capability(*technologies,
                                                 civilization_id,
                                                 capability_id);
  };
  return view;
}

bool civilization_exists(const FreshCampaignState &campaign,
                         int civilization_id) {
  return std::ranges::any_of(
      campaign.civilizations,
      [=](const auto &civilization) { return civilization.id == civilization_id; });
}

} // namespace

ShipDesignCommandResult create_ship_design(FreshCampaignState &campaign,
                                           int civilization_id,
                                           AuthoredShipDesign spec) {
  if (!civilization_exists(campaign, civilization_id))
    return {false, "Unknown civilization."};
  spec.owner_civilization_id = civilization_id;
  if (spec.id.empty())
    spec.id = allocate_ship_design_id(campaign.authored_ship_designs,
                                      civilization_id);
  else if (find_authored_ship_design(campaign.authored_ship_designs, spec.id))
    return {false, "A ship design with id '" + spec.id + "' already exists."};
  const auto validation =
      validate_authored_ship_design(spec, authored_view(campaign),
                                    civilization_id);
  if (!validation.valid()) {
    std::string message = "Ship design is invalid:";
    for (const auto &issue : validation.issues) message += " " + issue;
    return {false, std::move(message)};
  }
  campaign.authored_ship_designs.push_back(std::move(spec));
  return {true, "Ship design committed.",
          campaign.authored_ship_designs.back().id};
}

ShipDesignCommandResult update_ship_design_metadata(
    FreshCampaignState &campaign, int civilization_id,
    std::string_view design_id, std::string name, std::string description) {
  const auto owned = std::ranges::find_if(
      campaign.authored_ship_designs, [&](const auto &design) {
        return design.id == design_id &&
               design.owner_civilization_id == civilization_id;
      });
  if (owned == campaign.authored_ship_designs.end())
    return {false, "Unknown ship design."};
  if (name.empty()) return {false, "A design requires a name."};
  owned->name = std::move(name);
  owned->description = std::move(description);
  return {true, "Ship design updated.", owned->id};
}

ShipDesignCommandResult retire_ship_design(FreshCampaignState &campaign,
                                           int civilization_id,
                                           std::string_view design_id) {
  const auto owned = std::ranges::find_if(
      campaign.authored_ship_designs, [&](const auto &design) {
        return design.id == design_id &&
               design.owner_civilization_id == civilization_id;
      });
  if (owned == campaign.authored_ship_designs.end())
    return {false, "Unknown ship design."};
  for (const auto &yard : campaign.shipyards) {
    if (yard.civilization_id != civilization_id) continue;
    if (yard.active_design_id && *yard.active_design_id == design_id)
      return {false, "Cannot retire a design with an active shipyard build."};
    if (std::ranges::any_of(yard.queued_builds, [&](const auto &order) {
          return order.design_id == design_id;
        }))
      return {false,
              "Cannot retire a design with queued shipyard builds."};
  }
  campaign.authored_ship_designs.erase(owned);
  return {true, "Ship design retired."};
}

std::vector<AuthoredShipDesignSaveDto> capture_authored_ship_designs(
    std::span<const AuthoredShipDesign> designs) {
  std::vector<AuthoredShipDesignSaveDto> result;
  result.reserve(designs.size());
  for (const auto &design : designs)
    result.push_back({design.id, design.name, design.description,
                      design.hull_id, design.owner_civilization_id,
                      design.component_ids});
  return result;
}

std::vector<AuthoredShipDesign> restore_authored_ship_designs(
    std::span<const AuthoredShipDesignSaveDto> source,
    std::span<const Civilization> civilizations) {
  std::unordered_set<std::string_view> seen;
  std::vector<AuthoredShipDesign> result;
  result.reserve(source.size());
  for (const auto &dto : source) {
    const auto owner = "Ship design '" + dto.id + "'";
    if (dto.id.empty() || !seen.insert(dto.id).second)
      throw ShipDesignPersistenceError("Duplicate or empty ship design id.");
    if (!std::ranges::any_of(civilizations, [&](const auto &civilization) {
          return civilization.id == dto.owner_civilization_id;
        }))
      throw ShipDesignPersistenceError(owner +
                                       " references unknown civilization " +
                                       std::to_string(dto.owner_civilization_id) +
                                       ".");
    const auto *hull = find_ship_hull(dto.hull_id);
    if (!hull)
      throw ShipDesignPersistenceError(owner + " references unknown hull '" +
                                       dto.hull_id + "'.");
    std::vector<ShipComponentSlot> installed;
    for (const auto &component_id : dto.component_ids) {
      const auto *component = find_ship_component(component_id);
      if (!component)
        throw ShipDesignPersistenceError(owner +
                                         " references unknown component '" +
                                         component_id + "'.");
      installed.push_back(component->slot);
    }
    for (const auto &slot : {ShipComponentSlot::Engine, ShipComponentSlot::Warp,
                            ShipComponentSlot::Sensor, ShipComponentSlot::Weapon,
                            ShipComponentSlot::Defense, ShipComponentSlot::Utility,
                            ShipComponentSlot::Cargo,
                            ShipComponentSlot::Habitation})
      if (slot_count(installed, slot) > slot_count(hull->slots, slot))
        throw ShipDesignPersistenceError(
            owner + " exceeds its " +
            std::string(ship_component_slot_name(slot)) + " slot capacity.");
    result.push_back({dto.id, dto.name, dto.description,
                      dto.owner_civilization_id, dto.hull_id,
                      dto.component_ids});
  }
  return result;
}

} // namespace stellar::core
