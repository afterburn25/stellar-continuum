#include <stellar/core/developer_campaign_json.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <string>

using namespace stellar::core;
using Json = nlohmann::ordered_json;

namespace {
int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::string research_root;

bool throws_envelope_error(const std::string &json,
                           const char *expected_fragment) {
  try {
    auto runtime = load_adaptive_research_strategic_runtime(research_root);
    (void)restore_developer_campaign_json(std::move(runtime), json);
  } catch (const PlayerCampaignJsonError &error) {
    return std::string(error.what()).find(expected_fragment) !=
           std::string::npos;
  } catch (const std::exception &error) {
    return std::string(error.what()).find(expected_fragment) !=
           std::string::npos;
  }
  return false;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: developer_campaign_json_tests <catalog.json> "
                 "<research_root>\n";
    return 2;
  }
  research_root = argv[2];
  const auto catalog = load_nearby_catalog(argv[1]);
  const PlayerCampaignCaptureOptions options{1.5, "test-build",
                                             "2026-09-16T00:00:00Z"};

  auto player_world = seed_persistable_fresh_campaign(
      4242, catalog, {"2026-09-16T00:00:00Z", 250, 2, 0});
  auto player_runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      std::move(player_world));
  bool threw = false;
  try {
    (void)capture_developer_campaign_v17(player_runtime, options);
  } catch (const PlayerCampaignPersistenceOperationError &) {
    threw = true;
  }
  check(threw, "developer capture requires Developer provenance");

  auto world = seed_persistable_fresh_campaign(
      4242, catalog, {"2026-09-16T00:00:00Z", 250, 2, 0});
  world.developer_provenance = CampaignDeveloperProvenance{false};
  const auto player_id = world.player_civilization_id;
  const auto system_count = world.systems.size();
  auto runtime = IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(research_root),
      std::move(world));
  const auto payload = capture_developer_campaign_v17(runtime, options);
  check(payload.format_version == 17, "developer capture emits Player17 DTO");

  const auto envelope = encode_developer_campaign_json(payload, false);
  const auto parsed = Json::parse(envelope);
  check(parsed.at("DeveloperFormatVersion") == 1 &&
            parsed.at("Mode") == "Developer" &&
            parsed.at("ToolsUsed") == false &&
            parsed.at("Campaign").is_object(),
        "envelope declares the strict reference fields");
  check(parsed.at("Campaign").at("FormatVersion") == 17,
        "envelope nests a canonical v17 payload");

  auto restored = restore_developer_campaign_json(
      load_adaptive_research_strategic_runtime(research_root), envelope);
  check(restored.campaign.galaxy().developer_provenance.has_value() &&
            !restored.campaign.galaxy().developer_provenance->tools_used,
        "restore stamps Developer provenance from the envelope");
  check(!restored.tools_used &&
            restored.campaign.galaxy().player_civilization_id == player_id &&
            restored.campaign.galaxy().systems.size() == system_count,
        "restore preserves the canonical campaign state");
  check(restored.campaign.simulation_days() == 1.5,
        "restore preserves simulation days");

  auto tools_used_envelope = Json::parse(envelope);
  tools_used_envelope["ToolsUsed"] = true;
  auto tools_restored = restore_developer_campaign_json(
      load_adaptive_research_strategic_runtime(research_root),
      tools_used_envelope.dump());
  check(tools_restored.tools_used &&
            tools_restored.campaign.galaxy().developer_provenance->tools_used,
        "ToolsUsed round-trips through the envelope");

  threw = false;
  try {
    (void)restore_player_campaign_v17_json(
        load_adaptive_research_strategic_runtime(research_root), envelope);
  } catch (const std::exception &error) {
    threw = std::string(error.what()).find("Developer campaign envelopes") !=
            std::string::npos;
  }
  check(threw, "the Player loader rejects the Developer envelope");

  auto broken = Json::parse(envelope);
  broken["Extra"] = 1;
  check(throws_envelope_error(broken.dump(), "unexpected or duplicate field"),
        "envelope rejects unexpected fields");
  broken = Json::parse(envelope);
  broken.erase("Mode");
  check(throws_envelope_error(broken.dump(), "missing required fields"),
        "envelope rejects missing fields");
  broken = Json::parse(envelope);
  broken["Mode"] = "Player";
  check(throws_envelope_error(broken.dump(), "exactly as Developer"),
        "envelope rejects non-Developer Mode");
  broken = Json::parse(envelope);
  broken["ToolsUsed"] = "yes";
  check(throws_envelope_error(broken.dump(), "explicit boolean"),
        "envelope rejects non-boolean ToolsUsed");
  broken = Json::parse(envelope);
  broken["DeveloperFormatVersion"] = 2;
  check(throws_envelope_error(broken.dump(), "DeveloperFormatVersion 1"),
        "envelope rejects unsupported format versions");
  broken = Json::parse(envelope);
  broken["Campaign"]["ToolsUsed"] = false;
  check(throws_envelope_error(broken.dump(), "nested session metadata"),
        "envelope rejects nested session metadata");

  if (failures != 0) {
    std::cerr << failures << " developer campaign envelope checks failed\n";
    return 1;
  }
  std::cout << "developer campaign envelope checks passed\n";
  return 0;
}
