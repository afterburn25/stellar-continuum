#include <stellar/core/shipyard_persistence.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
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

bool known_design(std::string_view id) {
  return !blank(id) && find_ship_design(id) != nullptr;
}

bool known_species(std::string_view id) noexcept {
  return std::ranges::any_of(species_environment_profiles(),
                             [&](const SpeciesEnvironmentProfile &profile) {
                               return profile.id == id;
                             });
}

std::string amount(double value) {
  return detail::legacy_custom_fixed(value, 0, 3);
}

std::string require_species(std::optional<std::string_view> id,
                            std::string_view owner) {
  if (!id || blank(*id) || !known_species(*id))
    throw ShipyardPersistenceDataError(
        std::string(owner) + " references unknown population species ID '" +
        (id ? std::string(*id) : std::string{}) + "'.");
  return std::string(*id);
}

std::string civilization_species(std::span<const Civilization> civilizations,
                                  int civilization_id) {
  const auto found = std::ranges::find(civilizations, civilization_id,
                                       &Civilization::id);
  if (found == civilizations.end())
    throw ShipyardPersistenceDataError(
        "Population state references unknown civilization " +
        std::to_string(civilization_id) + ".");
  return require_species(found->species_id,
                         "civilization " + std::to_string(civilization_id));
}

std::string resolve_species(
    const std::optional<std::string> &saved_species, int civilization_id,
    std::span<const Civilization> civilizations, int save_format_version,
    std::string_view owner) {
  return save_format_version < 8
             ? civilization_species(civilizations, civilization_id)
             : require_species(saved_species
                                   ? std::optional<std::string_view>{*saved_species}
                                   : std::nullopt,
                               owner);
}

void validate_identities(int civilization_id, std::int64_t next_sequence,
                         std::span<const std::string> identities) {
  std::unordered_set<std::string_view> unique;
  for (const auto &identity : identities)
    if (!is_valid_persisted_shipyard_order_id(identity) ||
        !unique.insert(identity).second)
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(civilization_id) +
          " has invalid or duplicate order identities.");
  std::int64_t maximum = 0;
  for (const auto &identity : identities) {
    std::int64_t sequence{};
    if (try_read_canonical_shipyard_sequence(identity, civilization_id,
                                             sequence))
      maximum = std::max(maximum, sequence);
  }
  if (!identities.empty() && maximum >= next_sequence)
    throw ShipyardPersistenceDataError(
        "Shipyard " + std::to_string(civilization_id) +
        " order sequence does not follow its existing identities.");
}

void validate_loaded_identity(const ShipyardState &state) {
  std::vector<std::string> identities;
  identities.reserve(state.queued_builds.size() +
                     (state.active_design_id ? 1 : 0));
  for (const auto &order : state.queued_builds)
    identities.push_back(order.order_id);
  if (state.active_design_id)
    identities.push_back(state.active_order_id.value_or(""));
  validate_identities(state.civilization_id, state.next_order_sequence,
                      identities);
}

void validate_queue_population_safety(const ShipyardState &state) {
  try {
    validate_shipyard_population_persistence_safety(state);
  } catch (const std::invalid_argument &error) {
    throw ShipyardPersistenceOperationError(error.what());
  }
}

