#include <stellar/core/campaign_massive_combat.hpp>

#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/detail/fleet_combat_intelligence_indexed.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {

std::string grouped(std::int64_t value) {
  auto text = std::to_string(value);
  for (auto at = static_cast<std::ptrdiff_t>(text.size()) - 3; at > 0;
       at -= 3)
    text.insert(static_cast<std::size_t>(at), 1, ',');
  return text;
}

template <class T> void append_bits(std::string &out, T value) {
  const auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
  out.append(bytes.data(), bytes.size());
}
void append_string(std::string &out, const std::string &value) {
  append_bits(out, value.size());
  out.append(value);
}
std::string loadout_identity(const MassiveCombatLoadout &value) {
  std::string out;
  for (const auto scalar :
       {value.mass_per_ship, value.acceleration, value.maximum_speed,
        value.shield_per_ship, value.armor_per_ship, value.hull_per_ship,
        value.reactor_output_per_ship, value.cooling_per_ship,
        value.warp_stabilization, value.warp_spool_seconds,
        value.maximum_module_mass}) {
    if (!std::isfinite(scalar))
      throw CampaignMassiveCombatSerializationError(
          ".NET number values such as positive and negative infinity cannot be written as valid JSON. To make it work when using 'JsonSerializer', consider specifying 'JsonNumberHandling.AllowNamedFloatingPointLiterals' (see https://docs.microsoft.com/dotnet/api/system.text.json.serialization.jsonnumberhandling).");
    append_bits(out, scalar);
  }
  append_bits(out, value.module_slot_capacity);
  append_bits(out, value.weapons.size());
  for (const auto &weapon : value.weapons) {
    append_string(out, weapon.id);
    append_bits(out, weapon.kind);
    append_bits(out, weapon.mounts_per_ship);
    for (const auto scalar :
         {weapon.damage_per_shot, weapon.shots_per_second, weapon.range,
          weapon.accuracy, weapon.power_per_second, weapon.heat_per_second}) {
      if (!std::isfinite(scalar))
        throw CampaignMassiveCombatSerializationError(
            ".NET number values such as positive and negative infinity cannot be written as valid JSON. To make it work when using 'JsonSerializer', consider specifying 'JsonNumberHandling.AllowNamedFloatingPointLiterals' (see https://docs.microsoft.com/dotnet/api/system.text.json.serialization.jsonnumberhandling).");
      append_bits(out, scalar);
    }
  }
  append_bits(out, value.modules.size());
  for (const auto &module : value.modules) {
    append_string(out, module.id);
    append_bits(out, module.kind);
    append_bits(out, module.installed_count);
    for (const auto scalar :
         {module.mass_each, module.power_per_second_each,
          module.heat_per_second_each, module.condition,
          module.effective_range, module.field_strength,
          module.detection_signature}) {
      if (!std::isfinite(scalar))
        throw CampaignMassiveCombatSerializationError(
            ".NET number values such as positive and negative infinity cannot be written as valid JSON. To make it work when using 'JsonSerializer', consider specifying 'JsonNumberHandling.AllowNamedFloatingPointLiterals' (see https://docs.microsoft.com/dotnet/api/system.text.json.serialization.jsonnumberhandling).");
      append_bits(out, scalar);
    }
    append_bits(out, module.enabled);
    append_bits(out, module.slots);
  }
  return out;
}

MassiveVesselState clone_vessel(const MassiveVesselState &value) {
  for (const auto scalar :
       {value.hull_fraction, value.engine_fraction, value.sensor_fraction,
        value.warp_drive_fraction, value.reactor_fraction,
        value.interdictor_fraction})
    if (!std::isfinite(scalar))
      throw CampaignMassiveCombatSerializationError(
          ".NET number values such as positive and negative infinity cannot be written as valid JSON. To make it work when using 'JsonSerializer', consider specifying 'JsonNumberHandling.AllowNamedFloatingPointLiterals' (see https://docs.microsoft.com/dotnet/api/system.text.json.serialization.jsonnumberhandling).");
  return value;
}

const CombatProfileDefinition &profile(const FleetState &fleet) {
  if (fleet.combat) {
    if (const auto *found = find_combat_profile(fleet.combat->profile_id))
      return *found;
  }
  return get_combat_profile(default_combat_profile_id(fleet.role));
}
bool important(const MassiveVesselState &value) {
  return value.is_flagship || value.is_carrier || value.is_interdictor ||
         value.is_story_ship;
}
MassiveVesselState vessel(const FleetState &fleet,
                          const FleetCombatState &combat,
                          const CombatProfileDefinition &profile_value) {
  return {fleet.id,
          fleet.name,
          fleet.design_id.value_or(combat.profile_id),
          false,
          false,
          false,
          fleet.role == FleetRole::Colony,
          static_cast<float>(std::clamp(
              combat.hull / std::max(1.0, profile_value.max_hull), 0.0, 1.0))};
}
std::array<std::uint8_t, 16> deterministic_battle_id(std::uint64_t seed) {
  std::array<std::uint8_t, 16> result{};
  const auto second = seed ^ 0x9E3779B97F4A7C15ULL;
  for (int index = 0; index < 8; ++index) {
    result[index] = static_cast<std::uint8_t>(seed >> (index * 8));
    result[index + 8] = static_cast<std::uint8_t>(second >> (index * 8));
  }
  return result;
}
std::uint64_t begin_seed(std::int64_t seed, double day, int actor) {
  return std::bit_cast<std::uint64_t>(seed) ^ std::bit_cast<std::uint64_t>(day) ^
         static_cast<std::uint32_t>(actor);
}
int unchecked_increment(int value) noexcept {
  return std::bit_cast<int>(std::bit_cast<std::uint32_t>(value) + 1U);
}
double dotnet_max(double left, double right) {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return left > right ? left : right;
}
double dotnet_min(double left, double right) {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return left < right ? left : right;
}

