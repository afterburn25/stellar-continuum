#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_generation_metadata.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using Json = nlohmann::json;
using namespace stellar::core;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

float number(const Json &value) {
  if (value.is_number())
    return value.get<float>();
  const auto text = value.get<std::string>();
  if (text == "Infinity")
    return std::numeric_limits<float>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<float>::infinity();
  if (text == "NaN")
    return std::numeric_limits<float>::quiet_NaN();
  throw std::runtime_error("unknown named floating-point value");
}

std::optional<GalacticCoreMetadata> decode_core(const Json &value) {
  if (value.is_null())
    return std::nullopt;
  return GalacticCoreMetadata{value.at("LandmarkKey").get<std::string>(),
                              number(value.at("X")), number(value.at("Y")),
                              number(value.at("ExclusionRadius"))};
}

std::optional<GalaxyGenerationMetadata> decode_metadata(const Json &value) {
  if (value.is_null())
    return std::nullopt;
  GalaxyGenerationMetadata result;
  result.entered_seed = value.at("EnteredSeed").get<std::string>();
  result.internal_seed = value.at("InternalSeed").get<std::int64_t>();
  result.generator_version = value.at("GeneratorVersion").get<std::string>();
  result.created_at_utc = value.at("CreatedAtUtc").get<std::string>();
  result.system_count = value.at("SystemCount").get<int>();
  result.galaxy_shape = value.at("GalaxyShape").get<std::string>();
  result.stellar_variety = value.at("StellarVariety").get<std::string>();
  result.planet_bearing_systems =
      value.at("PlanetBearingSystems").get<std::string>();
  result.habitable_worlds = value.at("HabitableWorlds").get<std::string>();
  result.guaranteed_nearby_habitable_worlds =
      value.at("GuaranteedNearbyHabitableWorlds").get<int>();
  result.other_civilizations = value.at("OtherCivilizations").get<int>();
  result.ancient_civilizations =
      value.at("AncientCivilizations").get<std::string>();
  result.space_hazards = value.at("SpaceHazards").get<std::string>();
  result.starting_development =
      value.at("StartingDevelopment").get<std::string>();
  result.difficulty = value.at("Difficulty").get<std::string>();
  result.art_profile_version =
      value.at("ArtProfileVersion").get<std::string>();
  if (!value.at("PlayerSpeciesId").is_null())
    result.player_species_id =
        value.at("PlayerSpeciesId").get<std::string>();
  result.anomaly_frequency = value.at("AnomalyFrequency").get<std::string>();
  result.galactic_core = decode_core(value.at("GalacticCore"));
  return result;
}

Json encode_core(const std::optional<GalacticCoreMetadata> &value) {
  if (!value)
    return nullptr;
  return {{"LandmarkKey", value->landmark_key},
          {"X", value->x},
          {"Y", value->y},
          {"ExclusionRadius", value->exclusion_radius}};
}

Json encode_metadata(const std::optional<GalaxyGenerationMetadata> &value) {
  if (!value)
    return nullptr;
  return {{"EnteredSeed", value->entered_seed},
          {"InternalSeed", value->internal_seed},
          {"GeneratorVersion", value->generator_version},
          {"CreatedAtUtc", value->created_at_utc},
          {"SystemCount", value->system_count},
          {"GalaxyShape", value->galaxy_shape},
          {"StellarVariety", value->stellar_variety},
          {"PlanetBearingSystems", value->planet_bearing_systems},
          {"HabitableWorlds", value->habitable_worlds},
          {"GuaranteedNearbyHabitableWorlds",
           value->guaranteed_nearby_habitable_worlds},
          {"OtherCivilizations", value->other_civilizations},
          {"AncientCivilizations", value->ancient_civilizations},
          {"SpaceHazards", value->space_hazards},
          {"StartingDevelopment", value->starting_development},
          {"Difficulty", value->difficulty},
          {"ArtProfileVersion", value->art_profile_version},
          {"PlayerSpeciesId",
           value->player_species_id ? Json(*value->player_species_id)
                                    : Json(nullptr)},
          {"AnomalyFrequency", value->anomaly_frequency},
          {"GalacticCore", encode_core(value->galactic_core)}};
}

std::vector<StellarSystem> decode_systems(const Json &value) {
  std::vector<StellarSystem> result;
  for (const auto &item : value) {
    StellarSystem system;
    system.id = item.at("Id").get<int>();
    system.position = {number(item.at("X")), number(item.at("Y"))};
    result.push_back(std::move(system));
  }
  return result;
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open " + path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string sha256_hex(const std::string &bytes) {
  const auto digest = detail::adaptive_research_sha256(
      std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                bytes.size()));
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(digest.size() * 2);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0x0F]);
  }
  return result;
}

