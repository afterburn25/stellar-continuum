// Campaign scripted-content integration tests: the engine
// ScriptedContentRuntime layered over authoritative Core state through
// CampaignScriptedContentAdapter — step-event mapping, trigger evaluation,
// effect application, chronicle recording and save/load.
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/campaign_scripted_content.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_persistence.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar;

namespace {

int failures = 0;
void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

core::IntegratedAdaptiveCampaignRuntime
make_campaign(core::AdaptiveResearchStrategicRuntime research,
              core::FreshCampaignState world, double day = 0.0) {
  return core::IntegratedAdaptiveCampaignRuntime::create_fresh(
      std::move(research), std::move(world), core::DiplomacyState{}, day);
}

core::FreshCampaignState small_world(const fs::path &catalog_path) {
  core::PersistableFreshCampaignOptions options;
  options.created_at_utc = "2026-10-02T00:00:00Z";
  options.system_count = 250;
  options.pre_warp_civilization_count = 1;
  options.ancient_civilization_count = 0;
  return core::seed_persistable_fresh_campaign(
      7, core::load_nearby_catalog(catalog_path), options);
}

const char *grant_doc = R"json({"scripted_events":[{
  "id":"windfall","poll_days":1,
  "scope":{"kind":"civilization","id":-1},
  "trigger":{"all":[
    {"check":"civilization_is_player","scope":{"kind":"civilization","id":-1}},
    {"check":"treasury_at_least","scope":{"kind":"civilization","id":-1},
     "args":{"amount":"0"}}]},
  "effects":[{"do":"grant_credits","scope":{"kind":"civilization","id":-1},
              "args":{"amount":"5000"}}],
  "cooldown_days":5}]})json";

void adapter_validates_at_load(
    core::IntegratedAdaptiveCampaignRuntime &campaign) {
  std::vector<engine::ScriptLoadError> errors;
  require(!campaign.load_scripted_document(
              R"json({"scripted_events":[{"id":"bad","poll_days":1,
                "trigger":{"check":"does_not_exist"}}]})json",
              "bad.json", &errors),
          "adapter rejects an unknown check at load");
  require(!errors.empty() && errors.front().object_id == "bad" &&
              errors.front().file == "bad.json",
          "load error carries file and object id");
  errors.clear();
  require(!campaign.load_scripted_document(
              R"json({"scripted_events":[{"id":"bad_effect","poll_days":1,
                "trigger":{"check":"elapsed_days_at_least","args":{"days":"0"}},
                "effects":[{"do":"grant_credits"}]}]})json",
              "bad.json", &errors),
          "adapter rejects an effect missing its required arg");
  errors.clear();
  require(campaign.load_scripted_document(grant_doc, "windfall.json", &errors),
          "valid document loads through the adapter");
  require(errors.empty(), "valid document produces no errors");
}

void poll_applies_authoritative_effect(
    core::IntegratedAdaptiveCampaignRuntime &campaign) {
  const int player = campaign.world().campaign().player_civilization_id;
  const auto credits = [&] {
    for (const auto &economy : campaign.world().campaign().economies)
      if (economy.civilization_id == player) return economy.credits;
    return 0.0;
  };
  const double before = credits();
  [[maybe_unused]] const auto first_step = campaign.advance(1.0, 1.0);
  const double after_first = credits();
  const double first_delta = after_first - before;
  require(first_delta >= 5000.0,
          "polled scripted grant mutates civilization credits");
  // cooldown_days=5 suppresses an immediate refire on the next advance.
  [[maybe_unused]] const auto second_step = campaign.advance(1.0, 2.0);
  const double second_delta = credits() - after_first;
  require(second_delta + 4000.0 < first_delta,
          "cooldown suppresses scripted refire");
}

void event_mapping(core::IntegratedAdaptiveCampaignStepResult &step) {
  core::ExplorationEvent explored;
  explored.type = core::ExplorationEventType::AnomalySurveyed;
  explored.civilization_id = 1;
  explored.fleet_id = 9;
  explored.system_id = 4;
  explored.planetary_body_id = 77;
  step.core.exploration_events.push_back(explored);
  core::ColonizationEvent colony{2, 3, 8, 11, "settled"};
  step.core.colonization_events.push_back(colony);
  const auto events = core::script_events_for_step(step, {});
  require(events.size() == 2, "step maps its domain events");
  require(events[0].topic == "exploration.anomaly_surveyed",
          "exploration event maps to the canonical chronicle topic");
  require(events[0].context.civilization == 1 &&
              events[0].context.system == 4 && events[0].context.body == 77,
          "exploration context carries civilization/system/body");
  require(events[1].topic == "colony.founded" &&
              events[1].context.colony == 11,
          "colonization event maps with colony scope id");
}