float formation_power(const MassiveFormationState &formation) {
  const auto ships = formation.surviving_ship_count();
  const auto maximum = (formation.loadout.shield_per_ship +
                        formation.loadout.armor_per_ship +
                        formation.loadout.hull_per_ship) * std::max(1, ships);
  const auto condition = std::clamp(
      (formation.shield_pool + formation.armor_pool + formation.hull_pool) /
          std::max(1.F, maximum), 0.F, 1.F);
  return massive_combat_per_ship_power(formation.loadout) * ships * condition *
         std::clamp(formation.cohesion, .2F, 1.F);
}
float vessel_power(const MassiveCombatLoadout &loadout,
                   const MassiveVesselState &vessel_value) {
  return massive_combat_per_ship_power(loadout) * std::clamp(
      (vessel_value.hull_fraction + vessel_value.engine_fraction +
       vessel_value.sensor_fraction + vessel_value.warp_drive_fraction +
       vessel_value.reactor_fraction) / 5.F, 0.F, 1.F);
}
float heading_radians(const MassiveFormationState &formation) {
  auto heading = formation.heading;
  if (heading.x * heading.x + heading.y * heading.y < .000001F)
    heading = formation.velocity;
  return heading.x * heading.x + heading.y * heading.y < .000001F
             ? 0.F : std::atan2(heading.y, heading.x);
}
int uncertainty(int count, bool exact, float confidence) {
  if (exact) return 0;
  const auto rounded = std::ceil(count * (1.F - confidence) * .45F);
  // Match CLR's unchecked floating conversion without undefined C++ casts.
  const auto converted = !std::isfinite(rounded) ||
          static_cast<double>(rounded) < std::numeric_limits<int>::min() ||
          static_cast<double>(rounded) > std::numeric_limits<int>::max()
      ? std::numeric_limits<int>::min() : static_cast<int>(rounded);
  return std::max(1, converted);
}
CampaignCombatEngagement engagement(std::int64_t first, std::int64_t second) {
  return first < second ? CampaignCombatEngagement{first, second}
                       : CampaignCombatEngagement{second, first};
}

struct Prepared {
  FleetState *fleet{};
  FleetCombatState *combat{};
  const MassiveCombatLoadout *loadout{};
  MassiveVesselState vessel;
  std::string identity;
};
struct Compatibility {
  int civilization{};
  bool military{};
  std::string profile;
  std::uint64_t shields{}, armor{}, hull{};
  std::string identity;
  auto operator<=>(const Compatibility &) const = default;
};

} // namespace

CampaignMassiveCombatArgumentNullError::CampaignMassiveCombatArgumentNullError(
    std::string message, std::string parameter)
    : std::invalid_argument(std::move(message)), parameter_(std::move(parameter)) {}
const std::string &
CampaignMassiveCombatArgumentNullError::parameter() const noexcept {
  return parameter_;
}

struct CampaignMassiveCombat::Storage {
  struct EvidenceIndex {
    const FreshCampaignState *world{};
    const CampaignMassiveEncounter *encounter{};
    std::map<int, const FleetState *> fleets;
    std::map<int, std::int64_t> formation_by_fleet;
    std::vector<std::pair<int, std::int64_t>> binding_order;
    std::map<std::int64_t, std::vector<const FleetState *>> members;

    EvidenceIndex(const FreshCampaignState &source,
                  const CampaignMassiveEncounter &battle)
        : world(&source), encounter(&battle) {
      for (const auto &fleet : source.fleets)
        if (!fleets.emplace(fleet.id, &fleet).second)
          throw std::invalid_argument("An item with the same key has already been added.");
      for (const auto &binding : battle.vessels)
        if (!formation_by_fleet.emplace(binding.fleet_id, binding.formation_id).second)
          throw std::invalid_argument("An item with the same key has already been added.");
      for (const auto &binding : battle.vessels) {
        binding_order.emplace_back(binding.fleet_id, binding.formation_id);
        members[binding.formation_id].push_back(fleets.at(binding.fleet_id));
      }
    }

    [[nodiscard]] bool current(const FreshCampaignState &source,
                               const CampaignMassiveEncounter &battle) const {
      if (world != &source || encounter != &battle ||
          fleets.size() != source.fleets.size() ||
          formation_by_fleet.size() != battle.vessels.size())
        return false;
      for (const auto &fleet : source.fleets) {
        const auto found = fleets.find(fleet.id);
        if (found == fleets.end() || found->second != &fleet) return false;
      }
      for (std::size_t i = 0; i < battle.vessels.size(); ++i) {
        const auto &binding = battle.vessels[i];
        if (binding_order[i] != std::pair{binding.fleet_id, binding.formation_id})
          return false;
      }
      return true;
    }
  };
  CombatHostilityView hostility;
  MassiveCombatEngine engine;
  std::unique_ptr<EvidenceIndex> evidence_index;
  explicit Storage(CombatHostilityView value)
      : hostility(std::move(value)), engine(hostility) {}
};

CampaignMassiveCombat::CampaignMassiveCombat(CombatHostilityView hostility) {
  if (!hostility)
    throw CampaignMassiveCombatArgumentNullError(
        "Value cannot be null. (Parameter 'hostility')", "hostility");
  storage_ = std::make_unique<Storage>(std::move(hostility));
}
CampaignMassiveCombat::~CampaignMassiveCombat() = default;
CampaignMassiveCombat::CampaignMassiveCombat(CampaignMassiveCombat &&) noexcept =
    default;
CampaignMassiveCombat &CampaignMassiveCombat::operator=(
    CampaignMassiveCombat &&) noexcept = default;

