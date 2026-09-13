#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stellar/core/adaptive_research_campaign_simulation.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <typeinfo>
#include <vector>

using namespace stellar::core;
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {

std::string canonical_fingerprint(const fs::path &root) {
  std::vector<fs::path> paths;
  for (const auto &entry : fs::directory_iterator(fs::absolute(root))) {
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end(), [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::vector<std::uint8_t> bytes;
  for (const auto &path : paths) {
    const auto name = path.filename().string();
    bytes.insert(bytes.end(), name.begin(), name.end());
    std::ifstream input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error("Could not read canonical research file: " + path.string());
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    bytes.insert(bytes.end(), contents.begin(), contents.end());
  }
  const auto digest = detail::adaptive_research_sha256(bytes);
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const auto value : digest) {
    result.push_back(hex[value >> 4]);
    result.push_back(hex[value & 15]);
  }
  return result;
}

Json encode_campaign_snapshot(const AdaptiveResearchCampaignSnapshot &snapshot) {
  Json civilizations = Json::array();
  for (const auto &entry : snapshot.civilizations) {
    Json funding = Json::array();
    for (const auto &value : entry.project_funding) {
      funding.push_back({{"nodeId", value.node_id},
                         {"reservedMilestoneCredits", value.reserved_milestone_credits},
                         {"consumedMilestoneCredits", value.consumed_milestone_credits},
                         {"authorizationCredits", value.authorization_credits}});
    }
    civilizations.push_back(
        {{"civilizationId", entry.civilization_id},
         {"speciesId", entry.species_id},
         {"referenceProfileId", entry.reference_profile_id},
         {"applicabilityContextId", entry.applicability_context_id},
         {"research", Json::parse(detail::encode_adaptive_research_snapshot_v5_dto(entry.research))},
         {"projectFunding", std::move(funding)}});
  }
  return {{"schemaVersion", snapshot.schema_version},
          {"catalogId", snapshot.catalog_id},
          {"civilizations", std::move(civilizations)}};
}

AdaptiveResearchCampaignSnapshot decode_campaign_snapshot(const Json &value) {
  AdaptiveResearchCampaignSnapshot snapshot{value.at("schemaVersion").get<int>(),
                                              value.at("catalogId").get<std::string>(), {}};
  for (const auto &entry : value.at("civilizations")) {
    AdaptiveResearchCampaignCivilizationSnapshot civilization{
        entry.at("civilizationId").get<int>(),
        entry.at("speciesId").get<std::string>(),
        entry.at("referenceProfileId").get<std::string>(),
        entry.at("applicabilityContextId").get<std::string>(),
        detail::decode_adaptive_research_snapshot_v5_dto(entry.at("research").dump()),
        {}};
    for (const auto &funding : entry.at("projectFunding")) {
      civilization.project_funding.push_back(
          {funding.at("nodeId").get<std::string>(),
           funding.at("reservedMilestoneCredits").get<double>(),
           funding.at("consumedMilestoneCredits").get<double>(),
           funding.at("authorizationCredits").get<double>()});
    }
    snapshot.civilizations.push_back(std::move(civilization));
  }
  return snapshot;
}

std::vector<Civilization>
campaign_civilizations(const AdaptiveResearchCampaignSnapshot &snapshot) {
  std::vector<Civilization> result;
  for (const auto &entry : snapshot.civilizations) {
    Civilization civilization{};
    civilization.id = entry.civilization_id;
    civilization.species_id = entry.species_id;
    result.push_back(std::move(civilization));
  }
  return result;
}

double decode_number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::invalid_argument("Unknown retained numeric token: " + text);
}

Json encode_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

FreshCampaignState decode_world(const Json &value) {
  FreshCampaignState world{};
  world.seed = value.at("Seed").get<std::int64_t>();
  for (const auto &item : value.at("Civilizations")) {
    Civilization civilization{};
    civilization.id = item.at("Id").get<int>();
    civilization.species_id = item.at("SpeciesId").get<std::string>();
    civilization.is_player = item.at("IsPlayer").get<bool>();
    civilization.is_seeded_ancient = item.at("IsSeededAncient").get<bool>();
    civilization.development_stage =
        static_cast<CivilizationDevelopmentStage>(item.at("DevelopmentStage").get<int>());
    world.civilizations.push_back(std::move(civilization));
  }
  for (const auto &item : value.at("Economies")) {
    CivilizationEconomy economy{};
    economy.civilization_id = item.at("CivilizationId").get<int>();
    economy.credits = decode_number(item.at("Credits"));
    economy.last_research_spending_per_day =
        decode_number(item.at("LastResearchSpendingPerDay"));
    economy.last_research_funding_fraction =
        decode_number(item.at("LastResearchFundingFraction"));
    economy.last_credits_per_second = decode_number(item.at("LastCreditsPerSecond"));
    world.economies.push_back(std::move(economy));
  }
  for (const auto &item : value.at("Construction")) {
    ConstructionState construction{};
    construction.civilization_id = item.at("CivilizationId").get<int>();
    construction.completed_project_ids =
        item.at("CompletedProjectIds").get<std::vector<std::string>>();
    world.construction.push_back(std::move(construction));
  }
  for (const auto &item : value.at("Colonies")) {
    Colony colony{};
    colony.id = item.at("Id").get<int>();
    colony.civilization_id = item.at("CivilizationId").get<int>();
    colony.system_id = item.at("SystemId").get<int>();
    colony.name = item.at("Name").get<std::string>();
    colony.population_millions = decode_number(item.at("PopulationMillions"));
    for (const auto &building_value : item.at("Buildings")) {
      SurfaceBuilding building{};
      building.id = building_value.at("Id").get<int>();
      building.type_id = building_value.at("TypeId").get<std::string>();
      building.industry_progress = decode_number(building_value.at("IndustryProgress"));
      building.is_complete = building_value.at("IsComplete").get<bool>();
      building.is_enabled = building_value.at("IsEnabled").get<bool>();
      building.condition = decode_number(building_value.at("Condition"));
      building.stored_power_days = decode_number(building_value.at("StoredPowerDays"));
      colony.surface_buildings.push_back(std::move(building));
    }
    world.colonies.push_back(std::move(colony));
  }
  return world;
}

Json project_world(const FreshCampaignState &world,
                   const AdaptiveResearchCampaignState &campaign,
                   const AdaptiveResearchCampaignSnapshotCodec &codec) {
  Json civilizations = Json::array();
  for (const auto &value : world.civilizations) {
    civilizations.push_back({{"Id", value.id},
                             {"SpeciesId", value.species_id},
                             {"IsPlayer", value.is_player},
                             {"IsSeededAncient", value.is_seeded_ancient},
                             {"DevelopmentStage", static_cast<int>(value.development_stage)}});
  }
  Json economies = Json::array();
  for (const auto &value : world.economies) {
    economies.push_back({{"CivilizationId", value.civilization_id},
                         {"Credits", encode_number(value.credits)},
                         {"LastResearchSpendingPerDay",
                          encode_number(value.last_research_spending_per_day)},
                         {"LastResearchFundingFraction",
                          encode_number(value.last_research_funding_fraction)},
                         {"LastCreditsPerSecond", encode_number(value.last_credits_per_second)}});
  }
  Json construction = Json::array();
  for (const auto &value : world.construction) {
    construction.push_back({{"CivilizationId", value.civilization_id},
                            {"CompletedProjectIds", value.completed_project_ids}});
  }
  Json colonies = Json::array();
  for (const auto &value : world.colonies) {
    Json buildings = Json::array();
    for (const auto &building : value.surface_buildings) {
      buildings.push_back({{"Id", building.id},
                           {"TypeId", building.type_id},
                           {"IndustryProgress", encode_number(building.industry_progress)},
                           {"IsComplete", building.is_complete},
                           {"IsEnabled", building.is_enabled},
                           {"Condition", encode_number(building.condition)},
                           {"StoredPowerDays", encode_number(building.stored_power_days)}});
    }
    colonies.push_back({{"Id", value.id},
                        {"CivilizationId", value.civilization_id},
                        {"SystemId", value.system_id},
                        {"Name", value.name},
                        {"PopulationMillions", encode_number(value.population_millions)},
                        {"Buildings", std::move(buildings)}});
  }
  Json revisions = Json::array();
  for (const int id : campaign.civilization_ids()) {
    const auto &state = campaign.get_civilization(id);
    revisions.push_back({{"CivilizationId", state.civilization_id()},
                         {"Revision", state.revision()},
                         {"MaterializedViewRevision", state.materialized_view_revision()},
                         {"Expertise", state.expertise().revision()}});
  }
  return {{"Seed", world.seed},
          {"Civilizations", std::move(civilizations)},
          {"Economies", std::move(economies)},
          {"Construction", std::move(construction)},
          {"Colonies", std::move(colonies)},
          {"Campaign", encode_campaign_snapshot(codec.capture(campaign))},
          {"Revisions", std::move(revisions)}};
}

Json encode_events(const std::vector<AdaptiveResearchCampaignEvent> &events) {
  Json result = Json::array();
  for (const auto &event : events) {
    result.push_back({{"CivilizationId", event.civilization_id},
                      {"NodeId", event.node_id},
                      {"Message", event.message},
                      {"IsOutcome", event.is_outcome}});
  }
  return result;
}

Json encode_error(std::exception_ptr error) {
  if (!error)
    return nullptr;
  try {
    std::rethrow_exception(error);
  } catch (const AdaptiveResearchCampaignMissingState &exception) {
    return {{"Type", "KeyNotFoundException"}, {"Message", exception.what()}};
  } catch (const AdaptiveResearchCampaignOperationError &exception) {
    return {{"Type", "InvalidOperationException"}, {"Message", exception.what()}};
  } catch (const std::out_of_range &exception) {
    return {{"Type", "ArgumentOutOfRangeException"}, {"Message", exception.what()}};
  } catch (const std::exception &exception) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", exception.what()}};
  }
}

