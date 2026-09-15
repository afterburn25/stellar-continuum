#include <stellar/core/galaxy_reference_validation.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/galaxy_economy_persistence.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

namespace encounter_helper {
#define main embedded_massive_encounter_test_main
#include "massive_combat_persistence_tests.cpp"
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

double number(const Json &value) {
  return economy_helper::number(value);
}

template <class T>
std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}

PlanetaryBody body(const Json &value) {
  const auto &environment = value.at("Environment");
  return {value.at("Id"),
          value.at("SystemId"),
          optional_value<int>(value.at("ParentBodyId")),
          value.at("OrbitIndex"),
          value.at("Name"),
          static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>()),
          number(value.at("RadiusEarth")),
          number(value.at("MassEarth")),
          {number(environment.at("GravityG")),
           number(environment.at("TemperatureKelvin")),
           number(environment.at("PressureKPa")),
           static_cast<PlanetaryAtmosphereRegime>(
               environment.at("Atmosphere").get<int>()),
           static_cast<PlanetarySolventRegime>(
               environment.at("AvailableSolvent").get<int>()),
           number(environment.at("RadiationHazard")),
           environment.at("IsImmersedEnvironment"),
           environment.at("HasSolidSurface")},
          value.at("LegacyColonizationCandidate"),
          value.at("HasRareResource"),
          value.at("HasAnomaly"),
          value.at("HasPreWarpCivilization"),
          value.value("OrbitalEccentricity", 0.0),
          value.value("OrbitalInclinationDegrees", 0.0)};
}

void check_body(const PlanetaryBody &actual, const Json &expected,
                const std::string &label) {
  require(actual.id == expected.at("Id").get<int>() &&
              actual.system_id == expected.at("SystemId").get<int>() &&
              actual.parent_body_id ==
                  optional_value<int>(expected.at("ParentBodyId")) &&
              actual.orbit_index == expected.at("OrbitIndex").get<int>() &&
              actual.name == expected.at("Name").get<std::string>() &&
              static_cast<int>(actual.kind) == expected.at("Kind").get<int>(),
          label + " identity");
  economy_helper::equal_number(actual.radius_earth,
                               number(expected.at("RadiusEarth")),
                               label + ".RadiusEarth");
  economy_helper::equal_number(actual.mass_earth,
                               number(expected.at("MassEarth")),
                               label + ".MassEarth");
  const auto &environment = expected.at("Environment");
  economy_helper::equal_number(actual.environment.gravity_g,
                               number(environment.at("GravityG")),
                               label + ".GravityG");
  economy_helper::equal_number(actual.environment.temperature_kelvin,
                               number(environment.at("TemperatureKelvin")),
                               label + ".TemperatureKelvin");
  economy_helper::equal_number(actual.environment.pressure_kpa,
                               number(environment.at("PressureKPa")),
                               label + ".PressureKPa");
  economy_helper::equal_number(actual.environment.radiation_hazard,
                               number(environment.at("RadiationHazard")),
                               label + ".RadiationHazard");
  require(static_cast<int>(actual.environment.atmosphere) ==
              environment.at("Atmosphere").get<int>() &&
              static_cast<int>(actual.environment.available_solvent) ==
                  environment.at("AvailableSolvent").get<int>() &&
              actual.environment.is_immersed_environment ==
                  environment.at("IsImmersedEnvironment").get<bool>() &&
              actual.environment.has_solid_surface ==
                  environment.at("HasSolidSurface").get<bool>() &&
              actual.legacy_colonization_candidate ==
                  expected.at("LegacyColonizationCandidate").get<bool>() &&
              actual.has_rare_resource ==
                  expected.at("HasRareResource").get<bool>() &&
              actual.has_anomaly == expected.at("HasAnomaly").get<bool>() &&
              actual.has_pre_warp_civilization ==
                  expected.at("HasPreWarpCivilization").get<bool>(),
          label + " flags");
}

struct WorldStorage {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<FleetState> fleets;
  std::vector<FleetPowerObservation> intelligence;
  std::optional<CampaignMassiveEncounter> encounter;