CombatOrderResult CampaignMassiveCombat::begin(FreshCampaignState &galaxy,
                                                int civilization_id,
                                                int actor_fleet_id,
                                                double day, bool spatial_deployment) {
  if (!std::isfinite(day) || day < 0)
    return {false, "Combat time is invalid."};
  if (galaxy.active_combat_encounter &&
      !galaxy.active_combat_encounter->reconciled)
    return {false,
            "An encounter is already active. Use its tactical orders."};
  auto actor = std::ranges::find_if(galaxy.fleets, [&](const FleetState &fleet) {
    return fleet.id == actor_fleet_id && fleet.is_active &&
           fleet.civilization_id == civilization_id &&
           fleet.role == FleetRole::Military;
  });
  if (actor == galaxy.fleets.end() || !actor->current_system_id ||
      actor->destination_system_id)
    return {false,
            "An armed fleet must be stationed in a system before engaging."};
  const auto system_id = *actor->current_system_id;
  std::vector<FleetState *> participants;
  for (auto &fleet : galaxy.fleets)
    if (fleet.is_active && fleet.current_system_id == system_id &&
        !fleet.destination_system_id &&
        (fleet.civilization_id == civilization_id ||
         storage_->hostility(civilization_id, fleet.civilization_id)))
      participants.push_back(&fleet);
  std::ranges::sort(participants, {}, [](const FleetState *fleet) {
    return fleet->id;
  });
  if (std::ranges::none_of(participants, [&](const FleetState *fleet) {
        return storage_->hostility(civilization_id, fleet->civilization_id);
      }))
    return {false,
            "No attackable hostile formation is detected in this system."};
  if (participants.size() > massive_combat_max_ships)
    return {false, "This encounter exceeds the " +
                       grouped(massive_combat_max_ships) +
                       "-ship tactical limit."};

  std::map<std::string, MassiveCombatLoadout, std::less<>> legacy;
  std::vector<Prepared> prepared;
  for (auto *fleet : participants) {
    const auto &profile_value = profile(*fleet);
    auto &combat = ensure_fleet_combat_state(*fleet);
    auto [entry, inserted] = legacy.try_emplace(profile_value.id);
    if (inserted)
      entry->second = massive_loadout_from_legacy(profile_value);
    const auto &loadout = fleet->tactical_loadout
                              ? *fleet->tactical_loadout
                              : entry->second;
    auto identity = loadout_identity(loadout);
    auto tactical = fleet->tactical_vessel
                        ? clone_vessel(*fleet->tactical_vessel)
                        : vessel(*fleet, combat, profile_value);
    tactical.id = campaign_vessel_id_for_fleet(fleet->id);
    tactical.name = fleet->name;
    tactical.hull_fraction = static_cast<float>(std::clamp(
        combat.hull / std::max(1.0, profile_value.max_hull), 0.0, 1.0));
    tactical.destroyed = false;
    tactical.escaped = false;
    prepared.push_back({fleet, &combat, &loadout, std::move(tactical),
                        std::move(identity)});
  }
  if (std::ranges::any_of(prepared,
                          [](const Prepared &value) {
                            return value.combat->hull <= 0;
                          }))
    return {false, "A participating vessel has no combat-ready hull."};

  std::map<Compatibility, std::vector<Prepared *>> grouped_values;
  for (auto &value : prepared)
    grouped_values[{value.fleet->civilization_id,
                    value.fleet->role == FleetRole::Military,
                    value.combat->profile_id,
                    std::bit_cast<std::uint64_t>(value.combat->shields),
                    std::bit_cast<std::uint64_t>(value.combat->armor),
                    std::bit_cast<std::uint64_t>(value.combat->hull),
                    value.identity}]
        .push_back(&value);
  std::vector<std::pair<Compatibility, std::vector<Prepared *>>> groups(
      grouped_values.begin(), grouped_values.end());
  std::ranges::sort(groups, [&](const auto &left, const auto &right) {
    if (left.first.civilization != right.first.civilization)
      return left.first.civilization < right.first.civilization;
    const auto minimum = [](const auto &group) {
      return std::ranges::min(group.second, {},
                              [](const Prepared *value) {
                                return value->fleet->id;
                              })
          ->fleet->id;
    };
    return minimum(left) < minimum(right);
  });
  std::size_t formation_count = 0;
  for (const auto &group : groups) {
    const auto count = std::ranges::count_if(
        group.second, [](const Prepared *value) {
          return important(value->vessel);
        });
    formation_count += std::max<std::size_t>(
        1, (count + massive_combat_max_important_vessels_per_formation - 1) /
               massive_combat_max_important_vessels_per_formation);
  }
  if (formation_count > massive_combat_max_formations)
    return {false,
            "The participating vessels require more damage-compatible "
            "tactical groups than the formation limit permits."};

  std::vector<MassiveFormationState> formations;
  std::vector<CampaignCombatBinding> bindings;
  std::map<std::int64_t, std::vector<Prepared *>> members_by_formation;
  for (auto &group : groups) {
    std::ranges::sort(group.second, {}, [](const Prepared *value) {
      return value->fleet->id;
    });
    std::vector<Prepared *> ordinary, special;
    for (auto *value : group.second)
      (important(value->vessel) ? special : ordinary).push_back(value);
    const auto chunks = std::max<std::size_t>(
        1, (special.size() + massive_combat_max_important_vessels_per_formation -
            1) /
               massive_combat_max_important_vessels_per_formation);
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
      std::vector<Prepared *> selected;
      if (chunk == 0)
        selected.insert(selected.end(), ordinary.begin(), ordinary.end());
      const auto first = chunk * massive_combat_max_important_vessels_per_formation;
      const auto last = std::min(special.size(), first +
          massive_combat_max_important_vessels_per_formation);
      selected.insert(selected.end(), special.begin() + static_cast<std::ptrdiff_t>(first),
                      special.begin() + static_cast<std::ptrdiff_t>(last));
      std::ranges::sort(selected, {}, [](const Prepared *value) {
        return value->fleet->id;
      });
      if (selected.empty())
        continue;
      const auto *sample = selected.front();
      const auto id = static_cast<std::int64_t>(formations.size()) + 1;
      const auto side = sample->fleet->civilization_id == civilization_id ? -1.F : 1.F;
      MassiveFormationState formation;
      formation.id = id;
      formation.fleet_id = sample->fleet->id;
      formation.task_force_id = sample->fleet->civilization_id;
      formation.civilization_id = sample->fleet->civilization_id;
      formation.name = "Task Force " + grouped(id);
      formation.position = {side * 420.F,
                            (static_cast<int>(formations.size()) % 24 - 12) * 55.F};
      if(spatial_deployment)formation.position.z = static_cast<float>(static_cast<int>(formation.id % 5) - 2) * 40.F;
      formation.heading = {-side, 0};
      formation.order = group.first.military ? MassiveCombatOrderType::Engage
                                             : MassiveCombatOrderType::Retreat;
      formation.shape = group.first.military ? MassiveFormationShape::Line
                                             : MassiveFormationShape::RetreatColumn;
      formation.loadout = *sample->loadout;
      formation.hull_loss_threshold_per_ship =
          static_cast<float>(sample->combat->hull);
      for (auto *value : selected)
        if (important(value->vessel))
          formation.important_vessels.push_back(value->vessel);
      if (!ordinary.empty() && chunk == 0)
        formation.cohorts.push_back(
            {id * 1'000'000 + 1,
             sample->fleet->design_id.value_or(sample->combat->profile_id),
             static_cast<int>(ordinary.size()),
             static_cast<int>(ordinary.size()), .5F});
      for (auto *value : selected)
        bindings.push_back({value->fleet->id, id});
      members_by_formation.emplace(id, selected);
      formations.push_back(std::move(formation));
    }
  }
  MassiveCombatBattleState battle;
  battle.seed = begin_seed(galaxy.seed, day, actor_fleet_id);
  if (battle.seed == 0)
    battle.seed = 1;
  battle.battle_id = deterministic_battle_id(begin_seed(galaxy.seed, day, actor_fleet_id));
  battle.formations = std::move(formations);
  for (auto &formation : battle.formations) {
    formation.initial_ship_count = formation.active_ship_count();
    formation.shield_pool =
        formation.loadout.shield_per_ship * formation.active_ship_count();
    formation.armor_pool =
        formation.loadout.armor_per_ship * formation.active_ship_count();
    formation.hull_pool =
        formation.loadout.hull_per_ship * formation.active_ship_count();
  }
  validate_massive_combat_battle(battle);
  for (auto &formation : battle.formations) {
    const auto &members = members_by_formation.at(formation.id);
    double shield = 0, armor = 0, hull = 0;
    for (auto *member : members) {
      shield += member->combat->shields;
      armor += member->combat->armor;
      hull += member->combat->hull;
    }
    formation.shield_pool = static_cast<float>(shield);
    formation.armor_pool = static_cast<float>(armor);
    formation.hull_pool = static_cast<float>(hull);
  }
  storage_->evidence_index.reset();
  galaxy.active_combat_encounter = CampaignMassiveEncounter{
      system_id, day, std::move(battle), std::move(bindings)};
  validate_campaign_massive_encounter(
      *galaxy.active_combat_encounter, {galaxy.systems, galaxy.fleets});
  return {true, "Encounter established: " + grouped(participants.size()) +
                    " commissioned vessels. Tactical orders ready."};
}

