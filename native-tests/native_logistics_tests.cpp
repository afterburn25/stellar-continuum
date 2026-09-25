#include "native_logistics.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/engine/localization.hpp>

#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {
using namespace stellar::core;
using namespace stellar::native_logistics;

void require(const bool value, const std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

FreshCampaignState world(const int home_nodes = 2) {
  FreshCampaignState result;
  result.player_civilization_id = 1;
  result.systems = {{1, "Sol", {0.f, 0.f}}, {2, "Foreign", {10.f, 0.f}}};
  result.civilizations = {{1, "Player", 1}, {2, "Other", 2}};
  result.civilizations.front().is_player = true;
  result.economies = {{1}, {2}};
  result.construction = {{1}, {2}};
  for (int index = 0; index < home_nodes; ++index) {
    Colony colony;
    colony.id = index + 1;
    colony.civilization_id = 1;
    colony.system_id = 1;
    colony.name = "Home " + std::to_string(index + 1);
    colony.population_millions = 100. + index;
    colony.infrastructure = 1.;
    colony.stability = 1.;
    result.colonies.push_back(std::move(colony));
  }
  return result;
}

CivilizationLogisticsCoverage canonical(const FreshCampaignState &state, int id) {
  const auto construction = economic_construction_projection(state.construction);
  const auto fleets = economic_fleet_projection(state.fleets);
  return civilization_logistics_coverage({state.civilizations, state.bodies,
                                          construction, fleets},
                                         state.colonies, state.economies, id);
}

void observer_and_missing_state_are_unavailable() {
  auto state = world();
  state.civilizations.front().is_player = false;
  require(build_home_logistics(state, 1).state == LoadState::Unavailable,
          "invalid observer was projected as healthy data");
  state = world();
  state.economies.clear();
  const auto view = build_home_logistics(state, 1);
  require(view.state == LoadState::Unavailable && !view.message.empty(),
          "missing economy did not give useful unavailable state");
  require(view.message.contains("missing") && view.message.contains("Reload"),
          "missing economy message did not provide a recovery action");
  state = world();
  state.player_civilization_id = 2;
  require(build_home_logistics(state, 1).state == LoadState::Unavailable,
          "a flagged player who was not the active observer was accepted");
}

void home_only_and_canonical_totals() {
  auto state = world(3);
  const auto expected = canonical(state, 1);
  const auto view = build_home_logistics(state, 1);
  require(view.state == LoadState::Ready, "valid home network did not load");
  require(view.supply_per_day == expected.home_system.total_supply_offered_per_day &&
              view.demand_per_day == expected.home_system.total_demand_per_day &&
              view.delivered_per_day == expected.home_system.total_allocated_per_day &&
              view.shortfall_per_day == expected.home_system.total_unmet_demand_per_day,
          "view totals were recomputed instead of copied from Core");
  const auto baseline_nodes = view.nodes.size();
  Colony foreign;
  foreign.id = 999;
  foreign.civilization_id = 2;
  foreign.system_id = 2;
  foreign.name = "Foreign leak";
  foreign.population_millions = 9000.;
  state.colonies.push_back(foreign);
  state.economies[1].last_industry_per_second = 999999.;
  state.construction[1].completed_project_ids.push_back("orbital_shipyard");
  const auto after = build_home_logistics(state, 1);
  require(after.nodes.size() == baseline_nodes,
          "foreign settlement leaked into home node rows");
  for (const auto &node : after.nodes)
    require(node.name != "Foreign leak", "foreign node name leaked");
}

void all_nodes_are_preserved() {
  const auto view = build_home_logistics(world(11), 1);
  require(view.state == LoadState::Ready && view.nodes.size() == 11,
          "home nodes were truncated for presentation");
}

void failure_latches_until_explicit_retry() {
  auto state = world();
  int calls{};
  HomeLogisticsController controller([&calls](const FreshCampaignState &value,
                                              int id) {
    ++calls;
    if (calls == 1) throw std::runtime_error("test projector failure");
    return canonical(value, id);
  });
  require(controller.refresh(state, 1, 4), "first projection was not attempted");
  require(controller.view().state == LoadState::Failed &&
              controller.view().diagnostic == "test projector failure",
          "failed projection did not retain diagnostic");
  require(!controller.refresh(state, 1, 4) && calls == 1,
          "same failed identity retried without user request");
  require(controller.refresh(state, 1, 4, true) && calls == 2 &&
              controller.view().state == LoadState::Ready,
          "explicit retry did not retry and recover");
}

void malformed_projector_identity_fails_without_foreign_totals() {
  auto state = world();
  HomeLogisticsController controller([](const FreshCampaignState &value, int id) {
    auto result = canonical(value, id);
    result.civilization_id = 2;
    result.home_system.civilization_id = 2;
    result.home_system.home_system_id = 2;
    result.home_system.total_supply_offered_per_day = 42.;
    return result;
  });
  require(controller.refresh(state, 1, 9), "malformed projector was not attempted");
  require(controller.view().state == LoadState::Failed &&
              controller.view().supply_per_day == 0. &&
              controller.view().diagnostic.contains("identity"),
          "malformed projection exposed foreign home totals");
}

void locale_resolves_node_labels_and_unavailable_message() {
  stellar::engine::LocalizationTable locale{"en", "en"};
  require(locale.load_json(
              R"({"locale":"en","strings":{
                "SUPPLY_MESSAGE_LEGEND":"LEGENDE",
                "SUPPLY_KIND_HOMEWORLD":"KIND-A","SUPPLY_KIND_ORBITAL_HUB":"KIND-B",
                "SUPPLY_KIND_LUNAR":"KIND-C","SUPPLY_KIND_PLANETARY":"KIND-D",
                "SUPPLY_KIND_RESOURCE":"KIND-E","SUPPLY_KIND_DEPOT":"KIND-F",
                "SUPPLY_KIND_SHIPYARD":"KIND-G","SUPPLY_KIND_NODE":"KIND-H",
                "SUPPLY_STATUS_SELF":"STATUS-A","SUPPLY_STATUS_NODE":"STATUS-B",
                "SUPPLY_STATUS_FULL":"STATUS-C","SUPPLY_STATUS_SHORTFALL":"STATUS-D",
                "SUPPLY_ERR_ECONOMY":"WIRTSCHAFT FEHLT"}})"),
          "locale fixture did not parse");
  const auto ready = build_home_logistics(world(2), 1, &locale);
  require(ready.state == LoadState::Ready && ready.message == "LEGENDE" &&
              !ready.nodes.empty(),
          "locale did not resolve the ready-state legend");
  const auto localized = [](const std::string &value, const char *prefix) {
    return value.starts_with(prefix);
  };
  for (const auto &node : ready.nodes) {
    require(localized(node.kind_label, "KIND-"),
            "locale did not resolve a node kind label");
    require(localized(node.status, "STATUS-"),
            "locale did not resolve a node status label");
  }
  auto missing = world();
  missing.economies.clear();
  require(build_home_logistics(missing, 1, &locale).message == "WIRTSCHAFT FEHLT",
          "locale did not resolve the unavailable message");
  HomeLogisticsController controller;
  controller.set_localization(&locale);
  require(controller.refresh(missing, 1, 7) == false &&
              controller.view().message == "WIRTSCHAFT FEHLT",
          "controller locale did not resolve the unavailable message");
}

