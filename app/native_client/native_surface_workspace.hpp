#pragma once

#include "native_surface_construction_controller.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace stellar::native_colony_ui {

struct SurfaceWorkspaceLayout {
  float scale{};
  int heading_font{}, body_font{}, small_font{};
  stellar::native_map::UiRect surface, back, title, palette, palette_rows,
      terrain, inspector, rotate, remove, confirmation, confirm, cancel;
  [[nodiscard]] static SurfaceWorkspaceLayout for_viewport(int width,
                                                            int height) noexcept;
};

struct SurfaceViewport {
  double center_x{}, center_z{}, pixels_per_unit{.5};
  [[nodiscard]] stellar::native_map::Point world_to_screen(
      double x, double z, stellar::native_map::UiRect terrain) const noexcept;
  [[nodiscard]] std::pair<double, double> screen_to_world(
      stellar::native_map::Point, stellar::native_map::UiRect terrain) const
      noexcept;
  [[nodiscard]] SurfaceViewport translated(float dx, float dy) const noexcept;
  [[nodiscard]] SurfaceViewport zoomed_at(float factor,
                                          stellar::native_map::Point anchor,
                                          stellar::native_map::UiRect terrain,
                                          double minimum,
                                          double maximum) const noexcept;
};

enum class SurfaceWorkspaceCommandKind {
  None,
  Close,
  PreviewPlacement,
  ConfirmPlacement,
  PreviewRemoval,
  ConfirmRemoval,
  CancelQuote
};

struct SurfaceWorkspaceCommand {
  SurfaceWorkspaceCommandKind kind{SurfaceWorkspaceCommandKind::None};
  bool captured{}, open_confirmation{};
  std::string type_id;
  int building_id{};
  float x{}, z{}, rotation_degrees{};
  std::uint64_t quote_revision{};
};

class NativeSurfaceWorkspace final {
public:
  void open(stellar::native_colony::NativeColonyView, int width, int height);
  void set_view(stellar::native_colony::NativeColonyView);
  void close() noexcept;
  void discard_campaign() noexcept;
  void set_placement_quote(
      stellar::native_colony::NativeSurfacePlacementQuote,
      bool open_confirmation);
  void set_removal_quote(stellar::native_colony::NativeSurfaceRemovalQuote);
  void complete_command(std::string notice);
  void set_notice(std::string value) { notice_ = std::move(value); }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const std::optional<stellar::native_colony::NativeColonyView>&
  view() const noexcept { return view_; }
  [[nodiscard]] const std::optional<std::string>& selected_type_id() const
      noexcept { return selected_type_id_; }
  [[nodiscard]] const std::optional<int>& selected_building_id() const noexcept {
    return selected_building_id_;
  }
  [[nodiscard]] const std::optional<
      stellar::native_colony::NativeSurfacePlacementQuote>&
  placement_quote() const noexcept { return placement_quote_; }
  [[nodiscard]] const std::optional<
      stellar::native_colony::NativeSurfaceRemovalQuote>&
  removal_quote() const noexcept { return removal_quote_; }
  [[nodiscard]] const SurfaceViewport& viewport() const noexcept {
    return viewport_;
  }

  [[nodiscard]] SurfaceWorkspaceCommand handle(
      const stellar::native_map::InputEvent&, int width, int height);
  [[nodiscard]] std::optional<SurfaceWorkspaceCommand>
  take_preview_request() noexcept;
  void render(stellar::native_map::DrawList&, int width, int height) const;

private:
  void fit(int width, int height) noexcept;
  void reconcile();
  [[nodiscard]] std::optional<std::size_t> palette_hit(
      stellar::native_map::Point, const SurfaceWorkspaceLayout&) const noexcept;
  [[nodiscard]] std::optional<int> site_hit(
      stellar::native_map::Point, const SurfaceWorkspaceLayout&) const noexcept;
  [[nodiscard]] SurfaceWorkspaceCommand placement_request(
      stellar::native_map::Point, const SurfaceWorkspaceLayout&, bool confirm);

  bool visible_{}, pressed_{}, dragging_{};
  stellar::native_map::Point pointer_{}, press_{};
  SurfaceViewport viewport_{};
  std::optional<stellar::native_colony::NativeColonyView> view_;
  std::optional<std::string> selected_type_id_;
  std::optional<int> selected_building_id_;
  float rotation_degrees_{};
  float palette_scroll_{};
  std::optional<std::pair<float, float>> last_preview_position_;
  std::optional<stellar::native_colony::NativeSurfacePlacementQuote>
      placement_quote_;
  std::optional<stellar::native_colony::NativeSurfaceRemovalQuote>
      removal_quote_;
  std::variant<std::monostate,
               stellar::native_colony::NativeSurfacePlacementQuote,
               stellar::native_colony::NativeSurfaceRemovalQuote>
      confirmation_;
  std::string notice_;
  std::optional<SurfaceWorkspaceCommand> pending_preview_;
};

} // namespace stellar::native_colony_ui
