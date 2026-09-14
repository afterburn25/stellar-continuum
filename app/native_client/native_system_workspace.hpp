#pragma once

#include "native_system_view.hpp"
#include <stellar/engine/native_map_platform.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

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
enum class SystemWorkspaceCommandKind { none, close };
struct SystemWorkspaceCommand {SystemWorkspaceCommandKind kind{SystemWorkspaceCommandKind::none};bool captured{};};
struct SystemWorkspaceLayout {
  stellar::native_map::UiRect controls_row;
  stellar::native_map::UiRect back;
  stellar::native_map::UiRect reset;
  stellar::native_map::UiRect inspector;
  stellar::native_map::UiRect world_field;
  [[nodiscard]] static SystemWorkspaceLayout for_viewport(int width,int height) noexcept;
};
class NativeSystemWorkspace final {
public:
  explicit NativeSystemWorkspace(SystemImageProvider provider={});
  void open(stellar::native_system::NativeSystemSnapshot,int width,int height);
  void refresh(stellar::native_system::NativeSystemSnapshot);
  void close() noexcept;
  void discard_campaign() noexcept;
  [[nodiscard]] bool visible()const noexcept{return snapshot_.has_value();}
  [[nodiscard]] std::optional<int> system_id()const noexcept;
  [[nodiscard]] std::optional<int> selected_body_id()const noexcept{return selected_body_id_;}
  [[nodiscard]] std::optional<std::uint64_t> campaign_generation()const noexcept;
  [[nodiscard]] std::optional<stellar::core::SystemSurveyLevel> survey_level()const noexcept;
  [[nodiscard]] const stellar::native_system::SystemSpatialViewport *viewport()const noexcept;
  [[nodiscard]] std::size_t visible_body_count()const noexcept;
  [[nodiscard]] SystemWorkspaceCommand handle(const stellar::native_map::InputEvent&,int width,int height);
  void render(stellar::native_map::DrawList&,int width,int height);
  void reset_fit(int width,int height);
private:
  void resize(int width,int height);
  [[nodiscard]] const stellar::native_system::NativeSystemBody *selected_body()const noexcept;
  SystemImageProvider image_provider_;
  std::optional<stellar::native_system::NativeSystemSnapshot> snapshot_;
  std::optional<stellar::native_system::SystemSpatialSnapshot> spatial_;
  std::optional<stellar::native_system::SystemSpatialViewport> viewport_;
  std::optional<int> selected_body_id_;
  bool dragging_{};
  int width_{},height_{};
};
} // namespace stellar::native_system_ui
