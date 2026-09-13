#include <stellar/core/massive_combat_persistence.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {

bool utf8_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!utf8_whitespace(value))
      return false;
  return true;
}

template <class Enum> bool valid_enum(Enum value, int maximum) noexcept {
  const auto numeric = static_cast<int>(value);
  return numeric >= 0 && numeric <= maximum;
}

std::int32_t unchecked_add(std::int32_t left, std::int32_t right) noexcept {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(left) +
                                   static_cast<std::uint32_t>(right));
}

std::int32_t checked_add(std::int32_t left, std::int32_t right) {
  const auto result = static_cast<std::int64_t>(left) + right;
  if (result < std::numeric_limits<std::int32_t>::min() ||
      result > std::numeric_limits<std::int32_t>::max())
    throw std::overflow_error("Arithmetic operation resulted in an overflow.");
  return static_cast<std::int32_t>(result);
}

int cohort_survivors(const MassiveFormationState &formation) {
  std::int32_t total = 0;
  for (const auto &cohort : formation.cohorts)
    total = checked_add(total, std::max(0, cohort.active_count));
  return total;
}

void validate_cohort(const MassiveCohortState &cohort) {
  if (cohort.id <= 0 || blank(cohort.design_id) || cohort.initial_count < 0 ||
      cohort.active_count < 0 ||
      cohort.active_count > cohort.initial_count ||
      !std::isfinite(cohort.experience))
    throw MassiveCombatStateError("Cohort state is invalid.");
}

void validate_formation(MassiveFormationState &formation) {
  if (formation.id <= 0 || formation.civilization_id < 0 ||
      formation.fleet_id < 0 || formation.task_force_id < 0 ||
      !valid_enum(formation.shape, 7) || !valid_enum(formation.order, 18) ||
      !valid_enum(formation.interdictor_protection, 3))
    throw MassiveCombatStateError(
        "Formation hierarchy or tactical enums are invalid.");
  if (blank(formation.name) || !formation.position.is_finite() ||
      !formation.velocity.is_finite() || !formation.heading.is_finite() ||
      !formation.objective.is_finite())
    throw MassiveCombatStateError(
        "Formation presentation-independent geometry is invalid.");
  if (formation.cohorts.size() >
          massive_combat_max_cohorts_per_formation ||
      formation.important_vessels.size() >
          massive_combat_max_important_vessels_per_formation)
    throw MassiveCombatStateError("Formation detail limit exceeded.");

  std::unordered_set<std::int64_t> cohort_ids;
  for (const auto &cohort : formation.cohorts)
    if (!cohort_ids.insert(cohort.id).second)
      throw MassiveCombatStateError(
          "Cohort and important-vessel identities must be unique.");
  std::unordered_set<std::int64_t> vessel_ids;
  for (const auto &vessel : formation.important_vessels)
    if (!vessel_ids.insert(vessel.id).second)
      throw MassiveCombatStateError(
          "Cohort and important-vessel identities must be unique.");

  for (const auto &cohort : formation.cohorts)
    validate_cohort(cohort);
  for (const auto &vessel : formation.important_vessels) {
    try {
      vessel.validate();
    } catch (const std::invalid_argument &error) {
      throw MassiveCombatStateError(error.what());
    }
  }
  try {
    formation.loadout.validate();
  } catch (const std::invalid_argument &error) {
    throw MassiveCombatStateError(error.what());
  }

  if (formation.initial_ship_count <= 0) {
    const auto active = formation.active_ship_count();
    formation.initial_ship_count = unchecked_add(
        active, std::max(0, formation.destroyed_ships));
  }
  if (formation.destroyed_ships < 0 ||
      formation.initial_ship_count !=
          unchecked_add(formation.surviving_ship_count(),
                        formation.destroyed_ships))
    throw MassiveCombatStateError(
        "Formation ship accounting is inconsistent.");

  for (const auto value : {formation.shield_pool, formation.armor_pool,
                           formation.hull_pool,
                           formation.hull_loss_threshold_per_ship})
    if (!std::isfinite(value) || value < 0)
      throw MassiveCombatStateError("Formation durability is invalid.");
  for (const auto value : {formation.cohesion, formation.morale,
                           formation.heat, formation.power_reserve,
                           formation.warp_spool_progress,
                           formation.hull_damage_remainder})
    if (!std::isfinite(value) || value < 0)
      throw MassiveCombatStateError("Formation tactical state is invalid.");
  if (formation.cohesion > 1 || formation.morale > 1 ||
      formation.power_reserve > 1 || formation.warp_spool_progress > 1)
    throw MassiveCombatStateError(
        "Formation normalized tactical state is out of range.");
}

} // namespace