std::vector<CombatEvent>
CampaignMassiveCombat::reconcile(FreshCampaignState &galaxy) {
  auto *encounter = galaxy.active_combat_encounter
                        ? &*galaxy.active_combat_encounter
                        : nullptr;
  if (!encounter || encounter->reconciled) {
    storage_->evidence_index.reset();
    return {};
  }
  if (!encounter->battle.is_complete() &&
      storage_->engine.has_active_hostilities(encounter->battle))
    return {};
  std::unordered_map<int, FleetState *> fleet_map;
  for (auto &fleet : galaxy.fleets)
    if (!fleet_map.emplace(fleet.id, &fleet).second)
      throw std::invalid_argument("An item with the same key has already been added.");
  std::map<std::int64_t, std::vector<CampaignCombatBinding>> bindings;
  for (const auto &binding : encounter->vessels)
    bindings[binding.formation_id].push_back(binding);
  for (auto &[id, values] : bindings) {
    static_cast<void>(id);
    std::ranges::sort(values, {}, &CampaignCombatBinding::fleet_id);
  }
  std::vector<CombatEvent> events;
  for (auto &formation : encounter->battle.formations) {
    const auto bound = bindings[formation.id];
    std::map<std::int64_t, MassiveVesselState> tracked;
    for (const auto &value : formation.important_vessels)
      if (!tracked.emplace(value.id, value).second)
        throw std::invalid_argument("An item with the same key has already been added.");
    std::vector<int> survivors;
    for (const auto &[id, vessel_value] : tracked) {
      if (vessel_value.destroyed)
        continue;
      if (id == campaign_zero_fleet_vessel_id) {
        const auto binding = std::ranges::find(bound, 0,
                                               &CampaignCombatBinding::fleet_id);
        if (binding == bound.end())
          throw std::invalid_argument(
              "Campaign combat participant does not match its persistent vessel.");
        survivors.push_back(0);
        continue;
      }
      if (id < std::numeric_limits<int>::min() ||
          id > std::numeric_limits<int>::max())
        throw std::overflow_error(
            "Arithmetic operation resulted in an overflow.");
      survivors.push_back(static_cast<int>(id));
    }
    int cohort_survivors = 0;
    for (const auto &cohort : formation.cohorts) {
      const auto next = static_cast<std::int64_t>(cohort_survivors) +
                        cohort.active_count;
      if (next < std::numeric_limits<int>::min() ||
          next > std::numeric_limits<int>::max())
        throw std::overflow_error(
            "Arithmetic operation resulted in an overflow.");
      cohort_survivors = static_cast<int>(next);
    }
    for (const auto &binding : bound)
      if (!tracked.contains(campaign_vessel_id_for_fleet(binding.fleet_id)) &&
          cohort_survivors > 0) {
        survivors.push_back(binding.fleet_id);
        --cohort_survivors;
      }
    std::ranges::sort(survivors);
    const auto survived = [&](int id) {
      return std::ranges::binary_search(survivors, id);
    };
  const auto surviving_count = static_cast<float>(
      std::ranges::count_if(bound, [&](const CampaignCombatBinding &binding) {
        return survived(binding.fleet_id);
      }));
    const auto shields_each = surviving_count == 0 ? 0 : formation.shield_pool / surviving_count;
    const auto armor_each = surviving_count == 0 ? 0 : formation.armor_pool / surviving_count;
    const auto hull_each = surviving_count == 0 ? 0 : formation.hull_pool / surviving_count;
    for (const auto &binding : bound) {
      const auto found = fleet_map.find(binding.fleet_id);
      if (found == fleet_map.end())
        continue;
      auto &fleet = *found->second;
      const auto &profile_value = profile(fleet);
      auto &combat = ensure_fleet_combat_state(fleet);
      const auto alive = survived(fleet.id);
      const auto tactical_vessel_id = campaign_vessel_id_for_fleet(fleet.id);
      auto vessel_value = tracked.contains(tactical_vessel_id)
                              ? clone_vessel(tracked.at(tactical_vessel_id))
                              : (fleet.tactical_vessel
                                     ? clone_vessel(*fleet.tactical_vessel)
                                     : clone_vessel(
                                           vessel(fleet, combat, profile_value)));
      vessel_value.name = fleet.name;
      vessel_value.id = tactical_vessel_id;
      vessel_value.destroyed = !alive;
      vessel_value.escaped = alive && formation.escaped;
      vessel_value.battles_fought =
          unchecked_increment(vessel_value.battles_fought);
      static_cast<void>(loadout_identity(formation.loadout));
      auto cloned_loadout = formation.loadout;
      fleet.tactical_loadout = std::move(cloned_loadout);
      fleet.tactical_vessel = vessel_value;
      combat.shields = alive ? dotnet_min(profile_value.max_shields,
                                          static_cast<double>(shields_each))
                             : 0;
      combat.armor = alive ? dotnet_min(profile_value.max_armor,
                                        static_cast<double>(armor_each))
                           : 0;
      combat.hull = alive ? dotnet_min(profile_value.max_hull,
                                       static_cast<double>(hull_each))
                          : 0;
      fleet.tactical_vessel->hull_fraction = static_cast<float>(std::clamp(
          combat.hull / std::max(1.0, profile_value.max_hull), 0.0, 1.0));
      combat.order = MilitaryOrderType::Hold;
      combat.target_fleet_id.reset();
      combat.is_disengaged = alive && formation.escaped;
      combat.disengaged_system_id =
          combat.is_disengaged ? std::optional{encounter->system_id} : std::nullopt;
      if (alive)
        continue;
      fleet.is_active = false;
      const auto casualties =
          dotnet_max(0.0, fleet.embarked_population_millions);
      fleet.embarked_population_millions = 0;
      fleet.embarked_population_species_id.reset();
      fleet.destination_system_id.reset();
      fleet.planned_route_system_ids.clear();
      fleet.destination_planetary_body_id.reset();
      if (events.size() < 128)
        events.push_back({CombatEventType::FleetDestroyed,
                          encounter->system_id,
                          fleet.civilization_id,
                          fleet.id,
                          std::nullopt,
                          std::nullopt,
                          0,
                          0,
                          profile_value.max_hull,
                          fleet.name + " was lost in combat.",
                          casualties});
    }
  }
  encounter->reconciled = true;
  storage_->evidence_index.reset();
  events.push_back({CombatEventType::EngagementEnded,
                    encounter->system_id,
                    galaxy.player_civilization_id,
                    0,
                    std::nullopt,
                    std::nullopt,
                    0,
                    0,
                    0,
                    "Tactical encounter concluded. Damage and losses are persistent."});
  return events;
}

