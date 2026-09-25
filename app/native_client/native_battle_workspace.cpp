#include "native_battle_workspace.hpp"

#include "native_ui_theme.hpp"

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
using stellar::core::MassiveCombatOrder;

constexpr float minimum_zoom = .18f, maximum_zoom = 7.f;
constexpr float selection_radius = 28.f;
constexpr float maximum_world_coordinate = 10'000'000.f;
constexpr float maximum_screen_coordinate = 1'000'000.f;
constexpr float maximum_ship_target_size = 2'000.f;
constexpr std::size_t maximum_ship_targets = 32;
constexpr float degrees_to_radians = .01745329251994329577f;

[[nodiscard]] bool finite_point(Point value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::abs(value.x) <= maximum_screen_coordinate &&
         std::abs(value.y) <= maximum_screen_coordinate;
}
[[nodiscard]] bool valid_world_point(MassivePoint value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::abs(value.x) <= maximum_world_coordinate &&
         std::abs(value.y) <= maximum_world_coordinate && std::isfinite(value.z) && std::abs(value.z)<=maximum_world_coordinate;
}

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
constexpr Color focus_color{102, 178, 255, 255};
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
  if (!finite_point(a) || !finite_point(b)) return;
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
  if (!finite_point(center) || !std::isfinite(radius) || radius <= 0.f ||
      segments <= 0 || segments > 64)
    return;
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
void disc(DrawList &out, Point center, float radius, Color color,
          const UiRect &clip) {
  if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
      !std::isfinite(radius) || radius <= 0.f ||
      !intersection({center.x-radius,center.y-radius,radius*2.f,radius*2.f},clip)) return;
  TriangleMesh mesh;
  mesh.color = color;
  mesh.clip = clip;
  mesh.vertices.push_back(center);
  constexpr int segments = 12;
  for (int i=0;i<segments;++i) mesh.vertices.push_back({center.x+std::cos(i*6.283185307f/segments)*radius,center.y+std::sin(i*6.283185307f/segments)*radius});
  for (int i=0;i<segments;++i) { mesh.indices.push_back(0); mesh.indices.push_back(i+1); mesh.indices.push_back((i+1)%segments+1); }
  out.overlay.emplace_back(std::move(mesh));
}
void dashed(DrawList &out, Point a, Point b, Color color, const UiRect &clip,
            float dash = 7.f) {
  if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
      !std::isfinite(b.y) || dash <= 0.f) return;
  // Clip once before subdivision: distant world endpoints cannot turn into an
  // unbounded projected dash loop.
  const auto dx = b.x - a.x, dy = b.y - a.y;
  float t0 = 0.f, t1 = 1.f;
  const auto edge = [&](float p, float q) { if (p == 0.f) return q >= 0.f; const auto r=q/p; if(p<0.f){if(r>t1)return false;t0=std::max(t0,r);}else{if(r<t0)return false;t1=std::min(t1,r);} return true; };
  if (!edge(-dx,a.x-clip.x)||!edge(dx,clip.x+clip.width-a.x)||!edge(-dy,a.y-clip.y)||!edge(dy,clip.y+clip.height-a.y)||t0>=t1) return;
  a={a.x+t0*dx,a.y+t0*dy}; b={a.x+(t1-t0)*dx,a.y+(t1-t0)*dy};
  const auto length = std::hypot(b.x - a.x, b.y - a.y);
  if (length < .001f) return;
  const auto ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
  constexpr int maximum_dashes = 256;
  for (float at = 0.f, count = 0; at < length && count < maximum_dashes; at += dash * 2.f, ++count) {
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
std::string translate(const stellar::engine::LocalizationTable *locale,
                      std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}
std::string order_name(MassiveCombatOrderType type,
                       const stellar::engine::LocalizationTable *locale) {
  switch (type) {
  case MassiveCombatOrderType::Engage: return translate(locale,"BATTLE_ORDER_ENGAGE","engage");
  case MassiveCombatOrderType::Hold: return translate(locale,"BATTLE_ORDER_HOLD","hold");
  case MassiveCombatOrderType::Defend: return translate(locale,"BATTLE_ORDER_DEFEND","defend");
  case MassiveCombatOrderType::Advance: return translate(locale,"BATTLE_ORDER_ADVANCE","advance");
  case MassiveCombatOrderType::AdvanceCautiously: return translate(locale,"BATTLE_ORDER_ADVANCE_CAUTIOUS","advance cautiously");
  case MassiveCombatOrderType::StandoffAttack: return translate(locale,"BATTLE_ORDER_STANDOFF","standoff attack");
  case MassiveCombatOrderType::Screen: return translate(locale,"BATTLE_ORDER_SCREEN","screen");
  case MassiveCombatOrderType::ProtectCriticalAsset:
    return translate(locale,"BATTLE_ORDER_PROTECT","protect critical asset");
  case MassiveCombatOrderType::FocusFire: return translate(locale,"BATTLE_ORDER_FOCUS","focus fire");
  case MassiveCombatOrderType::FlankLeft: return translate(locale,"BATTLE_ORDER_FLANK_LEFT","flank left");
  case MassiveCombatOrderType::FlankRight: return translate(locale,"BATTLE_ORDER_FLANK_RIGHT","flank right");
  case MassiveCombatOrderType::Intercept: return translate(locale,"BATTLE_ORDER_INTERCEPT","intercept");
  case MassiveCombatOrderType::Pursue: return translate(locale,"BATTLE_ORDER_PURSUE","pursue");
  case MassiveCombatOrderType::BreakContact: return translate(locale,"BATTLE_ORDER_BREAK","break contact");
  case MassiveCombatOrderType::Disengage: return translate(locale,"BATTLE_ORDER_DISENGAGE","disengage");
  case MassiveCombatOrderType::Retreat: return translate(locale,"BATTLE_ORDER_RETREAT","retreat");
  case MassiveCombatOrderType::EmergencyRetreat: return translate(locale,"BATTLE_ORDER_EMERGENCY","emergency retreat");
  case MassiveCombatOrderType::Breakout: return translate(locale,"BATTLE_ORDER_BREAKOUT","breakout");
  case MassiveCombatOrderType::Surrender: return translate(locale,"BATTLE_ORDER_SURRENDER","surrender");
  }
  return translate(locale,"BATTLE_ORDER_GENERIC","order");
}
std::string shape_name(stellar::core::MassiveFormationShape shape,
                       const stellar::engine::LocalizationTable *locale) {
  switch (shape) {
  case stellar::core::MassiveFormationShape::Screen: return translate(locale,"BATTLE_SHAPE_SCREEN","SCREEN");
  case stellar::core::MassiveFormationShape::Line: return translate(locale,"BATTLE_SHAPE_LINE","LINE");
  case stellar::core::MassiveFormationShape::Wedge: return translate(locale,"BATTLE_SHAPE_WEDGE","WEDGE");
  case stellar::core::MassiveFormationShape::Standoff: return translate(locale,"BATTLE_SHAPE_STANDOFF","STANDOFF");
  case stellar::core::MassiveFormationShape::Dispersed: return translate(locale,"BATTLE_SHAPE_DISPERSED","DISPERSED");
  case stellar::core::MassiveFormationShape::Escort: return translate(locale,"BATTLE_SHAPE_ESCORT","ESCORT");
  case stellar::core::MassiveFormationShape::RetreatColumn:
    return translate(locale,"BATTLE_SHAPE_RETREAT","RETREAT COLUMN");
  case stellar::core::MassiveFormationShape::Breakout: return translate(locale,"BATTLE_SHAPE_BREAKOUT","BREAKOUT");
  }
  return translate(locale,"BATTLE_SHAPE_GENERIC","FORMATION");
}
std::string formation_state(const MassiveObservedFormation &value,
                            const stellar::engine::LocalizationTable *locale) {
  if (value.is_warp_blocked) return translate(locale,"BATTLE_WARP_BLOCKED","WARP BLOCKED");
  if (value.warp_spool_progress > 0.f) {
    auto pattern = translate(locale, "BATTLE_WARP_SPOOL", "WARP {0}%");
    const auto percent =
        std::to_string(static_cast<int>(value.warp_spool_progress * 100.f));
    if (const auto at = pattern.find("{0}"); at != std::string::npos)
      pattern.replace(at, 3, percent);
    return pattern;
  }
  return shape_name(value.shape, locale);
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
      {"Hold", MassiveCombatOrderType::Hold, false, "BATTLE_BTN_HOLD"},
      {"Defend", MassiveCombatOrderType::Defend, false, "BATTLE_BTN_DEFEND"},
      {"Advance", MassiveCombatOrderType::Advance, true, "BATTLE_BTN_ADVANCE"},
      {"Focus fire", MassiveCombatOrderType::FocusFire, true, "BATTLE_BTN_FOCUS"},
      {"Flank left", MassiveCombatOrderType::FlankLeft, true, "BATTLE_BTN_FLANK_LEFT"},
      {"Flank right", MassiveCombatOrderType::FlankRight, true, "BATTLE_BTN_FLANK_RIGHT"},
      {"Intercept", MassiveCombatOrderType::Intercept, true, "BATTLE_BTN_INTERCEPT"},
      {"Break contact", MassiveCombatOrderType::BreakContact, false, "BATTLE_BTN_BREAK"},
      {"Retreat", MassiveCombatOrderType::Retreat, false, "BATTLE_BTN_RETREAT"}};
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
Point battlefield_center(int width, int height) {
  const auto layout=BattleWorkspaceLayout::for_viewport(width,height);
  const auto top=layout.top_row.y+layout.top_row.height;
  const auto bottom=static_cast<float>(height)-116.f*layout.scale;
  const auto left=layout.event_feed.x+layout.event_feed.width+20.f*layout.scale;
  const auto right=layout.orders.x-20.f*layout.scale;
  return {(left+right)*.5f,top+(bottom-top)*.5f};
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
  layout.body_font_pixels = stellar::native_ui::type::compact_body(scale);
  layout.small_font_pixels = stellar::native_ui::type::compact_small(scale);
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
                       330.f * scale, 168.f * scale};
  layout.scale_bar = {margin, h - 152.f * scale, 200.f * scale,
                      30.f * scale};
  return layout;
}

