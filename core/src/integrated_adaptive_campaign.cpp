#include <stellar/core/integrated_adaptive_campaign.hpp>

#include <algorithm>
#include <utility>

namespace stellar::core {
namespace {
SourceCompatibleCampaignConfiguration configuration(
    AdaptiveResearchConstructionCapabilityView *construction,
    AdaptiveResearchShipbuildingCapabilityView *shipbuilding) {
  SourceCompatibleCampaignConfiguration result;
  result.advance_legacy_research = false;
  result.use_strategic_shipbuilding_preferences = true;
  result.construction_capability =
      [construction](std::span<const TechnologyState>, int civilization,
                     std::string_view capability) {
        return construction->has_civilization_capability(civilization,
                                                          capability);
      };
  result.shipbuilding_capability =
      [shipbuilding](std::span<const TechnologyState>, int civilization,
                     std::string_view capability) {
        return shipbuilding->has_civilization_capability(civilization,
                                                         capability);
      };
  return result;
}

CivilizationStrategicRuntimeCoordinator strategic_runtime(
    AdaptiveResearchShipbuildingCapabilityView *shipbuilding,
    DiplomacyState *diplomacy) {
  StrategicShipbuildingCapabilityQuery capability =
      [shipbuilding](const StrategicInputWorldView &, int civilization,
                     std::string_view id) {
        return shipbuilding->has_civilization_capability(civilization, id);
      };
  CivilizationStrategicDirector director(
      CivilizationStrategicInputBuilder({}, std::move(capability), {}));
  StrategicKnowledgeQuery knowledge = [diplomacy](int observer,
                                                   std::int64_t tick) {
    return DiplomacyStrategicKnowledgeProvider(*diplomacy).build(observer,
                                                                 tick);
  };
  return CivilizationStrategicRuntimeCoordinator(std::move(director),
                                                   std::move(knowledge));
}
} // namespace

struct IntegratedAdaptiveCampaignRuntime::Storage {
  AdaptiveResearchStrategicRuntime research_runtime;
  CampaignSimulationState world;
  DiplomacyState diplomacy;
  AdaptiveResearchCampaignState research;
  AdaptiveResearchConstructionCapabilityView construction;
  AdaptiveResearchShipbuildingCapabilityView shipbuilding;
  DiplomacyCampaignRuntimeCoordinator diplomacy_runtime;
  GalaxySimulationStepCoordinator core;
  AdaptiveResearchCampaignSimulation research_simulation;

