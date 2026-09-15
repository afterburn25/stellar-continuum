#include "native_battle_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_battle_ui {
namespace {
using namespace stellar::native_map;

constexpr float minimum_zoom = .18f, maximum_zoom = 7.f;
constexpr float selection_radius = 28.f;

// Reference palette (VisualPalette).
constexpr Color panel{7, 17, 32, 245};
constexpr Color button_color{12, 31, 54, 245};
constexpr Color hover_color{24, 61, 94, 250};
constexpr Color border{91, 151, 205, 235};
constexpr Color text_primary{235, 244, 255, 255};
constexpr Color text_secondary{151, 180, 207, 245};
constexpr Color success{94, 229, 157, 255};
constexpr Color unknown{124, 152, 176, 255};
constexpr Color danger{255, 107, 96, 255};
constexpr Color caution{245, 177, 82, 255};
constexpr Color focus{102, 178, 255, 255};
constexpr Color selected_color{245, 221, 114, 255};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, Point at, std::string value, Color color,
          int pixels, float wrap = 0.f,
          TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, std::nullopt, align});
}
std::optional<UiRect> intersection(const UiRect &a, const UiRect &b) {
  const auto x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
  const auto x1 = std::min(a.x + a.width, b.x + b.width),
             y1 = std::min(a.y + a.height, b.y + b.height);
  if (x1 <= x0 || y1 <= y0) return std::nullopt;
  return UiRect{x0, y0, x1 - x0, y1 - y0};
}
void clipped_text(DrawList &out, Point at, std::string value, Color color,
                  int pixels, float wrap, const UiRect &clip,
                  TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, clip, align});
}

// Overlay lines carry no clip; Liang–Barsky against the rect.
void clipped_line(DrawList &out, Point a, Point b, Color color,
                  const UiRect &clip) {
  const auto dx = b.x - a.x, dy = b.y - a.y;
  float t0 = 0.f, t1 = 1.f;
  const auto edge = [&](float p, float q) {
    if (p == 0.f) return q >= 0.f;
    const auto r = q / p;
    if (p < 0.f) {
      if (r > t1) return false;
      if (r > t0) t0 = r;
    } else {
      if (r < t0) return false;
      if (r < t1) t1 = r;
    }
    return true;
  };
  if (!edge(-dx, a.x - clip.x) || !edge(dx, clip.x + clip.width - a.x) ||
      !edge(-dy, a.y - clip.y) || !edge(dy, clip.y + clip.height - a.y) ||
      t0 >= t1)
    return;
  out.overlay.emplace_back(Line{{a.x + t0 * dx, a.y + t0 * dy},
                                {a.x + t1 * dx, a.y + t1 * dy}, color});
}
void ring(DrawList &out, Point center, float radius, Color color,
          const UiRect &clip, int segments = 28) {
  for (int i = 0; i < segments; ++i) {
    const auto a0 = static_cast<float>(i) * 6.283185307179586f / segments;
    const auto a1 = static_cast<float>(i + 1) * 6.283185307179586f / segments;
    clipped_line(out,
                 {center.x + std::cos(a0) * radius,
                  center.y + std::sin(a0) * radius},
                 {center.x + std::cos(a1) * radius,
                  center.y + std::sin(a1) * radius},
                 color, clip);
  }
}
void dashed(DrawList &out, Point a, Point b, Color color, const UiRect &clip,
            float dash = 7.f) {
  const auto length = std::hypot(b.x - a.x, b.y - a.y);
  if (length < .001f) return;
  const auto ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
  for (float at = 0.f; at < length; at += dash * 2.f) {
    const auto end = std::min(length, at + dash);
    clipped_line(out, {a.x + ux * at, a.y + uy * at},
                 {a.x + ux * end, a.y + uy * end}, color, clip);
  }
}
float pos_mod(float value, float divisor) {
  return std::fmod(std::fmod(value, divisor) + divisor, divisor);
}
std::string grouped(std::int64_t value) {
  auto text = std::to_string(value);
  for (auto at = static_cast<std::ptrdiff_t>(text.size()) - 3; at > 0; at -= 3)
    text.insert(static_cast<std::size_t>(at), 1, ',');
  return text;
}
std::string order_name(MassiveCombatOrderType type) {
  switch (type) {
  case MassiveCombatOrderType::Engage: return "engage";
  case MassiveCombatOrderType::Hold: return "hold";
  case MassiveCombatOrderType::Defend: return "defend";
  case MassiveCombatOrderType::Advance: return "advance";
  case MassiveCombatOrderType::AdvanceCautiously: return "advance cautiously";
  case MassiveCombatOrderType::StandoffAttack: return "standoff attack";
  case MassiveCombatOrderType::Screen: return "screen";
  case MassiveCombatOrderType::ProtectCriticalAsset:
    return "protect critical asset";
  case MassiveCombatOrderType::FocusFire: return "focus fire";
  case MassiveCombatOrderType::FlankLeft: return "flank left";
  case MassiveCombatOrderType::FlankRight: return "flank right";
  case MassiveCombatOrderType::Intercept: return "intercept";
  case MassiveCombatOrderType::Pursue: return "pursue";
  case MassiveCombatOrderType::BreakContact: return "break contact";
  case MassiveCombatOrderType::Disengage: return "disengage";
  case MassiveCombatOrderType::Retreat: return "retreat";
  case MassiveCombatOrderType::EmergencyRetreat: return "emergency retreat";
  case MassiveCombatOrderType::Breakout: return "breakout";
  case MassiveCombatOrderType::Surrender: return "surrender";
  }
  return "order";
}
std::string shape_name(stellar::core::MassiveFormationShape shape) {
  switch (shape) {
  case stellar::core::MassiveFormationShape::Screen: return "SCREEN";
  case stellar::core::MassiveFormationShape::Line: return "LINE";
  case stellar::core::MassiveFormationShape::Wedge: return "WEDGE";
  case stellar::core::MassiveFormationShape::Standoff: return "STANDOFF";
  case stellar::core::MassiveFormationShape::Dispersed: return "DISPERSED";
  case stellar::core::MassiveFormationShape::Escort: return "ESCORT";
  case stellar::core::MassiveFormationShape::RetreatColumn:
    return "RETREAT COLUMN";
  case stellar::core::MassiveFormationShape::Breakout: return "BREAKOUT";
  }
  return "FORMATION";
}
std::string formation_state(const MassiveObservedFormation &value) {
  if (value.is_warp_blocked) return "WARP BLOCKED";
  if (value.warp_spool_progress > 0.f) {
    std::ostringstream out;
    out << "WARP " << static_cast<int>(value.warp_spool_progress * 100.f)
        << "%";
    return out.str();
  }
  return shape_name(value.shape);
}
float hash01(std::int64_t id, int token) {
  auto value = static_cast<std::uint64_t>(id) ^
               (static_cast<std::uint64_t>(token + 1) *
                0x9E3779B97F4A7C15ULL);
  value ^= value >> 30;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27;
  value *= 0x94D049BB133111EBULL;
  value ^= value >> 31;
  return static_cast<float>(value & 0xffff) / 65535.f;
}
Point formation_offset(std::int64_t id, int token,
                       stellar::core::MassiveFormationShape shape) {
  if (token == 0) return {};
  const auto row = 1 + (token - 1) / 7;
  const auto column = static_cast<float>((token - 1) % 7 - 3);
  const auto jitter = hash01(id, token) - .5f;
  const auto r = static_cast<float>(row);
  using stellar::core::MassiveFormationShape;
  switch (shape) {
  case MassiveFormationShape::Wedge:
    return {-r * 10.f, column * (6.f + r * 1.2f) + jitter * 3.f};
  case MassiveFormationShape::Screen:
    return {column * 10.f + jitter * 3.f, r * 5.f};
  case MassiveFormationShape::Standoff:
    return {column * 11.f, r * 9.f + jitter * 3.f};
  case MassiveFormationShape::Dispersed:
    return {column * 13.f + jitter * 8.f,
            r * 11.f + hash01(id, token + 91) * 8.f};
  case MassiveFormationShape::Escort:
    return {std::cos(static_cast<float>(token) * 2.399f) * r * 10.f,
            std::sin(static_cast<float>(token) * 2.399f) * r * 7.f};
  case MassiveFormationShape::RetreatColumn:
  case MassiveFormationShape::Breakout:
    return {-r * 13.f, column * 5.f + jitter * 3.f};
  default: return {column * 9.f, r * 7.f + jitter * 2.f};
  }
}
} // namespace

