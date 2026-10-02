#include <stellar/core/campaign_scripted_content.hpp>

#include <stellar/core/campaign_event_history.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/engine/asset_registry.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>

namespace stellar::core {
namespace {

using engine::ScriptCondition;
using engine::ScriptEffect;
using engine::ScriptFiringContext;
using engine::ScriptScope;
using engine::ScriptScopeKind;

std::optional<std::string_view>
arg_of(std::span<const std::pair<std::string, std::string>> args,
       std::string_view key) {
  for (const auto &[name, value] : args)
    if (name == key) return std::string_view(value);
  return std::nullopt;
}

std::optional<double>
arg_number(std::span<const std::pair<std::string, std::string>> args,
           std::string_view key) {
  const auto value = arg_of(args, key);
  if (!value) return std::nullopt;
  double parsed{};
  const auto result =
      std::from_chars(value->data(), value->data() + value->size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value->data() + value->size())
    return std::nullopt;
  return parsed;
}

std::optional<std::int64_t>
arg_int(std::span<const std::pair<std::string, std::string>> args,
        std::string_view key) {
  const auto value = arg_of(args, key);
  if (!value) return std::nullopt;
  std::int64_t parsed{};
  const auto result =
      std::from_chars(value->data(), value->data() + value->size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value->data() + value->size())
    return std::nullopt;
  return parsed;
}

bool requires_arg(std::span<const std::pair<std::string, std::string>> args,
                  std::string_view key, std::string &error,
                  std::string_view what) {
  if (!arg_of(args, key)) {
    error = std::string(what) + " requires args." + std::string(key);
    return false;
  }
  return true;
}

// Civilization reference arguments accept a concrete id or a reserved
// name: "player" resolves to the campaign's player civilization, "origin"
// to the firing context's origin civilization (the other party in a
// contact/war event). Returns nullopt when the reference cannot resolve.
std::optional<std::int64_t>
arg_civilization(std::span<const std::pair<std::string, std::string>> args,
                 std::string_view key, const ScriptFiringContext &context,
                 const CampaignSimulationState &world) {
  const auto value = arg_of(args, key);
  if (!value) return std::nullopt;
  if (*value == "player") return world.campaign().player_civilization_id;
  if (*value == "origin") return context.origin;
  if (const auto id = arg_int(args, key)) return *id;
  return std::nullopt;
}

bool valid_civilization_arg(
    std::span<const std::pair<std::string, std::string>> args,
    std::string_view key) {
  const auto value = arg_of(args, key);
  return value && (*value == "player" || *value == "origin" ||
                   arg_int(args, key).has_value());
}

const char *scope_name(ScriptScopeKind kind) {
  switch (kind) {
  case ScriptScopeKind::global: return "global";
  case ScriptScopeKind::civilization: return "civilization";
  case ScriptScopeKind::colony: return "colony";
  case ScriptScopeKind::system: return "system";
  case ScriptScopeKind::body: return "body";
  case ScriptScopeKind::fleet: return "fleet";
  case ScriptScopeKind::origin: return "origin";
  }
  return "?";
}

bool scope_is(const ScriptScope &scope, ScriptScopeKind required,
              std::string &error, std::string_view what) {
  if (scope.kind != required && scope.kind != ScriptScopeKind::origin) {
    error = std::string(what) + " requires a " + scope_name(required) +
            " scope (got " + scope_name(scope.kind) + ")";
    return false;
  }
  return true;
}

const Colony *find_colony(const CampaignSimulationState &world, int id) {
  const auto &colonies = world.campaign().colonies;
  const auto it = std::find_if(colonies.begin(), colonies.end(),
                               [&](const Colony &c) { return c.id == id; });
  return it == colonies.end() ? nullptr : &*it;
}

Colony *find_colony(CampaignSimulationState &world, int id) {
  auto &colonies = world.campaign().colonies;
  const auto it = std::find_if(colonies.begin(), colonies.end(),
                               [&](const Colony &c) { return c.id == id; });
  return it == colonies.end() ? nullptr : &*it;
}

const PlanetaryBody *find_body(const CampaignSimulationState &world, int id) {
  const auto &bodies = world.campaign().bodies;
  const auto it = std::find_if(bodies.begin(), bodies.end(),
                               [&](const PlanetaryBody &b) { return b.id == id; });
  return it == bodies.end() ? nullptr : &*it;
}

PlanetaryBody *find_body(CampaignSimulationState &world, int id) {
  auto &bodies = world.campaign().bodies;
  const auto it = std::find_if(bodies.begin(), bodies.end(),
                               [&](PlanetaryBody &b) { return b.id == id; });
  return it == bodies.end() ? nullptr : &*it;
}

const StellarSystem *find_system(const CampaignSimulationState &world, int id) {
  const auto &systems = world.campaign().systems;
  const auto it =
      std::find_if(systems.begin(), systems.end(),
                   [&](const StellarSystem &s) { return s.id == id; });
  return it == systems.end() ? nullptr : &*it;
}

const Civilization *find_civilization(const CampaignSimulationState &world,
                                      int id) {
  const auto &civilizations = world.campaign().civilizations;
  const auto it =
      std::find_if(civilizations.begin(), civilizations.end(),
                   [&](const Civilization &c) { return c.id == id; });
  return it == civilizations.end() ? nullptr : &*it;
}

const CivilizationEconomy *find_economy(const CampaignSimulationState &world,
                                        int civilization_id) {
  const auto &economies = world.campaign().economies;
  const auto it = std::find_if(
      economies.begin(), economies.end(),
      [&](const CivilizationEconomy &e) {
        return e.civilization_id == civilization_id;
      });
  return it == economies.end() ? nullptr : &*it;
}

CivilizationEconomy *find_economy(CampaignSimulationState &world,
                                  int civilization_id) {
  auto &economies = world.campaign().economies;
  const auto it = std::find_if(
      economies.begin(), economies.end(),
      [&](CivilizationEconomy &e) {
        return e.civilization_id == civilization_id;
      });
  return it == economies.end() ? nullptr : &*it;
}

double relationship_axis(const DiplomaticRelationshipSnapshot &relationship,
                         std::string_view axis) {
  if (axis == "trust") return relationship.trust;
  if (axis == "hostility") return relationship.hostility;
  if (axis == "fear") return relationship.fear;
  if (axis == "respect") return relationship.respect;
  if (axis == "cooperation") return relationship.cooperation;
  return std::numeric_limits<double>::quiet_NaN();
}

bool valid_axis(std::string_view axis) {
  return axis == "trust" || axis == "hostility" || axis == "fear" ||
         axis == "respect" || axis == "cooperation";
}

std::int64_t diplomacy_tick(double day) {
  return DiplomacyCampaignClock::from_simulation_days(day);
}

ScriptFiringContext context_of(const Civilization &civilization) {
  ScriptFiringContext context;
  context.civilization = civilization.id;
  return context;
}
ScriptFiringContext context_of(const Colony &colony) {
  ScriptFiringContext context;
  context.civilization = colony.civilization_id;
  context.colony = colony.id;
  context.system = colony.system_id;
  if (colony.planetary_body_id) context.body = *colony.planetary_body_id;
  return context;
}
ScriptFiringContext context_of(const StellarSystem &system) {
  ScriptFiringContext context;
  context.system = system.id;
  return context;
}
ScriptFiringContext context_of(const PlanetaryBody &body) {
  ScriptFiringContext context;
  context.body = body.id;
  context.system = body.system_id;
  return context;
}
ScriptFiringContext context_of(const FleetState &fleet) {
  ScriptFiringContext context;
  context.civilization = fleet.civilization_id;
  context.fleet = fleet.id;
  if (fleet.current_system_id) context.system = *fleet.current_system_id;
  return context;
}

} // namespace

std::vector<ScriptedStepEvent>
script_events_for_step(const IntegratedAdaptiveCampaignStepResult &step,
    std::span<const DiplomaticHistoryEventSnapshot> diplomacy_events) {
  std::vector<ScriptedStepEvent> out;
  const auto reserve = step.core.construction_events.size() +
                       step.core.shipbuilding_events.size() +
                       step.core.research_events.size() +
                       step.core.exploration_events.size() +
                       step.core.combat_events.size() +
                       step.core.colonization_events.size() +
                       step.research_events.size() + diplomacy_events.size();
  out.reserve(reserve);

  for (const auto &event : step.core.construction_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    out.push_back({"construction.project", context});
  }
  for (const auto &event : step.core.shipbuilding_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    context.fleet = event.fleet_id;
    out.push_back({"shipbuilding.ship", context});
  }
  for (const auto &event : step.core.research_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    out.push_back({"research.legacy", context});
  }
  for (const auto &event : step.research_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    out.push_back({"research.adaptive", context});
  }
  for (const auto &event : step.core.exploration_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    context.fleet = event.fleet_id;
    context.system = event.system_id;
    if (event.planetary_body_id) context.body = *event.planetary_body_id;
    if (event.target_civilization_id)
      context.origin = *event.target_civilization_id;
    out.push_back(
        {std::string(exploration_event_category(event.type)), context});
  }
  for (const auto &event : step.core.combat_events) {
    ScriptFiringContext context;
    context.civilization = event.actor_civilization_id;
    context.fleet = event.actor_fleet_id;
    if (event.system_id) context.system = *event.system_id;
    if (event.target_civilization_id)
      context.origin = *event.target_civilization_id;
    out.push_back({std::string(combat_event_category(event.type)), context});
  }
  for (const auto &event : step.core.colonization_events) {
    ScriptFiringContext context;
    context.civilization = event.civilization_id;
    context.fleet = event.fleet_id;
    context.system = event.system_id;
    context.colony = event.colony_id;
    out.push_back({"colony.founded", context});
  }
  for (const auto &event : diplomacy_events) {
    ScriptFiringContext context;
    context.civilization = event.primary_civilization_id;
    if (event.secondary_civilization_id)
      context.origin = *event.secondary_civilization_id;
    if (event.system_id) context.system = *event.system_id;
    out.push_back(
        {std::string(diplomatic_event_category(event.kind)), context});
  }
  return out;
}

CampaignScriptedContentAdapter::CampaignScriptedContentAdapter(
    CampaignSimulationState &world, DiplomacyState &diplomacy,
    AdaptiveResearchCampaignState &research, engine::EventHistory &history)
    : world_(&world), diplomacy_state_(&diplomacy), diplomacy_(diplomacy),
      research_(&research), history_(&history) {}

std::int64_t
CampaignScriptedContentAdapter::scope_id(const ScriptScope &scope,
                                         const ScriptFiringContext &context)
    const {
  if (scope.id >= 0) return scope.id;
  switch (scope.kind) {
  case ScriptScopeKind::global: return -1;
  case ScriptScopeKind::civilization: return context.civilization;
  case ScriptScopeKind::colony: return context.colony;
  case ScriptScopeKind::system: return context.system;
  case ScriptScopeKind::body: return context.body;
  case ScriptScopeKind::fleet: return context.fleet;
  case ScriptScopeKind::origin: return context.origin;
  }
  return -1;
}

std::string CampaignScriptedContentAdapter::validate_condition(
    const ScriptCondition &leaf, std::string_view, std::string_view) {
  std::string error;
  const auto &args = leaf.args;
  const auto name = leaf.check;
  if (name == "treasury_at_least") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "amount", error, name))
      return error;
    return arg_number(args, "amount") ? "" : "treasury_at_least amount must be a number";
  }
  if (name == "colony_count_at_least") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "count", error, name))
      return error;
    return arg_int(args, "count") ? "" : "colony_count_at_least count must be an integer";
  }
  if (name == "population_at_least") {
    if (leaf.scope.kind != ScriptScopeKind::civilization &&
        leaf.scope.kind != ScriptScopeKind::colony &&
        leaf.scope.kind != ScriptScopeKind::origin) {
      return "population_at_least requires a civilization or colony scope";
    }
    if (!requires_arg(args, "millions", error, name)) return error;
    return arg_number(args, "millions") ? "" : "population_at_least millions must be a number";
  }
  if (name == "has_capability") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "capability", error, name))
      return error;
    return {};
  }
  if (name == "relationship_at_least") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "civilization", error, name) ||
        !requires_arg(args, "value", error, name))
      return error;
    if (!valid_civilization_arg(args, "civilization"))
      return "relationship_at_least civilization must be an id, \"player\" or \"origin\"";
    if (const auto axis = arg_of(args, "axis"); axis && !valid_axis(*axis))
      return "relationship_at_least axis must be trust|hostility|fear|respect|cooperation";
    return arg_number(args, "value") ? "" : "relationship_at_least value must be a number";
  }
  if (name == "at_war") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name))
      return error;
    if (arg_of(args, "civilization") &&
        !valid_civilization_arg(args, "civilization"))
      return "at_war civilization must be an id, \"player\" or \"origin\"";
    return {};
  }
  if (name == "system_known" || name == "system_fully_surveyed") {
    if (!scope_is(leaf.scope, ScriptScopeKind::system, error, name))
      return error;
    if (arg_of(args, "civilization") &&
        !valid_civilization_arg(args, "civilization"))
      return name + " civilization must be an id, \"player\" or \"origin\"";
    return {};
  }
  if (name == "body_has_anomaly") {
    if (!scope_is(leaf.scope, ScriptScopeKind::body, error, name))
      return error;
    return {};
  }
  if (name == "civilization_is_player") {
    if (!scope_is(leaf.scope, ScriptScopeKind::civilization, error, name))
      return error;
    return {};
  }
  if (name == "elapsed_days_at_least") {
    if (!requires_arg(args, "days", error, name)) return error;
    return arg_number(args, "days") ? "" : "elapsed_days_at_least days must be a number";
  }
  return "unknown check '" + name + "'";
}

