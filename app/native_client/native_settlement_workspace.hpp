#pragma once

#include "native_settlement_mission_controller.hpp"
#include <stellar/engine/native_map_platform.hpp>

#include <optional>

namespace stellar::native_colony_ui {

struct SettlementWorkspaceLayout {
  float scale{};
  int title_font{}, body_font{}, small_font{};
  stellar::native_map::UiRect shade, panel, confirm, cancel;
  [[nodiscard]] static SettlementWorkspaceLayout for_viewport(int, int) noexcept;
};

enum class SettlementWorkspaceCommandKind { None, Confirm, Cancel };
struct SettlementWorkspaceCommand {
  SettlementWorkspaceCommandKind kind{SettlementWorkspaceCommandKind::None};
  bool captured{};
};

[[nodiscard]] bool has_active_settlement_target(
    const stellar::native_colony::NativeSettlementMissionView&) noexcept;

class NativeSettlementWorkspace final {
public:
  void set_preview(stellar::native_colony::NativeSettlementTargetPreview);
  void clear() noexcept;
  void cancel_pending_input() noexcept { reset_gesture(); }
  void discard_campaign() noexcept { clear(); }
  [[nodiscard]] bool visible() const noexcept { return preview_.has_value(); }
  [[nodiscard]] const std::optional<stellar::native_colony::NativeSettlementTargetPreview>& preview() const noexcept { return preview_; }
  [[nodiscard]] SettlementWorkspaceCommand handle(
      const stellar::native_map::InputEvent&, int width, int height);
  void render(stellar::native_map::DrawList&, int width, int height) const;
private:
  enum class PressTarget { None, Confirm, Cancel };
  void reset_gesture() noexcept;
  std::optional<stellar::native_colony::NativeSettlementTargetPreview> preview_;
  stellar::native_map::Point pointer_{};
  bool pointer_owned_{};
  PressTarget pressed_{PressTarget::None};
  int press_width_{}, press_height_{};
};

} // namespace stellar::native_colony_ui
