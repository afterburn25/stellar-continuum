// Gate075 draft consumer. It deliberately reuses maintained parity codecs so
// this integration gate compares complete retained state instead of rebuilding
// a smaller, integration-specific model of the world.
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/diplomacy_state_access.hpp>
#include <stellar/core/diplomacy_simulation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <type_traits>
#include <utility>
#include <vector>

namespace campaign_support {
#define main campaign_coordinator_parity_unused_main
#include <campaign_coordinator_tests.cpp>
#undef main
} // namespace campaign_support

// The integrated campaign codec predates retained tactical fleet loadouts and
// intentionally emits them as null. Reuse the maintained massive-combat codec
// for those fields so Gate097 compares the complete post-reconciliation world.
namespace massive_codec {
#define main massive_combat_persistence_unused_main
#include <massive_combat_persistence_tests.cpp>
#undef main
} // namespace massive_codec

namespace research_support {
#define main adaptive_campaign_simulation_parity_unused_main
#include <adaptive_research_campaign_simulation_tests.cpp>
#undef main
} // namespace research_support

namespace diplomacy_support {
#define require diplomacy_require
#include <diplomacy_test_json.hpp>
#undef require
} // namespace diplomacy_support

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}

void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}

std::string read_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    fail("Could not read: " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string hex(std::span<const std::uint8_t> bytes) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}

void require_equal(const Json &actual, const Json &expected,
                   const std::string &context) {
  std::string path;
  if (!research_support::equal_json(actual, expected, path)) {
    fail(context + " differed at " + path + "\nactual=" + actual.dump() +
         "\nexpected=" + expected.dump());
  }
}

Json camelize(const Json &value) {
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto &item : value)
      result.push_back(camelize(item));
    return result;
  }
  if (!value.is_object())
    return value;
  Json result = Json::object();
  for (const auto &[key, item] : value.items()) {
    std::string name = key;
    if (!name.empty() && name.front() >= 'A' && name.front() <= 'Z')
      name.front() = static_cast<char>(name.front() - 'A' + 'a');
    result[name] = camelize(item);
  }
  return result;
}