std::string CampaignScriptedContentAdapter::validate_effect(
    const ScriptEffect &effect, std::string_view, std::string_view) {
  std::string error;
  const auto &args = effect.args;
  const auto name = effect.action;
  if (name == "grant_credits" || name == "grant_industry" ||
      name == "grant_science") {
    if (!scope_is(effect.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "amount", error, name))
      return error;
    return arg_number(args, "amount") ? "" : name + " amount must be a number";
  }
  if (name == "modify_relationship") {
    if (!scope_is(effect.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "civilization", error, name))
      return error;
    if (!valid_civilization_arg(args, "civilization"))
      return "modify_relationship civilization must be an id, \"player\" or \"origin\"";
    static const std::string_view axes[] = {"trust", "hostility", "fear",
                                            "respect", "cooperation"};
    bool any_axis = false;
    for (const auto axis : axes) {
      const auto value = arg_of(args, axis);
      if (!value) continue;
      any_axis = true;
      if (!arg_number(args, axis))
        return "modify_relationship " + std::string(axis) + " must be a number";
    }
    if (!any_axis)
      return "modify_relationship needs at least one axis delta";
    return {};
  }
  if (name == "set_hostile" || name == "declare_war") {
    if (!scope_is(effect.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "civilization", error, name))
      return error;
    return valid_civilization_arg(args, "civilization")
               ? ""
               : name + " civilization must be an id, \"player\" or \"origin\"";
  }
  if (name == "resolve_anomaly") {
    if (!scope_is(effect.scope, ScriptScopeKind::body, error, name))
      return error;
    return {};
  }
  if (name == "reveal_system") {
    if (!scope_is(effect.scope, ScriptScopeKind::system, error, name))
      return error;
    if (arg_of(args, "civilization") &&
        !valid_civilization_arg(args, "civilization"))
      return "reveal_system civilization must be an id, \"player\" or \"origin\"";
    return {};
  }
  if (name == "grant_capability") {
    if (!scope_is(effect.scope, ScriptScopeKind::civilization, error, name) ||
        !requires_arg(args, "capability", error, name))
      return error;
    return {};
  }
  if (name == "chronicle_record") {
    if (!requires_arg(args, "category", error, name) ||
        !requires_arg(args, "summary", error, name))
      return error;
    const auto category = *arg_of(args, "category");
    if (!category.starts_with("scripted."))
      return "chronicle_record category must start with 'scripted.'";
    if (const auto significance = arg_of(args, "significance");
        significance && !arg_number(args, "significance"))
      return "chronicle_record significance must be a number";
    if (arg_of(args, "visible_to") &&
        !valid_civilization_arg(args, "visible_to"))
      return "chronicle_record visible_to must be an id, \"player\" or \"origin\"";
    return {};
  }
  return "unknown effect '" + name + "'";
}

