#include <stellar/core/legacy_galaxy_payload_persistence.hpp>

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

bool supported(int version) noexcept {
  return (version >= 1 && version <= 8) || version == 10 || version == 12;
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

std::optional<GalacticCore>
geometric_core(const std::optional<GalacticCoreMetadata> &core) {
  if (!core)
    return std::nullopt;
  return GalacticCore{{core->x, core->y}, core->exclusion_radius};
}

void validate_references(FreshCampaignState &galaxy) {
  validate_galaxy_references(
      {galaxy.systems, galaxy.bodies, galaxy.civilizations, galaxy.colonies,
       galaxy.economies, galaxy.fleets, galaxy.combat_intelligence,
       galaxy.active_combat_encounter ? &*galaxy.active_combat_encounter
                                      : nullptr});
}

} // namespace

RestoredLegacyGalaxyPayload
restore_legacy_galaxy_payload(const LegacyGalaxyPayloadDto &input) {
  const auto &payload = input.galaxy;
  if (!supported(payload.format_version))
    throw GalaxyPayloadPersistenceDataError(
        "Unsupported save format " + std::to_string(payload.format_version) +
        "; maximum supported is 16.");

  FreshCampaignState galaxy;
  galaxy.seed = payload.seed;
  if (!payload.systems)
    throw GalaxyPayloadPersistenceArgumentNullError(
        "Value cannot be null. (Parameter 'source')");
  galaxy.systems = restore_stellar_systems(*payload.systems);
  galaxy.bodies = generate_planetary_catalog(galaxy.seed, galaxy.systems);
  try {
    galaxy.bodies = upgrade_saved_sol_catalog(galaxy.bodies, galaxy.systems);
  } catch (const std::invalid_argument &error) {
    throw GalaxyPayloadPersistenceDataError(
        "The saved canonical Sol catalog cannot be upgraded safely.",
        "InvalidOperationException", error.what());
  }

  const bool seed_civilizations_for_legacy =
      payload.format_version == 1 ||
      required(payload.civilizations).empty();
  if (seed_civilizations_for_legacy) {
    // The source no-body overload generates its own un-upgraded catalog for
    // homeworld planning. The already upgraded bodies above remain the
    // authoritative restored world catalog.
    const auto seeding_bodies =
        generate_planetary_catalog(galaxy.seed, galaxy.systems);
    galaxy.civilizations = seed_civilizations(
        galaxy.systems, seeding_bodies,
        std::min(8, static_cast<int>(galaxy.systems.size())),
        std::min(2, std::max(0, static_cast<int>(galaxy.systems.size()) - 8)),
        galaxy.seed);
    const auto player = std::ranges::find_if(
        galaxy.civilizations, [](const auto &value) { return value.is_player; });
    if (player == galaxy.civilizations.end())
      throw GalaxyPayloadPersistenceOperationError(
          "Sequence contains no matching element");
    galaxy.player_civilization_id = player->id;
    galaxy.knowledge =
        create_legacy_initial_knowledge(galaxy.systems, galaxy.civilizations);
  } else {
    galaxy.civilizations = restore_civilizations(
        *payload.civilizations, payload.format_version < 5,
        payload.format_version, galaxy.seed);
    galaxy.player_civilization_id = payload.player_civilization_id;
    galaxy.knowledge = restore_civilization_knowledge(payload.knowledge);
    if (galaxy.knowledge.known_systems(galaxy.player_civilization_id).empty()) {
      const auto player = std::ranges::find_if(
          galaxy.civilizations, [&](const auto &value) {
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
          player->is_seeded_ancient ? 420.0F : 95.0F);
    }
  }

  galaxy.fleets = payload.format_version < 3
      ? seed_fleets(galaxy.systems, galaxy.civilizations)
      : restore_fleet_dtos(required(payload.fleets), galaxy.civilizations,
                           payload.format_version,
                           payload.format_version >= 7);

  if (payload.format_version >= 12) {
    if (!payload.colonies || payload.colonies->empty() || !payload.economies ||
        payload.economies->empty())
      throw GalaxyPayloadPersistenceDataError(
          "Surface-aware saves require their authoritative colonies and "
          "economies; they cannot be reseeded.");
    validate_surface_economies(*payload.economies, galaxy.civilizations);
  }

  if (payload.format_version < 4 || required(payload.colonies).empty() ||
      required(payload.economies).empty()) {
    galaxy.colonies = seed_legacy_colonies(galaxy.civilizations);
    galaxy.economies = seed_economies(galaxy.civilizations);
  } else {
    galaxy.colonies = restore_colony_dtos(*payload.colonies,
                                          galaxy.civilizations,
                                          payload.format_version);
    galaxy.economies = restore_economy_dtos(*payload.economies);
  }

  if (payload.format_version < 7)
    ensure_legacy_expansion_fleets(galaxy.fleets, galaxy.systems,
                                   galaxy.civilizations, galaxy.colonies);

  galaxy.technologies = payload.format_version < 5 ||
                                required(payload.technologies).empty()
      ? create_migrated_legacy_technology_states(galaxy.civilizations)
      : restore_technology_dtos(*payload.technologies);
  galaxy.construction = payload.format_version < 6 ||
                                required(payload.construction_states).empty()
      ? create_migrated_legacy_construction_states(galaxy.civilizations)
      : restore_construction_dtos(*payload.construction_states);
  galaxy.shipyards = payload.format_version < 7 ||
                             required(payload.shipyard_states).empty()
      ? seed_shipyards(galaxy.civilizations)
      : restore_shipyard_states(*payload.shipyard_states,
                                galaxy.civilizations,
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
  const auto resolved_core = galaxy.galactic_core
      ? galaxy.galactic_core
      : (galaxy.generation_metadata
             ? galaxy.generation_metadata->galactic_core
             : std::optional<GalacticCoreMetadata>{});
  galaxy.galactic_core = resolved_core;
  galaxy.core = geometric_core(resolved_core);
  galaxy.active_combat_encounter = payload.active_combat_encounter;
  galaxy.combat_intelligence = payload.combat_intelligence.value_or(
      std::vector<FleetPowerObservation>{});
  (void)capture_planetary_bodies(galaxy.bodies, galaxy.systems);
  validate_references(galaxy);

  const double simulation_days = payload.format_version >= 5
      ? payload.simulation_days
      : input.simulation_seconds;
  return {std::move(galaxy), simulation_days, payload.game_version,
          payload.saved_at_utc};
}

} // namespace stellar::core
