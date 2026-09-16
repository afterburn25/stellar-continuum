#include "native_research_controller.hpp"
#include "native_research_presentation.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_recovery.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;
using namespace stellar::native_research;

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read '" + path.string() + "'.");
  std::ostringstream result;
  result << input.rdbuf();
  return result.str();
}

void write(const fs::path &path, const std::string &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output) throw std::runtime_error("Cannot write test campaign.");
}

[[nodiscard]] std::string source_player17(const fs::path &fixture_path) {
  const auto fixture = Json::parse(read(fixture_path));
  for (const auto &row : fixture.at("Rows"))
    if (row.at("Name") == "valid-current17")
      return row.at("InputJson").get<std::string>();
  throw std::runtime_error("Actual-source Player17 fixture lacks valid-current17.");
}

[[nodiscard]] CampaignFrame source_frame(const fs::path &research_root,
                                         const fs::path &fixture_path,
                                         const fs::path &scratch) {
  const auto path = scratch / "source-player17.json";
  write(path, source_player17(fixture_path));
  auto loaded = load_existing_player_campaign_v17(
      path,
      [research_root] {
        return load_adaptive_research_strategic_runtime(research_root);
      });
  StrategicClock clock;
  clock.restore(loaded.campaign.simulation_days());
  return CampaignFrame(std::move(loaded.campaign).activate(), std::move(clock),
                       CampaignFramePolicy::Player);
}

[[nodiscard]] CampaignFrame fresh_500_frame(const fs::path &research_root,
                                            const fs::path &catalog_path,
                                            std::string species =
                                                "terran_baseline") {
  auto world = seed_persistable_fresh_campaign(
      103500, load_nearby_catalog(catalog_path),
      {"2044-05-06T07:08:09Z", 500, 6, 1, std::move(species)});
  auto research_runtime =
      load_adaptive_research_strategic_runtime(research_root);
  auto research = AdaptiveResearchCampaignFactory(research_runtime).create(world);
  auto snapshot =
      AdaptiveResearchCampaignSnapshotCodec(research_runtime).capture(research);
  const auto civilization = std::ranges::find(
      snapshot.civilizations, world.player_civilization_id,
      &AdaptiveResearchCampaignCivilizationSnapshot::civilization_id);
  require(civilization != snapshot.civilizations.end(),
          "Fresh player research snapshot is missing.");
  auto &nodes = civilization->research.research.research.research.core.nodes;
  const auto locked = std::ranges::find_if(
      research_runtime.authority().catalog().nodes(), [&](const auto &definition) {
        return std::ranges::none_of(nodes, [&](const auto &node) {
          return node.node_id == definition.id;
        });
      });
  require(locked != research_runtime.authority().catalog().nodes().end(),
          "Fresh player unexpectedly knows the complete research catalog.");
  nodes.push_back({locked->id, ResearchMaturity::rumored, std::nullopt, 0., 0.});
  return CampaignFrame(IntegratedAdaptiveCampaignRuntime::restore_research(
                           std::move(research_runtime), std::move(world), snapshot,
                           {}, 0.),
                       StrategicClock{}, CampaignFramePolicy::Player);
}

