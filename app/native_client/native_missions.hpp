#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "stellar/core/colony_economy.hpp"
#include "stellar/core/fleet_state.hpp"
#include "stellar/core/fresh_campaign.hpp"
#include "stellar/engine/native_map_platform.hpp"
#include "native_settlement_mission_controller.hpp"

namespace stellar::native_missions {

// Reference ExplorationMissionPhase.
enum class NativeMissionPhase {
  awaiting_order,
  traveling,
  scouting,
  science_survey,
  establishing_colony
};

struct NativeMissionCard {
  int fleet_id{};
  core::FleetRole role{};
  NativeMissionPhase phase{};
  std::string fleet_name, destination, eta, summary;
};

// Reference UiExplorationMissions: ActiveMissions.Take(8) — owned, active
// scout/science/colony fleets carrying observer-safe status text.
struct NativeMissionBoard {
  std::vector<NativeMissionCard> missions;
};

[[nodiscard]] NativeMissionBoard
build_mission_board(const core::FreshCampaignState &campaign);

[[nodiscard]] std::string_view
mission_phase_label(NativeMissionPhase phase) noexcept;
// Reference ExplorationMissionPanel.MissionColor — keyed on the phase label,
// so only "Traveling" and "Science survey" tint; every other label is gold.
[[nodiscard]] native_map::Color mission_phase_color(NativeMissionPhase) noexcept;

// Reference ColonyOpportunityUiState — the Colony Sites tab's per-fleet
// bounded site browser state.
struct NativeColonySiteSelection {
  bool available{};
  int fleet_index{}, fleet_count{}, site_index{}, site_count{};
  std::optional<int> fleet_id, site_system_id, site_body_id;
  bool can_settle{}, outpost{};
  std::string details, status, action_label;
};

[[nodiscard]] NativeColonySiteSelection colony_site_selection(
    std::span<const native_colony::NativeSettlementMissionView> fleets,
    int requested_fleet_index, int requested_site_index);

// Reference UiOwnedColonies card row (bounded fields — the full snapshot
// stays inside the colony workspace once the card's View action opens it).
struct NativeMissionColonyRow {
  int colony_id{}, planetary_body_id{};
  std::string name, planet_name, system_name;
  double population_millions{};
  // Reference UiOwnedColonySnapshot: the Land affordance requires a solid
  // surface and the outpost Collect action is gated by freight availability.
  bool is_resource_outpost{}, can_land{};
  bool can_request_freight{};
  std::string freight_reason;
};

[[nodiscard]] std::vector<NativeMissionColonyRow>
build_owned_colony_rows(const core::FreshCampaignState &campaign);

// Reference FindAvailableFreighter: the lowest-id idle player bulk freighter
// stationed at one of the player's developed colonies.
[[nodiscard]] const core::FleetState *
find_available_freighter(const core::FreshCampaignState &campaign);

struct MissionLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, close_button, empty_hint;
  native_map::UiRect missions_tab, sites_tab;
  native_map::UiRect previous_fleet, next_fleet, previous_site, next_site,
      select_ship;
  native_map::UiRect details, action_status;
  std::vector<native_map::UiRect> cards;
  std::vector<native_map::UiRect> colony_rows, colony_view_buttons,
      colony_land_buttons, colony_collect_buttons;
};

[[nodiscard]] MissionLayout
mission_layout_for(const NativeMissionBoard &board,
                   const NativeColonySiteSelection &selection,
                   std::size_t colony_rows, int width, int height,
                   bool show_sites);

enum class MissionViewCommandKind {
  None, Close, FocusFleet, OpenColony, LandColony, CollectOutpostFreight
};

struct MissionViewCommand {
  MissionViewCommandKind kind{MissionViewCommandKind::None};
  bool captured{};
  int fleet_id{-1}, colony_id{-1};
};

// A focusable control inside the panel — actionable buttons only (disabled
// pagers and display-only mission cards never join the ring), (y,x) ordered.
struct MissionFocusTarget {
  native_map::UiRect bounds;
  std::string label;
};

[[nodiscard]] std::vector<MissionFocusTarget> mission_focus_targets(
    const MissionLayout &layout, const NativeColonySiteSelection &selection,
    bool show_sites, std::span<const NativeMissionColonyRow> colonies);

// Toggleable MISSIONS & SETTLEMENT panel (reference ExplorationMissionPanel's
// missions tab). Mission cards are display-only, matching the reference.
class NativeMissionView final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void open() noexcept { visible_ = true; focus_ = -1; }
  void close() noexcept { visible_ = false; focus_ = -1; }
  void toggle() noexcept { visible_ = !visible_; focus_ = -1; }

  // Keyboard focus contract: -1 until a nav key arms the ring; the label and
  // bounds of the ringed control feed the accessibility announcer.
  [[nodiscard]] int focus() const noexcept { return focus_; }
  [[nodiscard]] std::string focused_label(
      const NativeMissionBoard &board,
      std::span<const native_colony::NativeSettlementMissionView> fleets,
      std::span<const NativeMissionColonyRow> colonies, int width,
      int height) const;
  [[nodiscard]] std::optional<native_map::UiRect> focused_bounds(
      const NativeMissionBoard &board,
      std::span<const native_colony::NativeSettlementMissionView> fleets,
      std::span<const NativeMissionColonyRow> colonies, int width,
      int height) const;

  [[nodiscard]] MissionViewCommand handle(
      const native_map::InputEvent &event, const NativeMissionBoard &board,
      std::span<const native_colony::NativeSettlementMissionView> fleets,
      std::span<const NativeMissionColonyRow> colonies, int width, int height);
  void render(native_map::DrawList &out, const NativeMissionBoard &board,
              std::span<const native_colony::NativeSettlementMissionView>
                  fleets,
              std::span<const NativeMissionColonyRow> colonies, int width,
              int height) const;

 private:
  bool visible_{}, show_sites_{};
  int fleet_index_{}, site_index_{};
  int focus_{-1};
};

}  // namespace stellar::native_missions
