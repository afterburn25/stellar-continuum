#include <stellar/core/galaxy_payload_persistence.hpp>

#include <stellar/core/combat_state.hpp>
#include <stellar/core/galaxy_reference_validation.hpp>
#include <stellar/core/legacy_campaign_recovery.hpp>
#include <stellar/core/planetary_catalog.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace stellar::core {
namespace {

template <typename T>
const std::vector<T> &required(const std::optional<std::vector<T>> &value) {
  if (!value)
    throw GalaxyPayloadPersistenceNullReferenceError(
        "Object reference not set to an instance of an object.");
  return *value;
}

void validate_surface_economies(std::span<const EconomySaveDto> economies,
                                std::span<const Civilization> civilizations) {
  for (const auto &economy : economies)
    if (!std::isfinite(economy.credits) || economy.credits < 0 ||
        !std::isfinite(economy.industry) || economy.industry < 0 ||
        !std::isfinite(economy.science) || economy.science < 0)
      throw GalaxyPayloadPersistenceDataError(
          "Civilization " + std::to_string(economy.civilization_id) +
          " has invalid economy stock; resources must be finite and "
          "nonnegative.");
  std::set<int> ids;
  for (const auto &economy : economies)
    if (!ids.insert(economy.civilization_id).second)
      throw GalaxyPayloadPersistenceDataError(
          "Surface-aware saves require one economy for each civilization.");
  for (const auto &civilization : civilizations)
    if (!ids.contains(civilization.id))
      throw GalaxyPayloadPersistenceDataError(
          "Surface-aware saves require one economy for each civilization.");
}

void validate_references(FreshCampaignState &galaxy) {
  validate_galaxy_references(
      {galaxy.systems, galaxy.bodies, galaxy.civilizations, galaxy.colonies,
       galaxy.economies, galaxy.fleets, galaxy.combat_intelligence,
       galaxy.active_combat_encounter ? &*galaxy.active_combat_encounter
                                      : nullptr});
}

std::optional<GalacticCore>
geometric_core(const std::optional<GalacticCoreMetadata> &core) {
  if (!core)
    return std::nullopt;
  return GalacticCore{{core->x, core->y}, core->exclusion_radius};
}

} // namespace

GalaxyPayloadPersistenceDataError::GalaxyPayloadPersistenceDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

GalaxyPayloadPersistenceDataError::GalaxyPayloadPersistenceDataError(
    std::string message, std::string inner_type, std::string inner_message)
    : std::runtime_error(std::move(message)),
      inner_type_(std::move(inner_type)),
      inner_message_(std::move(inner_message)) {}

const std::optional<std::string> &
GalaxyPayloadPersistenceDataError::inner_type() const noexcept {
  return inner_type_;
}

const std::optional<std::string> &
GalaxyPayloadPersistenceDataError::inner_message() const noexcept {
  return inner_message_;
}

