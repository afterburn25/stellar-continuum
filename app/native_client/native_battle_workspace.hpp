#pragma once

// Full-screen tactical battle presentation ported from the reference
// MassiveCombatView. The workspace owns no battle state: it renders only the
// observer-filtered MassiveCombatSnapshot supplied by the host and issues
// commands through the returned BattleWorkspaceCommand values.

#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/massive_combat_observer.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_battle_ui {

using stellar::core::MassiveCombatOrderType;
using stellar::core::MassiveCombatSnapshot;
using stellar::core::MassiveObservedFormation;
using stellar::core::MassivePoint;

struct BattleWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect top_row;
  stellar::native_map::UiRect play;
  stellar::native_map::UiRect speed;
  stellar::native_map::UiRect fit;
  stellar::native_map::UiRect menu;
  stellar::native_map::UiRect orders;
  std::vector<stellar::native_map::UiRect> order_buttons;
  stellar::native_map::UiRect status;
  stellar::native_map::UiRect selection_summary;
  stellar::native_map::UiRect battle_summary;
  stellar::native_map::UiRect event_feed;
  stellar::native_map::UiRect scale_bar;

  [[nodiscard]] static BattleWorkspaceLayout for_viewport(int width,
                                                          int height);
};

// Order bar mirrors the reference grammar; targeted entries complete through
// the targeting pick state.
struct BattleOrderButton {
  std::string label;
  MassiveCombatOrderType type;
  bool needs_target{};
  std::string_view label_key{};
};
[[nodiscard]] const std::vector<BattleOrderButton> &battle_order_buttons();

enum class BattleWorkspaceCommandKind {
  None,
  IssueOrder,
  SetTacticalSpeed,
  TogglePause,
  CycleSpeed,
  Fit,
  Menu,
};

struct BattleWorkspaceCommand {
  BattleWorkspaceCommandKind kind{BattleWorkspaceCommandKind::None};
  bool captured{};
  // Context and non-targeted orders apply to every selected owned formation.
  // Targeted orders intentionally retain a single first-selected source.
  std::vector<stellar::core::MassiveCombatOrder> orders;
  double speed{};
};

struct BattleShipTarget {
  std::int64_t formation_id{};
  stellar::native_map::Point center{};
  float size{};
  float heading_degrees{};
};

class NativeBattleWorkspace final {
public:
  void open(MassiveCombatSnapshot snapshot, int observer_civilization_id,
            int width, int height);
  void close();
  void discard_campaign();
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const MassiveCombatSnapshot *snapshot() const noexcept;

  // Refreshes the observed state; ages transient weapon effects by the real
  // elapsed seconds the host reports each frame.
  void set_snapshot(MassiveCombatSnapshot snapshot, double elapsed_seconds);
  void set_tactical_speed(double current, double resume) noexcept;
  void set_status(std::string message, bool error = false);
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  // Replaces the prior frame's artwork hits. Targets remain valid only while
  // the viewport and camera exactly match the draw that supplied them.
  void set_ship_targets(std::vector<BattleShipTarget> targets, int width,
                        int height);

  [[nodiscard]] BattleWorkspaceCommand handle(
      const stellar::native_map::InputEvent &event, int width, int height);
  using ShipLayer = std::function<void(stellar::native_map::DrawList&,
      const stellar::native_map::UiRect&, float, float)>;
  void render(stellar::native_map::DrawList &out, int width,
              int height, const ShipLayer& ship_layer = {}) const;

  // Projects an observer-snapshot world coordinate; used by tests and smoke
  // diagnostics to place synthetic input deterministically.
  [[nodiscard]] stellar::native_map::Point project(MassivePoint value,
                                                   int width, int height) const;
  // Smoke evidence counters.
  [[nodiscard]] int rendered_tokens() const noexcept {
    return last_rendered_tokens_;
  }
  [[nodiscard]] std::size_t selected_count() const noexcept {
    return selection_.size();
  }
  [[nodiscard]] bool targeting() const noexcept {
    return targeting_source_.has_value();
  }
  [[nodiscard]] const std::set<std::int64_t> &selection() const noexcept {
    return selection_;
  }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string
  focused_label(const BattleWorkspaceLayout &) const;

private:
  [[nodiscard]] stellar::native_map::Point
  to_screen(MassivePoint value, int width, int height) const noexcept;
  [[nodiscard]] MassivePoint to_world(
      stellar::native_map::Point value, int width, int height) const noexcept;
  [[nodiscard]] std::optional<std::int64_t>
  hit_formation(stellar::native_map::Point point, int width,
                int height) const noexcept;
  [[nodiscard]] bool is_owned(
      const MassiveObservedFormation &formation) const noexcept;
  [[nodiscard]] std::vector<std::int64_t> selected_owned() const;
  void fit(int width, int height) noexcept;
  void issue_context(stellar::native_map::Point point, int width, int height,
                     BattleWorkspaceCommand &command) const;
  void adopt_viewport(int width, int height) noexcept;
  void invalidate_ship_targets() noexcept;
  struct FocusRect {
    stellar::native_map::UiRect bounds;
    std::string label;
  };
  [[nodiscard]] std::vector<FocusRect>
  focusables(const BattleWorkspaceLayout &) const;
  [[nodiscard]] bool ship_targets_current(int width, int height) const noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  std::optional<MassiveCombatSnapshot> snapshot_;
  int observer_civilization_id_{-1};
  bool visible_{};
  stellar::native_map::Point camera_center_{};
  int camera_viewport_width_{};
  int camera_viewport_height_{};
  float zoom_{1.f};
  MassivePoint world_center_{};
  bool camera_initialized_{};
  std::uint64_t camera_revision_{};
  std::vector<BattleShipTarget> ship_targets_;
  int ship_targets_width_{};
  int ship_targets_height_{};
  std::uint64_t ship_targets_camera_revision_{};
  enum class Gesture { None, LeftField, RightField, Chrome };
  Gesture gesture_{Gesture::None};
  bool panning_{};
  bool box_selecting_{};
  stellar::native_map::Point pointer_down_{};
  stellar::native_map::Point selection_end_{};
  stellar::native_map::Point pointer_{};
  std::set<std::int64_t> selection_;
  std::optional<std::int64_t> hovered_formation_;
  std::optional<std::int64_t> targeting_source_;
  MassiveCombatOrderType targeting_order_{};
  std::int64_t latest_event_sequence_{};
  double tactical_speed_{1.};
  double tactical_resume_speed_{1.};
  std::string status_;
  bool status_error_{};
  struct VisualEvent {
    stellar::core::MassiveCombatEventType type{};
    MassivePoint start{};
    MassivePoint end{};
    float age{}, lifetime{}, magnitude{1.f};
  };
  std::vector<VisualEvent> visual_events_;
  mutable int last_rendered_tokens_{};
  int focus_{-1};
};

} // namespace stellar::native_battle_ui
