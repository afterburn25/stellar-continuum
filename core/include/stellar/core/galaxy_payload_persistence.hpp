#pragma once

#include <stellar/core/campaign_foundation_persistence.hpp>
#include <stellar/core/campaign_payload_provenance.hpp>
#include <stellar/core/fleet_persistence.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_economy_persistence.hpp>
#include <stellar/core/knowledge_persistence.hpp>
#include <stellar/core/massive_combat_persistence.hpp>
#include <stellar/core/planetary_body_persistence.hpp>
#include <stellar/core/shipyard_persistence.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

class GalaxyPayloadPersistenceDataError final : public std::runtime_error {
public:
  explicit GalaxyPayloadPersistenceDataError(std::string message);
  GalaxyPayloadPersistenceDataError(std::string message, std::string inner_type,
                                    std::string inner_message);
  [[nodiscard]] const std::optional<std::string> &inner_type() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  inner_message() const noexcept;

private:
  std::optional<std::string> inner_type_;
  std::optional<std::string> inner_message_;
};

class GalaxyPayloadPersistenceOperationError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class GalaxyPayloadPersistenceNullReferenceError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class GalaxyPayloadPersistenceArgumentNullError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct GalaxyPayloadV16Dto {
  static constexpr int current_format_version = 16;

  int format_version{current_format_version};
  std::string game_version;
  // Opaque DateTimeOffset text owned by the future JSON/player wrapper.
  std::string saved_at_utc;
  double simulation_days{};

  std::int64_t seed{};
  std::optional<GalaxyGenerationMetadata> generation_metadata;
  std::optional<GalacticCoreMetadata> galactic_core;
  std::optional<std::vector<StellarSystemPersistenceDto>> systems;
  PlanetaryBodyPersistenceInput planetary_bodies;
  std::optional<std::vector<CivilizationPersistenceDto>> civilizations;
  std::optional<std::vector<FleetSaveDto>> fleets;
  std::optional<std::vector<ColonySaveDto>> colonies;
  std::optional<std::vector<EconomySaveDto>> economies;
  std::optional<std::vector<TechnologySaveDto>> technologies;
  std::optional<std::vector<ConstructionSaveDto>> construction_states;
  std::optional<std::vector<ShipyardPersistenceDto>> shipyard_states;
  int player_civilization_id{};
  CivilizationKnowledgePersistenceInput knowledge;
  std::optional<CampaignMassiveEncounter> active_combat_encounter;
  std::optional<std::vector<FleetPowerObservation>> combat_intelligence;
};

struct GalaxyPayloadCaptureOptions {
  double simulation_days{};
  std::string game_version;
  std::string saved_at_utc;
  bool developer_payload{};
};

struct RestoredGalaxyPayloadV16 {
  FreshCampaignState galaxy;
  double simulation_days{};
  std::string game_version;
  std::string saved_at_utc;
};

// The world is mutable because source validation can materialize legacy
// encounter formation counts and fleet capture normalizes combat state before a
// later validation error. The returned payload owns every nested value.
[[nodiscard]] GalaxyPayloadV16Dto
capture_galaxy_payload_v16(FreshCampaignState &galaxy,
                           const GalaxyPayloadCaptureOptions &options);

// Restores only exact galaxy format 16. Historical formats and wrapper/player
// envelopes are separate gates. The input remains owned and unchanged.
[[nodiscard]] RestoredGalaxyPayloadV16
restore_galaxy_payload_v16(const GalaxyPayloadV16Dto &payload);

} // namespace stellar::core