void validate_for_capture(const ShipyardState &state) {
  if (state.next_order_sequence <= 0 ||
      !std::isfinite(state.active_build_progress) ||
      state.active_build_progress < 0 ||
      !std::isfinite(state.active_authorization_credits) ||
      state.active_authorization_credits < 0 ||
      !std::isfinite(state.reserved_population_millions) ||
      state.reserved_population_millions < 0 ||
      (state.reserved_population_source_colony_id &&
       *state.reserved_population_source_colony_id < 0))
    throw ShipyardPersistenceOperationError(
        "Shipyard " + std::to_string(state.civilization_id) +
        " has invalid order accounting and cannot be saved.");

  const auto active_known =
      state.active_design_id && known_design(*state.active_design_id);
  if (!state.active_design_id) {
    if (state.active_build_progress != 0 ||
        state.active_authorization_credits != 0 ||
        state.reserved_population_millions != 0 || state.active_order_id)
      throw ShipyardPersistenceOperationError(
          "Shipyard " + std::to_string(state.civilization_id) +
          " has active accounting without an active design.");
  } else if (!active_known &&
             (state.active_build_progress != 0 ||
              state.active_authorization_credits != 0 ||
              state.reserved_population_millions != 0 ||
              (state.active_order_id && !blank(*state.active_order_id)))) {
    throw ShipyardPersistenceOperationError(
        state.reserved_population_millions > 0
            ? "Shipyard " + std::to_string(state.civilization_id) +
                  " has reserved colonists attached to an unknown active design."
            : "Shipyard " + std::to_string(state.civilization_id) +
                  " would lose active refund metadata for an unknown design.");
  } else if (active_known &&
             state.active_build_progress >
                 get_ship_design(*state.active_design_id).industry_cost +
                     .0001) {
    throw ShipyardPersistenceOperationError(
        "Shipyard " + std::to_string(state.civilization_id) +
        " exceeds its active vessel material requirement.");
  }

  // The source's QueuedBuilds getter runs its population-safety guard at the
  // first queue access in ValidateShipyardStateForSave.
  validate_queue_population_safety(state);
  const auto queue_capacity = std::max(
      0, maximum_pending_ship_builds - (active_known ? 1 : 0));
  std::vector<const ShipBuildOrderState *> persisted;
  persisted.reserve(state.queued_builds.size());
  for (std::size_t index = 0; index < state.queued_builds.size(); ++index) {
    const auto &order = state.queued_builds[index];
    if (!std::isfinite(order.authorization_credits) ||
        order.authorization_credits < 0 ||
        !std::isfinite(order.reserved_population_millions) ||
        order.reserved_population_millions < 0 ||
        (order.reserved_population_source_colony_id &&
         *order.reserved_population_source_colony_id < 0))
      throw ShipyardPersistenceOperationError(
          "Shipyard " + std::to_string(state.civilization_id) +
          " has invalid queued order accounting.");
    const auto recoverable = order.authorization_credits != 0 ||
                             order.reserved_population_millions != 0 ||
                             !blank(order.order_id);
    if (index >= maximum_pending_ship_builds) {
      if (recoverable)
        throw ShipyardPersistenceOperationError(
            "Shipyard " + std::to_string(state.civilization_id) +
            " has queued overflow carrying paid authorization, identity, or "
            "population metadata.");
      continue;
    }
    if (!known_design(order.design_id)) {
      if (recoverable)
        throw ShipyardPersistenceOperationError(
            "Shipyard " + std::to_string(state.civilization_id) +
            " would lose queued refund metadata for an unknown design.");
      continue;
    }
    if (persisted.size() >= static_cast<std::size_t>(queue_capacity)) {
      if (recoverable)
        throw ShipyardPersistenceOperationError(
            "Shipyard " + std::to_string(state.civilization_id) +
            " has queued overflow carrying paid authorization, identity, or "
            "population metadata.");
      continue;
    }
    persisted.push_back(&order);
  }

  std::vector<std::string> identities;
  identities.reserve(persisted.size() + (active_known ? 1 : 0));
  for (std::size_t index = 0; index < persisted.size(); ++index)
    identities.push_back(blank(persisted[index]->order_id)
                             ? "legacy-" +
                                   std::to_string(state.civilization_id) +
                                   "-queued-" + std::to_string(index + 1)
                             : persisted[index]->order_id);
  if (active_known)
    identities.push_back(
        !state.active_order_id || blank(*state.active_order_id)
            ? "legacy-" + std::to_string(state.civilization_id) + "-active"
            : *state.active_order_id);
  validate_identities(state.civilization_id, state.next_order_sequence,
                      identities);
}

} // namespace

ShipyardPersistenceDataError::ShipyardPersistenceDataError(std::string message)
    : std::runtime_error(std::move(message)) {}
ShipyardPersistenceOperationError::ShipyardPersistenceOperationError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