void NativeBattleWorkspace::open(MassiveCombatSnapshot snapshot,
                                 int observer_civilization_id, int width,
                                 int height) {
  snapshot_ = std::move(snapshot);
  invalidate_ship_targets();
  observer_civilization_id_ = observer_civilization_id;
  visible_ = true;
  focus_ = -1;
  if (!camera_initialized_) fit(width, height);
}
void NativeBattleWorkspace::close() {
  visible_ = false;
  focus_ = -1;
  snapshot_.reset();
  selection_.clear();
  visual_events_.clear();
  targeting_source_.reset();
  gesture_ = Gesture::None;
  panning_=false;
  box_selecting_=false;
  hovered_formation_.reset();
  latest_event_sequence_ = 0;
  camera_initialized_ = false;
  camera_viewport_width_ = camera_viewport_height_ = 0;
  ++camera_revision_;
  invalidate_ship_targets();
  status_.clear();
  status_error_ = false;
}
void NativeBattleWorkspace::discard_campaign() { close(); }
const MassiveCombatSnapshot *NativeBattleWorkspace::snapshot() const noexcept {
  return snapshot_ ? &*snapshot_ : nullptr;
}

void NativeBattleWorkspace::set_snapshot(MassiveCombatSnapshot snapshot,
                                       double elapsed_seconds) {
  const auto same_battle = snapshot_ && snapshot_->battle_id == snapshot.battle_id;
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
    if(!valid_world_point(event.start)||!valid_world_point(event.end))continue;
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
  if(targeting_source_&&!std::ranges::any_of(snapshot.formations,
      [&](const auto&formation){return formation.formation_id==*targeting_source_&&
                                      is_owned(formation);}))
    targeting_source_.reset();
  if (!same_battle) {
    invalidate_ship_targets();
  } else {
    std::erase_if(ship_targets_, [&](const BattleShipTarget &target) {
      return std::ranges::none_of(
          snapshot.formations, [&](const MassiveObservedFormation &formation) {
            return formation.formation_id == target.formation_id &&
                   is_owned(formation) && formation.is_exact;
          });
    });
  }
  snapshot_ = std::move(snapshot);
}
void NativeBattleWorkspace::set_tactical_speed(const double current,
                                               const double resume) noexcept {
  tactical_speed_ = current;
  tactical_resume_speed_ = resume;
}
std::string NativeBattleWorkspace::tr(std::string_view key,
                                      std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}
