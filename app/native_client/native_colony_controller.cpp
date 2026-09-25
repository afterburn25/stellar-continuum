#include "native_colony_controller.hpp"
#include <stellar/core/campaign_observation.hpp>

#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/freight.hpp>
#include <stellar/core/surface_construction.hpp>
#include <stellar/engine/localization.hpp>

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
  append(out, view.owner_civilization_id);
  append(out, view.owner_name);
  append(out, view.developer_inspection);
  append(out, view.foreign_settlement);
  append(out, view.system_id);
  append(out, view.body_id);
  append(out, view.colony_id);
  append(out, view.colony_name);
  append(out, view.body_display_name);
  append(out, view.system_name);
  append(out, view.operating_funding);
  append(out, view.operating_arrears);
  append(out, view.empire_credit_flow);
  append(out, view.empire_industry_flow);
  append(out, view.construction_multiplier);
  append(out, view.local_credit_flow.net_credits_per_day);
  append(out, view.local_credit_flow.colony_revenue_per_day);
  append(out, view.local_credit_flow.trade_revenue_per_day);
  append(out, view.local_credit_flow.operating_costs_per_day);
  append(out, view.local_credit_flow.colony_administration_per_day);
  append(out, view.local_credit_flow.population_services_per_day);
  append(out, view.local_credit_flow.habitat_support_per_day);
  append(out, view.local_credit_flow.surface_maintenance_per_day);
  append(out, view.population_species_id);
  append(out, view.resource_outpost);
  append(out, view.solid_surface);
  append(out, view.homeworld);
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
  append(out, view.hub_name);
  append(out, view.hub_upgrade_available);
  append(out, view.can_afford_hub_upgrade);
  append(out, view.hub_upgrade_credit_budget_units);
  append(out, view.hub_upgrade_industry_cost);
  append(out, view.hub_upgrade_lock_reason);
  append(out, view.hub_upgrade_days_remaining);
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
  append(out, view.active_research_facilities);
  append(out, view.active_research_lab_units);
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
    append(out, site.slot_index);
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
    append(out, site.can_upgrade);
    append(out, site.upgrade_name);
    append(out, site.upgrade_credit_budget_units);
    append(out, site.upgrade_industry_cost);
    append(out, site.can_afford_upgrade);
    append(out, site.upgrade_lock_reason);
    append(out, site.repair_industry_cost);
    append(out, site.can_afford_repair);
    append(out, site.essential_service);
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

