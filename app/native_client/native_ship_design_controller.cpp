#include "native_ship_design_controller.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/shipyard_state.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>

namespace stellar::native_ship_design {
namespace {
using namespace stellar::core;

template <class Range, class ProjectionMember>
[[nodiscard]] auto *find_one(Range &range, int id, ProjectionMember member) {
  const auto found = std::ranges::find(range, id, member);
  return found == range.end() ? nullptr : &*found;
}

[[nodiscard]] ShipDesignReadView
design_view(const FreshCampaignState &campaign,
            const AdaptiveResearchShipbuildingCapabilityView &capability) {
  ShipDesignReadView view;
  view.construction = campaign.construction;
  view.authored_designs = campaign.authored_ship_designs;
  view.capability_query = [&capability](int civilization_id,
                                        std::string_view capability_id) {
    return capability.has_civilization_capability(civilization_id,
                                                  capability_id);
  };
  return view;
}

[[nodiscard]] std::optional<std::string>
component_lock_reason(ShipDesignReadView world, int civilization_id,
                      const ShipComponentDefinition &component) {
  ShipDesignDefinition probe;
  probe.prerequisites = component.prerequisites;
  return ship_design_lock_reason(world, civilization_id, probe);
}

void fill_composition(NativeShipDesignComposition &out,
                      const ShipDesignDefinition &resolved,
                      const CombatProfileDefinition &combat) {
  out.industry_cost = resolved.industry_cost;
  out.credit_cost = resolved.credit_cost;
  out.strategic_speed = resolved.strategic_speed;
  out.maximum_leg_range_light_years =
      resolved.maximum_leg_range_light_years;
  out.fuel_endurance_light_years = resolved.fuel_endurance_light_years;
  out.sensor_range = resolved.sensor_range;
  out.cargo_capacity = resolved.cargo_material_capacity;
  out.max_shields = combat.max_shields;
  out.max_armor = combat.max_armor;
  out.max_hull = combat.max_hull;
  out.weapon_damage = combat.weapon_damage;
  out.crew = resolved.crew_complement_individuals;
}

} // namespace

void NativeShipDesignController::require_owner() const {
  if (owner_ != std::this_thread::get_id())
    throw std::runtime_error(
        "NativeShipDesignController is owned by the UI thread.");
}

std::string NativeShipDesignController::tr(std::string_view key,
                                           std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

NativeShipDesignView NativeShipDesignController::build(
    CampaignFrame &frame, std::uint64_t campaign_generation) {
  require_owner();
  auto &runtime = frame.runtime();
  auto &campaign = runtime.world().campaign();
  const auto *player = find_one(campaign.civilizations,
                                campaign.player_civilization_id,
                                &Civilization::id);
  if (!player || !player->is_player)
    throw std::runtime_error(
        "The campaign has no valid player civilization.");

  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  const auto world = design_view(campaign, capability);

  if (!generation_ || *generation_ != campaign_generation) {
    generation_ = campaign_generation;
    revision_ = 0;
  }
  if (revision_ == std::numeric_limits<std::uint64_t>::max())
    revision_ = 0;
  ++revision_;

  NativeShipDesignView view;
  view.campaign_generation = campaign_generation;
  view.design_revision = revision_;
  view.player_civilization_id = player->id;

  for (const auto &hull : ship_hull_catalog()) {
    NativeHullOption option;
    option.id = hull.id;
    option.name = hull.name;
    option.description = hull.description;
    option.role = hull.role;
    option.industry_cost = hull.industry_cost;
    option.credit_cost = hull.credit_cost;
    option.strategic_speed = hull.strategic_speed;
    option.slot_count = static_cast<int>(hull.slots.size());
    option.slots.assign(hull.slots.begin(), hull.slots.end());
    option.required_slots.assign(hull.required_slots.begin(),
                                 hull.required_slots.end());
    ShipDesignDefinition probe;
    probe.prerequisites = hull.prerequisites;
    option.lock_reason =
        ship_design_lock_reason(world, player->id, probe);
    view.hulls.push_back(std::move(option));
  }

  for (const auto &component : ship_component_catalog()) {
    NativeComponentOption option;
    option.id = component.id;
    option.name = component.name;
    option.description = component.description;
    option.slot = component.slot;
    option.slot_name = ship_component_slot_name(component.slot);
    option.industry_cost = component.industry_cost;
    option.credit_cost = component.credit_cost;
    option.lock_reason =
        component_lock_reason(world, player->id, component);
    view.components.push_back(std::move(option));
  }

  for (const auto &design : campaign.authored_ship_designs) {
    if (design.owner_civilization_id != player->id) continue;
    const auto *hull = find_ship_hull(design.hull_id);
    if (!hull) continue;
    NativeAuthoredDesignRow row;
    row.id = design.id;
    row.name = design.name;
    row.description = design.description;
    row.hull_id = design.hull_id;
    row.hull_name = hull->name;
    row.role = hull->role;
    row.component_ids = design.component_ids;
    row.component_names.reserve(design.component_ids.size());
    for (const auto &component_id : design.component_ids) {
      const auto *component = find_ship_component(component_id);
      row.component_names.emplace_back(component ? component->name
                                                 : component_id);
    }
    const auto resolved = resolve_authored_ship_design(design);
    const auto combat = resolve_authored_combat_profile(design);
    row.industry_cost = resolved.industry_cost;
    row.credit_cost = resolved.credit_cost;
    row.strategic_speed = resolved.strategic_speed;
    row.maximum_leg_range_light_years =
        resolved.maximum_leg_range_light_years;
    row.fuel_endurance_light_years = resolved.fuel_endurance_light_years;
    row.sensor_range = resolved.sensor_range;
    row.cargo_capacity = resolved.cargo_material_capacity;
    row.max_shields = combat.max_shields;
    row.max_armor = combat.max_armor;
    row.max_hull = combat.max_hull;
    row.weapon_damage = combat.weapon_damage;
    row.crew = resolved.crew_complement_individuals;

    const auto validation =
        validate_authored_ship_design(design, world, player->id);
    row.valid = validation.valid();
    row.issues = validation.issues;
    row.can_retire = true;
    for (const auto &yard : campaign.shipyards) {
      if (yard.civilization_id != player->id) continue;
      const bool referenced =
          (yard.active_design_id && *yard.active_design_id == design.id) ||
          std::ranges::any_of(yard.queued_builds, [&](const auto &order) {
            return order.design_id == design.id;
          });
      if (referenced) {
        row.can_retire = false;
        row.retire_blocker = tr(
            "DESIGN_RETIRE_BLOCKED",
            "A shipyard order is building this design.");
        break;
      }
    }
    view.designs.push_back(std::move(row));
  }
  return view;
}

NativeShipDesignComposition NativeShipDesignController::compose(
    CampaignFrame &frame, std::string_view hull_id,
    std::span<const std::string> component_ids) {
  require_owner();
  auto &runtime = frame.runtime();
  auto &campaign = runtime.world().campaign();
  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  const auto world = design_view(campaign, capability);

  NativeShipDesignComposition result;
  AuthoredShipDesign spec;
  spec.owner_civilization_id = campaign.player_civilization_id;
  spec.hull_id = std::string(hull_id);
  spec.name = "preview";
  spec.component_ids.assign(component_ids.begin(), component_ids.end());
  const auto validation = validate_authored_ship_design(
      spec, world, campaign.player_civilization_id);
  result.issues = validation.issues;
  result.can_commit = validation.valid();
  if (const auto *hull = find_ship_hull(hull_id); hull)
    fill_composition(result, resolve_authored_ship_design(spec),
                     resolve_authored_combat_profile(spec));
  return result;
}

namespace {
bool bound_command(std::uint64_t campaign_generation,
                   std::uint64_t expected_revision,
                   const std::optional<std::uint64_t> &generation,
                   std::uint64_t revision) {
  return generation && *generation == campaign_generation &&
         expected_revision == revision;
}
} // namespace

NativeShipDesignCommandOutcome NativeShipDesignController::commit(
    CampaignFrame &frame, std::uint64_t campaign_generation,
    std::uint64_t expected_design_revision, std::string name,
    std::string_view hull_id, std::vector<std::string> component_ids) {
  require_owner();
  if (!bound_command(campaign_generation, expected_design_revision,
                     generation_, revision_))
    return {false, tr("DESIGN_NOTICE_STALE",
                      "The design bureau changed; review the refreshed "
                      "list before committing.")};
  auto &runtime = frame.runtime();
  auto &campaign = runtime.world().campaign();
  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  const ShipDesignCapabilityQuery query =
      [&capability](int civilization_id, std::string_view capability_id) {
        return capability.has_civilization_capability(civilization_id,
                                                      capability_id);
      };
  AuthoredShipDesign spec;
  spec.name = std::move(name);
  spec.hull_id = std::string(hull_id);
  spec.component_ids = std::move(component_ids);
  const auto result = create_ship_design(
      campaign, campaign.player_civilization_id, std::move(spec), query);
  return {result.accepted, result.message, result.design_id};
}

NativeShipDesignCommandOutcome NativeShipDesignController::retire(
    CampaignFrame &frame, std::uint64_t campaign_generation,
    std::uint64_t expected_design_revision, std::string_view design_id) {
  require_owner();
  if (!bound_command(campaign_generation, expected_design_revision,
                     generation_, revision_))
    return {false, tr("DESIGN_NOTICE_STALE",
                      "The design bureau changed; review the refreshed "
                      "list before retiring.")};
  auto &campaign = frame.runtime().world().campaign();
  const auto result = retire_ship_design(
      campaign, campaign.player_civilization_id, design_id);
  return {result.accepted, result.message, result.design_id};
}

NativeShipDesignCommandOutcome NativeShipDesignController::rename(
    CampaignFrame &frame, std::uint64_t campaign_generation,
    std::uint64_t expected_design_revision, std::string_view design_id,
    std::string name, std::string description) {
  require_owner();
  if (!bound_command(campaign_generation, expected_design_revision,
                     generation_, revision_))
    return {false, tr("DESIGN_NOTICE_STALE",
                      "The design bureau changed; review the refreshed "
                      "list before editing.")};
  auto &campaign = frame.runtime().world().campaign();
  const auto result =
      update_ship_design_metadata(campaign, campaign.player_civilization_id,
                                  design_id, std::move(name),
                                  std::move(description));
  return {result.accepted, result.message, result.design_id};
}

} // namespace stellar::native_ship_design
