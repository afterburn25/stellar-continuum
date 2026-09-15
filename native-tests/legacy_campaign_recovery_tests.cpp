#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/galaxy_economy_persistence.hpp>
#include <stellar/core/legacy_campaign_recovery.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace economy_helper {
#define main embedded_galaxy_economy_test_main
#include "galaxy_economy_persistence_tests.cpp"
#undef main
}

namespace fleet_helper {
#define main embedded_fleet_state_test_main
#include "fleet_state_tests.cpp"
#undef main
}

namespace {
using Json = nlohmann::ordered_json;
using namespace stellar::core;

void require(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}

std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string hash(const std::string &value) {
  const auto digest = detail::adaptive_research_sha256(std::span(
      reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  for (const auto byte : digest) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

double number(const Json &value) { return economy_helper::number(value); }

StellarSystem system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.position = {
      static_cast<float>(number(value.at("Position").at("X"))),
      static_cast<float>(number(value.at("Position").at("Y")))};
  result.archetype = static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  return result;
}

Civilization civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.home_system_id = value.at("HomeSystemId");
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  result.is_player = value.at("IsPlayer");
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient");
  result.expansion_allowed = value.at("ExpansionAllowed");
  result.neutral_unless_provoked = value.at("NeutralUnlessProvoked");
  result.species_id = value.at("SpeciesId");
  return result;
}

template <typename T, typename Parser>
std::vector<T> parse_list(const Json &value, Parser parser) {
  std::vector<T> result;
  result.reserve(value.size());
  for (const auto &entry : value) result.push_back(parser(entry));
  return result;
}

void check_systems(std::span<const StellarSystem> actual, const Json &expected,
                   std::string_view label) {
  require(actual.size() == expected.size(), std::string(label) + " size");
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto &wanted = expected[index];
    require(actual[index].id == wanted.at("Id").get<int>() &&
                actual[index].name == wanted.at("Name").get<std::string>() &&
                actual[index].position.x == static_cast<float>(
                    number(wanted.at("Position").at("X"))) &&
                actual[index].position.y == static_cast<float>(
                    number(wanted.at("Position").at("Y"))),
            std::string(label) + " entry");
  }
}

void check_civilizations(std::span<const Civilization> actual,
                         const Json &expected, std::string_view label) {
  require(actual.size() == expected.size(), std::string(label) + " size");
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto &wanted = expected[index];
    require(actual[index].id == wanted.at("Id").get<int>() &&
                actual[index].name == wanted.at("Name").get<std::string>() &&
                actual[index].home_system_id ==
                    wanted.at("HomeSystemId").get<int>() &&
                static_cast<int>(actual[index].development_stage) ==
                    wanted.at("DevelopmentStage").get<int>() &&
                actual[index].is_player == wanted.at("IsPlayer").get<bool>() &&
                actual[index].is_seeded_ancient ==
                    wanted.at("IsSeededAncient").get<bool>() &&
                actual[index].expansion_allowed ==
                    wanted.at("ExpansionAllowed").get<bool>() &&
                actual[index].species_id ==
                    wanted.at("SpeciesId").get<std::string>(),
            std::string(label) + " entry");
  }
}

void check_colonies(std::span<const Colony> actual, const Json &expected,
                    std::string_view label) {
  require(actual.size() == expected.size(), std::string(label) + " size");
  for (std::size_t index = 0; index < actual.size(); ++index)
    economy_helper::check_colony(actual[index], expected[index],
                                 std::string(label) + " entry");
}

void check_fleets(std::span<const FleetState> actual, const Json &expected,
                  std::string_view label) {
  require(actual.size() == expected.size(), std::string(label) + " size");
  for (std::size_t index = 0; index < actual.size(); ++index)
    fleet_helper::check_fleet(actual[index], expected[index],
                              std::string(label) + " entry");
}

void check_knowledge(const CivilizationKnowledgeState &actual,
                     const Json &expected, std::string_view label) {
  for (const auto &entry : expected) {
    const int civilization_id = entry.at("CivilizationId");
    require(actual.known_systems(civilization_id) ==
                entry.at("KnownSystems").get<std::vector<int>>(),
            std::string(label) + " known systems");
    const auto surveys = actual.system_survey_knowledge(civilization_id);
    const auto &wanted = entry.at("Surveys");
    require(surveys.size() == wanted.size(),
            std::string(label) + " survey size");
    for (std::size_t index = 0; index < surveys.size(); ++index) {
      require(surveys[index].system_id ==
                      wanted[index].at("SystemId").get<int>() &&
                  static_cast<int>(surveys[index].level) ==
                      wanted[index].at("Level").get<int>(),
              std::string(label) + " survey identity");
      economy_helper::equal_number(
          surveys[index].progress, number(wanted[index].at("Progress")),
          std::string(label) + " survey progress");
    }
  }
}

struct NativeError {
  std::string type;
  std::string message;
};

void check_error(const std::optional<NativeError> &actual,
                 const Json &expected, const std::string &label) {
  require(actual.has_value() == !expected.is_null(), label + " error presence");
  if (!actual) return;
  require(actual->type == expected.at("Type").get<std::string>(),
          label + " error type");
  require(actual->message == expected.at("Message").get<std::string>(),
          label + " error message");
}

void verify_hashes(const std::filesystem::path &root, const Json &before,
                   const Json &after) {
  require(before == after, "source hashes before/after");
  for (const auto &[relative, expected] : before.items())
    require(hash(bytes(root / relative)) == expected.get<std::string>(),
            "source hash " + relative);
}

