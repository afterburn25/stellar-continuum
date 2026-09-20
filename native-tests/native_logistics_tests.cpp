#include "native_logistics.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>

#include <iostream>
#include <stdexcept>
#include <string_view>
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

HomeSystemLogisticsNetwork canonical(const FreshCampaignState &state, int id) {
  const auto construction = economic_construction_projection(state.construction);
  const auto fleets = economic_fleet_projection(state.fleets);
  return home_system_logistics({state.civilizations, state.bodies, construction,
                                fleets}, state.colonies, state.economies, id);
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
  require(view.supply_per_day == expected.total_supply_offered_per_day &&
              view.demand_per_day == expected.total_demand_per_day &&
              view.delivered_per_day == expected.total_allocated_per_day &&
              view.shortfall_per_day == expected.total_unmet_demand_per_day,
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
    result.home_system_id = 2;
    result.total_supply_offered_per_day = 42.;
    return result;
  });
  require(controller.refresh(state, 1, 9), "malformed projector was not attempted");
  require(controller.view().state == LoadState::Failed &&
              controller.view().supply_per_day == 0. &&
              controller.view().diagnostic.contains("identity"),
          "malformed projection exposed foreign home totals");
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
    identity_generation_and_clear_do_not_keep_stale_view();
    std::cout << "native logistics tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