MassiveCombatOrderResult CampaignMassiveCombat::issue_order(
    FreshCampaignState &galaxy, int civilization_id,
    MassiveCombatOrder order) {
  auto *encounter = galaxy.active_combat_encounter
                        ? &*galaxy.active_combat_encounter
                        : nullptr;
  if (!encounter || encounter->reconciled)
    return {false, "There is no active tactical encounter."};
  return storage_->engine.issue_order(encounter->battle, civilization_id,
                                      std::move(order));
}

MassiveCombatSnapshot CampaignMassiveCombat::observe(
    const FreshCampaignState &galaxy, int observer_civilization_id,
    bool scanning_capability) const {
  if (!galaxy.active_combat_encounter)
    throw std::runtime_error("No tactical encounter is active.");
  const auto &encounter = *galaxy.active_combat_encounter;
  std::map<std::int64_t, int> owners;
  for (const auto &formation : encounter.battle.formations)
    if (!owners.emplace(formation.id, formation.civilization_id).second)
      throw std::invalid_argument("An item with the same key has already been added.");
  std::set<std::pair<int, std::int64_t>> engaged;
  for (const auto &pair : encounter.engaged_formation_pairs) {
    engaged.emplace(owners.at(pair.first_formation_id), pair.second_formation_id);
    engaged.emplace(owners.at(pair.second_formation_id), pair.first_formation_id);
  }
  MassiveCombatSensorView sensors;
  sensors.confidence = [](int, std::int64_t) { return .65F; };
  sensors.identifies_cohorts = [scanning_capability, &engaged](int observer,
                                                                 std::int64_t id) {
    return scanning_capability || engaged.contains({observer, id});
  };
  sensors.identifies_important_vessels = sensors.identifies_cohorts;
  sensors.can_estimate_combat_power = sensors.identifies_cohorts;
  return build_massive_combat_snapshot(encounter.battle,
                                       observer_civilization_id, sensors);
}

