#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planetary_satellites.hpp>

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("test could not read fixture file");
  return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path &path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("test could not write fixture");
}
const Json &row(const Json &fixture, std::string_view name) {
  const auto &rows = fixture.at("rows");
  const auto found = std::ranges::find_if(rows, [&](const Json &value) {
    return value.at("name").get<std::string>() == name;
  });
  require(found != rows.end(), "missing oracle row " + std::string(name));
  return *found;
}
bool same(const Json &left, const Json &right) {
  if (left.is_number() && right.is_number())
    return std::abs(left.get<double>() - right.get<double>()) <= 1e-10;
  if (left.type() != right.type() || left.size() != right.size()) return false;
  if (left.is_array()) {
    for (std::size_t i = 0; i < left.size(); ++i)
      if (!same(left[i], right[i])) return false;
    return true;
  }
  if (left.is_object()) {
    for (const auto &[key, value] : left.items())
      if (!right.contains(key) || !same(value, right.at(key))) return false;
    return true;
  }
  return left == right;
}
Json parse_player_json(std::string bytes) {
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf) bytes.erase(0, 3);
  return Json::parse(bytes);
}
Json progress_json(const std::vector<PlayerCampaignRestorationProgress> &values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back({{"Fraction", value.fraction}, {"Status", value.status}});
  return result;
}
std::string regular_bytes(const fs::path &path) {
  std::error_code error;
  return fs::is_regular_file(path, error) ? read(path) : std::string("<missing>");
}

enum class Setup { Pair, CorruptPrimary, MissingPrimary, BothMalformed,
                   BothMissing, Developer, BlankPrimary, BadBackup, Utf8Bom,
                   InvalidUtf8, DirectoryPrimary, Utf16Pair };
void arrange(Setup setup, const fs::path &path, const fs::path &evidence) {
  fs::create_directories(path.parent_path());
  if (setup != Setup::BothMissing) {
    fs::copy_file(evidence / "primary.json", path,
                  fs::copy_options::overwrite_existing);
    fs::copy_file(evidence / "backup.json", fs::path(path).concat(".bak"),
                  fs::copy_options::overwrite_existing);
  }
  if (setup == Setup::CorruptPrimary) write(path, "{ malformed primary");
  if (setup == Setup::MissingPrimary) fs::remove(path);
  if (setup == Setup::BothMalformed) {
    write(path, "{ malformed primary"); write(fs::path(path).concat(".bak"), "{ malformed backup");
  }
  if (setup == Setup::Developer) {
    write(path, "{\"DeveloperFormatVersion\":1}");
    write(fs::path(path).concat(".bak"), "{\"DeveloperFormatVersion\":1}");
  }
  if (setup == Setup::BlankPrimary) write(path, "");
  if (setup == Setup::BadBackup) write(fs::path(path).concat(".bak"), "not-json");
  if (setup == Setup::Utf8Bom) write(path, std::string("\xef\xbb\xbf") + read(path));
  if (setup == Setup::InvalidUtf8) write(path, std::string("\xff\xfe\xfd", 3));
  if (setup == Setup::DirectoryPrimary) { fs::remove(path); fs::create_directory(path); }
  if (setup == Setup::Utf16Pair) {
    const auto convert = [](const std::string &input) {
      std::string output("\xff\xfe", 2);
      for (const unsigned char value : input) { output.push_back(static_cast<char>(value)); output.push_back('\0'); }
      return output;
    };
    write(path, convert(read(path)));
    write(fs::path(path).concat(".bak"), convert(read(fs::path(path).concat(".bak"))));
  }
}

struct CallbackMarker final : std::runtime_error { using std::runtime_error::runtime_error; };

void compare_progress(const Json &source_row,
                      const std::vector<PlayerCampaignRestorationProgress> &actual) {
  require(same(progress_json(actual), source_row.at("result").at("progress")),
          source_row.at("name").get<std::string>() + ": progress differs");
}