std::vector<engine::ScriptFiringContext>
CampaignScriptedContentAdapter::enumerate_contexts(
    const engine::ScriptedEventDefinition &definition) {
  const auto &campaign = world_->campaign();
  std::vector<ScriptFiringContext> contexts;
  switch (definition.scope.kind) {
  case ScriptScopeKind::global:
    contexts.push_back({});
    break;
  case ScriptScopeKind::civilization:
    if (definition.scope.id >= 0) {
      if (find_civilization(*world_, static_cast<int>(definition.scope.id))) {
        ScriptFiringContext context;
        context.civilization = definition.scope.id;
        contexts.push_back(context);
      }
      break;
    }
    for (const auto &civilization : campaign.civilizations)
      contexts.push_back(context_of(civilization));
    break;
  case ScriptScopeKind::colony:
    for (const auto &colony : campaign.colonies)
      if (definition.scope.id < 0 || colony.id == definition.scope.id)
        contexts.push_back(context_of(colony));
    break;
  case ScriptScopeKind::system:
    for (const auto &system : campaign.systems)
      if (definition.scope.id < 0 || system.id == definition.scope.id)
        contexts.push_back(context_of(system));
    break;
  case ScriptScopeKind::body:
    for (const auto &body : campaign.bodies)
      if (definition.scope.id < 0 || body.id == definition.scope.id)
        contexts.push_back(context_of(body));
    break;
  case ScriptScopeKind::fleet:
    for (const auto &fleet : campaign.fleets)
      if (definition.scope.id < 0 || fleet.id == definition.scope.id)
        contexts.push_back(context_of(fleet));
    break;
  case ScriptScopeKind::origin:
    // Origin scopes are contextual — nothing authoritative to enumerate.
    break;
  }
  return contexts;
}

