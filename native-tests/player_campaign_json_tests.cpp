#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planetary_satellites.hpp>

#include "../core/src/player_campaign_json_research.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;

namespace stellar::core::player_json_detail {
nlohmann::ordered_json jsnapshot(const DiplomacyStateSnapshot &snapshot);
}

namespace {

void check(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

std::string read_bytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(stream), {}};
}

std::string sha256(std::string_view value) {
  const auto *begin = reinterpret_cast<const std::uint8_t *>(value.data());
  const auto digest = detail::adaptive_research_sha256(
      std::span<const std::uint8_t>(begin, value.size()));
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest)
    output << std::setw(2) << static_cast<unsigned>(byte);
  auto result = output.str();
  for (auto &ch : result)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return result;
}

std::optional<std::string> optional_text(const Json &value,
                                         std::string_view key) {
  const auto &member = value.at(key);
  return member.is_null() ? std::nullopt
                          : std::optional(member.get<std::string>());
}

// Original composition path, kept only as a compatibility oracle: production
// must no longer serialize and parse a complete intermediate galaxy document.
std::string legacy_composed_encoding(const PlayerCampaignPayloadV17Dto &payload) {
  auto root = nlohmann::ordered_json::parse(encode_galaxy_payload_v16_json(payload.galaxy));
  root["FormatVersion"] = payload.format_version;
  root["GalaxyFormatVersion"] = payload.galaxy_format_version
      ? nlohmann::ordered_json(*payload.galaxy_format_version) : nlohmann::ordered_json(nullptr);
  root["Diplomacy"] = payload.diplomacy
      ? player_json_detail::jsnapshot(*payload.diplomacy) : nlohmann::ordered_json(nullptr);
  root["AdaptiveResearch"] = payload.adaptive_research
      ? player_json_detail::encode_research(*payload.adaptive_research) : nlohmann::ordered_json(nullptr);
  root["EventHistory"] = payload.event_history
      ? player_json_detail::encode_event_history(*payload.event_history) : nlohmann::ordered_json(nullptr);
  return root.dump(2);
}

void benchmark_encoding(const fs::path &save, const fs::path &research_root) {
  auto restored = restore_player_campaign_v17_json(
      load_adaptive_research_strategic_runtime(research_root), read_bytes(save));
  const PlayerCampaignCaptureOptions options{restored.simulation_days(),
      std::string(restored.game_version()), std::string(restored.saved_at_utc())};
  auto active = std::move(restored).activate();
  const auto payload = capture_player_campaign_v17(active, options);
  for (int trial = 0; trial < 3; ++trial) {
    const auto start = std::chrono::steady_clock::now();
    const auto encoded = encode_player_campaign_v17_json(payload);
    const auto end = std::chrono::steady_clock::now();
    std::cout << "campaign_encode trial=" << trial << " systems=" << active.world().campaign().systems.size()
              << " bytes=" << encoded.size() << " ms="
              << std::chrono::duration<double, std::milli>(end - start).count()
              << " sha256=" << sha256(encoded) << '\n';
  }
}

