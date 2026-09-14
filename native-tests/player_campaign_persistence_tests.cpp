#include <stellar/core/player_campaign_persistence.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include "legacy_galaxy_payload_test_helpers.hpp"
#include "diplomacy_test_json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <typeinfo>

namespace fs = std::filesystem;
using namespace stellar::core;

namespace {

void check(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string bytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(stream), {}};
}

std::string sha256(const std::string &value) {
  const auto *first = reinterpret_cast<const std::uint8_t *>(value.data());
  return hex(detail::adaptive_research_sha256(
      std::span(first, value.size())));
}

Json rename_keys(const Json &value, bool upper) {
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto &item : value) result.push_back(rename_keys(item, upper));
    return result;
  }
  if (!value.is_object()) return value;
  Json result = Json::object();
  for (const auto &[key, item] : value.items()) {
    auto renamed = key;
    if (!renamed.empty()) {
      const auto ch = static_cast<unsigned char>(renamed.front());
      renamed.front() = static_cast<char>(upper ? std::toupper(ch) : std::tolower(ch));
    }
    result[renamed] = rename_keys(item, upper);
  }
  return result;
}

AdaptiveResearchCampaignSnapshot research_snapshot(const Json &value) {
  AdaptiveResearchCampaignSnapshot result;
  result.schema_version = value.at("SchemaVersion").get<int>();
  result.catalog_id = value.at("CatalogId").get<std::string>();
  for (const auto &item : value.at("Civilizations")) {
    AdaptiveResearchCampaignCivilizationSnapshot civilization;
    civilization.civilization_id = item.at("CivilizationId").get<int>();
    civilization.species_id = item.at("SpeciesId").get<std::string>();
    civilization.reference_profile_id = item.at("ReferenceProfileId").get<std::string>();
    civilization.applicability_context_id = item.at("ApplicabilityContextId").get<std::string>();
    const auto lower = rename_keys(item.at("Research"), false);
    civilization.research = detail::decode_adaptive_research_snapshot_v5_dto(lower.dump());
    for (const auto &funding : item.at("ProjectFunding"))
      civilization.project_funding.push_back({
          funding.at("NodeId").get<std::string>(),
          funding.at("ReservedMilestoneCredits").get<double>(),
          funding.at("ConsumedMilestoneCredits").get<double>(),
          funding.at("AuthorizationCredits").get<double>()});
    result.civilizations.push_back(std::move(civilization));
  }
  return result;
}

Json research_json(const AdaptiveResearchCampaignSnapshot &snapshot) {
  Json civilizations = Json::array();
  for (const auto &value : snapshot.civilizations) {
    Json funding = Json::array();
    for (const auto &entry : value.project_funding)
      funding.push_back({{"NodeId", entry.node_id},
                         {"ReservedMilestoneCredits", entry.reserved_milestone_credits},
                         {"ConsumedMilestoneCredits", entry.consumed_milestone_credits},
                         {"AuthorizationCredits", entry.authorization_credits}});
    const auto research = Json::parse(
        detail::encode_adaptive_research_snapshot_v5_dto(value.research));
    civilizations.push_back({{"CivilizationId", value.civilization_id},
                             {"SpeciesId", value.species_id},
                             {"ReferenceProfileId", value.reference_profile_id},
                             {"ApplicabilityContextId", value.applicability_context_id},
                             {"Research", rename_keys(research, true)},
                             {"ProjectFunding", funding}});
  }
  return {{"SchemaVersion", snapshot.schema_version},
          {"CatalogId", snapshot.catalog_id},
          {"Civilizations", civilizations}};
}

PlayerCampaignPayloadV17Dto payload(const Json &value) {
  auto galaxy_json = value;
  galaxy_json["FormatVersion"] = value.contains("GalaxyFormatVersion")
      ? value.at("GalaxyFormatVersion")
      : Json(16);
  PlayerCampaignPayloadV17Dto result;
  result.format_version = value.at("FormatVersion").get<int>();
  result.galaxy_format_version = value.contains("GalaxyFormatVersion")
      ? std::optional(value.at("GalaxyFormatVersion").get<int>())
      : std::nullopt;
  result.galaxy = gate090_current::decode_payload(galaxy_json);
  if (value.contains("Diplomacy") && !value.at("Diplomacy").is_null())
    result.diplomacy = snapshot(value.at("Diplomacy"));
  if (value.contains("AdaptiveResearch") && !value.at("AdaptiveResearch").is_null())
    result.adaptive_research = research_snapshot(value.at("AdaptiveResearch"));
  return result;
}