void success_case(const Json &fixture, std::string_view name, Setup setup,
                  const fs::path &case_root, const fs::path &evidence,
                  const fs::path &research_root, bool callback_once = false,
                  bool compare_source_progress = true) {
  fs::create_directories(case_root);
  const auto path = case_root / "autosave.json";
  arrange(setup, path, evidence);
  const auto primary_before = regular_bytes(path);
  const auto backup_path = fs::path(path).concat(".bak");
  const auto backup_before = regular_bytes(backup_path);
  std::vector<PlayerCampaignRestorationProgress> progress;
  bool first = callback_once;
  auto loaded = load_existing_player_campaign_v17(
      path, [&] { return load_adaptive_research_strategic_runtime(research_root); },
      [&](const PlayerCampaignRestorationProgress &value) {
        if (first) { first = false; throw CallbackMarker("callback-marker"); }
        progress.push_back(value);
      });
  const auto &expected = row(fixture, name);
  if (compare_source_progress) compare_progress(expected, progress);
  const auto &metadata = expected.at("result").contains("source") ? expected :
      row(fixture, setup == Setup::CorruptPrimary
                       ? "corrupt-primary-backup-recovery" : "primary-success");
  const auto source = metadata.at("result").at("source").get<std::string>();
  require((loaded.origin == PlayerCampaignLoadOrigin::Primary) ==
              (source == "LoadedSave"), std::string(name) + ": origin differs");
  require(loaded.prior_attempts.size() ==
              (loaded.origin == PlayerCampaignLoadOrigin::Primary ? 0u : 1u),
          std::string(name) + ": prior attempts differ");
  if (!loaded.prior_attempts.empty())
    require(loaded.prior_attempts[0].origin == PlayerCampaignLoadOrigin::Primary,
            std::string(name) + ": prior attempt order differs");
  if (callback_once) {
    require(loaded.prior_attempts[0].exception != nullptr,
            "callback attempt lost exception identity");
    try { std::rethrow_exception(loaded.prior_attempts[0].exception); }
    catch (const CallbackMarker &value) {
      require(std::string(value.what()) == "callback-marker",
              "callback attempt changed exception identity");
    }
  }
  require(regular_bytes(path) == primary_before &&
              regular_bytes(backup_path) == backup_before,
          std::string(name) + ": recovery changed files");
  const double day = loaded.campaign.simulation_days();
  const std::string version(loaded.campaign.game_version());
  const std::string saved(loaded.campaign.saved_at_utc());
  require(day == metadata.at("result").at("SimulationDays").get<double>() &&
              version == metadata.at("result").at("GameVersion").get<std::string>(),
          std::string(name) + ": restored metadata differs");
  auto active = std::move(loaded.campaign).activate();
  const auto recaptured = capture_player_campaign_v17(active, {day, version, saved});
  auto actual_state = Json::parse(encode_player_campaign_v17_json(recaptured));
  auto expected_state = parse_player_json(read(
      evidence / (loaded.origin == PlayerCampaignLoadOrigin::Primary
                      ? "primary.json" : "backup.json")));
  const auto normalize_coordinates = [&](auto &&self, Json &value) -> void {
    if (value.is_array()) for (auto &item : value) self(self, item);
    else if (value.is_object()) for (auto &[key, item] : value.items()) {
      if ((key == "X" || key == "Y") && item.is_number())
        item = static_cast<double>(static_cast<float>(item.get<double>()));
      else self(self, item);
    }
  };
  normalize_coordinates(normalize_coordinates, expected_state);
  require(actual_state["Galaxy"]["GenerationMetadata"]["CreatedAtUtc"] ==
              "2030-01-02T03:04:05+00:00",
          std::string(name) + ": created timestamp instant differs");
  expected_state["Galaxy"]["GenerationMetadata"]["CreatedAtUtc"] =
      "2030-01-02T03:04:05+00:00";
  require(actual_state["SavedAtUtc"] == "2030-01-02T03:04:05+00:00",
          std::string(name) + ": saved timestamp instant differs");
  expected_state["SavedAtUtc"] = "2030-01-02T03:04:05+00:00";
  // The frozen evidence predates native migrations: the stellar activity clock
  // initializes at the saved epoch, absent appearances are derived, and the
  // canonical Sol roster is appended. Verify each, then compare the rest.
  if (!expected_state.contains("EventHistory")) {
    // The persistent chronicle tail was added after these saves: restore +
    // recapture already proves it round-trips, so drop it before comparing.
    require(actual_state.contains("EventHistory"),
            std::string(name) + ": missing persistent chronicle tail");
    actual_state.erase("EventHistory");
  }
  auto &actual_galaxy = actual_state["Galaxy"];
  auto &expected_galaxy = expected_state["Galaxy"];
  if (!expected_galaxy.contains("StellarActivityDay")) {
    require(actual_galaxy.at("StellarActivityDay").get<double>() == day,
            std::string(name) + ": activity clock did not initialize at the saved epoch");
    actual_galaxy.erase("StellarActivityDay");
  }
  auto &actual_bodies = actual_galaxy["PlanetaryBodies"];
  const auto &expected_bodies = expected_galaxy.at("PlanetaryBodies");
  std::unordered_set<int> canonical_sol_additions;
  for (const auto &moon : sol_moon_definitions())
    canonical_sol_additions.insert(moon.id);
  canonical_sol_additions.insert(pluto_body_id);
  std::unordered_set<int> expected_body_ids;
  for (const auto &body : expected_bodies)
    expected_body_ids.insert(body.at("Id").get<int>());
  Json migrated_bodies = Json::array();
  for (const auto &body : actual_bodies) {
    const bool canonical_addition =
        body.at("SystemId").get<int>() == sol_system_id &&
        canonical_sol_additions.contains(body.at("Id").get<int>()) &&
        !expected_body_ids.contains(body.at("Id").get<int>());
    if (!canonical_addition) migrated_bodies.push_back(body);
  }
  actual_bodies = std::move(migrated_bodies);
  require(actual_bodies.size() == expected_bodies.size(),
          std::string(name) + ": migrated body count changed");
  for (std::size_t i = 0; i < expected_bodies.size(); ++i)
    if (!expected_bodies[i].contains("PlanetAppearance")) {
      require(actual_bodies[i].contains("PlanetAppearance"),
              std::string(name) + ": missing appearance migration");
      actual_bodies[i].erase("PlanetAppearance");
    }
  if (!same(actual_state, expected_state)) {
    const auto differences = Json::diff(expected_state, actual_state);
    throw std::runtime_error(std::string(name) + ": full owned state differs at " +
        (differences.empty() ? std::string("unknown") : differences.front().dump()));
  }
}

