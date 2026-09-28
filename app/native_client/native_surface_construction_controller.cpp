#include "native_surface_construction_controller.hpp"
#include "native_data_names.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

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
  ConstructionWorld command;
};

Context context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto *player = find_one(world.civilizations,
                                world.player_civilization_id,
                                &Civilization::id);
  if (!player || !player->is_player)
    throw std::runtime_error(
        "The campaign has no valid player surface-construction owner.");
  const auto *research = &runtime.research();
  auto capability = [research](const int civilization_id,
                               const std::string_view capability_id) {
    return AdaptiveResearchConstructionCapabilityView(*research)
        .has_civilization_capability(civilization_id, capability_id);
  };
  return {runtime,
          world,
          *player,
          {world.civilizations, world.bodies, world.construction,
           world.colonies, world.economies, {}, std::move(capability)}};
}

const Colony *bound_colony(const Context &current,
                           const NativeColonyView &view,
                           const std::uint64_t generation) {
  if (view.campaign_generation != generation ||
      view.player_civilization_id != current.player.id ||
      !current.world.knowledge.is_system_known(current.player.id,
                                                view.system_id) ||
      current.world.knowledge.system_survey_level(current.player.id,
                                                   view.system_id) <
          SystemSurveyLevel::partially_surveyed)
    return nullptr;
  const auto *colony = find_one(current.world.colonies, view.colony_id,
                                &Colony::id);
  if (!colony || colony->civilization_id != current.player.id ||
      colony->system_id != view.system_id ||
      colony->planetary_body_id != view.body_id)
    return nullptr;
  const auto *body = find_one(current.world.bodies, view.body_id,
                              &PlanetaryBody::id);
  if (!body || body->system_id != view.system_id) return nullptr;
  return colony;
}

bool live_binding(const Context &current, const int player_id,
                  const int system_id, const int body_id,
                  const int colony_id) {
  if (player_id != current.player.id ||
      !current.world.knowledge.is_system_known(player_id, system_id) ||
      current.world.knowledge.system_survey_level(player_id, system_id) <
          SystemSurveyLevel::partially_surveyed)
    return false;
  const auto *colony = find_one(current.world.colonies, colony_id, &Colony::id);
  if (!colony || colony->civilization_id != player_id ||
      colony->system_id != system_id || colony->planetary_body_id != body_id)
    return false;
  const auto *body = find_one(current.world.bodies, body_id, &PlanetaryBody::id);
  return body && body->system_id == system_id;
}

