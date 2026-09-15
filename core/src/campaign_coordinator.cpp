#include <stellar/core/campaign_coordinator.hpp>

#include <stellar/core/strategic_input_support.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

bool same_system(const StellarSystem &left, const StellarSystem &right) {
  return left.id == right.id && left.name == right.name &&
         left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.depth_light_years == right.position.depth_light_years &&
         left.primary == right.primary && left.secondary == right.secondary &&
         left.tertiary == right.tertiary &&
         left.catalog_preset_id == right.catalog_preset_id &&
         left.stellar_catalog_id == right.stellar_catalog_id &&
         left.archetype == right.archetype &&
         left.has_habitable_world == right.has_habitable_world &&
         left.has_anomaly == right.has_anomaly &&
         left.has_rare_resource == right.has_rare_resource &&
         left.has_pre_warp_civilization ==
             right.has_pre_warp_civilization;
}

template <typename Query, typename Fallback>
std::shared_ptr<Query> shared_query(Query query, Fallback fallback) {
  if (!query)
    query = std::move(fallback);
  return std::make_shared<Query>(std::move(query));
}

CivilizationStrategicRuntimeCoordinator make_default_strategic_runtime(
    const std::shared_ptr<CampaignShipbuildingCapabilityQuery> &capability) {
  StrategicShipbuildingCapabilityQuery strategic_capability =
      [capability](const StrategicInputWorldView &world, int civilization_id,
                   std::string_view capability_id) {
        return (*capability)(world.technologies, civilization_id,
                             capability_id);
      };
  CivilizationStrategicInputBuilder input_builder{
      {}, std::move(strategic_capability), {}};
  CivilizationStrategicDirector director{std::move(input_builder)};
  return CivilizationStrategicRuntimeCoordinator{std::move(director)};
}

EconomyWorldView economy_world(FreshCampaignState &campaign,
                               std::span<const EconomyConstructionState> construction,
                               std::span<const EconomyFleetState> fleets) {
  return {campaign.civilizations, campaign.bodies, construction, fleets};
}

ConstructionWorld construction_world(
    FreshCampaignState &campaign,
    const std::shared_ptr<CampaignConstructionCapabilityQuery> &capability) {
  auto query = [capability, technologies = &campaign.technologies](
                   int civilization_id, std::string_view capability_id) {
    return (*capability)(*technologies, civilization_id, capability_id);
  };
  return {campaign.civilizations, campaign.bodies, campaign.construction,
          campaign.colonies,       campaign.economies,
          std::span<const CivilizationConstructionCapabilities>{},
          std::move(query)};
}

ShipbuildingWorld shipbuilding_world(
    FreshCampaignState &campaign,
    const std::shared_ptr<CampaignShipbuildingCapabilityQuery> &capability,
    CivilizationStrategicRuntimeCoordinator &strategic,
    bool use_strategic_preferences) {
  auto query = [capability, technologies = &campaign.technologies](
                   int civilization_id, std::string_view capability_id) {
    return (*capability)(*technologies, civilization_id, capability_id);
  };
  std::function<ShipbuildingStrategicPreference(int)> preference;
  if (use_strategic_preferences)
    preference = [&strategic](int civilization_id) {
      return strategic.get_shipbuilding_preference(civilization_id);
    };
  return {campaign.civilizations,
          campaign.systems,
          campaign.construction,
          campaign.shipyards,
          campaign.colonies,
          campaign.economies,
          campaign.fleets,
          std::span<const ShipbuildingCapabilities>{},
          std::span<const ShipbuildingStrategicPreference>{},
          std::move(query),
          std::move(preference)};
}

StrategicInputWorldView strategic_input_world(FreshCampaignState &campaign,
                                               InterstellarLaneNetwork &lanes) {
  return {campaign.systems,      campaign.bodies,
          campaign.civilizations, campaign.colonies,
          campaign.fleets,        campaign.economies,
          campaign.technologies,  campaign.construction,
          campaign.knowledge,     lanes};
}

void upsert_budget(std::vector<ConstructionIndustryBudget> &budgets,
                   int civilization_id, double industry) {
  const auto found = std::find_if(
      budgets.begin(), budgets.end(), [=](const auto &budget) {
        return budget.civilization_id == civilization_id;
      });
  if (found == budgets.end())
    budgets.push_back({civilization_id, industry});
  else
    found->industry = industry;
}

