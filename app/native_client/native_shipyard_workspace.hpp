#pragma once

#include "native_shipyard_controller.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace stellar::native_shipyard_ui {

struct ShipyardWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect close;
  stellar::native_map::UiRect designs;
  stellar::native_map::UiRect design_details;
  stellar::native_map::UiRect orders;
  stellar::native_map::UiRect readiness;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect action;

  [[nodiscard]] static ShipyardWorkspaceLayout for_viewport(int width,
                                                             int height) noexcept;
};

enum class ShipyardWorkspaceCommandKind { None, Start, PrepareCancel, Cancel };

struct ShipyardWorkspaceCommand {
  ShipyardWorkspaceCommandKind kind{ShipyardWorkspaceCommandKind::None};
  bool captured{};
  std::string id;
};

class NativeShipyardWorkspace final {
public:
  void open() noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept;

  void set_view(stellar::native_shipyard::NativeShipyardView view);
  void discard_campaign();
  void set_notice(std::string message, bool accepted);
  [[nodiscard]] bool arm_cancel_confirmation(std::string_view order_id);
  [[nodiscard]] const std::optional<stellar::native_shipyard::NativeShipyardView> &
  view() const noexcept;
  [[nodiscard]] const std::optional<std::string> &selected_design_id() const noexcept;
  [[nodiscard]] const std::optional<std::string> &selected_order_id() const noexcept;

  [[nodiscard]] ShipyardWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height) const;

private:
  [[nodiscard]] const stellar::native_shipyard::NativeShipDesign *
  selected_design() const noexcept;
  [[nodiscard]] const stellar::native_shipyard::NativeShipyardOrder *
  selected_order() const noexcept;
  void reconcile_selection();

  bool visible_{};
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_shipyard::NativeShipyardView> view_;
  std::optional<std::string> selected_design_id_;
  std::optional<std::string> selected_order_id_;
  std::optional<std::string> cancel_confirmation_id_;
  std::string notice_;
  bool notice_accepted_{};
  float design_scroll_{};
  float order_scroll_{};
};

} // namespace stellar::native_shipyard_ui