void check_result(const RestoredPlayerCampaignV17 &actual,
                  const Json &expected, const std::string &label) {
  auto raw = expected;
  if (!raw.at("Galaxy").contains("ActiveCombatEncounter"))
    raw["Galaxy"]["ActiveCombatEncounter"] = nullptr;
  if (!raw.at("Galaxy").contains("CombatIntelligence"))
    raw["Galaxy"]["CombatIntelligence"] = nullptr;
  gate090_current::check_raw_world(actual.galaxy(), raw, label + ": galaxy");
  check(jsnapshot(actual.diplomacy().snapshot()) == expected.at("Diplomacy"),
        label + ": diplomacy");
  const auto research = AdaptiveResearchCampaignSnapshotCodec(
      actual.research_runtime()).capture(actual.research());
  check(research_json(research) ==
            research_json(research_snapshot(expected.at("AdaptiveResearch"))),
        label + ": research");
  const auto expected_game_version = expected.at("GameVersion").get<std::string>();
  const auto expected_saved_at = expected.at("SavedAtUtc").get<std::string>();
  check(actual.simulation_days() == expected.at("SimulationDays").get<double>() &&
        actual.game_version() == std::string_view(expected_game_version) &&
        actual.saved_at_utc() == std::string_view(expected_saved_at),
        label + ": metadata");
}

Json world_projection(Json expected) {
  auto &galaxy = expected.at("Galaxy");
  if (!galaxy.contains("ActiveCombatEncounter"))
    galaxy["ActiveCombatEncounter"] = nullptr;
  if (!galaxy.contains("CombatIntelligence"))
    galaxy["CombatIntelligence"] = nullptr;
  return expected;
}

void check_capture(const PlayerCampaignPayloadV17Dto &actual,
                   const Json &expected, const std::string &label) {
  auto galaxy_expected = expected;
  galaxy_expected["FormatVersion"] = 16;
  gate090_current::check_payload(actual.galaxy, galaxy_expected,
                                 label + ": galaxy DTO");
  check(actual.diplomacy.has_value(), label + ": diplomacy presence");
  check(jsnapshot(*actual.diplomacy) == expected.at("Diplomacy"),
        label + ": diplomacy DTO");
  check(actual.adaptive_research.has_value(), label + ": research presence");
  const auto expected_research =
      research_json(research_snapshot(expected.at("AdaptiveResearch")));
  check(research_json(*actual.adaptive_research) == expected_research,
        label + ": research DTO");
  check(actual.format_version == expected.at("FormatVersion").get<int>() &&
            actual.galaxy_format_version ==
                std::optional(expected.at("GalaxyFormatVersion").get<int>()),
        label + ": wrapper versions");
}

void check_input(const PlayerCampaignPayloadV17Dto &actual,
                 const Json &expected, const std::string &label) {
  auto galaxy_expected = expected;
  galaxy_expected["FormatVersion"] = expected.contains("GalaxyFormatVersion")
      ? expected.at("GalaxyFormatVersion")
      : Json(16);
  gate090_current::check_payload(actual.galaxy, galaxy_expected,
                                 label + ": galaxy");
  check(actual.format_version == expected.at("FormatVersion").get<int>(),
        label + ": format");
  const auto expected_galaxy_format = expected.contains("GalaxyFormatVersion")
      ? std::optional(expected.at("GalaxyFormatVersion").get<int>())
      : std::nullopt;
  check(actual.galaxy_format_version == expected_galaxy_format,
        label + ": galaxy format");
  check(actual.diplomacy.has_value() ==
            (expected.contains("Diplomacy") && !expected.at("Diplomacy").is_null()),
        label + ": diplomacy presence");
  if (actual.diplomacy)
    check(jsnapshot(*actual.diplomacy) == expected.at("Diplomacy"),
          label + ": diplomacy");
  check(actual.adaptive_research.has_value() ==
            (expected.contains("AdaptiveResearch") &&
             !expected.at("AdaptiveResearch").is_null()),
        label + ": research presence");
  if (actual.adaptive_research)
    check(research_json(*actual.adaptive_research) ==
              research_json(research_snapshot(expected.at("AdaptiveResearch"))),
          label + ": research");
}