bool equal_json(const Json &actual, const Json &expected, std::string &path) {
  if (actual.type() != expected.type()) {
    if (actual.is_number() && expected.is_number()) {
      const bool actual_signed = actual.type() == Json::value_t::number_integer;
      const bool actual_unsigned = actual.type() == Json::value_t::number_unsigned;
      const bool expected_signed = expected.type() == Json::value_t::number_integer;
      const bool expected_unsigned = expected.type() == Json::value_t::number_unsigned;
      const bool actual_integer = actual_signed || actual_unsigned;
      const bool expected_integer = expected_signed || expected_unsigned;
      if (actual_integer && expected_integer) {
        if (actual_unsigned && expected_unsigned)
          return actual.get<std::uint64_t>() == expected.get<std::uint64_t>();
        if (actual_signed && expected_signed)
          return actual.get<std::int64_t>() == expected.get<std::int64_t>();
        const Json &signed_value = actual_signed ? actual : expected;
        const Json &unsigned_value = actual_unsigned ? actual : expected;
        const auto signed_number = signed_value.get<std::int64_t>();
        return signed_number >= 0 &&
               static_cast<std::uint64_t>(signed_number) == unsigned_value.get<std::uint64_t>();
      }
      const double left = actual.get<double>();
      const double right = expected.get<double>();
      return left == right || std::abs(left - right) <= 1e-10;
    }
    return false;
  }
  if (actual.is_primitive()) {
    if (actual.is_number_float()) {
      const double left = actual.get<double>();
      const double right = expected.get<double>();
      return left == right || std::abs(left - right) <= 1e-10;
    }
    return actual == expected;
  }
  if (actual.size() != expected.size())
    return false;
  if (actual.is_array()) {
    for (std::size_t index = 0; index < actual.size(); ++index) {
      const auto prior = path;
      path += "/" + std::to_string(index);
      if (!equal_json(actual[index], expected[index], path))
        return false;
      path = prior;
    }
    return true;
  }
  for (auto item = actual.begin(); item != actual.end(); ++item) {
    if (!expected.contains(item.key())) {
      path += "/" + item.key();
      return false;
    }
    const auto prior = path;
    path += "/" + item.key();
    if (!equal_json(item.value(), expected.at(item.key()), path))
      return false;
    path = prior;
  }
  return true;
}