bool MassivePoint::is_finite() const noexcept {
  return std::isfinite(x) && std::isfinite(y);
}

bool MassiveMissileSalvoState::is_valid() const noexcept {
  return id > 0 && source_formation_id > 0 && target_formation_id > 0 &&
         missile_count >= 0 && std::isfinite(damage) && damage >= 0 &&
         std::isfinite(remaining_seconds) && remaining_seconds >= 0 &&
         std::isfinite(initial_flight_seconds) && initial_flight_seconds >= 0 &&
         (!launch_position
              ? initial_flight_seconds == 0
              : launch_position->is_finite() && initial_flight_seconds > 0 &&
                    remaining_seconds <= initial_flight_seconds + .0001f);
}

int MassiveFormationState::surviving_ship_count() const {
  return unchecked_add(
      cohort_survivors(*this),
      static_cast<std::int32_t>(std::ranges::count_if(
          important_vessels,
          [](const MassiveVesselState &vessel) { return !vessel.destroyed; })));
}

int MassiveFormationState::active_ship_count() const {
  return escaped || surrendered ? 0 : surviving_ship_count();
}

bool MassiveFormationState::active() const {
  return !escaped && !surrendered && active_ship_count() > 0 && hull_pool > 0;
}

bool MassiveCombatBattleState::is_complete() const {
  std::optional<int> first;
  for (const auto &formation : formations) {
    if (!formation.active())
      continue;
    if (!first)
      first = formation.civilization_id;
    else if (*first != formation.civilization_id)
      return false;
  }
  return true;
}