std::string NativeColonyController::tr(std::string_view key,
                                       std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

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
      observation_survey_level(current.world, current.player.id, system.system_id) <
          SystemSurveyLevel::partially_surveyed)
    return {{}, tr("COLONY_MSG_CAMPAIGN", "The campaign changed; refresh the known system first.")};
  const auto shown_body = std::ranges::find(system.bodies, selected_body_id,
                                             &native_system::NativeSystemBody::id);
  if (shown_body == system.bodies.end())
    return {{}, tr("COLONY_MSG_SELECT_BODY", "Select a known body before opening its settlement.")};
  const auto colony = std::ranges::find_if(
      current.world.colonies, [&](const Colony &candidate) {
        return can_inspect_settlement(current.world, current.player.id, candidate) &&
               candidate.system_id == system.system_id &&
               candidate.planetary_body_id == selected_body_id;
      });
  if (colony == current.world.colonies.end())
    return {{}, tr("COLONY_MSG_NO_SETTLEMENT", "The selected known body has no owned settlement.")};

  const auto *owner = find_one(current.world.civilizations, colony->civilization_id,
                                &Civilization::id);
  const auto *economy = find_one(current.world.economies, colony->civilization_id,
                                  &CivilizationEconomy::civilization_id);
  if (!owner || !economy)
    return {{}, tr("COLONY_MSG_OWNER_UNAVAILABLE", "The settlement owner or economy is unavailable.")};

  const auto output = surface_colony_output(*colony);
  const auto specialization = surface_colony_specialization(*colony);
  const auto habitat = colony_habitat_support(*colony, current.world.bodies);
  const auto outpost = resource_outpost_snapshot(
      current.world.bodies, current.world.economies, *colony);
  const auto sustenance = colony_sustenance_capacity(
      current.world.bodies, *colony, surface_sustenance_projection(output));
  bool automation = false;
  if (const auto *construction =
          find_one(current.world.construction, owner->id,
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
  view.owner_civilization_id = owner->id;
  view.developer_inspection = developer_observation(current.world, current.player.id);
  view.foreign_settlement = owner->id != current.player.id;
  view.system_id = system.system_id;
  view.body_id = selected_body_id;
  view.colony_id = colony->id;
  view.system_name=system.catalog_name;view.planet=*shown_body;
  view.simulation_days=frame.clock().simulation_days();view.illumination_star=stellar_host_physics(native_system::snapshot_stellar_system(system),shown_body->stellar_host);
  view.stellar_lighting=stellar::native_planets::system_lighting(system,*shown_body);
  const auto projected=native_system::project_system(system);
  for(const auto& marker:projected.bodies)if(marker.body_id==selected_body_id){const auto host=projected.stellar_hosts[shown_body->stellar_host];view.rotation_parent_bearing=native_system::parent_facing_bearing(projected,marker);view.illumination_x=marker.offset_x-host.x;view.illumination_y=marker.offset_y-host.y;break;}
  view.owner_name=owner->name;
  if(habitat.environment)view.natural_habitability=habitat.environment->natural_habitability;
  std::vector<EconomyConstructionState> econ_construction;
  for(const auto& state:current.world.construction)econ_construction.push_back({state.civilization_id,state.completed_project_ids});
  auto flow=economy_credit_flow({current.world.civilizations,current.world.bodies,econ_construction,{}},std::span<const Colony>(&*colony,1),current.world.economies,owner->id,false);
  flow.operating_costs_per_day-=flow.orbital_maintenance_per_day;flow.orbital_maintenance_per_day=0;flow.net_credits_per_day=flow.gross_income_per_day-flow.operating_costs_per_day;
  view.local_credit_flow=flow;
  view.operating_funding=economy->last_base_operations_funding_fraction;
  view.operating_arrears=economy->operating_arrears;
  view.empire_credit_flow=economy->last_credits_per_second;view.empire_industry_flow=economy->last_industry_per_second;
  view.construction_multiplier=surface_construction_cost_multiplier(current.construction,*colony);
  view.colony_name = colony->name;
  view.body_display_name = shown_body->name;
  view.population_species_id = colony->population_species_id;
  view.population_species_name = species_environment_profile(colony->population_species_id).display_name;
  view.resource_outpost = colony->kind == SettlementKind::ResourceOutpost;
  if (system.system_id == owner->home_system_id) {
    // Use the same Core body resolution as campaign seeding, never display names.
    // A later hostile environment must not prevent the colony UI from opening.
    try {
      view.homeworld = resolve_species_homeworld(owner->id,
          owner->species_id, owner->home_system_id,
          current.world.bodies).planetary_body_id == selected_body_id;
    } catch (const std::invalid_argument &) {
      view.homeworld = false;
    }
  }
  view.solid_surface = shown_body->details && shown_body->details->has_solid_surface;
  view.currency = sovereign_currency_for_civilization(
      current.world.civilizations, owner->id);
  view.treasury_budget_units = economy->credits;
  view.stored_industry = economy->industry;
  view.formatted_treasury = view.currency.format(economy->credits);
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
                                    economy->last_base_operations_funding_fraction;
  view.science_per_day = output.science_per_day;
  const auto *research_state =
      current.runtime.research().try_get_civilization(owner->id);
  if (research_state) {
    const auto context_id = "colony:" + std::to_string(colony->id);
    const auto &institutions =
        current.runtime.research_runtime().authority().expertise_catalog();
    for (const auto &institution : research_state->expertise().institutions()) {
      if (institution.context_id != std::optional<std::string>{context_id} ||
          institution.active_count <= 0)
        continue;
      const auto &definition =
          institutions.get_institution(institution.institution_archetype_id);
      view.active_research_facilities += institution.active_count;
      view.active_research_lab_units +=
          definition.effective_lab_units * institution.active_count;
    }
  }
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
  bool is_capital_hub = false;
  if (colony->kind == SettlementKind::Colony &&
      colony->system_id == owner->home_system_id) {
    const Colony *capital = nullptr;
    for (const auto &candidate : current.world.colonies)
      if (candidate.civilization_id == owner->id &&
          candidate.kind == SettlementKind::Colony &&
          candidate.system_id == owner->home_system_id &&
          (!capital || candidate.population_millions >
                           capital->population_millions))
        capital = &candidate;
    is_capital_hub = capital && capital->id == colony->id;
  }
  view.hub_name = view.resource_outpost ? "Sealed outpost hub"
                  : is_capital_hub    ? "Planetary hub"
                                      : "Command center";
  const auto hub_upgrade =
      surface_hub_upgrade_cost(current.construction, *colony);
  const auto hub_lock =
      hub_upgrade ? surface_hub_upgrade_lock_reason(current.construction,
                                                    owner->id, *colony)
                  : std::nullopt;
  const bool hub_pending = colony->surface_hub_upgrade_days_remaining > 0.;
  view.hub_upgrade_available = hub_upgrade.has_value() && !hub_pending;
  view.hub_upgrade_credit_budget_units =
      hub_upgrade ? hub_upgrade->credit_cost : 0.;
  view.hub_upgrade_industry_cost =
      hub_upgrade ? hub_upgrade->industry_cost : 0.;
  view.hub_upgrade_lock_reason = hub_lock.value_or("");
  view.can_afford_hub_upgrade =
      view.hub_upgrade_available && !hub_lock &&
      economy->credits + .0001 >= hub_upgrade->credit_cost &&
      economy->industry + .0001 >= hub_upgrade->industry_cost;
  view.hub_upgrade_days_remaining =
      colony->surface_hub_upgrade_days_remaining;
  const auto slots=planetary_building_slots(*colony);
  for (const auto &building : colony->surface_buildings) {
    const auto *definition = find_surface_building(building.type_id);
    if (!definition)
      throw std::runtime_error("An owned colony has an unknown surface building.");
    const auto stage = surface_construction_stage(building);
    const auto *upgrade = definition->upgrade_type_id
                              ? find_surface_building(*definition->upgrade_type_id)
                              : nullptr;
    const auto upgrade_authorization = surface_upgrade_authorization_cost(
        current.construction, *colony, *definition);
    const auto upgrade_lock = surface_building_upgrade_lock_reason(
        current.construction, owner->id, *definition);
    const auto repair_cost = surface_repair_industry_cost(building);
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
         .upgrade_days_remaining = building.upgrade_days_remaining,
         .can_upgrade = building.is_complete && upgrade &&
                        !building.pending_upgrade_type_id,
         .upgrade_name = upgrade ? upgrade->name : "",
         .upgrade_credit_budget_units = upgrade_authorization,
         .upgrade_industry_cost = definition->upgrade_industry_cost,
         .can_afford_upgrade = building.is_complete && upgrade &&
             economy->credits + .0001 >= upgrade_authorization &&
             economy->industry + .0001 >=
                 definition->upgrade_industry_cost,
         .upgrade_lock_reason = upgrade_lock.value_or(""),
         .repair_industry_cost = repair_cost,
         .can_afford_repair = repair_cost > 0. &&
             economy->industry + .0001 >= repair_cost,
         .essential_service =
             surface_essential_service_priority(building.type_id) > 0,
         .slot_index = slots.at(building.id)});
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
