#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/detail/diplomacy_state_access.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {
std::string range_message(std::string_view parameter) {
  return "Specified argument was out of the range of valid values. (Parameter '" +
         std::string(parameter) + "')";
}

} // namespace

ExplorationDiplomacyBridge::ExplorationDiplomacyBridge(
    DiplomacySimulation &simulation) noexcept
    : simulation_(&simulation) {}

int ExplorationDiplomacyBridge::process(
    std::span<const ExplorationEvent> events, std::int64_t observed_at_tick) {
  if (observed_at_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("observedAtTick"));
  int processed = 0;
  for (const auto &event : events) {
    if (event.type != ExplorationEventType::FirstContact)
      continue;
    if (!event.target_civilization_id)
      throw DiplomacyOperationError(
          "Exploration FirstContact events must carry the legitimately "
          "identified target civilization ID.");
    const int target = *event.target_civilization_id;
    (void)simulation_->process_contact_opportunity(FirstContactOpportunity{
        event.civilization_id, "civilization:" + std::to_string(target), target,
        observed_at_tick, event.system_id,
        ContactAwareness::contact_established, ContactCondition::active, false,
        1.0});
    ++processed;
  }
  return processed;
}

CombatDiplomacyBridge::CombatDiplomacyBridge(DiplomacyState &state) noexcept
    : state_(&state), simulation_(state) {}

int CombatDiplomacyBridge::process(std::span<const CombatEvent> events,
                                    std::int64_t tick) {
  if (tick < 0)
    throw DiplomacyArgumentRangeError(range_message("tick"));
  int processed = 0;
  for (const auto &event : events) {
    if (!event.target_civilization_id ||
        *event.target_civilization_id == event.actor_civilization_id)
      continue;
    const int target = *event.target_civilization_id;
    if (!detail::DiplomacyStateAccess::has_identified(
            *state_, target, event.actor_civilization_id))
      continue;
    const std::string system =
        event.system_id ? std::to_string(*event.system_id) : "unknown";
    if (event.type == CombatEventType::EngagementStarted) {
      simulation_.set_hostile(
          target, event.actor_civilization_id, tick,
          "Hostile military engagement in system " + system + ": " +
              event.message);
      ++processed;
    } else if (event.type == CombatEventType::FleetDestroyed) {
      simulation_.apply_relationship_impact(
          target, event.actor_civilization_id,
          RelationshipImpact{0, 0, 0, 0, 0, 1,
                             "Military vessel destroyed in system " + system +
                                 ": " + event.message},
          tick);
      ++processed;
    }
  }
  return processed;
}

DiplomacyCombatHostilityView::DiplomacyCombatHostilityView(
    const DiplomacyState &state) noexcept
    : state_(&state) {}

bool DiplomacyCombatHostilityView::are_hostile(int first, int second) const {
  if (first == second)
    return false;
  const auto relationship = state_->get_relationship(first, second);
  return relationship &&
         (relationship->political_state == DiplomaticPoliticalState::hostile ||
          relationship->political_state == DiplomaticPoliticalState::at_war);
}

CombatHostilityView
DiplomacyCombatHostilityView::combat_hostility_view() const {
  const auto *state = state_;
  return [state](int first, int second) {
    if (first == second)
      return false;
    const auto relationship = state->get_relationship(first, second);
    return relationship &&
           (relationship->political_state ==
                DiplomaticPoliticalState::hostile ||
            relationship->political_state == DiplomaticPoliticalState::at_war);
  };
}

DiplomacyStrategicKnowledgeProvider::DiplomacyStrategicKnowledgeProvider(
    const DiplomacyState &state) noexcept
    : state_(&state) {}

StrategicKnowledgeSnapshot
DiplomacyStrategicKnowledgeProvider::build(int observer,
                                            std::int64_t now_tick) const {
  if (observer < 0)
    throw DiplomacyArgumentRangeError(range_message("observerCivilizationId"));
  if (now_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("nowTick"));
  const auto view = state_->build_view_for(observer);
  std::unordered_map<int, const DiplomaticRelationshipView *> relationships;
  for (const auto &relationship : view.relationships)
    relationships.emplace(relationship.other_civilization_id, &relationship);
  std::vector<int> targets;
  std::unordered_set<int> seen;
  for (const auto &contact : view.contacts) {
    if (contact.target_civilization_id &&
        contact.awareness >= ContactAwareness::identified &&
        *contact.target_civilization_id != observer &&
        seen.insert(*contact.target_civilization_id).second)
      targets.push_back(*contact.target_civilization_id);
  }
  std::ranges::sort(targets);
  StrategicKnowledgeSnapshot result{now_tick, {}};
  result.civilizations.reserve(targets.size());
  for (const int target : targets) {
    const auto found = relationships.find(target);
    const auto *relationship =
        found == relationships.end() ? nullptr : found->second;
    const double trust = relationship
                             ? std::clamp(relationship->trust -
                                              relationship->hostility,
                                          -1.0, 1.0)
                             : 0.0;
    const bool at_war = relationship &&
                        relationship->political_state ==
                            DiplomaticPoliticalState::at_war;
    result.civilizations.push_back(
        {target,
         KnownCivilization{target, trust, 0, 0, 0, 0, false, 0, 0, at_war,
                           false, false}});
  }
  return result;
}