std::vector<ShipyardState> restore_shipyard_states(
    std::span<const ShipyardPersistenceDto> source,
    std::span<const Civilization> civilizations, int save_format_version) {
  std::vector<ShipyardState> result;
  result.reserve(source.size());
  for (const auto &dto : source) {
    const auto active_order_present =
        dto.active_order_id && !blank(*dto.active_order_id);
    const auto active_species_present =
        dto.reserved_population_species_id &&
        !blank(*dto.reserved_population_species_id);
    if (!std::isfinite(dto.active_build_progress) ||
        dto.active_build_progress < 0 ||
        !std::isfinite(dto.active_authorization_credits) ||
        dto.active_authorization_credits < 0 ||
        !std::isfinite(dto.reserved_population_millions) ||
        (dto.reserved_population_millions < 0 &&
         (dto.active_authorization_credits != 0 || active_order_present ||
          dto.reserved_population_source_colony_id || active_species_present)) ||
        dto.next_order_sequence <= 0 ||
        (dto.reserved_population_source_colony_id &&
         *dto.reserved_population_source_colony_id < 0) ||
        !dto.queued_builds_present)
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(dto.civilization_id) +
          " has invalid order accounting.");
    for (const auto &build : dto.queued_builds) {
      const auto order_present = build.order_id && !blank(*build.order_id);
      const auto species_present = build.reserved_population_species_id &&
                                   !blank(*build.reserved_population_species_id);
      if (!std::isfinite(build.authorization_credits) ||
          build.authorization_credits < 0 ||
          !std::isfinite(build.reserved_population_millions) ||
          (build.reserved_population_millions < 0 &&
           (build.authorization_credits != 0 || order_present ||
            build.reserved_population_source_colony_id || species_present)) ||
          (build.reserved_population_source_colony_id &&
           *build.reserved_population_source_colony_id < 0) ||
          (order_present &&
           !is_valid_persisted_shipyard_order_id(*build.order_id)))
        throw ShipyardPersistenceDataError(
            "Shipyard " + std::to_string(dto.civilization_id) +
            " has invalid order accounting.");
    }
    if (active_order_present &&
        !is_valid_persisted_shipyard_order_id(*dto.active_order_id))
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(dto.civilization_id) +
          " has an invalid active order identity.");
    std::vector<std::string_view> explicit_ids;
    for (const auto &build : dto.queued_builds)
      if (build.order_id && !blank(*build.order_id))
        explicit_ids.push_back(*build.order_id);
    if (active_order_present)
      explicit_ids.push_back(*dto.active_order_id);
    std::unordered_set<std::string_view> unique_ids;
    for (const auto id : explicit_ids)
      if (!unique_ids.insert(id).second)
        throw ShipyardPersistenceDataError(
            "Shipyard " + std::to_string(dto.civilization_id) +
            " has duplicate order identities.");

    const auto reserved_population =
        std::max(0.0, dto.reserved_population_millions);
    auto active_design =
        dto.active_design_id && !blank(*dto.active_design_id)
            ? dto.active_design_id
            : std::nullopt;
    if (active_design && !known_design(*active_design)) {
      if (reserved_population > 0)
        throw ShipyardPersistenceDataError(
            "Shipyard " + std::to_string(dto.civilization_id) +
            " active build '" + *active_design + "' is unknown but retains " +
            amount(reserved_population) +
            " million reserved population; refusing to discard reserved "
            "colonists.");
      if (dto.active_build_progress != 0 ||
          dto.active_authorization_credits != 0 || active_order_present)
        throw ShipyardPersistenceDataError(
            "Shipyard " + std::to_string(dto.civilization_id) +
            " active build '" + *active_design +
            "' is unknown but retains refund metadata.");
      active_design.reset();
    }
    if (!active_design && reserved_population > 0)
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(dto.civilization_id) + " retains " +
          amount(reserved_population) +
          " million reserved population without a valid active design.");
    if (!active_design &&
        (dto.active_build_progress != 0 ||
         dto.active_authorization_credits != 0 || active_order_present))
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(dto.civilization_id) +
          " has active accounting without a valid active design.");
    if (active_design &&
        dto.active_build_progress >
            get_ship_design(*active_design).industry_cost + .0001)
      throw ShipyardPersistenceDataError(
          "Shipyard " + std::to_string(dto.civilization_id) +
          " exceeds its active vessel material requirement.");

    ShipyardState state;
    state.civilization_id = dto.civilization_id;
    state.next_order_sequence = dto.next_order_sequence;
    state.active_design_id = active_design;
    state.active_order_id = dto.active_order_id;
    state.active_build_progress = active_design ? dto.active_build_progress : 0;
    state.active_authorization_credits =
        std::max(0.0, dto.active_authorization_credits);
    state.reserved_population_millions = reserved_population;
    if (reserved_population > 0) {
      state.reserved_population_species_id = resolve_species(
          dto.reserved_population_species_id, dto.civilization_id,
          civilizations, save_format_version,
          "shipyard " + std::to_string(dto.civilization_id) +
              " active reservation");
      state.reserved_population_source_colony_id =
          dto.reserved_population_source_colony_id;
    }
    if (state.active_design_id &&
        (!state.active_order_id || blank(*state.active_order_id)))
      state.active_order_id = "legacy-" + std::to_string(dto.civilization_id) +
                              "-active";

    const auto queue_slots = maximum_pending_ship_builds -
                             (state.active_design_id ? 1 : 0);
    int accepted = 0;
    for (const auto &queued : dto.queued_builds) {
      const auto population =
          std::max(0.0, queued.reserved_population_millions);
      const auto design_known = known_design(queued.design_id);
      if (!design_known) {
        if (population > 0)
          throw ShipyardPersistenceDataError(
              "Shipyard " + std::to_string(dto.civilization_id) +
              " queued build '" + queued.design_id + "' is invalid but retains " +
              amount(population) +
              " million reserved population; refusing to discard reserved "
              "colonists.");
        if (queued.authorization_credits != 0 ||
            (queued.order_id && !blank(*queued.order_id)))
          throw ShipyardPersistenceDataError(
              "Shipyard " + std::to_string(dto.civilization_id) +
              " queued build '" + queued.design_id +
              "' is invalid but retains refund metadata.");
        continue;
      }
      if (accepted >= std::max(0, queue_slots)) {
        if (population > 0)
          throw ShipyardPersistenceDataError(
              "Shipyard " + std::to_string(dto.civilization_id) +
              " queue exceeds the bounded maximum while overflow build '" +
              queued.design_id + "' retains " + amount(population) +
              " million reserved population; refusing to truncate reserved "
              "colonists.");
        if (queued.authorization_credits != 0 ||
            (queued.order_id && !blank(*queued.order_id)))
          throw ShipyardPersistenceDataError(
              "Shipyard " + std::to_string(dto.civilization_id) +
              " queue overflow retains paid authorization or order identity "
              "metadata.");
        continue;
      }
      ShipBuildOrderState order;
      order.order_id = !queued.order_id || blank(*queued.order_id)
                           ? "legacy-" + std::to_string(dto.civilization_id) +
                                 "-queued-" + std::to_string(accepted + 1)
                           : *queued.order_id;
      order.design_id = queued.design_id;
      order.authorization_credits =
          std::max(0.0, queued.authorization_credits);
      order.reserved_population_millions = population;
      if (population > 0) {
        order.reserved_population_species_id = resolve_species(
            queued.reserved_population_species_id, dto.civilization_id,
            civilizations, save_format_version,
            "shipyard " + std::to_string(dto.civilization_id) +
                " queued reservation");
        order.reserved_population_source_colony_id =
            queued.reserved_population_source_colony_id;
      }
      state.queued_builds.push_back(std::move(order));
      ++accepted;
    }
    validate_loaded_identity(state);
    result.push_back(std::move(state));
  }
  return result;
}