std::string NativeBattleWorkspace::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale_->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

void NativeBattleWorkspace::set_status(std::string message, bool error) {
  status_ = std::move(message);
  status_error_ = error;
}
void NativeBattleWorkspace::set_ship_targets(
    std::vector<BattleShipTarget> targets, const int width, const int height) {
  invalidate_ship_targets();
  if (!visible_ || !snapshot_ || !camera_initialized_ || width <= 0 ||
      height <= 0)
    return;
  adopt_viewport(width, height);
  ship_targets_.reserve(std::min(targets.size(), maximum_ship_targets));
  for (const auto &target : targets) {
    if (ship_targets_.size() >= maximum_ship_targets)
      break;
    if (target.formation_id <= 0 || !finite_point(target.center) ||
        !std::isfinite(target.size) || target.size <= 0 ||
        target.size > maximum_ship_target_size ||
        !std::isfinite(target.heading_degrees) ||
        std::abs(target.heading_degrees) > 1'000'000.f)
      continue;
    const auto formation = std::ranges::find(
        snapshot_->formations, target.formation_id,
        &MassiveObservedFormation::formation_id);
    if (formation == snapshot_->formations.end() || !is_owned(*formation) ||
        !formation->is_exact)
      continue;
    ship_targets_.push_back(target);
  }
  ship_targets_width_ = width;
  ship_targets_height_ = height;
  ship_targets_camera_revision_ = camera_revision_;
}
void NativeBattleWorkspace::invalidate_ship_targets() noexcept {
  ship_targets_.clear();
  ship_targets_width_ = ship_targets_height_ = 0;
  ship_targets_camera_revision_ = camera_revision_;
}
bool NativeBattleWorkspace::ship_targets_current(const int width,
                                                 const int height) const noexcept {
  return width == ship_targets_width_ && height == ship_targets_height_ &&
         camera_revision_ == ship_targets_camera_revision_;
}
Point NativeBattleWorkspace::project(const MassivePoint value, const int width,
                                     const int height) const {
  return to_screen(value, width, height);
}

