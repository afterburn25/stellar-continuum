#pragma once

#include "native_system_view.hpp"
#include "native_system_travel.hpp"
#include "native_celestial_appearance.hpp"
#include "native_orbital_structure.hpp"
#include <stellar/engine/native_map_platform.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace stellar::native_system_ui {
struct SystemBodyAppearance {
  std::uint64_t campaign_generation{};
  int body_id{};
  bool fully_surveyed{};
  stellar::native_system::NativeSystemBodyVisualClass visual_class{};
  std::optional<std::string> texture_key;
  std::uint32_t deterministic_seed{};
  float lighting_longitude{};
};
using SystemImageProvider=std::function<std::shared_ptr<const stellar::native_map::RgbaImage>(const SystemBodyAppearance&)>;
using SystemTextMeasurer=std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>;
enum class SystemWorkspaceCommandKind {
  none,
  close,
  select_fleet,
  open_destination,
  open_colony,
  settlement_target,
  open_construction
};
struct SystemWorkspaceCommand {
  SystemWorkspaceCommandKind kind{SystemWorkspaceCommandKind::none};
  bool captured{};
  int target_id{-1};
  std::vector<int> hit_fleet_ids;
  std::string project_id;
};
struct NativeSystemSettlementStatus {
  int fleet_id{};
  std::string status;
  std::optional<int> destination_system_id, destination_body_id;
  double settlement_days_completed{}, establishment_days{};
};
struct SystemWorkspaceLayout {
  stellar::native_map::UiRect controls_row;
  stellar::native_map::UiRect back;
  stellar::native_map::UiRect reset;
  stellar::native_map::UiRect inspector;
  stellar::native_map::UiRect colony_action;
  stellar::native_map::UiRect infrastructure_action;
  stellar::native_map::UiRect world_field;
  [[nodiscard]] static SystemWorkspaceLayout for_viewport(int width,int height) noexcept;
};
class NativeSystemWorkspace final {
public:
  explicit NativeSystemWorkspace(SystemImageProvider provider={},SystemTextMeasurer measurer={});
  void open(stellar::native_system::NativeSystemSnapshot,int width,int height);
  void refresh(stellar::native_system::NativeSystemSnapshot);
  void refresh_travel(stellar::native_system_travel::NativeSystemTravelSnapshot,
                      std::optional<int> selected_fleet_id);
  void clear_travel() noexcept;
  void set_colony_body(std::optional<int>) noexcept;
  void set_settlement_status(std::optional<NativeSystemSettlementStatus> value) {
    settlement_status_ = std::move(value);
  }
  void set_notice(std::string);
  void close() noexcept;
  void discard_campaign() noexcept;
  [[nodiscard]] bool visible()const noexcept{return snapshot_.has_value();}
  [[nodiscard]] std::optional<int> system_id()const noexcept;
  [[nodiscard]] std::optional<int> selected_body_id()const noexcept{return selected_body_id_;}
  [[nodiscard]] std::optional<std::uint64_t> campaign_generation()const noexcept;
  [[nodiscard]] std::optional<stellar::core::SystemSurveyLevel> survey_level()const noexcept;
  [[nodiscard]] const stellar::native_system::SystemSpatialViewport *viewport()const noexcept;
  [[nodiscard]] const stellar::native_system::NativeSystemSnapshot *snapshot()const noexcept;
  [[nodiscard]] const stellar::native_system_travel::NativeSystemTravelSnapshot *travel_snapshot()const noexcept;
  [[nodiscard]] std::optional<int> selected_fleet_id()const noexcept{return selected_fleet_id_;}
  [[nodiscard]] std::optional<int> hovered_lane_id()const noexcept{return hovered_lane_id_;}
  [[nodiscard]] const std::string &notice()const noexcept{return notice_;}
  [[nodiscard]] std::size_t visible_body_count()const noexcept;
  [[nodiscard]] std::vector<stellar::native_system_travel::NativeLocalLaneGeometry> lane_geometry()const;
  [[nodiscard]] SystemWorkspaceCommand handle(const stellar::native_map::InputEvent&,int width,int height);
  void render(stellar::native_map::DrawList&,int width,int height);
  void reset_fit(int width,int height);
private:
  enum class InspectorFocus { automatic, body, fleet, infrastructure };
  void resize(int width,int height);
  [[nodiscard]] const stellar::native_system::NativeSystemBody *selected_body()const noexcept;
  [[nodiscard]] const stellar::native_system_travel::NativeLocalFleetMarker *selected_fleet()const noexcept;
  [[nodiscard]] const stellar::native_system::NativeSystemInfrastructureMarker *selected_infrastructure()const noexcept;
  [[nodiscard]] std::optional<stellar::native_map::Point>
  infrastructure_position(
      const stellar::native_system::NativeSystemInfrastructureMarker &,
      std::size_t index) const;
  [[nodiscard]] std::optional<std::string>
  hit_infrastructure(stellar::native_map::Point) const;
  [[nodiscard]] std::vector<int> fleet_hits(stellar::native_map::Point)const;
  SystemImageProvider image_provider_;
  SystemTextMeasurer text_measurer_;
  NativeCelestialAppearanceRenderer celestial_appearance_;
  stellar::native_orbital::NativeOrbitalStructureRenderer structures_;
  std::optional<stellar::native_system::NativeSystemSnapshot> snapshot_;
  std::optional<stellar::native_system::SystemSpatialSnapshot> spatial_;
  std::optional<stellar::native_system::SystemSpatialViewport> viewport_;
  std::optional<int> selected_body_id_;
  std::optional<std::string> selected_project_id_;
  std::optional<int> colony_body_id_;
  std::optional<NativeSystemSettlementStatus> settlement_status_;
  std::optional<stellar::native_system_travel::NativeSystemTravelSnapshot> travel_;
  std::vector<stellar::native_system_travel::NativeLaneLabelMetrics> lane_metrics_;
  std::optional<int> selected_fleet_id_,hovered_fleet_id_,hovered_lane_id_;
  std::string notice_;
  stellar::native_map::Point pointer_{};
  InspectorFocus inspector_focus_{InspectorFocus::automatic};
  bool dragging_{};
  int width_{},height_{};
};
} // namespace stellar::native_system_ui
