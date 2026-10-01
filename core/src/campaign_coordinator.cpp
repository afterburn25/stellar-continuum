#include <stellar/core/campaign_coordinator.hpp>

#include <stellar/core/strategic_input_support.hpp>
#include <stellar/core/campaign_civilization_control.hpp>

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
          std::move(query), campaign_civilization_control(campaign)};
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
          std::move(preference), campaign_civilization_control(campaign)};
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
      combat_(std::in_place_type<CombatCommandRuntime>), subsystems_{} {
  configure_phase_tasks();
}

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
      subsystems_(std::move(subsystems)) {
  configure_phase_tasks();
}

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
      subsystems_(std::move(subsystems)) {
  configure_phase_tasks();
}

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
      subsystems_(std::move(subsystems)) {
  configure_phase_tasks();
}

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
      subsystems_(std::move(subsystems)) {
  configure_phase_tasks();
}

GalaxySimulationStepCoordinator::GalaxySimulationStepCoordinator(
    GalaxySimulationStepCoordinator &&other)
    : profiling_enabled_(other.profiling_enabled_),
      performance_(other.performance_), step_(std::move(other.step_)),
      executor_(),
      advance_legacy_research_(other.advance_legacy_research_),
      use_strategic_shipbuilding_preferences_(
          other.use_strategic_shipbuilding_preferences_),
      construction_capability_(std::move(other.construction_capability_)),
      shipbuilding_capability_(std::move(other.shipbuilding_capability_)),
      strategic_(std::move(other.strategic_)),
      combat_(std::move(other.combat_)),
      subsystems_(std::move(other.subsystems_)) {
  // Rebind phase tasks to this object, then carry the source executor's
  // scheduler state (tick/cadence bookkeeping, pending wakeups).
  const auto carried = other.executor_.capture_state();
  configure_phase_tasks();
  executor_.restore_state(carried);
}

GalaxySimulationStepCoordinator &
GalaxySimulationStepCoordinator::operator=(
    GalaxySimulationStepCoordinator &&other) {
  if (this == &other)
    return *this;
  profiling_enabled_ = other.profiling_enabled_;
  performance_ = other.performance_;
  step_ = std::move(other.step_);
  advance_legacy_research_ = other.advance_legacy_research_;
  use_strategic_shipbuilding_preferences_ =
      other.use_strategic_shipbuilding_preferences_;
  construction_capability_ = std::move(other.construction_capability_);
  shipbuilding_capability_ = std::move(other.shipbuilding_capability_);
  strategic_ = std::move(other.strategic_);
  combat_ = std::move(other.combat_);
  subsystems_ = std::move(other.subsystems_);
  const auto carried = other.executor_.capture_state();
  executor_.clear();
  configure_phase_tasks();
  executor_.restore_state(carried);
  return *this;
}

