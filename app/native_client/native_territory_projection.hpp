#pragma once

#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace stellar::native_territory {

// Pure presentation geometry ported from Game.Presentation
// StrategicTerritoryProjection. Every input is already observer-filtered:
// the projection only reads observer knowledge, public settlement records and
// the DiplomaticStateView claim list, so it can never reveal contacts,
// ownership or claims hidden from the observing civilization.

enum class NativeTerritoryAnchorKind { home, settlement };

struct NativeTerritoryAnchor {
  int civilization_id{};
  int system_id{};
  native_map::Point position{};
  NativeTerritoryAnchorKind kind{};
};

struct NativeTerritoryFillRun {
  native_map::Point position, size;
};

struct NativeTerritoryRegion {
  int civilization_id{};
  std::string civilization_name;
  std::vector<NativeTerritoryAnchor> anchors;
  native_map::Point label_position{};
  std::vector<NativeTerritoryFillRun> fill_runs;
  std::vector<std::vector<native_map::Point>> fill_polygons;
  std::vector<std::vector<native_map::Point>> contours;
};

struct NativeTerritoryClaimOutline {
  int civilization_id{};
  int system_id{};
  native_map::Point position{};
  float radius{};
};

struct NativeTerritoryFogMask {
  native_map::Point position, size;
  int width{}, height{};
  std::vector<std::uint8_t> alpha;
};

struct NativeTerritoryProjection {
  std::vector<NativeTerritoryRegion> territories;
  std::vector<NativeTerritoryClaimOutline> claims;
  NativeTerritoryFogMask fog;
  std::unordered_set<int> unexplored_system_ids;
  int unowned_cell_count{};
  int grid_cell_count{};
};

// Detached, observer-filtered presentation input. It owns only catalog
// coordinates, observer knowledge bits, and already-visible territory data;
// it deliberately contains no Core references or simulation objects.
struct NativeTerritorySystemInput {
  int id{};
  native_map::Point position{};
  bool known{};
};
struct NativeTerritoryRegionInput {
  int civilization_id{};
  std::string civilization_name;
  std::vector<NativeTerritoryAnchor> anchors;
};
struct NativeTerritoryClaimInput {
  int civilization_id{};
  int system_id{};
};
struct NativeTerritoryInput {
  int observer_civilization_id{};
  float coordinate_scale{};
  std::vector<NativeTerritorySystemInput> systems;
  std::vector<NativeTerritoryRegionInput> regions;
  std::vector<NativeTerritoryClaimInput> claims;
};

[[nodiscard]] NativeTerritoryInput capture_native_territory_input(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale);

[[nodiscard]] NativeTerritoryProjection build_native_territory_projection(
    const NativeTerritoryInput &input);
[[nodiscard]] std::uint64_t native_territory_fingerprint(
    const NativeTerritoryInput &input);

// Builds the projection in scaled presentation space exactly like the C#
// reference: positions are multiplied by coordinate_scale (14 for the full
// galaxy map) before grid sizing so every ported constant keeps its meaning.
[[nodiscard]] NativeTerritoryProjection build_native_territory_projection(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale);

// Cheap change detector for the cached overlay. Hashes every input the
// projection reads; a change means the projection must be rebuilt.
[[nodiscard]] std::uint64_t native_territory_fingerprint(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale);

// Overview blend and detail factor ported from SpatialNavigationLayout and
// Main.StrategicTerritory. scale/fitted_scale are the C# zoom values, i.e.
// pixels per scaled world unit (native pixels_per_world / coordinate_scale).
[[nodiscard]] float native_territory_overview_blend(float scale,
                                                    float fitted_scale);
[[nodiscard]] float native_territory_detail(float overview_blend);

// TerritoryColor from Main.StrategicTerritory.cs / VisualPalette.cs.
[[nodiscard]] native_map::Color
native_territory_color(int civilization_id, int player_civilization_id);

} // namespace stellar::native_territory
