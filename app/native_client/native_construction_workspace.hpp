#pragma once

#include "native_construction_controller.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <initializer_list>
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
  void
  set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
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
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  project_bounds(std::string_view project_id, int width, int height) const;
  [[nodiscard]] ConstructionWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height) const;
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string
  focused_label(const ConstructionWorkspaceLayout &layout) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(const ConstructionWorkspaceLayout &) const;

private:
  struct FocusRect {
    stellar::native_map::UiRect bounds;
    std::string label;
  };
  [[nodiscard]] std::vector<FocusRect>
  focusables(const ConstructionWorkspaceLayout &layout) const;
  [[nodiscard]] const stellar::native_construction::NativeConstructionProject *
  selected_project() const noexcept;
  void reconcile_selection();
  void rebuild_status_order();
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;
  [[nodiscard]] std::string state_label(
      const stellar::native_construction::NativeConstructionProject &) const;
  [[nodiscard]] std::string
  category_label(stellar::core::ConstructionCategory) const;

  const stellar::engine::LocalizationTable *locale_{};
  bool visible_{};
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_construction::NativeConstructionView> view_;
  std::optional<std::string> selected_project_id_;
  std::optional<std::string> cancel_confirmation_id_;
  std::string notice_;
  bool notice_accepted_{};
  mutable stellar::engine::VirtualizedList project_scroll_{};
  mutable stellar::engine::VirtualizedList order_scroll_{};
  std::vector<std::size_t> status_order_;
  int focus_{-1};
};

} // namespace stellar::native_construction_ui