  GalaxyReferenceValidationView view() {
    return {systems, bodies, civilizations, colonies, economies, fleets,
            intelligence, encounter ? &*encounter : nullptr};
  }
};

WorldStorage world(const Json &value) {
  WorldStorage result;
  for (const auto &system : value.at("Systems")) {
    StellarSystem item;
    item.id = system.at("Id");
    result.systems.push_back(std::move(item));
  }
  for (const auto &entry : value.at("Bodies"))
    result.bodies.push_back(body(entry));
  for (const auto &entry : value.at("Civilizations")) {
    Civilization item;
    item.id = entry.at("Id");
    item.species_id = entry.at("SpeciesId");
    result.civilizations.push_back(std::move(item));
  }
  for (const auto &entry : value.at("Colonies"))
    result.colonies.push_back(economy_helper::colony(entry));
  for (const auto &entry : value.at("Economies"))
    result.economies.push_back(economy_helper::economy(entry));
  for (const auto &entry : value.at("Fleets"))
    result.fleets.push_back(fleet_helper::parse_fleet(entry));
  for (const auto &entry : value.at("CombatIntelligence"))
    result.intelligence.push_back({entry.at("ObserverId"), entry.at("FleetId"),
                                   number(entry.at("Power")),
                                   number(entry.at("ObservedDay")),
                                   entry.at("Evidence")});
  if (!value.at("ActiveCombatEncounter").is_null())
    result.encounter =
        encounter_helper::encounter(value.at("ActiveCombatEncounter"));
  return result;
}

template <typename JsonValue>
void add_system_numerics_projection(JsonValue &value) {
  if (value.is_array()) {
    for (auto &item : value) add_system_numerics_projection(item);
    return;
  }
  if (!value.is_object()) return;
  if (value.contains("X") && value.contains("Y") &&
      value.contains("Vector") && value.contains("IsFinite"))
    value["Vector"] = JsonValue{{"X", value.at("X")},
                                 {"Y", value.at("Y")}};
  for (auto &[key, item] : value.items()) {
    if (key != "Vector") add_system_numerics_projection(item);
  }
}

void check_world(const WorldStorage &actual, const Json &expected,
                 const std::string &label) {
  require(actual.systems.size() == expected.at("Systems").size(),
          label + " systems size");
  for (std::size_t index = 0; index < actual.systems.size(); ++index)
    require(actual.systems[index].id ==
                expected.at("Systems")[index].at("Id").get<int>(),
            label + " system id");
  require(actual.bodies.size() == expected.at("Bodies").size(),
          label + " bodies size");
  for (std::size_t index = 0; index < actual.bodies.size(); ++index)
    check_body(actual.bodies[index], expected.at("Bodies")[index],
               label + " body");
  require(actual.civilizations.size() ==
              expected.at("Civilizations").size(),
          label + " civilizations size");
  for (std::size_t index = 0; index < actual.civilizations.size(); ++index)
    require(actual.civilizations[index].id ==
                    expected.at("Civilizations")[index].at("Id").get<int>() &&
                actual.civilizations[index].species_id ==
                    expected.at("Civilizations")[index]
                        .at("SpeciesId").get<std::string>(),
            label + " civilization");
  economy_helper::check_list(actual.colonies, expected.at("Colonies"),
                             label + " colonies",
                             economy_helper::check_colony);
  economy_helper::check_list(actual.economies, expected.at("Economies"),
                             label + " economies",
                             economy_helper::check_economy_values);
  require(actual.fleets.size() == expected.at("Fleets").size(),
          label + " fleets size");
  for (std::size_t index = 0; index < actual.fleets.size(); ++index)
    fleet_helper::check_fleet(actual.fleets[index],
                              expected.at("Fleets")[index], label + " fleet");
  require(actual.intelligence.size() ==
              expected.at("CombatIntelligence").size(),
          label + " intelligence size");
  for (std::size_t index = 0; index < actual.intelligence.size(); ++index) {
    const auto &observation = actual.intelligence[index];
    const auto &wanted = expected.at("CombatIntelligence")[index];
    require(observation.observer_id == wanted.at("ObserverId").get<int>() &&
                observation.fleet_id == wanted.at("FleetId").get<int>() &&
                observation.evidence == wanted.at("Evidence").get<std::string>(),
            label + " intelligence identity");
    economy_helper::equal_number(observation.power,
                                 number(wanted.at("Power")),
                                 label + " intelligence power");
    economy_helper::equal_number(observation.observed_day,
                                 number(wanted.at("ObservedDay")),
                                 label + " intelligence day");
  }
  require(actual.encounter.has_value() ==
              !expected.at("ActiveCombatEncounter").is_null(),
          label + " encounter presence");
  if (actual.encounter) {
    auto retained = encounter_helper::encounter_json(*actual.encounter);
    // IncludeFields exposes MassivePoint.Vector's System.Numerics.Vector2
    // fields. Derive that computed projection from the authoritative X/Y.
    add_system_numerics_projection(retained);
    require(retained == expected.at("ActiveCombatEncounter"),
            label + " encounter");
  }
}