[[nodiscard]] std::set<std::string> detailed_ids(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  const auto &world = runtime.world().campaign();
  const auto &state = runtime.research().get_civilization(
      world.player_civilization_id);
  const auto civilization = std::ranges::find(
      world.civilizations, world.player_civilization_id, &Civilization::id);
  require(civilization != world.civilizations.end(),
          "Player civilization is missing.");
  const auto target = "species:" + civilization->species_id;
  const auto view = runtime.research_runtime().authority().kernel().build_view(
      state, target);
  std::set<std::string> result;
  for (const auto &node : view.visible_nodes)
    if (node.state >= ResearchMaturity::investigable)
      result.insert(node.node_id);
  return result;
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>>
recognized_undetailed_node(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  const auto &world = runtime.world().campaign();
  const auto &state = runtime.research().get_civilization(
      world.player_civilization_id);
  const auto civilization = std::ranges::find(
      world.civilizations, world.player_civilization_id, &Civilization::id);
  require(civilization != world.civilizations.end(),
          "Player civilization is missing.");
  const auto view = runtime.research_runtime().authority().kernel().build_view(
      state, "species:" + civilization->species_id);
  const auto node = std::ranges::find_if(view.visible_nodes, [](const auto &entry) {
    return entry.state < ResearchMaturity::investigable;
  });
  if (node == view.visible_nodes.end()) return std::nullopt;
  return std::pair{node->node_id, node->display_name};
}

[[nodiscard]] CivilizationEconomy &player_economy(CampaignFrame &frame) {
  auto &world = frame.runtime().world().campaign();
  const auto found = std::ranges::find(world.economies,
                                       world.player_civilization_id,
                                       &CivilizationEconomy::civilization_id);
  require(found != world.economies.end(), "Player economy is missing.");
  return *found;
}

[[nodiscard]] bool observer_safe_projection(
    CampaignFrame &frame, const std::uint64_t generation) {
  NativeResearchController controller;
  const auto authoritative = detailed_ids(frame);
  const auto window = controller.build(frame, generation);
  require(!window.nodes.empty() && window.nodes.size() == authoritative.size(),
          "Adapter did not project the complete detailed research workspace.");
  for (const auto &node : window.nodes)
    require(authoritative.contains(node.id),
            "Adapter exposed a node below the source workspace detail threshold.");
  const auto undetailed = recognized_undetailed_node(frame);
  if (undetailed) {
    const auto &[undetailed_id, undetailed_name] = *undetailed;
    require(std::ranges::none_of(window.nodes, [&](const auto &node) {
              return node.id == undetailed_id;
            }) &&
                controller.build(frame, generation, {{}, undetailed_id}).nodes.empty() &&
                controller.build(frame, generation, {{}, undetailed_name}).nodes.empty(),
            "An anonymous locked preview leaked its identifier or name.");
  }
  require(std::ranges::any_of(window.nodes, [](const auto &node) {
            return node.maturity == ResearchMaturity::mature &&
                   node.stage_progress == 1. && !node.cost;
          }),
          "Completed established knowledge was not represented safely.");

  const auto &catalog = frame.runtime().research_runtime().authority().catalog();
  const auto hidden = std::ranges::find_if(catalog.nodes(), [&](const auto &node) {
    return !authoritative.contains(node.id) &&
           controller.build(frame, generation, {{}, node.id}).nodes.empty() &&
           controller.build(frame, generation, {{}, node.name}).nodes.empty();
  });
  require(hidden != catalog.nodes().end(),
          "Representative campaign unexpectedly knows the whole catalog.");
  const auto hidden_result = controller.build(frame, generation, {{}, hidden->id});
  require(hidden_result.nodes.empty(),
          "Searching a hidden identifier revealed future research.");
  const auto hidden_command = controller.execute(
      frame, generation, window.research_revision, window.funding_revision,
      NativeResearchIntent::Start, hidden->id);
  require(!hidden_command.accepted &&
              hidden_command.message.find(hidden->id) == std::string::npos &&
              hidden_command.message.find(hidden->name) == std::string::npos,
          "Hidden command rejection leaked future research identity.");

  require(window.domain_tabs.size() > 1,
          "Known research did not produce domain tabs.");
  const auto &domain = window.domain_tabs[1];
  const auto domain_result =
      controller.build(frame, generation, {domain.id, {}});
  require(!domain_result.nodes.empty() &&
              std::ranges::all_of(domain_result.nodes, [&](const auto &node) {
                return research_category_matches(domain.id, node.domain_id);
              }),
          "Category filtering escaped its observer-safe known domains.");

  bool invalid_utf8_rejected{};
  try {
    (void)controller.build(frame, generation, {{}, std::string("\xc3", 1)});
  } catch (const std::invalid_argument &) {
    invalid_utf8_rejected = true;
  }
  require(invalid_utf8_rejected,
          "Malformed UTF-8 research search was not rejected.");

  controller.select(window.nodes.front().id);
  require(controller.selection().has_value(), "Research selection was not retained.");
  const auto replaced = controller.build(frame, generation + 1);
  require(!replaced.selected_node_id && !controller.selection(),
          "Campaign generation replacement retained stale research selection.");
  const auto stale = controller.execute(
      frame, generation, replaced.research_revision,
      replaced.funding_revision, NativeResearchIntent::Start,
      window.nodes.front().id);
  require(!stale.accepted && stale.message.find("campaign changed") != std::string::npos,
          "Stale campaign command was not rejected before mutation.");
  return undetailed.has_value();
}

void command_lifecycle(CampaignFrame &frame) {
  NativeResearchController controller;
  constexpr std::uint64_t generation = 7;
  const auto original_credit = player_economy(frame).credits;
  player_economy(frame).credits = std::max(1'000'000., original_credit);
  auto window = controller.build(frame, generation);
  const auto candidate = std::ranges::find_if(window.nodes, [](const auto &node) {
    return node.primary_action.intent == NativeResearchIntent::Start &&
           node.primary_action.enabled && node.cost;
  });
  require(candidate != window.nodes.end(),
          "Real 500-system campaign has no known start candidate.");
  const auto node_id = candidate->id;
  player_economy(frame).credits = 0.;
  window = controller.build(frame, generation);
  const auto denied_node = std::ranges::find(window.nodes, node_id,
                                             &NativeResearchNode::id);
  require(denied_node != window.nodes.end() && denied_node->cost &&
              !denied_node->primary_action.enabled,
          "Authoritative cost was not shown with precommit denial.");
  const auto before_denial_revision = window.research_revision;
  const auto denied = controller.execute(
      frame, generation, before_denial_revision, window.funding_revision,
      NativeResearchIntent::Start, node_id);
  require(!denied.accepted && denied.research_revision == before_denial_revision &&
              player_economy(frame).credits == 0.,
          "Rejected start mutated research or treasury state.");

  player_economy(frame).credits = std::max(1'000'000., original_credit);
  window = controller.build(frame, generation);
  const auto started = controller.execute(
      frame, generation, window.research_revision, window.funding_revision,
      NativeResearchIntent::Start, node_id);
  require(started.accepted, "Canonical Core rejected a funded start candidate: " +
                                started.message);
  (void)frame.advance(1.);
  window = controller.build(frame, generation);
  const auto active = std::ranges::find(window.nodes, node_id,
                                        &NativeResearchNode::id);
  require(active != window.nodes.end() && active->active && !active->paused &&
              active->stage_progress > 0. &&
              active->primary_action.intent == NativeResearchIntent::Pause,
          "Started research did not expose authoritative active progress.");
  const auto cancel_revision = window.research_revision;
  const auto cancelled = controller.execute(
      frame, generation, cancel_revision, window.funding_revision,
      NativeResearchIntent::Cancel, node_id);
  require(!cancelled.accepted && cancelled.research_revision == cancel_revision,
          "Unavailable cancellation mutated the active program.");
  const auto paused = controller.execute(
      frame, generation, cancel_revision, window.funding_revision,
      NativeResearchIntent::Pause, node_id);
  require(paused.accepted, "Canonical pause failed: " + paused.message);
  window = controller.build(frame, generation);
  const auto paused_node = std::ranges::find(window.nodes, node_id,
                                             &NativeResearchNode::id);
  require(paused_node != window.nodes.end() && paused_node->paused &&
              paused_node->stage_progress > 0. &&
              paused_node->primary_action.intent == NativeResearchIntent::Resume,
          "Paused program lost its retained progress or resume intent.");
  const auto resumed = controller.execute(
      frame, generation, window.research_revision,
      window.funding_revision, NativeResearchIntent::Resume, node_id);
  require(resumed.accepted, "Canonical resume failed: " + resumed.message);
}

void currency_projection(CampaignFrame &human, CampaignFrame &nonhuman) {
  NativeResearchController human_controller;
  auto human_window = human_controller.build(human, 41);
  require(human_window.currency.name == "United Earth Dollar" &&
              human_window.currency.code == "UED" &&
              human_window.currency.symbol == "$" &&
              human_window.treasury_credits &&
              human_window.formatted_treasury ==
                  human_window.currency.format(*human_window.treasury_credits),
          "Human research view did not own canonical currency and treasury.");
  const auto priced = std::ranges::find_if(
      human_window.nodes, [](const auto &node) { return node.cost.has_value(); });
  require(priced != human_window.nodes.end() &&
              priced->cost->formatted_authorization ==
                  human_window.currency.format(
                      priced->cost->authorization_credits) &&
              priced->cost->formatted_milestone_commitment ==
                  human_window.currency.format(
                      priced->cost->milestone_commitment_credits) &&
              priced->cost->formatted_operating_cost_rate ==
                  human_window.currency.format_rate(
                      -priced->cost->operating_credits_per_day) &&
              priced->cost->formatted_estimated_total ==
                  human_window.currency.format(
                      priced->cost->estimated_total_credits) &&
              priced->cost->formatted_credits_needed_to_start ==
                  human_window.currency.format(
                      priced->cost->credits_needed_to_start),
          "Research quote strings did not use the canonical human currency.");
  const auto human_funding_revision = human_window.funding_revision;
  const auto candidate = std::ranges::find_if(
      human_window.nodes, [](const auto &node) {
        return node.primary_action.intent == NativeResearchIntent::Start &&
               node.primary_action.enabled;
      });
  require(candidate != human_window.nodes.end(),
          "Human currency fixture lacks a startable research program.");
  auto &human_world = human.runtime().world().campaign();
  const auto human_player = std::ranges::find(
      human_world.civilizations, human_world.player_civilization_id,
      &Civilization::id);
  require(human_player != human_world.civilizations.end(),
          "Human currency fixture lost its player.");
  human_player->species_id = "pelagic_high_pressure";
  const auto stale_currency = human_controller.execute(
      human, 41, human_window.research_revision,
      human_window.funding_revision, NativeResearchIntent::Start,
      candidate->id);
  require(!stale_currency.accepted &&
              stale_currency.message.find("funding changed") !=
                  std::string::npos,
          "A changed sovereign currency accepted an old research window.");
  human_player->species_id = "terran_baseline";

  NativeResearchController nonhuman_controller;
  const auto nonhuman_window = nonhuman_controller.build(nonhuman, 42);
  require(nonhuman_window.currency.name == "Tide Mark" &&
              nonhuman_window.currency.code == "TM" &&
              nonhuman_window.currency.symbol == "◈" &&
              nonhuman_window.treasury_credits &&
              nonhuman_window.formatted_treasury ==
                  nonhuman_window.currency.format(
                      *nonhuman_window.treasury_credits),
          "Nonhuman research view reused the human currency.");

  player_economy(human).credits = 1e-15;
  human_window = human_controller.build(human, 41);
  require(human_window.formatted_treasury &&
              human_window.funding_revision == human_funding_revision &&
              human_window.formatted_treasury->starts_with("Under ") &&
              *human_window.formatted_treasury !=
                  human_window.currency.format(0.),
          "Tiny positive canonical money was presented as zero.");
}

void affordable_balance_change_allows_start(CampaignFrame &frame) {
  NativeResearchController controller;
  constexpr std::uint64_t generation = 51;
  player_economy(frame).credits = 1'000'000.;
  const auto window = controller.build(frame, generation);
  const auto candidate = std::ranges::find_if(window.nodes, [](const auto &node) {
    return node.primary_action.intent == NativeResearchIntent::Start &&
           node.primary_action.enabled;
  });
  require(candidate != window.nodes.end(),
          "Balance-change fixture lacks a startable research program.");
  player_economy(frame).credits -= 1.;
  const auto started = controller.execute(
      frame, generation, window.research_revision, window.funding_revision,
      NativeResearchIntent::Start, candidate->id);
  require(started.accepted,
          "An affordable treasury tick incorrectly invalidated Start.");
}

} // namespace