void corridors_match_the_canonical_link_graph() {
  const auto state = world(3);
  const auto expected = canonical(state, 1);
  const auto view = build_home_logistics(state, 1);
  require(view.state == LoadState::Ready,
          "valid home network did not load for corridor check");
  require(view.links.size() == expected.home_system.links.size(),
          "corridor rows diverged from the canonical link graph");
  std::unordered_map<int, double> expected_used;
  for (const auto &allocation : expected.home_system.daily_flow.allocations)
    for (const int link : allocation.route_link_ids)
      expected_used[link] += allocation.allocated_per_day;
  for (const auto &row : view.links) {
    const auto link = std::ranges::find(expected.home_system.links, row.id,
                                        &LogisticsLink::id);
    require(link != expected.home_system.links.end() && !row.from.empty() &&
                !row.to.empty() && !row.status.empty(),
            "corridor row lost its canonical link, endpoints or status");
    require(row.capacity_per_day == link->capacity_per_day &&
                row.transit_days == link->transit_days &&
                row.enabled == link->enabled &&
                row.bidirectional == link->bidirectional,
            "corridor row rewrote canonical link metrics");
    require(row.used_per_day == expected_used[row.id],
            "corridor usage diverged from flow allocations");
  }
  if (expected.home_system.links.empty())
    std::cout << "note: canonical home network had no links in the fixture\n";
}

