#pragma once

#include "native_ship_design_controller.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_ship_design_ui {

struct ShipDesignWorkspaceLayout {
  float scale{};
  int title_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  stellar::native_map::UiRect shade, panel;
  stellar::native_map::UiRect list, editor, details;
  stellar::native_map::UiRect hull_row, commit, retire, close_button;
  float slot_row_height{};

  [[nodiscard]] static ShipDesignWorkspaceLayout
  for_viewport(int width, int height) noexcept;
};

enum class ShipDesignWorkspaceCommandKind {
  None,
  SelectDesign,
  CycleHull,
  CycleComponent,
  Commit,
  Retire,
  Close
};

struct ShipDesignWorkspaceCommand {
  ShipDesignWorkspaceCommandKind kind{ShipDesignWorkspaceCommandKind::None};
  bool captured{};
  std::string design_id;
  int slot_index{};
  int direction{};
};

// The bureau keeps a local draft: the chosen hull plus one optional component
// per hull slot. Slot-fit issues are previewed locally; the authoritative
// commit path re-validates and rejects through the command outcome.
class NativeShipDesignWorkspace final {
public:
  void open() noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  void set_view(stellar::native_ship_design::NativeShipDesignView view);
  void discard_campaign();
  void set_notice(std::string message, bool accepted);
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

  [[nodiscard]] const std::optional<stellar::native_ship_design::
                                        NativeShipDesignView> &
  view() const noexcept {
    return view_;
  }
  [[nodiscard]] const std::optional<std::string> &
  selected_design_id() const noexcept {
    return selected_design_id_;
  }
  // Command-facing draft mutations — the workspace owns draft state, the
  // dispatcher routes Cycle commands here.
  void cycle_hull_command(int direction);
  void cycle_component_command(int slot_index, int direction);
  [[nodiscard]] std::string draft_hull_id() const;
  [[nodiscard]] std::vector<std::string> draft_component_ids() const;
  [[nodiscard]] std::string suggest_draft_name() const;
  [[nodiscard]] std::vector<std::string> draft_issues() const;
  [[nodiscard]] bool draft_committable() const;
  [[nodiscard]] bool retire_confirmation_open() const noexcept {
    return retire_confirmation_id_.has_value();
  }

  [[nodiscard]] ShipDesignWorkspaceLayout layout(int width,
                                                 int height) const noexcept;
  [[nodiscard]] ShipDesignWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width,
         int height);
  void render(stellar::native_map::DrawList &out, int width,
              int height) const;

  [[nodiscard]] int focus() const noexcept { return focus_; }
  [[nodiscard]] std::string
  focused_label(const ShipDesignWorkspaceLayout &) const;

private:
  enum class PressTarget { None, Commit, Retire, Close };
  struct SlotRow {
    stellar::native_map::UiRect bounds, previous, next;
  };
  [[nodiscard]] std::vector<SlotRow>
  slot_rows(const ShipDesignWorkspaceLayout &) const;
  [[nodiscard]] const stellar::native_ship_design::NativeHullOption *
  draft_hull() const noexcept;
  [[nodiscard]] const stellar::native_ship_design::NativeHullOption *
  unlocked_hull(std::size_t index) const noexcept;
  [[nodiscard]] std::vector<
      const stellar::native_ship_design::NativeComponentOption *>
  slot_options(int slot_index) const;
  void select_hull(std::size_t index);
  void cycle_hull(int direction);
  void cycle_component(int slot_index, int direction);
  void reconcile_draft();
  void reset_gesture() noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  std::optional<stellar::native_ship_design::NativeShipDesignView> view_;
  bool visible_{};
  std::optional<std::string> selected_design_id_;
  std::size_t hull_index_{};
  std::vector<std::optional<std::string>> draft_components_;
  std::string notice_;
  bool notice_accepted_{};
  std::optional<std::string> retire_confirmation_id_;
  stellar::native_map::Point pointer_{};
  bool pointer_owned_{};
  int press_width_{}, press_height_{};
  PressTarget pressed_{PressTarget::None};
  int focus_{-1};
};

} // namespace stellar::native_ship_design_ui