struct ResultError { std::string type; std::string message; std::optional<std::string> inner_type; std::optional<std::string> inner_message; };
ResultError classify(std::exception_ptr failure) {
  try { std::rethrow_exception(failure); }
  catch (const PlayerCampaignPersistenceDataError &e) { return {"InvalidDataException",e.what(),e.inner_type(),e.inner_message()}; }
  catch (const PlayerCampaignPersistenceOperationError &e) { return {"InvalidOperationException",e.what(),{}, {}}; }
  catch (const PlayerCampaignPersistenceRangeError &e) { return {"ArgumentOutOfRangeException",e.what(),{}, {}}; }
  catch (const DiplomacyArgumentRangeError &e) {
    return {"ArgumentOutOfRangeException", e.what(), {}, {}};
  }
  catch (const AdaptiveResearchCampaignDataError &e) { return {"InvalidDataException",e.what(),{}, {}}; }
  catch (const GalaxyPayloadPersistenceDataError &e) { return {"InvalidDataException",e.what(),e.inner_type(),e.inner_message()}; }
  catch (const GalaxyEconomyPersistenceDataError &e) {
    return {"InvalidDataException", e.what(), {}, {}};
  }
  catch (const DiplomacySnapshotValidationError &e) {
    return {"DiplomacySnapshotValidationException", e.what(), {}, {}};
  }
  catch (const std::exception &e) { return {"UnexpectedNativeException",e.what(),{}, {}}; }
}