int DiplomacyCampaignRuntimeStepResult::maintenance_transitions() const noexcept {
  return maintenance.contact_aging.newly_stale_contacts +
         maintenance.proposal_lifecycle.newly_expired_proposals;
}
int DiplomacyCampaignRuntimeStepResult::processed_diplomacy_events() const noexcept {
  return first_contact_events_processed + combat_incidents_processed;
}

struct DiplomacyCampaignRuntimeCoordinator::Storage {
  DiplomacyState *state;
  DiplomacySimulation simulation;
  ExplorationDiplomacyBridge exploration_bridge;
  CombatDiplomacyBridge combat_bridge;
  DiplomacyCampaignMaintenanceScheduler maintenance;
  ObserverDiplomacyCommandService commands;
  DiplomacyCombatHostilityView hostility;
  std::int64_t last_processed_tick{-1};

  Storage(DiplomacyState &value,
          std::optional<DiplomacyCampaignMaintenancePolicy> policy)
      : state(&value), simulation(value), exploration_bridge(simulation),
        combat_bridge(value), maintenance(value, policy), commands(value),
        hostility(value) {}
};

DiplomacyCampaignRuntimeCoordinator::DiplomacyCampaignRuntimeCoordinator(
    DiplomacyState &state,
    std::optional<DiplomacyCampaignMaintenancePolicy> maintenance_policy)
    : storage_(std::make_unique<Storage>(state, maintenance_policy)) {}
DiplomacyCampaignRuntimeCoordinator::~DiplomacyCampaignRuntimeCoordinator() = default;
DiplomacyCampaignRuntimeCoordinator::DiplomacyCampaignRuntimeCoordinator(
    DiplomacyCampaignRuntimeCoordinator &&) noexcept = default;
DiplomacyCampaignRuntimeCoordinator &
DiplomacyCampaignRuntimeCoordinator::operator=(
    DiplomacyCampaignRuntimeCoordinator &&) noexcept = default;
DiplomacyState &DiplomacyCampaignRuntimeCoordinator::state() noexcept {
  return *storage_->state;
}
const DiplomacyState &DiplomacyCampaignRuntimeCoordinator::state() const noexcept {
  return *storage_->state;
}
ObserverDiplomacyCommandService &
DiplomacyCampaignRuntimeCoordinator::commands() noexcept {
  return storage_->commands;
}
const ObserverDiplomacyCommandService &
DiplomacyCampaignRuntimeCoordinator::commands() const noexcept {
  return storage_->commands;
}
const DiplomacyCombatHostilityView &
DiplomacyCampaignRuntimeCoordinator::hostility_view() const noexcept {
  return storage_->hostility;
}
std::int64_t DiplomacyCampaignRuntimeCoordinator::last_processed_tick() const noexcept {
  return storage_->last_processed_tick;
}
std::int64_t
DiplomacyCampaignRuntimeCoordinator::next_maintenance_review_tick() const noexcept {
  return storage_->maintenance.next_review_tick();
}
CombatCommandRuntime
DiplomacyCampaignRuntimeCoordinator::create_combat_command_runtime() const {
  return CombatCommandRuntime(storage_->hostility.combat_hostility_view());
}
CombatSimulation
DiplomacyCampaignRuntimeCoordinator::create_combat_simulation() const {
  return CombatSimulation(storage_->hostility.combat_hostility_view());
}
DiplomaticStateView
DiplomacyCampaignRuntimeCoordinator::build_view(int observer) const {
  return storage_->commands.build_view(observer);
}
void DiplomacyCampaignRuntimeCoordinator::reset(double simulation_days,
                                                 bool review_immediately) {
  const auto tick = DiplomacyCampaignClock::from_simulation_days(simulation_days);
  storage_->maintenance.reset(tick, review_immediately);
  storage_->last_processed_tick = tick;
}
DiplomacyCampaignRuntimeStepResult DiplomacyCampaignRuntimeCoordinator::process(
    std::span<const ExplorationEvent> exploration_events,
    std::span<const CombatEvent> combat_events, double simulation_days) {
  const auto tick = DiplomacyCampaignClock::from_simulation_days(simulation_days);
  if (storage_->last_processed_tick >= 0 &&
      tick < storage_->last_processed_tick)
    throw DiplomacyOperationError(
        "Campaign Diplomacy runtime cannot process an earlier tick after later "
        "diplomatic history has been applied.");
  const int first = exploration_events.empty()
                        ? 0
                        : storage_->exploration_bridge.process(
                              exploration_events, tick);
  const int combat = combat_events.empty()
                         ? 0
                         : storage_->combat_bridge.process(combat_events, tick);
  auto maintenance = storage_->maintenance.review_if_due(tick);
  storage_->last_processed_tick = tick;
  return {tick, first, combat, std::move(maintenance)};
}

} // namespace stellar::core