std::vector<CombatEvent> CampaignMassiveCombat::advance(
    FreshCampaignState &galaxy, double elapsed_seconds,
    const std::function<bool(int)> &has_combat_scanner) {
  auto *encounter = galaxy.active_combat_encounter
                        ? &*galaxy.active_combat_encounter : nullptr;
  if (!encounter) {
    storage_->evidence_index.reset();
    throw std::runtime_error("No tactical encounter is active.");
  }
  if (encounter->reconciled) {
    storage_->evidence_index.reset();
    return {};
  }
  static_cast<void>(storage_->engine.advance(encounter->battle, elapsed_seconds));

  FleetCombatIntelligenceWorldView intelligence{galaxy.civilizations,
                                                 galaxy.fleets,
                                                 galaxy.combat_intelligence};
  std::vector<const MassiveCombatEvent *> new_events;
  for (const auto &event : encounter->battle.events)
    if (event.sequence > encounter->last_observed_event_sequence)
      new_events.push_back(&event);
  std::map<std::int64_t, MassiveFormationState *> formations;
  if (!new_events.empty())
    for (auto &formation : encounter->battle.formations)
      if (!formations.emplace(formation.id, &formation).second)
        throw std::invalid_argument("An item with the same key has already been added.");
  Storage::EvidenceIndex *index = nullptr;
  std::stable_sort(new_events.begin(), new_events.end(),
                   [](const auto *a, const auto *b) { return a->sequence < b->sequence; });
  for (const auto *event : new_events) {
    encounter->last_observed_event_sequence = std::max(
        encounter->last_observed_event_sequence, event->sequence);
    if (!event->target_formation_id ||
        !(event->type == MassiveCombatEventType::BeamVolley ||
          event->type == MassiveCombatEventType::KineticVolley ||
          event->type == MassiveCombatEventType::MissileSalvo ||
          event->type == MassiveCombatEventType::Damage))
      continue;
    const auto actor = formations.find(event->actor_formation_id);
    const auto target = formations.find(*event->target_formation_id);
    if (actor == formations.end() || target == formations.end()) continue;
    const auto pair = engagement(actor->second->id, target->second->id);
    const auto already = std::ranges::any_of(
        encounter->engaged_formation_pairs, [&](const auto &value) {
          return value.first_formation_id == pair.first_formation_id &&
                 value.second_formation_id == pair.second_formation_id;
        });
    const auto fresh = !already && encounter->engaged_formation_pairs.size() <
                                   campaign_massive_max_engagement_evidence;
    if (fresh) encounter->engaged_formation_pairs.push_back(pair);
    if (!fresh || actor->second->civilization_id == target->second->civilization_id)
      continue;
    if (!index) {
      if (!storage_->evidence_index ||
          !storage_->evidence_index->current(galaxy, *encounter))
        storage_->evidence_index = std::make_unique<Storage::EvidenceIndex>(galaxy, *encounter);
      index = storage_->evidence_index.get();
    }
    const auto &target_members = index->members.at(target->second->id);
    detail::observe_fleet_combat_power_many_indexed(intelligence, actor->second->civilization_id,
                                    target_members, encounter->started_day,
                                    true, false, index->fleets);
    const auto &actor_members = index->members.at(actor->second->id);
    detail::observe_fleet_combat_power_many_indexed(intelligence, target->second->civilization_id,
                                    actor_members, encounter->started_day,
                                    true, false, index->fleets);
  }
  if (!new_events.empty()) std::ranges::sort(encounter->engaged_formation_pairs, {},
                    [](const auto &pair) {
                      return std::pair{pair.first_formation_id,
                                       pair.second_formation_id};
                    });
  if (encounter->battle.tick % 10 == 0) {
    std::set<int> civilizations;
    for (const auto &formation : encounter->battle.formations)
      if (formation.active() && formation.civilization_id != galaxy.player_civilization_id)
        civilizations.insert(formation.civilization_id);
    for (const auto civilization_id : civilizations) {
      const auto snapshot = observe(galaxy, civilization_id,
                                    has_combat_scanner && has_combat_scanner(civilization_id));
      for (const auto &order : decide_massive_combat_doctrine(snapshot, civilization_id))
        static_cast<void>(storage_->engine.issue_order(encounter->battle,
                                                        civilization_id, order));
    }
  }
  return reconcile(galaxy);
}