MassiveCombatStateError::MassiveCombatStateError(std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignMassiveEncounterDataError::CampaignMassiveEncounterDataError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
CampaignMassiveEncounterArgumentError::CampaignMassiveEncounterArgumentError(
    std::string message)
    : std::invalid_argument(std::move(message)) {}

void validate_massive_combat_battle(MassiveCombatBattleState &battle) {
  if (std::ranges::all_of(battle.battle_id,
                          [](std::uint8_t value) { return value == 0; }))
    throw MassiveCombatStateError(
        "A massive battle requires a stable identity.");
  if (!std::isfinite(battle.pending_seconds) || battle.pending_seconds < 0 ||
      !std::isfinite(battle.simulated_seconds) || battle.simulated_seconds < 0)
    throw MassiveCombatStateError(
        "Battle time must be finite and nonnegative.");
  if (battle.formations.size() > massive_combat_max_formations)
    throw MassiveCombatStateError("Battle formation limit exceeded.");

  std::unordered_set<std::int64_t> formation_ids;
  for (const auto &formation : battle.formations)
    if (!formation_ids.insert(formation.id).second)
      throw MassiveCombatStateError("Formation identities must be unique.");
  for (auto &formation : battle.formations)
    validate_formation(formation);

  std::unordered_set<std::int64_t> important_ids;
  for (const auto &formation : battle.formations)
    for (const auto &vessel : formation.important_vessels)
      if (!important_ids.insert(vessel.id).second)
        throw MassiveCombatStateError(
            "Important-vessel identities must be globally unique within a "
            "battle.");

  std::int64_t initial_ships = 0;
  for (const auto &formation : battle.formations)
    initial_ships += static_cast<std::int64_t>(formation.initial_ship_count);
  if (initial_ships > massive_combat_max_ships)
    throw MassiveCombatStateError("Battle ship limit exceeded.");

  if (battle.events.size() > massive_combat_max_retained_events ||
      std::ranges::any_of(battle.events, [](const MassiveCombatEvent &event) {
        return !valid_enum(event.type, 11) || event.sequence <= 0 ||
               event.tick < 0 || !event.position.is_finite();
      }))
    throw MassiveCombatStateError(
        "Battle event stream is invalid or unbounded.");

  if (battle.active_salvos.size() > massive_combat_max_active_salvos ||
      std::ranges::any_of(
          battle.active_salvos, [&](const MassiveMissileSalvoState &salvo) {
            return !salvo.is_valid() ||
                   !formation_ids.contains(salvo.source_formation_id) ||
                   !formation_ids.contains(salvo.target_formation_id);
          }))
    throw MassiveCombatStateError(
        "Active missile salvo state is invalid or unbounded.");
  std::unordered_set<std::int64_t> salvo_ids;
  for (const auto &salvo : battle.active_salvos)
    if (!salvo_ids.insert(salvo.id).second)
      throw MassiveCombatStateError(
          "Active missile salvo state is invalid or unbounded.");

  std::int64_t maximum_event_sequence = 0;
  for (const auto &event : battle.events)
    maximum_event_sequence = std::max(maximum_event_sequence, event.sequence);
  std::int64_t maximum_salvo_id = 0;
  for (const auto &salvo : battle.active_salvos)
    maximum_salvo_id = std::max(maximum_salvo_id, salvo.id);
  if (battle.next_event_sequence <= maximum_event_sequence ||
      battle.next_salvo_id <= maximum_salvo_id)
    throw MassiveCombatStateError(
        "Battle sequence counters do not follow persisted state.");
}

void validate_campaign_massive_encounter(
    CampaignMassiveEncounter &encounter,
    CampaignMassiveEncounterWorldView world) {
  validate_massive_combat_battle(encounter.battle);
  const auto system_exists = std::ranges::any_of(
      world.systems, [&](const StellarSystem &system) {
        return system.id == encounter.system_id;
      });
  if (!system_exists || !std::isfinite(encounter.started_day) ||
      encounter.started_day < 0 || encounter.vessels.empty() ||
      encounter.vessels.size() > massive_combat_max_ships)
    throw CampaignMassiveEncounterDataError(
        "Campaign combat encounter identity, participants, or engagement "
        "evidence is invalid.");

  std::unordered_set<int> bound_fleet_ids;
  for (const auto &binding : encounter.vessels)
    if (!bound_fleet_ids.insert(binding.fleet_id).second)
      throw CampaignMassiveEncounterDataError(
          "Campaign combat encounter identity, participants, or engagement "
          "evidence is invalid.");
  if (encounter.last_observed_event_sequence < 0 ||
      encounter.engaged_formation_pairs.size() >
          campaign_massive_max_engagement_evidence)
    throw CampaignMassiveEncounterDataError(
        "Campaign combat encounter identity, participants, or engagement "
        "evidence is invalid.");
  std::set<std::pair<std::int64_t, std::int64_t>> engagement_pairs;
  for (const auto &pair : encounter.engaged_formation_pairs)
    if (!engagement_pairs
             .emplace(pair.first_formation_id, pair.second_formation_id)
             .second)
      throw CampaignMassiveEncounterDataError(
          "Campaign combat encounter identity, participants, or engagement "
          "evidence is invalid.");

  std::unordered_map<int, const FleetState *> fleets;
  for (const auto &fleet : world.fleets)
    if (!fleets.emplace(fleet.id, &fleet).second)
      throw CampaignMassiveEncounterArgumentError(
          "An item with the same key has already been added. Key: " +
          std::to_string(fleet.id));
  std::unordered_map<std::int64_t, const MassiveFormationState *> formations;
  for (const auto &formation : encounter.battle.formations)
    formations.emplace(formation.id, &formation);
  std::unordered_map<std::int64_t, std::vector<const CampaignCombatBinding *>>
      bindings_by_formation;
  for (const auto &binding : encounter.vessels)
    bindings_by_formation[binding.formation_id].push_back(&binding);

  for (const auto &binding : encounter.vessels) {
    const auto fleet = fleets.find(binding.fleet_id);
    if (fleet == fleets.end())
      throw CampaignMassiveEncounterDataError(
          "Campaign combat participant does not match its persistent vessel.");
    const auto formation = formations.find(binding.formation_id);
    if (formation == formations.end())
      throw CampaignMassiveEncounterDataError(
          "Campaign combat participant does not match its persistent vessel.");
    if (fleet->second->civilization_id !=
        formation->second->civilization_id)
      throw CampaignMassiveEncounterDataError(
          "Campaign combat participant does not match its persistent vessel.");
    const auto matches_important = std::ranges::any_of(
        formation->second->important_vessels,
        [&](const MassiveVesselState &vessel) {
          return vessel.id == binding.fleet_id;
        });
    std::optional<std::string_view> fleet_design;
    if (fleet->second->design_id)
      fleet_design = *fleet->second->design_id;
    else if (fleet->second->combat)
      fleet_design = fleet->second->combat->profile_id;
    const auto matches_cohort = fleet_design && std::ranges::any_of(
        formation->second->cohorts, [&](const MassiveCohortState &cohort) {
          return cohort.design_id == *fleet_design;
        });
    if (!matches_important && !matches_cohort)
      throw CampaignMassiveEncounterDataError(
          "Campaign combat participant does not match its persistent vessel.");
  }

  for (const auto &formation : encounter.battle.formations) {
    const auto found = bindings_by_formation.find(formation.id);
    const std::span<const CampaignCombatBinding *const> bound =
        found == bindings_by_formation.end()
            ? std::span<const CampaignCombatBinding *const>{}
            : std::span<const CampaignCombatBinding *const>{found->second};
    if (bound.size() != static_cast<std::size_t>(formation.initial_ship_count))
      throw CampaignMassiveEncounterDataError(
          "Campaign combat formation does not conserve its bound vessel "
          "inventory.");
    std::unordered_set<std::int64_t> important;
    for (const auto &vessel : formation.important_vessels)
      important.insert(vessel.id);
    const auto important_count = std::ranges::count_if(
        bound, [&](const CampaignCombatBinding *binding) {
          return important.contains(binding->fleet_id);
        });
    if (important_count !=
        static_cast<std::ptrdiff_t>(formation.important_vessels.size()))
      throw CampaignMassiveEncounterDataError(
          "Campaign combat formation does not conserve its bound vessel "
          "inventory.");
    std::int32_t cohort_count = 0;
    for (const auto &cohort : formation.cohorts)
      cohort_count = checked_add(cohort_count, cohort.initial_count);
    if (bound.size() - static_cast<std::size_t>(important_count) !=
            static_cast<std::size_t>(cohort_count))
      throw CampaignMassiveEncounterDataError(
          "Campaign combat formation does not conserve its bound vessel "
          "inventory.");
  }

  if (std::ranges::any_of(
          encounter.engaged_formation_pairs,
          [&](const CampaignCombatEngagement &pair) {
            return pair.first_formation_id >= pair.second_formation_id ||
                   !formations.contains(pair.first_formation_id) ||
                   !formations.contains(pair.second_formation_id);
          }))
    throw CampaignMassiveEncounterDataError(
        "Campaign combat engagement evidence references invalid formations.");
}

MassiveCombatBattleState
clone_massive_combat_battle(const MassiveCombatBattleState &source) {
  return source;
}

CampaignMassiveEncounter
clone_campaign_massive_encounter(const CampaignMassiveEncounter &source) {
  return source;
}

} // namespace stellar::core