GalaxyPayloadV16Dto
capture_galaxy_payload_v16(FreshCampaignState &galaxy,
                           const GalaxyPayloadCaptureOptions &options) {
  if (galaxy.developer_provenance.has_value() != options.developer_payload)
    throw GalaxyPayloadPersistenceOperationError(
        "Campaign payload provenance does not match the requested save mode.");

  const auto named_core =
      galaxy.galactic_core
          ? galaxy.galactic_core
          : (galaxy.generation_metadata
                 ? galaxy.generation_metadata->galactic_core
                 : std::optional<GalacticCoreMetadata>{});
  if (galaxy.core && !named_core)
    throw GalaxyPayloadPersistenceOperationError(
        "A native geometric core must have an authoritative named galactic-core landmark before capture.");
  if (galaxy.core && named_core) {
    const auto represented = geometric_core(named_core);
    if (!represented || represented->position.x != galaxy.core->position.x ||
        represented->position.y != galaxy.core->position.y ||
        represented->exclusion_radius != galaxy.core->exclusion_radius)
      throw GalaxyPayloadPersistenceOperationError(
          "The native geometric core disagrees with its authoritative named galactic-core landmark.");
  }

  auto systems = capture_stellar_systems(galaxy.systems);
  auto bodies = capture_planetary_bodies(galaxy.bodies, galaxy.systems);
  validate_references(galaxy);
  auto metadata = capture_galaxy_persistence_metadata(
      galaxy.generation_metadata, galaxy.galactic_core, galaxy.seed,
      galaxy.systems);

  GalaxyPayloadV16Dto result;
  result.game_version = options.game_version;
  result.saved_at_utc = options.saved_at_utc;
  result.simulation_days = options.simulation_days;
  result.seed = galaxy.seed;
  result.generation_metadata = std::move(metadata.generation_metadata);
  result.galactic_core = std::move(metadata.galactic_core);
  result.systems = std::move(systems);
  result.planetary_bodies = std::move(bodies);
  result.civilizations = capture_civilizations(galaxy.civilizations);
  result.fleets = capture_fleet_dtos(galaxy.fleets);
  result.colonies = capture_colony_dtos(galaxy.colonies);
  result.economies = capture_economy_dtos(galaxy.economies);
  result.technologies = capture_technology_dtos(galaxy.technologies);
  result.construction_states = capture_construction_dtos(galaxy.construction);
  result.shipyard_states = capture_shipyard_states(galaxy.shipyards);
  result.player_civilization_id = galaxy.player_civilization_id;
  result.knowledge = capture_civilization_knowledge(galaxy.knowledge);
  if (galaxy.active_combat_encounter)
    result.active_combat_encounter =
        clone_campaign_massive_encounter(*galaxy.active_combat_encounter);
  if (!galaxy.combat_intelligence.empty())
    result.combat_intelligence = galaxy.combat_intelligence;
  return result;
}