int run(const fs::path &fixture_path, const fs::path &source_root,
        const fs::path &research_root) {
  const auto fixture_bytes = bytes(fixture_path);
  check(sha256(fixture_bytes) ==
            "5471AD98B38E5801F598B32BE5EFE7CCCD509B8E0BA55100C33D7291CB45ACF1",
        "fixture fingerprint");
  const auto fixture = Json::parse(fixture_bytes);
  check(fixture.at("RowCount") == 17 && fixture.at("SourceOnlyRows") == 0,
        "exact row accounting");
  check(fixture.at("Rows").size() == fixture.at("RowCount").get<std::size_t>(), "row count");
  const auto row_named = [&](std::string_view wanted) -> const Json & {
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      if (name.compare(wanted) == 0)
        return row;
    }
    throw std::runtime_error("missing retained row " + std::string(wanted));
  };
  const auto &populated = row_named("capture-valid").at("Result");
  const auto &populated_diplomacy = populated.at("Diplomacy");
  check(!populated_diplomacy.at("Contacts").empty() &&
            !populated_diplomacy.at("Relationships").empty() &&
            !populated_diplomacy.at("Agreements").empty() &&
            !populated_diplomacy.at("Proposals").empty() &&
            !populated_diplomacy.at("RecentHistory").empty(),
        "populated Diplomacy coverage");
  bool has_paid_progress = false;
  for (const auto &civilization :
       populated.at("AdaptiveResearch").at("Civilizations")) {
    const auto &funding = civilization.at("ProjectFunding");
    const auto &projects = civilization.at("Research")
                               .at("Research")
                               .at("Research")
                               .at("Research")
                               .at("Core")
                               .at("ActiveProjects");
    if (!funding.empty() && !projects.empty() &&
        funding.front().at("AuthorizationCredits").get<double>() > 0.0 &&
        projects.front().at("StageResearchPoints").get<double>() > 0.0)
      has_paid_progress = true;
  }
  check(has_paid_progress, "paid active research coverage");
  const auto &advance_input =
      row_named("restore-activate-advance-nonbattle").at("Input");
  const auto &advance_result =
      row_named("restore-activate-advance-nonbattle").at("Result");
  bool advanced_project = false;
  const auto &before_civilizations =
      advance_input.at("AdaptiveResearch").at("Civilizations");
  const auto &after_civilizations =
      advance_result.at("AdaptiveResearch").at("Civilizations");
  check(before_civilizations.size() == after_civilizations.size(),
        "advance civilization count");
  for (std::size_t index = 0; index < before_civilizations.size(); ++index) {
    const auto &before_civilization = before_civilizations.at(index);
    const auto &after_civilization = after_civilizations.at(index);
    check(before_civilization.at("ProjectFunding") ==
              after_civilization.at("ProjectFunding"),
          "advance must not replay project authorization");
    const auto &before_projects = before_civilization.at("Research")
                                      .at("Research")
                                      .at("Research")
                                      .at("Research")
                                      .at("Core")
                                      .at("ActiveProjects");
    const auto &after_projects = after_civilization.at("Research")
                                     .at("Research")
                                     .at("Research")
                                     .at("Research")
                                     .at("Core")
                                     .at("ActiveProjects");
    if (!before_projects.empty() && !after_projects.empty() &&
        after_projects.front().at("StageResearchPoints").get<double>() >
            before_projects.front().at("StageResearchPoints").get<double>())
      advanced_project = true;
  }
  check(advanced_project, "subsequent advance progresses funded research");
  const std::array<std::string_view, 11> expected_sources{
      "Persistence/CampaignStatePersistenceService.cs",
      "Persistence/DiplomacyCampaignReferenceValidator.cs",
      "Persistence/CampaignSaveService.cs",
      "Simulation/Diplomacy/DiplomacySnapshotInvariantValidator.cs",
      "Simulation/Diplomacy/DiplomacySystem.cs",
      "Simulation/Research/Adaptive/AdaptiveResearchCampaignState.cs",
      "Presentation/Main.CoreIntegration.cs",
      "Simulation/GalaxySimulationStepCoordinator.cs",
      "Simulation/Research/Adaptive/AdaptiveResearchCampaignSimulation.cs",
      "Simulation/Diplomacy/DiplomacyCampaignRuntimeCoordinator.cs",
      "Simulation/Combat/FleetCombatPower.cs"};
  check(fixture.at("SourceFiles").size() == expected_sources.size(),
        "source inventory count");
  std::vector<std::pair<fs::path, std::string>> source_fingerprints;
  for (std::size_t index = 0; index < expected_sources.size(); ++index) {
    const auto &source = fixture.at("SourceFiles").at(index);
    const auto source_path = source.at("Path").get<std::string>();
    check(source_path.compare(expected_sources[index]) == 0,
          "source inventory order");
    const auto path = source_root / fs::path(expected_sources[index]);
    const auto fingerprint = sha256(bytes(path));
    check(fingerprint == source.at("Sha256").get<std::string>(),
          "source fingerprint for " + path.string());
    source_fingerprints.emplace_back(path, fingerprint);
  }
  int passed = 0;
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto operation = row.at("Operation").get<std::string>();
    check(operation == "Restore" || operation == "RestoreActivateSuccess" ||
              operation == "RestoreActivate" ||
              operation == "RestoreAdvance" || operation == "Capture",
          name + ": unknown operation");
    auto input = payload(row.at("Input"));
    check_input(input, row.at("Before"), name + ": input before");
    std::optional<PlayerCampaignPayloadV17Dto> captured;
    std::optional<RestoredPlayerCampaignV17> restored;
    std::optional<IntegratedAdaptiveCampaignRuntime> active;
    std::optional<AdaptiveResearchStrategicRuntime> operation_runtime;
    double capture_day{};
    if (operation == "Capture") {
      const auto &control = row.at("Input").at("Control");
      const bool invalid_diplomacy = control.value(
          "InvalidDiplomacyTrust", false);
      const bool invalid_economy = control.value(
          "InvalidEconomyPriority", false);
      const bool null_combat = control.value("NullFirstFleetCombat", false);
      auto setup = input;
      if (invalid_diplomacy)
        setup.diplomacy->relationships.front().trust = 0.0;
      if (invalid_economy)
        setup.galaxy.economies->front().industry_priority.reset();
      auto prepared = restore_player_campaign_v17(
          load_adaptive_research_strategic_runtime(research_root), setup);
      active.emplace(std::move(prepared).activate());
      if (invalid_diplomacy) {
        const auto &source_relationship = input.diplomacy->relationships.front();
        auto &relationship = detail::DiplomacyStateAccess::relationship(
            active->diplomacy(), source_relationship.civilization_a_id,
            source_relationship.civilization_b_id);
        relationship.trust = source_relationship.trust;
      }
      if (invalid_economy)
        active->world().campaign().economies.front().industry_priority =
            static_cast<IndustryPriority>(999);
      if (null_combat)
        active->world().campaign().fleets.front().combat.reset();
      if (control.at("DeveloperProvenance").get<bool>())
        active->world().campaign().developer_provenance =
            CampaignDeveloperProvenance{true};
      const auto before_world = world_projection(row.at("Before"));
      gate090_current::check_raw_world(active->world().campaign(), before_world,
                                       name + ": live before");
      const auto actual_diplomacy_before =
          jsnapshot(active->diplomacy().snapshot());
      check(actual_diplomacy_before == row.at("Before").at("Diplomacy"),
            name + ": live diplomacy before actual=" +
                actual_diplomacy_before.dump() + " expected=" +
                row.at("Before").at("Diplomacy").dump());
      capture_day = control.at("SimulationDays").is_string()
          ? std::numeric_limits<double>::quiet_NaN()
          : control.at("SimulationDays").get<double>();
    } else {
      operation_runtime.emplace(
          load_adaptive_research_strategic_runtime(research_root));
    }
    bool stable_move_preserved = true;
    bool runtime_association_preserved = true;
    std::exception_ptr failure;
    try {
      if (operation == "Restore") {
        restored = restore_player_campaign_v17(
            std::move(*operation_runtime), input);
      } else if (operation == "RestoreActivateSuccess") {
        auto prepared = restore_player_campaign_v17(
            std::move(*operation_runtime), input);
        active.emplace(std::move(prepared).activate());
        captured = capture_player_campaign_v17(
            *active, {input.galaxy.simulation_days,
                      input.galaxy.game_version,
                      input.galaxy.saved_at_utc});
      } else if (operation == "RestoreActivate") {
        restored = restore_player_campaign_v17(
            std::move(*operation_runtime), input);
        auto unusable = std::move(*restored).activate();
        (void)unusable;
      } else if (operation == "RestoreAdvance") {
        auto prepared = restore_player_campaign_v17(
            std::move(*operation_runtime), input);
        const auto *galaxy_address = &prepared.galaxy();
        const auto *research_address = &prepared.research();
        RestoredPlayerCampaignV17 moved(std::move(prepared));
        stable_move_preserved = &moved.galaxy() == galaxy_address &&
                                &moved.research() == research_address;
        active.emplace(std::move(moved).activate());
        runtime_association_preserved =
            &active->research().runtime() == &active->research_runtime();
        const double end_day = input.galaxy.simulation_days + 0.25;
        (void)active->advance(0.25, end_day);
        captured = capture_player_campaign_v17(
            *active, {end_day, input.galaxy.game_version,
                      input.galaxy.saved_at_utc});
      } else if (operation == "Capture") {
        captured = capture_player_campaign_v17(
            *active, {capture_day, input.galaxy.game_version,
                      input.galaxy.saved_at_utc});
      }
    } catch (...) { failure = std::current_exception(); }

    if (operation == "RestoreAdvance" && !failure) {
      check(stable_move_preserved, name + ": stable owner move");
      check(runtime_association_preserved,
            name + ": activated research/runtime association");
    }

    if (row.at("ErrorType").is_null()) {
      if (failure) {
        const auto unexpected = classify(failure);
        throw std::runtime_error(name + ": unexpected " + unexpected.type +
                                 " '" + unexpected.message + "'");
      }
      if (restored) check_result(*restored, row.at("Result"), name);
      else {
        check(captured.has_value(), name + ": capture result");
        auto expected = row.at("Result");
        check_capture(*captured, expected, name);
      }
    } else {
      check(failure != nullptr, name + ": expected failure");
      const auto actual = classify(failure);
      check(actual.type == row.at("ErrorType").get<std::string>(), name + ": error type " + actual.type + " '" + actual.message + "'");
      check(actual.message == row.at("ErrorMessage").get<std::string>(), name + ": error message");
      const auto expected_inner_type = row.at("InnerType").is_null()
          ? std::optional<std::string>{}
          : std::optional(row.at("InnerType").get<std::string>());
      const auto expected_inner_message = row.at("InnerMessage").is_null()
          ? std::optional<std::string>{}
          : std::optional(row.at("InnerMessage").get<std::string>());
      check(actual.inner_type == expected_inner_type,
            name + ": inner error type");
      check(actual.inner_message == expected_inner_message,
            name + ": inner error message");
    }
    if (operation == "Capture") {
      check(active.has_value(), name + ": live state retained");
      const auto after_world = world_projection(row.at("After"));
      gate090_current::check_raw_world(active->world().campaign(),
                                       after_world,
                                       name + ": live after");
      check(jsnapshot(active->diplomacy().snapshot()) ==
                row.at("After").at("Diplomacy"),
            name + ": live diplomacy after");
      check_input(input, row.at("Input"), name + ": input unchanged");
    } else {
      check_input(input, row.at("After"), name + ": input after");
    }
    ++passed;
  }

  const auto valid_input = payload(row_named("restore-valid").at("Input"));
  auto incoming = restore_player_campaign_v17(
      load_adaptive_research_strategic_runtime(research_root), valid_input);
  const auto *incoming_world = &incoming.galaxy();
  const auto *incoming_research = &incoming.research();
  auto target = restore_player_campaign_v17(
      load_adaptive_research_strategic_runtime(research_root), valid_input);
  target = std::move(incoming);
  check(&target.galaxy() == incoming_world &&
            &target.research() == incoming_research,
        "move-assignment preserves incoming stable storage");

  const auto negative_input = payload(
      row_named("restore-negative-time-activation-fails").at("Input"));
  auto consumed = restore_player_campaign_v17(
      load_adaptive_research_strategic_runtime(research_root), negative_input);
  bool activation_failed = false;
  try {
    auto unusable = std::move(consumed).activate();
    (void)unusable;
  } catch (const std::exception &) {
    activation_failed = true;
  }
  check(activation_failed,
        "large persisted time is returned before host activation rejects it");
  auto replacement = restore_player_campaign_v17(
      load_adaptive_research_strategic_runtime(research_root), valid_input);
  consumed = std::move(replacement);
  check(consumed.simulation_days() == valid_input.galaxy.simulation_days,
        "consumed-on-error owner accepts move assignment");

  for (const auto &[path, fingerprint] : source_fingerprints)
    check(sha256(bytes(path)) == fingerprint,
          "source changed during replay: " + path.string());
  check(sha256(bytes(fixture_path)) == sha256(fixture_bytes),
        "fixture changed during replay");
  std::cout << "Player17 typed campaign parity: " << passed << '/' << passed << " rows passed.\n";
  return 0;
}
}

