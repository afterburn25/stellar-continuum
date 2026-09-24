#pragma once

#include "native_fleet_controller.hpp"
#include "native_overview.hpp"
#include "native_ship_art_assets.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_fleet_ui {

struct FleetScreenMarker {
  int fleet_id{};
  stellar::native_map::Point position{};
};

enum class FleetWorkspacePresentation { Outliner, SelectedCommands };

struct FleetWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect panel;
  stellar::native_map::UiRect heading;
  stellar::native_map::UiRect list;
  stellar::native_map::UiRect details;
  stellar::native_map::UiRect route;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect confirm;
  stellar::native_map::UiRect recovery_left;
  stellar::native_map::UiRect recovery_right;
  stellar::native_map::UiRect order_hold;
  stellar::native_map::UiRect order_defend;
  stellar::native_map::UiRect order_retreat;
  stellar::native_map::UiRect locate;
  stellar::native_map::UiRect military_locate;
  stellar::native_map::UiRect civilian_locate;
  stellar::native_map::UiRect engage;

  [[nodiscard]] static FleetWorkspaceLayout for_viewport(int width,
      int height, FleetWorkspacePresentation presentation =
          FleetWorkspacePresentation::Outliner) noexcept;
};

enum class FleetWorkspaceCommandKind {
  None,
  Select,
  SelectHits,
  Preview,
  Confirm,
  Engage,
  MilitaryOrder,
  Locate,
  Recovery,
  OpenColony
};

struct FleetWorkspaceCommand {
  FleetWorkspaceCommandKind kind{FleetWorkspaceCommandKind::None};
  bool captured{};
  int fleet_id{};
  int target_system_id{};
  std::vector<int> hit_fleet_ids;
  int colony_id{};
  std::optional<stellar::native_fleet::NativeCivilianRecoveryQuote> recovery_quote;
  stellar::native_fleet::NativeCivilianRecoveryAction recovery_action{};
  bool confirm_abandon{};
  std::optional<stellar::native_fleet::NativeMilitaryOrderQuote> military_order_quote;
  stellar::core::MilitaryOrderType military_order{stellar::core::MilitaryOrderType::Hold};
  std::optional<stellar::native_fleet::NativeFleetLocateQuote> locate_quote;
};

class NativeFleetWorkspace final {
public:
  explicit NativeFleetWorkspace(FleetWorkspacePresentation presentation =
      FleetWorkspacePresentation::Outliner) : presentation_(presentation) {}
  [[nodiscard]] FleetWorkspaceLayout layout(int width, int height) const noexcept;
  [[nodiscard]] std::optional<stellar::native_map::UiRect> panel_bounds(
      int width, int height) const noexcept;
  void
  set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  void set_view(stellar::native_fleet::NativeFleetMapView view);
  void discard_campaign();
  void set_preview(stellar::native_fleet::NativeFleetRoutePreview preview,
                   std::string target_display_name);
  void clear_preview();
  void set_notice(std::string message, bool accepted);
  void set_recovery_result(const stellar::native_fleet::NativeCivilianRecoveryQuote &,
                           const stellar::native_fleet::NativeFleetOrderOutcome &);
  void cancel_recovery() noexcept;
  [[nodiscard]] bool recovery_confirmation_open() const noexcept {
    return pending_return_.has_value();
  }
  // Reference EmpireOverviewPanel (empire mode): while no fleet is selected,
  // the detail area shows the selected-system home reference, the own-colony
  // quick list and the combined fleet power instead of the selection hint.
  void set_overview(
      std::optional<stellar::native_overview::NativeEmpireOverview>
          overview) {
    overview_ = std::move(overview);
  }
  [[nodiscard]] const stellar::native_overview::NativeEmpireOverview *
  overview() const noexcept {
    return overview_ ? &*overview_ : nullptr;
  }
  [[nodiscard]] FleetWorkspaceCommand handle(
      const stellar::native_map::InputEvent &event, int width, int height,
      std::span<const FleetScreenMarker> markers,
      std::optional<int> target_system_id);
  void render(stellar::native_map::DrawList &out, int width, int height,
              std::span<const FleetScreenMarker> markers,
              stellar::native_ship_ui::NativeShipArtAssets *ship_art = nullptr,
              const stellar::native_overview::OverviewImageProvider
                  *portraits = nullptr) const;
  [[nodiscard]] int last_ship_art_rows() const noexcept {
    return last_ship_art_rows_;
  }

  [[nodiscard]] const std::optional<stellar::native_fleet::NativeFleetMapView> &
  view() const noexcept;
  [[nodiscard]] const std::optional<stellar::native_fleet::NativeFleetRoutePreview> &
  preview() const noexcept;
  [[nodiscard]] std::optional<int> selected_fleet_id() const noexcept;
  [[nodiscard]] int focus() const noexcept { return focus_; }

private:
  [[nodiscard]] std::vector<stellar::native_map::UiRect>
  focusables(const FleetWorkspaceLayout &) const;
  FleetWorkspacePresentation presentation_;
  enum class PressTarget { None, Hold, Defend, Retreat, Locate };
  [[nodiscard]] const stellar::native_fleet::NativeOwnFleet *
  selected_fleet() const noexcept;
  void clear_pressed_action() noexcept;
  [[nodiscard]] PressTarget pressed_target_at(
      stellar::native_map::Point, const FleetWorkspaceLayout &) const noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  std::optional<stellar::native_fleet::NativeFleetMapView> view_;
  std::optional<stellar::native_overview::NativeEmpireOverview> overview_;
  std::optional<stellar::native_fleet::NativeFleetRoutePreview> preview_;
  std::string target_display_name_;
  std::string notice_;
  std::optional<stellar::native_fleet::NativeCivilianRecoveryQuote> pending_return_;
  std::string return_warning_;
  bool notice_accepted_{};
  stellar::native_map::Point pointer_{};
  float list_scroll_{};
  PressTarget pressed_action_{PressTarget::None};
  stellar::native_map::UiRect pressed_bounds_{};
  std::optional<stellar::native_fleet::NativeMilitaryOrderQuote> pressed_military_quote_;
  std::optional<stellar::native_fleet::NativeFleetLocateQuote> pressed_locate_quote_;
  mutable int last_ship_art_rows_{};
  int focus_{-1};
};

} // namespace stellar::native_fleet_ui