Point NativeBattleWorkspace::to_screen(const MassivePoint value, int width,
                                       int height) const noexcept {
  if(!valid_world_point(value))return {std::numeric_limits<float>::quiet_NaN(),
                                       std::numeric_limits<float>::quiet_NaN()};
  const auto next_center=battlefield_center(width,height);
  const auto previous_center=camera_viewport_width_>0&&camera_viewport_height_>0
      ?battlefield_center(camera_viewport_width_,camera_viewport_height_)
      :next_center;
  const auto cx=camera_initialized_?next_center.x+camera_center_.x-previous_center.x:next_center.x;
  const auto cy=camera_initialized_?next_center.y+camera_center_.y-previous_center.y:next_center.y;
  return {cx + (value.x - world_center_.x) * zoom_,
          cy + (value.y - world_center_.y - .35f * value.z) * zoom_};
}
MassivePoint
NativeBattleWorkspace::to_world(const Point value, int width,
                                int height) const noexcept {
  if(!finite_point(value))return {std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::quiet_NaN()};
  const auto next_center=battlefield_center(width,height);
  const auto previous_center=camera_viewport_width_>0&&camera_viewport_height_>0
      ?battlefield_center(camera_viewport_width_,camera_viewport_height_)
      :next_center;
  const auto cx=camera_initialized_?next_center.x+camera_center_.x-previous_center.x:next_center.x;
  const auto cy=camera_initialized_?next_center.y+camera_center_.y-previous_center.y:next_center.y;
  const auto z = std::max(zoom_, .0001f);
  return {world_center_.x + (value.x - cx) / z,
          world_center_.y + (value.y - cy) / z};
}
std::optional<std::int64_t> NativeBattleWorkspace::hit_formation(
    const Point point, int width, int height) const noexcept {
  if (!snapshot_) return std::nullopt;
  if (ship_targets_current(width, height)) {
    for (auto target = ship_targets_.rbegin(); target != ship_targets_.rend();
         ++target) {
      const auto radians = target->heading_degrees * degrees_to_radians;
      const auto cosine = std::cos(radians);
      const auto sine = std::sin(radians);
      const auto dx = point.x - target->center.x;
      const auto dy = point.y - target->center.y;
      const auto local_x = cosine * dx + sine * dy;
      const auto local_y = -sine * dx + cosine * dy;
      const auto radius_x = target->size * .46f;
      const auto radius_y = target->size * .17f;
      const auto normalized = local_x * local_x / (radius_x * radius_x) +
                              local_y * local_y / (radius_y * radius_y);
      if (std::isfinite(normalized) && normalized <= 1.f)
        return target->formation_id;
    }
  }
  std::optional<std::int64_t> best;
  auto best_distance = selection_radius * selection_radius;
  for (const auto &formation : snapshot_->formations) {
    const auto center = to_screen(formation.position, width, height);
    if(!finite_point(center))continue;
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
  ++camera_revision_;
  invalidate_ship_targets();
  if (!snapshot_ || snapshot_->formations.empty()) {
    world_center_={};zoom_=1.f;camera_center_=battlefield_center(width,height);
    camera_viewport_width_=width;camera_viewport_height_=height;
    camera_initialized_ = true;
    return;
  }
  float min_x = std::numeric_limits<float>::max(),
        max_x = std::numeric_limits<float>::lowest(), min_y = min_x,
        max_y = max_x;
  bool found{};
  for (const auto &formation : snapshot_->formations) {
    if(!valid_world_point(formation.position))continue;
    found=true;
    min_x = std::min(min_x, formation.position.x);
    max_x = std::max(max_x, formation.position.x);
    min_y = std::min(min_y, formation.position.y-.35f*formation.position.z);
    max_y = std::max(max_y, formation.position.y-.35f*formation.position.z);
  }
  if(!found){world_center_={};zoom_=1.f;camera_center_=battlefield_center(width,height);camera_viewport_width_=width;camera_viewport_height_=height;camera_initialized_=true;return;}
  world_center_ = {(min_x + max_x) * .5f, (min_y + max_y) * .5f};
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto available_w =
      std::max(120.f, layout.orders.x-layout.event_feed.x-layout.event_feed.width-
                         80.f*layout.scale);
  const auto available_h =
      std::max(240.f, static_cast<float>(height) - 260.f * layout.scale);
  zoom_ = std::clamp(
      std::min(available_w / std::max(220.f, max_x - min_x),
               available_h / std::max(180.f, max_y - min_y)),
      minimum_zoom, maximum_zoom);
  camera_center_=battlefield_center(width,height);
  camera_viewport_width_=width;
  camera_viewport_height_=height;
  camera_initialized_ = true;
}
void NativeBattleWorkspace::adopt_viewport(int width,int height) noexcept {
  if(!camera_initialized_||width<=0||height<=0)return;
  if(width==camera_viewport_width_&&height==camera_viewport_height_)return;
  camera_center_=to_screen(world_center_,width,height);
  if(!finite_point(camera_center_))camera_center_=battlefield_center(width,height);
  camera_viewport_width_=width;
  camera_viewport_height_=height;
  ++camera_revision_;
  invalidate_ship_targets();
}
void NativeBattleWorkspace::issue_context(const Point point, int width,
                                          int height,
                                          BattleWorkspaceCommand &command) const {
  const auto selected = selected_owned();
  if (selected.empty()) {
    return;
  }
  const auto hit = hit_formation(point, width, height);
  for (const auto source : selected) { MassiveCombatOrder order; order.formation_id=source;
    if (hit && *hit != source) { order.type=MassiveCombatOrderType::Engage; order.target_formation_id=*hit; }
    else { auto objective=to_world(point,width,height);const auto current=std::ranges::find(snapshot_->formations,source,&MassiveObservedFormation::formation_id);if(current!=snapshot_->formations.end()){objective.z=current->position.z;objective.y+=.35f*objective.z;}if(!valid_world_point(objective))continue;order.type=MassiveCombatOrderType::Advance; order.objective=objective; }
    command.orders.push_back(std::move(order)); }
  if(!command.orders.empty())command.kind=BattleWorkspaceCommandKind::IssueOrder;
}

std::vector<NativeBattleWorkspace::FocusRect>
NativeBattleWorkspace::focusables(
    const BattleWorkspaceLayout &layout) const {
  std::vector<FocusRect> out{
      {layout.play, tr(tactical_speed_ > 0. ? "BATTLE_PAUSE" : "BATTLE_PLAY",
                       tactical_speed_ > 0. ? "Pause" : "Play")},
      {layout.speed,
       trf("BATTLE_SPEED_LABEL", {std::to_string(tactical_resume_speed_)},
           "Speed {0}x")},
      {layout.fit, tr("BATTLE_FIT", "Fit view")},
      {layout.menu, tr("BATTLE_MENU", "Menu")}};
  for (std::size_t index = 0; index < layout.order_buttons.size(); ++index)
    out.push_back({layout.order_buttons[index],
                   tr(battle_order_buttons()[index].label_key,
                      battle_order_buttons()[index].label)});
  std::ranges::sort(out, [](const FocusRect &a, const FocusRect &b) {
    if (a.bounds.y != b.bounds.y)
      return a.bounds.y < b.bounds.y;
    return a.bounds.x < b.bounds.x;
  });
  return out;
}

std::string NativeBattleWorkspace::focused_label(
    const BattleWorkspaceLayout &layout) const {
  if (focus_ < 0) return {};
  const auto items = focusables(layout);
  return focus_ < static_cast<int>(items.size())
             ? items[static_cast<std::size_t>(focus_)].label
             : std::string{};
}
std::optional<stellar::native_map::UiRect> NativeBattleWorkspace::focused_bounds(
    const BattleWorkspaceLayout &layout) const {
  if (focus_ < 0) return std::nullopt;
  const auto items = focusables(layout);
  return focus_ < static_cast<int>(items.size())
             ? std::optional<stellar::native_map::UiRect>{
                   items[static_cast<std::size_t>(focus_)].bounds}
             : std::nullopt;
}

BattleWorkspaceCommand
NativeBattleWorkspace::handle(const InputEvent &event, const int width,
                              const int height) {
  if (!visible_ || !snapshot_) return {};
  adopt_viewport(width,height);
  BattleWorkspaceCommand command{BattleWorkspaceCommandKind::None, true};
  if(event.type==InputEventType::PointerCancelled){
    panning_=false;box_selecting_=false;gesture_=Gesture::None;
    targeting_source_.reset();hovered_formation_.reset();
    focus_=-1;
    return command;
  }
  const auto pointer_event=event.type==InputEventType::Wheel||
      event.type==InputEventType::PointerMove||
      event.type==InputEventType::LeftPressed||
      event.type==InputEventType::LeftReleased||
      event.type==InputEventType::RightPressed||
      event.type==InputEventType::RightReleased;
  if(pointer_event&&(!finite_point(event.position)||
      (event.type==InputEventType::PointerMove&&!finite_point(event.delta)))){
    panning_=false;box_selecting_=false;gesture_=Gesture::None;
    hovered_formation_.reset();command.captured=false;return command;
  }
  pointer_ = event.position;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);

  if (event.type == InputEventType::EscapePressed) {
    gesture_ = Gesture::None;
    panning_ = false;
    box_selecting_ = false;
    if (targeting_source_) {
      targeting_source_.reset();
      set_status(tr("BATTLE_TARGET_CANCELLED","Target selection cancelled."));
    } else {
      command.kind = BattleWorkspaceCommandKind::Menu;
    }
    return command;
  }
  if (event.type == InputEventType::KeyPressed && event.key) {
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    const auto items = focusables(layout);
    const int count = static_cast<int>(items.size());
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      return command;
    }
    if (count > 0 && (fwd || bwd)) {
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      return command;
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &r = items[static_cast<std::size_t>(focus_)].bounds;
      InputEvent press{InputEventType::LeftPressed};
      press.position = {r.x + r.width * .5f, r.y + r.height * .5f};
      const int keep = focus_;
      command = handle(press, width, height);
      focus_ = keep;
      command.captured = true;
      return command;
    }
    return command;
  }
  if (event.type == InputEventType::Wheel) {
    if(!std::isfinite(event.wheel_y)||event.wheel_y==0.f||
       on_chrome(event.position,layout))return command;
    if(event.alt){
      for(const auto id:selected_owned()){
        const auto formation=std::ranges::find(snapshot_->formations,id,&MassiveObservedFormation::formation_id);
        if(formation==snapshot_->formations.end())continue;
        auto objective=formation->position;objective.z=std::clamp(objective.z+(event.wheel_y>0?100.f:-100.f),-10000.f,10000.f);
        command.orders.push_back({id,MassiveCombatOrderType::Advance,std::nullopt,objective,std::nullopt});
      }
      if(!command.orders.empty()){command.kind=BattleWorkspaceCommandKind::IssueOrder;set_status(tr("BATTLE_DEPTH_CHANGE","Changing formation depth."));}
      return command;
    }
    const auto factor = event.wheel_y > 0.f ? 1.25f : 1.f / 1.25f;
    const auto before = to_world(event.position, width, height);
    zoom_ = std::clamp(zoom_ * factor, minimum_zoom, maximum_zoom);
    const auto after = to_screen(before, width, height);
    camera_center_.x=std::clamp(camera_center_.x+event.position.x-after.x,
                                -maximum_screen_coordinate,
                                maximum_screen_coordinate);
    camera_center_.y=std::clamp(camera_center_.y+event.position.y-after.y,
                                -maximum_screen_coordinate,
                                maximum_screen_coordinate);
    ++camera_revision_;
    invalidate_ship_targets();
    return command;
  }
  if (event.type == InputEventType::PointerMove) {
    if (gesture_ == Gesture::RightField && panning_) {
      camera_center_.x=std::clamp(camera_center_.x+event.delta.x,
                                  -maximum_screen_coordinate,
                                  maximum_screen_coordinate);
      camera_center_.y=std::clamp(camera_center_.y+event.delta.y,
                                  -maximum_screen_coordinate,
                                  maximum_screen_coordinate);
      ++camera_revision_;
      invalidate_ship_targets();
      return command;
    }
    if (gesture_ == Gesture::LeftField && (event.delta.x != 0.f || event.delta.y != 0.f)) {
      selection_end_ = event.position;
      if (std::hypot(event.position.x - pointer_down_.x,
                     event.position.y - pointer_down_.y) >= 6.f)
        box_selecting_ = true;
    }
    hovered_formation_ = hit_formation(event.position, width, height);
    return command;
  }
  if (event.type == InputEventType::LeftPressed) {
    // Chrome first: top-row and order buttons. The matching release must not
    // fall through to the field selection path.
    gesture_ = Gesture::None;
    panning_ = false;
    box_selecting_ = false;
    focus_ = -1;
    const auto chrome_press = [&] {
      gesture_ = Gesture::Chrome;
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
        set_status(tr("BATTLE_SELECT_FIRST","Select one or more friendly formations first."), true);
        return command;
      }
      if (button.needs_target) {
        targeting_source_ = selected.front();
        targeting_order_ = button.type;
        set_status(trf("BATTLE_CHOOSE_OBJECTIVE",{order_name(button.type, locale_)},
                       "{0}: choose a formation or open-space objective."));
        return command;
      }
      command.kind = BattleWorkspaceCommandKind::IssueOrder;
      for (const auto source : selected) { MassiveCombatOrder order; order.formation_id=source; order.type=button.type; command.orders.push_back(std::move(order)); }
      return command;
    }
    if (on_chrome(event.position, layout)) {
      chrome_press();
      return command;
    }
    pointer_down_ = selection_end_ = event.position;
    box_selecting_ = false;
    gesture_ = Gesture::LeftField;
    return command;
  }
  if (event.type == InputEventType::LeftReleased) {
    const bool from_chrome = gesture_ == Gesture::Chrome;
    if (gesture_ != Gesture::LeftField && !from_chrome) { command.captured = false; return command; }
    gesture_ = Gesture::None;
    if (from_chrome) return command;
    if (targeting_source_) {
      // A release over chrome keeps the pick state; the same click that armed
      // a targeted order must not fire it at a button coordinate.
      if (on_chrome(event.position, layout)) return command;
      const auto source = *targeting_source_;
      targeting_source_.reset();
      const auto hit = hit_formation(event.position, width, height);
      MassiveCombatOrder order; order.formation_id = source; order.type = targeting_order_;
      if (hit && *hit != source)
        order.target_formation_id = *hit;
      else {
        auto objective=to_world(event.position,width,height);
        const auto current=std::ranges::find(snapshot_->formations,source,&MassiveObservedFormation::formation_id);
        if(current!=snapshot_->formations.end()){objective.z=current->position.z;objective.y+=.35f*objective.z;}
        if(!valid_world_point(objective))return command;
        order.objective=objective;
      }
      command.kind = BattleWorkspaceCommandKind::IssueOrder;
      command.orders.push_back(std::move(order));
      return command;
    }
    if (box_selecting_) {
      if(on_chrome(event.position,layout)){box_selecting_=false;return command;}
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
      if (ship_targets_current(width, height))
        for (const auto &target : ship_targets_)
          if (target.center.x >= x0 && target.center.x <= x1 &&
              target.center.y >= y0 && target.center.y <= y1)
            selection_.insert(target.formation_id);
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
    if (on_chrome(event.position, layout)) return command;
    panning_ = true;
    gesture_ = Gesture::RightField;
    pointer_down_ = event.position;
    return command;
  }
  if (event.type == InputEventType::RightReleased) {
    if (gesture_ != Gesture::RightField) { command.captured = false; return command; }
    const auto was_panning = panning_ && std::hypot(event.position.x - pointer_down_.x,
                               event.position.y - pointer_down_.y) >= 6.f;
    panning_ = false;
    gesture_ = Gesture::None;
    if (!was_panning && !on_chrome(event.position, layout))
      issue_context(event.position, width, height, command);
    return command;
  }
  return command;
}

