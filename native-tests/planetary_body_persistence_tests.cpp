#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

namespace {

double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("Unknown retained numeric literal: " + text);
}

Json floating(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

template <typename T>
std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
}

std::optional<double> optional_number(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<double>{number(value)};
}

StellarSystem system_value(const Json &value) {
  const auto optional_class = [&](std::string_view key) {
    const auto &field = value.at(std::string(key));
    return field.is_null()
               ? std::optional<StellarClass>{}
               : std::optional{static_cast<StellarClass>(field.get<int>())};
  };
  return {
      value.at("Id"),
      value.at("Name"),
      {value.at("X").get<float>(), value.at("Y").get<float>(),
       optional_number(value.at("GalacticDepthLightYears"))},
      optional_class("StellarClass"),
      optional_class("SecondaryStellarClass"),
      optional_class("TertiaryStellarClass"),
      optional_value<std::string>(value.at("CatalogPresetId")),
      optional_value<std::string>(value.at("StellarCatalogId")),
      static_cast<StarArchetype>(value.at("Archetype").get<int>()),
      value.at("HasHabitableWorld"),
      value.at("HasAnomaly"),
      value.at("HasRareResource"),
      value.at("HasPreWarpCivilization"),
  };
}

Json system_json(const StellarSystem &value) {
  const auto optional_enum = [](const auto &item) {
    return item ? Json(static_cast<int>(*item)) : Json(nullptr);
  };
  const auto optional_text = [](const auto &item) {
    return item ? Json(*item) : Json(nullptr);
  };
  return {
      {"Id", value.id},
      {"Name", value.name},
      {"X", floating(value.position.x)},
      {"Y", floating(value.position.y)},
      {"Archetype", static_cast<int>(value.archetype)},
      {"HasHabitableWorld", value.has_habitable_world},
      {"HasAnomaly", value.has_anomaly},
      {"HasRareResource", value.has_rare_resource},
      {"HasPreWarpCivilization", value.has_pre_warp_civilization},
      {"CatalogPresetId", optional_text(value.catalog_preset_id)},
      {"StellarClass", optional_enum(value.primary)},
      {"SecondaryStellarClass", optional_enum(value.secondary)},
      {"TertiaryStellarClass", optional_enum(value.tertiary)},
      {"GalacticDepthLightYears",
       value.position.depth_light_years
           ? floating(*value.position.depth_light_years)
           : Json(nullptr)},
      {"StellarCatalogId", optional_text(value.stellar_catalog_id)},
  };
}

PlanetaryEnvironmentPersistenceDto environment_dto(const Json &value) {
  return {
      number(value.at("GravityG")),
      number(value.at("TemperatureKelvin")),
      number(value.at("PressureKPa")),
      static_cast<PlanetaryAtmosphereRegime>(
          value.at("Atmosphere").get<int>()),
      static_cast<PlanetarySolventRegime>(
          value.at("AvailableSolvent").get<int>()),
      number(value.at("RadiationHazard")),
      value.at("IsImmersedEnvironment"),
      value.at("HasSolidSurface"),
  };
}

PlanetaryBodyPersistenceDto body_dto(const Json &value) {
  return {
      value.at("Id"),
      value.at("SystemId"),
      optional_value<int>(value.at("ParentBodyId")),
      value.at("OrbitIndex"),
      optional_value<std::string>(value.at("Name")),
      static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>()),
      number(value.at("RadiusEarth")),
      number(value.at("MassEarth")),
      value.at("Environment").is_null()
          ? std::nullopt
          : std::optional{environment_dto(value.at("Environment"))},
      value.at("LegacyColonizationCandidate"),
      value.at("HasRareResource"),
      value.at("HasAnomaly"),
      value.at("HasPreWarpCivilization"),
      number(value.at("OrbitalEccentricity")),
      number(value.at("OrbitalInclinationDegrees")),
  };
}

Json environment_json(const PlanetaryEnvironmentPersistenceDto &value) {
  return {
      {"GravityG", floating(value.gravity_g)},
      {"TemperatureKelvin", floating(value.temperature_kelvin)},
      {"PressureKPa", floating(value.pressure_kpa)},
      {"Atmosphere", static_cast<int>(value.atmosphere)},
      {"AvailableSolvent", static_cast<int>(value.available_solvent)},
      {"RadiationHazard", floating(value.radiation_hazard)},
      {"IsImmersedEnvironment", value.is_immersed_environment},
      {"HasSolidSurface", value.has_solid_surface},
  };
}

