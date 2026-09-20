#include "native_research_controller.hpp"
#include "native_research_presentation.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/developer_campaign.hpp>

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

void command_lifecycle(CampaignFrame &frame, const fs::path &research_root) {
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
  require(std::ranges::any_of(window.nodes,[](const auto &n){
    return !n.active && n.maturity<ResearchMaturity::mature && n.requirements_met && !n.primary_action.enabled;
  }),"Occupied research slot hid scientifically eligible programs from planning.");
  const auto old_progress = active->total_progress;
  const auto cancel_revision = window.research_revision;
  require(active->cancel_action.enabled && active->cancel_action.reason.find("Refund")!=std::string::npos,
      "Active program did not expose its real cancellation refund.");
  const auto cancelled = controller.execute(frame,generation,cancel_revision,window.funding_revision,NativeResearchIntent::Cancel,node_id);
  require(cancelled.accepted && cancelled.research_revision!=cancel_revision,"Funded research cancellation failed.");
  window=controller.build(frame,generation);
  const auto retained=std::ranges::find(window.nodes,node_id,&NativeResearchNode::id);
  require(retained!=window.nodes.end() && retained->cancelled && !retained->active &&
      retained->total_progress==old_progress && retained->primary_action.enabled,"Cancelled work was hidden or erased by projection.");
  const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(),"0.1.12-alpha","2050-03-21T00:00:00Z"};
  const auto saved=encode_player_campaign_v17_json(capture_player_campaign_v17(frame.runtime(),options));
  auto loaded=restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),saved);
  const auto player=frame.runtime().world().campaign().player_civilization_id;
  require(loaded.research().get_civilization(player).cancelled_project(node_id),"Player save lost cancelled research work.");
  auto activated=std::move(loaded).activate();
  require(Json::parse(encode_player_campaign_v17_json(capture_player_campaign_v17(activated,options)))==Json::parse(saved),
      "Cancelled research changed during the full campaign save/load round trip.");
  const auto credits_after=player_economy(frame).credits;
  const auto repeated=controller.execute(frame,generation,window.research_revision,window.funding_revision,NativeResearchIntent::Cancel,node_id);
  require(!repeated.accepted && player_economy(frame).credits==credits_after,"Repeated UI cancellation credited treasury twice.");
  const auto restarted=controller.execute(frame,generation,window.research_revision,window.funding_revision,NativeResearchIntent::Start,node_id);
  require(restarted.accepted,"Restarting cancelled work failed: "+restarted.message);
  window=controller.build(frame,generation);
  const auto paused = controller.execute(
      frame, generation, window.research_revision, window.funding_revision,
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

void campaign_planning(CampaignFrame &frame,const fs::path &research_root){
  NativeResearchController controller;
  auto &runtime=frame.runtime();auto &world=runtime.world().campaign();
  const auto id=world.player_civilization_id;
  player_economy(frame).credits=1000000.;
  auto view=controller.build(frame,20);
  std::vector<std::string> candidates;
  for(const auto &node:view.nodes)
    if(node.primary_action.intent==NativeResearchIntent::Start&&node.primary_action.enabled)
      candidates.push_back(node.id);
  require(candidates.size()>=2,"Planning fixture needs two legitimate research candidates.");
  const auto edit=[&](NativeResearchIntent intent,std::string node={}){
    view=controller.build(frame,20);
    return controller.execute(frame,20,view.research_revision,view.funding_revision,intent,node);
  };
  const auto first=candidates[0],second=candidates[1];
  require(edit(NativeResearchIntent::Enqueue,first).accepted&&edit(NativeResearchIntent::Enqueue,second).accepted,
      "Known research could not be queued.");
  const auto stale=view;
  require(!controller.execute(frame,20,stale.research_revision,stale.funding_revision,NativeResearchIntent::RemoveQueued,second).accepted,
      "Stale queue command was accepted.");
  require(edit(NativeResearchIntent::MoveUp,second).accepted&&runtime.research().plan(id).queue.front()==second,
      "Canonical queue did not reorder.");
  require(edit(NativeResearchIntent::MoveDown,second).accepted&&runtime.research().plan(id).queue.front()==first,
      "Canonical queue did not restore order.");
  require(edit(NativeResearchIntent::AddFavorite,first).accepted&&edit(NativeResearchIntent::SuggestionsOff).accepted,
      "Research planning preferences did not use validated commands.");
  const auto unknown=recognized_undetailed_node(frame);
  require(unknown.has_value(),"Planning fixture needs undiscovered research.");
  const auto before_denial=runtime.research().plan(id);
  require(!edit(NativeResearchIntent::Enqueue,unknown->first).accepted&&
      !edit(NativeResearchIntent::AddFavorite,unknown->first).accepted&&runtime.research().plan(id)==before_denial,
      "Planning exposed or changed undiscovered research.");
  const auto revision=runtime.research().get_civilization(id).revision();
  require(edit(NativeResearchIntent::Enqueue,first).accepted&&runtime.research().get_civilization(id).revision()==revision,
      "Duplicate enqueue changed the queue or revision.");
  for(int civ:runtime.research().civilization_ids())if(civ!=id)
    require(runtime.research().plan(civ)==AdaptiveResearchPlan{},"Player planning affected another civilization.");

  player_economy(frame).credits=0;
  auto blocked=AdaptiveResearchCampaignCommands::start_queued_research(
      {world.civilizations,world.economies},runtime.research(),id);
  require(blocked.empty()&&runtime.research().plan(id)==before_denial&&player_economy(frame).credits==0,
      "Unfunded queue skipped the head or reserved resources.");
  const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(),"0.1.12-alpha","2044-05-06T07:08:09Z"};
  const auto payload=capture_player_campaign_v17(runtime,options);
  require(payload.adaptive_research->schema_version==3,"Nonempty research plan did not use schema 3.");
  const auto saved=encode_player_campaign_v17_json(payload);
  auto loaded=restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),saved);
  require(loaded.research().plan(id)==before_denial,"Copied campaign lost its queue, favorites or suggestions setting.");
  // Revisions are session-local and are deliberately rebuilt by restore.
  for(const auto &node:runtime.research().get_civilization(id).node_states()){
    const auto *restored=loaded.research().get_civilization(id).try_get_node_state(node.node_id);
    require(restored&&restored->maturity==node.maturity&&restored->total_research_points==node.total_research_points,
        "Planning persistence changed scientific progress.");
  }
  auto bad=payload;
  auto civ=std::ranges::find(bad.adaptive_research->civilizations,id,&AdaptiveResearchCampaignCivilizationSnapshot::civilization_id);
  civ->plan.queue.push_back(first);
  bool rejected=false;try{(void)restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),encode_player_campaign_v17_json(bad));}catch(const std::exception&){rejected=true;}
  require(rejected,"Duplicate queue records survived save validation.");
  civ->plan.queue={unknown->first};rejected=false;
  try{(void)restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),encode_player_campaign_v17_json(bad));}catch(const std::exception&){rejected=true;}
  require(rejected,"Undiscovered queue record survived save validation.");
  civ->plan.queue.assign(129,first);rejected=false;
  try{(void)restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),encode_player_campaign_v17_json(bad));}catch(const std::exception&){rejected=true;}
  require(rejected,"Unbounded queue records survived save validation.");

  StrategicClock clock;clock.restore(loaded.simulation_days());clock.set_speed(StrategicSpeed::Paused);
  CampaignFrame resumed(std::move(loaded).activate(),std::move(clock),CampaignFramePolicy::Player);
  player_economy(resumed).credits=1000000.;
  (void)resumed.advance(1.);
  require(resumed.runtime().research().plan(id)==before_denial,"Paused simulation consumed queued intent.");
  resumed.clock().set_speed(StrategicSpeed::Normal);
  (void)resumed.advance(.01);
  const auto &state=resumed.runtime().research().get_civilization(id);
  require(std::ranges::any_of(state.active_projects(),[&](const auto &p){return p.node_id==first;}),
      "Headless campaign did not start queued research when funded.");
  require(std::ranges::find(resumed.runtime().research().plan(id).queue,first)==resumed.runtime().research().plan(id).queue.end(),
      "Started program remained in the waiting queue.");
  const auto count=state.active_projects().size();
  (void)resumed.advance(.01);
  require(state.active_projects().size()==count,"Repeated queue processing duplicated an active project.");
}
void developer_research_setup(CampaignFrame &frame,const fs::path &research_root){
  auto &runtime=frame.runtime();auto &world=runtime.world().campaign();const auto id=world.player_civilization_id;
  const PlayerCampaignCaptureOptions options{0,"0.1.12-alpha","2044-05-06T07:08:09Z"};
  const auto normal=encode_player_campaign_v17_json(capture_player_campaign_v17(runtime,options));
  bool rejected=false;try{(void)initialize_developer_research(runtime,{true,false});}catch(const std::invalid_argument&){rejected=true;}
  require(rejected&&normal==encode_player_campaign_v17_json(capture_player_campaign_v17(runtime,options)),
      "Developer research modified an ordinary campaign.");
  world.developer_provenance=CampaignDeveloperProvenance{};
  const auto initial=capture_developer_campaign_json(runtime,options);
  const auto off=initialize_developer_research(runtime,{});
  require(off.completed_normal==0&&off.events.empty()&&capture_developer_campaign_json(runtime,options)==initial,
      "Unchecked developer research option changed starting progression.");
  const auto completed=initialize_developer_research(runtime,{true,false});
  require(completed.completed_normal>0&&completed.completed_special==0&&world.developer_provenance->tools_used,
      "Normal-research setup did not establish real knowledge with provenance.");
  const auto &state=runtime.research().get_civilization(id);
  for(const auto &node:runtime.research_runtime().authority().catalog().nodes()){
    if(node.public_normal_research){
      const auto *current=state.try_get_node_state(node.id);
      require(current&&current->maturity==ResearchMaturity::mature,"Developer completion left normal research incomplete: "+node.id);
    }
  }
  require(state.has_capability("experimental_interstellar_transit"),"Research completion did not grant real propulsion capability.");
  require(runtime.research().project_funding(id).empty(),"Completed developer research left funding reservations.");
  const auto saved=capture_developer_campaign_json(runtime,options);
  const auto again=initialize_developer_research(runtime,{true,false});
  require(again.completed_normal==0&&again.events.empty()&&saved==capture_developer_campaign_json(runtime,options),
      "Repeated developer completion changed the campaign.");
  rejected=false;try{(void)capture_player_campaign_v17(runtime,options);}catch(const PlayerCampaignPersistenceOperationError&){rejected=true;}
  require(rejected,"Developer runtime was saved as Player17.");
  for(const auto &text:{saved,Json::parse(saved).at("Campaign").dump()}){
    rejected=false;try{(void)restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),text);}catch(const std::exception&){rejected=true;}
    require(rejected,"Player loader accepted developer envelope or nested payload.");
  }
  rejected=false;try{(void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(research_root),normal);}catch(const std::exception&){rejected=true;}
  require(rejected,"Developer loader silently promoted an ordinary save.");
  auto restored=restore_developer_campaign_json(load_adaptive_research_strategic_runtime(research_root),saved);
  require(restored.galaxy().developer_provenance==world.developer_provenance,"Developer save lost override provenance.");
  auto activated=std::move(restored).activate();
  require(Json::parse(capture_developer_campaign_json(activated,options))==Json::parse(saved),
      "Developer save/load changed canonical research, world or diplomacy.");
  auto missing=Json::parse(saved);missing["Campaign"].erase("DeveloperSession");
  rejected=false;try{(void)restore_developer_campaign_json(load_adaptive_research_strategic_runtime(research_root),missing.dump());}catch(const std::exception&){rejected=true;}
  require(rejected,"Developer loader accepted an unmarked nested payload.");
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
  command_lifecycle(fresh,research_root);
  auto planning=fresh_500_frame(research_root,catalog);
  campaign_planning(planning,research_root);
  auto developer=fresh_500_frame(research_root,catalog);
  developer_research_setup(developer,research_root);
  std::cout << "Native research observer-safe projection, tabs/search, canonical commands, cost denial, generation invalidation and paused progress passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native research controller test failed: " << error.what() << '\n';
  return 1;
}
