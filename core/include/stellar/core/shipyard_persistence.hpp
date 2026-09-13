#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/shipyard_state.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace stellar::core {

struct QueuedShipBuildPersistenceDto {
  std::optional<std::string> order_id;
  std::string design_id;
  double authorization_credits{};
  double reserved_population_millions{};
  std::optional<std::string> reserved_population_species_id;
  std::optional<int> reserved_population_source_colony_id;
};

struct ShipyardPersistenceDto {
  int civilization_id{};
  std::int64_t next_order_sequence{1};
  std::optional<std::string> active_design_id;
  std::optional<std::string> active_order_id;
  double active_build_progress{};
  double active_authorization_credits{};
  double reserved_population_millions{};
  std::optional<std::string> reserved_population_species_id;
  std::optional<int> reserved_population_source_colony_id;
  // Parser-facing presence bit for the source DTO's required nullable List.
  bool queued_builds_present{true};
  std::vector<QueuedShipBuildPersistenceDto> queued_builds;
};

class ShipyardPersistenceDataError final : public std::runtime_error {
public:
  explicit ShipyardPersistenceDataError(std::string message);
};

class ShipyardPersistenceOperationError final : public std::runtime_error {
public:
  explicit ShipyardPersistenceOperationError(std::string message);
};

// Reconstructs owned runtime shipyards in DTO order. Formats before 8 resolve
// reservation species from the authoritative civilization owner; format 8+
// requires the species recorded on each population-bearing order.
[[nodiscard]] std::vector<ShipyardState> restore_shipyard_states(
    std::span<const ShipyardPersistenceDto> source,
    std::span<const Civilization> civilizations,
    int save_format_version);

// Validates runtime save invariants before returning a detached, owned graph.
// Subsequent runtime mutation cannot affect the returned DTOs.
[[nodiscard]] std::vector<ShipyardPersistenceDto>
capture_shipyard_states(std::span<const ShipyardState> source);

} // namespace stellar::core