const std::vector<BattleOrderButton> &battle_order_buttons() {
  static const std::vector<BattleOrderButton> buttons{
      {"Hold", MassiveCombatOrderType::Hold, false},
      {"Defend", MassiveCombatOrderType::Defend, false},
      {"Advance", MassiveCombatOrderType::Advance, true},
      {"Focus fire", MassiveCombatOrderType::FocusFire, true},
      {"Flank left", MassiveCombatOrderType::FlankLeft, true},
      {"Flank right", MassiveCombatOrderType::FlankRight, true},
      {"Intercept", MassiveCombatOrderType::Intercept, true},
      {"Break contact", MassiveCombatOrderType::BreakContact, false},
      {"Retreat", MassiveCombatOrderType::Retreat, false}};
  return buttons;
}

namespace {
// Chrome rects consume clicks; a press or release over them must never alter
// the field selection or fire a pending targeted order.
bool on_chrome(const stellar::native_map::Point &position,
               const BattleWorkspaceLayout &layout) {
  return layout.top_row.contains(position) || layout.orders.contains(position) ||
         layout.event_feed.contains(position) ||
         layout.status.contains(position) ||
         layout.selection_summary.contains(position) ||
         layout.battle_summary.contains(position) ||
         layout.scale_bar.contains(position);
}
} // namespace

BattleWorkspaceLayout BattleWorkspaceLayout::for_viewport(const int width,
                                                          const int height) {
  const auto scale = std::clamp(static_cast<float>(height) / 720.f, .75f, 2.6f);
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  const auto margin = 12.f * scale;
  BattleWorkspaceLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(17.f * scale);
  layout.body_font_pixels = static_cast<int>(14.f * scale);
  layout.small_font_pixels = static_cast<int>(11.f * scale);
  layout.surface = {0.f, 0.f, w, h};
  layout.top_row = {margin, margin, w - margin * 2.f, 40.f * scale};
  auto x = layout.top_row.x;
  const auto pitch = [&](float button_width) {
    const UiRect rect{x, layout.top_row.y, button_width * scale,
                      34.f * scale};
    x += rect.width + 7.f * scale;
    return rect;
  };
  layout.play = pitch(44.f);
  layout.speed = pitch(64.f);
  layout.fit = pitch(52.f);
  layout.menu = {w - margin - 74.f * scale, layout.top_row.y, 74.f * scale,
                 34.f * scale};
  const auto &buttons = battle_order_buttons();
  layout.orders = {w - margin - 196.f * scale,
                   layout.top_row.y + layout.top_row.height + 10.f * scale,
                   196.f * scale, h - layout.top_row.height - 250.f * scale};
  for (std::size_t index = 0; index < buttons.size(); ++index)
    layout.order_buttons.push_back(
        {layout.orders.x + static_cast<float>(index % 2) * 100.f * scale,
         layout.orders.y + static_cast<float>(index / 2) * 42.f * scale,
         93.f * scale, 34.f * scale});
  layout.battle_summary = {margin, h - 118.f * scale, w - margin * 2.f,
                           20.f * scale};
  layout.selection_summary = {margin, h - 96.f * scale, w - margin * 2.f,
                              20.f * scale};
  layout.status = {margin, h - 66.f * scale, w - margin * 2.f, 22.f * scale};
  layout.event_feed = {margin, layout.top_row.y + layout.top_row.height +
                                   8.f * scale,
                       300.f * scale, 88.f * scale};
  layout.scale_bar = {margin, h - 132.f * scale, 200.f * scale,
                      22.f * scale};
  return layout;
}