  Storage(AdaptiveResearchStrategicRuntime runtime, FreshCampaignState campaign,
          DiplomacyState diplomacy_state,
          const AdaptiveResearchCampaignSnapshot *snapshot,
          double current_day)
      : research_runtime(std::move(runtime)), world(std::move(campaign)),
        diplomacy(std::move(diplomacy_state)),
        research(snapshot
                     ? AdaptiveResearchCampaignSnapshotCodec(research_runtime)
                           .restore(world.campaign(), *snapshot)
                     : AdaptiveResearchCampaignFactory(research_runtime)
                           .create(world.campaign())),
        construction(research), shipbuilding(research),
        diplomacy_runtime(diplomacy),
        core(configuration(&construction, &shipbuilding),
             strategic_runtime(&shipbuilding, &diplomacy),
             diplomacy_runtime.create_combat_command_runtime()) {
    diplomacy_runtime.reset(current_day, true);
  }
};

IntegratedAdaptiveCampaignRuntime::IntegratedAdaptiveCampaignRuntime(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}

IntegratedAdaptiveCampaignRuntime IntegratedAdaptiveCampaignRuntime::create_fresh(
    AdaptiveResearchStrategicRuntime research_runtime, FreshCampaignState world,
    DiplomacyState diplomacy, double current_simulation_day) {
  return IntegratedAdaptiveCampaignRuntime(std::make_unique<Storage>(
      std::move(research_runtime), std::move(world), std::move(diplomacy),
      nullptr, current_simulation_day));
}

IntegratedAdaptiveCampaignRuntime
IntegratedAdaptiveCampaignRuntime::restore_research(
    AdaptiveResearchStrategicRuntime research_runtime, FreshCampaignState world,
    const AdaptiveResearchCampaignSnapshot &research_snapshot,
    DiplomacyState diplomacy, double current_simulation_day) {
  return IntegratedAdaptiveCampaignRuntime(std::make_unique<Storage>(
      std::move(research_runtime), std::move(world), std::move(diplomacy),
      &research_snapshot, current_simulation_day));
}

IntegratedAdaptiveCampaignRuntime::~IntegratedAdaptiveCampaignRuntime() = default;
IntegratedAdaptiveCampaignRuntime::IntegratedAdaptiveCampaignRuntime(
    IntegratedAdaptiveCampaignRuntime &&) noexcept = default;
IntegratedAdaptiveCampaignRuntime &
IntegratedAdaptiveCampaignRuntime::operator=(
    IntegratedAdaptiveCampaignRuntime &&) noexcept = default;

CampaignSimulationState &IntegratedAdaptiveCampaignRuntime::world() noexcept {
  return storage_->world;
}
const CampaignSimulationState &
IntegratedAdaptiveCampaignRuntime::world() const noexcept {
  return storage_->world;
}
AdaptiveResearchCampaignState &
IntegratedAdaptiveCampaignRuntime::research() noexcept {
  return storage_->research;
}
const AdaptiveResearchCampaignState &
IntegratedAdaptiveCampaignRuntime::research() const noexcept {
  return storage_->research;
}
DiplomacyState &IntegratedAdaptiveCampaignRuntime::diplomacy() noexcept {
  return storage_->diplomacy;
}
const DiplomacyState &
IntegratedAdaptiveCampaignRuntime::diplomacy() const noexcept {
  return storage_->diplomacy;
}
const AdaptiveResearchStrategicRuntime &
IntegratedAdaptiveCampaignRuntime::research_runtime() const noexcept {
  return storage_->research_runtime;
}
GalaxySimulationStepCoordinator &
IntegratedAdaptiveCampaignRuntime::core() noexcept {
  return storage_->core;
}
DiplomacyCampaignRuntimeCoordinator &
IntegratedAdaptiveCampaignRuntime::diplomacy_runtime() noexcept {
  return storage_->diplomacy_runtime;
}
std::span<const FleetPowerObservation>
IntegratedAdaptiveCampaignRuntime::combat_intelligence() const noexcept {
  return storage_->world.campaign().combat_intelligence;
}
IntegratedAdaptiveCampaignStepResult
IntegratedAdaptiveCampaignRuntime::advance(double elapsed_days,
                                            double absolute_end_day,
                                            IntegratedAdaptiveCampaignAdvanceTrace *trace) {
  IntegratedAdaptiveCampaignStepResult result;
  if (trace)
    *trace = {};
  result.core = storage_->core.advance(&storage_->world, elapsed_days);
  if (trace)
    trace->core = result.core;
  if (elapsed_days > 0.0) {
    auto &campaign = storage_->world.campaign();
    std::vector<const Civilization *> civilizations;
    civilizations.reserve(campaign.civilizations.size());
    for (const auto &civilization : campaign.civilizations)
      civilizations.push_back(&civilization);
    std::stable_sort(civilizations.begin(), civilizations.end(),
                     [](const Civilization *left,
                        const Civilization *right) {
                       return left->id < right->id;
                     });
    for (const auto *civilization : civilizations) {
      const auto *research =
          storage_->research.try_get_civilization(civilization->id);
      const int recorded = record_fleet_sensor_contacts(
          {campaign.civilizations, campaign.fleets,
           campaign.combat_intelligence},
          civilization->id, absolute_end_day, has_combat_scanner(research));
      result.sensor_contacts.push_back({civilization->id, recorded});
      if (trace)
        trace->sensor_contacts.push_back({civilization->id, recorded});
    }
  }
  result.research_events = storage_->research_simulation.advance(
      storage_->world.campaign(), storage_->research, elapsed_days,
      absolute_end_day);
  if (trace)
    trace->research_events = result.research_events;
  result.diplomacy = storage_->diplomacy_runtime.process(
      result.core.exploration_events, result.core.combat_events,
      absolute_end_day);
  if (trace)
    trace->diplomacy = result.diplomacy;
  return result;
}

} // namespace stellar::core