void anomaly_scope_once(core::IntegratedAdaptiveCampaignRuntime &campaign) {
  auto &bodies = campaign.world().campaign().bodies;
  require(bodies.size() >= 2, "test galaxy has at least two bodies");
  bodies[0].has_anomaly = true;
  bodies[1].has_anomaly = true;
  const char *doc = R"json({"scripted_events":[{
    "id":"anomaly.reward","on":"exploration.anomaly_surveyed",
    "once_per_scope":true,"scope":{"kind":"body","id":-1},
    "trigger":{"check":"body_has_anomaly","scope":{"kind":"body","id":-1}},
    "effects":[{"do":"grant_credits","scope":{"kind":"civilization","id":-1},
                "args":{"amount":"40"}},
               {"do":"resolve_anomaly","scope":{"kind":"body","id":-1}}]}]})json";
  std::vector<engine::ScriptLoadError> errors;
  require(campaign.load_scripted_document(doc, "anomalies.json", &errors),
          "anomaly document loads");

  const auto player = campaign.world().campaign().player_civilization_id;
  const auto credits = [&] {
    for (const auto &economy : campaign.world().campaign().economies)
      if (economy.civilization_id == player) return economy.credits;
    return 0.0;
  };
  const double base = credits();

  engine::ScriptFiringContext context;
  context.civilization = player;
  context.system = bodies[0].system_id;
  context.body = bodies[0].id;
  campaign.scripted_content().handle_event("exploration.anomaly_surveyed",
                                           context, 1.0);
  require(credits() == base + 40.0, "first anomaly survey grants once");
  require(!bodies[0].has_anomaly, "resolve_anomaly consumes the site");
  campaign.scripted_content().handle_event("exploration.anomaly_surveyed",
                                           context, 2.0);
  require(credits() == base + 40.0,
          "once_per_scope suppresses a resurvey of the same body");
  context.body = bodies[1].id;
  context.system = bodies[1].system_id;
  campaign.scripted_content().handle_event("exploration.anomaly_surveyed",
                                           context, 3.0);
  require(credits() == base + 80.0,
          "a different body fires the scoped event again");
  require(campaign.history().size() >= 2,
          "fired events reach the campaign chronicle");
}

void chronicle_visibility(core::IntegratedAdaptiveCampaignRuntime &campaign) {
  const auto records = campaign.history().query(
      {.category = "scripted.anomaly.reward"});
  require(records.size() == 2, "both scoped anomaly fires are chronicled");
}

void persistence(core::IntegratedAdaptiveCampaignRuntime &campaign,
                 const fs::path &research_root) {
  core::PlayerCampaignCaptureOptions options;
  options.simulation_days = 3.0;
  options.game_version = "test";
  options.saved_at_utc = "2026-10-02T00:00:00Z";
  const auto payload = core::capture_player_campaign_v17(campaign, options);
  require(payload.scripted_content.has_value(),
          "capture carries scripted content state");
  require(payload.scripted_content->find("anomaly.reward") !=
              std::string::npos,
          "scoped fired keys persist");

  const auto text = core::encode_player_campaign_v17_json(payload);
  require(text.find("ScriptedContent") != std::string::npos,
          "encoded save includes the ScriptedContent member");
  auto runtime = core::load_adaptive_research_strategic_runtime(research_root);
  auto restored_owner =
      core::restore_player_campaign_v17_json(std::move(runtime), text);
  auto restored = std::move(restored_owner).activate();
  require(restored.scripted_content().fired("anomaly.reward"),
          "restored runtime remembers scoped fired events");
  require(!restored.world().campaign().bodies[0].has_anomaly &&
              !restored.world().campaign().bodies[1].has_anomaly,
          "consumed anomaly sites persist through save/load");
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: campaign_scripted_content_tests <research root> "
                 "<stellar catalog>\n";
    return 1;
  }
  try {
    const fs::path research_root = fs::absolute(argv[1]);
    const fs::path catalog = fs::absolute(argv[2]);
    auto step = core::IntegratedAdaptiveCampaignStepResult{};
    event_mapping(step);
    auto campaign = make_campaign(
        core::load_adaptive_research_strategic_runtime(research_root),
        small_world(catalog));
    adapter_validates_at_load(campaign);
    poll_applies_authoritative_effect(campaign);
    anomaly_scope_once(campaign);
    chronicle_visibility(campaign);
    persistence(campaign, research_root);
  } catch (const std::exception &error) {
    std::cerr << "UNCAUGHT: " << error.what() << '\n';
    return 1;
  }
  if (failures) {
    std::cerr << failures << " scripted-content campaign checks failed\n";
    return 1;
  }
  std::cout << "Campaign scripted content tests passed\n";
  return 0;
}
