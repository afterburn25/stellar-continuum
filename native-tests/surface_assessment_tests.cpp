#include <stellar/core/surface_construction.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace stellar::core;

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool close(double left, double right) {
  return std::abs(left - right) <= 1e-9;
}

struct Owner {
  std::vector<Civilization> civilizations;
  std::vector<PlanetaryBody> bodies;
  std::vector<ConstructionState> construction;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<CivilizationConstructionCapabilities> capabilities;

  ConstructionWorld world() {
    return {civilizations, bodies, construction, colonies, economies,
            capabilities};
  }
  ConstructionReadView read() const {
    return {civilizations, bodies, construction, colonies, economies,
            capabilities};
  }
};

Owner make_owner() {
  Owner owner;
  Civilization civilization;
  civilization.id = 1;
  civilization.name = "Assessment Test Civilization";
  civilization.is_player = true;
  civilization.species_id = "terran_baseline";
  owner.civilizations.push_back(std::move(civilization));

  PlanetaryBody body;
  body.id = 7;
  body.system_id = 3;
  body.name = "Assessment Test World";
  body.kind = PlanetaryBodyKind::Planet;
  body.radius_earth = 1.0;
  body.mass_earth = 1.0;
  body.environment = {1.0,
                      288.0,
                      101.325,
                      PlanetaryAtmosphereRegime::OxygenNitrogen,
                      PlanetarySolventRegime::Water,
                      0.0,
                      false,
                      true};
  owner.bodies.push_back(std::move(body));

  Colony colony;
  colony.id = 11;
  colony.civilization_id = 1;
  colony.system_id = 3;
  colony.planetary_body_id = 7;
  colony.name = "Assessment Test Colony";
  colony.surface_hub_level = 2;
  owner.colonies.push_back(std::move(colony));

  CivilizationEconomy economy;
  economy.civilization_id = 1;
  economy.credits = 1000.0;
  economy.industry = 800.0;
  owner.economies.push_back(economy);
  owner.construction.push_back({1});
  owner.capabilities.push_back({1});
  return owner;
}

void require_same_surface_state(const Owner &left, const Owner &right,
                                const std::string &context) {
  require(left.colonies.size() == right.colonies.size(),
          context + ": colony count changed");
  require(left.economies.size() == right.economies.size(),
          context + ": economy count changed");
  const auto &a = left.colonies.front();
  const auto &b = right.colonies.front();
  require(a.surface_buildings.size() == b.surface_buildings.size(),
          context + ": building count changed");
  for (std::size_t index = 0; index < a.surface_buildings.size(); ++index) {
    const auto &x = a.surface_buildings[index];
    const auto &y = b.surface_buildings[index];
    require(x.id == y.id && x.type_id == y.type_id && close(x.x, y.x) &&
                close(x.z, y.z) &&
                close(x.rotation_degrees, y.rotation_degrees) &&
                close(x.industry_progress, y.industry_progress) &&
                x.is_complete == y.is_complete,
            context + ": building state changed");
  }
  require(close(left.economies.front().credits,
                right.economies.front().credits) &&
              close(left.economies.front().industry,
                    right.economies.front().industry),
          context + ": economy changed");
}

void placement_assessment_is_immutable_and_matches_command() {
  auto assessed = make_owner();
  const auto before = assessed;
  const auto quote = assess_surface_building_placement(
      assessed.read(), 1, 11, "power_generator", 100.0F, 100.0F, -15.0F);
  require(quote.accepted, "expected valid placement assessment");
  require(quote.prepared_building_id == 1,
          "placement assessment did not reserve the canonical next id");
  require(close(quote.normalized_rotation_degrees, 345.0),
          "placement assessment did not normalize rotation");
  require(close(quote.authorization_cost, 25.0) &&
              close(quote.industry_cost, 300.0) &&
              quote.building_name == "Power generator" &&
              !quote.formatted_authorization.empty(),
          "placement assessment did not retain the canonical authorization");
  require_same_surface_state(assessed, before,
                             "placement assessment mutated the world");

  auto committed = assessed;
  auto direct = assessed;
  const auto commit_result =
      commit_surface_building_placement(committed.world(), quote);
  const auto direct_result = place_surface_building(
      direct.world(), 1, 11, "power_generator", 100.0F, 100.0F, -15.0F);
  require(commit_result.accepted == direct_result.accepted &&
              commit_result.message == direct_result.message,
          "placement assessment commit differs from the direct command");
  require_same_surface_state(committed, direct,
                             "placement assessment commit world differs");
}

