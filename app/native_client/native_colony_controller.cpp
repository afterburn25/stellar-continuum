#include "native_colony_controller.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/freight.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace stellar::native_colony {
namespace {
using namespace stellar::core;

template <class Range, class Member>
auto *find_one(Range &range, const int id, Member member) {
  const auto found = std::ranges::find(range, id, member);
  return found == range.end() ? nullptr : &*found;
}

struct Context {
  IntegratedAdaptiveCampaignRuntime &runtime;
  FreshCampaignState &world;
  const Civilization &player;
  CivilizationEconomy &economy;
  ConstructionReadView construction;
};

Context context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto *player = find_one(world.civilizations,
                                world.player_civilization_id,
                                &Civilization::id);
  auto *economy = find_one(world.economies, world.player_civilization_id,
                           &CivilizationEconomy::civilization_id);
  if (!player || !player->is_player || !economy)
    throw std::runtime_error(
        "The player campaign has no complete colony economy state.");
  const auto *research = &runtime.research();
  auto query = [research](const int civilization_id,
                          const std::string_view capability_id) {
    return AdaptiveResearchConstructionCapabilityView(*research)
        .has_civilization_capability(civilization_id, capability_id);
  };
  return {runtime,
          world,
          *player,
          *economy,
          {world.civilizations, world.bodies, world.construction,
           world.colonies, world.economies, {}, std::move(query)}};
}

void append(std::ostringstream &out, const std::string_view value) {
  out << value.size() << ':' << value << ';';
}
template <class Value> void append(std::ostringstream &out, const Value value) {
  out << value << ';';
}

std::string signature(const NativeColonyView &view) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  append(out, view.player_civilization_id);
  append(out, view.system_id);
  append(out, view.body_id);
  append(out, view.colony_id);
  append(out, view.colony_name);
  append(out, view.body_display_name);
  append(out, view.population_species_id);
  append(out, view.resource_outpost);
  append(out, view.solid_surface);
  append(out, view.currency.name);
  append(out, view.currency.code);
  append(out, view.currency.symbol);
  append(out, view.currency.local_units_per_budget_unit);
  append(out, view.treasury_budget_units);
  append(out, view.stored_industry);
  append(out, view.population_millions);
  append(out, view.infrastructure);
  append(out, view.stability);
  append(out, view.working_age_population_millions);
  append(out, view.employed_population_millions);
  append(out, view.unemployed_population_millions);
  append(out, view.employment_rate);
  append(out, view.workforce_available_millions);
  append(out, view.workforce_demand_millions);
  append(out, view.food_capacity_millions);
  append(out, view.water_capacity_millions);
  append(out, view.housing_capacity_millions);
  append(out, view.supported_population_millions);
  append(out, view.sustenance_support_ratio);
  append(out, view.limiting_sustenance_supply);
  append(out, view.food_reserve_days);
  append(out, view.water_reserve_days);
  append(out, view.required_habitat_systems);
  append(out, view.building_capacity);
  append(out, view.surface_hub_level);
  append(out, view.habitat_support_reduction);
  append(out, view.environmental_wear_multiplier);
  append(out, view.power_supply);
  append(out, view.power_demand);
  append(out, view.stored_power_days);
  append(out, view.power_storage_capacity_days);
  append(out, view.storage_charge_per_day);
  append(out, view.storage_discharge_per_day);
  append(out, view.credits_per_day);
  append(out, view.upkeep_credits_per_day);
  append(out, view.industry_per_day);
  append(out, view.science_per_day);
  append(out, view.cargo_transfer_capacity_per_day);
  append(out, view.specialization_name);
  append(out, view.specialization_description);
  append(out, view.specialization_complexes);
  append(out, view.specialization_active);
  append(out, view.has_confirmed_deposit);
  append(out, view.extraction_per_day);
  append(out, view.stored_extracted_materials);
  append(out, view.extracted_material_capacity);
  append(out, view.remaining_deposit_materials);
  append(out, view.initial_deposit_materials);
  append(out, view.deposit_accessibility);
  append(out, view.extraction_yield_multiplier);
  append(out, view.deposit_material_name);
  append(out, view.deposit_grade);
  append(out, view.outpost_status);
  for (const auto &site : view.construction_sites) {
    append(out, site.building_id);
    append(out, site.type_id);
    append(out, site.name);
    append(out, site.x);
    append(out, site.z);
    append(out, site.rotation_degrees);
    append(out, site.industry_progress);
    append(out, site.industry_cost);
    append(out, site.progress_fraction);
    append(out, site.complete);
    append(out, site.powered);
    append(out, site.staffed);
    append(out, site.enabled);
    append(out, site.prioritized);
    append(out, site.condition);
    append(out, site.efficiency);
    append(out, site.stored_power_days);
    append(out, site.construction_stage);
    append(out, site.construction_stage_progress);
    append(out, site.remaining_construction_materials);
    append(out, site.pending_upgrade_type_id.value_or(""));
    append(out, site.upgrade_days_remaining);
  }
  for (const auto &option : view.available_buildings) {
    append(out, option.type_id);
    append(out, option.name);
    append(out, option.description);
    append(out, option.industry_cost);
    append(out, option.authorization_budget_units);
    append(out, option.footprint_radius);
    append(out, option.power_supply);
    append(out, option.power_demand);
    append(out, option.workforce_required_millions);
  }
  return out.str();
}

} // namespace

