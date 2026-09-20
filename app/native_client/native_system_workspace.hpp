#pragma once

#include "native_system_view.hpp"
#include "native_small_body_renderer.hpp"
#include "native_planet_materials.hpp"
#include "native_stellar_observation.hpp"
#include "native_body_inspection_panel.hpp"
#include "native_system_travel.hpp"
#include "native_celestial_appearance.hpp"
#include "native_settlement_preparation.hpp"
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
  std::optional<stellar::core::PlanetAppearance> canonical;
};
using SystemImageProvider=std::function<std::shared_ptr<const stellar::native_map::RgbaImage>(const SystemBodyAppearance&)>;
using StellarArtProvider=std::function<void(stellar::native_map::DrawList&,stellar::native_map::Point,float,const stellar::native_stellar::ObservedStellarArtwork&,double,stellar::native_map::UiRect)>;
using StellarActivityProvider=std::function<void(stellar::native_map::DrawList&,stellar::native_map::Point,float,int,int,stellar::native_map::UiRect)>;
using SystemTextMeasurer=std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>;
enum class SystemWorkspaceCommandKind {
  none,
  close,
  select_fleet,
  open_destination,
  reconnaissance_required,
  open_colony,
  open_shipyard,
  open_orbital_shipyard,
  settlement_target,
  spawn_small_body_field,
  toggle_motion
};
struct SystemWorkspaceCommand {
  SystemWorkspaceCommandKind kind{SystemWorkspaceCommandKind::none};
  bool captured{};
  int target_id{-1};
  std::vector<int> hit_fleet_ids;
};
struct NativeSystemSettlementStatus {
  int fleet_id{};
  std::string status;
  std::optional<int> destination_system_id, destination_body_id;
  double settlement_days_completed{}, establishment_days{};
  bool operator==(const NativeSystemSettlementStatus &) const = default;
};
struct SystemWorkspaceLayout {
  stellar::native_map::UiRect controls_row;
  stellar::native_map::UiRect back;
  stellar::native_map::UiRect reset;
  stellar::native_map::UiRect inspector;
  stellar::native_map::UiRect colony_action;
  stellar::native_map::UiRect world_field;
  stellar::native_map::UiRect focus_action;
  [[nodiscard]] static SystemWorkspaceLayout for_viewport(int width,int height) noexcept;
};
class NativeSystemWorkspace final {
public:
  explicit NativeSystemWorkspace(SystemImageProvider provider={},SystemTextMeasurer measurer={});
  void set_stellar_art(StellarArtProvider value){stellar_art_=std::move(value);}
  void set_stellar_activity(StellarActivityProvider value){stellar_activity_=std::move(value);}
  void set_planet_materials(stellar::native_planets::MaterialProvider value){planet_materials_=std::move(value);}
  void set_small_body_images(SmallBodyImageProvider value){small_bodies_.set_images(std::move(value));}
  void set_simulation_days(double days);
  [[nodiscard]] std::optional<int> tracked_body_id()const noexcept{return tracked_body_id_;}
  void set_motion_running(bool running)noexcept{motion_running_=running;}
  void advance_tumble(double seconds,bool running){if(running&&std::isfinite(seconds)&&seconds>0)visual_seconds_+=std::min(seconds,.25);if(visible())small_bodies_.advance_tumble(seconds,running);}
  [[nodiscard]] float magnification()const{return viewport_?viewport_->scale/zoom_reference_scale_:1.f;}
  double visual_seconds()const noexcept{return visual_seconds_;}
  void inspect_small_body(std::size_t field,std::uint32_t body=0){small_body_field_=field;small_body_index_=body;small_body_panel_=true;}
  void focus_small_body(int width,int height);
  const auto& small_body_statistics()const{return small_bodies_.statistics();}
  void use_background_preparation(std::shared_ptr<stellar::native_map::ImagePreparationQueue>);
  [[nodiscard]] bool artwork_ready() const noexcept{return artwork_ready_;}
  void open(stellar::native_system::NativeSystemSnapshot,int width,int height);
  void refresh(stellar::native_system::NativeSystemSnapshot);
  void refresh_travel(stellar::native_system_travel::NativeSystemTravelSnapshot,
                      std::optional<int> selected_fleet_id);
  void clear_travel() noexcept;
  void set_colony_body(std::optional<int>) noexcept;
  void set_settlement_preparation(std::optional<stellar::native_settlement_preparation::View>);
  [[nodiscard]] const auto &settlement_preparation() const noexcept { return preparation_; }
  void set_settlement_status(std::optional<NativeSystemSettlementStatus> value);
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
  void render(stellar::native_map::DrawList&,int width,int height,bool draw_starfield=true);
  void reset_fit(int width,int height);
  [[nodiscard]] bool select_body(int body_id);
  void focus_selected_body(int width,int height);
  void focus_fleet(int fleet_id,int width,int height);
  void set_shipyard(std::optional<int> owner,int anchor_body,std::shared_ptr<const stellar::native_map::RgbaImage> image){shipyard_owner_=owner;shipyard_body_=anchor_body;shipyard_image_=std::move(image);}
  void focus_shipyard(int width,int height);
  [[nodiscard]] std::optional<int> selected_shipyard_owner()const{return shipyard_selected_?shipyard_owner_:std::nullopt;}
  [[nodiscard]] std::optional<stellar::native_map::UiRect> shipyard_bounds()const;
  [[nodiscard]] float inspection_scroll() const noexcept { return body_inspection_.scroll_offset(); }
private:
  bool motion_running_{};
  double visual_seconds_{};float zoom_reference_scale_{1.f};
  enum class InspectorFocus { automatic, body, fleet };
  void resize(int width,int height);
  void track_body(int body_id);
  void update_camera_tracking();
  void sync_body_inspection();
  std::optional<SystemWorkspaceCommand> handle_small_bodies(const stellar::native_map::InputEvent&,int,int);
  void render_small_body_panel(stellar::native_map::DrawList&,int,int);
  [[nodiscard]] const stellar::native_system::NativeSystemBody *selected_body()const noexcept;
  [[nodiscard]] const stellar::native_system_travel::NativeLocalFleetMarker *selected_fleet()const noexcept;
  [[nodiscard]] std::vector<int> fleet_hits(stellar::native_map::Point)const;
  StellarArtProvider stellar_art_;
  StellarActivityProvider stellar_activity_;
  stellar::native_planets::MaterialProvider planet_materials_;
  SystemImageProvider image_provider_;
  SystemTextMeasurer text_measurer_;
  BodyInspectionPanel body_inspection_;
  NativeCelestialAppearanceRenderer celestial_appearance_;
  NativeSmallBodyRenderer small_bodies_;
  bool small_body_panel_{},small_body_debug_{},small_body_focus_{};
  std::size_t small_body_field_{};
  std::uint32_t small_body_index_{};
  std::optional<stellar::native_system::NativeSystemSnapshot> snapshot_;
  std::optional<stellar::native_system::SystemSpatialSnapshot> spatial_;
  std::optional<stellar::native_system::SystemSpatialViewport> viewport_;
  std::optional<int> selected_body_id_;
  std::optional<int> tracked_body_id_;
  stellar::native_map::Point tracking_anchor_{.5f,.5f};
  std::optional<int> colony_body_id_;
  std::optional<stellar::native_settlement_preparation::View> preparation_;
  bool preparation_pressed_{};
  std::optional<NativeSystemSettlementStatus> settlement_status_;
  std::optional<stellar::native_system_travel::NativeSystemTravelSnapshot> travel_;
  std::vector<stellar::native_system_travel::NativeLaneLabelMetrics> lane_metrics_;
  std::optional<int> selected_fleet_id_,hovered_fleet_id_,hovered_lane_id_;
  std::optional<int> pressed_lane_id_;
  std::string notice_;
  stellar::native_map::Point pointer_{};
  InspectorFocus inspector_focus_{InspectorFocus::automatic};
  std::optional<int> shipyard_owner_;
  int shipyard_body_{-1};
  bool shipyard_selected_{},shipyard_pressed_{};
  std::shared_ptr<const stellar::native_map::RgbaImage> shipyard_image_;
  bool dragging_{},pending_initial_travel_fit_{};
  bool artwork_ready_{true};
  int width_{},height_{};
};
} // namespace stellar::native_system_ui
