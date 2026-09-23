#include <stellar/core/campaign_warfare_projection.hpp>

#include <stellar/core/combat_state.hpp>
#include <stellar/core/ship_designs.hpp>

#include <algorithm>
#include <unordered_map>

namespace stellar::core {
namespace {

const FleetState *find_fleet(std::span<const FleetState> fleets, int id) {
  const auto it =
      std::find_if(fleets.begin(), fleets.end(),
                   [id](const FleetState &f) { return f.id == id; });
  return it == fleets.end() ? nullptr : &*it;
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 int id) {
  const auto it =
      std::find_if(systems.begin(), systems.end(),
                   [id](const StellarSystem &s) { return s.id == id; });
  return it == systems.end() ? nullptr : &*it;
}

// The authoritative combat profile backing a fleet: its live combat
// state first, then the design's declared profile, then the role
// default — the same precedence Core uses when minting combat state.
const CombatProfileDefinition &profile_for(const FleetState &fleet) {
  if (fleet.combat && !fleet.combat->profile_id.empty())
    if (const auto *profile =
            find_combat_profile(fleet.combat->profile_id))
      return *profile;
  const auto &design = resolve_fleet_ship_design(
      fleet.design_id ? std::optional<std::string_view>(*fleet.design_id)
                      : std::nullopt,
      fleet.role);
  if (design.combat_profile_id && !design.combat_profile_id->empty())
    if (const auto *profile =
            find_combat_profile(*design.combat_profile_id))
      return *profile;
  return get_combat_profile(default_combat_profile_id(fleet.role));
}

engine::ShipClass class_for(const CombatProfileDefinition &profile,
                            double strategic_speed) {
  engine::ShipClass cls;
  cls.id = profile.id;
  cls.role = "fleet";
  cls.attack = profile.sustained_damage_per_day();
  cls.defense = 0.0;
  cls.hull = profile.max_shields + profile.max_armor + profile.max_hull;
  cls.speed = strategic_speed;
  cls.supply_per_day = 0.0;
  cls.interdiction = 0.0;
  return cls;
}

} // namespace

engine::FleetOrder
project_fleet_order(const FleetState &fleet,
                    std::span<const FleetState> fleets,
                    std::span<const StellarSystem> systems) {
  using engine::FleetOrderKind;
  const bool retreating =
      fleet.return_to_base_requested ||
      (fleet.combat &&
       (fleet.combat->order == MilitaryOrderType::Retreat ||
        fleet.combat->is_disengaged));
  if (retreating)
    return {FleetOrderKind::Retreat, fleet.position.x, fleet.position.y};
  if (fleet.transit_phase != FleetTransitPhase::None ||
      fleet.destination_system_id) {
    if (fleet.destination_system_id) {
      if (const auto *system =
              find_system(systems, *fleet.destination_system_id))
        return {FleetOrderKind::Move, system->position.x,
                system->position.y};
    }
    // Transit without a resolvable destination: steer at the in-flight
    // local target when present so projected headings stay meaningful.
    return {FleetOrderKind::Move, fleet.local_transit_target.x,
            fleet.local_transit_target.y};
  }
  if (fleet.combat && fleet.combat->order == MilitaryOrderType::Attack &&
      fleet.combat->target_fleet_id) {
    if (const auto *target =
            find_fleet(fleets, *fleet.combat->target_fleet_id))
      return {FleetOrderKind::Move, target->position.x,
              target->position.y};
  }
  return {FleetOrderKind::Hold, fleet.position.x, fleet.position.y};
}

engine::WarfareModel
project_warfare_theater(std::span<const FleetState> fleets,
                        std::span<const StellarSystem> systems) {
  engine::WarfareModel model;
  std::unordered_map<std::string, bool> defined;
  for (const auto &fleet : fleets) {
    if (!fleet.is_active) continue;
    const auto &profile = profile_for(fleet);
    if (defined.insert_or_assign(profile.id, true).second)
      model.define_class(class_for(profile, fleet.strategic_speed));
    model.add_fleet(static_cast<std::uint64_t>(fleet.id),
                    static_cast<std::uint64_t>(fleet.civilization_id),
                    fleet.position.x, fleet.position.y);
    double condition = 1.0;
    if (fleet.combat && profile.max_hull > 0.0)
      condition = std::clamp(fleet.combat->hull / profile.max_hull, 0.0,
                             1.0);
    double experience = 0.0;
    if (fleet.tactical_vessel)
      experience =
          std::min(1.0, fleet.tactical_vessel->battles_fought * 0.15);
    model.add_ships(static_cast<std::uint64_t>(fleet.id), profile.id,
                    1.0, condition, experience);
    model.set_order(static_cast<std::uint64_t>(fleet.id),
                    project_fleet_order(fleet, fleets, systems));
  }
  return model;
}

} // namespace stellar::core