std::string resolve(const stellar::engine::LocalizationTable *locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string resolved(const stellar::engine::LocalizationTable *locale,
                     std::string_view key,
                     std::initializer_list<std::string> args,
                     std::string_view fallback) {
  if (locale && locale->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

NativeSurfaceCommandOutcome stale(
    const stellar::engine::LocalizationTable *locale) {
  return {false,
          resolve(locale, "SURFACE_MSG_QUOTE_CHANGED",
                  "The campaign or surface quote changed; review it before confirming.")};
}

// The core assessment API reports denial reasons as stable English literals.
// Map each literal to a localization key so denied orders explain themselves
// in the active locale; unrecognized text passes through unchanged.
std::string localized_denial(
    const stellar::engine::LocalizationTable *locale,
    const std::string &message) {
  static const std::pair<std::string_view, std::string_view> table[] = {
      {"You can build only in a colony you own.", "SURFACE_DENY_OWNED"},
      {"You can build only on a planet you own.", "SURFACE_DENY_OWNED"},
      {"Extreme stellar irradiation prohibits all surface construction.",
       "SURFACE_DENY_IRRADIATION"},
      {"A surveyed colony on a solid planetary surface is required.",
       "SURFACE_DENY_SURVEYED_SOLID"},
      {"The colony has no construction economy.", "SURFACE_DENY_NO_ECONOMY"},
      {"A sealed resource outpost cannot support a civilian trade hub. Deliver extracted material by freighter.",
       "SURFACE_DENY_SEALED_OUTPOST"},
      {"That building type is available only as an upgrade.",
       "SURFACE_DENY_UPGRADE_ONLY"},
      {"Complete the Command Center before constructing planetary buildings.",
       "SURFACE_DENY_HUB_FIRST"},
      {"Complete the Command Center to unlock building slots.",
       "SURFACE_DENY_HUB_SLOTS"},
      {"Upgrade the Command Center to unlock this slot.",
       "SURFACE_DENY_SLOT_LOCKED"},
      {"This slot is occupied or reserved by construction.",
       "SURFACE_DENY_SLOT_TAKEN"},
      {"No compatible site is available for this building.",
       "SURFACE_DENY_NO_SITE"},
      {"No building identifier is available.", "SURFACE_DENY_NO_ID"},
      {"Unknown building type.", "SURFACE_DENY_UNKNOWN_TYPE"},
      {"Building position and rotation must be finite.",
       "SURFACE_DENY_FINITE"},
      {"Choose a position inside the colony boundary.",
       "SURFACE_DENY_BOUNDARY"},
      {"Leave room around the colony hub.", "SURFACE_DENY_HUB_CLEARANCE"},
      {"This slope is too steep. Choose flatter ground.", "SURFACE_DENY_SLOPE"},
      {"The colony contains an unknown building type.",
       "SURFACE_DENY_UNKNOWN_MEMBER"},
      {"That position overlaps another building or construction site.",
       "SURFACE_DENY_OVERLAP"},
      {"This demo colony has reached its 64-building limit.",
       "SURFACE_DENY_LIMIT"},
      {"You can remove buildings only from a colony you own.",
       "SURFACE_DENY_REMOVE_OWNED"},
      {"That surface building no longer exists.", "SURFACE_DENY_REMOVE_GONE"},
      {"That surface building has an unknown type and cannot be removed safely.",
       "SURFACE_DENY_REMOVE_UNKNOWN"},
      {"Extreme stellar irradiation prevents surface operations.",
       "SURFACE_DENY_IRRADIATION_OPS"},
      {"You can upgrade buildings only in a colony you own.",
       "SURFACE_DENY_UPGRADE_OWNED"},
      {"Complete construction before upgrading this building.",
       "SURFACE_DENY_UPGRADE_INCOMPLETE"},
      {"This building is already being upgraded.",
       "SURFACE_DENY_UPGRADE_RUNNING"},
      {"This building has no further upgrade available.",
       "SURFACE_DENY_UPGRADE_NONE"},
      {"You can upgrade only a colony you own.", "SURFACE_DENY_HUB_OWNED"},
      {"A Command Center requires an owned settlement on a solid surface.",
       "SURFACE_DENY_HUB_SOLID"},
      {"The hub expansion is already under construction.",
       "SURFACE_DENY_HUB_RUNNING"},
      {"A sealed resource outpost must be terraformed before it can become a "
       "full colony command center.",
       "SURFACE_DENY_HUB_OUTPOST"},
      {"This planetary hub is already at maximum capacity.",
       "SURFACE_DENY_HUB_MAX"},
      {"You can repair buildings only in a colony you own.",
       "SURFACE_DENY_REPAIR_OWNED"},
      {"Complete construction before repairing this building.",
       "SURFACE_DENY_REPAIR_INCOMPLETE"},
      {"That surface building has an unknown type and cannot be repaired "
       "safely.",
       "SURFACE_DENY_REPAIR_UNKNOWN"},
      {"The colony has no maintenance economy.",
       "SURFACE_DENY_NO_MAINTENANCE"},
      {"You can manage buildings only in a colony you own.",
       "SURFACE_DENY_MANAGE_OWNED"},
      {"Complete construction before changing operating status.",
       "SURFACE_DENY_STATE_INCOMPLETE"},
      {"That surface building has an unknown type and cannot be managed "
       "safely.",
       "SURFACE_DENY_MANAGE_UNKNOWN"},
      {"You can prioritize buildings only in a colony you own.",
       "SURFACE_DENY_PRIORITY_OWNED"},
      {"Complete construction before assigning operating priority.",
       "SURFACE_DENY_PRIORITY_INCOMPLETE"},
      {"This building already has priority.", "SURFACE_DENY_ALREADY_PRIORITY"},
      {"This building already has normal priority.",
       "SURFACE_DENY_ALREADY_NORMAL"},
  };
  for (const auto &[literal, key] : table)
    if (message == literal) return resolve(locale, key, message);
  const auto frag = [&](std::string_view text) {
    return stellar::native_data::localized_authored_fragment(locale, text);
  };
  const auto between = [&](std::string_view head, std::string_view tail)
      -> std::optional<std::string_view> {
    if (message.size() <= head.size() + tail.size() ||
        !message.starts_with(head) || !message.ends_with(tail))
      return std::nullopt;
    return std::string_view(message).substr(
        head.size(), message.size() - head.size() - tail.size());
  };
  const auto split_and = [&](std::string_view text)
      -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto at = text.rfind(" and ");
    if (at == std::string_view::npos) return std::nullopt;
    return std::pair{text.substr(0, at), text.substr(at + 5)};
  };
  // "{name} upgrade started: {d} game days. Existing facilities remain
  //  operational until completion."
  constexpr std::string_view upgrade_started_mid = " upgrade started: ";
  constexpr std::string_view upgrade_started_tail =
      " game days. Existing facilities remain operational until completion.";
  if (const auto at = message.find(upgrade_started_mid);
      at != std::string::npos && at > 0 &&
      message.ends_with(upgrade_started_tail)) {
    const auto days = std::string_view(message).substr(
        at + upgrade_started_mid.size(),
        message.size() - at - upgrade_started_mid.size() -
            upgrade_started_tail.size());
    return resolved(
        locale, "SURFACE_MGMT_UPGRADE_STARTED",
        {frag(std::string_view(message).substr(0, at)), std::string(days)},
        "{0} upgrade started: {1} game days. Existing facilities remain "
        "operational until completion.");
  }
  // "Upgrading to {name} requires {money} and {n} available materials."
  if (const auto rest = between("Upgrading to ", " available materials.");
      rest && message.ends_with(" available materials.")) {
    if (const auto at = rest->find(" requires ");
        at != std::string_view::npos)
      if (const auto parts =
              split_and(rest->substr(at + 10)))
        return resolved(
            locale, "SURFACE_MGMT_UPGRADE_REQUIRES",
            {frag(rest->substr(0, at)), std::string(parts->first),
             std::string(parts->second)},
            "Upgrading to {0} requires {1} and {2} available materials.");
  }
  // "Hub expansion requires {money} and {n} materials."
  if (const auto rest = between("Hub expansion requires ", " materials.");
      rest && message.ends_with(" materials.") &&
      !message.starts_with("Hub expansion authorized:"))
    if (const auto parts = split_and(*rest))
      return resolved(locale, "SURFACE_MGMT_HUB_REQUIRES",
                      {std::string(parts->first), std::string(parts->second)},
                      "Hub expansion requires {0} and {1} materials.");
  // "Hub expansion authorized: {d} game days. Capacity increases when
  //  construction completes."
  if (const auto days =
          between("Hub expansion authorized: ",
                  " game days. Capacity increases when construction "
                  "completes."))
    return resolved(
        locale, "SURFACE_MGMT_HUB_STARTED", {std::string(*days)},
        "Hub expansion authorized: {0} game days. Capacity increases when "
        "construction completes.");
  // "Repairing {name} requires {money} and {n} materials."
  if (const auto rest = between("Repairing ", " materials.");
      rest && message.ends_with(" materials.")) {
    if (const auto at = rest->find(" requires ");
        at != std::string_view::npos)
      if (const auto parts = split_and(rest->substr(at + 10)))
        return resolved(locale, "SURFACE_MGMT_REPAIR_REQUIRES",
                        {frag(rest->substr(0, at)), std::string(parts->first),
                         std::string(parts->second)},
                        "Repairing {0} requires {1} and {2} materials.");
  }
  // "{name} restored to full condition using {money} and {n} materials."
  if (const auto at =
          message.find(" restored to full condition using ");
      at != std::string::npos && message.ends_with(" materials.")) {
    const auto rest =
        std::string_view(message).substr(at + 33,
                                         message.size() - at - 33 - 11);
    if (const auto parts = split_and(rest))
      return resolved(
          locale, "SURFACE_MGMT_REPAIR_DONE",
          {frag(std::string_view(message).substr(0, at)),
           std::string(parts->first), std::string(parts->second)},
          "{0} restored to full condition using {1} and {2} materials.");
  }
  // "{name} is already at full condition."
  if (const auto name = between("", " is already at full condition.");
      name && !name->empty())
    return resolved(locale, "SURFACE_MGMT_REPAIR_FULL", {frag(*name)},
                    "{0} is already at full condition.");
  // "{name} is already operating." / "{name} is already shut down."
  if (const auto name = between("", " is already operating.");
      name && !name->empty())
    return resolved(locale, "SURFACE_MGMT_ALREADY_ON", {frag(*name)},
                    "{0} is already operating.");
  if (const auto name = between("", " is already shut down.");
      name && !name->empty())
    return resolved(locale, "SURFACE_MGMT_ALREADY_OFF", {frag(*name)},
                    "{0} is already shut down.");
  // "{name} restarted. ..." / "{name} shut down. ..."
  if (const auto name = between(
          "",
          " restarted. Staffing, power demand, output and upkeep resume "
          "when capacity is available.");
      name && !name->empty())
    return resolved(
        locale, "SURFACE_MGMT_RESTARTED", {frag(*name)},
        "{0} restarted. Staffing, power demand, output and upkeep resume "
        "when capacity is available.");
  if (const auto name = between(
          "",
          " shut down. Its staffing, power demand, output and upkeep are "
          "suspended.");
      name && !name->empty())
    return resolved(
        locale, "SURFACE_MGMT_SHUTDOWN", {frag(*name)},
        "{0} shut down. Its staffing, power demand, output and upkeep are "
        "suspended.");
  // "{name} prioritized. It receives available workers and power before
  //  normal buildings."
  if (const auto name = between(
          "",
          " prioritized. It receives available workers and power before "
          "normal buildings.");
      name && !name->empty())
    return resolved(
        locale, "SURFACE_MGMT_PRIORITIZED", {frag(*name)},
        "{0} prioritized. It receives available workers and power before "
        "normal buildings.");
  // "{name} returned to normal operating priority."
  if (const auto name = between("", " returned to normal operating priority.");
      name && !name->empty())
    return resolved(locale, "SURFACE_MGMT_NORMAL", {frag(*name)},
                    "{0} returned to normal operating priority.");
  // Hub/building upgrade lock reasons share the colony-view resolver.
  const std::string locked =
      stellar::native_data::surface_lock_reason(locale, message);
  return locked != message ? locked : message;
}

// Assessment and quote records carry the authored English building name;
// resolve it through the stable type id at this display boundary so
// comparisons and rendered text stay consistent in every locale.
std::string localized_building_name(
    const stellar::engine::LocalizationTable *locale, std::string_view type_id,
    std::string_view authored) {
  const auto *definition = find_surface_building(type_id);
  return definition
             ? stellar::native_data::surface_building_name(locale, *definition)
             : std::string(authored);
}

std::string placement_preview_message(
    const SurfaceBuildingPlacementAssessment &assessment,
    const stellar::engine::LocalizationTable *locale) {
  return resolved(locale, "SURFACE_PLACEMENT_AUTHORIZE",
                  {assessment.formatted_authorization},
                  "Authorize construction for {0}. Materials are consumed as work progresses.");
}

struct PreviewWorld {
  std::vector<Civilization> civilizations;
  std::vector<PlanetaryBody> bodies;
  std::vector<ConstructionState> construction;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<CivilizationConstructionCapabilities> capabilities;
  std::function<bool(int, std::string_view)> capability_query;

  [[nodiscard]] ConstructionWorld command() {
    return {civilizations, bodies, construction, colonies, economies,
            capabilities, capability_query};
  }
};

PreviewWorld preview_world(const Context &current) {
  return {{current.world.civilizations.begin(), current.world.civilizations.end()},
          {current.world.bodies.begin(), current.world.bodies.end()},
          {current.world.construction.begin(), current.world.construction.end()},
          {current.world.colonies.begin(), current.world.colonies.end()},
          {current.world.economies.begin(), current.world.economies.end()},
          {current.command.capabilities.begin(), current.command.capabilities.end()},
          current.command.capability_query};
}

std::string management_action_label(const NativeSurfaceManagementAction action,
                                    const bool value,
                                    const stellar::engine::LocalizationTable *locale) {
  switch (action) {
  case NativeSurfaceManagementAction::UpgradeBuilding:
    return resolve(locale, "SURFACE_ACTION_UPGRADE", "Upgrade building");
  case NativeSurfaceManagementAction::RepairBuilding:
    return resolve(locale, "SURFACE_ACTION_REPAIR", "Repair building");
  case NativeSurfaceManagementAction::SetEnabled:
    return value ? resolve(locale, "SURFACE_ACTION_RESTART", "Restart building")
                 : resolve(locale, "SURFACE_ACTION_SHUTDOWN", "Shut down building");
  case NativeSurfaceManagementAction::SetPriority:
    return value ? resolve(locale, "SURFACE_ACTION_PRIORITIZE", "Prioritize building")
                 : resolve(locale, "SURFACE_ACTION_NORMAL", "Set normal priority");
  case NativeSurfaceManagementAction::UpgradeHub:
    return resolve(locale, "SURFACE_ACTION_UPGRADE_HUB", "Upgrade hub");
  }
  return resolve(locale, "SURFACE_ACTION_MANAGE", "Manage surface");
}

bool is_management_action(const NativeSurfaceManagementAction action) {
  switch (action) {
  case NativeSurfaceManagementAction::UpgradeBuilding:
  case NativeSurfaceManagementAction::RepairBuilding:
  case NativeSurfaceManagementAction::SetEnabled:
  case NativeSurfaceManagementAction::SetPriority:
  case NativeSurfaceManagementAction::UpgradeHub:
    return true;
  }
  return false;
}

bool has_management_site(const Colony &colony,
                         const NativeSurfaceManagementAction action,
                         const int building_id) {
  return action == NativeSurfaceManagementAction::UpgradeHub ||
         find_one(colony.surface_buildings, building_id, &SurfaceBuilding::id);
}

std::string formatted_days(const double days) {
  std::ostringstream result;
  result.imbue(std::locale::classic());
  result << std::fixed << std::setprecision(1) << days;
  return result.str();
}

std::string management_description(
    const PreviewWorld &world, const int colony_id,
    const NativeSurfaceManagementAction action, const int building_id,
    const bool value, const bool accepted,
    const stellar::engine::LocalizationTable *locale) {
  const auto unavailable =
      resolve(locale, "SURFACE_DESC_UNAVAILABLE", "Confirmation is currently unavailable.");
  if (!accepted) return unavailable;
  const auto *colony = find_one(world.colonies, colony_id, &Colony::id);
  if (!colony) return unavailable;
  switch (action) {
  case NativeSurfaceManagementAction::UpgradeBuilding: {
    const auto *building = find_one(colony->surface_buildings, building_id,
                                    &SurfaceBuilding::id);
    if (!building)
      return resolve(locale, "SURFACE_DESC_UPGRADE",
                     "Confirmation will start the building upgrade.");
    const std::string days = formatted_days(building->upgrade_days_remaining);
    return building->is_enabled
               ? resolved(locale, "SURFACE_DESC_UPGRADE_ON", {days},
                          "Confirmation will start a {0}-day upgrade; the current facility will remain operational.")
               : resolved(locale, "SURFACE_DESC_UPGRADE_OFF", {days},
                          "Confirmation will start a {0}-day upgrade; the current facility will remain shut down.");
  }
  case NativeSurfaceManagementAction::RepairBuilding:
    return resolve(locale, "SURFACE_DESC_REPAIR",
                   "Confirmation will restore this building immediately.");
  case NativeSurfaceManagementAction::SetEnabled:
    return value
               ? resolve(locale, "SURFACE_DESC_RESUME",
                         "Confirmation will resume staffing, power use, output, and upkeep.")
               : resolve(locale, "SURFACE_DESC_SUSPEND",
                         "Confirmation will suspend staffing, power use, output, and upkeep.");
  case NativeSurfaceManagementAction::SetPriority:
    return value
               ? resolve(locale, "SURFACE_DESC_PRIORITY",
                         "Confirmation will give this building workers and power first.")
               : resolve(locale, "SURFACE_DESC_NORMAL",
                         "Confirmation will return this building to normal allocation.");
  case NativeSurfaceManagementAction::UpgradeHub:
    return resolved(locale, "SURFACE_DESC_HUB",
                    {formatted_days(colony->surface_hub_upgrade_days_remaining)},
                    "Confirmation will start a {0}-day hub expansion.");
  }
  return unavailable;
}

ConstructionOrderResult execute_management(
    ConstructionWorld world, const int player_id, const int colony_id,
    const NativeSurfaceManagementAction action, const int building_id,
    const bool value,
    const stellar::engine::LocalizationTable *locale) {
  switch (action) {
  case NativeSurfaceManagementAction::UpgradeBuilding:
    return upgrade_surface_building(world, player_id, colony_id, building_id);
  case NativeSurfaceManagementAction::RepairBuilding:
    return repair_surface_building(world, player_id, colony_id, building_id);
  case NativeSurfaceManagementAction::SetEnabled:
    return set_surface_building_enabled(world, player_id, colony_id,
                                        building_id, value);
  case NativeSurfaceManagementAction::SetPriority:
    return set_surface_building_priority(world, player_id, colony_id,
                                         building_id, value);
  case NativeSurfaceManagementAction::UpgradeHub:
    return upgrade_surface_hub(world, player_id, colony_id);
  }
  return {false, resolve(locale, "SURFACE_MSG_ACTION_UNAVAILABLE",
                         "The selected surface management action is unavailable.")};
}

NativeSurfaceManagementQuote assess_management(
    const Context &current, const std::uint64_t generation,
    const std::uint64_t colony_revision, const int system_id, const int body_id,
    const int colony_id, const NativeSurfaceManagementAction action,
    const int building_id, const bool value,
    const stellar::engine::LocalizationTable *locale) {
  NativeSurfaceManagementQuote quote;
  quote.campaign_generation = generation;
  quote.colony_revision = colony_revision;
  quote.player_civilization_id = current.player.id;
  quote.system_id = system_id;
  quote.body_id = body_id;
  quote.colony_id = colony_id;
  quote.building_id = building_id;
  quote.action = action;
  quote.value = value;
  quote.action_label = management_action_label(action, value, locale);

  const auto *colony = find_one(current.world.colonies, colony_id, &Colony::id);
  if (!colony) {
    quote.message = resolve(locale, "SURFACE_MSG_SETTLEMENT_GONE",
                            "That owned settlement is no longer available.");
    return quote;
  }
  if (action == NativeSurfaceManagementAction::UpgradeHub) {
    quote.building_name = resolved(locale, "SURFACE_HUB_NAME", {colony->name}, "{0} hub");
    quote.description = resolve(locale, "SURFACE_DESC_HUB_EXPAND",
                                "Authorize the next timed expansion of this colony hub.");
  } else if (const auto *building = find_one(colony->surface_buildings,
                                               building_id, &SurfaceBuilding::id)) {
    if (const auto *definition = find_surface_building(building->type_id)) {
      quote.building_name =
          stellar::native_data::surface_building_name(locale, *definition);
      quote.description =
          stellar::native_data::surface_building_description(locale,
                                                             *definition);
    } else {
      quote.building_name = resolve(locale, "SURFACE_BUILDING_NAME", "Surface building");
      quote.description = resolve(locale, "SURFACE_DESC_REVIEW",
                                  "Review the selected surface building before confirming.");
    }
  } else {
    quote.building_name = resolve(locale, "SURFACE_BUILDING_NAME", "Surface building");
    quote.description = resolve(locale, "SURFACE_DESC_REVIEW",
                                "Review the selected surface building before confirming.");
  }

  auto copied = preview_world(current);
  const auto *before = find_one(copied.economies, current.player.id,
                                &CivilizationEconomy::civilization_id);
  const double credits_before = before ? before->credits : 0.;
  const double industry_before = before ? before->industry : 0.;
  const auto result = execute_management(copied.command(), current.player.id,
                                         colony_id, action, building_id, value,
                                         locale);
  const auto *after = find_one(copied.economies, current.player.id,
                               &CivilizationEconomy::civilization_id);
  quote.accepted = result.accepted;
  quote.message = result.message;
  quote.description = management_description(copied, colony_id, action,
                                             building_id, value, result.accepted,
                                             locale);
  quote.authorization_budget_units =
      after ? std::max(0., credits_before - after->credits) : 0.;
  quote.industry_cost =
      after ? std::max(0., industry_before - after->industry) : 0.;
  quote.formatted_authorization = sovereign_currency_for_civilization(
      current.command.read().civilizations, current.player.id)
                                     .format(quote.authorization_budget_units);
  return quote;
}

bool same_management_quote(const NativeSurfaceManagementQuote &left,
                           const NativeSurfaceManagementQuote &right) {
  return left.campaign_generation == right.campaign_generation &&
         left.colony_revision == right.colony_revision &&
         left.quote_revision == right.quote_revision &&
         left.player_civilization_id == right.player_civilization_id &&
         left.system_id == right.system_id && left.body_id == right.body_id &&
         left.colony_id == right.colony_id &&
         left.building_id == right.building_id && left.action == right.action &&
         left.value == right.value && left.accepted == right.accepted &&
         left.action_label == right.action_label &&
         left.building_name == right.building_name &&
         left.formatted_authorization == right.formatted_authorization &&
         left.description == right.description && left.message == right.message &&
         left.authorization_budget_units == right.authorization_budget_units &&
         left.industry_cost == right.industry_cost;
}
} // namespace

NativeSurfaceConstructionController::ManagementSnapshot
NativeSurfaceConstructionController::management_snapshot(
    const Colony &colony, const NativeSurfaceManagementAction action,
    const int building_id) {
  ManagementSnapshot snapshot;
  snapshot.hub_level = colony.surface_hub_level;
  snapshot.hub_upgrade_days_remaining = colony.surface_hub_upgrade_days_remaining;
  if (action == NativeSurfaceManagementAction::UpgradeHub) return snapshot;
  const auto *building = find_one(colony.surface_buildings, building_id,
                                  &SurfaceBuilding::id);
  if (!building) return snapshot;
  snapshot.building_type_id = building->type_id;
  snapshot.building_complete = building->is_complete;
  snapshot.building_enabled = building->is_enabled;
  snapshot.building_priority = building->operating_priority;
  snapshot.building_condition = building->condition;
  snapshot.building_upgrade_days_remaining = building->upgrade_days_remaining;
  snapshot.pending_upgrade_type_id = building->pending_upgrade_type_id;
  return snapshot;
}

void NativeSurfaceConstructionController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native surface construction must run on the simulation owner thread.");
}

