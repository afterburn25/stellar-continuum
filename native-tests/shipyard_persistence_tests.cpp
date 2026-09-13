#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/shipyard_persistence.hpp>

#include <nlohmann/json.hpp>

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
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

namespace {

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
  throw std::runtime_error("Unknown named floating-point input.");
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

template <class T> std::optional<T> decode_optional(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
}

Json encode_optional(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

Json encode_optional(const std::optional<int> &value) {
  return value ? Json(*value) : Json(nullptr);
}

QueuedShipBuildPersistenceDto decode_queued_dto(const Json &value) {
  return {decode_optional<std::string>(value.at("OrderId")),
          value.at("DesignId").get<std::string>(),
          decode_number(value.at("AuthorizationCredits")),
          decode_number(value.at("ReservedPopulationMillions")),
          decode_optional<std::string>(
              value.at("ReservedPopulationSpeciesId")),
          decode_optional<int>(value.at("ReservedPopulationSourceColonyId"))};
}

ShipyardPersistenceDto decode_dto(const Json &value) {
  ShipyardPersistenceDto result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.next_order_sequence = value.at("NextOrderSequence").get<std::int64_t>();
  result.active_design_id =
      decode_optional<std::string>(value.at("ActiveDesignId"));
  result.active_order_id =
      decode_optional<std::string>(value.at("ActiveOrderId"));
  result.active_build_progress = decode_number(value.at("ActiveBuildProgress"));
  result.active_authorization_credits =
      decode_number(value.at("ActiveAuthorizationCredits"));
  result.reserved_population_millions =
      decode_number(value.at("ReservedPopulationMillions"));
  result.reserved_population_species_id =
      decode_optional<std::string>(value.at("ReservedPopulationSpeciesId"));
  result.reserved_population_source_colony_id =
      decode_optional<int>(value.at("ReservedPopulationSourceColonyId"));
  result.queued_builds_present = !value.at("QueuedBuilds").is_null();
  if (result.queued_builds_present)
    for (const auto &item : value.at("QueuedBuilds"))
      result.queued_builds.push_back(decode_queued_dto(item));
  return result;
}

ShipBuildOrderState decode_order(const Json &value) {
  return {value.at("OrderId").get<std::string>(),
          value.at("DesignId").get<std::string>(),
          decode_number(value.at("AuthorizationCredits")),
          decode_number(value.at("ReservedPopulationMillions")),
          decode_optional<std::string>(
              value.at("ReservedPopulationSpeciesId")),
          decode_optional<int>(value.at("ReservedPopulationSourceColonyId"))};
}

ShipyardState decode_state(const Json &value) {
  ShipyardState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.next_order_sequence = value.at("NextOrderSequence").get<std::int64_t>();
  result.active_design_id =
      decode_optional<std::string>(value.at("ActiveDesignId"));
  result.active_order_id =
      decode_optional<std::string>(value.at("ActiveOrderId"));
  result.active_build_progress = decode_number(value.at("ActiveBuildProgress"));
  result.active_authorization_credits =
      decode_number(value.at("ActiveAuthorizationCredits"));
  result.reserved_population_millions =
      decode_number(value.at("ReservedPopulationMillions"));
  result.reserved_population_species_id =
      decode_optional<std::string>(value.at("ReservedPopulationSpeciesId"));
  result.reserved_population_source_colony_id =
      decode_optional<int>(value.at("ReservedPopulationSourceColonyId"));
  for (const auto &item : value.at("QueuedBuilds"))
    result.queued_builds.push_back(decode_order(item));
  return result;
}

Json queued_dto_json(const QueuedShipBuildPersistenceDto &value) {
  return {{"OrderId", encode_optional(value.order_id)},
          {"DesignId", value.design_id},
          {"AuthorizationCredits", encode_number(value.authorization_credits)},
          {"ReservedPopulationMillions",
           encode_number(value.reserved_population_millions)},
          {"ReservedPopulationSpeciesId",
           encode_optional(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           encode_optional(value.reserved_population_source_colony_id)}};
}

Json dto_json(const ShipyardPersistenceDto &value) {
  Json queue = value.queued_builds_present ? Json::array() : Json(nullptr);
  if (value.queued_builds_present)
    for (const auto &item : value.queued_builds)
      queue.push_back(queued_dto_json(item));
  return {{"CivilizationId", value.civilization_id},
          {"NextOrderSequence", value.next_order_sequence},
          {"ActiveDesignId", encode_optional(value.active_design_id)},
          {"ActiveOrderId", encode_optional(value.active_order_id)},
          {"ActiveBuildProgress", encode_number(value.active_build_progress)},
          {"ActiveAuthorizationCredits",
           encode_number(value.active_authorization_credits)},
          {"ReservedPopulationMillions",
           encode_number(value.reserved_population_millions)},
          {"ReservedPopulationSpeciesId",
           encode_optional(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           encode_optional(value.reserved_population_source_colony_id)},
          {"QueuedBuilds", std::move(queue)}};
}

Json order_json(const ShipBuildOrderState &value) {
  return {{"OrderId", value.order_id},
          {"DesignId", value.design_id},
          {"AuthorizationCredits", encode_number(value.authorization_credits)},
          {"ReservedPopulationMillions",
           encode_number(value.reserved_population_millions)},
          {"ReservedPopulationSpeciesId",
           encode_optional(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           encode_optional(value.reserved_population_source_colony_id)}};
}

Json state_json(const ShipyardState &value) {
  Json queue = Json::array();
  for (const auto &item : value.queued_builds)
    queue.push_back(order_json(item));
  return {{"CivilizationId", value.civilization_id},
          {"NextOrderSequence", value.next_order_sequence},
          {"ActiveDesignId", encode_optional(value.active_design_id)},
          {"ActiveOrderId", encode_optional(value.active_order_id)},
          {"ActiveBuildProgress", encode_number(value.active_build_progress)},
          {"ActiveAuthorizationCredits",
           encode_number(value.active_authorization_credits)},
          {"ReservedPopulationMillions",
           encode_number(value.reserved_population_millions)},
          {"ReservedPopulationSpeciesId",
           encode_optional(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId",
           encode_optional(value.reserved_population_source_colony_id)},
          {"QueuedBuilds", std::move(queue)}};
}

template <class T, class Projection>
Json array_json(const std::vector<T> &values, Projection projection) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back(projection(value));
  return result;
}

bool integer_equal(const Json &left, const Json &right) {
  if (left.is_number_unsigned()) {
    const auto value = left.get<std::uint64_t>();
    if (right.is_number_unsigned())
      return value == right.get<std::uint64_t>();
    const auto other = right.get<std::int64_t>();
    return other >= 0 && value == static_cast<std::uint64_t>(other);
  }
  const auto value = left.get<std::int64_t>();
  if (!right.is_number_unsigned())
    return value == right.get<std::int64_t>();
  return value >= 0 &&
         static_cast<std::uint64_t>(value) == right.get<std::uint64_t>();
}

bool equal_json(const Json &left, const Json &right, std::string &path) {
  if (left.type() != right.type()) {
    if (!left.is_number() || !right.is_number())
      return false;
    const auto left_integer =
        left.is_number_integer() || left.is_number_unsigned();
    const auto right_integer =
        right.is_number_integer() || right.is_number_unsigned();
    if (left_integer && right_integer)
      return integer_equal(left, right);
    return std::abs(left.get<double>() - right.get<double>()) <= 1e-10;
  }
  if (left.is_primitive()) {
    if (left.is_number_float())
      return left == right ||
             std::abs(left.get<double>() - right.get<double>()) <= 1e-10;
    return left == right;
  }
  if (left.size() != right.size())
    return false;
  if (left.is_array()) {
    for (std::size_t index = 0; index < left.size(); ++index) {
      const auto previous = path;
      path += "/" + std::to_string(index);
      if (!equal_json(left[index], right[index], path))
        return false;
      path = previous;
    }
    return true;
  }
  for (auto item = left.begin(); item != left.end(); ++item) {
    const auto previous = path;
    path += "/" + item.key();
    if (!right.contains(item.key()) ||
        !equal_json(item.value(), right.at(item.key()), path))
      return false;
    path = previous;
  }
  return true;
}

Json operation_error(std::exception_ptr error) {
  if (!error)
    return nullptr;
  try {
    std::rethrow_exception(error);
  } catch (const ShipyardPersistenceDataError &value) {
    return {{"Type", "InvalidDataException"}, {"Message", value.what()}};
  } catch (const ShipyardPersistenceOperationError &value) {
    return {{"Type", "InvalidOperationException"}, {"Message", value.what()}};
  } catch (const std::exception &value) {
    return {{"Type", "UnexpectedNativeException"},
            {"Message", value.what()}};
  }
}

void require_equal(const Json &actual, const Json &expected,
                   const std::string &name, std::string_view field) {
  std::string path;
  if (!equal_json(actual, expected, path))
    throw std::runtime_error(name + " " + std::string(field) + " " + path);
}

std::string read_bytes(const fs::path &path, std::string_view description) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("Cannot open " + std::string(description) + ": " +
                             path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string sha256(std::string_view value) {
  const auto digest = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest)
    output << std::setw(2) << static_cast<unsigned>(byte);
  auto text = output.str();
  std::ranges::transform(text, text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return text;
}

void replay_restore(const Json &row, const std::string &name) {
  const auto save_format_version = row.at("SaveFormatVersion").get<int>();
  std::vector<ShipyardPersistenceDto> dtos;
  for (const auto &value : row.at("Input").at("Dtos"))
    dtos.push_back(decode_dto(value));
  std::vector<Civilization> civilizations;
  for (const auto &value : row.at("Input").at("Civilizations")) {
    Civilization civilization;
    civilization.id = value.at("Id").get<int>();
    civilization.species_id = value.at("SpeciesId").get<std::string>();
    civilizations.push_back(std::move(civilization));
  }
  Json civilization_values = Json::array();
  for (const auto &value : civilizations)
    civilization_values.push_back(
        {{"Id", value.id}, {"SpeciesId", value.species_id}});
  const auto input_before = Json{
      {"Dtos", array_json(dtos, dto_json)},
      {"Civilizations", civilization_values}};
  require_equal(input_before, row.at("Before"), name, "decoded before");

  std::vector<ShipyardState> result;
  std::exception_ptr error;
  try {
    result = restore_shipyard_states(dtos, civilizations, save_format_version);
  } catch (...) {
    error = std::current_exception();
  }

  Json civilizations_after = Json::array();
  for (const auto &value : civilizations)
    civilizations_after.push_back(
        {{"Id", value.id}, {"SpeciesId", value.species_id}});
  const auto input_after = Json{
      {"Dtos", array_json(dtos, dto_json)},
      {"Civilizations", std::move(civilizations_after)}};
  require_equal(input_after, row.at("After"), name, "after");
  const auto expected_error = row.at("ErrorType").is_null()
                                  ? Json(nullptr)
                                  : Json{{"Type", row.at("ErrorType")},
                                         {"Message", row.at("ErrorMessage")}};
  require_equal(operation_error(error), expected_error, name, "error");
  if (!error)
    require_equal(array_json(result, state_json), row.at("Result"), name,
                  "result");
}

void replay_capture(const Json &row, const std::string &name) {
  std::vector<ShipyardState> states;
  for (const auto &value : row.at("Input"))
    states.push_back(decode_state(value));
  const auto input_before = array_json(states, state_json);
  require_equal(input_before, row.at("Before"), name, "decoded before");

  std::vector<ShipyardPersistenceDto> result;
  std::exception_ptr error;
  try {
    result = capture_shipyard_states(states);
  } catch (...) {
    error = std::current_exception();
  }

  require_equal(array_json(states, state_json), row.at("After"), name, "after");
  const auto expected_error = row.at("ErrorType").is_null()
                                  ? Json(nullptr)
                                  : Json{{"Type", row.at("ErrorType")},
                                         {"Message", row.at("ErrorMessage")}};
  require_equal(operation_error(error), expected_error, name, "error");
  if (!error) {
    const auto detached = array_json(result, dto_json);
    if (!states.empty()) {
      states.front().active_design_id = "mutated-live";
      states.front().queued_builds.clear();
    }
    const auto after_live_mutation = array_json(result, dto_json);
    std::string path;
    const Json actual = {{"Dtos", detached},
                         {"DetachedAfterLiveMutation", after_live_mutation},
                         {"Stable",
                          equal_json(detached, after_live_mutation, path)}};
    require_equal(actual, row.at("Result"), name, "result");
  }
}

int replay(const fs::path &fixture_path, const fs::path &source_root) {
  std::string regression_path;
  if (equal_json(Json(9007199254740993ULL), Json(9007199254740992ULL),
                 regression_path))
    throw std::runtime_error("Integer comparator lost precision.");

  const auto fixture_bytes = read_bytes(fixture_path, "retained fixture");
  if (sha256(fixture_bytes) !=
      "A0080BB768A2876AA454C4425C50112191369F2105C545C295377D1BB0814063")
    throw std::runtime_error("Retained fixture fingerprint changed.");
  const auto fixture = Json::parse(fixture_bytes);
  constexpr std::array expected_sources = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Shipbuilding/ShipyardState.cs",
      "Simulation/Shipbuilding/ShipDesignRegistry.cs",
      "Simulation/Species/SpeciesCatalog.cs"};
  if (fixture.at("SchemaVersion") != 1 || fixture.at("RowCount") != 48 ||
      fixture.at("Rows").size() != 48 ||
      fixture.at("SourceFiles").size() != expected_sources.size())
    throw std::runtime_error("Fixture metadata mismatch.");

  std::vector<std::pair<fs::path, std::string>> source_fingerprints;
  for (std::size_t index = 0; index < expected_sources.size(); ++index) {
    const auto &source = fixture.at("SourceFiles")[index];
    if (source.at("Path") != expected_sources[index])
      throw std::runtime_error("Fixture source inventory mismatch.");
    const auto path = source_root / source.at("Path").get<std::string>();
    const auto expected = source.at("Sha256").get<std::string>();
    if (sha256(read_bytes(path, "retained source")) != expected)
      throw std::runtime_error("Retained source fingerprint changed: " +
                               path.string());
    source_fingerprints.emplace_back(path, expected);
  }

  std::size_t native_count = 0;
  std::size_t source_only_count = 0;
  for (const auto &row : fixture.at("Rows")) {
    if (row.at("SourceOnly").get<bool>()) {
      if (row.at("Name") != "restore-null-queue-item-source-only" ||
          row.at("Operation") != "Restore" ||
          row.at("ErrorType") != "InvalidDataException" ||
          row.at("Result") != nullptr ||
          row.at("SourceOnlyReason") !=
              "Source DTO lists can contain null reference elements; native typed DTO vectors contain values only.")
        throw std::runtime_error("Invalid source-only boundary metadata.");
      ++source_only_count;
      continue;
    }

    const auto name = row.at("Name").get<std::string>();
    const auto operation = row.at("Operation").get<std::string>();
    if (operation == "Restore")
      replay_restore(row, name);
    else if (operation == "Capture")
      replay_capture(row, name);
    else
      throw std::runtime_error("Unknown fixture operation: " + operation);
    ++native_count;
  }

  if (native_count != 47 || source_only_count != 1)
    throw std::runtime_error("Fixture row accounting mismatch.");
  for (const auto &[path, expected] : source_fingerprints)
    if (sha256(read_bytes(path, "retained source after replay")) != expected)
      throw std::runtime_error("Retained source changed during replay: " +
                               path.string());

  std::cout << "Shipyard persistence native replay: " << native_count
            << "/47 native rows, " << source_only_count
            << " explicit source-only boundary passed.\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: shipyard_persistence_tests <fixture> <source-root>");
    return replay(fs::absolute(argv[1]), fs::absolute(argv[2]));
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