bool CampaignScriptedContentAdapter::evaluate_condition(
    const ScriptCondition &leaf, const ScriptFiringContext &context) {
  const auto &campaign = world_->campaign();
  const auto &args = leaf.args;
  const auto id = scope_id(leaf.scope, context);
  const auto name = leaf.check;

  if (name == "treasury_at_least") {
    if (id < 0) return false;
    const auto *economy = find_economy(*world_, static_cast<int>(id));
    return economy && arg_number(args, "amount") &&
           economy->credits >= *arg_number(args, "amount");
  }
  if (name == "colony_count_at_least") {
    if (id < 0) return false;
    const auto required = arg_int(args, "count");
    if (!required) return false;
    std::int64_t count = 0;
    for (const auto &colony : campaign.colonies)
      if (colony.civilization_id == id) ++count;
    return count >= *required;
  }
  if (name == "population_at_least") {
    if (id < 0) return false;
    const auto required = arg_number(args, "millions");
    if (!required) return false;
    // Colony scope matches one colony; civilization (and origin, which
    // resolves to a civilization) sums that civilization's colonies.
    const bool colony_scope = leaf.scope.kind == ScriptScopeKind::colony;
    double total = 0.0;
    for (const auto &colony : campaign.colonies)
      if (colony_scope ? colony.id == id : colony.civilization_id == id)
        total += colony.population_millions;
    return total >= *required;
  }
  if (name == "has_capability") {
    if (id < 0) return false;
    const auto capability = arg_of(args, "capability");
    if (!capability) return false;
    const auto *civilization = research_->try_get_civilization(static_cast<int>(id));
    return civilization &&
           civilization->has_capability(
               std::string(*capability),
               std::nullopt);
  }
  if (name == "relationship_at_least") {
    if (id < 0) return false;
    const auto target =
        arg_civilization(args, "civilization", context, *world_);
    const auto value = arg_number(args, "value");
    if (!target || !value || *target < 0) return false;
    const auto relationship = diplomacy_state_->get_relationship(
        static_cast<int>(id), static_cast<int>(*target));
    if (!relationship) return false;
    const auto axis = arg_of(args, "axis").value_or("trust");
    const double measured = relationship_axis(*relationship, axis);
    return !std::isnan(measured) && measured >= *value;
  }
  if (name == "at_war") {
    if (id < 0) return false;
    if (arg_of(args, "civilization")) {
      const auto target =
          arg_civilization(args, "civilization", context, *world_);
      if (!target || *target < 0) return false;
      const auto relationship = diplomacy_state_->get_relationship(
          static_cast<int>(id), static_cast<int>(*target));
      return relationship && relationship->political_state ==
                                 DiplomaticPoliticalState::at_war;
    }
    for (const auto &other : campaign.civilizations) {
      if (other.id == id) continue;
      const auto relationship =
          diplomacy_state_->get_relationship(static_cast<int>(id), other.id);
      if (relationship && relationship->political_state ==
                              DiplomaticPoliticalState::at_war)
        return true;
    }
    return false;
  }
  if (name == "system_known" || name == "system_fully_surveyed") {
    if (id < 0) return false;
    const auto observer =
        arg_civilization(args, "civilization", context, *world_)
            .value_or(context.civilization);
    if (observer < 0) return false;
    return name == "system_known"
               ? campaign.knowledge.is_system_known(static_cast<int>(observer),
                                                    static_cast<int>(id))
               : campaign.knowledge.is_system_fully_surveyed(
                     static_cast<int>(observer), static_cast<int>(id));
  }
  if (name == "body_has_anomaly") {
    if (id < 0) return false;
    const auto *body = find_body(*world_, static_cast<int>(id));
    return body && body->has_anomaly;
  }
  if (name == "civilization_is_player") {
    return id >= 0 && static_cast<int>(id) == campaign.player_civilization_id;
  }
  if (name == "elapsed_days_at_least") {
    const auto required = arg_number(args, "days");
    return required && day_ >= *required;
  }
  return false;
}