MassiveCombatSnapshot build_massive_combat_snapshot(
    const MassiveCombatBattleState &battle, int observer_civilization_id,
    const MassiveCombatSensorView &sensors) {
  if (!sensors.confidence || !sensors.identifies_cohorts ||
      !sensors.identifies_important_vessels || !sensors.can_estimate_combat_power)
    throw std::invalid_argument("Massive combat sensors must provide every observation capability.");
  MassiveCombatSnapshot result{battle.battle_id, battle.tick,
                               battle.simulated_seconds};
  std::vector<const MassiveFormationState *> visible_formations;
  for (const auto &formation : battle.formations) {
    if (formation.active() &&
        (formation.civilization_id == observer_civilization_id ||
         sensors.confidence(observer_civilization_id, formation.id) > 0.F))
      visible_formations.push_back(&formation);
  }
  std::ranges::sort(visible_formations, {}, &MassiveFormationState::id);
  for (const auto *visible : visible_formations) {
    const auto &formation = *visible;
    const auto own = formation.civilization_id == observer_civilization_id;
    const auto confidence = own ? 1.F : std::clamp(
        sensors.confidence(observer_civilization_id, formation.id), 0.F, 1.F);
    const auto exact = own || confidence >= .999F;
    const auto count = formation.active_ship_count();
    MassiveObservedFormation observed;
    if (own || sensors.can_estimate_combat_power(observer_civilization_id, formation.id)) {
      const auto strength = formation_power(formation);
      const auto range = exact ? 0.F : strength * (1.F - confidence) * .5F;
      observed.strength_low = static_cast<float>(dotnet_max(0.F, strength - range));
      observed.strength_high = strength + range;
    }
    const auto visible_vessels = own || sensors.identifies_important_vessels(observer_civilization_id, formation.id);
    const auto visible_cohorts = own || sensors.identifies_cohorts(observer_civilization_id, formation.id);
    const auto power_known = own || sensors.can_estimate_combat_power(observer_civilization_id, formation.id);
    observed.formation_id = formation.id;
    observed.civilization_id = formation.civilization_id;
    observed.display_name = own || confidence >= .65F ? formation.name : "Unidentified formation";
    observed.position = formation.position;
    observed.velocity = formation.velocity;
    observed.shape = formation.shape;
    const auto spread = uncertainty(count, exact, confidence);
    observed.ship_count_low = std::max(0, count - spread);
    observed.ship_count_high = count + spread;
    observed.is_exact = exact;
    observed.is_interdicting = visible_vessels && std::ranges::any_of(
        formation.loadout.modules, [](const auto &module) {
          return module.kind == MassiveModuleKind::WarpInterdictor &&
                 module.enabled && module.condition > .05F;
        });
    observed.is_warp_blocked = own && formation.warp_blocked;
    observed.warp_spool_progress = own ? formation.warp_spool_progress : 0.F;
    observed.heading_radians = heading_radians(formation);
    if (visible_cohorts) {
      for (const auto &cohort : formation.cohorts) if (cohort.active_count > 0) {
        const auto cohort_spread = uncertainty(cohort.active_count, exact, confidence);
        observed.cohorts.push_back({cohort.id, cohort.design_id,
                                    std::max(0, cohort.active_count - cohort_spread),
                                    cohort.active_count + cohort_spread, true});
      }
      std::ranges::sort(observed.cohorts, {}, &MassiveObservedCohort::cohort_id);
    } else if (count > 0) {
      observed.cohorts.push_back({formation.id, "Unidentified ships",
                                  observed.ship_count_low, observed.ship_count_high, false});
    }
    if (visible_vessels) for (const auto &vessel_value : formation.important_vessels)
      if (!vessel_value.destroyed && !vessel_value.escaped)
        observed.important_vessels.push_back({vessel_value.id, vessel_value.name,
          vessel_value.design_id, power_known ? std::optional{vessel_power(formation.loadout, vessel_value)} : std::nullopt,
          vessel_value.is_flagship, vessel_value.is_carrier, vessel_value.is_interdictor,
          vessel_value.hull_fraction < .3F});
    std::ranges::sort(observed.important_vessels, {}, &MassiveObservedVessel::vessel_id);
    if (power_known)
      observed.per_ship_combat_power = formation.surviving_ship_count() <= 0
          ? 0.F : formation_power(formation) / formation.surviving_ship_count();
    result.formations.push_back(std::move(observed));
  }
  std::map<std::int64_t, const MassiveFormationState *> formations;
  for (const auto &formation : battle.formations)
    if (!formations.emplace(formation.id, &formation).second)
      throw std::invalid_argument("An item with the same key has already been added.");
  std::vector<const MassiveCombatEvent *> visible_events;
  for (const auto &event : battle.events)
    if (event.actor_civilization_id == observer_civilization_id ||
        event.target_civilization_id == observer_civilization_id ||
        sensors.confidence(observer_civilization_id, event.actor_formation_id) >= .65F)
      visible_events.push_back(&event);
  const auto first_event = visible_events.size() > 128 ? visible_events.size() - 128 : 0;
  for (auto index = first_event; index < visible_events.size(); ++index) {
    const auto &event = *visible_events[index];
    const auto actor_known = event.actor_civilization_id == observer_civilization_id ||
        sensors.confidence(observer_civilization_id, event.actor_formation_id) >= .65F;
    const auto target_known = !event.target_formation_id ||
        event.target_civilization_id == observer_civilization_id ||
        sensors.confidence(observer_civilization_id, *event.target_formation_id) >= .65F;
    MassiveObservedCombatEvent observed{event.sequence, event.tick, event.type};
    if (actor_known && target_known) {
      observed.actor_civilization_id = event.actor_civilization_id;
      observed.actor_formation_id = event.actor_formation_id;
      observed.target_civilization_id = event.target_civilization_id;
      observed.target_formation_id = event.target_formation_id;
      observed.magnitude = event.magnitude;
      observed.position = event.position;
      observed.message = event.message;
      observed.details_known = true;
    } else if (actor_known) {
      observed.actor_civilization_id = event.actor_civilization_id;
      observed.actor_formation_id = event.actor_formation_id;
      observed.position = event.position;
      observed.message = "An observed formation acted against an unidentified contact.";
    } else {
      if (event.target_civilization_id == observer_civilization_id) {
        observed.target_civilization_id = event.target_civilization_id;
        observed.target_formation_id = event.target_formation_id;
      }
      observed.message = "An unidentified hostile action affected a friendly formation.";
    }
    const auto impact_type = event.type == MassiveCombatEventType::Damage ||
        event.type == MassiveCombatEventType::MissileIntercepted ||
        event.type == MassiveCombatEventType::FormationDestroyed;
    if (!(actor_known && !target_known) && impact_type) {
      const auto target = event.target_formation_id
          ? formations.find(*event.target_formation_id) : formations.end();
      const auto may_locate_target = target_known &&
          (actor_known || event.target_civilization_id == observer_civilization_id);
      if (may_locate_target && target != formations.end())
        observed.impact_position = target->second->position;
      else if (event.type == MassiveCombatEventType::MissileIntercepted && actor_known)
        observed.impact_position = event.position;
    }
    result.events.push_back(std::move(observed));
  }
  std::vector<const MassiveMissileSalvoState *> salvos;
  for (const auto &salvo : battle.active_salvos) salvos.push_back(&salvo);
  std::ranges::sort(salvos, {}, &MassiveMissileSalvoState::id);
  for (const auto *salvo : salvos) {
    const auto source = formations.find(salvo->source_formation_id);
    const auto target = formations.find(salvo->target_formation_id);
    if (source == formations.end() || target == formations.end() || !target->second->active()) continue;
    const auto source_own = source->second->civilization_id == observer_civilization_id;
    const auto target_own = target->second->civilization_id == observer_civilization_id;
    const auto source_confidence = source_own ? 1.F : std::clamp(sensors.confidence(observer_civilization_id, source->second->id), 0.F, 1.F);
    const auto target_confidence = target_own ? 1.F : std::clamp(sensors.confidence(observer_civilization_id, target->second->id), 0.F, 1.F);
    const auto source_known = source_own || source_confidence >= .65F;
    const auto target_known = target_own || target_confidence >= .65F;
    if (!source_own && !target_own && !(source_known && target_known)) continue;
    MassiveObservedMissileSalvo observed; observed.salvo_id = salvo->id;
    observed.remaining_seconds = salvo->remaining_seconds; observed.incoming_to_own = target_own;
    if (source_known) { observed.source_formation_id = source->second->id;
      observed.source_position = salvo->launch_position.value_or(source->second->position); }
    if (target_known) { observed.target_formation_id = target->second->id; observed.target_position = target->second->position; }
    if (salvo->launch_position && salvo->initial_flight_seconds > 0 && source_known && target_known) {
      const auto progress = std::clamp(1.F - salvo->remaining_seconds / salvo->initial_flight_seconds, 0.F, 1.F);
      observed.progress_01 = progress;
      const auto inverse = 1.F - progress;
      observed.current_position = MassivePoint{salvo->launch_position->x * inverse + target->second->position.x * progress,
                                                salvo->launch_position->y * inverse + target->second->position.y * progress,
                                                salvo->launch_position->z * inverse + target->second->position.z * progress};
    }
    if (source_known) { const auto spread = uncertainty(salvo->missile_count, source_own || source_confidence >= .999F, source_confidence);
      observed.count_low = std::max(0, salvo->missile_count - spread); observed.count_high = salvo->missile_count + spread; }
    result.active_missile_salvos.push_back(std::move(observed));
  }
  for (const auto &formation : battle.formations)
    if (formation.civilization_id == observer_civilization_id)
      result.exact_own_ships += formation.active_ship_count();
  return result;
}

