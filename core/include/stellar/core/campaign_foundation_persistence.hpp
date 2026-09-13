#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

struct StellarSystemPersistenceDto {
  int id{};
  std::string name;
  float x{};
  float y{};
  StarArchetype archetype{};
  bool has_habitable_world{};
  bool has_anomaly{};
  bool has_rare_resource{};
  bool has_pre_warp_civilization{};
  std::optional<std::string> catalog_preset_id;
  std::optional<StellarClass> stellar_class;
  std::optional<StellarClass> secondary_stellar_class;
  std::optional<StellarClass> tertiary_stellar_class;
  std::optional<double> galactic_depth_light_years;
  std::optional<std::string> stellar_catalog_id;
};

struct CivilizationPersistenceDto {
  // False represents the source's null dictionary and requests a founding
  // roster. True with an empty vector represents an authored empty dictionary.
  bool leadership_present{};
  struct LeadershipEntry {
    std::string office;
    std::optional<CivilizationCharacter> character;
  };
  std::vector<LeadershipEntry> leadership;
  int id{};
  std::string name;
  int home_system_id{};
  CivilizationArchetype archetype{};
  CivilizationTraits traits;
  bool is_player{};
  CivilizationDevelopmentStage development_stage{};
  bool is_seeded_ancient{};
  bool expansion_allowed{true};
  bool neutral_unless_provoked{};
  std::string species_id;
};

class CampaignFoundationPersistenceDataError final : public std::runtime_error {
public:
  explicit CampaignFoundationPersistenceDataError(std::string message);
};

class CampaignFoundationPersistenceArgumentError final
    : public std::runtime_error {
public:
  explicit CampaignFoundationPersistenceArgumentError(std::string message);
};

class CampaignFoundationPersistenceNullArgumentError final
    : public std::runtime_error {
public:
  explicit CampaignFoundationPersistenceNullArgumentError(std::string message);
};

class CampaignFoundationPersistenceRangeError final
    : public std::runtime_error {
public:
  explicit CampaignFoundationPersistenceRangeError(std::string message);
};

class CampaignFoundationPersistenceOperationError final
    : public std::runtime_error {
public:
  explicit CampaignFoundationPersistenceOperationError(std::string message);
};

// Both functions preserve DTO/input order and return detached owned records.
// Restore performs the source stellar validation immediately after
// materializing the complete list; capture performs the same validation before
// copying.
[[nodiscard]] std::vector<StellarSystem>
restore_stellar_systems(std::span<const StellarSystemPersistenceDto> source);
[[nodiscard]] std::vector<StellarSystemPersistenceDto>
capture_stellar_systems(std::span<const StellarSystem> source);

// Historical species and warp overrides match CampaignSaveService. A null
// leadership dictionary creates the exact founding roster; a present dictionary
// is restored in StringComparer.Ordinal (UTF-16 code-unit) key order.
[[nodiscard]] std::vector<Civilization>
restore_civilizations(std::span<const CivilizationPersistenceDto> source,
                      bool legacy_already_warp_capable, int save_format_version,
                      std::int64_t campaign_seed);
[[nodiscard]] std::vector<CivilizationPersistenceDto>
capture_civilizations(std::span<const Civilization> source);

} // namespace stellar::core