void CampaignScriptedContentAdapter::apply_effect(
    const ScriptEffect &effect, const ScriptFiringContext &context) {
  const auto &args = effect.args;
  const auto id = scope_id(effect.scope, context);
  const auto name = effect.action;
  const auto amount = arg_number(args, "amount").value_or(0.0);

  if (name == "grant_credits" || name == "grant_industry" ||
      name == "grant_science") {
    if (id < 0) return;
    auto *economy = find_economy(*world_, static_cast<int>(id));
    if (!economy) return;
    if (name == "grant_credits") economy->credits += amount;
    else if (name == "grant_industry") economy->industry += amount;
    else economy->science += amount;
    return;
  }
  if (name == "modify_relationship" || name == "set_hostile" ||
      name == "declare_war") {
    if (id < 0) return;
    const auto target =
        arg_civilization(args, "civilization", context, *world_);
    if (!target || *target < 0) return;
    try {
      if (name == "declare_war") {
        diplomacy_.declare_war(static_cast<int>(id), static_cast<int>(*target),
                               diplomacy_tick(day_));
      } else if (name == "set_hostile") {
        const auto reason =
            arg_of(args, "reason").value_or("scripted hostility");
        diplomacy_.set_hostile(static_cast<int>(id), static_cast<int>(*target),
                               diplomacy_tick(day_), std::string(reason));
      } else {
        RelationshipImpact impact;
        impact.trust_delta = arg_number(args, "trust").value_or(0.0);
        impact.hostility_delta = arg_number(args, "hostility").value_or(0.0);
        impact.fear_delta = arg_number(args, "fear").value_or(0.0);
        impact.respect_delta = arg_number(args, "respect").value_or(0.0);
        impact.cooperation_delta =
            arg_number(args, "cooperation").value_or(0.0);
        impact.grievance_severity =
            arg_number(args, "grievance").value_or(0.0);
        impact.reason = std::string(
            arg_of(args, "reason").value_or("scripted event"));
        diplomacy_.apply_relationship_impact(static_cast<int>(id),
                                             static_cast<int>(*target), impact,
                                             diplomacy_tick(day_));
      }
    } catch (const std::exception &) {
      // Diplomacy rejects impossible commands (e.g. war against an
      // unidentified civilization) — impossible-at-runtime effects no-op.
    }
    return;
  }
  if (name == "resolve_anomaly") {
    if (id < 0) return;
    if (auto *body = find_body(*world_, static_cast<int>(id)))
      body->has_anomaly = false;
    return;
  }
  if (name == "reveal_system") {
    if (id < 0) return;
    const auto observer =
        arg_civilization(args, "civilization", context, *world_)
            .value_or(context.civilization);
    if (observer < 0) return;
    world_->campaign().knowledge.reveal_system(static_cast<int>(observer),
                                               static_cast<int>(id));
    return;
  }
  if (name == "grant_capability") {
    if (id < 0) return;
    const auto capability = arg_of(args, "capability");
    if (!capability || !research_->try_get_civilization(static_cast<int>(id)))
      return;
    auto &civilization =
        detail::AdaptiveResearchCampaignStateAccess::get_civilization(
            *research_, static_cast<int>(id));
    detail::AdaptiveResearchStateWriter::add_capability(
        civilization, {std::string(*capability), std::nullopt});
    return;
  }
  if (name == "chronicle_record") {
    const auto category = arg_of(args, "category");
    const auto summary = arg_of(args, "summary");
    if (!category || !summary || !category->starts_with("scripted.")) return;
    engine::HistoryEvent record;
    record.category = std::string(*category);
    record.summary = std::string(*summary);
    record.at_day = day_;
    record.significance = arg_number(args, "significance").value_or(0.5);
    if (context.civilization >= 0)
      record.actors.push_back(
          static_cast<std::uint64_t>(context.civilization));
    if (context.system >= 0) record.location = context.system;
    record.visible_to = record.actors;
    // An explicit audience (e.g. "player") widens visibility beyond the
    // involved actors so player-directed scripted news reaches the player.
    if (const auto audience =
            arg_civilization(args, "visible_to", context, *world_);
        audience && *audience >= 0 &&
        !std::ranges::contains(record.visible_to,
                               static_cast<std::uint64_t>(*audience)))
      record.visible_to.push_back(static_cast<std::uint64_t>(*audience));
    std::vector<engine::HistoryEvent> batch{std::move(record)};
    widen_history_visibility(batch, world_->campaign());
    for (auto &entry : batch) history_->record(std::move(entry));
    return;
  }
}