void validate_allocation_value(double value, const char *parameter) {
  if (!std::isfinite(value) || value < 0.0)
    throw std::out_of_range(
        "Industry values must be finite and non-negative. (Parameter '" +
        std::string(parameter) + "')");
}

double sanitized_nonnegative(double value) {
  return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

struct MutableCombatCivilizationOutcome {
  double shield_damage_dealt{};
  double armor_damage_dealt{};
  double hull_damage_dealt{};
  double shield_damage_taken{};
  double armor_damage_taken{};
  double hull_damage_taken{};
  int enemy_vessels_destroyed{};
  int own_vessels_lost{};
  int retreats_initiated{};
  int successful_escapes{};
  double casualties_inflicted{};
  double casualties_suffered{};
};

struct CombatOutcomeAccumulator {
  int event_count{};
  int engagements_started{};
  int engagements_ended{};
  int damage_events{};
  int vessels_destroyed{};
  int retreats_initiated{};
  int successful_escapes{};
  double total_damage_applied{};
  double casualties{};
  std::map<int, MutableCombatCivilizationOutcome> civilizations;
};

void accumulate(CombatOutcomeAccumulator &result, const CombatEvent &event) {
  ++result.event_count;
  auto &actor = result.civilizations[event.actor_civilization_id];
  MutableCombatCivilizationOutcome *target = nullptr;
  if (event.target_civilization_id)
    target = &result.civilizations[*event.target_civilization_id];

  switch (event.type) {
  case CombatEventType::EngagementStarted:
    ++result.engagements_started;
    break;
  case CombatEventType::EngagementEnded:
    ++result.engagements_ended;
    break;
  case CombatEventType::DamageApplied: {
    ++result.damage_events;
    const auto shield = sanitized_nonnegative(event.shield_damage);
    const auto armor = sanitized_nonnegative(event.armor_damage);
    const auto hull = sanitized_nonnegative(event.hull_damage);
    result.total_damage_applied += shield + armor + hull;
    actor.shield_damage_dealt += shield;
    actor.armor_damage_dealt += armor;
    actor.hull_damage_dealt += hull;
    if (target) {
      target->shield_damage_taken += shield;
      target->armor_damage_taken += armor;
      target->hull_damage_taken += hull;
    }
    break;
  }
  case CombatEventType::FleetDestroyed: {
    ++result.vessels_destroyed;
    ++actor.enemy_vessels_destroyed;
    if (target)
      ++target->own_vessels_lost;
    const auto casualties = sanitized_nonnegative(
        event.embarked_population_casualties_millions);
    result.casualties += casualties;
    actor.casualties_inflicted += casualties;
    if (target)
      target->casualties_suffered += casualties;
    break;
  }
  case CombatEventType::FleetRetreatInitiated:
    ++result.retreats_initiated;
    ++actor.retreats_initiated;
    break;
  case CombatEventType::FleetEscaped:
    ++result.successful_escapes;
    ++actor.successful_escapes;
    break;
  }
}

std::vector<CombatCivilizationOutcomeSummary>
civilization_summaries(const CombatOutcomeAccumulator &source) {
  std::vector<CombatCivilizationOutcomeSummary> result;
  result.reserve(source.civilizations.size());
  for (const auto &[id, value] : source.civilizations)
    result.push_back(
        {id,
         value.shield_damage_dealt,
         value.armor_damage_dealt,
         value.hull_damage_dealt,
         value.shield_damage_taken,
         value.armor_damage_taken,
         value.hull_damage_taken,
         value.enemy_vessels_destroyed,
         value.own_vessels_lost,
         value.retreats_initiated,
         value.successful_escapes,
         value.casualties_inflicted,
         value.casualties_suffered});
  return result;
}

CombatSystemOutcomeSummary system_summary(
    std::optional<int> system_id, const CombatOutcomeAccumulator &source) {
  return {system_id,
          source.event_count,
          source.engagements_started,
          source.engagements_ended,
          source.damage_events,
          source.vessels_destroyed,
          source.retreats_initiated,
          source.successful_escapes,
          source.total_damage_applied,
          source.casualties,
          civilization_summaries(source)};
}

CombatSimulation &simulation(
    std::variant<CombatCommandRuntime, CombatSimulation> &combat) {
  if (auto *matched = std::get_if<CombatCommandRuntime>(&combat))
    return matched->simulation();
  return std::get<CombatSimulation>(combat);
}

const CombatSimulation &simulation(
    const std::variant<CombatCommandRuntime, CombatSimulation> &combat) {
  if (const auto *matched = std::get_if<CombatCommandRuntime>(&combat))
    return matched->simulation();
  return std::get<CombatSimulation>(combat);
}

} // namespace

