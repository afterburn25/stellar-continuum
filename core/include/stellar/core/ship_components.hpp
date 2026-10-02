#pragma once
#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/combat_state.hpp>
#include <stellar/core/ship_designs.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
// Component slots are the authored-design composition vocabulary. Slots are
// design-level kinds; tactical loadouts derive from the massive module types.
enum class ShipComponentSlot {
    Engine, Warp, Sensor, Weapon, Defense, Utility, Cargo, Habitation
};
std::string_view ship_component_slot_name(ShipComponentSlot slot);

// A component contributes additive stat deltas on top of a hull. Costs add to
// the design's build cost; prerequisites reuse the ship-design vocabulary and
// are validated at design-commit time against the owning civilization.
struct ShipComponentDefinition {
    std::string id, name, description;
    ShipComponentSlot slot{};
    double industry_cost{}, credit_cost{};
    double strategic_speed_delta{}, leg_range_delta_light_years{},
        fuel_endurance_delta_light_years{};
    float sensor_range_delta{};
    double cargo_capacity_delta{}, cargo_transfer_rate_delta{},
        population_cost_millions_delta{};
    int crew_delta{};
    double max_shields_delta{}, max_armor_delta{}, max_hull_delta{},
        weapon_damage_delta{};
    // Absolute weapon cadence contributed by a weapon component; the resolved
    // profile uses the smallest provided interval.
    std::optional<double> weapon_interval_days;
    ShipDesignPrerequisites prerequisites;
    // Optional tactical-bridge contributions copied into the derived
    // MassiveCombatLoadout so authored modules reach battle encounters.
    std::optional<MassiveWeaponGroup> weapon_group;
    std::optional<MassiveModuleState> module;
};
struct ShipHullDefinition {
    std::string id, name, description;
    FleetRole role{};
    double industry_cost{}, credit_cost{}, population_cost_millions{};
    double strategic_speed{}, maximum_leg_range_light_years{},
        fuel_endurance_light_years{};
    float sensor_range{};
    double cargo_material_capacity{}, cargo_transfer_rate_per_day{};
    int crew_complement_individuals{};
    double max_shields{}, max_armor{}, max_hull{}, weapon_damage{},
        weapon_interval_days{}, retreat_delay_days{};
    ShipDesignPrerequisites prerequisites;
    // Ordered slot plan: each entry is one installable component.
    std::vector<ShipComponentSlot> slots;
    // Slot kinds that must hold at least one component for a valid design.
    std::vector<ShipComponentSlot> required_slots;
};
struct AuthoredShipDesignValidation {
    std::vector<std::string> issues;
    bool valid() const noexcept { return issues.empty(); }
};
struct AuthoredShipDesignSaveDto {
    std::string id, name, description, hull_id;
    int owner_civilization_id{};
    std::vector<std::string> component_ids;
};
struct ShipDesignCommandResult {
    bool accepted{};
    std::string message;
    std::optional<std::string> design_id;
};
struct FreshCampaignState;
// Thrown by restore_authored_ship_designs when a saved design is structurally
// invalid (unknown hull/component, slot overflow, duplicate id, bad owner).
class ShipDesignPersistenceError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

std::span<const ShipHullDefinition> ship_hull_catalog();
std::span<const ShipComponentDefinition> ship_component_catalog();
const ShipHullDefinition *find_ship_hull(std::string_view id);
const ShipHullDefinition &get_ship_hull(std::string_view id);
const ShipComponentDefinition *find_ship_component(std::string_view id);
const ShipComponentDefinition &get_ship_component(std::string_view id);
const AuthoredShipDesign *find_authored_ship_design(
    std::span<const AuthoredShipDesign> designs, std::string_view id);
const AuthoredShipDesign *find_authored_ship_design(
    std::span<const AuthoredShipDesign> designs, int civilization_id,
    std::string_view id);

// Validation evaluates hull/component compatibility plus prerequisites against
// the owning civilization; it never mutates state.
AuthoredShipDesignValidation validate_authored_ship_design(
    const AuthoredShipDesign &design, ShipDesignReadView world,
    int civilization_id);
// Deterministic derived stats: hull base plus summed component deltas.
ShipDesignDefinition resolve_authored_ship_design(
    const AuthoredShipDesign &design);
CombatProfileDefinition resolve_authored_combat_profile(
    const AuthoredShipDesign &design);
MassiveCombatLoadout massive_loadout_from_authored(
    const AuthoredShipDesign &design);
// Resolves authored designs owned by the civilization first, then the fixed
// catalog. Returns nullopt when nothing matches.
std::optional<ShipDesignDefinition> resolve_ship_design(
    ShipDesignReadView world, int civilization_id, std::string_view design_id);
// Deterministic "design.<civ>.<n>" allocation over existing ids.
std::string allocate_ship_design_id(
    std::span<const AuthoredShipDesign> designs, int civilization_id);

// Authoritative design commands against campaign state. Validation runs before
// any mutation; rejected commands leave the campaign untouched.
ShipDesignCommandResult create_ship_design(FreshCampaignState &campaign,
                                           int civilization_id,
                                           AuthoredShipDesign spec);
ShipDesignCommandResult update_ship_design_metadata(
    FreshCampaignState &campaign, int civilization_id,
    std::string_view design_id, std::string name, std::string description);
ShipDesignCommandResult retire_ship_design(FreshCampaignState &campaign,
                                           int civilization_id,
                                           std::string_view design_id);

// Persistence codec. Restore validates structure (owner, hull, components,
// slot capacity) but does not re-evaluate capability prerequisites.
std::vector<AuthoredShipDesignSaveDto> capture_authored_ship_designs(
    std::span<const AuthoredShipDesign> designs);
std::vector<AuthoredShipDesign> restore_authored_ship_designs(
    std::span<const AuthoredShipDesignSaveDto> source,
    std::span<const Civilization> civilizations);
} // namespace stellar::core