void NativeBattleWorkspace::open(MassiveCombatSnapshot snapshot,
                                 int observer_civilization_id, int width,
                                 int height) {
  snapshot_ = std::move(snapshot);
  observer_civilization_id_ = observer_civilization_id;
  visible_ = true;
  if (!camera_initialized_) fit(width, height);
}
void NativeBattleWorkspace::close() {
  visible_ = false;
  snapshot_.reset();
  selection_.clear();
  visual_events_.clear();
  targeting_source_.reset();
  pressed_on_chrome_ = false;
  hovered_formation_.reset();
  latest_event_sequence_ = 0;
  camera_initialized_ = false;
  status_.clear();
  status_error_ = false;
}
void NativeBattleWorkspace::discard_campaign() { close(); }
const MassiveCombatSnapshot *NativeBattleWorkspace::snapshot() const noexcept {
  return snapshot_ ? &*snapshot_ : nullptr;
}

void NativeBattleWorkspace::set_snapshot(MassiveCombatSnapshot snapshot,
                                       double elapsed_seconds) {
  const auto elapsed = static_cast<float>(
      std::max(0., std::isfinite(elapsed_seconds) ? elapsed_seconds : 0.));
  for (auto &event : visual_events_) event.age += elapsed;
  std::erase_if(visual_events_,
                [](const VisualEvent &event) {
                  return event.age >= event.lifetime;
                });
  for (const auto &value : snapshot.events) {
    if (value.sequence <= latest_event_sequence_) continue;
    latest_event_sequence_ =
        std::max(latest_event_sequence_, value.sequence);
    if (!value.details_known || !value.position) continue;
    const auto find_position = [&](std::optional<std::int64_t> id) {
      if (!id) return std::optional<MassivePoint>{};
      for (const auto &formation : snapshot.formations)
        if (formation.formation_id == *id)
          return std::optional<MassivePoint>{formation.position};
      return std::optional<MassivePoint>{};
    };
    const auto source = find_position(value.actor_formation_id);
    const auto target = find_position(value.target_formation_id);
    VisualEvent event;
    event.type = value.type;
    event.start = source.value_or(*value.position);
    event.end = target.value_or(*value.position);
    switch (value.type) {
    case stellar::core::MassiveCombatEventType::BeamVolley:
      event.lifetime = .42f;
      break;
    case stellar::core::MassiveCombatEventType::KineticVolley:
      event.lifetime = .7f;
      break;
    case stellar::core::MassiveCombatEventType::MissileSalvo:
      event.lifetime = 2.2f;
      break;
    case stellar::core::MassiveCombatEventType::MissileIntercepted:
      event.lifetime = .8f;
      break;
    case stellar::core::MassiveCombatEventType::Damage:
      event.lifetime = .65f;
      break;
    default: event.lifetime = .9f; break;
    }
    event.magnitude = static_cast<float>(value.magnitude.value_or(1));
    visual_events_.push_back(event);
  }
  if (visual_events_.size() > 192)
    visual_events_.erase(visual_events_.begin(),
                         visual_events_.end() - 192);
  // Drop selections whose formations left the observed battle.
  for (auto it = selection_.begin(); it != selection_.end();) {
    const auto found =
        std::ranges::any_of(snapshot.formations, [&](const auto &formation) {
          return formation.formation_id == *it;
        });
    it = found ? std::next(it) : selection_.erase(it);
  }
  snapshot_ = std::move(snapshot);
}
void NativeBattleWorkspace::set_tactical_speed(const double current,
                                               const double resume) noexcept {
  tactical_speed_ = current;
  tactical_resume_speed_ = resume;
}
void NativeBattleWorkspace::set_status(std::string message, bool error) {
  status_ = std::move(message);
  status_error_ = error;
}
Point NativeBattleWorkspace::project(const MassivePoint value, const int width,
                                     const int height) const {
  return to_screen(value, width, height);
}

