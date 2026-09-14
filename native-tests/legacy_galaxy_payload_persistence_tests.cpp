#include <stellar/core/legacy_galaxy_payload_persistence.hpp>
#include <stellar/core/galaxy_reference_validation.hpp>

#include "legacy_galaxy_payload_test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace {
using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

void check(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

gate090_current::Error classify(std::exception_ptr failure) {
  try {
    std::rethrow_exception(failure);
  } catch (const CampaignMassiveEncounterDataError &error) {
    return {"InvalidDataException", error.what(), {}, {}};
  } catch (const GalaxyReferenceValidationDataError &error) {
    return {"InvalidDataException", error.what(), {}, {}};
  } catch (const GalaxyReferenceValidationOperationError &error) {
    return {"InvalidOperationException", error.what(), {}, {}};
  } catch (const GalaxyReferenceValidationArgumentError &error) {
    return {"ArgumentException", error.what(), {}, {}};
  } catch (const GalaxyReferenceValidationRangeError &error) {
    return {"ArgumentOutOfRangeException", error.what(), {}, {}};
  } catch (const GalaxyReferenceValidationOverflowError &error) {
    return {"OverflowException", error.what(), {}, {}};
  } catch (...) {
    return gate090_current::classify(std::current_exception());
  }
}

std::string bytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(stream), {}};
}

void verify_source_inventory(const Json &fixture, const fs::path &source_root) {
  constexpr std::string_view expected[] = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Generation/CivilizationSeeder.cs",
      "Simulation/Generation/PlanetaryBodyGenerator.cs",
      "Simulation/Generation/SolCatalogPreset.cs",
      "Simulation/Generation/FleetSeeder.cs",
      "Simulation/Generation/ColonySeeder.cs",
      "Simulation/Generation/ShipyardSeeder.cs",
      "Simulation/Knowledge/CivilizationKnowledgeState.cs",
      "Simulation/Models/GalaxyState.cs",
  };
  check(fixture.at("SourceFiles").size() == std::size(expected),
        "source inventory count");
  for (std::size_t index = 0; index < std::size(expected); ++index) {
    const auto &entry = fixture.at("SourceFiles")[index];
    check(entry.at("Path").get<std::string>() == expected[index],
          "source inventory order");
    check(gate090_current::sha256(
              bytes(source_root / std::string(expected[index]))) ==
              entry.at("Sha256").get<std::string>(),
          "source fingerprint " + std::string(expected[index]));
  }
}

