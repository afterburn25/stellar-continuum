#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <stellar/core/civilization_catalog.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace stellar::core {
inline constexpr int maximum_pending_ship_builds = 8,
                     maximum_shipyard_order_id_length = 128;
struct ShipBuildOrderState {
  std::string order_id, design_id;
  double authorization_credits{}, reserved_population_millions{};
  std::optional<std::string> reserved_population_species_id;
  std::optional<int> reserved_population_source_colony_id;
};
struct ShipyardState {
  int civilization_id{};
  std::int64_t next_order_sequence{1};
  std::optional<std::string> active_design_id, active_order_id;
  double active_build_progress{}, active_authorization_credits{},
      reserved_population_millions{};
  std::optional<std::string> reserved_population_species_id;
  std::optional<int> reserved_population_source_colony_id;
  std::vector<ShipBuildOrderState> queued_builds;
  int pending_build_count() const;
};
std::string format_shipyard_order_id(int civilization_id,
                                     std::int64_t sequence);
bool try_read_canonical_shipyard_sequence(
    std::optional<std::string_view> order_id, int civilization_id,
    std::int64_t &sequence);
bool is_valid_persisted_shipyard_order_id(
    std::optional<std::string_view> order_id);
void validate_shipyard_population_persistence_safety(
    const ShipyardState &state);
std::vector<ShipyardState>
seed_shipyards(std::span<const Civilization> civilizations);
} // namespace stellar::core