double CombatCivilizationOutcomeSummary::total_damage_dealt() const noexcept {
  return shield_damage_dealt + armor_damage_dealt + hull_damage_dealt;
}

double CombatCivilizationOutcomeSummary::total_damage_taken() const noexcept {
  return shield_damage_taken + armor_damage_taken + hull_damage_taken;
}

CombatOutcomeSummary
summarize_combat_outcome(std::span<const CombatEvent> events) {
  if (events.empty())
    return {};

  CombatOutcomeAccumulator overall;
  std::map<int, CombatOutcomeAccumulator> systems;
  std::optional<CombatOutcomeAccumulator> unknown_system;
  for (const auto &event : events) {
    accumulate(overall, event);
    if (event.system_id)
      accumulate(systems[*event.system_id], event);
    else {
      if (!unknown_system)
        unknown_system.emplace();
      accumulate(*unknown_system, event);
    }
  }

  CombatOutcomeSummary result{
      overall.event_count,
      overall.engagements_started,
      overall.engagements_ended,
      overall.damage_events,
      overall.vessels_destroyed,
      overall.retreats_initiated,
      overall.successful_escapes,
      overall.total_damage_applied,
      overall.casualties,
      civilization_summaries(overall),
      {}};
  result.systems.reserve(systems.size() + (unknown_system ? 1U : 0U));
  for (const auto &[system_id, accumulator] : systems)
    result.systems.push_back(system_summary(system_id, accumulator));
  if (unknown_system)
    result.systems.push_back(system_summary(std::nullopt, *unknown_system));
  return result;
}

CombatOutcomeSummary SimulationStepResult::combat_outcome() const {
  return summarize_combat_outcome(combat_events);
}

CampaignSimulationState::CampaignSimulationState(FreshCampaignState campaign)
    : campaign_(std::move(campaign)) {}

FreshCampaignState &CampaignSimulationState::campaign() noexcept {
  return campaign_;
}

const FreshCampaignState &CampaignSimulationState::campaign() const noexcept {
  return campaign_;
}

InterstellarLaneNetwork &CampaignSimulationState::lanes() {
  const bool current =
      lane_astronomy_.size() == campaign_.systems.size() &&
      std::equal(lane_astronomy_.begin(), lane_astronomy_.end(),
                 campaign_.systems.begin(), same_system);
  if (!lanes_ || !current) {
    auto astronomy = campaign_.systems;
    InterstellarLaneNetwork replacement{astronomy};
    lanes_ = std::move(replacement);
    lane_astronomy_ = std::move(astronomy);
  }
  return *lanes_;
}

