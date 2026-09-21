#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "stellar/core/fleet_state.hpp"
#include "stellar/core/fresh_campaign.hpp"
#include "stellar/core/galaxy_catalog.hpp"
#include "stellar/engine/native_map_platform.hpp"

namespace stellar::native_overview {

// UiOwnedColonySnapshot subset rendered by the reference EMPIRE OVERVIEW
// card. All rows are the observer's own holdings — nothing foreign is read.
struct NativeOverviewColony {
  int colony_id{}, system_id{};
  std::optional<int> body_id;
  std::string planet_name{"Orbital habitat"}, system_name;
  double population_millions{};
};

// EmpireOverviewPanel (empire mode): the selected-system home reference plus
// the player's colony quick list and combined fleet power. The reference
// panel shares its slot with the ship inspector; natively the fleet
// workspace panel owns that slot, so this content renders in its detail area
// while no fleet is selected. The reference's fleet rows are covered by the
// outliner list directly above.
struct NativeEmpireOverview {
  std::string selected_system_name{"No target"},
      selected_system_distance{"Unavailable"};
  std::vector<NativeOverviewColony> colonies;
  double combined_power{};
};

[[nodiscard]] NativeEmpireOverview
build_empire_overview(const core::FreshCampaignState &campaign,
                      std::optional<int> selected_system_id);

// Reference CivilizationArtworkLibrary leadership council — three fixed
// office portraits rendered under the empire summary. The roles are static
// until an authoritative leader system exists (docs/ASSET_MANIFEST.md).
struct NativeLeaderCard {
  std::string_view role, responsibility, asset_path;
};
[[nodiscard]] std::span<const NativeLeaderCard> leadership_council() noexcept;

// Resolves an assets/-relative path to a decoded image; the fleet workspace
// owns the cache.
using OverviewImageProvider = std::function<
    std::shared_ptr<const native_map::RgbaImage>(std::string_view)>;

struct OverviewLayout {
  float scale{};
  int heading_font_pixels{}, label_font_pixels{}, body_font_pixels{},
      small_font_pixels{};
  native_map::UiRect bounds;
  // One rect per colony row — the reference button click target.
  std::vector<native_map::UiRect> colony_rows;
  // LEADERSHIP COUNCIL heading rect (zero-size when the section is clipped).
  native_map::UiRect council_heading{};
  // One rect per leadership council card (may be empty when clipped away).
  std::vector<native_map::UiRect> leader_rows;
};

// Lays the overview out inside `content` (the fleet workspace detail area).
[[nodiscard]] OverviewLayout
overview_layout_for(const NativeEmpireOverview &overview,
                    native_map::UiRect content) noexcept;

void render_empire_overview(native_map::DrawList &out,
                            const NativeEmpireOverview &overview,
                            const OverviewLayout &layout,
                            native_map::Point pointer,
                            const OverviewImageProvider *portraits = nullptr);

}  // namespace stellar::native_overview