void failure_case(const Json &fixture, std::string_view name, Setup setup,
                  PlayerCampaignLoadAttemptKind expected_kind,
                  const fs::path &case_root, const fs::path &evidence,
                  const fs::path &research_root, bool compare_source_progress = true) {
  fs::create_directories(case_root);
  const auto path = case_root / "autosave.json";
  arrange(setup, path, evidence);
  std::vector<PlayerCampaignRestorationProgress> progress;
  try {
    (void)load_existing_player_campaign_v17(
        path, [&] { return load_adaptive_research_strategic_runtime(research_root); },
        [&](const auto &value) { progress.push_back(value); });
    throw std::runtime_error(std::string(name) + ": unexpectedly loaded");
  } catch (const PlayerCampaignLoadError &error) {
    require(error.attempts().size() == 2, std::string(name) + ": attempt count");
    require(error.attempts()[0].origin == PlayerCampaignLoadOrigin::Primary &&
                error.attempts()[1].origin == PlayerCampaignLoadOrigin::Backup,
            std::string(name) + ": attempt order");
    require(error.attempts()[0].kind == expected_kind &&
                error.attempts()[1].kind == expected_kind,
            std::string(name) + ": attempt kinds");
  }
  if (compare_source_progress) compare_progress(row(fixture, name), progress);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 6) throw std::invalid_argument(
        "Usage: player_campaign_recovery_tests <fixture> <evidence> <research-root> <source-root> <scratch>");
    const auto fixture = Json::parse(read(fs::absolute(argv[1])));
    require(fixture.at("schemaVersion") == 1 && fixture.at("rowCount") == 17,
            "oracle schema/count");
    const auto source_root = fs::absolute(argv[4]) / "src/Game";
    for (const auto &source : fixture.at("sourceFiles")) {
      const auto bytes = read(source_root / source.at("path").get<std::string>());
      const auto digest = detail::adaptive_research_sha256(std::span(
          reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
      static constexpr char digits[] = "0123456789ABCDEF";
      std::string hex; hex.reserve(digest.size() * 2);
      for (const auto value : digest) { hex.push_back(digits[value >> 4]); hex.push_back(digits[value & 15]); }
      require(hex == source.at("sha256").get<std::string>(), "source fingerprint differs");
    }
    const auto evidence = fs::absolute(argv[2]);
    const auto research = fs::absolute(argv[3]);
    const auto scratch = fs::absolute(argv[5]);
    std::error_code error; fs::remove_all(scratch, error); fs::create_directories(scratch);
    success_case(fixture,"primary-success",Setup::Pair,scratch/"primary",evidence,research);
    success_case(fixture,"corrupt-primary-backup-recovery",Setup::CorruptPrimary,scratch/"corrupt",evidence,research);
    success_case(fixture,"missing-primary-backup-recovery",Setup::MissingPrimary,scratch/"missing",evidence,research);
    failure_case(fixture,"both-malformed",Setup::BothMalformed,PlayerCampaignLoadAttemptKind::Failed,scratch/"malformed",evidence,research);
    failure_case(fixture,"both-missing",Setup::BothMissing,PlayerCampaignLoadAttemptKind::Missing,scratch/"none",evidence,research);
    failure_case(fixture,"developer-envelope-rejected",Setup::Developer,PlayerCampaignLoadAttemptKind::Failed,scratch/"developer",evidence,research);
    success_case(fixture,"blank-primary-backup-valid",Setup::BlankPrimary,scratch/"blank",evidence,research);
    success_case(fixture,"primary-valid-backup-malformed",Setup::BadBackup,scratch/"prefer-primary",evidence,research);
    success_case(fixture,"primary-progress-stages",Setup::Pair,scratch/"progress",evidence,research);
    success_case(fixture,"backup-progress-carry",Setup::CorruptPrimary,scratch/"carry",evidence,research);
    success_case(fixture,"utf8-bom-primary",Setup::Utf8Bom,scratch/"bom",evidence,research);
    success_case(fixture,"invalid-utf8-primary-backup-recovery",Setup::InvalidUtf8,scratch/"invalid-utf8",evidence,research,false,false);
    success_case(fixture,"directory-primary-backup-recovery",Setup::DirectoryPrimary,scratch/"directory",evidence,research);
    success_case(fixture,"unicode-\xE8\xB7\xAF\xE5\xBE\x84-\xF0\x9F\x9A\x80",Setup::Pair,scratch/fs::path(u8"unicode-\u8def\u5f84-\U0001f680"),evidence,research);
    success_case(fixture,"callback-primary-failure-backup-recovery",Setup::Pair,scratch/"callback",evidence,research,true);
    try { (void)load_existing_player_campaign_v17(" \t\r\n ", [&]{return load_adaptive_research_strategic_runtime(research);}); throw std::runtime_error("whitespace path accepted"); }
    catch (const std::invalid_argument &) {}
    const auto primary = read(evidence/"primary.json");
    try { (void)restore_player_campaign_v17_json(load_adaptive_research_strategic_runtime(research), primary, {{[](PlayerCampaignJsonStage stage){if(stage==PlayerCampaignJsonStage::GalaxyDecode)throw CallbackMarker("identity");}}}); throw std::runtime_error("codec callback did not throw"); }
    catch (const CallbackMarker &error_value) { require(std::string(error_value.what())=="identity","codec callback identity"); }
    failure_case(fixture,"utf16-bom-primary-source-only",Setup::Utf16Pair,PlayerCampaignLoadAttemptKind::Failed,scratch/"utf16",evidence,research,false);
    std::cout << "player recovery parity: 14 exact source-progress rows, 1 encoding-different source outcome, 1 native callback identity contract, 1 explicit UTF-16 exclusion passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ExceptionType: " << typeid(error).name() << "\nMessage: " << error.what() << '\n';
    return 1;
  }
}