std::string sha256_file_hex(const std::filesystem::path &path) {
  return sha256_hex(read_bytes(path));
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3,
            "Expected source root and actual-source fixture path.");
    const auto source_root = std::filesystem::absolute(argv[1]);
    const auto fixture_path = std::filesystem::absolute(argv[2]);
    const auto fixture_bytes = read_bytes(fixture_path);
    require(sha256_hex(fixture_bytes) ==
                "D411FB95DA948B43C64096D391070D042488AD57F3978473B1529FDACA7DCFB9",
            "actual-source fixture SHA-256 mismatch");
    const auto root = Json::parse(fixture_bytes);
    require(root.at("Schema") == "stellar-galaxy-metadata-persistence-v1",
            "fixture schema mismatch");
    require(root.at("RowCount").get<std::size_t>() == root.at("Rows").size(),
            "fixture row count mismatch");
    const FreshCampaignState default_campaign{};
    require(!default_campaign.generation_metadata &&
                !default_campaign.galactic_core,
            "appended persistence metadata must default absent");
    for (const auto &[relative, expected] :
         root.at("SourceHashes").items()) {
      require(sha256_file_hex(source_root / relative) ==
                  expected.get<std::string>(),
              "source hash mismatch: " + relative);
    }

    std::size_t checked = 0;
    for (const auto &row : root.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto operation = row.at("Operation").get<std::string>();
      require(operation == "Metadata" || operation == "Core" ||
                  operation == "Capture" || operation == "Agreement",
              name + ": unknown fixture operation");
      const auto &input = row.at("Input");
      const auto seed = input.at("Seed").get<std::int64_t>();
      auto systems = decode_systems(input.at("Systems"));
      auto metadata = decode_metadata(input.at("Metadata"));
      auto core = decode_core(input.at("StateCore"));
      const auto metadata_before = metadata;
      const auto core_before = core;
      Json result = nullptr;
      std::optional<std::string> error;
      std::optional<GalaxyGenerationMetadata> metadata_result;
      std::optional<GalacticCoreMetadata> core_result;
      std::optional<GalaxyPersistenceMetadataCapture> capture_result;
      try {
        if (operation == "Metadata") {
          metadata_result =
              validate_galaxy_generation_metadata(metadata, seed, systems);
        } else if (operation == "Core") {
          core_result = validate_galactic_core_metadata(core, systems);
        } else if (operation == "Capture") {
          capture_result =
              capture_galaxy_persistence_metadata(metadata, core, seed, systems);
        } else if (operation == "Agreement") {
          validate_galactic_core_agreement(
              metadata ? metadata->galactic_core : std::nullopt, core);
        } else {
          throw std::runtime_error("unknown operation: " + operation);
        }
      } catch (const std::exception &exception) {
        error = exception.what();
      }

      if (!error) {
        if (operation == "Metadata") result = encode_metadata(metadata_result);
        else if (operation == "Core") result = encode_core(core_result);
        else if (operation == "Capture") {
          if (name == "actual-detached-capture") {
            if (metadata) {
              metadata->entered_seed = "mutated after capture";
              if (metadata->galactic_core)
                metadata->galactic_core->landmark_key = "nested mutation";
            }
            if (core) core->landmark_key = "state mutation";
          }
          result = {{"GenerationMetadata",
                     encode_metadata(capture_result->generation_metadata)},
                    {"GalacticCore",
                     encode_core(capture_result->galactic_core)}};
        }
      }
      if (name != "actual-detached-capture") {
        require(metadata == metadata_before, name + ": metadata input mutated");
        require(core == core_before, name + ": core input mutated");
      }

      if (row.at("Error").is_null()) {
        require(!error, name + ": unexpected error: " +
                            (error ? *error : std::string{}));
        Json expected;
        if (operation == "Metadata") {
          expected = encode_metadata(decode_metadata(row.at("Result")));
        } else if (operation == "Core") {
          expected = encode_core(decode_core(row.at("Result")));
        } else if (operation == "Capture") {
          expected = {
              {"GenerationMetadata", encode_metadata(decode_metadata(
                                         row.at("Result").at("GenerationMetadata")))},
              {"GalacticCore", encode_core(decode_core(
                                   row.at("Result").at("GalacticCore")))}};
        } else expected = nullptr;
        require(result == expected, name + ": result mismatch");
      } else {
        require(error.has_value(), name + ": expected an error");
        require(*error == row.at("Error").at("Message").get<std::string>(),
                name + ": error mismatch");
        require(row.at("Error").at("Type") == "InvalidDataException",
                name + ": unexpected source error category");
      }
      ++checked;
    }
    for (const auto &[relative, expected] :
         root.at("SourceHashes").items()) {
      require(sha256_file_hex(source_root / relative) ==
                  expected.get<std::string>(),
              "source hash changed during replay: " + relative);
    }
    require(checked == 22, "not every actual-source row was replayed");
    std::cout << "galaxy metadata persistence parity: " << checked
              << " actual-source rows passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "galaxy metadata persistence parity failure: "
              << exception.what() << "\n"
              << "cwd: " << std::filesystem::current_path().string() << "\n";
    if (argc >= 2)
      std::cerr << "source root: " << std::filesystem::absolute(argv[1]).string()
                << "\n";
    if (argc >= 3)
      std::cerr << "fixture: " << std::filesystem::absolute(argv[2]).string()
                << "\n";
    return 1;
  }
}