Json body_dto_json(const PlanetaryBodyPersistenceDto &value) {
  return {
      {"Id", value.id},
      {"SystemId", value.system_id},
      {"ParentBodyId",
       value.parent_body_id ? Json(*value.parent_body_id) : Json(nullptr)},
      {"OrbitIndex", value.orbit_index},
      {"Name", value.name ? Json(*value.name) : Json(nullptr)},
      {"Kind", static_cast<int>(value.kind)},
      {"RadiusEarth", floating(value.radius_earth)},
      {"MassEarth", floating(value.mass_earth)},
      {"Environment", value.environment ? environment_json(*value.environment)
                                          : Json(nullptr)},
      {"LegacyColonizationCandidate", value.legacy_colonization_candidate},
      {"HasRareResource", value.has_rare_resource},
      {"HasAnomaly", value.has_anomaly},
      {"HasPreWarpCivilization", value.has_pre_warp_civilization},
      {"OrbitalEccentricity", floating(value.orbital_eccentricity)},
      {"OrbitalInclinationDegrees",
       floating(value.orbital_inclination_degrees)},
  };
}

PlanetaryBody body_value(const Json &value) {
  const auto environment = environment_dto(value.at("Environment"));
  return {
      value.at("Id"),
      value.at("SystemId"),
      optional_value<int>(value.at("ParentBodyId")),
      value.at("OrbitIndex"),
      value.at("Name"),
      static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>()),
      number(value.at("RadiusEarth")),
      number(value.at("MassEarth")),
      {environment.gravity_g,
       environment.temperature_kelvin,
       environment.pressure_kpa,
       environment.atmosphere,
       environment.available_solvent,
       environment.radiation_hazard,
       environment.is_immersed_environment,
       environment.has_solid_surface},
      value.at("LegacyColonizationCandidate"),
      value.at("HasRareResource"),
      value.at("HasAnomaly"),
      value.at("HasPreWarpCivilization"),
      number(value.at("OrbitalEccentricity")),
      number(value.at("OrbitalInclinationDegrees")),
  };
}

Json body_json(const PlanetaryBody &value) {
  return body_dto_json({
      value.id,
      value.system_id,
      value.parent_body_id,
      value.orbit_index,
      value.name,
      value.kind,
      value.radius_earth,
      value.mass_earth,
      PlanetaryEnvironmentPersistenceDto{
          value.environment.gravity_g,
          value.environment.temperature_kelvin,
          value.environment.pressure_kpa,
          value.environment.atmosphere,
          value.environment.available_solvent,
          value.environment.radiation_hazard,
          value.environment.is_immersed_environment,
          value.environment.has_solid_surface},
      value.legacy_colonization_candidate,
      value.has_rare_resource,
      value.has_anomaly,
      value.has_pre_warp_civilization,
      value.orbital_eccentricity,
      value.orbital_inclination_degrees,
  });
}

template <typename T, typename Project>
Json array_json(const std::vector<T> &values, Project project) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(project(value));
  return result;
}

Json input_json(const PlanetaryBodyPersistenceInput &input,
                const std::vector<StellarSystem> &systems) {
  auto bodies = input.bodies_present ? Json::array() : Json(nullptr);
  if (input.bodies_present) {
    for (const auto &body : input.bodies)
      bodies.push_back(body ? body_dto_json(*body) : Json(nullptr));
  }
  return {
      {"BodiesPresent", input.bodies_present},
      {"Bodies", std::move(bodies)},
      {"Systems", array_json(systems, system_json)},
  };
}

Json capture_input_json(const std::vector<PlanetaryBody> &bodies,
                        const std::vector<StellarSystem> &systems) {
  return {
      {"BodiesPresent", true},
      {"Bodies", array_json(bodies, body_json)},
      {"Systems", array_json(systems, system_json)},
  };
}