int main(int argc, char **argv) try {
  if (argc != 5)
    throw std::invalid_argument(
        "Usage: native_research_controller_tests <research-root> <stellar-catalog> <Player17-fixture> <scratch>");
  const auto research_root = fs::absolute(argv[1]);
  require(research_purpose("unlisted", "Life Medicine", "Public Purpose") ==
              "Explores a practical Life Medicine approach in the Public Purpose field.",
          "Fallback research explanation retained debug identifiers instead of player-facing labels.");
  const auto catalog = fs::absolute(argv[2]);
  const auto player_fixture = fs::absolute(argv[3]);
  const auto scratch = fs::absolute(argv[4]);
  fs::create_directories(scratch);

  auto authored = source_frame(research_root, player_fixture, scratch);
  const auto authored_has_locked_preview = observer_safe_projection(authored, 1);
  auto fresh = fresh_500_frame(research_root, catalog);
  auto nonhuman =
      fresh_500_frame(research_root, catalog, "pelagic_high_pressure");
  auto balance_change = fresh_500_frame(research_root, catalog);
  currency_projection(fresh, nonhuman);
  affordable_balance_change_allows_start(balance_change);
  player_economy(fresh).credits = 500.;
  const auto fresh_has_locked_preview = observer_safe_projection(fresh, 3);
  require(authored_has_locked_preview || fresh_has_locked_preview,
          "Representative campaigns lack an anonymous locked preview.");
  command_lifecycle(fresh);
  std::cout << "Native research observer-safe projection, tabs/search, canonical commands, cost denial, generation invalidation and paused progress passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native research controller test failed: " << error.what() << '\n';
  return 1;
}