int replay(const fs::path &root, const fs::path &fixture_path) {
  {
    std::string path;
    const Json higher = std::uint64_t{9007199254740993ULL};
    const Json lower = std::uint64_t{9007199254740992ULL};
    if (equal_json(higher, lower, path))
      throw std::runtime_error("Integer JSON comparison lost precision above 2^53.");
  }
  const auto before_fingerprint = canonical_fingerprint(root);
  std::ifstream input(fixture_path);
  if (!input)
    throw std::runtime_error("Could not read fixture file: " + fixture_path.string());
  const Json fixture = Json::parse(input);
  if (fixture.at("CanonicalFingerprint").get<std::string>() != before_fingerprint)
    throw std::runtime_error("Canonical research fingerprint does not match fixture.");

  auto runtime = load_adaptive_research_strategic_runtime(root);
  AdaptiveResearchCampaignFactory factory(runtime);
  AdaptiveResearchCampaignSnapshotCodec codec(runtime);
  AdaptiveResearchCampaignSimulation simulation;
  std::size_t count{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    if (row.at("Kind").get<std::string>() != "advance")
      throw std::invalid_argument("Unknown fixture operation: " + row.at("Kind").dump());
    if (row.at("BeforeFingerprint").get<std::string>() != before_fingerprint ||
        row.at("AfterFingerprint").get<std::string>() != before_fingerprint)
      throw std::runtime_error(name + ": retained source fingerprint mismatch");
    const double days = decode_number(row.at("Input").at("Days"));
    const double now = decode_number(row.at("Input").at("Now"));
    FreshCampaignState world = decode_world(row.at("BeforeState"));
    const auto input_snapshot = decode_campaign_snapshot(row.at("BeforeState").at("Campaign"));
    const auto snapshot_civilizations = campaign_civilizations(input_snapshot);
    const auto input_snapshot_before = encode_campaign_snapshot(input_snapshot).dump();
    const auto setup_kind = row.at("SetupKind").get<std::string>();
    if (setup_kind != "Fresh" && setup_kind != "Recovered")
      throw std::invalid_argument("Unknown fixture setup kind: " + setup_kind);
    auto campaign = setup_kind == "Fresh" ? factory.create(snapshot_civilizations)
                                            : codec.restore(snapshot_civilizations, input_snapshot);
    std::string mismatch_path;
    const auto native_before_state = project_world(world, campaign, codec);
    if (!equal_json(native_before_state, row.at("BeforeState"), mismatch_path)) {
      std::cerr << name << ": typed input projection mismatch at " << mismatch_path << "\n";
      std::ofstream("actual-input-mismatch.json") << native_before_state.dump(2);
      std::ofstream("expected-input-mismatch.json") << row.at("BeforeState").dump(2);
      return 1;
    }

    std::vector<AdaptiveResearchCampaignEvent> events;
    std::exception_ptr error;
    try {
      events = simulation.advance(world, campaign, days, now);
    } catch (...) {
      error = std::current_exception();
    }
    const auto actual_error = encode_error(error);
    const Json actual_events = error ? Json(nullptr) : encode_events(events);
    const Json actual_state = project_world(world, campaign, codec);
    if (actual_error != row.at("Error")) {
      std::cerr << name << ": error mismatch\n"
                << actual_error.dump(2) << "\n"
                << row.at("Error").dump(2) << "\n";
      return 1;
    }
    mismatch_path.clear();
    if (!equal_json(actual_events, row.at("Events"), mismatch_path)) {
      std::cerr << name << ": event mismatch\n"
                << actual_events.dump(2) << "\n"
                << row.at("Events").dump(2) << "\n";
      return 1;
    }
    mismatch_path.clear();
    if (!equal_json(actual_state, row.at("State"), mismatch_path)) {
      std::cerr << name << ": state mismatch at " << mismatch_path << "\n";
      std::ofstream("actual-state-mismatch.json") << actual_state.dump(2);
      std::ofstream("expected-state-mismatch.json") << row.at("State").dump(2);
      return 1;
    }
    if (encode_campaign_snapshot(input_snapshot).dump() != input_snapshot_before) {
      std::cerr << name << ": caller snapshot mutated\n";
      return 1;
    }
    ++count;
  }
  if (count != fixture.at("Rows").size())
    throw std::runtime_error("Native replay did not invoke every retained source row.");
  if (canonical_fingerprint(root) != before_fingerprint)
    throw std::runtime_error("Native replay changed canonical research inputs.");
  std::cout << "adaptive_research_campaign_simulation_tests: " << count
            << " actual-source rows passed\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected canonical research directory and fixture path.");
    return replay(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception &exception) {
    std::cerr << "ExceptionType: " << typeid(exception).name() << "\nMessage: "
              << exception.what() << "\nCurrentDirectory: " << fs::current_path().string()
              << "\nResearchRoot: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nFixturePath: " << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ExceptionType: unknown\nMessage: non-standard exception\nCurrentDirectory: "
              << fs::current_path().string() << "\nResearchRoot: "
              << (argc > 1 ? argv[1] : "<missing>") << "\nFixturePath: "
              << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  }
}