bool equal_json(const Json &left, const Json &right, std::string &path) {
  if (left.type() == right.type()) {
    if (left.is_object()) {
      if (left.size() != right.size())
        return false;
      for (auto item = left.begin(); item != left.end(); ++item) {
        const auto other = right.find(item.key());
        if (other == right.end())
          return false;
        const auto previous = path;
        path += "/" + item.key();
        if (!equal_json(item.value(), *other, path))
          return false;
        path = previous;
      }
      return true;
    }
    if (left.is_array()) {
      if (left.size() != right.size())
        return false;
      for (std::size_t index = 0; index < left.size(); ++index) {
        const auto previous = path;
        path += "/" + std::to_string(index);
        if (!equal_json(left[index], right[index], path))
          return false;
        path = previous;
      }
      return true;
    }
    if (left.is_number_float()) {
      const auto a = left.get<double>();
      const auto b = right.get<double>();
      return (std::isnan(a) && std::isnan(b)) || std::abs(a - b) <= 1e-10;
    }
    return left == right;
  }
  const auto left_integer = left.is_number_integer() || left.is_number_unsigned();
  const auto right_integer =
      right.is_number_integer() || right.is_number_unsigned();
  if (left_integer && right_integer) {
    if (left.is_number_unsigned() && right.is_number_unsigned())
      return left.get<std::uint64_t>() == right.get<std::uint64_t>();
    if (left.is_number_integer() && right.is_number_integer())
      return left.get<std::int64_t>() == right.get<std::int64_t>();
    if (left.is_number_unsigned()) {
      const auto signed_value = right.get<std::int64_t>();
      return signed_value >= 0 && left.get<std::uint64_t>() ==
                                      static_cast<std::uint64_t>(signed_value);
    }
    const auto signed_value = left.get<std::int64_t>();
    return signed_value >= 0 && right.get<std::uint64_t>() ==
                                    static_cast<std::uint64_t>(signed_value);
  }
  if (left.is_number() && right.is_number()) {
    const auto a = left.get<double>();
    const auto b = right.get<double>();
    return (std::isnan(a) && std::isnan(b)) || std::abs(a - b) <= 1e-10;
  }
  return false;
}

void require_equal(const Json &actual, const Json &expected,
                   std::string_view row, std::string_view field) {
  std::string path;
  if (!equal_json(actual, expected, path)) {
    std::ostringstream message;
    message << row << ' ' << field << " differs at " << path << "\nactual: "
            << actual.dump() << "\nexpected: " << expected.dump();
    throw std::runtime_error(message.str());
  }
}

