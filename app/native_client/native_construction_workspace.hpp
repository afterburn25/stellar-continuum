#pragma once

#include "native_construction_controller.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_construction_ui {

struct ConstructionWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect close;
  stellar::native_map::UiRect projects;
  stellar::native_map::UiRect details;
  stellar::native_map::UiRect orders;
  stellar::native_map::UiRect costs;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect primary_action;
  stellar::native_map::UiRect secondary_action;

  [[nodiscard]] static ConstructionWorkspaceLayout
  for_viewport(int width, int height) noexcept;
};

enum class ConstructionWorkspaceCommandKind {
  None,
  Start,
  Queue,
  PrepareCancel,
  Cancel
};

struct ConstructionWorkspaceCommand {
  ConstructionWorkspaceCommandKind kind{ConstructionWorkspaceCommandKind::None};
  bool captured{};
  std::string project_id;
};

class NativeConstructionWorkspace final {
public:
  void open() noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept;
  [[nodiscard]] bool confirmation_open() const noexcept {
    return cancel_confirmation_id_.has_value();
  }
  void set_view(stellar::native_construction::NativeConstructionView view);
  void discard_campaign();
  void set_notice(std::string message, bool accepted);
  [[nodiscard]] bool arm_cancel_confirmation(std::string_view project_id);

  [[nodiscard]] const std::optional<
      stellar::native_construction::NativeConstructionView> &
  view() const noexcept;
  [[nodiscard]] const std::optional<std::string> &
  selected_project_id() const noexcept;
  [[nodiscard]] ConstructionWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height) const;

private:
  [[nodiscard]] const stellar::native_construction::NativeConstructionProject *
  selected_project() const noexcept;
  void reconcile_selection();
  void rebuild_status_order();

  bool visible_{};
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_construction::NativeConstructionView> view_;
  std::optional<std::string> selected_project_id_;
  std::optional<std::string> cancel_confirmation_id_;
  std::string notice_;
  bool notice_accepted_{};
  float project_scroll_{};
  float order_scroll_{};
  std::vector<std::size_t> status_order_;
};

} // namespace stellar::native_construction_ui
