#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>

#include "galaxy_payload_test_helpers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

void check(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

std::string bytes(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(bool(stream), "Cannot open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(stream), {}};
}

std::string sha256(std::string_view value) {
  const auto digest = detail::adaptive_research_sha256(std::span(
      reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(64);
  for (const auto byte : digest) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}

FreshCampaignState baseline_state(const fs::path &catalog_path) {
  constexpr std::int64_t seed = 871603;
  const auto catalog = load_nearby_catalog(catalog_path);
  auto physical = generate_stellar_catalog(seed, 250, catalog);
  auto founding = create_founding_catalog(seed, physical, 8, 0);

  FreshCampaignState result;
  result.seed = seed;
  result.systems = std::move(founding.systems);
  result.bodies = std::move(founding.bodies);
  result.civilizations = std::move(founding.civilizations);
  result.used_constrained_home_fallback =
      founding.used_constrained_home_fallback;
  result.colonies = seed_colonies(result.civilizations, result.bodies);
  result.fleets =
      seed_fleets(result.systems, result.civilizations, result.colonies);
  result.economies = seed_economies(result.civilizations);
  result.technologies = seed_legacy_technologies(result.civilizations);
  result.construction = seed_construction(result.civilizations);
  result.shipyards = seed_shipyards(result.civilizations);
  for (const auto &civilization : result.civilizations) {
    result.knowledge.mark_system_fully_surveyed(
        civilization.id, civilization.home_system_id);
    result.knowledge.reveal_within_sensor_range(
        civilization.id, civilization.home_system_id, result.systems,
        civilization.is_seeded_ancient ? 25.0F : 8.0F);
  }
  const auto player = std::ranges::find_if(
      result.civilizations, [](const auto &value) { return value.is_player; });
  check(player != result.civilizations.end(), "baseline player");
  result.player_civilization_id = player->id;
  return result;
}

struct InputFingerprint {
  std::size_t systems{}, bodies{}, civilizations{}, fleets{}, colonies{};
  std::size_t economies{}, technologies{}, construction{}, shipyards{};
  std::size_t knowledge{};
  std::int64_t seed{};
  int player{};
  bool metadata{}, core{}, encounter{}, intelligence{};
  std::string first_system;
  std::string last_system;
  std::string first_civilization;

  bool operator==(const InputFingerprint &) const = default;
};

InputFingerprint fingerprint(const GalaxyPayloadV16Dto &value) {
  const auto count = [](const auto &optional) {
    return optional ? optional->size() : std::size_t(-1);
  };
  InputFingerprint result{
      count(value.systems),
      value.planetary_bodies.bodies.size(),
      count(value.civilizations),
      count(value.fleets),
      count(value.colonies),
      count(value.economies),
      count(value.technologies),
      count(value.construction_states),
      count(value.shipyard_states),
      value.knowledge.entries.size(),
      value.seed,
      value.player_civilization_id,
      value.generation_metadata.has_value(),
      value.galactic_core.has_value(),
      value.active_combat_encounter.has_value(),
      value.combat_intelligence.has_value()};
  if (value.systems && !value.systems->empty()) {
    result.first_system = value.systems->front().name;
    result.last_system = value.systems->back().name;
  }
  if (value.civilizations && !value.civilizations->empty())
    result.first_civilization = value.civilizations->front().name;
  return result;
}

struct Error {
  std::string type;
  std::string message;
  std::optional<std::string> inner_type;
  std::optional<std::string> inner_message;
};

Error classify(std::exception_ptr failure) {
  try {
    std::rethrow_exception(failure);
  } catch (const GalaxyPayloadPersistenceDataError &error) {
    return {"InvalidDataException", error.what(), error.inner_type(),
            error.inner_message()};
  } catch (const GalaxyPayloadPersistenceOperationError &error) {
    return {"InvalidOperationException", error.what(), {}, {}};
  } catch (const GalaxyPayloadPersistenceNullReferenceError &error) {
    return {"NullReferenceException", error.what(), {}, {}};
  } catch (const GalaxyPayloadPersistenceArgumentNullError &error) {
    return {"ArgumentNullException", error.what(), {}, {}};
  } catch (const GalaxyEconomyPersistenceDataError &error) {
    return {"InvalidDataException", error.what(), {}, {}};
  } catch (const std::exception &error) {
    return {"UnexpectedNativeException", error.what(), {}, {}};
  }
}

template <class T, class Decode>
std::optional<std::vector<T>> decode_optional_array(const Json &value,
                                                    Decode decode) {
  if (value.is_null())
    return std::nullopt;
  std::vector<T> result;
  result.reserve(value.size());
  for (const auto &item : value)
    result.push_back(decode(item));
  return result;
}

FleetSaveDto decode_fleet(const Json &value) {
  using gate087_fleet_state::number;
  using gate087_fleet_state::optional_value;
  FleetSaveDto result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.name = value.at("Name");
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional_value<std::string>(value.at("DesignId"));
  result.x = static_cast<float>(number(value.at("X")));
  result.y = static_cast<float>(number(value.at("Y")));
  result.current_system_id = optional_value<int>(value.at("CurrentSystemId"));
  result.destination_system_id =
      optional_value<int>(value.at("DestinationSystemId"));
  result.transit_phase =
      static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  result.transit_origin_system_id =
      optional_value<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id =
      optional_value<int>(value.at("TransitTargetSystemId"));
  result.transit_progress = number(value.at("TransitProgress"));
  result.local_transit_start_x =
      static_cast<float>(number(value.at("LocalTransitStartX")));
  result.local_transit_start_y =
      static_cast<float>(number(value.at("LocalTransitStartY")));
  result.local_transit_position_x =
      static_cast<float>(number(value.at("LocalTransitPositionX")));
  result.local_transit_position_y =
      static_cast<float>(number(value.at("LocalTransitPositionY")));
  result.local_transit_target_x =
      static_cast<float>(number(value.at("LocalTransitTargetX")));
  result.local_transit_target_y =
      static_cast<float>(number(value.at("LocalTransitTargetY")));
  if (!value.at("PlannedRouteSystemIds").is_null())
    result.planned_route_system_ids =
        value.at("PlannedRouteSystemIds").get<std::vector<int>>();
  result.hold_requested = value.at("HoldRequested");
  result.return_to_base_requested = value.at("ReturnToBaseRequested");
  result.return_to_base_failure_reason =
      optional_value<std::string>(value.at("ReturnToBaseFailureReason"));
  result.mission_order_revision = value.at("MissionOrderRevision");
  result.destination_planetary_body_id =
      optional_value<int>(value.at("DestinationPlanetaryBodyId"));
  result.prevent_automatic_settlement = value.at("PreventAutomaticSettlement");
  result.settlement_body_id = optional_value<int>(value.at("SettlementBodyId"));
  result.settlement_days_completed = number(value.at("SettlementDaysCompleted"));
  result.reconnaissance_system_id =
      optional_value<int>(value.at("ReconnaissanceSystemId"));
  result.reconnaissance_days_completed =
      number(value.at("ReconnaissanceDaysCompleted"));
  result.freight_target_outpost_id =
      optional_value<int>(value.at("FreightTargetOutpostId"));
  result.freight_home_colony_id =
      optional_value<int>(value.at("FreightHomeColonyId"));
  result.cargo_material_capacity = number(value.at("CargoMaterialCapacity"));
  result.cargo_materials = number(value.at("CargoMaterials"));
  result.strategic_speed = number(value.at("StrategicSpeed"));
  result.maximum_leg_range_light_years =
      number(value.at("MaximumLegRangeLightYears"));
  result.fuel_capacity_light_years = number(value.at("FuelCapacityLightYears"));
  if (!value.at("FuelRemainingLightYears").is_null())
    result.fuel_remaining_light_years =
        number(value.at("FuelRemainingLightYears"));
  result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  result.is_active = value.at("IsActive");
  if (!value.at("EmbarkedPopulationMillions").is_null())
    result.embarked_population_millions =
        number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id =
      optional_value<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  if (!value.at("Combat").is_null()) {
    const auto combat = gate087_fleet_state::parse_combat(value.at("Combat"));
    result.combat = FleetCombatSaveDto{
        combat.profile_id,
        combat.shields,
        combat.armor,
        combat.hull,
        combat.weapon_cooldown_remaining_days,
        combat.order,
        combat.target_fleet_id,
        combat.defend_system_id,
        combat.retreat_progress_days,
        combat.retreat_started,
        combat.is_disengaged,
        combat.disengaged_system_id};
  }
  if (!value.at("TacticalLoadout").is_null())
    result.tactical_loadout =
        gate087_fleet_state::parse_loadout(value.at("TacticalLoadout"));
  if (!value.at("TacticalVessel").is_null())
    result.tactical_vessel =
        gate087_fleet_state::parse_vessel(value.at("TacticalVessel"));
  return result;
}

Json normalized_body_json(const Json &value) {
  auto result = value;
  if (!result.contains("OrbitalEccentricity"))
    result["OrbitalEccentricity"] = 0.0;
  if (!result.contains("OrbitalInclinationDegrees"))
    result["OrbitalInclinationDegrees"] = 0.0;
  return result;
}

Json normalized_fleet_json(const Json &value) {
  auto result = value;
  if (!result.contains("TacticalLoadout"))
    result["TacticalLoadout"] = nullptr;
  if (!result.contains("TacticalVessel"))
    result["TacticalVessel"] = nullptr;
  return result;
}

Json fleet_state_json(const Json &value) {
  auto result = normalized_fleet_json(value);
  result["Position"] = {{"X", result.at("X")}, {"Y", result.at("Y")}};
  result["LocalTransitStart"] = {
      {"X", result.at("LocalTransitStartX")},
      {"Y", result.at("LocalTransitStartY")}};
  result["LocalTransitPosition"] = {
      {"X", result.at("LocalTransitPositionX")},
      {"Y", result.at("LocalTransitPositionY")}};
  result["LocalTransitTarget"] = {
      {"X", result.at("LocalTransitTargetX")},
      {"Y", result.at("LocalTransitTargetY")}};
  return result;
}

GalaxyPayloadV16Dto decode_payload(const Json &value) {
  const auto &galaxy = value.at("Galaxy");
  GalaxyPayloadV16Dto result;
  result.format_version = value.at("FormatVersion");
  result.game_version = value.at("GameVersion");
  result.saved_at_utc = value.at("SavedAtUtc");
  result.simulation_days = value.at("SimulationDays");
  result.seed = galaxy.at("Seed");
  result.generation_metadata =
      gate087_metadata::decode_metadata(galaxy.at("GenerationMetadata"));
  result.galactic_core = gate087_metadata::decode_core(galaxy.at("GalacticCore"));
  result.systems = decode_optional_array<StellarSystemPersistenceDto>(
      galaxy.at("Systems"), gate087_foundation::system_dto);
  result.planetary_bodies.bodies_present =
      !galaxy.at("PlanetaryBodies").is_null();
  if (result.planetary_bodies.bodies_present)
    for (const auto &item : galaxy.at("PlanetaryBodies"))
      result.planetary_bodies.bodies.push_back(
          item.is_null()
              ? std::nullopt
              : std::optional{gate087_planetary::body_dto(
                    normalized_body_json(item))});
  result.civilizations = decode_optional_array<CivilizationPersistenceDto>(
      galaxy.at("Civilizations"), gate087_foundation::civilization_dto);
  result.fleets = decode_optional_array<FleetSaveDto>(
      galaxy.at("Fleets"), [](const Json &item) {
        return decode_fleet(normalized_fleet_json(item));
      });
  result.colonies = decode_optional_array<ColonySaveDto>(
      galaxy.at("Colonies"), gate087_economy::colony_dto);
  result.economies = decode_optional_array<EconomySaveDto>(
      galaxy.at("Economies"), gate087_economy::economy_dto);
  result.technologies = decode_optional_array<TechnologySaveDto>(
      galaxy.at("Technologies"), gate087_economy::technology_dto);
  result.construction_states = decode_optional_array<ConstructionSaveDto>(
      galaxy.at("ConstructionStates"), gate087_economy::construction_dto);
  result.shipyard_states = decode_optional_array<ShipyardPersistenceDto>(
      galaxy.at("ShipyardStates"), gate087_shipyard::decode_dto);
  result.player_civilization_id = galaxy.at("PlayerCivilizationId");
  result.knowledge.entries_present = !galaxy.at("Knowledge").is_null();
  if (result.knowledge.entries_present)
    for (const auto &item : galaxy.at("Knowledge"))
      result.knowledge.entries.push_back(
          item.is_null() ? std::nullopt
                         : std::optional{gate087_knowledge::entry(item)});
  if (const auto found = galaxy.find("ActiveCombatEncounter");
      found != galaxy.end() && !found->is_null())
    result.active_combat_encounter = gate087_massive::encounter(*found);
  if (const auto found = galaxy.find("CombatIntelligence");
      found != galaxy.end() && !found->is_null()) {
    result.combat_intelligence.emplace();
    for (const auto &item : *found)
      result.combat_intelligence->push_back(
          gate087_intelligence::observation(item));
  }
  return result;
}

void check_fleet_dto(const FleetSaveDto &actual, const Json &expected,
                     const std::string &label) {
  const auto normalized = normalized_fleet_json(expected);
  const auto expected_dto = decode_fleet(normalized);
  check(actual.id == expected_dto.id &&
            actual.civilization_id == expected_dto.civilization_id &&
            actual.name == expected_dto.name && actual.role == expected_dto.role &&
            actual.design_id == expected_dto.design_id,
        label + ": identity");
  check(actual.current_system_id == expected_dto.current_system_id &&
            actual.destination_system_id == expected_dto.destination_system_id &&
            actual.transit_phase == expected_dto.transit_phase &&
            actual.transit_origin_system_id ==
                expected_dto.transit_origin_system_id &&
            actual.transit_target_system_id ==
                expected_dto.transit_target_system_id &&
            actual.planned_route_system_ids ==
                expected_dto.planned_route_system_ids,
        label + ": transit identity");
  const auto same_number = [](double left, double right) {
    return (std::isnan(left) && std::isnan(right)) || left == right;
  };
#define CHECK_NUMBER(member)                                                    \
  check(same_number(actual.member, expected_dto.member),                       \
        label + ": " #member)
  CHECK_NUMBER(x);
  CHECK_NUMBER(y);
  CHECK_NUMBER(transit_progress);
  CHECK_NUMBER(local_transit_start_x);
  CHECK_NUMBER(local_transit_start_y);
  CHECK_NUMBER(local_transit_position_x);
  CHECK_NUMBER(local_transit_position_y);
  CHECK_NUMBER(local_transit_target_x);
  CHECK_NUMBER(local_transit_target_y);
  CHECK_NUMBER(settlement_days_completed);
  CHECK_NUMBER(reconnaissance_days_completed);
  CHECK_NUMBER(cargo_material_capacity);
  CHECK_NUMBER(cargo_materials);
  CHECK_NUMBER(strategic_speed);
  CHECK_NUMBER(maximum_leg_range_light_years);
  CHECK_NUMBER(fuel_capacity_light_years);
  CHECK_NUMBER(sensor_range);
#undef CHECK_NUMBER
  check(actual.hold_requested == expected_dto.hold_requested &&
            actual.return_to_base_requested ==
                expected_dto.return_to_base_requested &&
            actual.return_to_base_failure_reason ==
                expected_dto.return_to_base_failure_reason &&
            actual.mission_order_revision ==
                expected_dto.mission_order_revision &&
            actual.destination_planetary_body_id ==
                expected_dto.destination_planetary_body_id &&
            actual.prevent_automatic_settlement ==
                expected_dto.prevent_automatic_settlement &&
            actual.settlement_body_id == expected_dto.settlement_body_id &&
            actual.reconnaissance_system_id ==
                expected_dto.reconnaissance_system_id &&
            actual.freight_target_outpost_id ==
                expected_dto.freight_target_outpost_id &&
            actual.freight_home_colony_id ==
                expected_dto.freight_home_colony_id &&
            actual.fuel_remaining_light_years ==
                expected_dto.fuel_remaining_light_years &&
            actual.is_active == expected_dto.is_active &&
            actual.embarked_population_millions ==
                expected_dto.embarked_population_millions &&
            actual.embarked_population_species_id ==
                expected_dto.embarked_population_species_id,
        label + ": optional/flag fields");
  check(actual.combat.has_value() == expected_dto.combat.has_value() &&
            actual.tactical_loadout.has_value() ==
                expected_dto.tactical_loadout.has_value() &&
            actual.tactical_vessel.has_value() ==
                expected_dto.tactical_vessel.has_value(),
        label + ": combat presence");
  if (actual.combat) {
    FleetCombatState state{actual.combat->profile_id,
                           actual.combat->shields,
                           actual.combat->armor,
                           actual.combat->hull,
                           actual.combat->weapon_cooldown_remaining_days,
                           actual.combat->order,
                           actual.combat->target_fleet_id,
                           actual.combat->defend_system_id,
                           actual.combat->retreat_progress_days,
                           actual.combat->retreat_started,
                           actual.combat->is_disengaged,
                           actual.combat->disengaged_system_id};
    gate087_fleet_state::check_combat(state, normalized.at("Combat"),
                                      label + ": combat");
  }
  if (actual.tactical_loadout)
    gate087_fleet_state::check_loadout(*actual.tactical_loadout,
                                      normalized.at("TacticalLoadout"),
                                      label + ": tactical loadout");
  if (actual.tactical_vessel)
    gate087_fleet_state::check_vessel(*actual.tactical_vessel,
                                     normalized.at("TacticalVessel"),
                                     label + ": tactical vessel");
}

template <class T, class CheckItem>
void check_optional_items(const std::optional<std::vector<T>> &actual,
                          const Json &expected, const std::string &label,
                          CheckItem check_item) {
  check(actual.has_value() == !expected.is_null(), label + ": presence");
  if (!actual)
    return;
  check(actual->size() == expected.size(), label + ": size");
  for (std::size_t index = 0; index < actual->size(); ++index)
    check_item((*actual)[index], expected[index],
               label + '[' + std::to_string(index) + ']');
}

void check_payload(const GalaxyPayloadV16Dto &actual, const Json &expected,
                   const std::string &label) {
  const auto &galaxy = expected.at("Galaxy");
  check(actual.format_version == expected.at("FormatVersion").get<int>() &&
            actual.game_version == expected.at("GameVersion").get<std::string>() &&
            actual.saved_at_utc == expected.at("SavedAtUtc").get<std::string>() &&
            actual.simulation_days == expected.at("SimulationDays").get<double>() &&
            actual.seed == galaxy.at("Seed").get<std::int64_t>() &&
            actual.player_civilization_id ==
                galaxy.at("PlayerCivilizationId").get<int>(),
        label + ": envelope/scalars");
  check(gate087_metadata::encode_metadata(actual.generation_metadata) ==
            gate087_metadata::encode_metadata(
                gate087_metadata::decode_metadata(
                    galaxy.at("GenerationMetadata"))),
        label + ": generation metadata");
  check(gate087_metadata::encode_core(actual.galactic_core) ==
            gate087_metadata::encode_core(
                gate087_metadata::decode_core(galaxy.at("GalacticCore"))),
        label + ": galactic core");
  check_optional_items(actual.systems, galaxy.at("Systems"), label + ": systems",
      [](const auto &item, const auto &source, const auto &field) {
        check(gate087_foundation::system_dto_json(item) ==
                  gate087_foundation::system_dto_json(
                      gate087_foundation::system_dto(source)),
              field);
      });
  check(actual.planetary_bodies.bodies_present ==
            !galaxy.at("PlanetaryBodies").is_null(),
        label + ": body presence");
  if (actual.planetary_bodies.bodies_present) {
    check(actual.planetary_bodies.bodies.size() ==
              galaxy.at("PlanetaryBodies").size(),
          label + ": body size");
    for (std::size_t index = 0;
         index < actual.planetary_bodies.bodies.size(); ++index) {
      const auto &item = actual.planetary_bodies.bodies[index];
      check(item.has_value() == !galaxy.at("PlanetaryBodies")[index].is_null(),
            label + ": body item presence");
      if (item) {
        const auto normalized =
            normalized_body_json(galaxy.at("PlanetaryBodies")[index]);
        check(gate087_planetary::body_dto_json(*item) ==
                  gate087_planetary::body_dto_json(
                      gate087_planetary::body_dto(normalized)),
              label + ": body item");
      }
    }
  }
  check_optional_items(actual.civilizations, galaxy.at("Civilizations"),
      label + ": civilizations",
      [](const auto &item, const auto &source, const auto &field) {
        check(gate087_foundation::civilization_dto_json(item) ==
                  gate087_foundation::civilization_dto_json(
                      gate087_foundation::civilization_dto(source)),
              field);
      });
  check_optional_items(actual.fleets, galaxy.at("Fleets"), label + ": fleets",
      check_fleet_dto);
  check_optional_items(actual.colonies, galaxy.at("Colonies"), label + ": colonies",
      [](const auto &item, const auto &source, const auto &field) {
        gate087_economy::check_colony_dto(item, source, field);
      });
  check_optional_items(actual.economies, galaxy.at("Economies"), label + ": economies",
      [](const auto &item, const auto &source, const auto &field) {
        gate087_economy::check_economy_dto(item, source, field);
      });
  check_optional_items(actual.technologies, galaxy.at("Technologies"),
      label + ": technologies",
      [](const auto &item, const auto &source, const auto &field) {
        gate087_economy::check_technology_dto(item, source, field);
      });
  check_optional_items(actual.construction_states,
      galaxy.at("ConstructionStates"), label + ": construction",
      [](const auto &item, const auto &source, const auto &field) {
        gate087_economy::check_construction_dto(item, source, field);
      });
  check_optional_items(actual.shipyard_states, galaxy.at("ShipyardStates"),
      label + ": shipyards",
      [](const auto &item, const auto &source, const auto &field) {
        check(gate087_shipyard::dto_json(item) ==
                  gate087_shipyard::dto_json(
                      gate087_shipyard::decode_dto(source)),
              field);
      });
  Json knowledge = Json::array();
  if (actual.knowledge.entries_present)
    for (const auto &item : actual.knowledge.entries)
      knowledge.push_back(item ? gate087_knowledge::entry_json(*item)
                               : Json(nullptr));
  else
    knowledge = nullptr;
  CivilizationKnowledgePersistenceInput expected_knowledge;
  expected_knowledge.entries_present = !galaxy.at("Knowledge").is_null();
  if (expected_knowledge.entries_present)
    for (const auto &item : galaxy.at("Knowledge"))
      expected_knowledge.entries.push_back(
          item.is_null() ? std::nullopt
                         : std::optional{gate087_knowledge::entry(item)});
  check(knowledge == gate087_knowledge::input_json(expected_knowledge).at("Entries"),
        label + ": knowledge");
  const auto encounter = galaxy.find("ActiveCombatEncounter");
  check(actual.active_combat_encounter.has_value() ==
            (encounter != galaxy.end() && !encounter->is_null()),
        label + ": encounter presence");
  if (actual.active_combat_encounter)
    check(gate087_massive::encounter_json(*actual.active_combat_encounter) ==
              gate087_massive::encounter_json(
                  gate087_massive::encounter(*encounter)),
          label + ": encounter");
  const auto intelligence = galaxy.find("CombatIntelligence");
  check(actual.combat_intelligence.has_value() ==
            (intelligence != galaxy.end() && !intelligence->is_null()),
        label + ": intelligence presence");
  if (actual.combat_intelligence) {
    check(actual.combat_intelligence->size() == intelligence->size(),
          label + ": intelligence size");
    for (std::size_t index = 0; index < actual.combat_intelligence->size();
         ++index)
      check(gate087_intelligence::observation_json(
                (*actual.combat_intelligence)[index]) ==
                gate087_intelligence::observation_json(
                    gate087_intelligence::observation((*intelligence)[index])),
            label + ": intelligence item");
  }
}

void check_raw_world(const FreshCampaignState &actual, const Json &envelope,
                     const std::string &label) {
  const auto &expected = envelope.at("Galaxy");
  check(actual.seed == expected.at("Seed").get<std::int64_t>() &&
            actual.player_civilization_id ==
                expected.at("PlayerCivilizationId").get<int>(),
        label + ": scalar state");
  check(gate087_metadata::encode_metadata(actual.generation_metadata) ==
            gate087_metadata::encode_metadata(
                gate087_metadata::decode_metadata(
                    expected.at("GenerationMetadata"))),
        label + ": generation metadata");
  check(gate087_metadata::encode_core(actual.galactic_core) ==
            gate087_metadata::encode_core(gate087_metadata::decode_core(
                expected.at("GalacticCore"))),
        label + ": named core");

  const auto systems = capture_stellar_systems(actual.systems);
  check(systems.size() == expected.at("Systems").size(),
        label + ": systems size");
  for (std::size_t i = 0; i < systems.size(); ++i)
    gate087_foundation::require_equal(
        gate087_foundation::system_dto_json(systems[i]),
        gate087_foundation::system_dto_json(
            gate087_foundation::system_dto(expected.at("Systems")[i])),
        label, "system");

  const auto bodies = capture_planetary_bodies(actual.bodies, actual.systems);
  check(bodies.bodies.size() == expected.at("PlanetaryBodies").size(),
        label + ": bodies size");
  for (std::size_t i = 0; i < bodies.bodies.size(); ++i)
    gate087_planetary::require_equal(
        gate087_planetary::body_dto_json(*bodies.bodies[i]),
        gate087_planetary::body_dto_json(gate087_planetary::body_dto(
            normalized_body_json(expected.at("PlanetaryBodies")[i]))),
        label, "body");

  const auto civilizations = capture_civilizations(actual.civilizations);
  check(civilizations.size() == expected.at("Civilizations").size(),
        label + ": civilizations size");
  for (std::size_t i = 0; i < civilizations.size(); ++i)
    gate087_foundation::require_equal(
        gate087_foundation::civilization_dto_json(civilizations[i]),
        expected.at("Civilizations")[i], label, "civilization");

  check(actual.fleets.size() == expected.at("Fleets").size(),
        label + ": fleets size");
  for (std::size_t i = 0; i < actual.fleets.size(); ++i)
    gate087_fleet_state::check_fleet(actual.fleets[i],
                                     fleet_state_json(expected.at("Fleets")[i]),
                                     label + ": fleet");

  check(actual.colonies.size() == expected.at("Colonies").size(),
        label + ": colonies size");
  for (std::size_t i = 0; i < actual.colonies.size(); ++i)
    gate087_economy::check_colony(actual.colonies[i],
                                  expected.at("Colonies")[i],
                                  label + ": colony");
  check(actual.economies.size() == expected.at("Economies").size(),
        label + ": economies size");
  for (std::size_t i = 0; i < actual.economies.size(); ++i)
    gate087_economy::check_economy_values(actual.economies[i],
                                          expected.at("Economies")[i],
                                          label + ": economy");
  check(actual.technologies.size() == expected.at("Technologies").size(),
        label + ": technologies size");
  for (std::size_t i = 0; i < actual.technologies.size(); ++i)
    gate087_economy::check_technology(actual.technologies[i],
                                      expected.at("Technologies")[i],
                                      label + ": technology");
  check(actual.construction.size() ==
            expected.at("ConstructionStates").size(),
        label + ": construction size");
  for (std::size_t i = 0; i < actual.construction.size(); ++i)
    gate087_economy::check_construction(
        actual.construction[i], expected.at("ConstructionStates")[i],
        label + ": construction");
  check(actual.shipyards.size() == expected.at("ShipyardStates").size(),
        label + ": shipyards size");
  for (std::size_t i = 0; i < actual.shipyards.size(); ++i)
    gate087_shipyard::require_equal(
        gate087_shipyard::state_json(actual.shipyards[i]),
        expected.at("ShipyardStates")[i], label, "shipyard");

  const auto knowledge = capture_civilization_knowledge(actual.knowledge);
  Json knowledge_json = Json::array();
  for (const auto &entry : knowledge.entries)
    knowledge_json.push_back(entry ? gate087_knowledge::entry_json(*entry)
                                   : Json(nullptr));
  check(knowledge_json == expected.at("Knowledge"), label + ": knowledge");

  check(actual.active_combat_encounter.has_value() ==
            !expected.at("ActiveCombatEncounter").is_null(),
        label + ": encounter presence");
  if (actual.active_combat_encounter)
    check(gate087_massive::encounter_json(*actual.active_combat_encounter) ==
              expected.at("ActiveCombatEncounter"),
          label + ": encounter");
  const bool expected_intelligence =
      !expected.at("CombatIntelligence").is_null();
  check(expected_intelligence == !actual.combat_intelligence.empty(),
        label + ": intelligence presence");
  if (expected_intelligence) {
    check(actual.combat_intelligence.size() ==
              expected.at("CombatIntelligence").size(),
          label + ": intelligence size");
    for (std::size_t i = 0; i < actual.combat_intelligence.size(); ++i)
      check(gate087_intelligence::observation_json(
                actual.combat_intelligence[i]) ==
                gate087_intelligence::observation_json(
                    gate087_intelligence::observation(
                        expected.at("CombatIntelligence")[i])),
            label + ": intelligence");
  }
}

void mutate_for_row(GalaxyPayloadV16Dto &payload, std::string_view name) {
  if (name == "surface-null-colonies")
    payload.colonies.reset();
  else if (name == "surface-null-economies")
    payload.economies.reset();
  else if (name == "surface-null-both-colonies-first") {
    payload.colonies.reset();
    payload.economies.reset();
  } else if (name == "surface-empty-colonies")
    payload.colonies->clear();
  else if (name == "surface-empty-economies")
    payload.economies->clear();
  else if (name == "null-systems-null-reference")
    payload.systems.reset();
  else if (name == "null-fleets-null-reference")
    payload.fleets.reset();
  else if (name == "metadata-core-fallback") {
    GalacticCoreMetadata core{GalacticCoreMetadata::stable_landmark_key,
                              5000.0F, 5000.0F, 10.0F};
    payload.galactic_core.reset();
    payload.generation_metadata = GalaxyGenerationMetadata{
        "871603", payload.seed, "galaxy-v4", "2042-01-01T00:00:00+00:00",
        static_cast<int>(payload.systems->size()), "Full galaxy", "Balanced",
        "Common", "Uncommon", 0, 7, "None", "Standard",
        "Early Space Age", "Standard", "legacy-static-v1",
        std::string("terran_baseline"), "Standard", core};
  } else if (name == "player-fallback-unknown-home") {
    payload.knowledge.entries.clear();
    auto player = std::ranges::find_if(
        *payload.civilizations, [&](const auto &value) {
          return value.id == payload.player_civilization_id;
        });
    check(player != payload.civilizations->end(), "row player");
    player->home_system_id = 987654321;
  } else if (name == "no-civilizations-regenerates-seeding-catalog") {
    payload.civilizations->clear();
  } else {
    check(name == "valid-current16" ||
              name == "reversed-authored-collections",
          "Unknown fixture row '" + std::string(name) + "'.");
  }
}

void verify_source_inventory(const Json &fixture, const fs::path &source_root) {
  static constexpr std::string_view expected[] = {
      "Persistence/CampaignSaveService.cs",
      "Simulation/Generation/CivilizationSeeder.cs",
      "Simulation/Generation/PlanetaryBodyGenerator.cs",
      "Simulation/Generation/SolCatalogPreset.cs",
      "Simulation/Knowledge/CivilizationKnowledgeState.cs",
      "Simulation/Models/GalaxyState.cs",
  };
  check(fixture.at("SourceFiles").size() == std::size(expected),
        "source inventory count");
  for (std::size_t index = 0; index < std::size(expected); ++index) {
    const auto &entry = fixture.at("SourceFiles")[index];
    check(entry.at("Path").get<std::string>() == expected[index],
          "source inventory order");
    const auto path = source_root / std::string(expected[index]);
    check(sha256(bytes(path)) == entry.at("Sha256").get<std::string>(),
          "source fingerprint " + std::string(expected[index]));
  }
}

int run(const fs::path &fixture_path, const fs::path &source_root,
        const fs::path &catalog_path) {
  const auto fixture_bytes = bytes(fixture_path);
  check(sha256(fixture_bytes) ==
            "8217317A7AA1FE8873FAF76B4FAF2FD04E1A6BB61251D647A168E69EE33C056B",
        "fixture fingerprint");
  const auto fixture = Json::parse(fixture_bytes);
  check(fixture.at("SchemaVersion") == 1, "schema");
  check(fixture.at("RowCount") == 14, "row count metadata");
  check(fixture.at("Rows").size() == 14, "row count");
  check(fixture.at("SourceOnlyRows") == 0, "source-only count");
  verify_source_inventory(fixture, source_root);

  int exact_passed = 0;
  std::optional<FreshCampaignState> continuation_world;
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto operation = row.at("Operation").get<std::string>();
    check(operation == "Restore" || operation == "Capture",
          name + ": operation");
    auto input = decode_payload(row.at("Input"));
    const bool partial_capture =
        name == "capture-partial-fleet-normalization-before-economy-error";
    check_payload(input, partial_capture ? row.at("Input") : row.at("Before"),
                  name + ": decoded input");

    std::optional<RestoredGalaxyPayloadV16> result;
    std::optional<GalaxyPayloadV16Dto> captured;
    std::exception_ptr failure;
    if (operation == "Restore") {
      try {
        result = restore_galaxy_payload_v16(input);
      } catch (...) {
        failure = std::current_exception();
      }
      check_payload(input, row.at("After"), name + ": input after");
    } else {
      auto prepared = restore_galaxy_payload_v16(input);
      check_raw_world(prepared.galaxy, row.at("Input"),
                      name + ": direct prepared runtime state");
      if (partial_capture) {
        prepared.galaxy.fleets.front().combat.reset();
        prepared.galaxy.economies.front().industry_priority =
            static_cast<IndustryPriority>(999);
        check_raw_world(prepared.galaxy, row.at("Before"),
                        name + ": direct state before failed capture");
        auto projected_before = input;
        projected_before.fleets->front().combat.reset();
        projected_before.economies->front().industry_priority =
            static_cast<IndustryPriority>(999);
        check_payload(projected_before, row.at("Before"),
                      name + ": complete state before failed capture");
      }
      GalaxyPayloadCaptureOptions options{prepared.simulation_days,
                                          prepared.game_version,
                                          prepared.saved_at_utc, false};
      try {
        captured = capture_galaxy_payload_v16(prepared.galaxy, options);
      } catch (...) {
        failure = std::current_exception();
      }
      if (captured)
        check_payload(*captured, row.at("After"), name + ": state after");
      if (partial_capture) {
        check_raw_world(prepared.galaxy, row.at("After"),
                        name + ": direct state after failed capture");
        auto projected_after = input;
        projected_after.fleets = capture_fleet_dtos(prepared.galaxy.fleets);
        projected_after.economies->front().industry_priority =
            prepared.galaxy.economies.front().industry_priority;
        check_payload(projected_after, row.at("After"),
                      name + ": complete state after failed capture");
      }
      check_payload(input, partial_capture ? row.at("Input") : row.at("Before"),
                    name + ": capture DTO input");
    }
    if (row.at("ErrorType").is_null()) {
      check(!failure, name + ": unexpected exact-input failure");
      const auto &expected = row.at("Result");
      if (operation == "Capture") {
        check(captured.has_value(), name + ": missing captured result");
        check_payload(*captured, expected, name + ": complete capture result");
        ++exact_passed;
        continue;
      }
      check(result.has_value(), name + ": missing exact-input result");
      check_raw_world(result->galaxy, expected,
                      name + ": direct restored runtime state");
      check(result->simulation_days ==
                expected.at("SimulationDays").get<double>() &&
                result->game_version == expected.at("GameVersion").get<std::string>() &&
                result->saved_at_utc == input.saved_at_utc,
            name + ": restored envelope");
      GalaxyPayloadCaptureOptions options{
          result->simulation_days, result->game_version, result->saved_at_utc,
          false};
      auto recaptured = capture_galaxy_payload_v16(result->galaxy, options);
      Json expected_envelope = {
          {"FormatVersion", GalaxyPayloadV16Dto::current_format_version},
          {"GameVersion", result->game_version},
          {"SavedAtUtc", result->saved_at_utc},
          {"SimulationDays", result->simulation_days},
          {"Galaxy", expected.at("Galaxy")},
      };
      check_payload(recaptured, expected_envelope,
                    name + ": complete restored world");
      if (name == "valid-current16")
        continuation_world = result->galaxy;
    } else {
      check(bool(failure), name + ": expected exact-input failure");
      const auto actual = classify(failure);
      check(actual.type == row.at("ErrorType").get<std::string>(),
            name + ": exact error type " + actual.type);
      check(actual.message == row.at("ErrorMessage").get<std::string>(),
            name + ": exact error message '" + actual.message + "'");
      const auto expected_inner_type = row.at("InnerType").is_null()
          ? std::optional<std::string>{}
          : std::optional(row.at("InnerType").get<std::string>());
      const auto expected_inner_message = row.at("InnerMessage").is_null()
          ? std::optional<std::string>{}
          : std::optional(row.at("InnerMessage").get<std::string>());
      check(actual.inner_type == expected_inner_type,
            name + ": exact inner error type");
      check(actual.inner_message == expected_inner_message,
            name + ": exact inner error message");
    }
    ++exact_passed;
  }
  check(exact_passed == 14, "exact source row accounting");
  check(continuation_world.has_value(), "continuation world retained");

  // A live tactical encounter must be routed by the future player lifecycle.
  // Continue only the source-restored nonbattle campaign through the existing
  // integrated coordinator and prove all restored civilizations initialize.
  continuation_world->active_combat_encounter.reset();
  const auto continuation_civilizations =
      continuation_world->civilizations.size();
  auto integrated = IntegratedAdaptiveCampaignRuntime::create_fresh(
      load_adaptive_research_strategic_runtime(
          source_root.parent_path().parent_path() / "data/research/v1"),
      std::move(*continuation_world), {}, 37.25);
  const auto integrated_step = integrated.advance(0.125, 37.375);
  check(integrated.world().campaign().civilizations.size() ==
            continuation_civilizations,
        "integrated continuation civilization ownership");
  check(integrated.research().civilization_ids().size() ==
            continuation_civilizations,
        "integrated continuation research initialization");
  check(integrated_step.sensor_contacts.size() == continuation_civilizations,
        "integrated continuation sensor phase");

  auto world = baseline_state(catalog_path);
  const GalaxyPayloadCaptureOptions options{37.25, "native-gate087",
                                             "2042-03-04T05:06:07+00:00",
                                             false};
  const auto baseline = capture_galaxy_payload_v16(world, options);
  check(!baseline.active_combat_encounter && !baseline.combat_intelligence,
        "baseline optional state");

  int passed = 0;
  for (const auto &row : fixture.at("Rows")) {
    if (row.at("Operation") == "Capture")
      continue;
    const auto name = row.at("Name").get<std::string>();
    auto input = baseline;
    mutate_for_row(input, name);
    const auto before = fingerprint(input);
    std::optional<RestoredGalaxyPayloadV16> result;
    std::exception_ptr failure;
    try {
      result = restore_galaxy_payload_v16(input);
    } catch (...) {
      failure = std::current_exception();
    }
    check(fingerprint(input) == before, name + ": input changed");

    if (row.at("ErrorType").is_null()) {
      check(!failure, name + ": unexpected failure");
      check(result.has_value(), name + ": missing result");
      check(result->simulation_days == input.simulation_days,
            name + ": simulation days");
      check(result->game_version == input.game_version,
            name + ": game version");
      check(result->saved_at_utc == input.saved_at_utc,
            name + ": saved timestamp");
      check(result->galaxy.systems.size() == input.systems->size(),
            name + ": systems");
      check(result->galaxy.bodies.size() ==
                input.planetary_bodies.bodies.size(),
            name + ": bodies");
      if (name == "metadata-core-fallback") {
        check(result->galaxy.galactic_core ==
                  input.generation_metadata->galactic_core,
              name + ": resolved persisted core");
        check(result->galaxy.core.has_value(), name + ": geometric core");
      }
    } else {
      check(bool(failure), name + ": expected failure");
      const auto actual = classify(failure);
      check(actual.type == row.at("ErrorType").get<std::string>(),
            name + ": error type " + actual.type);
      check(actual.message == row.at("ErrorMessage").get<std::string>(),
            name + ": error message '" + actual.message + "'");
    }
    ++passed;
  }

  auto clone = baseline;
  clone.systems->front().name = "independent clone";
  check(baseline.systems->front().name != clone.systems->front().name,
        "payload deep copy");
  auto restored = restore_galaxy_payload_v16(baseline);
  restored.galaxy.systems.front().name = "independent restored state";
  check(baseline.systems->front().name !=
            restored.galaxy.systems.front().name,
        "restored state owns values");

  const auto continuation_system = restored.galaxy.systems.back().id;
  const auto continuation_changed =
      restored.galaxy.knowledge.mark_system_fully_surveyed(
          restored.galaxy.player_civilization_id, continuation_system);
  check(continuation_changed ||
            restored.galaxy.knowledge.is_system_fully_surveyed(
                restored.galaxy.player_civilization_id, continuation_system),
        "successful restore continuation");

  auto unrepresented = baseline_state(catalog_path);
  unrepresented.core = full_galaxy_core(250);
  std::exception_ptr unrepresented_failure;
  try {
    (void)capture_galaxy_payload_v16(unrepresented, options);
  } catch (...) {
    unrepresented_failure = std::current_exception();
  }
  check(unrepresented_failure &&
            classify(unrepresented_failure).type == "InvalidOperationException",
        "unrepresented native core capture boundary");

  auto provenance = baseline_state(catalog_path);
  provenance.developer_provenance = CampaignDeveloperProvenance{true};
  std::exception_ptr provenance_failure;
  try {
    (void)capture_galaxy_payload_v16(provenance, options);
  } catch (...) {
    provenance_failure = std::current_exception();
  }
  check(provenance_failure &&
            classify(provenance_failure).type == "InvalidOperationException",
        "capture provenance boundary");

  std::cout << "Galaxy payload parity: " << exact_passed << '/'
            << exact_passed << " exact actual-source rows; " << passed
            << " supplemental branch rows plus copy, continuation, core and provenance probes.\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::runtime_error(
          "Usage: galaxy_payload_persistence_tests <fixture> <Game source "
          "root> <astronomy catalog>");
    return run(fs::absolute(argv[1]), fs::absolute(argv[2]),
               fs::absolute(argv[3]));
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n'
              << "Working directory: " << fs::current_path().string() << '\n'
              << "Fixture path: "
              << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>")
              << '\n'
              << "Source root: "
              << (argc > 2 ? fs::absolute(argv[2]).string() : "<missing>")
              << '\n'
              << "Catalog path: "
              << (argc > 3 ? fs::absolute(argv[3]).string() : "<missing>")
              << '\n';
    return 1;
  }
}