void placement_commit_revalidates_current_terms() {
  auto owner = make_owner();
  const auto quote = assess_surface_building_placement(
      owner.read(), 1, 11, "power_generator", 100.0F, 100.0F, 5.0F);
  require(quote.accepted, "expected stale placement fixture quote");
  owner.colonies.front().surface_buildings.push_back(
      {9, "power_generator", -180.0F, -180.0F, 0.0F});
  const auto before = owner;
  const auto result = commit_surface_building_placement(owner.world(), quote);
  require(!result.accepted &&
              result.message ==
                  "Surface placement terms changed; review the current quote.",
          "stale placement did not require a refreshed quote");
  require_same_surface_state(owner, before, "stale placement mutated the world");

  auto overlap = make_owner();
  const auto first = assess_surface_building_placement(
      overlap.read(), 1, 11, "power_generator", 100.0F, 100.0F, 0.0F);
  require(commit_surface_building_placement(overlap.world(), first).accepted,
          "first placement failed");
  const auto placed = overlap;
  const auto repeated =
      commit_surface_building_placement(overlap.world(), first);
  require(!repeated.accepted &&
              repeated.message.find("overlaps") != std::string::npos,
          "repeated placement did not return the current canonical denial");
  require_same_surface_state(overlap, placed,
                             "repeated placement mutated the world");

  auto funding = make_owner();
  const auto funded_quote = assess_surface_building_placement(
      funding.read(), 1, 11, "power_generator", 100.0F, 100.0F, 0.0F);
  require(funded_quote.accepted, "expected funded placement quote");
  funding.economies.front().credits = 0.0;
  const auto unfunded = funding;
  const auto denied =
      commit_surface_building_placement(funding.world(), funded_quote);
  require(!denied.accepted && denied.message.find("required") !=
                                  std::string::npos,
          "funding loss did not return the current canonical denial");
  require_same_surface_state(funding, unfunded,
                             "funding-loss placement mutated the world");
}

Owner owner_with_site(bool complete) {
  auto owner = make_owner();
  const auto placed = place_surface_building(
      owner.world(), 1, 11, "power_generator", 100.0F, 100.0F, 0.0F);
  require(placed.accepted, "removal fixture placement failed");
  auto &site = owner.colonies.front().surface_buildings.front();
  site.industry_progress = complete ? 300.0 : 75.0;
  site.is_complete = complete;
  return owner;
}

void incomplete_removal_quote_matches_command_without_industry_refund() {
  auto assessed = owner_with_site(false);
  const auto before = assessed;
  const auto quote =
      assess_surface_building_removal(assessed.read(), 1, 11, 1);
  require(quote.accepted && quote.cancellation,
          "incomplete site was not assessed as a cancellation");
  require(close(quote.refund, 12.5) && !quote.formatted_refund.empty(),
          "cancellation quote did not retain the exact half authorization");
  require(quote.message.find("spent industry was not recoverable") !=
              std::string::npos,
          "cancellation quote omitted the no-industry-refund consequence");
  require_same_surface_state(assessed, before,
                             "removal assessment mutated the world");

  auto committed = assessed;
  auto direct = assessed;
  const auto committed_result =
      commit_surface_building_removal(committed.world(), quote);
  const auto direct_result =
      remove_surface_building(direct.world(), 1, 11, 1);
  require(committed_result.accepted == direct_result.accepted &&
              committed_result.message == direct_result.message,
          "removal assessment commit differs from direct command");
  require_same_surface_state(committed, direct,
                             "removal assessment commit world differs");
  require(close(committed.economies.front().industry, 800.0),
          "cancellation refunded spent industry");
  require(close(committed.economies.front().credits, 987.5),
          "cancellation did not preserve the original half-refund result");
}