std::vector<MassiveCombatOrder> decide_massive_combat_doctrine(
    const MassiveCombatSnapshot &snapshot, int civilization_id) {
  std::vector<const MassiveObservedFormation *> own, hostile;
  for (const auto &formation : snapshot.formations) {
    if (formation.civilization_id != civilization_id) hostile.push_back(&formation);
    else if (formation.is_exact) own.push_back(&formation);
  }
  std::ranges::sort(own, {}, [](const auto *formation) { return formation->formation_id; });
  std::ranges::sort(hostile, {}, [](const auto *formation) { return formation->formation_id; });
  std::vector<MassiveCombatOrder> orders;
  const auto interdictor = std::ranges::find_if(own, [](const auto *value) { return value->is_interdicting; });
  for (const auto *formation : own) {
    if (formation->is_warp_blocked) {
      std::vector<const MassiveObservedFormation *> hostile_interdictors;
      for (const auto *value : hostile)
        if (value->is_interdicting) hostile_interdictors.push_back(value);
      const auto source = std::ranges::min_element(hostile_interdictors, {}, [&](const auto *value) {
        const auto dx = value->position.x - formation->position.x, dy = value->position.y - formation->position.y;
        const auto dz=value->position.z-formation->position.z;return dx * dx + dy * dy + dz*dz; });
      if (source != hostile_interdictors.end())
        orders.push_back({formation->formation_id, MassiveCombatOrderType::Breakout, (*source)->formation_id, std::nullopt, MassiveFormationShape::Breakout});
      else {
        const auto squared = formation->position.x * formation->position.x + formation->position.y * formation->position.y + formation->position.z*formation->position.z;
        const auto length = std::sqrt(squared);
        const auto x = squared > .01F ? formation->position.x / length : 1.F, y = squared > .01F ? formation->position.y / length : 0.F;
        orders.push_back({formation->formation_id, MassiveCombatOrderType::EmergencyRetreat, std::nullopt,
                          MassivePoint{formation->position.x + x * 2000.F, formation->position.y + y * 2000.F, formation->position.z+(squared>.01F?formation->position.z/length*2000.F:0)}, MassiveFormationShape::RetreatColumn});
      }
    } else if (interdictor != own.end() && formation->formation_id != (*interdictor)->formation_id &&
               (formation->shape == MassiveFormationShape::Screen || formation->shape == MassiveFormationShape::Escort)) {
      orders.push_back({formation->formation_id, MassiveCombatOrderType::ProtectCriticalAsset, (*interdictor)->formation_id, std::nullopt, MassiveFormationShape::Escort});
    } else if (!hostile.empty()) {
      const auto target = std::ranges::min_element(hostile, {}, [&](const auto *value) {
        const auto dx = value->position.x - formation->position.x, dy = value->position.y - formation->position.y;
        const auto dz=value->position.z-formation->position.z;return std::pair{dx * dx + dy * dy + dz*dz, value->formation_id}; });
      orders.push_back({formation->formation_id, MassiveCombatOrderType::Engage, (*target)->formation_id});
    }
  }
  return orders;
}

} // namespace stellar::core