void GalaxySimulationStepCoordinator::configure_phase_tasks() {
  namespace eng = stellar::engine;
  // One task per phase, chained in the historical phase order. Phases
  // start at the Active tier; set_phase_tier() demotes without
  // restructuring advance(). Each day-integrating body scales its span
  // by ctx.elapsed_ticks so a coarse run conserves simulated time.
  const std::array<eng::SimulationTask, phase_names.size()> phases{{
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         advance_colony_economies(
             economy_world(campaign, step_.economic_construction,
                           step_.economic_fleets),
             campaign.colonies, campaign.economies, phase_days,
             advance_legacy_research_);
         timing.finish(performance_[0]);
       },
       .domain = "economy"},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         (void)strategic_.advance(
             {campaign.seed,
              strategic_input_world(campaign, step_.state->lanes()),
              campaign_civilization_control(campaign)},
             phase_days);
         timing.finish(performance_[1]);
       },
       .domain = "strategic_ai"},
      {.run = [this](const eng::SimulationTickContext &) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         ensure_automatic_construction_orders(
             construction_world(campaign, construction_capability_));
         ensure_automatic_ship_orders(shipbuilding_world(
             campaign, shipbuilding_capability_, strategic_,
             use_strategic_shipbuilding_preferences_));
         timing.finish(performance_[2]);
       },
       .domain = "automatic_orders"},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         for (const auto &civilization : campaign.civilizations) {
           if (civilization.is_seeded_ancient)
             continue;
           const auto economy = std::find_if(
               campaign.economies.begin(), campaign.economies.end(),
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
               construction_industry_demand(construction.read(),
                                            civilization.id,
                                            phase_days),
               shipbuilding_industry_demand(shipbuilding.read(),
                                            civilization.id,
                                            phase_days)};
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
           upsert_budget(step_.construction_budgets, civilization.id,
                         allocation.construction_allocated);
           upsert_budget(step_.shipbuilding_budgets, civilization.id,
                         allocation.shipbuilding_allocated);
           step_.result->industry_allocations.push_back(
               std::move(allocation));
         }
         timing.finish(performance_[3]);
       },
       .domain = "industry_allocation", .depends_on = {2}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         step_.result->construction_events = advance_construction(
             construction_world(campaign, construction_capability_),
             std::span<const ConstructionIndustryBudget>{
                 step_.construction_budgets},
             phase_days);
         timing.finish(performance_[4]);
       },
       .domain = "construction", .depends_on = {3}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         step_.result->shipbuilding_events = advance_shipbuilding(
             shipbuilding_world(campaign, shipbuilding_capability_,
                                strategic_,
                                use_strategic_shipbuilding_preferences_),
             std::span<const ConstructionIndustryBudget>{
                 step_.shipbuilding_budgets},
             phase_days);
         timing.finish(performance_[5]);
       },
       .domain = "shipbuilding", .depends_on = {3}},
      {.run = [this](const eng::SimulationTickContext &) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         if (advance_legacy_research_)
           step_.result->research_events = subsystems_.research.advance(
               {campaign.civilizations, campaign.technologies,
                campaign.construction, campaign.economies,
                campaign_civilization_control(campaign)});
         timing.finish(performance_[6]);
       },
       .domain = "legacy_research", .depends_on = {5}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         step_.result->exploration_events = subsystems_.exploration.advance(
             {campaign.systems, campaign.bodies, campaign.civilizations,
              campaign.fleets, campaign.colonies, campaign.economies,
              campaign.knowledge, step_.state->lanes(),
              campaign_civilization_control(campaign),
              campaign.generation_metadata &&
                      campaign.generation_metadata->phenomena
                  ? &*campaign.generation_metadata->phenomena
                  : nullptr},
             phase_days);
         timing.finish(performance_[7]);
       },
       .domain = "exploration", .depends_on = {6}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         subsystems_.freight.advance(
             {campaign.systems, campaign.civilizations, campaign.bodies,
              campaign.construction, campaign.fleets, campaign.colonies,
              campaign.economies, step_.state->lanes()},
             phase_days);
         timing.finish(performance_[8]);
       },
       .domain = "freight", .depends_on = {7}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         step_.result->combat_events = simulation(combat_).advance(
             {campaign.systems, campaign.fleets}, phase_days);
         timing.finish(performance_[9]);
       },
       .domain = "combat", .depends_on = {8}},
      {.run = [this](const eng::SimulationTickContext &ctx) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         const double phase_days =
             step_.simulation_days * static_cast<double>(ctx.elapsed_ticks);
         step_.result->colonization_events = subsystems_.colonization.advance(
             {campaign.systems, campaign.bodies, campaign.civilizations,
              campaign.colonies, campaign.fleets, campaign.economies,
              campaign.knowledge, step_.state->lanes(),
              campaign_civilization_control(campaign)},
             phase_days);
         timing.finish(performance_[10]);
       },
       .domain = "colonization", .depends_on = {9}},
      {.run = [this](const eng::SimulationTickContext &) {
         eng::PhaseTimer timing(profiling_enabled_);
         auto &campaign = step_.state->campaign();
         step_.economic_construction =
             economic_construction_projection(campaign.construction);
         step_.economic_fleets =
             economic_fleet_projection(campaign.fleets);
         apply_industry_storage_caps(
             economy_world(campaign, step_.economic_construction,
                           step_.economic_fleets),
             campaign.colonies, campaign.economies,
             step_.existing_reserves);
         timing.finish(performance_[11]);
       },
       .domain = "economy_storage", .depends_on = {10}},
  }};
  for (std::size_t i = 0; i < phase_names.size(); ++i) {
    auto task = phases[i];
    task.tier = eng::SimulationTier::Active;
    if (i > 0 && task.depends_on.empty())
      task.depends_on = {static_cast<eng::SimulationExecutor::Key>(i - 1)};
    executor_.add(static_cast<eng::SimulationExecutor::Key>(i),
                  std::move(task));
  }
}

std::optional<std::size_t>
GalaxySimulationStepCoordinator::phase_index(std::string_view phase) noexcept {
  for (std::size_t i = 0; i < phase_names.size(); ++i)
    if (phase_names[i] == phase) return i;
  return std::nullopt;
}

void GalaxySimulationStepCoordinator::set_phase_tier(
    std::string_view phase, stellar::engine::SimulationTier tier) {
  const auto index = phase_index(phase);
  if (!index)
    throw std::invalid_argument("Unknown coordinator phase: " +
                                std::string(phase));
  executor_.set_tier(
      static_cast<stellar::engine::SimulationExecutor::Key>(*index), tier);
}

stellar::engine::SimulationTier
GalaxySimulationStepCoordinator::phase_tier(std::string_view phase) const {
  const auto index = phase_index(phase);
  if (!index)
    throw std::invalid_argument("Unknown coordinator phase: " +
                                std::string(phase));
  return executor_.tier(
      static_cast<stellar::engine::SimulationExecutor::Key>(*index));
}

void GalaxySimulationStepCoordinator::wake_phase(std::string_view phase) {
  const auto index = phase_index(phase);
  if (!index)
    throw std::invalid_argument("Unknown coordinator phase: " +
                                std::string(phase));
  executor_.wake(
      static_cast<stellar::engine::SimulationExecutor::Key>(*index));
}

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
  step_.state = state;
  step_.simulation_days = simulation_days;
  step_.construction_budgets.clear();
  step_.shipbuilding_budgets.clear();

  step_.existing_reserves.clear();
  step_.existing_reserves.reserve(campaign.economies.size());
  for (const auto &economy : campaign.economies) {
    if (std::any_of(step_.existing_reserves.begin(),
                    step_.existing_reserves.end(),
                    [&](const auto &reserve) {
                      return reserve.civilization_id ==
                             economy.civilization_id;
                    }))
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " +
          std::to_string(economy.civilization_id));
    step_.existing_reserves.push_back(
        {economy.civilization_id, economy.industry});
  }

  step_.economic_construction =
      economic_construction_projection(campaign.construction);
  step_.economic_fleets = economic_fleet_projection(campaign.fleets);

  SimulationStepResult result;
  result.simulation_days = simulation_days;
  step_.result = &result;
  try {
    executor_.advance();
  } catch (...) {
    // An aborted step consumed this tick: phases that ran integrated
    // their span, and the retry is a new logical step — un-run phases
    // must not double-integrate the aborted span on top of it.
    executor_.consume_tick();
    throw;
  }
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
