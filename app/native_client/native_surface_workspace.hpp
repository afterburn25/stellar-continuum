#pragma once

#include "native_surface_construction_controller.hpp"
#include "native_surface_relief.hpp"
#include "native_surface_scene.hpp"
#include "native_surface_viewport.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <functional>
#include <unordered_map>

namespace stellar::native_colony_ui {

struct SurfaceWorkspaceLayout {
  float scale{};
  int heading_font{}, body_font{}, small_font{};
  stellar::native_map::UiRect surface, back, title, palette, palette_rows,
      terrain, inspector, overview, focus, rotate, remove, confirmation,
      confirm, cancel, upgrade, repair, toggle_operation, priority, hub_upgrade;
  [[nodiscard]] static SurfaceWorkspaceLayout for_viewport(int width,
                                                            int height) noexcept;
};

enum class SurfaceWorkspaceCommandKind {
  None,
  Close,
  PreviewPlacement,
  ConfirmPlacement,
  PreviewRemoval,
  ConfirmRemoval,
  CancelQuote,
  PreviewManagement,
  ConfirmManagement
};

struct SurfaceWorkspaceCommand {
  SurfaceWorkspaceCommandKind kind{SurfaceWorkspaceCommandKind::None};
  bool captured{}, open_confirmation{};
  std::string type_id;
  int building_id{};
  float x{}, z{}, rotation_degrees{};
  std::uint64_t quote_revision{};
  stellar::native_colony::NativeSurfaceManagementAction management_action{};
  bool value{};
};

class NativeSurfaceWorkspace final {
public:
  // The app prepares this immutable image outside the workspace and supplies
  // only the ready result, keeping the UI headless and dependency-free.
  void set_terrain_image(
      std::shared_ptr<const stellar::native_map::RgbaImage>) noexcept;
  void use_relief_preparation(
      std::shared_ptr<stellar::native_map::ImagePreparationQueue>);
  void set_relief_suppressed(bool value) noexcept { relief_suppressed_ = value; }
  void set_building_images(SurfaceBuildingReadyProvider,
                          std::optional<ReadySurfaceBuildingImage> preview = std::nullopt);
  void open(stellar::native_colony::NativeColonyView, int width, int height);
  void set_view(stellar::native_colony::NativeColonyView);
  void close() noexcept;
  void discard_campaign() noexcept;
  void set_placement_quote(
      stellar::native_colony::NativeSurfacePlacementQuote,
      bool open_confirmation);
  void set_removal_quote(stellar::native_colony::NativeSurfaceRemovalQuote);
  void set_management_quote(stellar::native_colony::NativeSurfaceManagementQuote);
  void complete_management(std::string notice);
  void set_text_measurer(std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>);
  const std::optional<stellar::native_colony::NativeSurfaceManagementQuote>& management_quote() const noexcept { return management_quote_; }
  void complete_command(std::string notice, bool accepted);
  void set_notice(std::string value) { notice_ = std::move(value); }
  void set_artwork_notice(std::string value) { artwork_notice_ = std::move(value); }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool modal_open() const noexcept {
    return !std::holds_alternative<std::monostate>(confirmation_);
  }
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
  [[nodiscard]] NativeSurfaceSceneDiagnostics scene_diagnostics() const;
  [[nodiscard]] stellar::native_surface::NativeSurfaceReliefStats
  relief_stats() const noexcept { return relief_.stats(); }
  [[nodiscard]] const std::string &relief_error() const noexcept {
    return relief_.error();
  }

  [[nodiscard]] SurfaceWorkspaceCommand handle(
      const stellar::native_map::InputEvent&, int width, int height);
  [[nodiscard]] std::optional<SurfaceWorkspaceCommand>
  take_preview_request() noexcept;
  void render(stellar::native_map::DrawList&, int width, int height) const;

private:
  void fit(int width, int height) noexcept;
  void focus_selected(int width, int height) noexcept;
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
  std::optional<int> construction_completion_watch_id_;
  float rotation_degrees_{};
  float palette_scroll_{};
  std::optional<std::pair<float, float>> last_preview_position_;
  std::optional<stellar::native_colony::NativeSurfacePlacementQuote>
      placement_quote_;
  std::optional<stellar::native_colony::NativeSurfaceRemovalQuote>
      removal_quote_;
  std::variant<std::monostate,
               stellar::native_colony::NativeSurfacePlacementQuote,
               stellar::native_colony::NativeSurfaceRemovalQuote,
               stellar::native_colony::NativeSurfaceManagementQuote>
      confirmation_;
  std::optional<stellar::native_colony::NativeSurfaceManagementQuote> management_quote_;
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> text_measurer_;
  mutable std::unordered_map<std::string, float> inspector_heights_;
  mutable float inspector_content_height_{}, inspector_scroll_{};
  std::string notice_;
  std::string artwork_notice_;
  std::optional<SurfaceWorkspaceCommand> pending_preview_;
  std::shared_ptr<const stellar::native_map::RgbaImage> terrain_image_;
  SurfaceBuildingReadyProvider building_images_;
  std::optional<ReadySurfaceBuildingImage> preview_image_;
  mutable std::size_t scene_sites_{}, scene_meshes_{}, scene_triangles_{},
      scene_road_segments_{}, scene_replaced_structures_{};
  mutable NativeSurfaceScene scene_;
  mutable stellar::native_surface::NativeSurfaceRelief relief_;
  bool relief_suppressed_{};
};

} // namespace stellar::native_colony_ui
