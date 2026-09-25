#include "native_logistics.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <exception>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace stellar::native_logistics {
namespace {
using namespace stellar::core;

std::string resolve(const engine::LocalizationTable *locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string kind_label(const LogisticsNodeKind kind,
                       const engine::LocalizationTable *locale) {
  switch (kind) {
    case LogisticsNodeKind::Homeworld:
      return resolve(locale, "SUPPLY_KIND_HOMEWORLD", "Homeworld");
    case LogisticsNodeKind::OrbitalHub:
      return resolve(locale, "SUPPLY_KIND_ORBITAL_HUB", "Orbital hub");
    case LogisticsNodeKind::LunarSettlement:
      return resolve(locale, "SUPPLY_KIND_LUNAR", "Lunar settlement");
    case LogisticsNodeKind::PlanetarySettlement:
      return resolve(locale, "SUPPLY_KIND_PLANETARY", "Planetary settlement");
    case LogisticsNodeKind::ResourceSite:
      return resolve(locale, "SUPPLY_KIND_RESOURCE", "Resource site");
    case LogisticsNodeKind::Depot:
      return resolve(locale, "SUPPLY_KIND_DEPOT", "Depot");
    case LogisticsNodeKind::Shipyard:
      return resolve(locale, "SUPPLY_KIND_SHIPYARD", "Shipyard");
  }
  return resolve(locale, "SUPPLY_KIND_NODE", "Node");
}

View unavailable(std::string message) {
  View view;
  view.message = std::move(message);
  return view;
}

const Civilization *valid_observer(const FreshCampaignState &campaign,
                                   const int civilization_id) {
  if (campaign.player_civilization_id != civilization_id) return nullptr;
  const auto observer = std::ranges::find(campaign.civilizations,
                                          civilization_id, &Civilization::id);
  if (observer == campaign.civilizations.end() || !observer->is_player)
    return nullptr;
  return &*observer;
}

bool has_economy(const FreshCampaignState &campaign, const int id) {
  return std::ranges::any_of(campaign.economies,
                             [id](const auto &item) {
                               return item.civilization_id == id;
                             });
}

bool has_construction(const FreshCampaignState &campaign, const int id) {
  return std::ranges::any_of(campaign.construction,
                             [id](const auto &item) {
                               return item.civilization_id == id;
                             });
}

std::optional<View> unavailable_for(const FreshCampaignState &campaign,
                                    const int civilization_id,
                                    const engine::LocalizationTable *locale) {
  const auto *observer = valid_observer(campaign, civilization_id);
  if (!observer)
    return unavailable(resolve(locale, "SUPPLY_ERR_OBSERVER",
                               "Supply network unavailable: player observer is missing or invalid."));
  if (std::ranges::find(campaign.systems, observer->home_system_id,
                        &StellarSystem::id) == campaign.systems.end())
    return unavailable(resolve(locale, "SUPPLY_ERR_HOME",
                               "Supply network unavailable: the observer home system is missing."));
  if (!has_economy(campaign, civilization_id))
    return unavailable(resolve(locale, "SUPPLY_ERR_ECONOMY",
                               "Supply network unavailable: economy state is missing. Reload the campaign or refresh when ready."));
  if (!has_construction(campaign, civilization_id))
    return unavailable(resolve(locale, "SUPPLY_ERR_CONSTRUCTION",
                               "Supply network unavailable: construction state is missing. Reload the campaign or refresh when ready."));
  return std::nullopt;
}

HomeSystemLogisticsNetwork canonical_project(const FreshCampaignState &campaign,
                                             const int civilization_id) {
  auto construction = economic_construction_projection(campaign.construction);
  auto fleets = economic_fleet_projection(campaign.fleets);
  const EconomyWorldView world{campaign.civilizations, campaign.bodies,
                               construction, fleets};
  return home_system_logistics(world, campaign.colonies, campaign.economies,
                               civilization_id);
}

View make_view(const FreshCampaignState &campaign, const int civilization_id,
               const HomeSystemLogisticsNetwork &network,
               const engine::LocalizationTable *locale) {
  const auto observer = std::ranges::find(campaign.civilizations,
                                          civilization_id, &Civilization::id);
  if (network.civilization_id != civilization_id ||
      observer == campaign.civilizations.end() ||
      network.home_system_id != observer->home_system_id)
    throw std::invalid_argument("Home logistics projection identity does not match the active observer.");
  View view;
  view.state = LoadState::Ready;
  const auto system = std::ranges::find(campaign.systems, network.home_system_id,
                                        &StellarSystem::id);
  view.system_name = system == campaign.systems.end()
                         ? resolve(locale, "SUPPLY_HOME_SYSTEM", "HOME SYSTEM")
                         : system->name;
  view.message = resolve(locale, "SUPPLY_MESSAGE_LEGEND",
                         "Available is exportable surplus; demand is required imports. Values are supply units per day.");
  view.corridor_count = static_cast<int>(network.links.size());
  view.supply_per_day = network.total_supply_offered_per_day;
  view.demand_per_day = network.total_demand_per_day;
  view.delivered_per_day = network.total_allocated_per_day;
  view.shortfall_per_day = network.total_unmet_demand_per_day;

  std::unordered_map<int, double> supply, demand, delivered;
  for (const auto &item : network.supply_offers) supply[item.node_id] += item.available_per_day;
  for (const auto &item : network.demands) demand[item.node_id] += item.required_per_day;
  for (const auto &item : network.daily_flow.allocations)
    delivered[item.destination_node_id] += item.allocated_per_day;
  std::unordered_map<int, std::string_view> sealed_names;
  for (const auto &node : network.nodes) {
    // Do not trust a malformed projection to disclose another civilization or
    // system. Core normally guarantees this, but presentation remains sealed.
    if (node.civilization_id != civilization_id ||
        node.system_id != network.home_system_id)
      continue;
    sealed_names.emplace(node.id, node.name);
    const double node_demand = demand[node.id];
    const double node_delivered = delivered[node.id];
    const std::string status =
        node_demand <= 0.0
            ? supply[node.id] <= 0.0
                  ? resolve(locale, "SUPPLY_STATUS_SELF", "Self-sufficient")
                  : resolve(locale, "SUPPLY_STATUS_NODE", "Supply node")
            : node_delivered + .0001 >= node_demand
                  ? resolve(locale, "SUPPLY_STATUS_FULL", "Fully supplied")
                  : resolve(locale, "SUPPLY_STATUS_SHORTFALL", "Shortfall");
    view.nodes.push_back({node.id, node.name, kind_label(node.kind, locale),
                          status, supply[node.id], node_demand,
                          node_delivered});
  }
  // Freight corridors: the canonical link graph plus per-link usage summed
  // from the day's flow allocations. Links touching a sealed node are dropped
  // rather than partially disclosed.
  std::unordered_map<int, double> link_used;
  for (const auto &allocation : network.daily_flow.allocations)
    for (const int link_id : allocation.route_link_ids)
      link_used[link_id] += allocation.allocated_per_day;
  for (const auto &link : network.links) {
    if (link.civilization_id != civilization_id) continue;
    const auto from = sealed_names.find(link.from_node_id);
    const auto to = sealed_names.find(link.to_node_id);
    if (from == sealed_names.end() || to == sealed_names.end()) continue;
    const double used = link_used[link.id];
    const char *key = "SUPPLY_LINK_IDLE";
    if (!link.enabled) key = "SUPPLY_LINK_DISABLED";
    else if (link.capacity_per_day <= .0001)
      key = used > .0001 ? "SUPPLY_LINK_SATURATED" : "SUPPLY_LINK_IDLE";
    else if (used >= link.capacity_per_day - .0001)
      key = "SUPPLY_LINK_SATURATED";
    else if (used >= link.capacity_per_day * .75) key = "SUPPLY_LINK_BUSY";
    else if (used > .0001) key = "SUPPLY_LINK_NORMAL";
    const char *fallback = !link.enabled ? "Disabled"
        : link.capacity_per_day <= .0001
              ? (used > .0001 ? "Saturated" : "Idle")
        : used >= link.capacity_per_day - .0001 ? "Saturated"
        : used >= link.capacity_per_day * .75   ? "Busy"
        : used > .0001                          ? "Normal"
                                                : "Idle";
    view.links.push_back({link.id, std::string(from->second),
                          std::string(to->second), resolve(locale, key, fallback),
                          link.capacity_per_day, used, link.transit_days,
                          link.enabled, link.bidirectional});
  }
  return view;
}

View failed(const std::exception &error,
            const engine::LocalizationTable *locale) {
  View view;
  view.state = LoadState::Failed;
  view.message = resolve(locale, "SUPPLY_ERR_FAILED",
                         "Supply network failed to load. Retry; if it persists, export diagnostics from the pause menu.");
  view.diagnostic = error.what();
  return view;
}
}  // namespace

View build_home_logistics(const FreshCampaignState &campaign,
                          const int civilization_id,
                          const engine::LocalizationTable *locale) {
  if (const auto absent = unavailable_for(campaign, civilization_id, locale))
    return *absent;
  try {
    return make_view(campaign, civilization_id,
                     canonical_project(campaign, civilization_id), locale);
  } catch (const std::exception &error) {
    return failed(error, locale);
  }
}

HomeLogisticsController::HomeLogisticsController()
    : projector_(canonical_project) {}

HomeLogisticsController::HomeLogisticsController(Projector projector)
    : projector_(projector ? std::move(projector) : Projector{canonical_project}) {}

bool HomeLogisticsController::refresh(const FreshCampaignState &campaign,
                                      const int civilization_id,
                                      const std::uint64_t generation,
                                      const bool explicit_retry) {
  const bool same_identity = civilization_id_ && generation_ &&
                             *civilization_id_ == civilization_id &&
                             *generation_ == generation;
  if (same_identity && failure_latched_ && !explicit_retry) return false;
  civilization_id_ = civilization_id;
  generation_ = generation;
  if (const auto absent = unavailable_for(campaign, civilization_id, locale_)) {
    view_ = *absent;
    failure_latched_ = false;
    return false;
  }
  ++attempted_refresh_count_;
  try {
    view_ = make_view(campaign, civilization_id,
                      projector_(campaign, civilization_id), locale_);
    ++successful_refresh_count_;
    failure_latched_ = false;
  } catch (const std::exception &error) {
    view_ = failed(error, locale_);
    failure_latched_ = true;
  }
  return true;
}

void HomeLogisticsController::clear() noexcept {
  view_ = View{};
  civilization_id_.reset();
  generation_.reset();
  failure_latched_ = false;
}

}  // namespace stellar::native_logistics