struct NativeError {
  std::string type;
  std::string message;
};

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "Expected source root and fixture path.");
    const auto source_root = std::filesystem::absolute(argv[1]);
    const auto fixture_path = std::filesystem::absolute(argv[2]);
    const auto fixture_bytes = bytes(fixture_path);
    require(hash(fixture_bytes) ==
                "1B3BCDFB7476F0262C54A92E7D1F313B80FE190B37A84F0F96BB2C4B4CFDE630",
            "fixture hash");
    const auto fixture = Json::parse(fixture_bytes);
    require(fixture.at("Schema") ==
                "stellar-galaxy-reference-validation-v1",
            "fixture schema");
    require(fixture.at("RowCount").get<std::size_t>() ==
                fixture.at("Rows").size(),
            "fixture row count");
    require(fixture.at("SourceHashesBefore") ==
                fixture.at("SourceHashesAfter"),
            "oracle source changed");
    for (const auto &[path, expected] :
         fixture.at("SourceHashesBefore").items())
      require(hash(bytes(source_root / path)) == expected.get<std::string>(),
              "source hash before " + path);

    std::size_t replayed = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      auto input = world(row.at("Before"));
      check_world(input, row.at("Before"), name + " decoded before");
      std::optional<NativeError> error;
      try {
        validate_galaxy_references(input.view());
      } catch (const GalaxyReferenceValidationDataError &caught) {
        error = NativeError{"InvalidDataException", caught.what()};
      } catch (const GalaxyReferenceValidationOperationError &caught) {
        error = NativeError{"InvalidOperationException", caught.what()};
      } catch (const GalaxyReferenceValidationArgumentError &caught) {
        error = NativeError{"ArgumentException", caught.what()};
      } catch (const GalaxyReferenceValidationRangeError &caught) {
        error = NativeError{"ArgumentOutOfRangeException", caught.what()};
      } catch (const GalaxyReferenceValidationOverflowError &caught) {
        error = NativeError{"OverflowException", caught.what()};
      }
      check_world(input, row.at("After"), name + " after");
      if (row.at("Error").is_null()) {
        require(!error, name + " unexpected error");
      } else {
        require(error.has_value(), name + " expected error");
        require(error->type == row.at("Error").at("Type").get<std::string>(),
                name + " error type");
        require(error->message ==
                    row.at("Error").at("Message").get<std::string>(),
                name + " error message");
      }
      ++replayed;
    }
    require(replayed == 56, "exact row accounting");
    for (const auto &[path, expected] :
         fixture.at("SourceHashesAfter").items())
      require(hash(bytes(source_root / path)) == expected.get<std::string>(),
              "source hash after " + path);
    std::cout << "galaxy reference validation parity: " << replayed
              << " actual-source rows passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "galaxy reference validation parity failure: "
              << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd: " << std::filesystem::current_path().string() << '\n'
              << "source root: "
              << (argc >= 2 ? std::filesystem::absolute(argv[1]).string()
                            : "<missing>") << '\n'
              << "fixture: "
              << (argc >= 3 ? std::filesystem::absolute(argv[2]).string()
                            : "<missing>") << '\n';
    return 1;
  }
}
