#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fleet_power_observation.hpp>
#include <stellar/core/own_combat_fleet_status.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::native_fleet {

// Reference FleetRole civilian set: hold/resume and return-to-base orders.
[[nodiscard]] inline bool
is_civilian_role(stellar::core::FleetRole role) noexcept {
  return role == stellar::core::FleetRole::Scout ||
         role == stellar::core::FleetRole::Science ||
         role == stellar::core::FleetRole::Colony;
}

struct NativeOwnFleet {
  int id{};
  std::string name;
  stellar::core::FleetRole role{};
  std::optional<std::string> design_id;
  stellar::core::Vec2 position{};
  std::optional<int> current_system_id;
  std::optional<int> destination_system_id;
  stellar::core::FleetTransitPhase transit_phase{};
  double transit_progress{};
  std::vector<int> planned_route_system_ids;
  double strategic_speed{};
  double maximum_leg_range_light_years{};
  double fuel_capacity_light_years{};
  double fuel_remaining_light_years{};
  int mission_order_revision{};
  std::optional<stellar::core::OwnCombatFleetStatus> combat_status;
  double combat_power{};
  // Civilian recovery state (reference UiOwnedFleetSnapshot): populated for
  // every owned fleet; the return preview is only computed for the selected
  // civilian fleet so the route planner never runs for hidden rows.
  bool hold_requested{}, return_to_base_requested{};
  std::optional<std::string> return_to_base_failure_reason;
  std::string civilian_return_preview;
};

// A historic intelligence record owns no current position. The strategic
// observer surface does not authorize projecting live foreign FleetState.
struct NativeForeignFleetContact {
  int fleet_id{};
  double observed_power{};
  double observed_day{};
  std::string evidence;
};

struct NativeFleetMapView {
  std::uint64_t campaign_generation{};
  int player_civilization_id{};
  std::vector<NativeOwnFleet> own_fleets;
  std::vector<NativeForeignFleetContact> foreign_contacts;
  std::optional<int> selected_fleet_id;
};

struct NativeFleetSelectionOutcome {
  bool accepted{};
  std::string message;
};

struct NativeFleetRoutePreview {
  std::uint64_t campaign_generation{};
  int fleet_id{};
  int expected_mission_order_revision{};
  int target_system_id{};
  bool route_supported{};
  bool route_authoritative{};
  bool command_available{};
  std::string message;
  std::vector<int> route_system_ids;
  double route_distance_light_years{};
  std::optional<double> estimated_transit_days;
};

struct NativeFleetOrderOutcome {
  bool accepted{};
  std::string message;
  int mission_order_revision{};
  // Reference UiSelectedCivilianReturnNeedsConfirmation: the order is held
  // until the operator confirms abandoning paid colony work.
  bool requires_confirmation{};
};

class NativeFleetController final {
public:
  NativeFleetController() = default;

  [[nodiscard]] NativeFleetMapView
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation);
  [[nodiscard]] NativeFleetSelectionOutcome
  select(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
         int fleet_id);
  // The presentation supplies its screen-space hits. Valid owned IDs are
  // sorted, then repeated calls cycle from the current selection exactly as
  // the preserved campaign map does.
  [[nodiscard]] NativeFleetSelectionOutcome
  select_next_hit(stellar::core::CampaignFrame &,
                  std::uint64_t campaign_generation,
                  std::span<const int> hit_fleet_ids);
  void clear_selection();
  [[nodiscard]] NativeFleetRoutePreview
  preview_selected_route(stellar::core::CampaignFrame &,
                         std::uint64_t campaign_generation,
                         int target_system_id);
  [[nodiscard]] NativeFleetOrderOutcome
  issue_selected_route(stellar::core::CampaignFrame &,
                       const NativeFleetRoutePreview &preview);
  // Reference UiToggleSelectedCivilianFleetHold /
  // UiRequestSelectedCivilianReturnToBase.
  // Reference UiIssueMilitaryOrder (non-tactical branch): the armed-fleet
  // HOLD/DEFEND/RETREAT buttons issue a strategic MilitaryOrder through the
  // coordinator. Defend anchors at the fleet's current system.
  [[nodiscard]] NativeFleetOrderOutcome issue_selected_military_order(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      stellar::core::MilitaryOrderType type);
  [[nodiscard]] NativeFleetOrderOutcome toggle_selected_civilian_hold(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation);
  [[nodiscard]] NativeFleetOrderOutcome request_selected_civilian_return(
      stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
      bool confirm_abandon);
  [[nodiscard]] std::optional<int> selection() const;

private:
  void require_owner() const;
  void bind_generation(std::uint64_t campaign_generation);

  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::optional<int> selected_fleet_id_;
};

} // namespace stellar::native_fleet