int main(int argc, char **argv) {
  fs::path fixture = argc > 1 ? fs::absolute(argv[1]) : fs::path("<missing>");
  fs::path source = argc > 2 ? fs::absolute(argv[2]) : fs::path("<missing>");
  fs::path research = argc > 3 ? fs::absolute(argv[3]) : fs::path("<missing>");
  try {
    if (argc != 4) throw std::runtime_error("Usage: player_campaign_persistence_tests <fixture> <Game source root> <research root>");
    return run(fixture, source, research);
  } catch (const PlayerCampaignPersistenceDataError &e) {
    std::cerr << "class " << typeid(e).name() << ": " << e.what() << '\n';
    if (e.inner_type())
      std::cerr << "Inner " << *e.inner_type() << ": "
                << e.inner_message().value_or("") << '\n';
    std::cerr << "Working directory: " << fs::current_path().string() << '\n'
              << "Fixture path: " << fixture.string() << '\n'
              << "Source root: " << source.string() << '\n'
              << "Research root: " << research.string() << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "class " << typeid(e).name() << ": " << e.what() << '\n'
              << "Working directory: " << fs::current_path().string() << '\n'
              << "Fixture path: " << fixture.string() << '\n'
              << "Source root: " << source.string() << '\n'
              << "Research root: " << research.string() << '\n';
    return 1;
  }
}