void check_player_research_enum_encoding() {
  AdaptiveResearchCampaignSnapshot snapshot;
  snapshot.schema_version = 2;
  snapshot.catalog_id = "enum-regression";
  AdaptiveResearchCampaignCivilizationSnapshot civilization;
  civilization.civilization_id = 7;
  civilization.species_id = "species-id";
  civilization.reference_profile_id = "profile-id";
  civilization.applicability_context_id = "context-id";

  ResearchOutcomeHistoryRecord outcome;
  outcome.sequence = 5;
  outcome.node_id = "node-id";
  outcome.checkpoint_id = "checkpoint-id";
  outcome.attempt_index = 2;
  outcome.outcome = ResearchOutcomeKind::hypothesis_supported;
  outcome.year = 42.5;
  outcome.explanation = "A readable explanation remains a string.";
  civilization.research.outcomes.recent_records.push_back(std::move(outcome));

  ForeignTechnologyAssessmentSnapshot assessment;
  assessment.foreign_technology_reference = "foreign-reference";
  assessment.source_lineage_reference = "lineage-reference";
  assessment.understanding = ForeignUnderstandingState::engineering_understood;
  assessment.operability = ForeignOperabilityState::adapted_operation;
  assessment.reproduction = ForeignReproductionState::foreign_process_replication;
  assessment.adaptation = ForeignAdaptationState::native_derivative;
  civilization.research.research.foreign_assessments.push_back(
      std::move(assessment));

  ResearchTacitAssetSnapshot asset;
  asset.asset_id = "asset-id";
  asset.asset_type_id = "asset-type-id";
  asset.scope_kind = ResearchTacitScopeKind::facility_or_process;
  asset.scope_ref = "scope-reference";
  asset.assimilation_stage = ResearchTacitAssimilationStage::native_practice;
  asset.provenance = "provenance";
  civilization.research.research.research.research.expertise.tacit_assets
      .push_back(std::move(asset));
  snapshot.civilizations.push_back(std::move(civilization));

  const auto encoded = player_json_detail::encode_research(snapshot);
  const auto &research = encoded.at("Civilizations").at(0).at("Research");
  const auto &record = research.at("Outcomes").at("RecentRecords").at(0);
  check(record.at("Outcome").is_number_integer(),
        "Player17 outcome enum must be numeric");
  check(record.at("Outcome") ==
            static_cast<int>(ResearchOutcomeKind::hypothesis_supported),
        "Player17 outcome enum value");
  check(record.at("Explanation") == "A readable explanation remains a string.",
        "Player17 preserves non-enum outcome strings");

  const auto &v4 = research.at("Research");
  const auto &foreign = v4.at("ForeignAssessments").at(0);
  for (const auto key : {"Understanding", "Operability", "Reproduction",
                         "Adaptation"})
    check(foreign.at(key).is_number_integer(),
          std::string("Player17 foreign enum must be numeric: ") + key);
  check(foreign.at("Understanding") ==
            static_cast<int>(ForeignUnderstandingState::engineering_understood) &&
            foreign.at("Operability") ==
                static_cast<int>(ForeignOperabilityState::adapted_operation) &&
            foreign.at("Reproduction") == static_cast<int>(
                ForeignReproductionState::foreign_process_replication) &&
            foreign.at("Adaptation") ==
                static_cast<int>(ForeignAdaptationState::native_derivative),
        "Player17 foreign enum values");
  check(foreign.at("ForeignTechnologyReference") == "foreign-reference",
        "Player17 preserves foreign reference string");

  const auto &tacit = v4.at("Research")
                          .at("Research")
                          .at("Expertise")
                          .at("TacitAssets")
                          .at(0);
  check(tacit.at("ScopeKind").is_number_integer() &&
            tacit.at("AssimilationStage").is_number_integer(),
        "Player17 tacit enums must be numeric");
  check(tacit.at("ScopeKind") ==
            static_cast<int>(ResearchTacitScopeKind::facility_or_process) &&
            tacit.at("AssimilationStage") ==
                static_cast<int>(ResearchTacitAssimilationStage::native_practice),
        "Player17 tacit enum values");
  check(tacit.at("ScopeRef") == "scope-reference",
        "Player17 preserves tacit scope reference string");
}