std::vector<ShipyardPersistenceDto>
capture_shipyard_states(std::span<const ShipyardState> source) {
  std::vector<ShipyardPersistenceDto> result;
  result.reserve(source.size());
  for (const auto &state : source) {
    validate_for_capture(state);
    ShipyardPersistenceDto dto;
    dto.civilization_id = state.civilization_id;
    dto.next_order_sequence = state.next_order_sequence;
    dto.active_design_id = state.active_design_id;
    dto.active_order_id = state.active_order_id;
    dto.active_build_progress = state.active_build_progress;
    dto.active_authorization_credits = state.active_authorization_credits;
    dto.reserved_population_millions =
        std::max(0.0, state.reserved_population_millions);
    if (dto.reserved_population_millions > 0) {
      dto.reserved_population_species_id = require_species(
          state.reserved_population_species_id
              ? std::optional<std::string_view>{
                    *state.reserved_population_species_id}
              : std::nullopt,
          "shipyard " + std::to_string(state.civilization_id) +
              " active reservation");
      dto.reserved_population_source_colony_id =
          state.reserved_population_source_colony_id;
    }
    const auto count = std::min<std::size_t>(state.queued_builds.size(),
                                             maximum_pending_ship_builds);
    dto.queued_builds.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto &source_order = state.queued_builds[index];
      QueuedShipBuildPersistenceDto order;
      order.order_id = source_order.order_id;
      order.design_id = source_order.design_id;
      order.authorization_credits = source_order.authorization_credits;
      order.reserved_population_millions =
          std::max(0.0, source_order.reserved_population_millions);
      if (order.reserved_population_millions > 0) {
        order.reserved_population_species_id = require_species(
            source_order.reserved_population_species_id
                ? std::optional<std::string_view>{
                      *source_order.reserved_population_species_id}
                : std::nullopt,
            "shipyard " + std::to_string(state.civilization_id) +
                " queued reservation");
        order.reserved_population_source_colony_id =
            source_order.reserved_population_source_colony_id;
      }
      dto.queued_builds.push_back(std::move(order));
    }
    result.push_back(std::move(dto));
  }
  return result;
}

} // namespace stellar::core