std::string file_bytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("Cannot open: " + path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string sha256(std::string_view bytes) {
  auto digest = detail::adaptive_research_sha256(
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
  std::ostringstream output;
  output << std::uppercase << std::hex << std::setfill('0');
  for (const auto byte : digest)
    output << std::setw(2) << static_cast<unsigned>(byte);
  return output.str();
}

Json error_json(const std::exception_ptr &failure) {
  if (!failure)
    return nullptr;
  try {
    std::rethrow_exception(failure);
  } catch (const PlanetaryBodyPersistenceDataError &error) {
    return {
        {"Type", "InvalidDataException"},
        {"Message", error.what()},
        {"InnerType", error.inner_type() ? Json(*error.inner_type())
                                          : Json(nullptr)},
        {"InnerMessage", error.inner_message() ? Json(*error.inner_message())
                                                : Json(nullptr)},
    };
  } catch (const std::exception &error) {
    return {{"Type", "UnexpectedNativeException"},
            {"Message", error.what()},
            {"InnerType", nullptr},
            {"InnerMessage", nullptr}};
  }
}

Json expected_error(const Json &row) {
  if (row.at("ErrorType").is_null())
    return nullptr;
  return {
      {"Type", row.at("ErrorType")},
      {"Message", row.at("ErrorMessage")},
      {"InnerType", row.at("ErrorInnerType")},
      {"InnerMessage", row.at("ErrorInnerMessage")},
  };
}

void replay(const Json &row) {
  const auto name = row.at("Name").get<std::string>();
  const auto operation = row.at("Operation").get<std::string>();
  if (operation != "Restore" && operation != "Capture")
    throw std::runtime_error(name + " has unknown operation");

  const auto &input = row.at("Input");
  std::vector<StellarSystem> systems;
  for (const auto &system : input.at("Systems"))
    systems.push_back(system_value(system));

  std::exception_ptr failure;
  Json result;
  Json after;
  if (operation == "Restore") {
    PlanetaryBodyPersistenceInput bodies;
    bodies.bodies_present = input.at("BodiesPresent");
    if (bodies.bodies_present) {
      for (const auto &body : input.at("Bodies"))
        bodies.bodies.push_back(body.is_null()
                                    ? std::nullopt
                                    : std::optional{body_dto(body)});
    }
    require_equal(input_json(bodies, systems), row.at("Before"), name,
                  "before");
    std::vector<PlanetaryBody> typed_result;
    try {
      typed_result = restore_planetary_bodies(bodies, systems);
    } catch (...) {
      failure = std::current_exception();
    }
    after = input_json(bodies, systems);
    if (!failure)
      result = array_json(typed_result, body_json);
  } else {
    std::vector<PlanetaryBody> bodies;
    for (const auto &body : input.at("Bodies"))
      bodies.push_back(body_value(body));
    require_equal(capture_input_json(bodies, systems), row.at("Before"), name,
                  "before");
    PlanetaryBodyPersistenceInput typed_result;
    try {
      typed_result = capture_planetary_bodies(bodies, systems);
    } catch (...) {
      failure = std::current_exception();
    }
    after = capture_input_json(bodies, systems);
    if (!failure) {
      if (!typed_result.bodies_present)
        throw std::runtime_error(name + " capture result is not present");
      result = Json::array();
      for (const auto &body : typed_result.bodies)
        result.push_back(body ? body_dto_json(*body) : Json(nullptr));
    }
  }

  require_equal(after, row.at("After"), name, "after");
  require_equal(error_json(failure), expected_error(row), name, "error");
  if (!failure)
    require_equal(result, row.at("Result"), name, "result");
}

void ownership_probes() {
  StellarSystem system{1, "System", {0, 0}, std::nullopt, std::nullopt,
                       std::nullopt, std::nullopt, std::nullopt,
                       StarArchetype::Standard, false, false, false, false};
  PlanetaryBody body{1,
                     1,
                     std::nullopt,
                     0,
                     "Original",
                     PlanetaryBodyKind::Planet,
                     1,
                     1,
                     {1,
                      280,
                      100,
                      PlanetaryAtmosphereRegime::OxygenNitrogen,
                      PlanetarySolventRegime::Water,
                      0,
                      false,
                      true},
                     true,
                     false,
                     false,
                     false};
  std::vector bodies{body};
  std::vector systems{system};
  auto captured = capture_planetary_bodies(bodies, systems);
  bodies.front().name = "Changed";
  systems.front().name = "Changed";
  if (!captured.bodies.front() ||
      captured.bodies.front()->name != std::optional<std::string>{"Original"})
    throw std::runtime_error("capture result aliases source body");

  auto restored = restore_planetary_bodies(captured, systems);
  captured.bodies.front()->name = "DTO changed";
  if (restored.front().name != "Original")
    throw std::runtime_error("restore result aliases source DTO");

  // Managed strings are valid Unicode. A truncated native UTF-8 prefix is an
  // explicit typed-input boundary, but the shared validator must still inspect
  // it without reading past the supplied byte range.
  auto truncated_utf8 = body;
  truncated_utf8.name.assign(1, static_cast<char>(0xe2));
  validate_planetary_body(truncated_utf8);
}

int run(const fs::path &fixture, const fs::path &source_root) {
  std::string integer_path;
  if (equal_json(Json(9007199254740993ULL), Json(9007199254740992ULL),
                 integer_path))
    throw std::runtime_error("integer comparator lost precision");

  const auto retained_bytes = file_bytes(fixture);
  if (sha256(retained_bytes) !=
      "4C8170428B5E2E05AEDF0EF9C1C6D148E7AA79448531CA4E6DDB2932D43BB1AE")
    throw std::runtime_error("fixture fingerprint mismatch");
  const auto document = Json::parse(retained_bytes);
  constexpr std::array expected_sources = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Models/PlanetaryBodyState.cs",
      "Simulation/Models/StarSystemState.cs",
  };
  if (document.at("SchemaVersion") != 1 || document.at("RowCount") != 32 ||
      document.at("Rows").size() != 32 ||
      document.at("SourceFiles").size() != expected_sources.size())
    throw std::runtime_error("fixture metadata mismatch");

  std::vector<std::pair<fs::path, std::string>> source_fingerprints;
  for (std::size_t index = 0; index < expected_sources.size(); ++index) {
    const auto &source = document.at("SourceFiles")[index];
    if (source.at("Path") != expected_sources[index])
      throw std::runtime_error("source inventory mismatch");
    const auto path = source_root / source.at("Path").get<std::string>();
    const auto expected = source.at("Sha256").get<std::string>();
    if (sha256(file_bytes(path)) != expected)
      throw std::runtime_error("source fingerprint mismatch: " + path.string());
    source_fingerprints.emplace_back(path, expected);
  }

  for (const auto &row : document.at("Rows"))
    replay(row);
  ownership_probes();

  for (const auto &[path, expected] : source_fingerprints)
    if (sha256(file_bytes(path)) != expected)
      throw std::runtime_error("source changed during replay: " + path.string());
  if (sha256(file_bytes(fixture)) !=
      "4C8170428B5E2E05AEDF0EF9C1C6D148E7AA79448531CA4E6DDB2932D43BB1AE")
    throw std::runtime_error("fixture changed during replay");

  std::cout << "Campaign planetary native replay: 32/32 rows and ownership "
               "probes passed.\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: campaign_planetary_persistence_tests <fixture> "
          "<source-root>");
    return run(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what()
              << "\nWorking directory: " << fs::current_path()
              << "\nFixture path: "
              << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
              << "\nSource root: "
              << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
              << '\n';
    return 1;
  }
}