RestoredGalaxyPayloadV16
restore_galaxy_payload_v16(const GalaxyPayloadV16Dto &payload) {
  if (payload.format_version != GalaxyPayloadV16Dto::current_format_version)
    throw GalaxyPayloadPersistenceDataError(
        "Unsupported native galaxy payload format " +
        std::to_string(payload.format_version) + ".");

  FreshCampaignState galaxy;
  galaxy.seed = payload.seed;
  if (!payload.systems)
    throw GalaxyPayloadPersistenceArgumentNullError(
        "Value cannot be null. (Parameter 'source')");
  galaxy.systems = restore_stellar_systems(*payload.systems);
  galaxy.bodies =
      restore_planetary_bodies(payload.planetary_bodies, galaxy.systems);
  try {
    galaxy.bodies = upgrade_saved_sol_catalog(galaxy.bodies, galaxy.systems);
  } catch (const std::invalid_argument &error) {
    throw GalaxyPayloadPersistenceDataError(
        "The saved canonical Sol catalog cannot be upgraded safely.",
        "InvalidOperationException", error.what());
  }

  const auto &civilization_dtos = required(payload.civilizations);
  if (civilization_dtos.empty()) {
    // The source compatibility overload regenerates the canonical physical
    // catalog solely for homeworld planning; the restored bodies remain the
    // authoritative campaign catalog.
    const auto seeding_bodies =
        generate_planetary_catalog(galaxy.seed, galaxy.systems);
    galaxy.civilizations = seed_civilizations(
        galaxy.systems, seeding_bodies,
        std::min(8, static_cast<int>(galaxy.systems.size())),
        std::min(2, std::max(0, static_cast<int>(galaxy.systems.size()) - 8)),
        galaxy.seed);
    const auto player =
        std::find_if(galaxy.civilizations.begin(), galaxy.civilizations.end(),
                     [](const auto &value) { return value.is_player; });
    if (player == galaxy.civilizations.end())
      throw GalaxyPayloadPersistenceOperationError(
          "Sequence contains no matching element");
    galaxy.player_civilization_id = player->id;
    galaxy.knowledge =
        create_legacy_initial_knowledge(galaxy.systems, galaxy.civilizations);
  } else {
    galaxy.civilizations = restore_civilizations(
        civilization_dtos, false, payload.format_version, galaxy.seed);
    galaxy.player_civilization_id = payload.player_civilization_id;
    galaxy.knowledge = restore_civilization_knowledge(payload.knowledge);
    if (galaxy.knowledge.known_systems(galaxy.player_civilization_id).empty()) {
      const auto player =
          std::find_if(galaxy.civilizations.begin(), galaxy.civilizations.end(),
                       [&](const auto &value) {
                         return value.id == galaxy.player_civilization_id;
                       });
      if (player == galaxy.civilizations.end())
        throw GalaxyPayloadPersistenceOperationError(
            "Sequence contains no matching element");
      galaxy.knowledge.mark_system_fully_surveyed(player->id,
                                                  player->home_system_id);
      if (std::ranges::none_of(galaxy.systems, [&](const auto &system) {
            return system.id == player->home_system_id;
          }))
        throw GalaxyPayloadPersistenceOperationError(
            "Unknown sensor origin system " +
            std::to_string(player->home_system_id) + ".");
      galaxy.knowledge.reveal_within_sensor_range(
          player->id, player->home_system_id, galaxy.systems,
          player->is_seeded_ancient ? 420.0f : 95.0f);
    }
  }

  galaxy.fleets =
      restore_fleet_dtos(required(payload.fleets), galaxy.civilizations,
                         payload.format_version, true);
  if (!payload.colonies || payload.colonies->empty() || !payload.economies ||
      payload.economies->empty())
    throw GalaxyPayloadPersistenceDataError(
        "Surface-aware saves require their authoritative colonies and "
        "economies; they cannot be reseeded.");
  const auto &colony_dtos = *payload.colonies;
  const auto &economy_dtos = *payload.economies;
  validate_surface_economies(economy_dtos, galaxy.civilizations);
  galaxy.colonies = restore_colony_dtos(colony_dtos, galaxy.civilizations,
                                        payload.format_version);
  galaxy.economies = restore_economy_dtos(economy_dtos);

  const auto &technology_dtos = required(payload.technologies);
  galaxy.technologies =
      technology_dtos.empty()
          ? create_migrated_legacy_technology_states(galaxy.civilizations)
          : restore_technology_dtos(technology_dtos);
  const auto &construction_dtos = required(payload.construction_states);
  galaxy.construction =
      construction_dtos.empty()
          ? create_migrated_legacy_construction_states(galaxy.civilizations)
          : restore_construction_dtos(construction_dtos);
  const auto &shipyard_dtos = required(payload.shipyard_states);
  galaxy.shipyards =
      shipyard_dtos.empty()
          ? seed_shipyards(galaxy.civilizations)
          : restore_shipyard_states(shipyard_dtos, galaxy.civilizations,
                                    payload.format_version);
  for (auto &fleet : galaxy.fleets)
    ensure_fleet_combat_state(fleet);

  galaxy.generation_metadata = validate_galaxy_generation_metadata(
      payload.generation_metadata, galaxy.seed, galaxy.systems);
  galaxy.galactic_core =
      validate_galactic_core_metadata(payload.galactic_core, galaxy.systems);
  validate_galactic_core_agreement(
      galaxy.generation_metadata ? galaxy.generation_metadata->galactic_core
                                 : std::optional<GalacticCoreMetadata>{},
      galaxy.galactic_core);
  const auto resolved_core =
      galaxy.galactic_core ? galaxy.galactic_core
                           : (galaxy.generation_metadata
                                  ? galaxy.generation_metadata->galactic_core
                                  : std::nullopt);
  galaxy.galactic_core = resolved_core;
  galaxy.core = geometric_core(resolved_core);
  galaxy.active_combat_encounter = payload.active_combat_encounter;
  galaxy.combat_intelligence = payload.combat_intelligence.value_or(
      std::vector<FleetPowerObservation>{});
  // Capture performs the same maintained catalog validation as the source's
  // final ValidatePlanetaryCatalog call. Its owned projection is discarded.
  (void)capture_planetary_bodies(galaxy.bodies, galaxy.systems);
  validate_references(galaxy);
  return {std::move(galaxy), payload.simulation_days, payload.game_version,
          payload.saved_at_utc};
}

} // namespace stellar::core