void completed_removal_has_no_refund_or_treasury_write() {
  auto owner = owner_with_site(true);
  const auto quote =
      assess_surface_building_removal(owner.read(), 1, 11, 1);
  require(quote.accepted && !quote.cancellation && close(quote.refund, 0.0) &&
              quote.formatted_refund.empty(),
          "completed demolition exposed a refund");
  const auto credits = owner.economies.front().credits;
  const auto result = commit_surface_building_removal(owner.world(), quote);
  require(result.accepted && owner.colonies.front().surface_buildings.empty(),
          "completed demolition failed");
  require(close(owner.economies.front().credits, credits),
          "completed demolition changed the treasury");
}

void removal_commit_revalidates_completion_state() {
  auto owner = owner_with_site(false);
  const auto quote =
      assess_surface_building_removal(owner.read(), 1, 11, 1);
  require(quote.accepted && quote.cancellation,
          "expected cancellation quote for stale removal test");
  auto &site = owner.colonies.front().surface_buildings.front();
  site.is_complete = true;
  site.industry_progress = 300.0;
  const auto before = owner;
  const auto result = commit_surface_building_removal(owner.world(), quote);
  require(!result.accepted &&
              result.message ==
                  "Surface removal terms changed; review the current quote.",
          "stale removal did not require a refreshed quote");
  require_same_surface_state(owner, before, "stale removal mutated the world");
}

void rejected_assessments_do_not_mutate() {
  auto owner = make_owner();
  owner.economies.front().credits = 0.0;
  const auto before = owner;
  const auto placement = assess_surface_building_placement(
      owner.read(), 1, 11, "power_generator", 100.0F, 100.0F, 0.0F);
  require(!placement.accepted && placement.message.find("required") !=
                                     std::string::npos,
          "insufficient funding placement assessment was accepted");
  auto direct_owner = owner;
  const auto direct = place_surface_building(
      direct_owner.world(), 1, 11, "power_generator", 100.0F, 100.0F, 0.0F);
  require(!direct.accepted && direct.message == placement.message,
          "rejected placement assessment differs from direct command");
  require_same_surface_state(direct_owner, owner,
                             "rejected direct command mutated the world");
  const auto removal =
      assess_surface_building_removal(owner.read(), 1, 11, 999);
  require(!removal.accepted && removal.message.find("no longer exists") !=
                                   std::string::npos,
          "missing building removal assessment was accepted");
  require_same_surface_state(owner, before,
                             "rejected assessment mutated the world");
}

void malformed_currency_preserves_legacy_direct_order() {
  auto assessed = owner_with_site(false);
  assessed.civilizations.clear();
  const auto assessment_before = assessed;
  bool assessment_threw = false;
  try {
    (void)assess_surface_building_removal(assessed.read(), 1, 11, 1);
  } catch (const std::runtime_error &) {
    assessment_threw = true;
  }
  require(assessment_threw,
          "malformed currency assessment did not report the source error");
  require_same_surface_state(assessed, assessment_before,
                             "throwing assessment mutated the world");

  auto direct = assessment_before;
  bool direct_threw = false;
  try {
    (void)remove_surface_building(direct.world(), 1, 11, 1);
  } catch (const std::runtime_error &) {
    direct_threw = true;
  }
  require(direct_threw,
          "malformed direct removal did not retain the source error");
  require(direct.colonies.front().surface_buildings.empty(),
          "malformed direct removal changed the legacy erase order");
  require(close(direct.economies.front().credits, 987.5),
          "malformed direct removal changed the legacy refund order");
}
} // namespace

int main() {
  try {
    placement_assessment_is_immutable_and_matches_command();
    placement_commit_revalidates_current_terms();
    incomplete_removal_quote_matches_command_without_industry_refund();
    completed_removal_has_no_refund_or_treasury_write();
    removal_commit_revalidates_completion_state();
    rejected_assessments_do_not_mutate();
    malformed_currency_preserves_legacy_direct_order();
    std::cout << "surface assessment tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
