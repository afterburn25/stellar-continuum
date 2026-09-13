#pragma once

#include <stellar/core/galaxy_catalog.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace stellar::core {

struct GalacticCoreMetadata {
  static constexpr const char *stable_landmark_key =
      "galactic-core-smbh-v1";

  std::string landmark_key;
  float x{};
  float y{};
  float exclusion_radius{};

  bool operator==(const GalacticCoreMetadata &) const;
};

struct GalaxyGenerationMetadata {
  std::string entered_seed;
  std::int64_t internal_seed{};
  std::string generator_version;
  // Retained exactly as supplied by the persistence boundary. This bounded DTO
  // neither reads a wall clock nor interprets a timestamp.
  std::string created_at_utc;
  int system_count{};
  std::string galaxy_shape;
  std::string stellar_variety;
  std::string planet_bearing_systems;
  std::string habitable_worlds;
  int guaranteed_nearby_habitable_worlds{};
  int other_civilizations{};
  std::string ancient_civilizations;
  std::string space_hazards;
  std::string starting_development;
  std::string difficulty;
  std::string art_profile_version{"legacy-static-v1"};
  std::optional<std::string> player_species_id;
  std::string anomaly_frequency{"Standard"};
  std::optional<GalacticCoreMetadata> galactic_core;

  bool operator==(const GalaxyGenerationMetadata &) const = default;
};

struct GalaxyPersistenceMetadataCapture {
  std::optional<GalaxyGenerationMetadata> generation_metadata;
  std::optional<GalacticCoreMetadata> galactic_core;
};

std::optional<GalacticCoreMetadata>
validate_galactic_core_metadata(
    const std::optional<GalacticCoreMetadata> &core,
    std::span<const StellarSystem> systems);

std::optional<GalaxyGenerationMetadata>
validate_galaxy_generation_metadata(
    const std::optional<GalaxyGenerationMetadata> &metadata,
    std::int64_t seed, std::span<const StellarSystem> systems);

void validate_galactic_core_agreement(
    const std::optional<GalacticCoreMetadata> &metadata_core,
    const std::optional<GalacticCoreMetadata> &state_core);

GalaxyPersistenceMetadataCapture capture_galaxy_persistence_metadata(
    const std::optional<GalaxyGenerationMetadata> &metadata,
    const std::optional<GalacticCoreMetadata> &core, std::int64_t seed,
    std::span<const StellarSystem> systems);

} // namespace stellar::core