std::size_t CampaignSimulationState::cached_lane_route_tree_count() {
  return lanes().cached_route_tree_count();
}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    SourceCompatibleCampaignConfiguration configuration)
    : advance_legacy_research_(configuration.advance_legacy_research),
      use_strategic_shipbuilding_preferences_(
          configuration.use_strategic_shipbuilding_preferences),
      construction_capability_(shared_query(
          std::move(configuration.construction_capability),
          CampaignConstructionCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_construction_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      shipbuilding_capability_(shared_query(
          std::move(configuration.shipbuilding_capability),
          CampaignShipbuildingCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_shipbuilding_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      strategic_(make_default_strategic_runtime(shipbuilding_capability_)),
      combat_(std::in_place_type<CombatCommandRuntime>), subsystems_{} {}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    SourceCompatibleCampaignConfiguration configuration,
    CombatCommandRuntime matched_combat, CampaignSubsystemRuntime subsystems)
    : advance_legacy_research_(configuration.advance_legacy_research),
      use_strategic_shipbuilding_preferences_(
          configuration.use_strategic_shipbuilding_preferences),
      construction_capability_(shared_query(
          std::move(configuration.construction_capability),
          CampaignConstructionCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_construction_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      shipbuilding_capability_(shared_query(
          std::move(configuration.shipbuilding_capability),
          CampaignShipbuildingCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_shipbuilding_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      strategic_(make_default_strategic_runtime(shipbuilding_capability_)),
      combat_(std::in_place_type<CombatCommandRuntime>,
              std::move(matched_combat)),
      subsystems_(std::move(subsystems)) {}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    SourceCompatibleCampaignConfiguration configuration,
    CombatSimulation raw_combat, CampaignSubsystemRuntime subsystems)
    : advance_legacy_research_(configuration.advance_legacy_research),
      use_strategic_shipbuilding_preferences_(
          configuration.use_strategic_shipbuilding_preferences),
      construction_capability_(shared_query(
          std::move(configuration.construction_capability),
          CampaignConstructionCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_construction_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      shipbuilding_capability_(shared_query(
          std::move(configuration.shipbuilding_capability),
          CampaignShipbuildingCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_shipbuilding_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      strategic_(make_default_strategic_runtime(shipbuilding_capability_)),
      combat_(std::in_place_type<CombatSimulation>, std::move(raw_combat)),
      subsystems_(std::move(subsystems)) {}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    SourceCompatibleCampaignConfiguration configuration,
    CivilizationStrategicRuntimeCoordinator strategic,
    CombatCommandRuntime matched_combat, CampaignSubsystemRuntime subsystems)
    : advance_legacy_research_(configuration.advance_legacy_research),
      use_strategic_shipbuilding_preferences_(
          configuration.use_strategic_shipbuilding_preferences),
      construction_capability_(shared_query(
          std::move(configuration.construction_capability),
          CampaignConstructionCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_construction_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      shipbuilding_capability_(shared_query(
          std::move(configuration.shipbuilding_capability),
          CampaignShipbuildingCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_shipbuilding_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      strategic_(std::move(strategic)),
      combat_(std::in_place_type<CombatCommandRuntime>,
              std::move(matched_combat)),
      subsystems_(std::move(subsystems)) {}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    SourceCompatibleCampaignConfiguration configuration,
    CivilizationStrategicRuntimeCoordinator strategic,
    CombatSimulation raw_combat, CampaignSubsystemRuntime subsystems)
    : advance_legacy_research_(configuration.advance_legacy_research),
      use_strategic_shipbuilding_preferences_(
          configuration.use_strategic_shipbuilding_preferences),
      construction_capability_(shared_query(
          std::move(configuration.construction_capability),
          CampaignConstructionCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_construction_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      shipbuilding_capability_(shared_query(
          std::move(configuration.shipbuilding_capability),
          CampaignShipbuildingCapabilityQuery{
              [](std::span<const TechnologyState> technologies,
                 int civilization_id, std::string_view capability_id) {
                return prototype_shipbuilding_has_capability(
                    technologies, civilization_id, capability_id);
              }})),
      strategic_(std::move(strategic)),
      combat_(std::in_place_type<CombatSimulation>, std::move(raw_combat)),
      subsystems_(std::move(subsystems)) {}

SimulationStepResult GalaxySimulationStepCoordinator::advance(
    CampaignSimulationState *state, double simulation_days) {
  if (!state)
    throw std::invalid_argument(
        "Value cannot be null. (Parameter 'galaxy')");
  if (!std::isfinite(simulation_days) || simulation_days < 0.0)
    throw std::out_of_range(
        "Simulation time must be finite and non-negative. (Parameter "
        "'simulationDays')");
  if (simulation_days <= 0.0)
    return {};

  auto &campaign = state->campaign();
  std::vector<IndustryReserve> existing_reserves;
  existing_reserves.reserve(campaign.economies.size());
  for (const auto &economy : campaign.economies) {
    if (std::any_of(existing_reserves.begin(), existing_reserves.end(),
                    [&](const auto &reserve) {
                      return reserve.civilization_id ==
                             economy.civilization_id;
                    }))
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " +
          std::to_string(economy.civilization_id));
    existing_reserves.push_back(
        {economy.civilization_id, economy.industry});
  }

  auto economic_construction =
      economic_construction_projection(campaign.construction);
  auto economic_fleets = economic_fleet_projection(campaign.fleets);
  advance_colony_economies(
      economy_world(campaign, economic_construction, economic_fleets),
      campaign.colonies, campaign.economies, simulation_days,
      advance_legacy_research_);

  (void)strategic_.advance(
      {campaign.seed, strategic_input_world(campaign, state->lanes())},
      simulation_days);

  ensure_automatic_construction_orders(
      construction_world(campaign, construction_capability_));
  ensure_automatic_ship_orders(shipbuilding_world(
      campaign, shipbuilding_capability_, strategic_,
      use_strategic_shipbuilding_preferences_));

  std::vector<ConstructionIndustryBudget> construction_budgets;
  std::vector<ConstructionIndustryBudget> shipbuilding_budgets;
  SimulationStepResult result;
  result.simulation_days = simulation_days;
  for (const auto &civilization : campaign.civilizations) {
    if (civilization.is_seeded_ancient)
      continue;
    const auto economy =
        std::find_if(campaign.economies.begin(), campaign.economies.end(),
                     [&](const auto &candidate) {
                       return candidate.civilization_id == civilization.id;
                     });
    if (economy == campaign.economies.end())
      throw std::runtime_error("Sequence contains no matching element");
    const auto construction =
        construction_world(campaign, construction_capability_);
    const auto shipbuilding = shipbuilding_world(
        campaign, shipbuilding_capability_, strategic_,
        use_strategic_shipbuilding_preferences_);
    const IndustryAllocationContext context{
        civilization.id,
        std::max(economy->industry, 0.0),
        construction_industry_demand(construction.read(), civilization.id,
                                     simulation_days),
        shipbuilding_industry_demand(shipbuilding.read(), civilization.id,
                                     simulation_days)};
    validate_allocation_value(context.available_industry,
                              "AvailableIndustry");
    validate_allocation_value(context.construction_demand,
                              "ConstructionDemand");
    validate_allocation_value(context.shipbuilding_demand,
                              "ShipbuildingDemand");
    const auto weights = campaign_industry_weights(
        campaign.economies, civilization.id,
        strategic_.get_industry_weights(civilization.id));
    auto allocation = allocate_industry(context, weights);
    upsert_budget(construction_budgets, civilization.id,
                  allocation.construction_allocated);
    upsert_budget(shipbuilding_budgets, civilization.id,
                  allocation.shipbuilding_allocated);
    result.industry_allocations.push_back(std::move(allocation));
  }

  result.construction_events = advance_construction(
      construction_world(campaign, construction_capability_),
      std::span<const ConstructionIndustryBudget>{construction_budgets},
      simulation_days);
  result.shipbuilding_events = advance_shipbuilding(
      shipbuilding_world(campaign, shipbuilding_capability_, strategic_,
                         use_strategic_shipbuilding_preferences_),
      std::span<const ConstructionIndustryBudget>{shipbuilding_budgets},
      simulation_days);

  if (advance_legacy_research_)
    result.research_events = subsystems_.research.advance(
        {campaign.civilizations, campaign.technologies, campaign.construction,
         campaign.economies});

  result.exploration_events = subsystems_.exploration.advance(
      {campaign.systems, campaign.bodies, campaign.civilizations,
       campaign.fleets, campaign.colonies, campaign.economies,
       campaign.knowledge, state->lanes()},
      simulation_days);
  subsystems_.freight.advance(
      {campaign.systems, campaign.civilizations, campaign.bodies,
       campaign.construction, campaign.fleets, campaign.colonies,
       campaign.economies, state->lanes()},
      simulation_days);
  result.combat_events = simulation(combat_).advance(
      {campaign.systems, campaign.fleets}, simulation_days);
  result.colonization_events = subsystems_.colonization.advance(
      {campaign.systems, campaign.bodies, campaign.civilizations,
       campaign.colonies, campaign.fleets, campaign.economies,
       campaign.knowledge, state->lanes()},
      simulation_days);

  economic_construction =
      economic_construction_projection(campaign.construction);
  economic_fleets = economic_fleet_projection(campaign.fleets);
  apply_industry_storage_caps(
      economy_world(campaign, economic_construction, economic_fleets),
      campaign.colonies, campaign.economies, existing_reserves);
  return result;
}

bool GalaxySimulationStepCoordinator::has_matched_combat_runtime() const
    noexcept {
  return std::holds_alternative<CombatCommandRuntime>(combat_);
}

CivilizationStrategicRuntimeCoordinator &
GalaxySimulationStepCoordinator::strategic_runtime() noexcept {
  return strategic_;
}

const CivilizationStrategicRuntimeCoordinator &
GalaxySimulationStepCoordinator::strategic_runtime() const noexcept {
  return strategic_;
}

CombatSimulation &
GalaxySimulationStepCoordinator::combat_simulation() noexcept {
  return simulation(combat_);
}

const CombatSimulation &
GalaxySimulationStepCoordinator::combat_simulation() const noexcept {
  return simulation(combat_);
}

} // namespace stellar::core
