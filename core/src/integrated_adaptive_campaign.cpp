#include <stellar/core/integrated_adaptive_campaign.hpp>

#include <stellar/core/campaign_event_history.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace stellar::core {
void validate_campaign_runtime_continuation(const CampaignRuntimeContinuation &state,
    const FreshCampaignState &world,double day){
  CivilizationStrategicRuntimeCoordinator probe;probe.restore(state.strategic);
  state.diplomacy.validate();
  if(!std::isfinite(day)||day<0||state.strategic.strategic_days>day+1e-7||
      (state.strategic.campaign_seed&&*state.strategic.campaign_seed!=world.seed)||
      state.diplomacy.last_processed_tick>DiplomacyCampaignClock::from_simulation_days(day))
    throw std::invalid_argument("Campaign continuation does not match its world or date.");
  for(const auto &plan:state.strategic.plans)
    if(std::ranges::none_of(world.civilizations,[&](const auto &c){return c.id==plan.civilization_id&&!c.is_seeded_ancient;}))
      throw std::invalid_argument("Strategic plan references an absent civilization.");
}
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
  StellarActivityScheduler stellar_activity;
  stellar::engine::EventHistory history{100000};
  bool profiling_enabled{};
  std::array<stellar::engine::PerformanceCounter,4> performance{};

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
    auto& activity_day=world.campaign().stellar_activity_day;
    // Legacy saves without a clock start activity at the saved epoch; clamp
    // pathological epochs to the clock domain instead of rejecting the load.
    if(!activity_day)
      activity_day=std::isfinite(current_day)?std::clamp(current_day,0.,1e12):1e12;
    validate_stellar_activity_clock(activity_day);
    initialize_stellar_activity(world.campaign().seed,world.campaign().systems,*activity_day);
    stellar_activity.rebuild(world.campaign().systems);
  }
};

StellarActivityScheduler& IntegratedAdaptiveCampaignRuntime::stellar_activity() noexcept {return storage_->stellar_activity;}
double IntegratedAdaptiveCampaignRuntime::stellar_activity_day() const noexcept {return storage_->world.campaign().stellar_activity_day.value_or(0.);}
std::vector<TravelingCmeLaunch> IntegratedAdaptiveCampaignRuntime::advance_stellar_activity(double simulation_hours){
  if(!std::isfinite(simulation_hours)||simulation_hours<0)throw std::invalid_argument("Invalid stellar activity frame time");
  if(simulation_hours==0)return {};
  const double day=stellar_activity_day()+simulation_hours/24.;
  validate_stellar_activity_clock(day);
  auto launches=storage_->stellar_activity.advance(storage_->world.campaign().systems,day);
  storage_->world.campaign().stellar_activity_day=day;
  return launches;
}

CampaignRuntimeContinuation IntegratedAdaptiveCampaignRuntime::continuation() const {
  return {storage_->core.strategic_runtime().snapshot(),storage_->diplomacy_runtime.schedule()};
}
void IntegratedAdaptiveCampaignRuntime::restore_continuation(
    const CampaignRuntimeContinuation &state,double day){
  validate_campaign_runtime_continuation(state,storage_->world.campaign(),day);
  storage_->core.strategic_runtime().restore(state.strategic);
  storage_->diplomacy_runtime.restore_schedule(state.diplomacy);
}
void IntegratedAdaptiveCampaignRuntime::set_profiling_enabled(bool enabled) noexcept {
  storage_->profiling_enabled=enabled;storage_->core.set_profiling_enabled(enabled);
}
void IntegratedAdaptiveCampaignRuntime::reset_performance_counters() noexcept {
  storage_->performance={};storage_->core.reset_performance_counters();
}
std::vector<CampaignPerformanceSample> IntegratedAdaptiveCampaignRuntime::performance_samples() const {
  std::vector<CampaignPerformanceSample> result;
  for(std::size_t i=0;i<GalaxySimulationStepCoordinator::phase_names.size();++i)
    result.push_back({GalaxySimulationStepCoordinator::phase_names[i],storage_->core.performance_counters()[i]});
  constexpr std::array<std::string_view,4> names{"core_total","sensor_contacts","adaptive_research","diplomacy"};
  for(std::size_t i=0;i<names.size();++i)result.push_back({names[i],storage_->performance[i]});
  return result;
}

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
stellar::engine::EventHistory &IntegratedAdaptiveCampaignRuntime::history() noexcept {
  return storage_->history;
}
const stellar::engine::EventHistory &IntegratedAdaptiveCampaignRuntime::history() const noexcept {
  return storage_->history;
}
IntegratedAdaptiveCampaignStepResult
IntegratedAdaptiveCampaignRuntime::advance(double elapsed_days,
                                            double absolute_end_day,
                                            IntegratedAdaptiveCampaignAdvanceTrace *trace) {
  IntegratedAdaptiveCampaignStepResult result;
  if (trace)
    *trace = {};
  stellar::engine::PhaseTimer timing(storage_->profiling_enabled);
  result.core = storage_->core.advance(&storage_->world, elapsed_days);
  timing.finish(storage_->performance[0]);
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
  timing.finish(storage_->performance[1]);
  result.research_events = storage_->research_simulation.advance(
      storage_->world.campaign(), storage_->research, elapsed_days,
      absolute_end_day);
  timing.finish(storage_->performance[2]);
  if (trace)
    trace->research_events = result.research_events;
  result.diplomacy = storage_->diplomacy_runtime.process(
      result.core.exploration_events, result.core.combat_events,
      absolute_end_day);
  timing.finish(storage_->performance[3]);
  if (trace)
    trace->diplomacy = result.diplomacy;
  record_step_events(storage_->history, result, absolute_end_day);
  return result;
}

} // namespace stellar::core