void NativeBattleWorkspace::render(DrawList &out, const int width,
                                   const int height, const ShipLayer& ship_layer) const {
  if (!visible_) return;
  const auto layout = BattleWorkspaceLayout::for_viewport(width, height);
  const auto top = layout.top_row.y + layout.top_row.height;
  const auto bottom = static_cast<float>(height) - 116.f * layout.scale;
  const UiRect field{0.f, top, static_cast<float>(width), bottom - top};

  fill(out, layout.surface, {4, 10, 20, 255});

  // Reference tactical grid.
  const auto spacing = std::clamp(80.f * zoom_, 42.f, 132.f);
  const auto rendered_camera_center=to_screen(world_center_,width,height);
  const auto offset_x = pos_mod(rendered_camera_center.x, spacing),
             offset_y = pos_mod(rendered_camera_center.y, spacing);
  for (float gx = offset_x; gx < field.x + field.width; gx += spacing)
    clipped_line(out, {gx, top}, {gx, bottom}, {30, 61, 79, 46}, field);
  for (float gy = std::max(top, offset_y); gy < bottom; gy += spacing)
    clipped_line(out, {0.f, gy}, {field.width, gy}, {30, 61, 79, 46}, field);

  if (!snapshot_) {
    text(out, {field.width * .5f, (top + bottom) * .5f},
         tr("BATTLE_NONE","No tactical encounter is active."), text_secondary,
         layout.body_font_pixels, 0.f, TextAlign::Center);
    return;
  }
  const auto &formations = snapshot_->formations;

  const auto formation_color = [&](const MassiveObservedFormation &formation) {
    if (is_owned(formation)) return success;
    if (!formation.is_exact && !formation.strength_low) return unknown;
    const auto hue=static_cast<float>((static_cast<std::int64_t>(
        formation.civilization_id)*67)%31)/310.f;
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
      disc(out,end,3.f+effect.magnitude/80.f,{204,242,255,alpha},field);
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
      disc(out,missile,3.2f,{255,224,140,alpha},field);
      break;
    }
    case stellar::core::MassiveCombatEventType::MissileIntercepted:
      ring(out, end, 6.f + effect.age * 18.f, {89, 217, 255, alpha}, field,
           20);
      break;
    case stellar::core::MassiveCombatEventType::Damage:
      disc(out,end,5.f+effect.age*22.f,{255,56,31,static_cast<std::uint8_t>(alpha*.18f)},field);
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
    disc(out,from,3.4f,{255,224,140,235},field);
  }

  // Formation guides and token clusters.
  last_rendered_tokens_ = 0;
  for (const auto &formation : formations) {
    const auto center = to_screen(formation.position, width, height);
    if(!finite_point(center))continue;
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
                     focus_color, field);
      }
    }
    if (targeting_source_ && *targeting_source_ == formation.formation_id)
      ring(out, center, 43.f, focus_color, field, 36);

    // Token sample: bounded by zoom tier and the 4096 reference pool cap.
    const auto midpoint=std::max<std::int64_t>(1,
        (static_cast<std::int64_t>(formation.ship_count_low)+
         static_cast<std::int64_t>(formation.ship_count_high))/2);
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
    const auto heading_length=valid_world_point(formation.velocity)
        ?std::hypot(formation.velocity.x,formation.velocity.y):0.f;
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
      disc(out,at,radius,color,field);
      if (token == 0)
        disc(out,{at.x+heading.x*(radius+3.f),at.y+heading.y*(radius+3.f)},1.4f,color,field);
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
        disc(out,at,size,vessel_color,field);
        if (vessel.is_flagship)
          ring(out, at, size + 3.f, caution, field, 12);
        if (vessel.is_interdictor)
          ring(out, at, size + 5.f, {color.r, color.g, color.b, 140}, field,
               12);
      }
    }
  }

  // Ship artwork belongs above the field but below labels and command chrome.
  if (ship_layer) ship_layer(out, field, zoom_, layout.scale);

  // Labels with a bounded collision budget.
  const auto label_budget =
      zoom_ < .5f ? 48 : zoom_ < 1.1f ? 120 : 360;
  std::vector<UiRect> occupied;
  occupied.reserve(64);
  occupied.push_back(layout.event_feed);
  occupied.push_back(layout.orders);
  if (ship_targets_current(width, height))
    for (const auto &target : ship_targets_) {
      const auto radians = target.heading_degrees * degrees_to_radians;
      const auto half_extent = target.size * .5f *
          (std::abs(std::cos(radians)) + std::abs(std::sin(radians)));
      if (std::isfinite(half_extent))
        occupied.push_back({target.center.x - half_extent,
                            target.center.y - half_extent, half_extent * 2,
                            half_extent * 2});
    }
  int labelled = 0;
  // Reserve readable space for the hovered detail before less important labels.
  std::vector<const MassiveObservedFormation *> label_order;
  label_order.reserve(formations.size());
  for (int priority = 0; priority < 3; ++priority)
    for (const auto &formation : formations) {
      const auto rank = hovered_formation_ == formation.formation_id ? 0 :
                        selection_.contains(formation.formation_id) ? 1 : 2;
      if (rank == priority) label_order.push_back(&formation);
    }
  for (const auto *entry : label_order) {
    const auto &formation = *entry;
    const auto center = to_screen(formation.position, width, height);
    if(!finite_point(center))continue;
    if (center.x < -100.f || center.y < top - 50.f ||
        center.x > field.width + 100.f || center.y > bottom + 80.f)
      continue;
    const auto priority = selection_.contains(formation.formation_id) ||
                          hovered_formation_ == formation.formation_id;
    if (!priority && labelled >= label_budget) continue;
    const auto line_height = 20.f * layout.scale;
    const auto label_height = line_height *
        (hovered_formation_ == formation.formation_id ? 3.f : 2.f);
    std::optional<UiRect> placement;
    for (const auto vertical : {-7.f, 47.f, -67.f}) {
      for (const auto horizontal : {19.f, -229.f}) {
        const UiRect candidate{center.x + (horizontal - 3.f) * layout.scale,
                               center.y + vertical * layout.scale,
                               220.f * layout.scale, label_height};
        if (candidate.x < field.x || candidate.y < field.y ||
            candidate.x + candidate.width > field.x + field.width ||
            candidate.y + candidate.height > field.y + field.height) continue;
        if (std::ranges::any_of(occupied, [&](const UiRect &box) {
              return intersection({box.x - 4.f, box.y - 4.f, box.width + 8.f,
                                   box.height + 8.f}, candidate).has_value();
            })) continue;
        placement = candidate;
        break;
      }
      if (placement) break;
    }
    if (!placement) continue;
    const auto bounds = *placement;
    const auto label_x = bounds.x + 3.f * layout.scale;
    occupied.push_back(bounds);
    ++labelled;
    const auto color = formation_color(formation);
    const auto count =
        formation.is_exact
            ? grouped(formation.ship_count_low)
            : grouped(formation.ship_count_low) + "-" +
                  grouped(formation.ship_count_high);
    clipped_text(out, {label_x, bounds.y},
                 formation.display_name, text_primary,
                 static_cast<int>(12.f * layout.scale), 0.f,
                 {bounds.x, bounds.y, bounds.width, line_height});
    clipped_text(out, {label_x, bounds.y + line_height},
                 trf("BATTLE_SHIP_COUNT",{count,formation_state(formation,locale_)},"{0} ships · {1}"), color,
                 layout.small_font_pixels, 0.f,
                 {bounds.x, bounds.y + line_height, bounds.width, line_height});
    if (hovered_formation_ == formation.formation_id) {
      std::string strength = tr("BATTLE_POWER_UNKNOWN","Power unknown");
      if (formation.strength_low) {
        strength = formation.strength_low == formation.strength_high
                       ? trf("BATTLE_POWER",{grouped(static_cast<std::int64_t>(*formation.strength_low))},"Power {0}")
                       : trf("BATTLE_POWER_RANGE",{grouped(static_cast<std::int64_t>(*formation.strength_low)),
                             grouped(static_cast<std::int64_t>(*formation.strength_high))},"Power {0}-{1}");
      }
      clipped_text(out, {label_x, bounds.y + line_height * 2.f},
                   strength, text_secondary, layout.small_font_pixels,
                   0.f, {bounds.x, bounds.y + line_height * 2.f, bounds.width, line_height});
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
    const auto y = layout.scale_bar.y + layout.scale_bar.height - 5.f * layout.scale;
    clipped_line(out, {18.f, y}, {18.f + pixels, y}, text_secondary, field);
    clipped_line(out, {18.f, y - 4.f}, {18.f, y + 4.f}, text_secondary,
                 field);
    clipped_line(out, {18.f + pixels, y - 4.f}, {18.f + pixels, y + 4.f},
                 text_secondary, field);
    text(out, {18.f, layout.scale_bar.y}, trf("BATTLE_SCALE_KM",{std::to_string(static_cast<int>(kilometres))},"{0} km"), text_secondary,
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
            ? tr("BATTLE_HINT","Select or drag around friendly formations · right-click to engage or advance · right-drag to pan · wheel to zoom")
            : trf(selected == 1 ? "BATTLE_SELECTED_ONE" : "BATTLE_SELECTED_MANY",
                  {std::to_string(selected)},
                  selected == 1 ? "{0} formation" : "{0} formations");
    if (selected > 0) {
      std::int64_t low = 0, high = 0;
      bool exact = true;
      for (const auto &formation : formations)
        if (selection_.contains(formation.formation_id)) {
          low += formation.ship_count_low;
          high += formation.ship_count_high;
          exact &= formation.is_exact;
        }
      summary += trf(exact ? "BATTLE_SHIPS_EXACT" : "BATTLE_SHIPS_ESTIMATED",
                     {grouped(low), grouped(high)},
                     exact ? " · {0} ships" : " · {0}-{1} estimated ships");
      summary += tr("BATTLE_DEPTH_HINT"," · Alt + wheel to change depth");
    }
    text(out, {layout.selection_summary.x, layout.selection_summary.y},
         summary, text_secondary, layout.body_font_pixels);
    const auto friendly = static_cast<int>(
        std::ranges::count_if(formations,
                              [&](const auto &f) { return is_owned(f); }));
    const auto contacts =
        static_cast<int>(formations.size()) - friendly;
    std::ostringstream seconds;
    seconds << std::fixed << std::setprecision(1) << snapshot_->simulated_seconds;
    text(out, {layout.battle_summary.x, layout.battle_summary.y},
         trf(contacts == 1 ? "BATTLE_SUMMARY_ONE" : "BATTLE_SUMMARY_MANY",
             {seconds.str(), grouped(snapshot_->exact_own_ships),
              std::to_string(contacts)},
             contacts == 1
                 ? "T+{0}s  ·  {1} friendly ships  ·  {2} detected hostile formation"
                 : "T+{0}s  ·  {1} friendly ships  ·  {2} detected hostile formations"),
         text_secondary, layout.body_font_pixels);
  }

  // Recent combat events feed (observer-filtered snapshot tail). Severity is
  // derived from the already-exposed actor/target ids: losses to the observer
  // read danger, losses inflicted read success, disruption reads caution.
  {
    const auto event_color=[&](const auto &event){
      if(!event.details_known)return unknown;
      using EventType=stellar::core::MassiveCombatEventType;
      const bool own_actor=event.actor_civilization_id==observer_civilization_id_;
      const bool own_target=event.target_civilization_id==observer_civilization_id_;
      switch(event.type){
        case EventType::Damage:case EventType::FormationDestroyed:
          return own_target?danger:own_actor?success:caution;
        case EventType::MissileIntercepted:case EventType::Escaped:
        case EventType::Surrendered:case EventType::WarpBlocked:
          return caution;
        case EventType::WarpSpooling:
          return own_actor?caution:unknown;
        default:return text_secondary;
      }
    };
    int lines = 0;
    for (auto it = snapshot_->events.rbegin();
         it != snapshot_->events.rend() && lines < 4; ++it, ++lines) {
      const auto row_height = 42.f * layout.scale;
      const auto y = layout.event_feed.y + static_cast<float>(lines) * row_height;
      // Translucent card + severity accent bar keep event text legible over
      // the starfield — same severity vocabulary as notification cards.
      const UiRect card{layout.event_feed.x, y, layout.event_feed.width,
                        row_height - 4.f * layout.scale};
      fill(out, card, {7, 19, 31, 170});
      fill(out, {card.x, card.y, 2.5f * layout.scale, card.height},
           event_color(*it));
      clipped_text(out, {card.x + 7.f * layout.scale, y},
                   it->details_known ? it->message : tr("BATTLE_INTERCEPT","Signal intercept."),
                   event_color(*it),
                   layout.small_font_pixels,
                   card.width - 7.f * layout.scale, card);
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
    std::ostringstream speed_value;
    speed_value << tactical_resume_speed_;
    text(out, {layout.speed.x + layout.speed.width * .5f,
               layout.speed.y + layout.speed.height * .32f},
         trf("BATTLE_SPEED",{speed_value.str()},"> {0}x"),
         tactical_speed_ > 0. ? text_primary : caution,
         layout.body_font_pixels, 0.f, TextAlign::Center);
  }
  fill(out, layout.fit,
       layout.fit.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.fit, border);
  text(out, {layout.fit.x + layout.fit.width * .5f,
             layout.fit.y + layout.fit.height * .32f},
         tr("BATTLE_FIT","FIT"), text_primary, layout.body_font_pixels, 0.f,
         TextAlign::Center);
  fill(out, layout.menu,
       layout.menu.contains(pointer_) ? hover_color : button_color);
  stroke(out, layout.menu, border);
  text(out, {layout.menu.x + layout.menu.width * .5f,
             layout.menu.y + layout.menu.height * .32f},
         tr("BATTLE_MENU","MENU"), text_primary, layout.body_font_pixels, 0.f,
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
    stroke(out, rect, active_targeting ? focus_color : border);
    clipped_text(out,
                 {rect.x + rect.width * .5f, rect.y + rect.height * .3f},
                 tr(button.label_key, button.label), text_primary, layout.small_font_pixels,
                 rect.width - 6.f, rect, TextAlign::Center);
  }

  if (focus_ >= 0) {
    const auto items = focusables(layout);
    if (focus_ < static_cast<int>(items.size()))
      stellar::native_ui::focus_ring(
          out, items[static_cast<std::size_t>(focus_)].bounds);
  }
}
} // namespace stellar::native_battle_ui
