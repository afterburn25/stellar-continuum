#pragma once

#include "native_fleet_controller.hpp"
#include "native_ship_art_assets.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stellar::native_fleet_ui {

struct FleetScreenMarker {
  int fleet_id{};
  stellar::native_map::Point position{};
};

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

  [[nodiscard]] static FleetWorkspaceLayout for_viewport(int width,
                                                          int height) noexcept;
};

enum class FleetWorkspaceCommandKind {
  None,
  Select,
  SelectHits,
  Preview,
  Confirm
};

struct FleetWorkspaceCommand {
  FleetWorkspaceCommandKind kind{FleetWorkspaceCommandKind::None};
  bool captured{};
  int fleet_id{};
  int target_system_id{};
  std::vector<int> hit_fleet_ids;
};

class NativeFleetWorkspace final {
public:
  void set_view(stellar::native_fleet::NativeFleetMapView view);
  void discard_campaign();
  void set_preview(stellar::native_fleet::NativeFleetRoutePreview preview,
                   std::string target_display_name);
  void clear_preview();
  void set_notice(std::string message, bool accepted);

  [[nodiscard]] FleetWorkspaceCommand handle(
      const stellar::native_map::InputEvent &event, int width, int height,
      std::span<const FleetScreenMarker> markers,
      std::optional<int> target_system_id);
  void render(stellar::native_map::DrawList &out, int width, int height,
              std::span<const FleetScreenMarker> markers,
              stellar::native_ship_ui::NativeShipArtAssets *ship_art = nullptr) const;
  [[nodiscard]] int last_ship_art_rows() const noexcept {
    return last_ship_art_rows_;
  }

  [[nodiscard]] const std::optional<stellar::native_fleet::NativeFleetMapView> &
  view() const noexcept;
  [[nodiscard]] const std::optional<stellar::native_fleet::NativeFleetRoutePreview> &
  preview() const noexcept;
  [[nodiscard]] std::optional<int> selected_fleet_id() const noexcept;

private:
  [[nodiscard]] const stellar::native_fleet::NativeOwnFleet *
  selected_fleet() const noexcept;

  std::optional<stellar::native_fleet::NativeFleetMapView> view_;
  std::optional<stellar::native_fleet::NativeFleetRoutePreview> preview_;
  std::string target_display_name_;
  std::string notice_;
  bool notice_accepted_{};
  stellar::native_map::Point pointer_{};
  float list_scroll_{};
  mutable int last_ship_art_rows_{};
};

} // namespace stellar::native_fleet_ui
