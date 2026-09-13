#include <stellar/core/shipyard_state.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/ship_designs.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
bool known_design(const std::optional<std::string> &id) {
  return id && find_ship_design(*id) != nullptr;
}

bool numeric_whitespace(char character) {
  return character == ' ' || (character >= static_cast<char>(0x09) &&
                              character <= static_cast<char>(0x0d));
}

std::string amount(double value) {
  return detail::legacy_custom_fixed(value, 0, 3);
}
} // namespace

int ShipyardState::pending_build_count() const {
  return (active_design_id ? 1 : 0) + static_cast<int>(queued_builds.size());
}

std::string format_shipyard_order_id(int civilization_id,
                                     std::int64_t sequence) {
  return "shipyard-" + std::to_string(civilization_id) + "-" +
         std::to_string(sequence);
}

bool try_read_canonical_shipyard_sequence(
    std::optional<std::string_view> order_id, int civilization_id,
    std::int64_t &sequence) {
  sequence = 0;
  const auto prefix = "shipyard-" + std::to_string(civilization_id) + "-";
  if (!order_id || !order_id->starts_with(prefix))
    return false;

  auto suffix = order_id->substr(prefix.size());
  while (!suffix.empty() && numeric_whitespace(suffix.front()))
    suffix.remove_prefix(1);
  if (suffix.empty())
    return false;

  if (suffix.front() == '+') {
    suffix.remove_prefix(1);
    if (suffix.empty() || suffix.front() < '0' || suffix.front() > '9')
      return false;
  }

  std::int64_t parsed{};
  const auto *first = suffix.data();
  const auto *last = first + suffix.size();
  const auto result = std::from_chars(first, last, parsed);
  if (result.ec != std::errc{})
    return false;
  auto *remainder = result.ptr;
  while (remainder != last && numeric_whitespace(*remainder))
    ++remainder;
  if (std::any_of(remainder, last,
                  [](char character) { return character != '\0'; }))
    return false;
  sequence = parsed;
  return sequence > 0;
}

bool is_valid_persisted_shipyard_order_id(
    std::optional<std::string_view> order_id) {
  if (!order_id || order_id->empty() ||
      order_id->size() > maximum_shipyard_order_id_length)
    return false;
  return std::all_of(order_id->begin(), order_id->end(), [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_';
  });
}

void validate_shipyard_population_persistence_safety(
    const ShipyardState &state) {
  if (!std::isfinite(state.reserved_population_millions))
    throw std::invalid_argument(
        "Shipyard " + std::to_string(state.civilization_id) +
        " has non-finite active reserved population; refusing to persist "
        "ambiguous colonist state.");

  const auto active_population =
      std::max(0.0, state.reserved_population_millions);
  const auto has_known_active_design = known_design(state.active_design_id);
  if (active_population > 0 && !has_known_active_design)
    throw std::invalid_argument(
        "Shipyard " + std::to_string(state.civilization_id) + " has " +
        amount(active_population) +
        " million reserved population without a valid active design; refusing "
        "a state transition that could discard reserved colonists.");

  const auto loader_queue_capacity = std::max(
      0, maximum_pending_ship_builds - (has_known_active_design ? 1 : 0));
  int accepted_queue_entries = 0;
  for (std::size_t index = 0; index < state.queued_builds.size(); ++index) {
    const auto &build = state.queued_builds[index];
    if (!std::isfinite(build.reserved_population_millions))
      throw std::invalid_argument(
          "Shipyard " + std::to_string(state.civilization_id) +
          " queued build '" + build.design_id +
          "' has non-finite reserved population; refusing to persist "
          "ambiguous colonist state.");

    const auto population = std::max(0.0, build.reserved_population_millions);
    if (index >= maximum_pending_ship_builds) {
      if (population > 0)
        throw std::invalid_argument(
            "Shipyard " + std::to_string(state.civilization_id) +
            " has an unserialized overflow build '" + build.design_id +
            "' retaining " + amount(population) +
            " million reserved population; refusing to truncate reserved "
            "colonists.");
      continue;
    }

    if (!find_ship_design(build.design_id)) {
      if (population > 0)
        throw std::invalid_argument(
            "Shipyard " + std::to_string(state.civilization_id) +
            " queued build '" + build.design_id + "' has " +
            amount(population) +
            " million reserved population but no valid design; refusing to "
            "discard reserved colonists.");
      continue;
    }

    if (accepted_queue_entries >= loader_queue_capacity) {
      if (population > 0)
        throw std::invalid_argument(
            "Shipyard " + std::to_string(state.civilization_id) +
            " queue exceeds its bounded capacity while overflow build '" +
            build.design_id + "' retains " + amount(population) +
            " million reserved population; refusing to truncate reserved "
            "colonists.");
      continue;
    }
    ++accepted_queue_entries;
  }
}

std::vector<ShipyardState>
seed_shipyards(std::span<const Civilization> civilizations) {
  std::vector<ShipyardState> result;
  result.reserve(civilizations.size());
  for (const auto &civilization : civilizations)
    result.push_back({.civilization_id = civilization.id});
  return result;
}
} // namespace stellar::core
