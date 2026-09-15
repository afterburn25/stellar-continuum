#include "native_voice_bridge.hpp"

#include "native_campaign_calendar.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/legacy_technology.hpp>
#include <stellar/core/logistics.hpp>
#include <stellar/core/own_combat_fleet_status.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/strategic_input_support.hpp>

#include <algorithm>
#include <ranges>
#include <sstream>
#include <unordered_set>

namespace stellar::native_voice {
namespace {

using Clock = std::chrono::system_clock;

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

[[nodiscard]] std::string format_amount(double value) {
  std::ostringstream out;
  out.precision(3);
  out << std::fixed << value;
  auto text = out.str();
  while (text.size() > 1 && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  return text;
}

// The adaptive-research capabilities the reference promotes to a major
// breakthrough cue.
[[nodiscard]] bool is_major_node(const core::AdaptiveResearchNodeDefinition &node) {
  static const std::unordered_set<std::string_view> major{
      "ftl_prototype",
      "experimental_interstellar_transit",
      "reliable_ftl",
      "interstellar_transit",
      "extended_ftl_range",
      "extended_interstellar_transit",
      "infrastructure_ftl_transit",
      "interstellar_gateway_network",
  };
  return std::ranges::any_of(node.declared_capabilities, [](const auto &id) {
    return major.contains(id);
  });
}

} // namespace

NativeGameplayVoiceBridge::NativeGameplayVoiceBridge(NativeVoiceRouter &router)
    : router_(router) {}

void NativeGameplayVoiceBridge::reset(
    const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  fleet_transit_.clear();
  seen_proposals_.clear();
  seen_diplomacy_.clear();
  critical_hulls_.clear();
  critical_logistics_.clear();
  mature_research_.clear();
  baseline_ = false;
  opening_ = true;
  has_interstellar_launch_ = has_extrasolar_arrival_ = has_war_ =
      has_first_contact_ = false;
  seed_baselines(runtime);
}

void NativeGameplayVoiceBridge::seed_baselines(
    const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto &campaign = runtime.world().campaign();
  const auto player_id = campaign.player_civilization_id;
  const auto player = std::ranges::find(campaign.civilizations, player_id,
                                        &core::Civilization::id);
  if (player == campaign.civilizations.end()) return;
  const auto home = player->home_system_id;
  const auto known_systems = campaign.knowledge.known_systems(player_id);
  const auto surveyed_elsewhere =
      std::ranges::any_of(known_systems, [&](int id) { return id != home; });
  has_interstellar_launch_ =
      std::ranges::any_of(campaign.fleets, [&](const auto &fleet) {
        return fleet.civilization_id == player_id &&
               ((fleet.current_system_id && *fleet.current_system_id != home) ||
                (fleet.destination_system_id &&
                 *fleet.destination_system_id != home));
      }) ||
      std::ranges::any_of(campaign.colonies, [&](const auto &colony) {
        return colony.civilization_id == player_id && colony.system_id != home;
      }) ||
      surveyed_elsewhere;
  has_extrasolar_arrival_ =
      std::ranges::any_of(campaign.fleets, [&](const auto &fleet) {
        return fleet.civilization_id == player_id && fleet.current_system_id &&
               *fleet.current_system_id != home;
      }) ||
      std::ranges::any_of(campaign.colonies, [&](const auto &colony) {
        return colony.civilization_id == player_id && colony.system_id != home;
      }) ||
      surveyed_elsewhere;
  has_first_contact_ = std::ranges::any_of(
      campaign.civilizations, [&](const auto &civilization) {
        return civilization.id != player_id &&
               campaign.knowledge.is_civilization_known(player_id,
                                                        civilization.id);
      });
  const auto view = runtime.diplomacy().build_view_for(player_id);
  has_war_ = std::ranges::any_of(view.recent_events, [&](const auto &event) {
    return event.kind == core::DiplomaticEventKind::war_declared &&
           (event.primary_civilization_id == player_id ||
            event.secondary_civilization_id == player_id);
  });
  mature_research_.clear();
  if (const auto *state = runtime.research().try_get_civilization(player_id))
    for (const auto &node : state->node_states())
      if (node.counts_as_established_knowledge())
        mature_research_.insert(node.node_id);
}

void NativeGameplayVoiceBridge::observe_opening(
    core::IntegratedAdaptiveCampaignRuntime &runtime, double simulation_days,
    VoiceFrequency frequency) {
  if (!opening_) return;
  opening_ = false;
  const auto &campaign = runtime.world().campaign();
  const auto player_id = campaign.player_civilization_id;
  const Scope scope{player_id, frequency,
                    core::DiplomacyCampaignClock::from_simulation_days(
                        simulation_days),
                    native_campaign::format_campaign_date(simulation_days)};
  NativeGameplayVoiceEvent event;
  event.event_key = "opening";
  event.source_civilization_id = player_id;
  event.unique_event_id = "opening:" +
                          std::to_string(campaign.seed);
  event.variables["detail"] = "";
  event.simulation_tick = scope.tick;
  event.simulation_date = scope.date;
  event.source_species_id = species_of(runtime, player_id, "");
  event.first_occurrence = true;
  router_.emit(event, {player_id, frequency, false});
}

void NativeGameplayVoiceBridge::observe(
    const core::CampaignFrameResult &result,
    core::IntegratedAdaptiveCampaignRuntime &runtime, double simulation_days,
    VoiceFrequency frequency) {
  const auto &campaign = runtime.world().campaign();
  const Scope scope{campaign.player_civilization_id, frequency,
                    core::DiplomacyCampaignClock::from_simulation_days(
                        simulation_days),
                    native_campaign::format_campaign_date(simulation_days)};
  route_events(scope, runtime, result);
  observe_fleets(scope, runtime);
  observe_hull(scope, runtime);
  observe_diplomacy(scope, runtime);
  observe_logistics(scope, runtime);
  if (baseline_) observe_economy(scope, runtime);
  baseline_ = true;
}

std::string NativeGameplayVoiceBridge::species_of(
    const core::IntegratedAdaptiveCampaignRuntime &runtime, int civilization_id,
    std::string_view fallback) const {
  const auto &civilizations = runtime.world().campaign().civilizations;
  const auto found = std::ranges::find(civilizations, civilization_id,
                                       &core::Civilization::id);
  return found == civilizations.end() ? std::string(fallback)
                                      : found->species_id;
}

std::string NativeGameplayVoiceBridge::public_system_name(
    const core::IntegratedAdaptiveCampaignRuntime &runtime,
    int system_id) const {
  const auto &systems = runtime.world().campaign().systems;
  const auto found = std::ranges::find(systems, system_id,
                                       &core::StellarSystem::id);
  return found == systems.end() ? "Unknown system" : found->name;
}

bool NativeGameplayVoiceBridge::emit_owned(
    const Scope &scope, std::string_view key, int civ,
    std::string_view species, std::string_view identity,
    std::map<std::string, std::string> variables, bool first,
    std::optional<NativeVoiceOverrides> overrides) {
  if (civ != scope.player_id) return false;
  NativeGameplayVoiceEvent event;
  event.event_key = std::string(key);
  event.source_civilization_id = civ;
  event.unique_event_id =
      std::string(key) + ":civ:" + std::to_string(civ) + ":" +
      std::string(identity) + ":tick:" + std::to_string(scope.tick);
  event.variables = std::move(variables);
  event.simulation_tick = scope.tick;
  event.simulation_date = scope.date;
  event.source_species_id = std::string(species);
  event.audience = VoiceAudience::OwnCivilization;
  event.first_occurrence = first;
  event.overrides = std::move(overrides);
  return router_.emit(event, {scope.player_id, scope.frequency, false});
}

bool NativeGameplayVoiceBridge::emit_direct(
    const Scope &scope, std::string_view key, int civ,
    std::string_view species, int recipient, std::string_view identity,
    std::map<std::string, std::string> variables) {
  NativeGameplayVoiceEvent event;
  event.event_key = std::string(key);
  event.source_civilization_id = civ;
  event.unique_event_id =
      std::string(key) + ":civ:" + std::to_string(civ) + ":" +
      std::string(identity) + ":tick:" + std::to_string(scope.tick);
  event.variables = std::move(variables);
  event.simulation_tick = scope.tick;
  event.simulation_date = scope.date;
  event.source_species_id = std::string(species);
  event.audience = VoiceAudience::DirectCommunication;
  event.recipient_civilization_id = recipient;
  return router_.emit(event, {scope.player_id, scope.frequency, false});
}

void NativeGameplayVoiceBridge::route_events(
    const Scope &scope, const core::IntegratedAdaptiveCampaignRuntime &runtime,
    const core::CampaignFrameResult &result) {
  const auto &campaign = runtime.world().campaign();
  const auto player_id = scope.player_id;
  const auto species = species_of(runtime, player_id, "");
  const auto own_fleet_name = [&](int fleet_id) -> std::string {
    const auto found = std::ranges::find(campaign.fleets, fleet_id,
                                         &core::FleetState::id);
    return found != campaign.fleets.end() &&
                   found->civilization_id == player_id
               ? found->name
               : "";
  };
  for (const auto &step : result.strategic_results) {
    // Adaptive research outcomes — nodes that newly count as established
    // knowledge announce once, mirroring RouteAdaptiveResearchVoice.
    for (const auto &event : step.research_events) {
      if (event.civilization_id != player_id || !event.is_outcome) continue;
      if (!mature_research_.insert(event.node_id).second) continue;
      const auto *state = runtime.research().try_get_civilization(player_id);
      const auto *node =
          state ? runtime.research()
                          .runtime()
                          .authority()
                          .catalog()
                          .find_node(event.node_id)
                : nullptr;
      emit_owned(scope,
                 node && is_major_node(*node)
                     ? "research.breakthrough.major"
                     : "research.completed",
                 player_id, species,
                 "adaptive-research:" + event.node_id,
                 {{"research_name",
                   node ? node->name : "Current research program"}});
    }
    for (const auto &event : step.core.research_events) {
      if (event.civilization_id != player_id) continue;
      const auto &technology = core::get_legacy_technology(event.technology_id);
      emit_owned(scope,
                 technology.category == core::TechnologyCategory::Ftl
                     ? "research.breakthrough.major"
                     : "research.completed",
                 player_id, species,
                 "technology:" + event.technology_id,
                 {{"research_name", technology.name}});
    }
    for (const auto &event : step.core.construction_events) {
      if (event.civilization_id != player_id) continue;
      const auto key =
          event.project_id == "orbital_launch_complex"
              ? "construction.orbital_launch_complex.completed"
              : event.project_id == "orbital_shipyard"
                    ? "construction.orbital_shipyard.completed"
                    : "construction.completed";
      emit_owned(scope, key, player_id, species,
                 "project:" + event.project_id,
                 {{"project_name",
                   core::get_construction_project(event.project_id).name}});
    }
    for (const auto &event : step.core.shipbuilding_events) {
      if (event.civilization_id != player_id) continue;
      const auto *design = core::find_ship_design(event.design_id);
      const auto ship_name = own_fleet_name(event.fleet_id);
      if (!design || ship_name.empty()) continue;
      emit_owned(scope, "ship.completed", player_id, species,
                 "fleet:" + std::to_string(event.fleet_id) +
                     ":design:" + event.design_id,
                 {{"ship_name", ship_name}, {"ship_class", design->name}});
    }
    for (const auto &event : step.core.exploration_events) {
      if (event.civilization_id != player_id) continue;
      const auto system_name = public_system_name(runtime, event.system_id);
      const auto identity =
          "exploration:" + std::to_string(static_cast<int>(event.type)) +
          ":fleet:" + std::to_string(event.fleet_id) + ":system:" +
          std::to_string(event.system_id) + ":body:" +
          (event.planetary_body_id
               ? std::to_string(*event.planetary_body_id)
               : "none");
      switch (event.type) {
      case core::ExplorationEventType::SystemSurveyed:
        emit_owned(scope, "exploration.survey.completed", player_id, species,
                   identity, {{"system_name", system_name}});
        break;
      case core::ExplorationEventType::AnomalySignatureDetected:
      case core::ExplorationEventType::AnomalySurveyed: {
        std::string planet = system_name;
        if (event.planetary_body_id) {
          const auto body = std::ranges::find(
              campaign.bodies, *event.planetary_body_id,
              &core::PlanetaryBody::id);
          if (body != campaign.bodies.end()) planet = body->name;
        }
        emit_owned(scope, "exploration.anomaly.discovered", player_id,
                   species, identity, {{"planet_name", planet}});
        break;
      }
      case core::ExplorationEventType::ActivitySignatureDetected:
        emit_owned(scope, "contact.unknown.detected", player_id, species,
                   identity, {{"system_name", system_name}});
        break;
      case core::ExplorationEventType::FirstContact: {
        std::string target = "unidentified civilization";
        if (event.target_civilization_id &&
            campaign.knowledge.is_civilization_known(
                player_id, *event.target_civilization_id)) {
          const auto civilization = std::ranges::find(
              campaign.civilizations, *event.target_civilization_id,
              &core::Civilization::id);
          if (civilization != campaign.civilizations.end())
            target = civilization->name;
        }
        const bool first = !has_first_contact_;
        emit_owned(scope, "contact.first", player_id, species, identity,
                   {{"civilization_name", target}}, first);
        has_first_contact_ = true;
        break;
      }
      default:
        break;
      }
    }
    for (const auto &event : step.core.colonization_events) {
      if (event.civilization_id != player_id) continue;
      const auto colony = std::ranges::find(campaign.colonies, event.colony_id,
                                            &core::Colony::id);
      if (colony == campaign.colonies.end() ||
          colony->civilization_id != event.civilization_id)
        continue;
      std::string planet = colony->name;
      if (colony->planetary_body_id) {
        const auto body = std::ranges::find(
            campaign.bodies, *colony->planetary_body_id,
            &core::PlanetaryBody::id);
        if (body != campaign.bodies.end()) planet = body->name;
      }
      const auto player = std::ranges::find(campaign.civilizations, player_id,
                                            &core::Civilization::id);
      const auto home = player != campaign.civilizations.end()
                            ? player->home_system_id
                            : 0;
      const bool first_extrasolar =
          std::ranges::count_if(campaign.colonies, [&](const auto &entry) {
            return entry.civilization_id == colony->civilization_id &&
                   entry.system_id != home;
          }) == 1;
      emit_owned(scope, "colony.founded", player_id, species,
                 "colony:" + std::to_string(event.colony_id),
                 {{"planet_name", planet}, {"colony_name", colony->name}},
                 first_extrasolar);
    }
    for (const auto &event : step.core.combat_events) {
      const auto identity =
          "combat:" + std::to_string(static_cast<int>(event.type)) +
          ":actor:" + std::to_string(event.actor_fleet_id) + ":target:" +
          (event.target_fleet_id ? std::to_string(*event.target_fleet_id)
                                 : "none") +
          ":system:" +
          (event.system_id ? std::to_string(*event.system_id) : "none");
      const auto system_name =
          event.system_id ? public_system_name(runtime, *event.system_id)
                          : "deep space";
      switch (event.type) {
      case core::CombatEventType::EngagementStarted: {
        if (event.actor_civilization_id != player_id &&
            event.target_civilization_id != player_id)
          break;
        const auto fleet_id = event.target_civilization_id == player_id
                                  ? event.target_fleet_id
                                  : event.actor_fleet_id;
        const auto name = fleet_id ? own_fleet_name(*fleet_id) : "";
        if (!name.empty())
          emit_owned(scope, "combat.fleet.attacked", player_id, species,
                     identity,
                     {{"fleet_name", name}, {"system_name", system_name}});
        break;
      }
      case core::CombatEventType::FleetRetreatInitiated:
        if (event.actor_civilization_id == player_id) {
          const auto name = own_fleet_name(event.actor_fleet_id);
          if (!name.empty())
            emit_owned(scope, "combat.fleet.retreat_initiated", player_id,
                       species, identity,
                       {{"fleet_name", name}, {"system_name", system_name}});
        }
        break;
      case core::CombatEventType::FleetDestroyed:
        if (event.target_civilization_id == player_id &&
            event.target_fleet_id) {
          const auto name = own_fleet_name(*event.target_fleet_id);
          if (!name.empty())
            emit_owned(scope, "combat.fleet.destroyed", player_id, species,
                       identity,
                       {{"fleet_name", name}, {"system_name", system_name}});
        }
        break;
      case core::CombatEventType::EngagementEnded:
        if (event.actor_civilization_id == player_id ||
            event.target_civilization_id == player_id)
          emit_owned(scope, "combat.engagement.concluded", player_id, species,
                     identity, {{"system_name", system_name}});
        break;
      default:
        break;
      }
    }
  }
  // Tactical engagement events follow the same route — the tactical phase
  // produces the identical CombatEvent payloads.
  for (const auto &event : result.tactical_events) {
    const auto identity =
        "combat:" + std::to_string(static_cast<int>(event.type)) +
        ":actor:" + std::to_string(event.actor_fleet_id) + ":target:" +
        (event.target_fleet_id ? std::to_string(*event.target_fleet_id)
                               : "none") +
        ":system:" +
        (event.system_id ? std::to_string(*event.system_id) : "none");
    if (event.type == core::CombatEventType::EngagementEnded &&
        (event.actor_civilization_id == player_id ||
         event.target_civilization_id == player_id))
      emit_owned(scope, "combat.engagement.concluded", player_id, species,
                 identity,
                 {{"system_name",
                   event.system_id
                       ? public_system_name(runtime, *event.system_id)
                       : "deep space"}});
  }
}

void NativeGameplayVoiceBridge::observe_fleets(
    const Scope &scope, const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto &campaign = runtime.world().campaign();
  const auto player_id = scope.player_id;
  const auto species = species_of(runtime, player_id, "");
  const auto player = std::ranges::find(campaign.civilizations, player_id,
                                        &core::Civilization::id);
  const auto home =
      player != campaign.civilizations.end() ? player->home_system_id : 0;
  std::unordered_set<int> live;
  for (const auto &fleet : campaign.fleets) {
    if (fleet.civilization_id != player_id) continue;
    live.insert(fleet.id);
    const bool transit =
        !fleet.current_system_id && fleet.destination_system_id.has_value();
    if (baseline_) {
      const auto previous = fleet_transit_.find(fleet.id);
      if (previous != fleet_transit_.end()) {
        if (!previous->second && transit) {
          const bool first = !has_interstellar_launch_;
          emit_owned(scope,
                     first ? "ship.interstellar.first_launch" : "ship.launched",
                     player_id, species,
                     "fleet:" + std::to_string(fleet.id) + ":departure",
                     {{"ship_name", fleet.name}}, first);
          has_interstellar_launch_ = true;
        } else if (previous->second && !transit && fleet.current_system_id) {
          const bool extrasolar = *fleet.current_system_id != home &&
                                  !has_extrasolar_arrival_;
          emit_owned(scope, "exploration.system.reached", player_id, species,
                     "fleet:" + std::to_string(fleet.id) + ":arrival:system:" +
                         std::to_string(*fleet.current_system_id),
                     {{"system_name",
                       public_system_name(runtime, *fleet.current_system_id)}},
                     extrasolar);
          if (*fleet.current_system_id != home)
            has_extrasolar_arrival_ = true;
        }
      }
    }
    fleet_transit_[fleet.id] = transit;
  }
  for (auto it = fleet_transit_.begin(); it != fleet_transit_.end();)
    if (!live.contains(it->first))
      it = fleet_transit_.erase(it);
    else
      ++it;
}

void NativeGameplayVoiceBridge::observe_hull(
    const Scope &scope, const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto &campaign = runtime.world().campaign();
  const auto status = core::build_own_combat_fleet_status(
      core::CombatReadinessView{campaign.civilizations, campaign.fleets},
      scope.player_id);
  std::unordered_set<int> live;
  for (const auto &fleet : status.fleets) {
    live.insert(fleet.fleet_id);
    if (fleet.hull_integrity_ratio() > .25) {
      critical_hulls_.erase(fleet.fleet_id);
      continue;
    }
    if (critical_hulls_.insert(fleet.fleet_id).second && baseline_)
      emit_owned(scope, "combat.hull.critical", scope.player_id,
                 species_of(runtime, scope.player_id, ""),
                 "fleet:" + std::to_string(fleet.fleet_id) + ":hull-critical",
                 {{"ship_name", fleet.fleet_name}});
  }
  for (auto it = critical_hulls_.begin(); it != critical_hulls_.end();)
    if (!live.contains(*it))
      it = critical_hulls_.erase(it);
    else
      ++it;
}

void NativeGameplayVoiceBridge::observe_diplomacy(
    const Scope &scope, core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto player_id = scope.player_id;
  const auto species = species_of(runtime, player_id, "");
  const auto view = runtime.diplomacy().build_view_for(player_id);
  for (const auto &proposal : view.proposals) {
    if (proposal.recipient_civilization_id != player_id) continue;
    if (!seen_proposals_.insert(proposal.proposal_id).second || !baseline_)
      continue;
    emit_direct(scope, "diplomacy.alien.transmission",
                proposal.proposer_civilization_id,
                species_of(runtime, proposal.proposer_civilization_id, ""),
                player_id, "proposal:" + std::to_string(proposal.proposal_id),
                {{"message", proposal.summary}});
  }
  for (auto it = seen_proposals_.begin(); it != seen_proposals_.end();)
    if (std::ranges::none_of(view.proposals, [&](const auto &p) {
          return p.proposal_id == *it;
        }))
      it = seen_proposals_.erase(it);
    else
      ++it;
  for (const auto &event : view.recent_events) {
    if (!seen_diplomacy_.insert(event.event_id).second) continue;
    const bool involved = event.primary_civilization_id == player_id ||
                          event.secondary_civilization_id == player_id;
    if (!involved) continue;
    if (!baseline_) {
      if (event.kind == core::DiplomaticEventKind::war_declared) has_war_ = true;
      continue;
    }
    if (event.kind == core::DiplomaticEventKind::war_declared) {
      const auto enemy = event.primary_civilization_id == player_id
                             ? event.secondary_civilization_id
                             : std::optional<int>{event.primary_civilization_id};
      if (!enemy) continue;
      const auto name = [&]() -> std::string {
        const auto found = std::ranges::find(
            runtime.world().campaign().civilizations, *enemy,
            &core::Civilization::id);
        return found == runtime.world().campaign().civilizations.end()
                   ? ""
                   : found->name;
      }();
      if (name.empty()) continue;
      NativeVoiceOverrides overrides;
      if (event.primary_civilization_id == player_id)
        overrides.exact_line =
            "War has been declared against {enemy_name}.";
      emit_owned(scope, "diplomacy.war.declared", player_id, species,
                 "diplomacy-event:" + std::to_string(event.event_id),
                 {{"enemy_name", name}}, !has_war_,
                 overrides.exact_line ? std::move(overrides)
                                      : std::optional<NativeVoiceOverrides>{});
      has_war_ = true;
    } else if (event.kind == core::DiplomaticEventKind::proposal_rejected ||
               event.kind == core::DiplomaticEventKind::agreement_activated ||
               event.kind == core::DiplomaticEventKind::border_warning_issued) {
      const auto key =
          event.kind == core::DiplomaticEventKind::proposal_rejected
              ? "diplomacy.proposal.rejected"
              : event.kind == core::DiplomaticEventKind::agreement_activated
                    ? "diplomacy.agreement.activated"
                    : "diplomacy.border_warning.issued";
      emit_owned(scope, key, player_id, species,
                 "diplomacy-event:" + std::to_string(event.event_id),
                 {{"message", event.summary}});
    }
  }
  for (auto it = seen_diplomacy_.begin(); it != seen_diplomacy_.end();)
    if (std::ranges::none_of(view.recent_events, [&](const auto &e) {
          return e.event_id == *it;
        }))
      it = seen_diplomacy_.erase(it);
    else
      ++it;
}

void NativeGameplayVoiceBridge::observe_economy(
    const Scope &scope,
    const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto &economies = runtime.world().campaign().economies;
  const auto economy = std::ranges::find(economies, scope.player_id,
                                         &core::CivilizationEconomy::civilization_id);
  if (economy == economies.end()) return;
  const auto health = core::assess_treasury(
      economy->credits, economy->last_credits_per_second,
      economy->operating_arrears);
  const bool critical =
      health.state == core::TreasuryHealthState::Arrears ||
      health.state == core::TreasuryHealthState::Depleted ||
      (health.state == core::TreasuryHealthState::Deficit &&
       health.runway_days <= 30.);
  if (!critical) return;
  emit_owned(scope, "economy.treasury.critical", scope.player_id,
             species_of(runtime, scope.player_id, ""), "treasury-critical",
             {{"amount", format_amount(economy->credits)}});
}

void NativeGameplayVoiceBridge::observe_logistics(
    const Scope &scope,
    const core::IntegratedAdaptiveCampaignRuntime &runtime) {
  const auto &campaign = runtime.world().campaign();
  const auto construction =
      core::economic_construction_projection(campaign.construction);
  const auto fleets = core::economic_fleet_projection(campaign.fleets);
  const core::EconomyWorldView world{campaign.civilizations, campaign.bodies,
                                     construction, fleets};
  const auto logistics = core::economy_logistics(world, campaign.colonies,
                                                 campaign.economies,
                                                 scope.player_id);
  std::unordered_set<int> critical;
  for (const auto &colony : logistics.colonies)
    if (colony.condition == core::SupplyCondition::Critical)
      critical.insert(colony.system_id);
  for (const auto system_id : critical) {
    if (!critical_logistics_.insert(system_id).second || !baseline_) continue;
    double shortfall = 0;
    for (const auto &colony : logistics.colonies)
      if (colony.system_id == system_id)
        shortfall += colony.imported_support_required_per_day;
    emit_owned(scope, "logistics.critical", scope.player_id,
               species_of(runtime, scope.player_id, ""),
               "system:" + std::to_string(system_id) + ":logistics-critical",
               {{"system_name", public_system_name(runtime, system_id)},
                {"detail", "Local support is short by " +
                               format_amount(shortfall) +
                               " units per day."}});
  }
  for (auto it = critical_logistics_.begin(); it != critical_logistics_.end();)
    if (!critical.contains(*it))
      it = critical_logistics_.erase(it);
    else
      ++it;
}

} // namespace stellar::native_voice