Point NativeBattleWorkspace::to_screen(const MassivePoint value, int width,
                                       int height) const noexcept {
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto top = layout.top_row.y + layout.top_row.height;
  const auto bottom = height - 116.f * layout.scale;
  const auto cx = camera_center_.x > 0.f
                      ? camera_center_.x
                      : static_cast<float>(width) * .5f;
  const auto cy = camera_center_.y > 0.f ? camera_center_.y
                                         : top + (bottom - top) * .5f;
  return {cx + (value.x - world_center_.x) * zoom_,
          cy + (value.y - world_center_.y) * zoom_};
}
MassivePoint
NativeBattleWorkspace::to_world(const Point value, int width,
                                int height) const noexcept {
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto top = layout.top_row.y + layout.top_row.height;
  const auto bottom = height - 116.f * layout.scale;
  const auto cx = camera_center_.x > 0.f
                      ? camera_center_.x
                      : static_cast<float>(width) * .5f;
  const auto cy = camera_center_.y > 0.f ? camera_center_.y
                                         : top + (bottom - top) * .5f;
  const auto z = std::max(zoom_, .0001f);
  return {world_center_.x + (value.x - cx) / z,
          world_center_.y + (value.y - cy) / z};
}
std::optional<std::int64_t> NativeBattleWorkspace::hit_formation(
    const Point point, int width, int height) const noexcept {
  if (!snapshot_) return std::nullopt;
  std::optional<std::int64_t> best;
  auto best_distance = selection_radius * selection_radius;
  for (const auto &formation : snapshot_->formations) {
    const auto center = to_screen(formation.position, width, height);
    const auto dx = point.x - center.x, dy = point.y - center.y;
    const auto distance = dx * dx + dy * dy;
    if (distance <= best_distance &&
        (!best || distance < best_distance ||
         (distance == best_distance && formation.formation_id < *best))) {
      best = formation.formation_id;
      best_distance = distance;
    }
  }
  return best;
}
bool NativeBattleWorkspace::is_owned(
    const MassiveObservedFormation &formation) const noexcept {
  return formation.civilization_id == observer_civilization_id_;
}
std::vector<std::int64_t> NativeBattleWorkspace::selected_owned() const {
  std::vector<std::int64_t> result;
  if (!snapshot_) return result;
  for (const auto id : selection_)
    if (std::ranges::any_of(snapshot_->formations, [&](const auto &formation) {
          return formation.formation_id == id && is_owned(formation);
        }))
      result.push_back(id);
  return result;
}
void NativeBattleWorkspace::fit(int width, int height) noexcept {
  if (!snapshot_ || snapshot_->formations.empty()) {
    camera_initialized_ = true;
    return;
  }
  float min_x = std::numeric_limits<float>::max(),
        max_x = std::numeric_limits<float>::lowest(), min_y = min_x,
        max_y = max_x;
  for (const auto &formation : snapshot_->formations) {
    min_x = std::min(min_x, formation.position.x);
    max_x = std::max(max_x, formation.position.x);
    min_y = std::min(min_y, formation.position.y);
    max_y = std::max(max_y, formation.position.y);
  }
  world_center_ = {(min_x + max_x) * .5f, (min_y + max_y) * .5f};
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto available_w =
      std::max(320.f, static_cast<float>(width) - 120.f * layout.scale);
  const auto available_h =
      std::max(240.f, static_cast<float>(height) - 260.f * layout.scale);
  zoom_ = std::clamp(
      std::min(available_w / std::max(220.f, max_x - min_x),
               available_h / std::max(180.f, max_y - min_y)),
      minimum_zoom, maximum_zoom);
  const auto top = layout.top_row.y + layout.top_row.height;
  camera_center_ = {static_cast<float>(width) * .5f,
                    top + (height - 116.f * layout.scale - top) * .5f};
  camera_initialized_ = true;
}
void NativeBattleWorkspace::issue_context(const Point point, int width,
                                          int height,
                                          BattleWorkspaceCommand &command) const {
  const auto selected = selected_owned();
  if (selected.empty()) {
    command.kind = BattleWorkspaceCommandKind::IssueOrder;
    command.order.formation_id = -1;
    return;
  }
  const auto hit = hit_formation(point, width, height);
  command.kind = BattleWorkspaceCommandKind::IssueOrder;
  command.order.formation_id = selected.front();
  if (hit && *hit != selected.front()) {
    command.order.type = MassiveCombatOrderType::Engage;
    command.order.target_formation_id = *hit;
  } else {
    command.order.type = MassiveCombatOrderType::Advance;
    command.order.objective = to_world(point, width, height);
  }
}

