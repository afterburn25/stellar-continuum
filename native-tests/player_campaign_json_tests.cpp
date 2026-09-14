#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/player_campaign_json.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;

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

void check_success(RestoredPlayerCampaignV17 restored, const Json &expected,
                   const std::string &label, const fs::path &native_output) {
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
  check(encode_player_campaign_v17_json(recaptured) == encoded,
        label + ": repeated encoding changed bytes or input state");
  auto actual_json = Json::parse(encoded);
  auto expected_json = expected;
  expected_json.erase("Control");
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
  if (argc != 4)
    throw std::runtime_error("Usage: player_campaign_json_tests <fixture> "
                             "<research root> <native output>");
  const fs::path fixture = fs::absolute(argv[1]);
  const fs::path research_root = fs::absolute(argv[2]);
  const fs::path native_output = fs::absolute(argv[3]);
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
      check_success(std::move(restored), row.at("Result"), name, native_output);
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