void NativeColonyController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native colony projection must run on the simulation owner thread.");
}

void NativeColonyController::bind_generation(
    const std::uint64_t campaign_generation) {
  if (generation_ && campaign_generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace the current colony view.");
  if (!generation_ || *generation_ != campaign_generation) {
    generation_ = campaign_generation;
    last_signature_.clear();
    revision_ = 0;
  }
}

bool NativeColonyController::is_current_generation(
    const std::uint64_t value) const noexcept {
  return generation_ && *generation_ == value;
}

NativeColonyViewResult NativeColonyController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const native_system::NativeSystemSnapshot &system,
    const int selected_body_id) {
  require_owner();
  bind_generation(campaign_generation);
  auto current = context(frame);
  if (system.campaign_generation != campaign_generation ||
      system.observer_civilization_id != current.player.id ||
      !current.world.knowledge.is_system_known(current.player.id,
                                                system.system_id) ||
      current.world.knowledge.system_survey_level(current.player.id,
                                                   system.system_id) <
          SystemSurveyLevel::partially_surveyed)
    return {{}, "The campaign changed; refresh the known system first."};
  const auto shown_body = std::ranges::find(system.bodies, selected_body_id,
                                             &native_system::NativeSystemBody::id);
  if (shown_body == system.bodies.end())
    return {{}, "Select a known body before opening its settlement."};
  const auto colony = std::ranges::find_if(
      current.world.colonies, [&](const Colony &candidate) {
        return candidate.civilization_id == current.player.id &&
               candidate.system_id == system.system_id &&
               candidate.planetary_body_id == selected_body_id;
      });
  if (colony == current.world.colonies.end())
    return {{}, "The selected known body has no owned settlement."};

  const auto output = surface_colony_output(*colony);
  const auto specialization = surface_colony_specialization(*colony);
  const auto habitat = colony_habitat_support(*colony, current.world.bodies);
  const auto outpost = resource_outpost_snapshot(
      current.world.bodies, current.world.economies, *colony);
  const auto sustenance = colony_sustenance_capacity(
      current.world.bodies, *colony, surface_sustenance_projection(output));
  bool automation = false;
  if (const auto *construction =
          find_one(current.world.construction, current.player.id,
                   &ConstructionState::civilization_id))
    automation = std::ranges::contains(construction->completed_project_ids,
                                       "industrial_automation");
  const auto labor = colony_labor(
      *colony, automation,
      std::min(output.workforce_available_millions,
               output.workforce_demand_millions));

  NativeColonyView view;
  view.campaign_generation = campaign_generation;
  view.player_civilization_id = current.player.id;
  view.system_id = system.system_id;
  view.body_id = selected_body_id;
  view.colony_id = colony->id;
  view.colony_name = colony->name;
  view.body_display_name = shown_body->name;
  view.population_species_id = colony->population_species_id;
  view.resource_outpost = colony->kind == SettlementKind::ResourceOutpost;
  view.solid_surface = shown_body->details && shown_body->details->has_solid_surface;
  view.surface_visual_class = shown_body->visual_class;
  view.currency = sovereign_currency_for_civilization(
      current.world.civilizations, current.player.id);
  view.treasury_budget_units = current.economy.credits;
  view.stored_industry = current.economy.industry;
  view.formatted_treasury = view.currency.format(current.economy.credits);
  view.population_millions = colony->population_millions;
  view.infrastructure = colony->infrastructure;
  view.stability = colony->stability;
  view.working_age_population_millions = labor.working_age_population_millions;
  view.employed_population_millions = labor.employed_population_millions;
  view.unemployed_population_millions = labor.unemployed_population_millions;
  view.employment_rate = labor.employment_rate;
  view.workforce_available_millions = output.workforce_available_millions;
  view.workforce_demand_millions = output.workforce_demand_millions;
  view.food_capacity_millions = sustenance.food_capacity_millions;
  view.water_capacity_millions = sustenance.water_capacity_millions;
  view.housing_capacity_millions = sustenance.housing_capacity_millions;
  view.supported_population_millions = sustenance.supported_population_millions;
  view.sustenance_support_ratio = sustenance.support_ratio;
  view.limiting_sustenance_supply = sustenance.limiting_supply;
  const auto reserve_population = std::max(.001, colony->population_millions);
  view.food_reserve_days =
      colony->stored_food_population_days_millions / reserve_population;
  view.water_reserve_days =
      colony->stored_water_population_days_millions / reserve_population;
  view.required_habitat_systems = habitat.environment
                                      ? habitat.environment->required_mitigation_categories
                                      : 0;
  view.building_capacity = surface_building_capacity(*colony);
  view.surface_hub_level = colony->surface_hub_level;
  view.habitat_support_reduction = output.habitat_support_reduction;
  view.environmental_wear_multiplier =
      surface_environmental_wear(current.world.bodies, *colony);
  view.power_supply = output.supply;
  view.power_demand = output.demand;
  view.stored_power_days = output.stored_power_days;
  view.power_storage_capacity_days = output.power_storage_capacity_days;
  view.storage_charge_per_day = output.storage_charge_per_day;
  view.storage_discharge_per_day = output.storage_discharge_per_day;
  view.credits_per_day = view.resource_outpost ? 0. : output.credits_per_day;
  view.upkeep_credits_per_day = output.upkeep_credits_per_day;
  view.industry_per_day = view.resource_outpost
                              ? 0.
                              : output.industry_per_day *
                                    current.economy.last_base_operations_funding_fraction;
  view.science_per_day = output.science_per_day;
  view.cargo_transfer_capacity_per_day =
      FreightSimulation::port_transfer_capacity_per_day(*colony);
  view.specialization_name = specialization.name;
  view.specialization_description = specialization.description;
  view.specialization_complexes = specialization.completed_complexes;
  view.specialization_active = specialization.active;
  view.has_confirmed_deposit = outpost.has_confirmed_deposit;
  view.extraction_per_day = outpost.extraction_per_day;
  view.stored_extracted_materials = outpost.stored_materials;
  view.extracted_material_capacity = outpost.storage_capacity;
  view.remaining_deposit_materials = outpost.remaining_deposit_materials;
  view.initial_deposit_materials = outpost.initial_deposit_materials;
  view.deposit_accessibility = outpost.deposit_accessibility;
  view.extraction_yield_multiplier = outpost.extraction_yield_multiplier;
  view.deposit_material_name = outpost.deposit_material_name;
  view.deposit_grade = outpost.deposit_grade;
  view.outpost_status = outpost.status;

  std::unordered_set<int> powered(output.powered_building_ids.begin(),
                                  output.powered_building_ids.end());
  std::unordered_set<int> staffed(output.staffed_building_ids.begin(),
                                  output.staffed_building_ids.end());
  for (const auto &building : colony->surface_buildings) {
    const auto *definition = find_surface_building(building.type_id);
    if (!definition)
      throw std::runtime_error("An owned colony has an unknown surface building.");
    const auto stage = surface_construction_stage(building);
    view.construction_sites.push_back(
        {.building_id = building.id,
         .type_id = building.type_id,
         .name = definition->name,
         .x = building.x,
         .z = building.z,
         .rotation_degrees = building.rotation_degrees,
         .industry_progress = building.industry_progress,
         .industry_cost = definition->industry_cost,
         .progress_fraction = definition->industry_cost > 0.
                                  ? std::clamp(building.industry_progress /
                                                   definition->industry_cost,
                                               0., 1.)
                                  : 1.,
         .complete = building.is_complete,
         .powered = powered.contains(building.id),
         .staffed = staffed.contains(building.id),
         .enabled = building.is_enabled,
         .prioritized = building.operating_priority > 0,
         .condition = building.condition,
         .efficiency = building.condition <= minimum_operational_condition
                           ? 0.
                           : .5 + .5 * building.condition,
         .stored_power_days = building.stored_power_days,
         .construction_stage = stage.name,
         .construction_stage_progress = stage.phase_progress,
         .remaining_construction_materials = stage.remaining_materials,
         .pending_upgrade_type_id = building.pending_upgrade_type_id,
         .upgrade_days_remaining = building.upgrade_days_remaining});
  }
  std::ranges::sort(view.construction_sites, {}, &NativeSurfaceSite::building_id);
  for (const auto &definition : surface_building_catalog()) {
    if (!surface_available_for_settlement(*colony, definition)) continue;
    const auto authorization =
        surface_authorization_cost(current.construction, *colony, definition);
    view.available_buildings.push_back(
        {.type_id = definition.id,
         .name = definition.name,
         .description = definition.description,
         .industry_cost = definition.industry_cost,
         .authorization_budget_units = authorization,
         .formatted_authorization = view.currency.format(authorization),
         .footprint_radius = definition.footprint_radius,
         .power_supply = definition.power_supply,
         .power_demand = definition.power_demand,
         .workforce_required_millions =
             definition.workforce_required_millions});
  }

  const auto current_signature = signature(view);
  if (current_signature != last_signature_) {
    ++revision_;
    last_signature_ = current_signature;
  }
  view.revision = revision_;
  return {std::move(view), {}};
}

} // namespace stellar::native_colony