BattleWorkspaceCommand
NativeBattleWorkspace::handle(const InputEvent &event, const int width,
                              const int height) {
  if (!visible_ || !snapshot_) return {};
  pointer_ = event.position;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  BattleWorkspaceCommand command{BattleWorkspaceCommandKind::None, true};

  if (event.type == InputEventType::EscapePressed) {
    if (targeting_source_) {
      targeting_source_.reset();
      set_status("Target selection cancelled.");
    } else {
      command.kind = BattleWorkspaceCommandKind::Menu;
    }
    return command;
  }
  if (event.type == InputEventType::Wheel) {
    const auto factor = event.wheel_y > 0.f ? 1.25f : 1.f / 1.25f;
    const auto before = to_world(event.position, width, height);
    zoom_ = std::clamp(zoom_ * factor, minimum_zoom, maximum_zoom);
    const auto after = to_screen(before, width, height);
    camera_center_.x += event.position.x - after.x;
    camera_center_.y += event.position.y - after.y;
    return command;
  }
  if (event.type == InputEventType::PointerMove) {
    if (panning_) {
      camera_center_.x += event.delta.x;
      camera_center_.y += event.delta.y;
      return command;
    }
    if (event.delta.x != 0.f || event.delta.y != 0.f) {
      selection_end_ = event.position;
      if (!pressed_on_chrome_ &&
          std::hypot(event.position.x - pointer_down_.x,
                     event.position.y - pointer_down_.y) >= 6.f)
        box_selecting_ = true;
    }
    hovered_formation_ = hit_formation(event.position, width, height);
    return command;
  }
  if (event.type == InputEventType::LeftPressed) {
    // Chrome first: top-row and order buttons. The matching release must not
    // fall through to the field selection path.
    pressed_on_chrome_ = false;
    const auto chrome_press = [&] {
      pressed_on_chrome_ = true;
      pointer_down_ = event.position;
    };
    if (layout.play.contains(event.position)) {
      chrome_press();
      command.kind = BattleWorkspaceCommandKind::TogglePause;
      return command;
    }
    if (layout.speed.contains(event.position)) {
      chrome_press();
      command.kind = BattleWorkspaceCommandKind::CycleSpeed;
      return command;
    }
    if (layout.fit.contains(event.position)) {
      chrome_press();
      fit(width, height);
      return command;
    }
    if (layout.menu.contains(event.position)) {
      chrome_press();
      command.kind = BattleWorkspaceCommandKind::Menu;
      return command;
    }
    for (std::size_t index = 0; index < layout.order_buttons.size(); ++index) {
      if (!layout.order_buttons[index].contains(event.position)) continue;
      chrome_press();
      const auto &button = battle_order_buttons()[index];
      const auto selected = selected_owned();
      if (selected.empty()) {
        set_status("Select one or more friendly formations first.", true);
        return command;
      }
      if (button.needs_target) {
        targeting_source_ = selected.front();
        targeting_order_ = button.type;
        set_status(order_name(button.type) +
                   ": choose a formation or open-space objective.");
        return command;
      }
      command.kind = BattleWorkspaceCommandKind::IssueOrder;
      command.order.formation_id = selected.front();
      command.order.type = button.type;
      return command;
    }
    pointer_down_ = selection_end_ = event.position;
    box_selecting_ = false;
    return command;
  }
  if (event.type == InputEventType::LeftReleased) {
    const bool from_chrome = pressed_on_chrome_;
    pressed_on_chrome_ = false;
    if (targeting_source_) {
      // A release over chrome keeps the pick state; the same click that armed
      // a targeted order must not fire it at a button coordinate.
      if (on_chrome(event.position, layout)) return command;
      const auto source = *targeting_source_;
      targeting_source_.reset();
      const auto hit = hit_formation(event.position, width, height);
      command.kind = BattleWorkspaceCommandKind::IssueOrder;
      command.order.formation_id = source;
      command.order.type = targeting_order_;
      if (hit && *hit != source)
        command.order.target_formation_id = *hit;
      else
        command.order.objective = to_world(event.position, width, height);
      return command;
    }
    if (box_selecting_) {
      const auto x0 = std::min(pointer_down_.x, event.position.x),
                 y0 = std::min(pointer_down_.y, event.position.y);
      const auto x1 = std::max(pointer_down_.x, event.position.x),
                 y1 = std::max(pointer_down_.y, event.position.y);
      selection_.clear();
      for (const auto &formation : snapshot_->formations) {
        if (!is_owned(formation)) continue;
        const auto center = to_screen(formation.position, width, height);
        if (center.x >= x0 && center.x <= x1 && center.y >= y0 &&
            center.y <= y1)
          selection_.insert(formation.formation_id);
      }
    } else if (!from_chrome && !on_chrome(event.position, layout)) {
      const auto hit = hit_formation(event.position, width, height);
      selection_.clear();
      if (hit && std::ranges::any_of(
                     snapshot_->formations, [&](const auto &formation) {
                       return formation.formation_id == *hit &&
                              is_owned(formation);
                     }))
        selection_.insert(*hit);
    }
    box_selecting_ = false;
    return command;
  }
  if (event.type == InputEventType::RightPressed) {
    panning_ = true;
    pointer_down_ = event.position;
    return command;
  }
  if (event.type == InputEventType::RightReleased) {
    const auto was_panning =
        panning_ && std::hypot(event.position.x - pointer_down_.x,
                               event.position.y - pointer_down_.y) >= 6.f;
    panning_ = false;
    if (!was_panning) issue_context(event.position, width, height, command);
    return command;
  }
  if (event.type == InputEventType::PointerCancelled) {
    panning_ = false;
    box_selecting_ = false;
    pressed_on_chrome_ = false;
    return command;
  }
  return command;
}