void external_coverage_projects_owned_distant_systems() {
  auto state = world(2);
  // A second owned system gives the canonical coverage an external entry.
  state.systems.push_back({3, "Frontier", {30.f, 0.f}});
  Colony distant;
  distant.id = 100;
  distant.civilization_id = 1;
  distant.system_id = 3;
  distant.name = "Frontier Colony";
  distant.population_millions = 55.;
  distant.infrastructure = 1.;
  distant.stability = 1.;
  state.colonies.push_back(distant);
  const auto expected = canonical(state, 1);
  const auto view = build_home_logistics(state, 1);
  require(view.state == LoadState::Ready,
          "coverage fixture did not load as ready");
  require(view.external.size() == expected.external_systems.size() &&
              !view.external.empty(),
          "external systems were truncated from the view");
  require(view.owned_system_count == expected.owned_system_count &&
              view.support_gap_per_day ==
                  expected.unrepresented_interstellar_support_per_day,
          "coverage totals were recomputed instead of copied from Core");
  for (const auto &row : view.external) {
    const auto entry = std::ranges::find(
        expected.external_systems, row.system_id,
        &ExternalSystemLogisticsStatus::system_id);
    require(entry != expected.external_systems.end() && !row.name.empty() &&
                !row.status.empty(),
            "external row lost its canonical entry, name or status");
    require(row.colony_count == entry->colony_count &&
                row.condition == entry->condition &&
                row.capacity_per_day == entry->local_support_capacity_per_day &&
                row.demand_per_day == entry->support_demand_per_day &&
                row.import_per_day == entry->import_requirement_per_day &&
                row.corridor ==
                    entry->has_represented_interstellar_freight_corridor,
            "external row rewrote canonical coverage metrics");
    require(row.name == "Frontier",
            "external row did not resolve the owning system's name");
  }
  // Foreign-owned external colonies and the home system itself stay out.
  require(std::ranges::none_of(view.external,
                               [](const auto &row) {
                                 return row.name == "Foreign" ||
                                        row.name == "Sol";
                               }),
          "foreign or home system leaked into external coverage rows");
}

void identity_generation_and_clear_do_not_keep_stale_view() {
  auto state = world();
  HomeLogisticsController controller;
  require(controller.refresh(state, 1, 1) && controller.view().state == LoadState::Ready,
          "baseline view did not load");
  require(controller.refresh(state, 1, 2),
          "generation change did not get a fresh projection");
  controller.clear();
  require(controller.view().state == LoadState::Unavailable &&
              controller.view().nodes.empty(),
          "clear retained stale logistics data");
  require(controller.refresh(state, 1, 1),
          "clear did not remove identity/generation binding");
}
}  // namespace

int main() {
  try {
    observer_and_missing_state_are_unavailable();
    home_only_and_canonical_totals();
    all_nodes_are_preserved();
    failure_latches_until_explicit_retry();
    malformed_projector_identity_fails_without_foreign_totals();
    locale_resolves_node_labels_and_unavailable_message();
    corridors_match_the_canonical_link_graph();
    external_coverage_projects_owned_distant_systems();
    identity_generation_and_clear_do_not_keep_stale_view();
    std::cout << "native logistics tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
