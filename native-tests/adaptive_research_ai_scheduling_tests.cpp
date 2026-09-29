// Verifies the adaptive-research AI's final-slot scheduling policy: when a
// civilization's directed program has a single free slot, an over-horizon
// candidate is deferred while a bounded-horizon alternative is startable and
// affordable, and is still started when nothing shorter qualifies.
#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/adaptive_research_campaign_simulation.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace stellar::core;
using Writer = detail::AdaptiveResearchStateWriter;
using Access = detail::AdaptiveResearchCampaignStateAccess;

constexpr std::string_view kFrontierNode = "planetary_deflection_network";
constexpr std::string_view kCheapNode = "systems_engineering";
constexpr int kCivilizationId = 7;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::pair<FreshCampaignState, AdaptiveResearchCampaignState>
build_world(const AdaptiveResearchCampaignFactory &factory, int civilization_id) {
  Civilization civilization{};
  civilization.id = civilization_id;
  civilization.species_id = "terran_baseline";
  FreshCampaignState world;
  world.seed = 17;
  world.civilizations.push_back(civilization);
  CivilizationEconomy economy{};
  economy.civilization_id = civilization_id;
  economy.credits = 1000000000.0;
  world.economies.push_back(economy);
  ConstructionState construction{};
  construction.civilization_id = civilization_id;
  world.construction.push_back(construction);
  auto campaign =
      factory.create(std::span<const Civilization>(&civilization, 1));
  return {std::move(world), std::move(campaign)};
}

void seed_two_candidate_shortlist(
    const AdaptiveResearchStrategicRuntime &runtime,
    AdaptiveResearchCivilizationState &state, bool include_cheap) {
  // Sixteen effective labs — the regime the scale campaign's pre-warp
  // civilizations actually run — leaves the frontier candidate several times
  // longer than a foundation candidate, so the final-slot commitment rule
  // should defer it.
  Writer::set_total_effective_research_labs(state, 16);
  // The simulation forces interstellar_distance >= 45 for pre-warp
  // civilizations, which materializes pressure-aware nodes mid-step. Prime the
  // same pressure up front so the investigable set is stable below.
  (void)runtime.authority().set_pressure(state, "interstellar_distance", 45);
  // Strong hazard pressure pushes the frontier candidate to the top of the
  // shortlist while the cheap candidate stays a startable alternative.
  (void)runtime.authority().set_pressure(state, "stellar_hazard", 100);
  std::vector<std::string> to_mature;
  for (const auto &node : state.node_states())
    if (node.maturity == ResearchMaturity::investigable &&
        node.node_id != kFrontierNode && node.node_id != kCheapNode)
      to_mature.push_back(node.node_id);
  for (const auto &node_id : to_mature)
    Writer::set_node_state(state, {node_id, ResearchMaturity::mature,
                                   std::nullopt, 0, 0, 0});
  // Established prerequisites so the frontier candidate is startable.
  Writer::set_node_state(state, {"asteroid_repositioning",
                                 ResearchMaturity::mature, std::nullopt, 0, 0,
                                 0});
  Writer::set_node_state(state, {"deep_space_radar", ResearchMaturity::mature,
                                 std::nullopt, 0, 0, 0});
  Writer::set_node_state(state, {std::string(kFrontierNode),
                                 ResearchMaturity::investigable, std::nullopt,
                                 0, 0, 0});
  Writer::set_node_state(state,
                         {std::string(kCheapNode),
                          include_cheap ? ResearchMaturity::investigable
                                        : ResearchMaturity::mature,
                          std::nullopt, 0, 0, 0});
}

std::string first_active_node(const AdaptiveResearchCivilizationState &state) {
  require(!state.active_projects().empty(),
          "AI selection produced no active project.");
  return state.active_projects().front().node_id;
}

int run(const std::filesystem::path &research_root) {
  auto runtime = load_adaptive_research_strategic_runtime(research_root);
  AdaptiveResearchCampaignFactory factory(runtime);
  AdaptiveResearchCampaignSimulation simulation;

  // Deferral: over-horizon top candidate is skipped for a startable bounded
  // alternative while starting it would consume the final directed slot.
  {
    auto [world, campaign] = build_world(factory, kCivilizationId);
    auto &state = Access::get_civilization(campaign, kCivilizationId);
    seed_two_candidate_shortlist(runtime, state, true);
    const auto shortlist = runtime.agenda().build_visible_shortlist(state);
    std::vector<const ResearchVisibleProjectCandidate *> startable;
    for (const auto &candidate : shortlist)
      if (candidate.can_start)
        startable.push_back(&candidate);
    require(startable.size() >= 2 &&
                startable.front()->node_id == kFrontierNode,
            "Frontier candidate did not top the startable shortlist.");
    double shortest = std::numeric_limits<double>::infinity();
    for (const auto *candidate : startable)
      shortest = std::min(shortest, candidate->estimated_years_to_mature);
    require(startable.front()->estimated_years_to_mature > 4.0 * shortest,
            "Frontier candidate was not disproportionately long.");
    require(startable.front()->estimated_years_to_mature > 2.0,
            "Frontier candidate was under the commitment floor.");
    const auto bounded = std::ranges::find_if(
        startable, [](const auto *candidate) {
          return candidate->estimated_years_to_mature <= 2.0;
        });
    require(bounded != startable.end() &&
            (*bounded)->node_id == std::string(kCheapNode),
            "Cheap bounded alternative missing from the shortlist.");
    (void)simulation.advance(world, campaign, 5.0, 36525.0);
    require(first_active_node(state) == std::string(kCheapNode),
            "Final-slot AI should defer the over-horizon candidate.");
  }

  // Fallback: with no bounded alternative the over-horizon candidate starts.
  {
    auto [world, campaign] = build_world(factory, kCivilizationId);
    auto &state = Access::get_civilization(campaign, kCivilizationId);
    seed_two_candidate_shortlist(runtime, state, false);
    const auto shortlist = runtime.agenda().build_visible_shortlist(state);
    const auto startable = std::ranges::find_if(
        shortlist, [](const auto &candidate) { return candidate.can_start; });
    require(startable != shortlist.end() &&
            startable->node_id == std::string(kFrontierNode),
            "Frontier candidate should be the only startable option.");
    (void)simulation.advance(world, campaign, 5.0, 36525.0);
    require(first_active_node(state) == std::string(kFrontierNode),
            "Final-slot AI should still start an over-horizon candidate when "
            "nothing shorter qualifies.");
  }

  std::cout << "adaptive_research_ai_scheduling_tests: 2 scenarios passed\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::invalid_argument("Expected research root path.");
    return run(std::filesystem::absolute(argv[1]));
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n';
    if (argc > 1)
      std::cerr << "research-root=" << std::filesystem::absolute(argv[1]).string()
                << '\n';
    return 1;
  }
}