void NativeBattleWorkspace::render(DrawList &out, const int width,
                                   const int height) const {
  if (!visible_) return;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto top = layout.top_row.y + layout.top_row.height;
  const auto bottom = static_cast<float>(height) - 116.f * layout.scale;
  const UiRect field{0.f, top, static_cast<float>(width), bottom - top};

  fill(out, layout.surface, {4, 10, 20, 255});

  // Reference tactical grid.
  const auto spacing = std::clamp(80.f * zoom_, 42.f, 132.f);
  const auto offset_x = pos_mod(camera_center_.x, spacing),
             offset_y = pos_mod(camera_center_.y, spacing);
  for (float gx = offset_x; gx < field.x + field.width; gx += spacing)
    clipped_line(out, {gx, top}, {gx, bottom}, {30, 61, 79, 46}, field);
  for (float gy = std::max(top, offset_y); gy < bottom; gy += spacing)
    clipped_line(out, {0.f, gy}, {field.width, gy}, {30, 61, 79, 46}, field);

  if (!snapshot_) {
    text(out, {field.width * .5f, (top + bottom) * .5f},
         "No tactical encounter is active.", text_secondary,
         layout.body_font_pixels, 0.f, TextAlign::Center);
    return;
  }
  const auto &formations = snapshot_->formations;

  const auto formation_color = [&](const MassiveObservedFormation &formation) {
    if (is_owned(formation)) return success;
    if (!formation.is_exact && !formation.strength_low) return unknown;
    const auto hue =
        static_cast<float>((formation.civilization_id * 67) % 31) / 310.f;
    return Color{255,
                 static_cast<std::uint8_t>((.25f + hue) * 255.f),
                 static_cast<std::uint8_t>((.22f + hue * .45f) * 255.f), 255};
  };

  // Interdiction fields under tokens.
  int fields = 0;
  for (const auto &formation : formations) {
    if (!formation.is_interdicting || fields >= 32) continue;
    ++fields;
    const auto center = to_screen(formation.position, width, height);
    const auto color = formation_color(formation);
    const auto radius =
        std::clamp(78.f * std::sqrt(std::max(.25f, zoom_)), 52.f, 170.f);
    ring(out, center, radius, {color.r, color.g, color.b, 71}, field, 48);
  }

  // Weapon visual events.
  for (const auto &effect : visual_events_) {
    const auto alpha = static_cast<std::uint8_t>(
        std::clamp(1.f - effect.age / effect.lifetime, 0.f, 1.f) * 255.f);
    const auto start = to_screen(effect.start, width, height);
    const auto end = to_screen(effect.end, width, height);
    switch (effect.type) {
    case stellar::core::MassiveCombatEventType::BeamVolley:
      clipped_line(out, start, end, {89, 230, 255, alpha}, field);
      out.circles.push_back({end, 3.f + effect.magnitude / 80.f,
                             {204, 242, 255, alpha}});
      break;
    case stellar::core::MassiveCombatEventType::KineticVolley:
      dashed(out, start, end, {255, 199, 89, alpha}, field, 5.f);
      break;
    case stellar::core::MassiveCombatEventType::MissileSalvo: {
      const auto t = effect.age / effect.lifetime;
      const auto progress = t * t * (3.f - 2.f * t);
      const Point missile{start.x + (end.x - start.x) * progress,
                          start.y + (end.y - start.y) * progress};
      clipped_line(
          out,
          {start.x + (missile.x - start.x) * .35f,
           start.y + (missile.y - start.y) * .35f},
          missile, {255, 107, 46, alpha}, field);
      out.circles.push_back({missile, 3.2f, {255, 224, 140, alpha}});
      break;
    }
    case stellar::core::MassiveCombatEventType::MissileIntercepted:
      ring(out, end, 6.f + effect.age * 18.f, {89, 217, 255, alpha}, field,
           20);
      break;
    case stellar::core::MassiveCombatEventType::Damage:
      out.circles.push_back(
          {end, 5.f + effect.age * 22.f,
           {255, 56, 31, static_cast<std::uint8_t>(alpha * .18f)}});
      ring(out, end, 5.f + effect.age * 22.f, {255, 148, 56, alpha}, field,
           20);
      break;
    default: break;
    }
  }

  // Active missile salvo tracks.
  for (const auto &salvo : snapshot_->active_missile_salvos) {
    if (!salvo.current_position) continue;
    const auto from = to_screen(*salvo.current_position, width, height);
    const auto track = salvo.incoming_to_own ? danger : caution;
    if (salvo.target_position)
      dashed(out, from, to_screen(*salvo.target_position, width, height),
             {track.r, track.g, track.b, 130}, field, 6.f);
    out.circles.push_back({from, 3.4f, {255, 224, 140, 235}});
  }

  // Formation guides and token clusters.
  last_rendered_tokens_ = 0;
  for (const auto &formation : formations) {
    const auto center = to_screen(formation.position, width, height);
    if (center.x < -140.f || center.x > field.width + 140.f ||
        center.y < top - 140.f || center.y > bottom + 140.f)
      continue;
    const auto color = formation_color(formation);
    const auto chosen = selection_.contains(formation.formation_id);
    if (chosen) {
      ring(out, center, 25.f, selected_color, field);
      const MassivePoint future{formation.position.x +
                                    formation.velocity.x * 10.f,
                                formation.position.y +
                                    formation.velocity.y * 10.f};
      dashed(out, center, to_screen(future, width, height),
             {selected_color.r, selected_color.g, selected_color.b, 178},
             field);
    }
    if (hovered_formation_ == formation.formation_id)
      ring(out, center, 31.f, {color.r, color.g, color.b, 230}, field);
    if (formation.is_warp_blocked)
      ring(out, center, 35.f, danger, field);
    if (formation.warp_spool_progress > 0.f) {
      const auto sweep =
          static_cast<int>(28.f * formation.warp_spool_progress);
      for (int i = 0; i < sweep; ++i) {
        const auto a0 = -1.57079632679f +
                        static_cast<float>(i) * 6.283185307179586f / 28.f;
        const auto a1 = -1.57079632679f +
                        static_cast<float>(i + 1) * 6.283185307179586f / 28.f;
        clipped_line(out,
                     {center.x + std::cos(a0) * 39.f,
                      center.y + std::sin(a0) * 39.f},
                     {center.x + std::cos(a1) * 39.f,
                      center.y + std::sin(a1) * 39.f},
                     focus, field);
      }
    }
    if (targeting_source_ && *targeting_source_ == formation.formation_id)
      ring(out, center, 43.f, focus, field, 36);

    // Token sample: bounded by zoom tier and the 4096 reference pool cap.
    const auto midpoint = std::max(
        1, (formation.ship_count_low + formation.ship_count_high) / 2);
    const auto desired =
        zoom_ < .55f
            ? 1
            : zoom_ < 1.15f
                  ? std::clamp(static_cast<int>(std::ceil(
                                   std::sqrt(static_cast<float>(midpoint)) /
                                   2.4f)),
                               1, 12)
                  : std::clamp(static_cast<int>(std::ceil(
                                   std::sqrt(static_cast<float>(midpoint)) /
                                   1.35f)),
                               2, 28);
    const auto heading_length = std::hypot(formation.velocity.x,
                                           formation.velocity.y);
    const Point heading{heading_length > .0001f
                            ? formation.velocity.x / heading_length
                            : 1.f,
                        heading_length > .0001f
                            ? formation.velocity.y / heading_length
                            : 0.f};
    for (int token = 0; token < desired &&
                        last_rendered_tokens_ < 4096;
         ++token) {
      const auto offset = formation_offset(
          formation.formation_id, token, formation.shape);
      const Point at{center.x + offset.x * std::max(.6f, zoom_ * .8f),
                     center.y + offset.y * std::max(.6f, zoom_ * .8f)};
      const auto radius = (token == 0 ? 3.6f : 2.7f) *
                          (chosen ? 1.28f : 1.f) * layout.scale;
      out.circles.push_back({at, radius, color});
      if (token == 0)
        out.circles.push_back(
            {{at.x + heading.x * (radius + 3.f),
              at.y + heading.y * (radius + 3.f)},
             1.4f, color});
      ++last_rendered_tokens_;
    }

    // Important vessel diamonds render as small discs around the token cloud.
    if (zoom_ >= .72f) {
      const auto visible =
          std::min<std::size_t>(formation.important_vessels.size(),
                                zoom_ > 2.f ? 20 : 8);
      for (std::size_t index = 0; index < visible; ++index) {
        const auto &vessel = formation.important_vessels[index];
        const auto angle = static_cast<float>(index) * 2.399963f;
        const auto radius = 28.f + 8.f * std::sqrt(static_cast<float>(index));
        const Point at{center.x + std::cos(angle) * radius,
                       center.y + std::sin(angle) * radius};
        const auto size = vessel.is_flagship      ? 4.f
                          : vessel.is_carrier || vessel.is_interdictor
                              ? 3.2f
                              : 2.4f;
        const auto vessel_color =
            vessel.is_critically_damaged
                ? danger
                : Color{static_cast<std::uint8_t>(
                            std::min(255, color.r + 46)),
                        static_cast<std::uint8_t>(
                            std::min(255, color.g + 46)),
                        static_cast<std::uint8_t>(
                            std::min(255, color.b + 46)),
                        255};
        out.circles.push_back({at, size, vessel_color});
        if (vessel.is_flagship)
          ring(out, at, size + 3.f, caution, field, 12);
        if (vessel.is_interdictor)
          ring(out, at, size + 5.f, {color.r, color.g, color.b, 140}, field,
               12);
      }
    }
  }

  // Labels with a bounded collision budget.
  const auto label_budget =
      zoom_ < .5f ? 48 : zoom_ < 1.1f ? 120 : 360;
  std::vector<UiRect> occupied;
  occupied.reserve(64);
  int labelled = 0;
  for (const auto &formation : formations) {
    const auto center = to_screen(formation.position, width, height);
    if (center.x < -100.f || center.y < top - 50.f ||
        center.x > field.width + 100.f || center.y > bottom + 80.f)
      continue;
    const auto priority = selection_.contains(formation.formation_id) ||
                          hovered_formation_ == formation.formation_id;
    if (!priority && labelled >= label_budget) continue;
    const auto label_offset = is_owned(formation) ? 19.f : -229.f;
    const UiRect bounds{center.x + label_offset - 3.f, center.y - 17.f,
                        220.f,
                        hovered_formation_ == formation.formation_id ? 48.f
                                                                     : 34.f};
    const auto overlaps = std::ranges::any_of(occupied, [&](const UiRect &box) {
      return intersection({box.x - 4.f, box.y - 4.f, box.width + 8.f,
                           box.height + 8.f},
                          bounds)
          .has_value();
    });
    if (!priority && overlaps) continue;
    occupied.push_back(bounds);
    ++labelled;
    const auto color = formation_color(formation);
    const auto count =
        formation.is_exact
            ? grouped(formation.ship_count_low)
            : grouped(formation.ship_count_low) + "-" +
                  grouped(formation.ship_count_high);
    clipped_text(out, {center.x + label_offset, center.y - 5.f},
                 formation.display_name, text_primary,
                 static_cast<int>(12.f * layout.scale), 210.f, bounds);
    clipped_text(out, {center.x + label_offset, center.y + 11.f},
                 count + " ships · " + formation_state(formation), color,
                 layout.small_font_pixels, 220.f, bounds);
    if (hovered_formation_ == formation.formation_id) {
      std::string strength = "Power unknown";
      if (formation.strength_low) {
        strength = formation.strength_low == formation.strength_high
                       ? "Power " + grouped(static_cast<std::int64_t>(
                                          *formation.strength_low))
                       : "Power " + grouped(static_cast<std::int64_t>(
                                          *formation.strength_low)) +
                             "-" +
                             grouped(static_cast<std::int64_t>(
                                 *formation.strength_high));
      }
      clipped_text(out, {center.x + label_offset, center.y + 27.f},
                   strength, text_secondary, layout.small_font_pixels,
                   220.f, bounds);
    }
  }

  // Box-select rectangle while dragging.
  if (box_selecting_) {
    const UiRect box{std::min(pointer_down_.x, selection_end_.x),
                     std::min(pointer_down_.y, selection_end_.y),
                     std::abs(selection_end_.x - pointer_down_.x),
                     std::abs(selection_end_.y - pointer_down_.y)};
    fill(out, box, {94, 229, 157, 24});
    stroke(out, box, {94, 229, 157, 140});
  }

  // Scale bar (110 px at current zoom).
  {
    const auto pixels = 110.f;
    const auto kilometres = pixels / std::max(zoom_, .0001f);
    const auto y = bottom - 12.f;
    clipped_line(out, {18.f, y}, {18.f + pixels, y}, text_secondary, field);
    clipped_line(out, {18.f, y - 4.f}, {18.f, y + 4.f}, text_secondary,
                 field);
    clipped_line(out, {18.f + pixels, y - 4.f}, {18.f + pixels, y + 4.f},
                 text_secondary, field);
    std::ostringstream scale_label;
    scale_label << static_cast<int>(kilometres) << " km";
    text(out, {18.f, y - 8.f}, scale_label.str(), text_secondary,
         layout.small_font_pixels);
  }

  // Bottom summary + status.
  {
    const auto selected =
        std::ranges::count_if(formations, [&](const auto &formation) {
          return selection_.contains(formation.formation_id);
        });
    std::string summary =
        selected == 0
            ? "Select or drag around friendly formations · right-click to "
              "engage or advance · right-drag to pan · wheel to zoom"
            : std::to_string(selected) + " formation" +
                  (selected == 1 ? "" : "s");
    if (selected > 0) {
      std::int64_t low = 0, high = 0;
      bool exact = true;
      for (const auto &formation : formations)
        if (selection_.contains(formation.formation_id)) {
          low += formation.ship_count_low;
          high += formation.ship_count_high;
          exact &= formation.is_exact;
        }
      summary += " · " + grouped(low) +
                 (exact ? " ships" : "-" + grouped(high) + " estimated ships");
    }
    text(out, {layout.selection_summary.x, layout.selection_summary.y},
         summary, text_secondary, layout.body_font_pixels);
    const auto friendly = static_cast<int>(
        std::ranges::count_if(formations,
                              [&](const auto &f) { return is_owned(f); }));
    const auto contacts =
        static_cast<int>(formations.size()) - friendly;
    std::ostringstream battle;
    battle << "T+" << std::fixed << std::setprecision(1)
           << snapshot_->simulated_seconds << "s"
           << "  ·  " << grouped(snapshot_->exact_own_ships)
           << " friendly ships  ·  " << contacts << " detected hostile "
           << "formation" << (contacts == 1 ? "" : "s");
    text(out, {layout.battle_summary.x, layout.battle_summary.y},
         battle.str(), text_secondary, layout.body_font_pixels);
  }

  // Recent combat events feed (observer-filtered snapshot tail).
  {
    int lines = 0;
    for (auto it = snapshot_->events.rbegin();
         it != snapshot_->events.rend() && lines < 4; ++it, ++lines) {
      const auto y = layout.event_feed.y + layout.event_feed.height -
                     static_cast<float>(lines + 1) * 20.f * layout.scale;
      clipped_text(out, {layout.event_feed.x, y},
                   it->details_known ? it->message : "Signal intercept.",
                   it->details_known ? text_secondary : unknown,
                   layout.small_font_pixels, layout.event_feed.width,
                   layout.event_feed);
    }
  }

  // Status line.
  if (!status_.empty())
    text(out, {layout.status.x, layout.status.y}, status_,
         status_error_ ? danger : text_secondary, layout.body_font_pixels);

  // Top row: play/pause, speed, fit, menu.
  fill(out, layout.play,
       layout.play.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.play, border);
  text(out, {layout.play.x + layout.play.width * .5f,
             layout.play.y + layout.play.height * .32f},
       tactical_speed_ > 0. ? "| |" : ">", text_primary,
       layout.body_font_pixels, 0.f, TextAlign::Center);
  fill(out, layout.speed,
       layout.speed.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.speed, border);
  {
    std::ostringstream label;
    label << "> " << tactical_resume_speed_ << "x";
    text(out, {layout.speed.x + layout.speed.width * .5f,
               layout.speed.y + layout.speed.height * .32f},
         label.str(),
         tactical_speed_ > 0. ? text_primary : caution,
         layout.body_font_pixels, 0.f, TextAlign::Center);
  }
  fill(out, layout.fit,
       layout.fit.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.fit, border);
  text(out, {layout.fit.x + layout.fit.width * .5f,
             layout.fit.y + layout.fit.height * .32f},
         "FIT", text_primary, layout.body_font_pixels, 0.f,
         TextAlign::Center);
  fill(out, layout.menu,
       layout.menu.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.menu, border);
  text(out, {layout.menu.x + layout.menu.width * .5f,
             layout.menu.y + layout.menu.height * .32f},
         "MENU", text_primary, layout.body_font_pixels, 0.f,
         TextAlign::Center);

  // Order column.
  for (std::size_t index = 0; index < layout.order_buttons.size(); ++index) {
    const auto &button = battle_order_buttons()[index];
    const auto &rect = layout.order_buttons[index];
    const auto active_targeting =
        targeting_source_ && targeting_order_ == button.type;
    fill(out, rect,
         active_targeting ? selected_color
                          : rect.contains(pointer_) ? hover_color
                                                    : button_color);
    stroke(out, rect, active_targeting ? focus : border);
    clipped_text(out,
                 {rect.x + rect.width * .5f, rect.y + rect.height * .3f},
                 button.label, text_primary, layout.small_font_pixels,
                 rect.width - 6.f, rect, TextAlign::Center);
  }
}
} // namespace stellar::native_battle_ui