int run(const fs::path &fixture_path, const fs::path &source_root) {
  const auto fixture_bytes = bytes(fixture_path);
  check(gate090_current::sha256(fixture_bytes) ==
            "258509C53878D404896005B82A69484A8D163260C6F35F430EBF2173482DC672",
        "fixture fingerprint");
  const auto fixture = Json::parse(fixture_bytes);
  check(fixture.at("SchemaVersion") == 1, "schema");
  check(fixture.at("RowCount") == 37 && fixture.at("Rows").size() == 37,
        "row count");
  check(fixture.at("SourceOnlyRows") == 0, "source-only count");
  verify_source_inventory(fixture, source_root);

  int passed = 0;
  std::optional<RestoredLegacyGalaxyPayload> retained_success;
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    check(row.at("Operation") == "Restore", name + ": operation");
    LegacyGalaxyPayloadDto input;
    input.galaxy = gate090_current::decode_payload(row.at("Input"));
    input.simulation_seconds =
        row.at("Input").at("SimulationSeconds").get<double>();
    gate090_current::check_payload(input.galaxy, row.at("Before"),
                                   name + ": decoded input");
    const auto retained_seconds = input.simulation_seconds;

    std::optional<RestoredLegacyGalaxyPayload> result;
    std::exception_ptr failure;
    try {
      result = restore_legacy_galaxy_payload(input);
    } catch (...) {
      failure = std::current_exception();
    }

    gate090_current::check_payload(input.galaxy, row.at("After"),
                                   name + ": retained input");
    check(input.simulation_seconds == retained_seconds,
          name + ": retained simulation seconds");
    if (row.at("ErrorType").is_null()) {
      check(!failure && result.has_value(), name + ": expected success");
      const auto &expected = row.at("Result");
      check(result->simulation_days ==
                expected.at("SimulationDays").get<double>() &&
                result->game_version ==
                    expected.at("GameVersion").get<std::string>() &&
                result->saved_at_utc == input.galaxy.saved_at_utc,
            name + ": restored envelope");
      const auto &raw_technology_ids =
          expected.at("RawTechnologyCompletedIds");
      check(result->galaxy.technologies.size() == raw_technology_ids.size(),
            name + ": raw technology state count");
      for (std::size_t index = 0; index < result->galaxy.technologies.size();
           ++index) {
        const auto ids = std::vector<std::string>(
            result->galaxy.technologies[index]
                .completed_technology_ids.values()
                .begin(),
            result->galaxy.technologies[index]
                .completed_technology_ids.values()
                .end());
        check(ids == raw_technology_ids[index].get<std::vector<std::string>>(),
              name + ": raw technology IDs");
      }
      const auto &raw_construction_ids =
          expected.at("RawConstructionCompletedIds");
      check(result->galaxy.construction.size() == raw_construction_ids.size(),
            name + ": raw construction state count");
      for (std::size_t index = 0; index < result->galaxy.construction.size();
           ++index)
        check(result->galaxy.construction[index].completed_project_ids ==
                  raw_construction_ids[index].get<std::vector<std::string>>(),
              name + ": raw construction IDs");

      auto raw_expected = expected;
      if (!raw_expected.at("Galaxy").contains("ActiveCombatEncounter"))
        raw_expected["Galaxy"]["ActiveCombatEncounter"] = nullptr;
      if (!raw_expected.at("Galaxy").contains("CombatIntelligence"))
        raw_expected["Galaxy"]["CombatIntelligence"] = nullptr;
      for (std::size_t index = 0; index < raw_technology_ids.size(); ++index)
        raw_expected["Galaxy"]["Technologies"][index]
                    ["CompletedTechnologyIds"] = raw_technology_ids[index];
      for (std::size_t index = 0; index < raw_construction_ids.size(); ++index)
        raw_expected["Galaxy"]["ConstructionStates"][index]
                    ["CompletedProjectIds"] = raw_construction_ids[index];
      gate090_current::check_raw_world(
          result->galaxy, raw_expected,
          name + ": direct postmigration world");

      GalaxyPayloadCaptureOptions options{
          result->simulation_days, result->game_version,
          result->saved_at_utc, false};
      const auto recaptured =
          capture_galaxy_payload_v16(result->galaxy, options);
      Json expected_envelope = {
          {"FormatVersion", GalaxyPayloadV16Dto::current_format_version},
          {"GameVersion", result->game_version},
          {"SavedAtUtc", result->saved_at_utc},
          {"SimulationDays", result->simulation_days},
          {"Galaxy", expected.at("Galaxy")},
      };
      gate090_current::check_payload(
          recaptured, expected_envelope, name + ": complete DTO projection");
      if (name == "legacy-format-8")
        retained_success = *result;
    } else {
      check(failure != nullptr, name + ": expected failure");
      const auto actual = classify(failure);
      check(actual.type == row.at("ErrorType").get<std::string>(),
            name + ": error type " + actual.type + " ('" + actual.message +
                "')");
      check(actual.message == row.at("ErrorMessage").get<std::string>(),
            name + ": error message '" + actual.message + "'");
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
    ++passed;
  }

  check(passed == 37, "exact row accounting");
  check(retained_success.has_value(), "retained success");
  auto independent = *retained_success;
  independent.galaxy.systems.front().name = "independent legacy clone";
  independent.galaxy.fleets.front().planned_route_system_ids.push_back(
      987654321);
  check(retained_success->galaxy.systems.front().name !=
            independent.galaxy.systems.front().name &&
            retained_success->galaxy.fleets.front().planned_route_system_ids !=
                independent.galaxy.fleets.front().planned_route_system_ids,
        "restored legacy world deep copy");
  verify_source_inventory(fixture, source_root);
  std::cout << "Legacy galaxy payload parity: " << passed << '/' << passed
            << " exact actual-source rows passed.\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "Usage: legacy_galaxy_payload_persistence_tests <fixture> "
          "<Game source root>");
    return run(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n'
              << "Working directory: " << fs::current_path().string() << '\n'
              << "Fixture path: "
              << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
              << '\n'
              << "Source root: "
              << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
              << '\n';
    return 1;
  }
}