std::string NativeSurfaceConstructionController::tr(
    std::string_view key, std::string_view fallback) const {
  return resolve(locale_, key, fallback);
}

std::string NativeSurfaceConstructionController::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  return resolved(locale_, key, args, fallback);
}

void NativeSurfaceConstructionController::bind_generation(
    const std::uint64_t generation) {
  if (generation_ && generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace surface construction.");
  if (!generation_ || *generation_ != generation) {
    generation_ = generation;
    quotes_.clear();
  }
}

bool NativeSurfaceConstructionController::is_current_generation(
    const std::uint64_t generation) const noexcept {
  return generation_ && *generation_ == generation;
}

NativeSurfacePlacementQuote
NativeSurfaceConstructionController::preview_placement(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const std::string_view type_id, const float x,
    const float z, const float rotation_degrees, const std::optional<int> slot) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  NativeSurfacePlacementQuote result;
  result.campaign_generation = generation;
  result.colony_revision = view.revision;
  if (next_quote_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("No surface quote revision is available.");
  result.quote_revision = next_quote_revision_++;
  result.player_civilization_id = view.player_civilization_id;
  result.system_id = view.system_id;
  result.body_id = view.body_id;
  result.colony_id = view.colony_id;
  auto current = context(frame);
  if (!bound_colony(current, view, generation)) {
    result.message =
        tr("SURFACE_MSG_PLACEMENT_CHANGED", "The owned known settlement changed; refresh it before placement.");
    return result;
  }
  auto assessment = slot ? assess_planetary_building_slot(current.command.read(),current.player.id,view.colony_id,*slot,type_id)
      : assess_surface_building_placement(current.command.read(), current.player.id, view.colony_id, type_id, x, z, rotation_degrees);
  result.slot_index = assessment.slot_index;
  result.type_id = assessment.type_id;
  result.building_name = localized_building_name(
      locale_, assessment.type_id, assessment.building_name);
  result.x = assessment.x;
  result.z = assessment.z;
  result.normalized_rotation_degrees = assessment.normalized_rotation_degrees;
  result.prepared_building_id = assessment.prepared_building_id;
  result.authorization_budget_units = assessment.authorization_cost;
  result.industry_cost = assessment.industry_cost;
  result.formatted_authorization = assessment.formatted_authorization;
  result.accepted = assessment.accepted;
  result.message = assessment.accepted ? placement_preview_message(assessment, locale_)
                                      : localized_denial(locale_, assessment.message);
  if (assessment.accepted)
    quotes_.emplace(result.quote_revision,
                    PlacementRecord{view.system_id, view.body_id, view.revision,
                                    std::move(assessment)});
  return result;
}

NativeSurfaceRemovalQuote NativeSurfaceConstructionController::preview_removal(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  NativeSurfaceRemovalQuote result;
  result.campaign_generation = generation;
  result.colony_revision = view.revision;
  if (next_quote_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("No surface quote revision is available.");
  result.quote_revision = next_quote_revision_++;
  result.player_civilization_id = view.player_civilization_id;
  result.system_id = view.system_id;
  result.body_id = view.body_id;
  result.colony_id = view.colony_id;
  result.building_id = building_id;
  auto current = context(frame);
  if (!bound_colony(current, view, generation)) {
    result.message =
        tr("SURFACE_MSG_REMOVAL_CHANGED", "The owned known settlement changed; refresh it before removal.");
    return result;
  }
  auto assessment = assess_surface_building_removal(
      current.command.read(), current.player.id, view.colony_id, building_id);
  result.type_id = assessment.type_id;
  result.building_name = localized_building_name(
      locale_, assessment.type_id, assessment.building_name);
  result.accepted = assessment.accepted;
  result.cancellation = assessment.cancellation;
  result.refund_budget_units = assessment.refund;
  result.formatted_refund = assessment.formatted_refund;
  result.message = localized_denial(locale_, assessment.message);
  if (assessment.accepted)
    quotes_.emplace(result.quote_revision,
                    RemovalRecord{view.system_id, view.body_id, view.revision,
                                  std::move(assessment)});
  return result;
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::confirm_placement(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeSurfacePlacementQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != generation ||
      quote.campaign_generation != generation)
    return stale(locale_);
  const auto found = quotes_.find(quote.quote_revision);
  if (found == quotes_.end() ||
      !std::holds_alternative<PlacementRecord>(found->second))
    return stale(locale_);
  const auto record = std::get<PlacementRecord>(found->second);
  quotes_.erase(found); // confirmation tokens are single-use, including denial.
  const auto &assessment = record.assessment;
  if (!quote.accepted || quote.player_civilization_id != assessment.civilization_id ||
      quote.colony_revision != record.colony_revision ||
      quote.colony_id != assessment.colony_id || quote.system_id != record.system_id ||
      quote.body_id != record.body_id || quote.type_id != assessment.type_id ||
      quote.building_name != localized_building_name(locale_, assessment.type_id,
                                                     assessment.building_name) ||
      quote.x != assessment.x ||
      quote.z != assessment.z ||
      quote.normalized_rotation_degrees != assessment.normalized_rotation_degrees ||
      quote.prepared_building_id != assessment.prepared_building_id ||
      quote.slot_index != assessment.slot_index ||
      quote.authorization_budget_units != assessment.authorization_cost ||
      quote.industry_cost != assessment.industry_cost ||
      quote.formatted_authorization != assessment.formatted_authorization ||
      quote.message != placement_preview_message(assessment, locale_))
    return stale(locale_);
  auto current = context(frame);
  if (!live_binding(current, assessment.civilization_id, record.system_id,
                    record.body_id, assessment.colony_id))
    return stale(locale_);
  auto result =
      commit_surface_building_placement(current.command, assessment);
  if (result.accepted)
    result.message = resolved(
        locale_, "SURFACE_PLACEMENT_ORDERED",
        {localized_building_name(locale_, assessment.type_id,
                                 assessment.building_name),
         assessment.formatted_authorization},
        "{0} placed and authorized for {1}. Construction uses available "
        "materials.");
  else
    result.message = localized_denial(locale_, result.message);
  return {result.accepted, result.message};
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::confirm_removal(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeSurfaceRemovalQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != generation ||
      quote.campaign_generation != generation)
    return stale(locale_);
  const auto found = quotes_.find(quote.quote_revision);
  if (found == quotes_.end() ||
      !std::holds_alternative<RemovalRecord>(found->second))
    return stale(locale_);
  const auto record = std::get<RemovalRecord>(found->second);
  quotes_.erase(found);
  const auto &assessment = record.assessment;
  if (!quote.accepted || quote.player_civilization_id != assessment.civilization_id ||
      quote.colony_revision != record.colony_revision ||
      quote.colony_id != assessment.colony_id || quote.system_id != record.system_id ||
      quote.body_id != record.body_id || quote.building_id != assessment.building_id ||
      quote.type_id != assessment.type_id ||
      quote.building_name != localized_building_name(locale_, assessment.type_id,
                                                     assessment.building_name) ||
      quote.cancellation != assessment.cancellation ||
      quote.refund_budget_units != assessment.refund ||
      quote.formatted_refund != assessment.formatted_refund ||
      quote.message != assessment.message)
    return stale(locale_);
  auto current = context(frame);
  if (!live_binding(current, assessment.civilization_id, record.system_id,
                    record.body_id, assessment.colony_id))
    return stale(locale_);
  auto result = commit_surface_building_removal(current.command, assessment);
  if (result.accepted) {
    const auto building_name = localized_building_name(
        locale_, assessment.type_id, assessment.building_name);
    result.message = assessment.cancellation
        ? resolved(locale_, "SURFACE_REMOVAL_CANCELLED",
                   {building_name, assessment.formatted_refund},
                   "{0} construction cancelled. {1} was recovered; spent "
                   "industry was not recoverable.")
        : resolved(locale_, "SURFACE_REMOVAL_DEMOLISHED",
                   {building_name},
                   "{0} demolished. Its power use and production have "
                   "stopped.");
  } else
    result.message = localized_denial(locale_, result.message);
  return {result.accepted, result.message};
}

NativeSurfaceManagementQuote
NativeSurfaceConstructionController::preview_management(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const NativeSurfaceManagementAction action,
    const int building_id, const bool value) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  if (next_quote_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("No surface quote revision is available.");
  auto current = context(frame);
  NativeSurfaceManagementQuote result;
  result.campaign_generation = generation;
  result.colony_revision = view.revision;
  result.quote_revision = next_quote_revision_++;
  result.player_civilization_id = view.player_civilization_id;
  result.system_id = view.system_id;
  result.body_id = view.body_id;
  result.colony_id = view.colony_id;
  result.building_id = building_id;
  result.action = action;
  result.value = value;
  result.action_label = management_action_label(action, value, locale_);
  if (!bound_colony(current, view, generation)) {
    result.message =
        tr("SURFACE_MSG_MANAGE_CHANGED", "The owned known settlement changed; refresh it before managing it.");
    return result;
  }
  const auto *colony = find_one(current.world.colonies, view.colony_id,
                                &Colony::id);
  if (!is_management_action(action) || !colony ||
      !has_management_site(*colony, action, building_id)) {
    result.message = tr("SURFACE_MSG_TARGET_CHANGED", "The selected surface target changed; refresh it first.");
    return result;
  }
  result = assess_management(current, generation, view.revision, view.system_id,
                             view.body_id, view.colony_id, action, building_id,
                             value, locale_);
  result.quote_revision = next_quote_revision_ - 1;
  if (result.accepted)
    quotes_.emplace(result.quote_revision,
                    ManagementRecord{view.system_id, view.body_id, view.revision,
                                     result,
                                     management_snapshot(*colony, action,
                                                         building_id)});
  return result;
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::confirm_management(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeSurfaceManagementQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != generation ||
      quote.campaign_generation != generation)
    return stale(locale_);
  const auto found = quotes_.find(quote.quote_revision);
  if (found == quotes_.end() ||
      !std::holds_alternative<ManagementRecord>(found->second))
    return stale(locale_);
  const auto record = std::get<ManagementRecord>(found->second);
  quotes_.erase(found); // Tokens are single-use, including a rejected confirm.
  if (!same_management_quote(quote, record.quote)) return stale(locale_);

  auto current = context(frame);
  if (!live_binding(current, record.quote.player_civilization_id,
                    record.system_id, record.body_id, record.quote.colony_id))
    return stale(locale_);
  const auto *colony = find_one(current.world.colonies, record.quote.colony_id,
                                &Colony::id);
  if (!colony || management_snapshot(*colony, record.quote.action,
                                     record.quote.building_id) != record.snapshot)
    return stale(locale_);

  auto refreshed = assess_management(
      current, generation, record.colony_revision, record.system_id,
      record.body_id, record.quote.colony_id, record.quote.action,
      record.quote.building_id, record.quote.value, locale_);
  refreshed.quote_revision = record.quote.quote_revision;
  if (!same_management_quote(refreshed, record.quote)) return stale(locale_);

  quotes_.clear();
  const auto result = execute_management(
      current.command, current.player.id, record.quote.colony_id,
      record.quote.action, record.quote.building_id, record.quote.value,
      locale_);
  return {result.accepted, result.message};
}

bool NativeSurfaceConstructionController::cancel_quote(
    const std::uint64_t generation, const std::uint64_t quote_revision) {
  require_owner();
  if (!generation_ || *generation_ != generation) return false;
  return quotes_.erase(quote_revision) == 1;
}

} // namespace stellar::native_colony
