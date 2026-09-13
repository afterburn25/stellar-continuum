#include <stellar/core/combat_state.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {
namespace {
template <class Range, class Predicate>
auto &first(Range &&range, Predicate predicate) {
  auto item = std::find_if(range.begin(), range.end(), predicate);
  if (item == range.end())
    throw std::out_of_range("Sequence contains no matching element");
  return *item;
}
template <class Range, class Predicate>
auto &single(Range &&range, Predicate predicate) {
  auto item = std::find_if(range.begin(), range.end(), predicate);
  if (item == range.end())
    throw std::out_of_range("Sequence contains no matching element");
  if (std::find_if(std::next(item), range.end(), predicate) != range.end())
    throw std::runtime_error(
        "Sequence contains more than one matching element");
  return *item;
}
bool dotnet_whitespace(std::uint32_t c) {
  return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0x85 || c == 0xa0 ||
         c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
         c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
bool blank(std::string_view value) {
  if (value.empty())
    return true;
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    std::uint32_t code{};
    std::size_t count{};
    if (lead < 0x80) {
      code = lead;
      count = 1;
    } else if ((lead & 0xe0) == 0xc0 && i + 1 < value.size()) {
      code = lead & 0x1f;
      count = 2;
    } else if ((lead & 0xf0) == 0xe0 && i + 2 < value.size()) {
      code = lead & 0x0f;
      count = 3;
    } else if ((lead & 0xf8) == 0xf0 && i + 3 < value.size()) {
      code = lead & 0x07;
      count = 4;
    } else
      return false;
    for (std::size_t j = 1; j < count; ++j) {
      const auto continuation = static_cast<unsigned char>(value[i + j]);
      if ((continuation & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (continuation & 0x3f);
    }
    if (!dotnet_whitespace(code))
      return false;
    i += count;
  }
  return true;
}
double math_max(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}
double math_min(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}
bool known_species(const std::optional<std::string> &id) {
  if (!id || blank(*id))
    return false;
  try {
    (void)species_environment_profile(*id);
    return true;
  } catch (const std::out_of_range &) {
    return false;
  }
}
const ShipbuildingStrategicPreference &
preference_for(ShipbuildingReadView world, int id) {
  static const ShipbuildingStrategicPreference none{};
  auto item = std::find_if(
      world.strategic_preferences.begin(), world.strategic_preferences.end(),
      [=](const auto &p) { return p.civilization_id == id; });
  return item == world.strategic_preferences.end() ? none : *item;
}
std::optional<std::string> prepare_order_id(const ShipyardState &state) {
  if (state.next_order_sequence <= 0 ||
      state.next_order_sequence == std::numeric_limits<std::int64_t>::max())
    return std::nullopt;
  validate_shipyard_population_persistence_safety(state); // QueuedBuilds getter
  std::vector<std::string> identities;
  identities.reserve(state.queued_builds.size() + 1);
  for (const auto &order : state.queued_builds)
    identities.push_back(order.order_id);
  if (state.active_design_id)
    identities.push_back(state.active_order_id.value_or(""));
  std::unordered_set<std::string> unique;
  for (const auto &id : identities)
    if (!is_valid_persisted_shipyard_order_id(id) || !unique.insert(id).second)
      throw std::invalid_argument("invalid-identities");
  for (const auto &id : identities) {
    std::int64_t sequence{};
    if (try_read_canonical_shipyard_sequence(id, state.civilization_id,
                                             sequence) &&
        sequence >= state.next_order_sequence)
      throw std::invalid_argument("counter");
  }
  auto candidate = format_shipyard_order_id(state.civilization_id,
                                            state.next_order_sequence);
  validate_shipyard_population_persistence_safety(state); // QueuedBuilds.Any
  if ((state.active_order_id && *state.active_order_id == candidate) ||
      std::any_of(
          state.queued_builds.begin(), state.queued_builds.end(),
          [&](const auto &order) { return order.order_id == candidate; }))
    throw std::invalid_argument("collision");
  return candidate;
}
bool can_promote(const ShipBuildOrderState &order, std::string &reason) {
  if (!find_ship_design(order.design_id)) {
    reason = "The next queued vessel references an unknown design and cannot "
             "be promoted.";
    return false;
  }
  if (!is_valid_persisted_shipyard_order_id(order.order_id) ||
      !std::isfinite(order.authorization_credits) ||
      order.authorization_credits < 0 ||
      !std::isfinite(order.reserved_population_millions) ||
      order.reserved_population_millions < 0) {
    reason =
        "The next queued vessel has invalid accounting and cannot be promoted.";
    return false;
  }
  return true;
}
void promote(ShipyardState &state) {
  validate_shipyard_population_persistence_safety(
      state); // QueuedBuilds.Count getter
  if (state.queued_builds.empty())
    return;
  validate_shipyard_population_persistence_safety(state); // QueuedBuilds[0]
  const auto next = state.queued_builds.front();
  validate_shipyard_population_persistence_safety(
      state); // QueuedBuilds.RemoveAt
  state.queued_builds.erase(state.queued_builds.begin());
  state.active_design_id = next.design_id;
  state.active_order_id = next.order_id;
  state.active_build_progress = 0;
  state.active_authorization_credits = next.authorization_credits;
  state.reserved_population_millions = next.reserved_population_millions;
  state.reserved_population_species_id = next.reserved_population_species_id;
  state.reserved_population_source_colony_id =
      next.reserved_population_source_colony_id;
}
bool resolve_population_return(ShipbuildingReadView world, int civilization_id,
                               double population,
                               const std::optional<std::string> &species_id,
                               const std::optional<int> &source_id,
                               const Colony *&colony, std::string &reason) {
  colony = nullptr;
  if (population <= 0)
    return true;
  if (source_id) {
    auto item =
        std::find_if(world.colonies.begin(), world.colonies.end(),
                     [=](const Colony &c) { return c.id == *source_id; });
    if (item != world.colonies.end())
      colony = &*item;
  }
  if (!colony || colony->civilization_id != civilization_id || !species_id ||
      colony->population_species_id != *species_id) {
    reason = "Reserved colonists cannot be returned because their original "
             "colony is no longer a valid owned source.";
    return false;
  }
  if (!std::isfinite(colony->population_millions) ||
      colony->population_millions >
          std::numeric_limits<double>::max() - population) {
    reason = "Reserved colonists cannot be returned because their original "
             "colony has invalid population accounting.";
    return false;
  }
  return true;
}
double resolve_budget(
    std::optional<std::span<const ConstructionIndustryBudget>> budgets, int id,
    double available) {
  if (!budgets)
    return available;
  auto item =
      std::find_if(budgets->begin(), budgets->end(),
                   [=](const auto &b) { return b.civilization_id == id; });
  if (item == budgets->end())
    return 0;
  if (!std::isfinite(item->industry))
    throw std::out_of_range(
        "Industry budgets must be finite. (Parameter 'industryBudgets')");
  return math_min(available, math_max(0, item->industry));
}
const ShipDesignDefinition *select_ai_design(ShipbuildingReadView world,
                                             const Civilization &civilization) {
  auto available = available_ship_designs(world.designs(), civilization.id);
  if (available.empty())
    return nullptr;
  const auto pref = preference_for(world, civilization.id);
  auto active = [&](FleetRole role, bool populated = false) {
    return std::any_of(
        world.fleets.begin(), world.fleets.end(), [&](const FleetState &f) {
          return f.is_active && f.civilization_id == civilization.id &&
                 f.role == role &&
                 (!populated || f.embarked_population_millions > 0);
        });
  };
  bool need = false;
  if (pref.preferred_new_fleet_role)
    switch (*pref.preferred_new_fleet_role) {
    case FleetRole::Scout:
      need = !active(FleetRole::Scout);
      break;
    case FleetRole::Science:
      need = !active(FleetRole::Science);
      break;
    case FleetRole::Military:
      need = !active(FleetRole::Military);
      break;
    case FleetRole::Colony:
      need = !pref.defer_new_colonization && civilization.expansion_allowed &&
             !active(FleetRole::Colony, true);
      break;
    case FleetRole::Logistics:
      break;
    }
  if (need)
    for (const auto &d : available)
      if (d.role == *pref.preferred_new_fleet_role)
        return find_ship_design(d.id);
  if (!active(FleetRole::Scout))
    for (const auto &d : available)
      if (d.role == FleetRole::Scout)
        return find_ship_design(d.id);
  if (civilization.traits.scientific_curiosity >= .60 &&
      !active(FleetRole::Science))
    for (const auto &d : available)
      if (d.role == FleetRole::Science)
        return find_ship_design(d.id);
  if (!pref.defer_new_colonization && civilization.expansion_allowed &&
      !active(FleetRole::Colony, true))
    for (const auto &d : available)
      if (d.role == FleetRole::Colony)
        return find_ship_design(d.id);
  return nullptr;
}
FleetState create_fleet(ShipbuildingReadView world,
                        const Civilization &civilization,
                        const ShipDesignDefinition &design, double population,
                        const std::optional<std::string> &species) {
  const auto propulsion =
      effective_ship_propulsion(world.designs(), civilization.id, design);
  const auto &home = first(world.systems, [&](const StellarSystem &s) {
    return s.id == civilization.home_system_id;
  });
  int next_id = 0;
  if (!world.fleets.empty()) {
    const auto max_id =
        std::max_element(
            world.fleets.begin(), world.fleets.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; })
            ->id;
    if (max_id == std::numeric_limits<int>::max())
      throw std::overflow_error("The fleet identity space is exhausted.");
    next_id = max_id + 1;
  }
  const auto count =
      std::count_if(world.fleets.begin(), world.fleets.end(),
                    [&](const FleetState &f) {
                      return f.civilization_id == civilization.id &&
                             f.role == design.role;
                    }) +
      1;
  std::string name;
  switch (design.role) {
  case FleetRole::Scout:
    name =
        civilization.is_player ? "Pathfinder " : civilization.name + " Scout ";
    break;
  case FleetRole::Science:
    name =
        civilization.is_player ? "Discovery " : civilization.name + " Science ";
    break;
  case FleetRole::Colony:
    if (design.id == "resource_outpost_ship")
      name = civilization.is_player ? "Prospector "
                                    : civilization.name + " Prospector ";
    else
      name =
          civilization.is_player ? "Pioneer " : civilization.name + " Pioneer ";
    break;
  case FleetRole::Military:
    name =
        civilization.is_player ? "Sentinel " : civilization.name + " Patrol ";
    break;
  case FleetRole::Logistics:
    name = civilization.is_player ? "Lifeline "
                                  : civilization.name + " Freighter ";
    break;
  default:
    name = civilization.name + " Vessel ";
    break;
  }
  name += std::to_string(count);
  const bool populated = design.role == FleetRole::Colony && population > 0;
  if (populated && !known_species(species))
    throw std::invalid_argument(
        "A populated colony ship must carry a known species identity.");
  FleetState fleet;
  fleet.id = next_id;
  fleet.civilization_id = civilization.id;
  fleet.name = std::move(name);
  fleet.role = design.role;
  fleet.design_id = design.id;
  fleet.position = {home.position.x, home.position.y};
  fleet.current_system_id = home.id;
  fleet.strategic_speed = propulsion.strategic_speed;
  fleet.maximum_leg_range_light_years =
      propulsion.maximum_leg_range_light_years;
  fleet.fuel_capacity_light_years = propulsion.fuel_endurance_light_years;
  fleet.fuel_remaining_light_years = propulsion.fuel_endurance_light_years;
  fleet.sensor_range = design.sensor_range;
  fleet.cargo_material_capacity = design.cargo_material_capacity;
  fleet.is_active = true;
  fleet.embarked_population_millions = populated ? math_max(0, population) : 0;
  fleet.embarked_population_species_id = populated ? species : std::nullopt;
  fleet.combat = create_initial_fleet_combat_state(
      design.combat_profile_id
          ? std::optional<std::string_view>(*design.combat_profile_id)
          : std::nullopt,
      design.role);
  return fleet;
}
std::vector<ShipbuildingEvent>
advance_core(ShipbuildingWorld world,
             std::optional<std::span<const ConstructionIndustryBudget>> budgets,
             std::optional<int> only, double days) {
  if (!std::isfinite(days) || days < 0)
    throw std::out_of_range("Specified argument was out of the range of valid "
                            "values. (Parameter 'simulationDays')");
  if (days == 0)
    return {};
  if (!only)
    ensure_automatic_ship_orders(world);
  std::vector<ShipbuildingEvent> events;
  for (const auto &civilization : world.civilizations) {
    if (only && civilization.id != *only || civilization.is_seeded_ancient)
      continue;
    auto &state = first(world.shipyards, [&](const auto &s) {
      return s.civilization_id == civilization.id;
    });
    if (!state.active_design_id)
      continue;
    const auto &design = get_ship_design(*state.active_design_id);
    auto &economy = first(world.economies, [&](const auto &e) {
      return e.civilization_id == civilization.id;
    });
    const double remaining =
        math_max(0, design.industry_cost - state.active_build_progress);
    const double available =
        resolve_budget(budgets, civilization.id, economy.industry);
    const double spend = math_min(
        remaining, math_min(available, shipbuilding_industry_per_day * days));
    if (spend <= 0 && remaining > .0001)
      continue;
    economy.industry -= spend;
    state.active_build_progress += spend;
    if (state.active_build_progress + .0001 < design.industry_cost)
      continue;
    const auto fleet = create_fleet(world.read(), civilization, design,
                                    state.reserved_population_millions,
                                    state.reserved_population_species_id);
    world.fleets.push_back(fleet);
    state.active_design_id.reset();
    state.active_order_id.reset();
    state.active_build_progress = 0;
    state.active_authorization_credits = 0;
    state.reserved_population_millions = 0;
    state.reserved_population_species_id.reset();
    state.reserved_population_source_colony_id.reset();
    promote(state);
    events.push_back({civilization.id, fleet.id, design.id,
                      civilization.name + " completed " + fleet.name + "."});
  }
  return events;
}
} // namespace

ShipbuildingOrderResult start_ship_build(ShipbuildingWorld world,
                                         int civilization_id,
                                         std::string_view design_id) {
  auto civ =
      std::find_if(world.civilizations.begin(), world.civilizations.end(),
                   [=](const auto &c) { return c.id == civilization_id; });
  if (civ == world.civilizations.end())
    return {false, "Unknown civilization."};
  auto &state = first(world.shipyards, [=](const auto &s) {
    return s.civilization_id == civilization_id;
  });
  if (state.pending_build_count() >= maximum_pending_ship_builds)
    return {false, "The shipyard queue is full (8 pending vessels maximum)."};
  const auto *design = find_ship_design(design_id);
  if (!design)
    return {false, "Unknown ship design."};
  if (auto reason = ship_design_lock_reason(world.read().designs(),
                                            civilization_id, *design))
    return {false, design->name + " " + *reason + "."};
  auto &economy = first(world.economies, [=](const auto &e) {
    return e.civilization_id == civilization_id;
  });
  const auto currency =
      sovereign_currency_for_civilization(world.civilizations, civilization_id);
  if (!std::isfinite(economy.credits) ||
      economy.credits + .0001 < design->credit_cost)
    return {false, currency.format(design->credit_cost) +
                       " is required to authorize " + design->name + "."};
  std::optional<std::string> order_id;
  try {
    order_id = prepare_order_id(state);
  } catch (const std::invalid_argument &e) {
    const std::string_view kind = e.what();
    if (kind == "invalid-identities")
      return {
          false,
          "This shipyard has invalid or duplicate vessel order identities."};
    if (kind == "counter")
      return {false, "This shipyard's order counter does not follow its "
                     "existing vessel identities."};
    if (kind == "collision")
      return {false, "This shipyard's next order identity collides with an "
                     "existing vessel order."};
    throw;
  }
  if (!order_id)
    return {false,
            "This shipyard cannot allocate another stable order identity."};
  double population = 0;
  std::optional<std::string> species;
  std::optional<int> source_id;
  Colony *source = nullptr;
  if (design->population_cost_millions > 0) {
    for (auto &colony : world.colonies)
      if (colony.civilization_id == civilization_id &&
          (!source ||
           (std::isnan(source->population_millions) &&
            !std::isnan(colony.population_millions)) ||
           colony.population_millions > source->population_millions))
        source = &colony;
    if (!source || !std::isfinite(source->population_millions) ||
        source->population_millions < design->population_cost_millions + 500)
      return {false, "At least " +
                         std::to_string(static_cast<int>(
                             design->population_cost_millions + 500)) +
                         " million population is required before reserving "
                         "colonists for this ship."};
    species = source->population_species_id;
    if (!known_species(species))
      return {false,
              "The source colony references unknown population species '" +
                  *species + "'."};
    population = design->population_cost_millions;
    source_id = source->id;
  }
  economy.credits -= design->credit_cost;
  if (source_id)
    first(world.colonies, [&](const Colony &colony) {
      return colony.id == *source_id;
    }).population_millions -= population;
  if (!state.active_design_id) {
    state.active_design_id = design->id;
    state.active_order_id = *order_id;
    state.active_build_progress = 0;
    state.active_authorization_credits = design->credit_cost;
    state.reserved_population_millions = population;
    state.reserved_population_species_id = species;
    state.reserved_population_source_colony_id = source_id;
    ++state.next_order_sequence;
    return {true, "Ship construction started: " + design->name +
                      ". Authorized for " +
                      currency.format(design->credit_cost) + "."};
  }
  validate_shipyard_population_persistence_safety(
      state); // QueuedBuilds.Add getter
  state.queued_builds.push_back({*order_id, design->id, design->credit_cost,
                                 population, species, source_id});
  ++state.next_order_sequence;
  return {true, "Queued " + design->name + " for " +
                    currency.format(design->credit_cost) + ". " +
                    std::to_string(state.pending_build_count()) +
                    "/8 pending vessel slots are now in use."};
}

ShipbuildingCancellationAssessment
assess_ship_build_cancellation(ShipbuildingReadView world, int civilization_id,
                               std::string_view order_id) {
  auto state = std::find_if(
      world.shipyards.begin(), world.shipyards.end(),
      [=](const auto &s) { return s.civilization_id == civilization_id; });
  auto economy = std::find_if(
      world.economies.begin(), world.economies.end(),
      [=](const auto &e) { return e.civilization_id == civilization_id; });
  if (state == world.shipyards.end() || economy == world.economies.end() ||
      blank(order_id))
    return {false, false, std::nullopt, 0, "Unknown shipyard order."};
  const bool active = state->active_order_id &&
                      *state->active_order_id == order_id &&
                      state->active_design_id.has_value();
  validate_shipyard_population_persistence_safety(
      *state); // QueuedBuilds getter
  std::vector<const ShipBuildOrderState *> queued;
  for (const auto &q : state->queued_builds)
    if (q.order_id == order_id)
      queued.push_back(&q);
  if ((active ? 1 : 0) + queued.size() == 0)
    return {false, false, std::nullopt, 0,
            "That shipyard order is no longer pending."};
  if ((active ? 1 : 0) + queued.size() != 1)
    return {false, false, std::nullopt, 0,
            "Shipyard order identity is ambiguous; repair the save before "
            "cancelling."};
  const std::string design_id =
      active ? *state->active_design_id : queued[0]->design_id;
  const auto *design = find_ship_design(design_id);
  if (!design)
    return {false, active, design_id, 0,
            "The shipyard order references an unknown design."};
  const double quote = active ? state->active_authorization_credits
                              : queued[0]->authorization_credits;
  const double progress = active ? state->active_build_progress : 0;
  const double population = active ? state->reserved_population_millions
                                   : queued[0]->reserved_population_millions;
  const auto &species = active ? state->reserved_population_species_id
                               : queued[0]->reserved_population_species_id;
  const auto &source = active ? state->reserved_population_source_colony_id
                              : queued[0]->reserved_population_source_colony_id;
  if (!std::isfinite(quote) || quote < 0 || !std::isfinite(progress) ||
      progress < 0 || !std::isfinite(population) || population < 0)
    return {false, active, design_id, 0,
            "The shipyard order has invalid accounting data."};
  if (active && progress > design->industry_cost + .0001)
    return {
        false, true, design_id, 0,
        "The shipyard order records more progress than the design requires."};
  const Colony *colony{};
  std::string reason;
  if (!resolve_population_return(world, civilization_id, population, species,
                                 source, colony, reason))
    return {false, active, design_id, 0, reason};
  const double refund =
      active ? quote * std::clamp((design->industry_cost - progress) /
                                      design->industry_cost,
                                  0., 1.)
             : quote;
  if (!std::isfinite(economy->credits) || !std::isfinite(refund) ||
      economy->credits > std::numeric_limits<double>::max() - refund)
    return {false, active, design_id, 0,
            "The refund cannot be represented in the civilization treasury."};
  validate_shipyard_population_persistence_safety(
      *state); // QueuedBuilds.Count getter
  if (active && !state->queued_builds.empty()) {
    validate_shipyard_population_persistence_safety(*state); // QueuedBuilds[0]
    if (!can_promote(state->queued_builds.front(), reason))
      return {false, true, design_id, 0, reason};
  }
  return {true, active, design_id, refund, std::nullopt};
}

ShipbuildingCancellationResult cancel_ship_build(ShipbuildingWorld world,
                                                 int civilization_id,
                                                 std::string_view order_id) {
  const auto assessment =
      assess_ship_build_cancellation(world.read(), civilization_id, order_id);
  if (!assessment.can_cancel)
    return {false, assessment.blocker.value_or("Unknown shipyard order."), 0};
  auto &state = single(world.shipyards, [=](const auto &s) {
    return s.civilization_id == civilization_id;
  });
  auto &economy = single(world.economies, [=](const auto &e) {
    return e.civilization_id == civilization_id;
  });
  const auto &design = get_ship_design(*assessment.design_id);
  const auto currency =
      sovereign_currency_for_civilization(world.civilizations, civilization_id);
  if (assessment.is_active) {
    const Colony *found{};
    std::string reason;
    resolve_population_return(
        world.read(), civilization_id, state.reserved_population_millions,
        state.reserved_population_species_id,
        state.reserved_population_source_colony_id, found, reason);
    if (found)
      first(world.colonies, [&](const Colony &c) {
        return &c == found;
      }).population_millions += state.reserved_population_millions;
    economy.credits += assessment.refund_credits;
    state.active_design_id.reset();
    state.active_order_id.reset();
    state.active_build_progress = 0;
    state.active_authorization_credits = 0;
    state.reserved_population_millions = 0;
    state.reserved_population_species_id.reset();
    state.reserved_population_source_colony_id.reset();
    promote(state);
    return {true,
            "Cancelled " + design.name + "; refunded " +
                currency.format(assessment.refund_credits) +
                ". Consumed materials are not refunded.",
            assessment.refund_credits};
  }
  validate_shipyard_population_persistence_safety(
      state); // QueuedBuilds.FindIndex getter
  auto item =
      std::find_if(state.queued_builds.begin(), state.queued_builds.end(),
                   [&](const auto &q) { return q.order_id == order_id; });
  validate_shipyard_population_persistence_safety(state); // QueuedBuilds[index]
  auto queued = *item;
  const Colony *found{};
  std::string reason;
  resolve_population_return(
      world.read(), civilization_id, queued.reserved_population_millions,
      queued.reserved_population_species_id,
      queued.reserved_population_source_colony_id, found, reason);
  if (found)
    first(world.colonies, [&](const Colony &c) {
      return &c == found;
    }).population_millions += queued.reserved_population_millions;
  validate_shipyard_population_persistence_safety(
      state); // QueuedBuilds.RemoveAt
  state.queued_builds.erase(item);
  economy.credits += assessment.refund_credits;
  return {true,
          "Cancelled queued " + design.name + "; refunded " +
              currency.format(assessment.refund_credits) + ".",
          assessment.refund_credits};
}

double shipbuilding_industry_demand(ShipbuildingReadView world,
                                    int civilization_id, double days) {
  const auto &state = first(world.shipyards, [=](const auto &s) {
    return s.civilization_id == civilization_id;
  });
  if (!state.active_design_id)
    return 0;
  const auto &design = get_ship_design(*state.active_design_id);
  return math_min(
      math_max(0, design.industry_cost - state.active_build_progress),
      shipbuilding_industry_per_day * math_max(0, days));
}
void ensure_automatic_ship_orders(ShipbuildingWorld world) {
  for (const auto &civilization : world.civilizations) {
    if (civilization.is_seeded_ancient || civilization.is_player)
      continue;
    auto &state = first(world.shipyards, [&](const auto &s) {
      return s.civilization_id == civilization.id;
    });
    if (state.active_design_id)
      continue;
    if (const auto *design = select_ai_design(world.read(), civilization))
      (void)start_ship_build(world, civilization.id, design->id);
  }
}
std::vector<ShipbuildingEvent> advance_shipbuilding(
    ShipbuildingWorld world,
    std::optional<std::span<const ConstructionIndustryBudget>> budgets,
    double days) {
  return advance_core(world, budgets, std::nullopt, days);
}
std::vector<ShipbuildingEvent> advance_shipbuilding_for_civilization(
    ShipbuildingWorld world, int civilization_id, double budget, double days) {
  const ConstructionIndustryBudget item{civilization_id, budget};
  return advance_core(world,
                      std::span<const ConstructionIndustryBudget>(&item, 1),
                      civilization_id, days);
}
} // namespace stellar::core