void CampaignScriptedContentAdapter::event_fired(
    const engine::ScriptedEventDefinition &definition,
    const ScriptFiringContext &context) {
  engine::HistoryEvent record;
  record.category = "scripted." + definition.id;
  record.summary = "Scripted event '" + definition.id + "' fired";
  record.at_day = day_;
  record.significance = 0.5;
  if (context.civilization >= 0)
    record.actors.push_back(static_cast<std::uint64_t>(context.civilization));
  if (context.origin >= 0 && context.origin != context.civilization)
    record.actors.push_back(static_cast<std::uint64_t>(context.origin));
  if (context.system >= 0) record.location = context.system;
  if (context.colony >= 0)
    record.tags.push_back("colony:" + std::to_string(context.colony));
  if (context.body >= 0)
    record.tags.push_back("body:" + std::to_string(context.body));
  if (context.fleet >= 0)
    record.tags.push_back("fleet:" + std::to_string(context.fleet));
  record.visible_to = record.actors;
  std::vector<engine::HistoryEvent> batch{std::move(record)};
  widen_history_visibility(batch, world_->campaign());
  for (auto &entry : batch) history_->record(std::move(entry));
}

std::vector<engine::ScriptLoadError> load_scripted_content_directory(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const std::filesystem::path &root) {
  std::vector<engine::ScriptLoadError> errors;
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec) {
    errors.push_back({root.string(), {},
                      "scripted content directory not found"});
    return errors;
  }
  std::vector<std::filesystem::path> documents;
  for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) break;
    if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
      documents.push_back(entry.path());
  }
  if (ec) {
    errors.push_back(
        {root.string(), {},
         "scripted content directory enumeration failed: " + ec.message()});
    return errors;
  }
  std::ranges::sort(documents);
  for (const auto &path : documents) {
    const auto file = path.filename().generic_string();
    auto stream = engine::resource_stream(path);
    if (!stream) {
      errors.push_back({file, {}, "unable to read scripted document"});
      continue;
    }
    const std::string document{std::istreambuf_iterator<char>(stream),
                               std::istreambuf_iterator<char>()};
    [[maybe_unused]] const bool loaded =
        campaign.load_scripted_document(document, file, &errors);
  }
  return errors;
}

} // namespace stellar::core