void replay(const std::filesystem::path &root,
            const std::filesystem::path &fixture_path) {
  const auto fixture_bytes = bytes(fixture_path);
  require(hash(fixture_bytes) ==
              "24D852F6E0E6D8224C4DAADBE658AE5B1D9577C72304EEE757240E7B1C0A0221",
          "fixture SHA-256");
  const auto fixture = Json::parse(fixture_bytes);
  require(fixture.at("Schema") ==
              "stellar.legacy-campaign-recovery.actual-source.v1",
          "fixture schema");
  require(fixture.at("RowCount") == 28, "fixture row count");
  require(fixture.at("Rows").size() == 28, "fixture rows size");
  verify_hashes(root, fixture.at("SourceHashesBefore"),
                fixture.at("SourceHashesAfter"));

  std::size_t replayed = 0;
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto operation = row.at("Operation").get<std::string>();
    require(operation == "InitialKnowledge" ||
                operation == "MigratedTechnology" ||
                operation == "MigratedConstruction" ||
                operation == "ExpansionFleets",
            name + " known operation");

    std::optional<NativeError> error;
    if (operation == "InitialKnowledge") {
      auto systems = parse_list<StellarSystem>(row.at("SystemsBefore"), system);
      auto civilizations = parse_list<Civilization>(
          row.at("CivilizationsBefore"), civilization);
      check_systems(systems, row.at("SystemsBefore"), name + " decoded systems");
      check_civilizations(civilizations, row.at("CivilizationsBefore"),
                          name + " decoded civilizations");
      std::optional<CivilizationKnowledgeState> result;
      try {
        result = create_legacy_initial_knowledge(systems, civilizations);
      } catch (const LegacyCampaignRecoveryOperationError &caught) {
        error = {{"InvalidOperationException"}, caught.what()};
      }
      check_error(error, row.at("Error"), name);
      if (result) check_knowledge(*result, row.at("Result"), name);
      check_systems(systems, row.at("SystemsAfter"), name + " systems after");
      check_civilizations(civilizations, row.at("CivilizationsAfter"),
                          name + " civilizations after");
    } else if (operation == "MigratedTechnology" ||
               operation == "MigratedConstruction") {
      auto civilizations = parse_list<Civilization>(
          row.at("CivilizationsBefore"), civilization);
      check_civilizations(civilizations, row.at("CivilizationsBefore"),
                          name + " decoded civilizations");
      if (operation == "MigratedTechnology") {
        const auto result =
            create_migrated_legacy_technology_states(civilizations);
        require(result.size() == row.at("Result").size(), name + " result size");
        for (std::size_t index = 0; index < result.size(); ++index)
          economy_helper::check_technology(result[index], row.at("Result")[index],
                                           name + " technology");
      } else {
        const auto result =
            create_migrated_legacy_construction_states(civilizations);
        require(result.size() == row.at("Result").size(), name + " result size");
        for (std::size_t index = 0; index < result.size(); ++index)
          economy_helper::check_construction(result[index], row.at("Result")[index],
                                             name + " construction");
      }
      check_civilizations(civilizations, row.at("CivilizationsAfter"),
                          name + " civilizations after");
      check_error(error, row.at("Error"), name);
    } else {
      auto systems = parse_list<StellarSystem>(row.at("SystemsBefore"), system);
      auto civilizations = parse_list<Civilization>(
          row.at("CivilizationsBefore"), civilization);
      auto colonies = parse_list<Colony>(row.at("ColoniesBefore"),
                                         economy_helper::colony);
      auto fleets = parse_list<FleetState>(row.at("FleetsBefore"),
                                           fleet_helper::parse_fleet);
      check_systems(systems, row.at("SystemsBefore"), name + " decoded systems");
      check_civilizations(civilizations, row.at("CivilizationsBefore"),
                          name + " decoded civilizations");
      check_colonies(colonies, row.at("ColoniesBefore"), name + " decoded colonies");
      check_fleets(fleets, row.at("FleetsBefore"), name + " decoded fleets");
      try {
        ensure_legacy_expansion_fleets(fleets, systems, civilizations, colonies);
      } catch (const LegacyCampaignRecoveryDataError &caught) {
        error = {{"InvalidDataException"}, caught.what()};
      } catch (const LegacyCampaignRecoveryOperationError &caught) {
        error = {{"InvalidOperationException"}, caught.what()};
      }
      check_error(error, row.at("Error"), name);
      check_systems(systems, row.at("SystemsAfter"), name + " systems after");
      check_civilizations(civilizations, row.at("CivilizationsAfter"),
                          name + " civilizations after");
      check_colonies(colonies, row.at("ColoniesAfter"), name + " colonies after");
      check_fleets(fleets, row.at("FleetsAfter"), name + " fleets after");
    }
    ++replayed;
  }
  require(replayed == 28, "exact row accounting");
  std::cout << "legacy campaign recovery parity: " << replayed
            << " actual-source rows passed\n";
}

} // namespace

int main(int argc, char **argv) {
  const auto cwd = std::filesystem::current_path();
  const std::string root_argument = argc > 1 ? argv[1] : "<missing>";
  const std::string fixture_argument = argc > 2 ? argv[2] : "<missing>";
  try {
    require(argc == 3, "Expected source root and fixture path.");
    replay(std::filesystem::absolute(root_argument),
           std::filesystem::absolute(fixture_argument));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "legacy campaign recovery parity failure: "
              << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd: " << cwd.string() << '\n'
              << "source root: "
              << (root_argument == "<missing>"
                      ? root_argument
                      : std::filesystem::absolute(root_argument).string())
              << '\n'
              << "fixture: "
              << (fixture_argument == "<missing>"
                      ? fixture_argument
                      : std::filesystem::absolute(fixture_argument).string())
              << '\n';
    return 1;
  }
}
