#pragma once

// Full-screen tactical battle presentation ported from the reference
// MassiveCombatView. The workspace owns no battle state: it renders only the
// observer-filtered MassiveCombatSnapshot supplied by the host and issues
// commands through the returned BattleWorkspaceCommand values.

#include <stellar/core/massive_combat_engine.hpp>
#include <stellar/core/massive_combat_observer.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
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
  stellar::core::MassiveCombatOrder order{};
  double speed{};
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

  [[nodiscard]] BattleWorkspaceCommand handle(
      const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width,
              int height) const;

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

  std::optional<MassiveCombatSnapshot> snapshot_;
  int observer_civilization_id_{-1};
  bool visible_{};
  stellar::native_map::Point camera_center_{};
  float zoom_{1.f};
  MassivePoint world_center_{};
  bool camera_initialized_{};
  bool panning_{};
  bool box_selecting_{};
  bool pressed_on_chrome_{};
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
};

} // namespace stellar::native_battle_ui
