#pragma once

#include "native_settlement_mission_controller.hpp"
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <initializer_list>
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
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] bool visible() const noexcept { return preview_.has_value(); }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  [[nodiscard]] const std::optional<stellar::native_colony::NativeSettlementTargetPreview>& preview() const noexcept { return preview_; }
  [[nodiscard]] SettlementWorkspaceCommand handle(
      const stellar::native_map::InputEvent&, int width, int height);
  void render(stellar::native_map::DrawList&, int width, int height) const;
private:
  enum class PressTarget { None, Confirm, Cancel };
  void reset_gesture() noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::optional<stellar::native_colony::NativeSettlementTargetPreview> preview_;
  stellar::native_map::Point pointer_{};
  bool pointer_owned_{};
  PressTarget pressed_{PressTarget::None};
  int press_width_{}, press_height_{}, focus_{-1};
};

} // namespace stellar::native_colony_ui