void check_success(RestoredPlayerCampaignV17 restored, const Json &expected,
                   const std::string &label, const fs::path &native_output,
                   const fs::path &research_root) {
  check(restored.simulation_days() ==
            expected.at("SimulationDays").get<double>(),
        label + ": simulation days");
  check(restored.game_version() ==
            expected.at("GameVersion").get<std::string>(),
        label + ": game version");
  check(restored.saved_at_utc() == expected.at("SavedAtUtc").get<std::string>(),
        label + ": saved timestamp");

  auto active = std::move(restored).activate();
  const PlayerCampaignCaptureOptions options{
      expected.at("SimulationDays").get<double>(),
      expected.at("GameVersion").get<std::string>(),
      expected.at("SavedAtUtc").get<std::string>()};
  const auto recaptured = capture_player_campaign_v17(active, options);
  const auto encoded = encode_player_campaign_v17_json(recaptured);
  check(encoded == legacy_composed_encoding(recaptured),
        label + ": composed encoder changed persisted bytes");
  check(encode_player_campaign_v17_json(recaptured) == encoded,
        label + ": repeated encoding changed bytes or input state");
  auto actual_json = Json::parse(encoded);
  auto expected_json = expected;
  expected_json.erase("Control");
  // The frozen C# oracle predates native appearance migration and the unscaled
  // stellar clock. Check the new data by a complete second load/capture, then
  // compare every original field to the immutable compatibility fixture.
  auto replay=restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research_root),encoded);
  auto replay_active=std::move(replay).activate();
  check(encode_player_campaign_v17_json(capture_player_campaign_v17(replay_active,options))==encoded,
        label+": migrated appearance/activity metadata changed on second load");
  if(!expected_json.at("Galaxy").contains("StellarActivityDay")){
    check(actual_json.at("Galaxy").at("StellarActivityDay")==options.simulation_days,
          label+": old-save activity clock did not initialize at the saved epoch");
    actual_json["Galaxy"].erase("StellarActivityDay");
  }
  // The frozen oracle predates the persistent chronicle tail: the second
  // load/capture above already proves "EventHistory" round-trips through
  // restore and re-encode, so compare every original fixture field without it.
  if(!expected_json.contains("EventHistory")){
    check(actual_json.contains("EventHistory"),
          label+": missing persistent chronicle tail");
    actual_json.erase("EventHistory");
  }
  auto& actual_bodies=actual_json["Galaxy"]["PlanetaryBodies"];
  const auto& expected_bodies=expected_json.at("Galaxy").at("PlanetaryBodies");
  // The frozen oracle predates the canonical Sol expansion: loading a legacy
  // save performs upgrade_saved_sol_catalog (verified by sol_catalog and
  // native_moons tests). Drop exactly those appended canonical bodies — a
  // missing expected body or an unexpected addition still fails below.
  std::unordered_set<int> canonical_sol_additions;
  for(const auto& moon:sol_moon_definitions())canonical_sol_additions.insert(moon.id);
  canonical_sol_additions.insert(pluto_body_id);
  Json migrated_bodies=Json::array();
  for(const auto& body:actual_bodies){
    const bool canonical_addition=body.at("SystemId").get<int>()==sol_system_id&&
        canonical_sol_additions.contains(body.at("Id").get<int>())&&
        std::none_of(expected_bodies.begin(),expected_bodies.end(),
            [&](const Json& e){return e.at("Id")==body.at("Id");});
    if(!canonical_addition)migrated_bodies.push_back(body);
  }
  actual_bodies=std::move(migrated_bodies);
  check(actual_bodies.size()==expected_bodies.size(),label+": migrated body count changed: expected "+
      std::to_string(expected_bodies.size())+" got "+std::to_string(actual_bodies.size()));
  for(std::size_t i=0;i<expected_bodies.size();++i)if(!expected_bodies[i].contains("PlanetAppearance")){
    check(actual_bodies[i].contains("PlanetAppearance")&&actual_bodies[i]["PlanetAppearance"].is_object(),
          label+": missing native planet appearance migration");
    actual_bodies[i].erase("PlanetAppearance");
  }
  auto normalize_float_coordinates = [&](auto &&self, Json &value) -> void {
    if (value.is_array()) {
      for (auto &item : value)
        self(self, item);
    } else if (value.is_object()) {
      for (auto &[key, item] : value.items()) {
        if ((key == "X" || key == "Y") && item.is_number())
          item = static_cast<double>(static_cast<float>(item.get<double>()));
        else
          self(self, item);
      }
    }
  };
  normalize_float_coordinates(normalize_float_coordinates, expected_json);
  if (actual_json != expected_json)
    throw std::runtime_error(label + ": complete recapture mismatch: " +
                             Json::diff(expected_json, actual_json).dump());
  if (label == "valid-current17" ||
      label == "valid-populated-research-dictionaries") {
    const auto output =
        label == "valid-current17"
            ? native_output
            : native_output.parent_path() / "native-dictionaries.json";
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    check(bool(stream), "Cannot write native interoperability input");
    stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    check(bool(stream), "Could not complete native interoperability input");
  }

  if (label == "valid-current17") {
    auto unicode = recaptured;
    unicode.galaxy.game_version = "Unicode \xc3\xa9 \xe6\x98\x9f \xf0\x9f\x8c\x8c";
    unicode.galaxy.simulation_days = -0.0;
    unicode.galaxy.systems->front().name = "Escapes \"\n\\ and \xc3\xb1";
    check(encode_player_campaign_v17_json(unicode) == legacy_composed_encoding(unicode),
          "Unicode, escapes or negative zero changed during save composition");
    unicode.diplomacy.reset();
    unicode.adaptive_research.reset();
    unicode.galaxy_format_version.reset();
    check(encode_player_campaign_v17_json(unicode) == legacy_composed_encoding(unicode),
          "Optional empty save sections changed during composition");
    for (const auto &bad : {std::string(1, static_cast<char>(0xc3)),
                            std::string("\xed\xa0\x80"), std::string("\xc0\xaf")}) {
      unicode.galaxy.game_version = bad;
      try {
        (void)encode_player_campaign_v17_json(unicode);
        throw std::runtime_error("Invalid UTF-8 player encoding succeeded");
      } catch (const PlayerCampaignJsonError &error) {
        check(error.stage() == PlayerCampaignJsonStage::Encode && error.path() == "$",
              "Invalid UTF-8 galaxy error lost its player encode stage/path");
      }
    }
    auto bad_text = recaptured;
    bad_text.adaptive_research->catalog_id = std::string("\xf4\x90\x80\x80");
    try {
      (void)encode_player_campaign_v17_json(bad_text);
      throw std::runtime_error("Invalid UTF-8 research encoding succeeded");
    } catch (const PlayerCampaignJsonError &error) {
      check(error.stage() == PlayerCampaignJsonStage::Encode && error.path().empty(),
            "Invalid UTF-8 research error changed its encode stage/path");
    }
    auto bad_research = recaptured;
    bad_research.adaptive_research->civilizations.front()
        .project_funding.push_back(
            {"nonfinite", std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0});
    try {
      (void)encode_player_campaign_v17_json(bad_research);
      throw std::runtime_error("non-finite research encoding succeeded");
    } catch (const PlayerCampaignJsonError &error) {
      check(error.stage() == PlayerCampaignJsonStage::Encode,
            "non-finite research encoding phase");
    }

    auto bad_nested_research = recaptured;
    bad_nested_research.adaptive_research->civilizations.front()
        .research.research.research.research.expertise.fields.front()
        .current.theoretical = std::numeric_limits<double>::infinity();
    try {
      (void)encode_player_campaign_v17_json(bad_nested_research);
      throw std::runtime_error("non-finite nested research encoding succeeded");
    } catch (const PlayerCampaignJsonError &error) {
      check(error.stage() == PlayerCampaignJsonStage::Encode,
            "non-finite nested research encoding phase");
      check(error.path().find("/Expertise/Fields/") != std::string::npos,
            "non-finite nested research encoding path");
    }

    auto bad_optional_research = recaptured;
    bad_optional_research.adaptive_research->civilizations.front()
        .research.research.research.agenda.last_major_review_year =
        std::numeric_limits<double>::quiet_NaN();
    try {
      (void)encode_player_campaign_v17_json(bad_optional_research);
      throw std::runtime_error(
          "non-finite optional research encoding succeeded");
    } catch (const PlayerCampaignJsonError &error) {
      check(error.stage() == PlayerCampaignJsonStage::Encode,
            "non-finite optional research encoding phase");
      check(error.path().ends_with("/LastMajorReviewYear"),
            "non-finite optional research encoding path");
    }

    auto bad_diplomacy = recaptured;
    DiplomaticContactSnapshot contact;
    contact.observer_civilization_id = 0;
    contact.contact_id = "nonfinite";
    contact.awareness = ContactAwareness::detected_unidentified;
    contact.confidence = std::numeric_limits<double>::quiet_NaN();
    bad_diplomacy.diplomacy->contacts.push_back(std::move(contact));
    try {
      (void)encode_player_campaign_v17_json(bad_diplomacy);
      throw std::runtime_error("non-finite diplomacy encoding succeeded");
    } catch (const PlayerCampaignJsonError &error) {
      check(error.stage() == PlayerCampaignJsonStage::Encode,
            "non-finite diplomacy encoding phase");
    }
  }
}

} // namespace