void verify_source_fingerprints(const Json &fixture,
                                const fs::path &source_root) {
  const auto &before = fixture.at("SourceFilesBefore");
  const auto &after = fixture.at("SourceFilesAfter");
  require(before == after, "managed source changed during fixture generation");
  require(before.size() == 9, "unexpected pinned source file count");
  std::vector<std::string> paths;
  for (const auto &file : before) {
    const auto relative = file.at("Path").get<std::string>();
    require(std::find(paths.begin(), paths.end(), relative) == paths.end(),
            "duplicate source fingerprint: " + relative);
    paths.push_back(relative);
    const auto bytes = read_file(source_root / relative);
    const auto digest = detail::adaptive_research_sha256(std::span(
        reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
    require(hex(digest) == file.at("Sha256").get<std::string>(),
            "source fingerprint mismatch: " + relative);
  }
}

FleetPowerObservation decode_observation(const Json &value) {
  return {value.at("ObserverId").get<int>(), value.at("FleetId").get<int>(),
          value.at("Power").get<double>(), value.at("ObservedDay").get<double>(),
          value.at("Evidence").get<std::string>()};
}

Json encode_observations(std::span<const FleetPowerObservation> values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back({{"ObserverId", value.observer_id},
                      {"FleetId", value.fleet_id},
                      {"Power", value.power},
                      {"ObservedDay", value.observed_day},
                      {"Evidence", value.evidence}});
  }
  return result;
}

FreshCampaignState decode_world(const Json &value) {
  Json compatible = value;
  for (std::size_t index = 0; index < compatible.at("Systems").size(); ++index) {
    compatible["Systems"][index]["Position"] = {
        {"X", value.at("SystemPositions")[index].at("X")},
        {"Y", value.at("SystemPositions")[index].at("Y")}};
  }
  for (std::size_t index = 0; index < compatible.at("Fleets").size(); ++index) {
    const auto &position = value.at("FleetPositions")[index];
    compatible["Fleets"][index]["Position"] = {
        {"X", position.at("X")}, {"Y", position.at("Y")}};
    compatible["Fleets"][index]["LocalTransitStart"] = {
        {"X", position.at("LocalStartX")},
        {"Y", position.at("LocalStartY")}};
    compatible["Fleets"][index]["LocalTransitPosition"] = {
        {"X", position.at("LocalPositionX")},
        {"Y", position.at("LocalPositionY")}};
    compatible["Fleets"][index]["LocalTransitTarget"] = {
        {"X", position.at("LocalTargetX")},
        {"Y", position.at("LocalTargetY")}};
  }
  FreshCampaignState world = campaign_support::parse_campaign(
      compatible, value.at("UsedConstrainedHomeFallback").get<bool>());
  for (std::size_t index = 0; index < world.fleets.size(); ++index) {
    const auto &fleet = value.at("Fleets")[index];
    if (!fleet.at("TacticalLoadout").is_null())
      world.fleets[index].tactical_loadout =
          massive_codec::loadout(massive_codec::Json::parse(
              fleet.at("TacticalLoadout").dump()));
    if (!fleet.at("TacticalVessel").is_null())
      world.fleets[index].tactical_vessel =
          massive_codec::vessel(massive_codec::Json::parse(
              fleet.at("TacticalVessel").dump()));
  }
  for (const auto &observation : value.at("CombatIntelligence"))
    world.combat_intelligence.push_back(decode_observation(observation));
  return world;
}

Json encode_world(const FreshCampaignState &world) {
  Json result = campaign_support::encode_campaign(world);
  Json system_positions = Json::array();
  for (std::size_t index = 0; index < world.systems.size(); ++index) {
    const auto &value = world.systems[index];
    system_positions.push_back(
        {{"Id", value.id}, {"X", value.position.x}, {"Y", value.position.y}});
    result["Systems"][index]["Position"] = Json::object();
  }
  Json fleet_positions = Json::array();
  for (std::size_t index = 0; index < world.fleets.size(); ++index) {
    const auto &value = world.fleets[index];
    fleet_positions.push_back(
        {{"Id", value.id},
         {"X", value.position.x},
         {"Y", value.position.y},
         {"LocalStartX", value.local_transit_start.x},
         {"LocalStartY", value.local_transit_start.y},
         {"LocalPositionX", value.local_transit_position.x},
         {"LocalPositionY", value.local_transit_position.y},
         {"LocalTargetX", value.local_transit_target.x},
         {"LocalTargetY", value.local_transit_target.y}});
    result["Fleets"][index]["Position"] = Json::object();
    result["Fleets"][index]["LocalTransitStart"] = Json::object();
    result["Fleets"][index]["LocalTransitPosition"] = Json::object();
    result["Fleets"][index]["LocalTransitTarget"] = Json::object();
    result["Fleets"][index]["TacticalLoadout"] = value.tactical_loadout
        ? Json::parse(massive_codec::loadout_json(*value.tactical_loadout).dump())
        : Json(nullptr);
    result["Fleets"][index]["TacticalVessel"] = value.tactical_vessel
        ? Json::parse(massive_codec::vessel_json(*value.tactical_vessel).dump())
        : Json(nullptr);
  }
  result["UsedConstrainedHomeFallback"] = world.used_constrained_home_fallback;
  result["CombatIntelligence"] = encode_observations(world.combat_intelligence);
  result["SystemPositions"] = std::move(system_positions);
  result["FleetPositions"] = std::move(fleet_positions);
  return result;
}

Json encode_research_capabilities(const AdaptiveResearchCampaignState &campaign) {
  Json result = Json::array();
  for (const int civilization_id : campaign.civilization_ids()) {
    const auto &state = campaign.get_civilization(civilization_id);
    const bool quantum = state.has_capability("tech:quantum_sensors");
    const bool distributed =
        state.has_capability("tech:distributed_sensor_network");
    result.push_back(
        {{"CivilizationId", civilization_id},
         {"QuantumSensors", quantum},
         {"DistributedSensors", distributed},
         {"Scanner", quantum || distributed},
         {"OrbitalIndustry", state.has_capability("orbital_industry")},
         {"SpacecraftConstruction",
          state.has_capability("spacecraft_construction")},
         {"ExperimentalTransit",
          state.has_capability("experimental_interstellar_transit")}});
  }
  return result;
}

Json encode_research_snapshot(const AdaptiveResearchStrategicRuntime &runtime,
                              const AdaptiveResearchCampaignState &campaign) {
  const AdaptiveResearchCampaignSnapshotCodec codec(runtime);
  return research_support::encode_campaign_snapshot(codec.capture(campaign));
}

Json encode_state(const IntegratedAdaptiveCampaignRuntime &owner) {
  return {{"World", encode_world(owner.world().campaign())},
          {"Research",
           encode_research_snapshot(owner.research_runtime(), owner.research())},
          {"ResearchCapabilityProjection",
           encode_research_capabilities(owner.research())},
          {"Diplomacy",
           diplomacy_support::jsnapshot(owner.diplomacy().snapshot())}};
}

Json encode_research_events(
    const std::vector<AdaptiveResearchCampaignEvent> &events) {
  Json result = Json::array();
  for (const auto &event : events) {
    result.push_back({{"CivilizationId", event.civilization_id},
                      {"NodeId", event.node_id},
                      {"Message", event.message},
                      {"IsOutcome", event.is_outcome}});
  }
  return result;
}

Json encode_sensor_results(
    std::span<const IntegratedSensorContactRecordingResult> values,
    const Json &capabilities_before) {
  Json result = Json::array();
  for (const auto &value : values) {
    const auto expected_capability = std::find_if(
        capabilities_before.begin(), capabilities_before.end(),
        [&value](const Json &entry) {
          return entry.at("CivilizationId").get<int>() ==
                 value.civilization_id;
        });
    require(expected_capability != capabilities_before.end(),
            "missing pre-advance sensor capability projection");
    result.push_back(
        {{"CivilizationId", value.civilization_id},
         {"Scanner", expected_capability->at("Scanner")},
         {"Recorded", value.recorded_contacts}});
  }
  return result;
}

Json encode_diplomacy_result(
    const DiplomacyCampaignRuntimeStepResult &value) {
  return {{"Tick", value.tick},
          {"FirstContactEventsProcessed", value.first_contact_events_processed},
          {"CombatIncidentsProcessed", value.combat_incidents_processed},
          {"Maintenance",
           {{"Ran", value.maintenance.ran},
            {"ReviewTick", value.maintenance.review_tick},
            {"ContactAging",
             {{"ReviewedContacts",
               value.maintenance.contact_aging.reviewed_contacts},
              {"NewlyStaleContacts",
               value.maintenance.contact_aging.newly_stale_contacts}}},
            {"ProposalLifecycle",
             {{"PendingProposalsReviewed",
               value.maintenance.proposal_lifecycle.pending_proposals_reviewed},
              {"NewlyExpiredProposals",
               value.maintenance.proposal_lifecycle.newly_expired_proposals}}}}},
          {"MaintenanceTransitions", value.maintenance_transitions()},
          {"ProcessedDiplomacyEvents", value.processed_diplomacy_events()}};
}

Json encode_error(std::exception_ptr error) {
  if (!error)
    return nullptr;
  try {
    std::rethrow_exception(error);
  } catch (const std::out_of_range &caught) {
    return {{"Type", "ArgumentOutOfRangeException"},
            {"Message", caught.what()}};
  } catch (const std::invalid_argument &caught) {
    return {{"Type", "ArgumentException"}, {"Message", caught.what()}};
  } catch (const std::runtime_error &caught) {
    return {{"Type", "InvalidOperationException"},
            {"Message", caught.what()}};
  }
}

Json encode_phase_evidence(IntegratedAdaptiveCampaignRuntime &owner,
                           const IntegratedAdaptiveCampaignAdvanceTrace &trace,
                           bool ai_privacy) {
  Json known = Json::array();
  if (ai_privacy) {
    const auto snapshot =
        DiplomacyStrategicKnowledgeProvider(owner.diplomacy()).build(
            2, std::max<std::int64_t>(
                   0, static_cast<std::int64_t>(std::floor(
                          owner.core().strategic_runtime().strategic_days()))));
    for (const auto &entry : snapshot.civilizations)
      known.push_back(entry.key);
  }
  Json weights = nullptr;
  if (ai_privacy) {
    const auto value = owner.core().strategic_runtime().get_industry_weights(2);
    weights = {{"ConstructionWeight", value.construction_weight},
               {"ShipbuildingWeight", value.shipbuilding_weight}};
  }
  return {{"CoreCompleted", trace.core.has_value()},
          {"SensorCivilizationsCompleted", trace.sensor_contacts.size()},
          {"ResearchCompleted", trace.research_events.has_value()},
          {"DiplomacyCompleted", trace.diplomacy.has_value()},
          {"DiplomacyLastProcessedTick",
           owner.diplomacy_runtime().last_processed_tick()},
          {"DiplomacyNextMaintenanceReviewTick",
           owner.diplomacy_runtime().next_maintenance_review_tick()},
          {"StrategicIntentCount",
           owner.core().strategic_runtime().published_intent_count()},
          {"AiIndustryWeights", std::move(weights)},
          {"AiKnownCivilizations", std::move(known)}};
}

IntegratedAdaptiveCampaignRuntime create_owner_for_row(
    const fs::path &research_root, const Json &row) {
  FreshCampaignState world = decode_world(row.at("Before").at("World"));
  const auto research_snapshot = research_support::decode_campaign_snapshot(
      camelize(row.at("Before").at("Research")));
  DiplomacyState diplomacy = DiplomacyState::restore(
      diplomacy_support::snapshot(row.at("Before").at("Diplomacy")));
  const double reset_day = row.at("Arguments").at("Reset").get<double>();
  auto runtime = load_adaptive_research_strategic_runtime(research_root);
  auto owner = row.at("Restored").get<bool>()
                   ? IntegratedAdaptiveCampaignRuntime::restore_research(
                         std::move(runtime), std::move(world), research_snapshot,
                         std::move(diplomacy), reset_day)
                   : IntegratedAdaptiveCampaignRuntime::create_fresh(
                         std::move(runtime), std::move(world),
                         std::move(diplomacy), reset_day);
  if (row.at("Arguments").at("ScannerKnown").get<bool>()) {
    auto &civilization = const_cast<AdaptiveResearchCivilizationState &>(
        owner.research().get_civilization(1));
    require(detail::AdaptiveResearchStateWriter::add_capability(
                civilization, {"tech:quantum_sensors", std::nullopt}),
            row.at("Name").get<std::string>() +
                ": scanner setup was not a new capability");
  }
  return owner;
}

void verify_boundary(const Json &fixture) {
  const auto &boundary = fixture.at("Boundary");
  require(!boundary.at("AdvanceLegacyResearch").get<bool>() &&
              !boundary.at("AccrueLegacyScience").get<bool>(),
          "fixture did not freeze disabled legacy research/science");
  require(boundary.at("ConstructionAuthority") ==
              "AdaptiveResearchConstructionCapabilityView" &&
              boundary.at("ShipbuildingAuthority") ==
                  "AdaptiveResearchShipbuildingCapabilityView" &&
              boundary.at("AiKnowledgeAuthority") ==
                  "DiplomacyStrategicKnowledgeProvider(observer-filtered)" &&
              boundary.at("CombatAuthority") ==
                  "DiplomacyCampaignRuntimeCoordinator.CreateCombatCommandRuntime" &&
              boundary.at("AdvanceOrder") ==
                  "core,sensor-contacts,adaptive-research,diplomacy",
          "unexpected managed integration boundary");
}

void replay(const fs::path &research_root, const fs::path &source_root,
            const fs::path &fixture_path) {
  const std::string fixture_bytes = read_file(fixture_path);
  const Json fixture = Json::parse(fixture_bytes);
  require(fixture.at("Schema") ==
              "stellar-integrated-adaptive-campaign-oracle-v3",
          "unexpected fixture schema");
  require(fixture.at("Scope") ==
              "plain C# Main.CoreIntegration composition reconstruction, not Godot Main invocation and not player-save17 restore",
          "unexpected fixture scope");
  verify_boundary(fixture);
  verify_source_fingerprints(fixture, source_root);
  const auto fixture_digest = detail::adaptive_research_sha256(std::span(
      reinterpret_cast<const std::uint8_t *>(fixture_bytes.data()),
      fixture_bytes.size()));
  require(hex(fixture_digest) ==
              "DB84FF5A90F6BEFDFEB1E6D55C2B7636A6EA8630EC8025C0019C0D6807AD3677",
          "fixture SHA-256 mismatch");

  std::size_t invoked{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    require(row.at("Composition") ==
                "plain C# reconstruction of Main.CoreIntegration; no Godot Main invocation; no player-save17 restore",
            name + ": unexpected composition provenance");
    require(row.at("Arguments") == row.at("InputBefore") &&
                row.at("Arguments") == row.at("InputAfter"),
            name + ": managed input mutated");

    // Decode every typed native input before entering the expected production
    // exception boundary. Decode or harness defects therefore cannot be
    // mistaken for a source-compatible production error.
    const double elapsed_days = row.at("Arguments").at("Days").get<double>();
    const double end_day = row.at("Arguments").at("End").get<double>();
    const bool ai_privacy = row.at("Arguments").at("AiPrivacy").get<bool>();

    auto owner = create_owner_for_row(research_root, row);

    // The owner keeps borrowed members in one final heap allocation. Moving
    // the outer value must preserve those addresses and working callbacks.
    auto moved = std::move(owner);
    const Json capabilities_before =
        row.at("Before").at("ResearchCapabilityProjection");
    require_equal(encode_state(moved), row.at("Before"),
                  name + ": full retained before state");

    std::optional<IntegratedAdaptiveCampaignStepResult> result;
    IntegratedAdaptiveCampaignAdvanceTrace trace;
    std::exception_ptr caught;
    try {
      result = moved.advance(elapsed_days, end_day, &trace);
    } catch (...) {
      caught = std::current_exception();
    }

    require_equal(encode_error(caught), row.at("Error"), name + ": error");
    Json core_output = nullptr;
    if (trace.core)
      core_output = Json::parse(campaign_support::encode_result(*trace.core).dump());
    require_equal(core_output, row.at("Core"), name + ": core phase output");
    require_equal(encode_sensor_results(trace.sensor_contacts,
                                        capabilities_before),
                  row.at("SensorContacts"), name + ": sensor phase output");
    require_equal(trace.research_events
                      ? encode_research_events(*trace.research_events)
                      : Json(nullptr),
                  row.at("ResearchEvents"), name + ": research phase output");
    require_equal(trace.diplomacy ? encode_diplomacy_result(*trace.diplomacy)
                                  : Json(nullptr),
                  row.at("Diplomacy"), name + ": diplomacy phase output");
    require_equal(encode_phase_evidence(moved, trace, ai_privacy),
                  row.at("PhaseEvidence"), name + ": phase/cadence evidence");
    require_equal(encode_state(moved), row.at("After"),
                  name + ": full retained after state");
    require(result.has_value() == row.at("Error").is_null(),
            name + ": ordinary return/error boundary differed");

    if (name == "fresh-zero" || name == "fresh-positive-numeric-order" ||
        name == "core-failure-missing-economy") {
      auto untraced = create_owner_for_row(research_root, row);
      std::exception_ptr untraced_error;
      try {
        (void)untraced.advance(elapsed_days, end_day);
      } catch (...) {
        untraced_error = std::current_exception();
      }
      require_equal(encode_error(untraced_error), row.at("Error"),
                    name + ": default no-trace error");
      require_equal(encode_state(untraced), row.at("After"),
                    name + ": default no-trace final state");
    }
    ++invoked;
  }
  require(invoked == fixture.at("RowCount").get<std::size_t>(),
          "did not replay every managed source row");
  require(invoked == 15, "unexpected exact row count");
  std::cout << "integrated adaptive campaign parity: " << invoked
            << " actual-source rows passed\n";
}
} // namespace

int gate075_support_main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::invalid_argument(
          "Expected research root, source root, and fixture path.");
    replay(fs::absolute(argv[1]), fs::absolute(argv[2]),
           fs::absolute(argv[3]));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ExceptionType: " << typeid(error).name()
              << "\nMessage: " << error.what()
              << "\nCurrentDirectory: " << fs::current_path().string()
              << "\nResearchRoot: "
              << (argc > 1 ? argv[1] : "<missing>")
              << "\nSourceRoot: " << (argc > 2 ? argv[2] : "<missing>")
              << "\nFixturePath: " << (argc > 3 ? argv[3] : "<missing>")
              << '\n';
    return 1;
  }
}