int main(int argc, char **argv) try {
  if (argc == 4 && std::string_view(argv[1]) == "--benchmark") {
    benchmark_encoding(argv[2], argv[3]);
    return 0;
  }
  if (argc != 4)
    throw std::runtime_error("Usage: player_campaign_json_tests <fixture> "
                             "<research root> <native output>");
  const fs::path fixture = fs::absolute(argv[1]);
  const fs::path research_root = fs::absolute(argv[2]);
  const fs::path native_output = fs::absolute(argv[3]);
  check_player_research_enum_encoding();
  const auto fixture_bytes = read_bytes(fixture);
  check(sha256(fixture_bytes) ==
            "138CDDA12594A77352294FE265F0632FCF9D3CAE9DEEDBAE869D51FE30ED8FCF",
        "Gate092 fixture SHA-256 mismatch");
  const auto document = Json::parse(fixture_bytes);
  check(document.at("RowCount") == document.at("Rows").size(),
        "fixture row accounting");

  std::size_t successes = 0;
  std::size_t errors = 0;
  for (const auto &row : document.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto input = row.at("InputJson").get<std::string>();
    check(sha256(input) == row.at("BeforeSha256").get<std::string>(),
          name + ": decoded input fingerprint");
    check(row.at("BeforeSha256") == row.at("AfterSha256"),
          name + ": actual source mutated its input file");
    const auto expected_type = optional_text(row, "ErrorType");
    try {
      auto runtime = load_adaptive_research_strategic_runtime(research_root);
      auto restored =
          restore_player_campaign_v17_json(std::move(runtime), input);
      check(!expected_type, name + ": native succeeded unexpectedly");
      check_success(std::move(restored), row.at("Result"), name, native_output,research_root);
      ++successes;
    } catch (const PlayerCampaignJsonError &error) {
      check(expected_type.has_value(), name + ": unexpected native JSON error");
      const bool expected_representability =
          name == "null-research-species-id" ||
          name == "null-research-node-id" ||
          name == "null-research-node-list-entry" ||
          name == "null-research-active-pressure-id" ||
          name == "null-research-context-trait-list" ||
          name == "null-research-priority-value";
      if (expected_representability) {
        check(error.stage() == PlayerCampaignJsonStage::Representability,
              name + ": native representability stage");
        check(error.source_type() == "NativeRepresentabilityException",
              name + ": native representability type");
        check(std::string(error.what()).find("cannot be represented") !=
                  std::string::npos,
              name + ": native representability message");
        check(!error.path().empty(), name + ": representability path");
        check(error.byte().has_value(),
              name + ": representability original byte offset");
        ++errors;
        continue;
      }
      check(error.source_type() == *expected_type,
            name + ": source error type mismatch: " + error.source_type());
      const bool native_lexical_or_conversion =
          error.stage() == PlayerCampaignJsonStage::Parse ||
          error.source_type() == "JsonException" ||
          error.source_type() == "InvalidOperationException";
      if (!native_lexical_or_conversion)
        check(std::string(error.what()) ==
                  row.at("ErrorMessage").get<std::string>(),
              name +
                  ": authored source error message mismatch: " + error.what());
      check(error.inner_type() == optional_text(row, "InnerType"),
            name + ": inner type mismatch");
      if (error.inner_type() != std::optional<std::string>("JsonException"))
        check(error.inner_message() == optional_text(row, "InnerMessage"),
              name + ": authored inner message mismatch");
      if (error.stage() == PlayerCampaignJsonStage::Parse)
        check(error.byte().has_value(), name + ": missing lexical byte offset");
      if (error.inner_type() == std::optional<std::string>("JsonException") &&
          (error.stage() == PlayerCampaignJsonStage::DiplomacyDecode ||
           error.stage() == PlayerCampaignJsonStage::ResearchDecode)) {
        check(!error.path().empty(), name + ": missing conversion path");
        check(error.byte().has_value(), name + ": missing conversion byte");
      }
      ++errors;
    }
  }
  check(successes == 6 && errors == 59 && successes + errors == 65,
        "exact native row accounting");
  std::cout << "Player17 JSON replay passed: rows=65 success=6 errors=59\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Player17 JSON harness failure: " << typeid(error).name() << ": "
            << error.what() << '\n';
  std::cerr << "cwd: " << fs::current_path().string() << '\n';
  std::cerr << "fixture: "
            << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
            << '\n';
  std::cerr << "research root: "
            << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
            << '\n';
  std::cerr << "native output: "
            << (argc > 3 ? fs::absolute(argv[3]).string() : "<missing>")
            << '\n';
  return 1;
}
