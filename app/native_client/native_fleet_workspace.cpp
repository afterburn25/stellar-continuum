#include "native_campaign_calendar.hpp"
#include "native_fleet_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_fleet_ui {
namespace {
using namespace stellar::native_fleet;
using namespace stellar::native_map;
namespace theme = stellar::native_ui;

constexpr Color row_color = theme::color::surface_secondary;
constexpr Color hover_color = theme::color::surface_hover;
constexpr Color selected_color = theme::color::surface_raised;
constexpr Color own_color = theme::color::success;
constexpr Color bright = theme::color::text_primary;
constexpr Color muted = theme::color::text_secondary;
constexpr Color warning = theme::color::caution;
constexpr Color failure = theme::color::danger;

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}

void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}

void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, FontFace face = FontFace::Interface,
          TextAlign align = TextAlign::Left) {
  const auto x = align == TextAlign::Center
                     ? bounds.x + bounds.width * .5f
                     : align == TextAlign::Right ? bounds.x + bounds.width
                                                 : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align, face});
}

[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

// Status group in display order — most urgent first. A fleet belongs to the
// first group whose predicate it satisfies.
[[nodiscard]] int fleet_group(const NativeOwnFleet &fleet) {
  if (fleet.combat_status && fleet.combat_status->has_assigned_attack_target)
    return 0;
  if (fleet.destination_system_id ||
      fleet.transit_phase != stellar::core::FleetTransitPhase::None)
    return 1;
  if ((fleet.reconnaissance && !fleet.reconnaissance->completed) ||
      (fleet.science_survey && !fleet.science_survey->completed) ||
      fleet.recovery)
    return 2;
  return 3;
}

[[nodiscard]] std::string visible_message(std::string value) {
  constexpr std::size_t limit = 120;
  if (value.size() <= limit) return value;
  auto end = limit - 3;
  while (end > 0 &&
         (static_cast<unsigned char>(value[end]) & 0xc0u) == 0x80u)
    --end;
  value.resize(end);
  value += "...";
  return value;
}

[[nodiscard]] std::string role_name(stellar::core::FleetRole role) {
  using stellar::core::FleetRole;
  switch (role) {
  case FleetRole::Scout: return "Scout";
  case FleetRole::Science: return "Science";
  case FleetRole::Colony: return "Colony";
  case FleetRole::Military: return "Military";
  case FleetRole::Logistics: return "Logistics";
  }
  return "Fleet";
}

[[nodiscard]] const char *role_key(stellar::core::FleetRole role) {
  using stellar::core::FleetRole;
  switch (role) {
  case FleetRole::Scout: return "FLEET_ROLE_SCOUT";
  case FleetRole::Science: return "FLEET_ROLE_SCIENCE";
  case FleetRole::Colony: return "FLEET_ROLE_COLONY";
  case FleetRole::Military: return "FLEET_ROLE_MILITARY";
  case FleetRole::Logistics: return "FLEET_ROLE_LOGISTICS";
  }
  return "FLEET_ROLE_FLEET";
}

[[nodiscard]] std::string transit_name(stellar::core::FleetTransitPhase phase) {
  using stellar::core::FleetTransitPhase;
  switch (phase) {
  case FleetTransitPhase::None: return "Stationed";
  case FleetTransitPhase::LocalDeparture: return "Departing";
  case FleetTransitPhase::InterstellarWarp: return "In transit";
  case FleetTransitPhase::LocalArrival: return "Arriving";
  }
  return "Active";
}

[[nodiscard]] const char *transit_key(stellar::core::FleetTransitPhase phase) {
  using stellar::core::FleetTransitPhase;
  switch (phase) {
  case FleetTransitPhase::None: return "FLEET_TRANSIT_STATIONED";
  case FleetTransitPhase::LocalDeparture: return "FLEET_TRANSIT_DEPARTING";
  case FleetTransitPhase::InterstellarWarp: return "FLEET_TRANSIT_WARP";
  case FleetTransitPhase::LocalArrival: return "FLEET_TRANSIT_ARRIVING";
  }
  return "FLEET_TRANSIT_ACTIVE";
}

[[nodiscard]] std::optional<UiRect> intersection(UiRect left,
                                                 UiRect right) noexcept {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto r = std::min(left.x + left.width, right.x + right.width);
  const auto b = std::min(left.y + left.height, right.y + right.height);
  if (r <= x || b <= y) return std::nullopt;
  return UiRect{x, y, r - x, b - y};
}

} // namespace

FleetWorkspaceLayout FleetWorkspaceLayout::for_viewport(int width,
    int height, FleetWorkspacePresentation presentation) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto scale = NativeUiLayout::for_viewport(width,height).scale;
  const auto inset = 12.f * scale;
  const bool commands = presentation == FleetWorkspacePresentation::SelectedCommands;
  const auto top = commands ? 108.f * scale :
      std::min(260.f * scale, std::max(52.f * scale,h-440.f*scale));
  const auto panel_width = (commands ? 286.f : 254.f) * scale;
  const UiRect panel{commands ? (native_navigation_content_left + 8.f) * scale : w - inset - panel_width,
      top, panel_width, std::max(0.f, std::min((commands ? 462.f : 620.f)*scale,
          h - top - (commands ? 104.f * scale : inset)))};
  const auto inner_x = panel.x + 12.f * scale;
  const auto inner_width = panel.width - 24.f * scale;
  const UiRect heading{inner_x, panel.y + 12.f * scale, inner_width,
                       22.f * scale};
  const auto list_height = commands ? 0.f : std::clamp(panel.height * .16f, 68.f * scale,
                                      112.f * scale);
  const UiRect list{inner_x, heading.y + heading.height + 8.f * scale,
                    inner_width, list_height};
  const UiRect confirm{inner_x, panel.y + panel.height - 48.f * scale,
                       inner_width, 36.f * scale};
  const UiRect feedback{inner_x, confirm.y - 54.f * scale, inner_width,
                        46.f * scale};
  const auto detail_y = list.y + list.height + 10.f * scale;
  const auto detail_space = std::max(0.f, feedback.y - detail_y - 6.f * scale);
  // Keep seven telemetry lines plus the pinned order rail legible at 720p.
  const auto fleet_height = std::min(180.f * scale,
                                      std::max(0.f,detail_space - 78.f * scale));
  const UiRect details{inner_x, detail_y, inner_width, fleet_height};
  const UiRect route{inner_x, detail_y + fleet_height + 6.f * scale,
                     inner_width,
                     std::max(0.f, detail_space - fleet_height - 6.f * scale)};
  // Strategic choices use the details footer; Locate/Engage share the action
  // rail that recovery already owns. This keeps every action distinct at 720p.
  const auto order_gap = 6.f * scale;
  const auto order_width = (inner_width - 2.f * order_gap) / 3.f;
  const auto details_action_height=std::min(28.f*scale,details.height);
  const auto details_action_y=details.y+details.height-details_action_height;
  const UiRect order_hold{details.x, details_action_y,order_width,details_action_height};
  const UiRect order_defend{order_hold.x + order_width + order_gap, order_hold.y,
                            order_width, order_hold.height};
  const UiRect order_retreat{order_defend.x + order_width + order_gap, order_hold.y,
                             order_width, order_hold.height};
  const auto paired_width = (confirm.width - 8.f * scale) * .5f;
  const UiRect military_locate{confirm.x, confirm.y, paired_width, confirm.height};
  const UiRect engage{confirm.x + paired_width + 8.f * scale, confirm.y,
                       paired_width, confirm.height};
  const UiRect locate{confirm};
  const UiRect civilian_locate{details.x, details_action_y,details.width,details_action_height};
  return {scale,
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(14.f * scale)),
          static_cast<int>(std::lround(11.f * scale)),
          panel,
          heading,
          list,
          details,
          route,
          feedback,
           confirm,
           {confirm.x, confirm.y, (confirm.width - 8.f * scale) * .5f, confirm.height},
           {confirm.x + (confirm.width + 8.f * scale) * .5f, confirm.y,
           (confirm.width - 8.f * scale) * .5f, confirm.height},
           order_hold, order_defend, order_retreat, locate, military_locate,
          civilian_locate, engage};
}

std::string NativeFleetWorkspace::tr(std::string_view key,
                                     std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeFleetWorkspace::trf(
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

FleetWorkspaceLayout NativeFleetWorkspace::layout(int width, int height) const noexcept {
  return FleetWorkspaceLayout::for_viewport(width, height, presentation_);
}

std::optional<UiRect> NativeFleetWorkspace::panel_bounds(int width, int height) const noexcept {
  if (presentation_ == FleetWorkspacePresentation::SelectedCommands && !selected_fleet())
    return std::nullopt;
  auto bounds = layout(width, height).panel;
  if (!view_ || view_->own_fleets.empty()) bounds.height = 142.f * layout(width,height).scale;
  return bounds;
}

[[nodiscard]] std::string military_order_name(stellar::core::MilitaryOrderType order) {
  using stellar::core::MilitaryOrderType;
  switch (order) {
  case MilitaryOrderType::Hold: return "Hold";
  case MilitaryOrderType::Defend: return "Defend";
  case MilitaryOrderType::Attack: return "Attack";
  case MilitaryOrderType::Retreat: return "Retreat";
  }
  return "Hold";
}

[[nodiscard]] const char *military_order_key(
    stellar::core::MilitaryOrderType order) {
  using stellar::core::MilitaryOrderType;
  switch (order) {
  case MilitaryOrderType::Hold: return "FLEET_ORDER_HOLD";
  case MilitaryOrderType::Defend: return "FLEET_ORDER_DEFEND";
  case MilitaryOrderType::Attack: return "FLEET_ORDER_ATTACK";
  case MilitaryOrderType::Retreat: return "FLEET_ORDER_RETREAT";
  }
  return "FLEET_ORDER_HOLD";
}

void NativeFleetWorkspace::set_view(NativeFleetMapView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  if (generation_changed) {
    preview_.reset();
    target_display_name_.clear();
    notice_.clear();
    list_scroll_.scroll_offset = 0.f;
    focus_ = -1;
  }
  const auto selected_changed = [&] {
    if (!view_ || view_->selected_fleet_id != view.selected_fleet_id ||
        view_->player_civilization_id != view.player_civilization_id)
      return true;
    if (!view_->selected_fleet_id) return false;
    const auto before=std::ranges::find(view_->own_fleets,*view_->selected_fleet_id,&NativeOwnFleet::id);
    const auto after=std::ranges::find(view.own_fleets,*view.selected_fleet_id,&NativeOwnFleet::id);
    return before==view_->own_fleets.end()||after==view.own_fleets.end()||
        before->mission_order_revision!=after->mission_order_revision||
        before->military_order_quote!=after->military_order_quote||before->locate!=after->locate;
  };
  if (generation_changed || selected_changed()) clear_pressed_action();
  view_ = std::move(view);
  const auto selected = view_->selected_fleet_id
                            ? std::ranges::find(view_->own_fleets,
                                                *view_->selected_fleet_id,
                                                &NativeOwnFleet::id)
                            : view_->own_fleets.end();
  if (pending_return_ && (selected == view_->own_fleets.end() ||
      selected->recovery != pending_return_)) {
    cancel_recovery();
    notice_ = tr("FLEET_MISSION_CHANGED",
                 "Mission changed. Review Return to Base again before confirming.");
    notice_accepted_ = false;
  }
  if (preview_ &&
      (selected == view_->own_fleets.end() ||
       preview_->fleet_id != selected->id ||
       preview_->expected_mission_order_revision !=
           selected->mission_order_revision)) {
    preview_.reset();
    target_display_name_.clear();
  }
}

void NativeFleetWorkspace::discard_campaign() {
  cancel_recovery();
  view_.reset();
  overview_.reset();
  preview_.reset();
  target_display_name_.clear();
  notice_.clear();
  list_scroll_.scroll_offset = 0.f;
  clear_pressed_action();
  focus_ = -1;
}

void NativeFleetWorkspace::set_preview(NativeFleetRoutePreview preview,
                                       std::string target_display_name) {
  cancel_recovery();
  preview_ = std::move(preview);
  target_display_name_ = std::move(target_display_name);
  notice_.clear();
  clear_pressed_action();
}

void NativeFleetWorkspace::clear_preview() {
  preview_.reset();
  target_display_name_.clear();
  clear_pressed_action();
}

void NativeFleetWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
}

void NativeFleetWorkspace::cancel_recovery() noexcept {
  pending_return_.reset();
  return_warning_.clear();
  // Main invokes this public cancellation hook for focus loss, navigation,
  // and menu transitions. It must also revoke a release-gated tactical press.
  clear_pressed_action();
}

void NativeFleetWorkspace::clear_pressed_action() noexcept {
  pressed_action_=PressTarget::None;
  pressed_bounds_={};
  pressed_military_quote_.reset();
  pressed_locate_quote_.reset();
}

NativeFleetWorkspace::PressTarget NativeFleetWorkspace::pressed_target_at(
    Point point,const FleetWorkspaceLayout& layout) const noexcept {
  if(layout.order_hold.contains(point)) return PressTarget::Hold;
  if(layout.order_defend.contains(point)) return PressTarget::Defend;
  if(layout.order_retreat.contains(point)) return PressTarget::Retreat;
  if(layout.locate.contains(point)||layout.military_locate.contains(point)||
     layout.civilian_locate.contains(point)) return PressTarget::Locate;
  return PressTarget::None;
}

void NativeFleetWorkspace::set_recovery_result(
    const NativeCivilianRecoveryQuote &quote, const NativeFleetOrderOutcome &outcome) {
  cancel_recovery();
  const auto *fleet = selected_fleet();
  if (!outcome.accepted && outcome.requires_confirmation && fleet &&
      fleet->recovery == quote) {
    pending_return_ = quote;
    return_warning_ = outcome.message;
  }
  set_notice(outcome.message, outcome.accepted || outcome.requires_confirmation);
}

std::vector<NativeFleetWorkspace::FleetListRow>
NativeFleetWorkspace::fleet_rows(const FleetWorkspaceLayout &layout) const {
  std::vector<FleetListRow> rows;
  if (!view_ || view_->own_fleets.empty()) return rows;
  const float s = layout.scale;
  std::array<std::vector<std::size_t>, 4> groups;
  for (std::size_t i = 0; i < view_->own_fleets.size(); ++i)
    groups[static_cast<std::size_t>(fleet_group(view_->own_fleets[i]))]
        .push_back(i);
  int nonempty = 0;
  for (const auto &group : groups) nonempty += !group.empty();
  const bool grouped = nonempty > 1;
  static const std::array<const char *, 4> keys = {
      "FLEET_GROUP_ENGAGED", "FLEET_GROUP_TRANSIT", "FLEET_GROUP_MISSION",
      "FLEET_GROUP_STATIONED"};
  static const std::array<const char *, 4> names = {
      "IN COMBAT", "IN TRANSIT", "ON MISSION", "STATIONED"};
  float top = 0.f;
  for (std::size_t group = 0; group < groups.size(); ++group) {
    if (groups[group].empty()) continue;
    if (grouped) {
      rows.push_back({0, true,
                      trf(keys[group],
                          {std::to_string(groups[group].size())},
                          std::string(names[group]) + "  ·  {0}"),
                      top, 24.f * s});
      top += 24.f * s;
    }
    for (const auto index : groups[group]) {
      rows.push_back({index, false, {}, top, 45.f * s});
      top += 45.f * s;
    }
  }
  return rows;
}

float NativeFleetWorkspace::fleet_content_height(
    const FleetWorkspaceLayout &layout) const {
  const auto rows = fleet_rows(layout);
  return rows.empty() ? 0.f : rows.back().top + rows.back().height;
}

std::vector<NativeFleetWorkspace::FocusRect> NativeFleetWorkspace::focusables(
    const FleetWorkspaceLayout &layout) const {
  std::vector<FocusRect> out;
  const auto *fleet = selected_fleet();
  if (view_ && presentation_ == FleetWorkspacePresentation::Outliner)
    for (const auto &row : fleet_rows(layout)) {
      if (row.header) continue;
      const UiRect rect{layout.list.x,
                        layout.list.y - list_scroll_.scroll_offset + row.top,
                        layout.list.width, row.height - 4.f * layout.scale};
      if (const auto clipped = intersection(rect, layout.list))
        out.push_back(
            {*clipped, view_->own_fleets[row.fleet_index].name, rect});
    }
  if (overview_ && !selected_fleet_id()) {
    const UiRect content{layout.details.x, layout.details.y,
                         layout.details.width,
                         layout.route.y + layout.route.height -
                             layout.details.y};
    for (const auto &row :
         native_overview::overview_layout_for(*overview_, content).colony_rows)
      out.push_back({row, tr("FLEET_COLONY", "Colony")});
  }
  if (fleet) {
    if (!preview_ && !pending_return_) {
      if (fleet->military_order_quote) {
        out.push_back({layout.order_hold, tr("FLEET_ORDER_HOLD_BTN", "HOLD")});
        out.push_back({layout.order_defend, tr("FLEET_ORDER_DEFEND_BTN", "DEFEND")});
        out.push_back({layout.order_retreat, tr("FLEET_ORDER_RETREAT_BTN", "RETREAT")});
        if (fleet->locate)
          out.push_back({layout.military_locate, tr("FLEET_LOCATE", "LOCATE")});
      } else if (fleet->recovery && fleet->locate) {
        out.push_back({layout.civilian_locate, tr("FLEET_LOCATE", "LOCATE")});
      } else if (fleet->locate) {
        out.push_back({layout.locate, tr("FLEET_LOCATE", "LOCATE")});
      }
    }
    if (!preview_ && fleet->recovery) {
      const auto queued = fleet->recovery->return_requested;
      out.push_back({layout.recovery_left,
                     pending_return_
                         ? tr("FLEET_CONFIRM_RETURN", "CONFIRM RETURN")
                         : tr(fleet->recovery->hold_requested ? "FLEET_RESUME"
                                                              : "FLEET_HOLD",
                              fleet->recovery->hold_requested ? "RESUME"
                                                              : "HOLD")});
      if (pending_return_ || !queued)
        out.push_back({layout.recovery_right,
                       pending_return_
                           ? tr("SETTINGS_CANCEL", "CANCEL")
                           : tr(queued ? "FLEET_RETURN_QUEUED"
                                       : "FLEET_RETURN_BASE",
                                queued ? "RETURN QUEUED" : "RETURN TO BASE")});
    }
    if (!preview_ && !pending_return_ && !fleet->foreign_inspection &&
        fleet->role == stellar::core::FleetRole::Military &&
        fleet->current_system_id && !fleet->destination_system_id &&
        fleet->combat_status && fleet->combat_status->is_armed)
      out.push_back({layout.engage, tr("FLEET_ENGAGE", "ENGAGE HOSTILES")});
  }
  if (preview_ && preview_->command_available)
    out.push_back({layout.confirm, tr("FLEET_CONFIRM_TRAVEL", "CONFIRM TRAVEL")});
  std::ranges::sort(out, [](const FocusRect &a, const FocusRect &b) {
    if (a.bounds.y != b.bounds.y)
      return a.bounds.y < b.bounds.y;
    return a.bounds.x < b.bounds.x;
  });
  return out;
}

std::string NativeFleetWorkspace::focused_label(
    const FleetWorkspaceLayout &layout) const {
  if (focus_ < 0)
    return {};
  const auto items = focusables(layout);
  return focus_ < static_cast<int>(items.size())
             ? items[static_cast<std::size_t>(focus_)].label
             : std::string{};
}
std::optional<stellar::native_map::UiRect> NativeFleetWorkspace::focused_bounds(
    const FleetWorkspaceLayout &layout) const {
  if (focus_ < 0)
    return std::nullopt;
  const auto items = focusables(layout);
  return focus_ < static_cast<int>(items.size())
             ? std::optional<stellar::native_map::UiRect>{
                   items[static_cast<std::size_t>(focus_)].bounds}
             : std::nullopt;
}

FleetWorkspaceCommand NativeFleetWorkspace::handle(
    const InputEvent &event, int width, int height,
    std::span<const FleetScreenMarker> markers,
    std::optional<int> target_system_id) {
  pointer_ = event.position;
  auto layout = this->layout(width, height);
  const auto visible_panel = panel_bounds(width, height);
  if (!visible_panel) { layout.panel = {}; layout.list = {}; }
  if(!view_||view_->own_fleets.empty())layout.panel.height=142.f*layout.scale;
  if (event.type == InputEventType::PointerCancelled) {
    clear_pressed_action();
    cancel_recovery();
    focus_ = -1;
    return {FleetWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::LeftReleased &&
      pressed_action_ != PressTarget::None) {
    const auto pressed=std::exchange(pressed_action_,PressTarget::None);
    const auto pressed_bounds=std::exchange(pressed_bounds_,UiRect{});
    const auto military=std::exchange(pressed_military_quote_,std::nullopt);
    const auto locate=std::exchange(pressed_locate_quote_,std::nullopt);
    FleetWorkspaceCommand command;
    command.captured=true;
    const auto *fleet=selected_fleet();
    if(!pressed_bounds.contains(event.position)||preview_||pending_return_||!fleet) return command;
    if(military&&fleet->military_order_quote==military) {
      command.kind=FleetWorkspaceCommandKind::MilitaryOrder;
      command.fleet_id=fleet->id;
      command.military_order_quote=std::move(military);
      command.military_order=pressed==PressTarget::Hold?stellar::core::MilitaryOrderType::Hold:
          pressed==PressTarget::Defend?stellar::core::MilitaryOrderType::Defend:
                                          stellar::core::MilitaryOrderType::Retreat;
    } else if(locate&&fleet->locate==locate) {
      command.kind=FleetWorkspaceCommandKind::Locate;
      command.fleet_id=fleet->id;
      command.locate_quote=std::move(locate);
    }
    return command;
  }
  if (event.type == InputEventType::Wheel &&
      presentation_ == FleetWorkspacePresentation::Outliner && layout.list.contains(event.position)) {
    list_scroll_.sync(fleet_content_height(layout), layout.list.height);
    list_scroll_.scroll_by(-event.wheel_y * 36.f * layout.scale);
    return {FleetWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::RightPressed) {
    if (visible_panel && layout.panel.contains(event.position))
      return {FleetWorkspaceCommandKind::None, true};
    if (target_system_id && selected_fleet_id() && selected_fleet() && !selected_fleet()->foreign_inspection)
      return {FleetWorkspaceCommandKind::Preview, true, 0,
              *target_system_id};
    return {};
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
    // Rows clipped by the outliner viewport stay in the ring; when focus
    // lands on one, snap the list so the row is fully visible — which
    // exposes the next row and keeps the whole list keyboard-reachable.
    const auto snap_focused = [&] {
      if (focus_ < 0 || focus_ >= count) return;
      const auto &target = items[static_cast<std::size_t>(focus_)];
      if (!target.unclipped) return;
      list_scroll_.sync(fleet_content_height(layout),
                        layout.list.height);
      list_scroll_.scroll_interval_into_view(
          target.unclipped->y, target.unclipped->y + target.unclipped->height,
          layout.list.y, layout.list.y + layout.list.height);
    };
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      snap_focused();
      return {FleetWorkspaceCommandKind::None, true};
    }
    if (count > 0 && (fwd || bwd)) {
      if (focus_ < 0 || focus_ >= count) {
        focus_ = bwd ? count - 1 : 0;
      } else {
        // Walking past a boundary releases the ring so the dispatcher can
        // hand the same key to the next map focus group.
        const int next = focus_ + (bwd ? -1 : 1);
        if (next < 0 || next >= count) {
          focus_ = -1;
          return {};
        }
        focus_ = next;
      }
      snap_focused();
      return {FleetWorkspaceCommandKind::None, true};
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &r = items[static_cast<std::size_t>(focus_)].bounds;
      const Point at{r.x + r.width * .5f, r.y + r.height * .5f};
      InputEvent press{InputEventType::LeftPressed};
      press.position = at;
      const int keep = focus_;
      auto command = handle(press, width, height, markers, target_system_id);
      if (command.kind == FleetWorkspaceCommandKind::None) {
        // Release-gated controls (orders, locate) fire on the matched release.
        InputEvent release{InputEventType::LeftReleased};
        release.position = at;
        command = handle(release, width, height, markers, target_system_id);
      }
      focus_ = keep;
      command.captured = true;
      return command;
    }
    return {};
  }
  if (event.type != InputEventType::LeftPressed) return {};
  focus_ = -1;
  if (visible_panel && layout.panel.contains(event.position)) {
    // New strategic and Locate controls require an exact matched release and
    // retain the displayed quote, never a later selection's authority.
    if (!preview_ && !pending_return_) {
      if(const auto *fleet=selected_fleet();fleet) {
        const auto target=pressed_target_at(event.position,layout);
        const bool military_target=target==PressTarget::Hold||target==PressTarget::Defend||target==PressTarget::Retreat;
        const bool locate_target=fleet->locate&&
            ((fleet->recovery&&layout.civilian_locate.contains(event.position))||
             (fleet->military_order_quote&&layout.military_locate.contains(event.position))||
             (!fleet->recovery&&!fleet->military_order_quote&&layout.locate.contains(event.position)));
        if(military_target&&fleet->military_order_quote) {
          pressed_action_=target;
          pressed_bounds_=target==PressTarget::Hold?layout.order_hold:
              target==PressTarget::Defend?layout.order_defend:layout.order_retreat;
          pressed_military_quote_=fleet->military_order_quote;
          pressed_locate_quote_.reset();
          return {FleetWorkspaceCommandKind::None,true};
        }
        if(locate_target) {
          pressed_action_=PressTarget::Locate;
          pressed_bounds_=fleet->recovery?layout.civilian_locate:
              fleet->military_order_quote?layout.military_locate:layout.locate;
          pressed_locate_quote_=fleet->locate;
          pressed_military_quote_.reset();
          return {FleetWorkspaceCommandKind::None,true};
        }
      }
    }
    if (const auto *fleet = selected_fleet(); !preview_ && fleet && fleet->recovery) {
      const bool left = layout.recovery_left.contains(event.position);
      const bool right = layout.recovery_right.contains(event.position);
      if (pending_return_ && right) {
        cancel_recovery();
        set_notice(tr("FLEET_RETURN_CANCELLED",
                      "Return cancelled. The existing mission is unchanged."),
                   true);
        return {FleetWorkspaceCommandKind::None, true};
      }
      if (left || (right && !fleet->recovery->return_requested)) {
        FleetWorkspaceCommand command{FleetWorkspaceCommandKind::Recovery, true, fleet->id};
        command.recovery_quote = pending_return_.value_or(*fleet->recovery);
        command.confirm_abandon = pending_return_.has_value();
        command.recovery_action = pending_return_ || right
            ? NativeCivilianRecoveryAction::ReturnToBase
            : fleet->recovery->hold_requested ? NativeCivilianRecoveryAction::Resume
                                               : NativeCivilianRecoveryAction::Hold;
        return command;
      }
    }
    if (preview_ && preview_->command_available &&
        layout.confirm.contains(event.position))
      return {FleetWorkspaceCommandKind::Confirm, true};
    if (const auto* fleet=selected_fleet(); !preview_&&fleet&&!fleet->foreign_inspection&&
        fleet->role==stellar::core::FleetRole::Military&&fleet->current_system_id&&
        !fleet->destination_system_id&&fleet->combat_status&&fleet->combat_status->is_armed&&
        layout.engage.contains(event.position))
      return {FleetWorkspaceCommandKind::Engage,true,fleet->id};
    if (view_ && presentation_ == FleetWorkspacePresentation::Outliner) {
      for (const auto &row : fleet_rows(layout)) {
        if (row.header) continue;
        const UiRect rect{layout.list.x,
                          layout.list.y - list_scroll_.scroll_offset +
                              row.top,
                          layout.list.width, row.height - 4.f * layout.scale};
        const auto clipped = intersection(rect, layout.list);
        if (clipped && clipped->contains(event.position))
          return {FleetWorkspaceCommandKind::Select, true,
                  view_->own_fleets[row.fleet_index].id};
      }
    }
    // EmpireOverviewPanel colony buttons (empire mode — no fleet selected).
    if (overview_ && !selected_fleet_id()) {
      const UiRect content{layout.details.x, layout.details.y,
                           layout.details.width,
                           layout.route.y + layout.route.height -
                               layout.details.y};
      const auto overview_layout =
          native_overview::overview_layout_for(*overview_, content);
      for (std::size_t index = 0;
           index < overview_layout.colony_rows.size(); ++index)
        if (overview_layout.colony_rows[index].contains(event.position))
          return {FleetWorkspaceCommandKind::OpenColony, true, 0, 0, {},
                  overview_->colonies[index].colony_id};
    }
    return {FleetWorkspaceCommandKind::None, true};
  }
  std::vector<int> hits;
  const auto radius = 11.f * layout.scale;
  for (const auto &marker : markers)
    if (std::hypot(marker.position.x - event.position.x,
                   marker.position.y - event.position.y) <= radius)
      hits.push_back(marker.fleet_id);
  if (!hits.empty())
    return {FleetWorkspaceCommandKind::SelectHits, true, 0, 0,
            std::move(hits)};
  return {};
}

void NativeFleetWorkspace::render(
    DrawList &out, int width, int height,
    std::span<const FleetScreenMarker> markers,
    stellar::native_ship_ui::NativeShipArtAssets *ship_art,
    const stellar::native_overview::OverviewImageProvider *portraits) const {
  last_ship_art_rows_ = 0;
  auto layout = this->layout(width, height);
  if(!view_||view_->own_fleets.empty()){
    layout.panel.height=142.f*layout.scale;
    layout.list.height=80.f*layout.scale;
  }
  const auto artwork = [&](const stellar::native_fleet::NativeOwnFleet &fleet) {
    if (!ship_art) return std::shared_ptr<const RgbaImage>{};
    return ship_art->image_for(
        fleet.design_id ? std::optional<std::string_view>(*fleet.design_id)
                        : std::nullopt,
        fleet.role);
  };
  for (const auto &marker : markers) {
    const auto selected = selected_fleet_id() == marker.fleet_id;
    out.circles.push_back(
        {marker.position, selected ? 9.f * layout.scale : 6.f * layout.scale,
         {own_color.r, own_color.g, own_color.b, 55}});
    out.circles.push_back(
        {marker.position, selected ? 5.f * layout.scale : 3.5f * layout.scale,
         own_color});
  }

  if (!panel_bounds(width, height)) return;
  theme::panel(out, layout.panel);
  const bool outliner = presentation_ == FleetWorkspacePresentation::Outliner;
  text(out, layout.heading, tr(outliner ? (view_&&view_->developer_inspection?"FLEET_ALL":"FLEET_PLAYER") : (selected_fleet()&&selected_fleet()->foreign_inspection?"FLEET_INSPECTION":"FLEET_COMMAND"), outliner ? (view_&&view_->developer_inspection?"ALL FLEETS":"PLAYER FLEETS") : (selected_fleet()&&selected_fleet()->foreign_inspection?"FLEET INSPECTION":"FLEET COMMAND")), bright,
       layout.title_font_pixels, FontFace::Heading);
  if (outliner) {
  theme::fill(out,layout.list,theme::color::surface);
  theme::stroke(out,layout.list,theme::color::keyline);
  if (!view_ || view_->own_fleets.empty()) {
    text(out,
         {layout.list.x + 10.f * layout.scale,
          layout.list.y + 12.f * layout.scale,
          layout.list.width - 20.f * layout.scale,
          layout.list.height - 24.f * layout.scale},
         tr("FLEET_NO_FLEETS",
            "No active player fleets. Complete construction before issuing "
            "travel orders."),
         muted, layout.body_font_pixels);
  } else {
    for (const auto &entry : fleet_rows(layout)) {
      const UiRect row{layout.list.x,
                       layout.list.y - list_scroll_.scroll_offset +
                           entry.top,
                       layout.list.width,
                       entry.height - (entry.header ? 0.f : 4.f * layout.scale)};
      const auto clipped = intersection(row, layout.list);
      if (!clipped) continue;
      if (entry.header) {
        theme::section_header(
            out, {row.x + 8.f * layout.scale, row.y + 2.f * layout.scale,
                  row.width - 16.f * layout.scale, row.height - 4.f * layout.scale},
            entry.caption, layout.small_font_pixels, theme::Tone::Neutral,
            {}, layout.list);
        continue;
      }
      const auto &fleet = view_->own_fleets[entry.fleet_index];
      const auto selected = view_->selected_fleet_id == fleet.id;
      fill(out, *clipped,
           selected ? selected_color
                    : clipped->contains(pointer_) ? hover_color : row_color);
      if (selected)
        fill(out, {clipped->x, clipped->y, 3.f, clipped->height}, own_color);
      float text_left = row.x + 8.f * layout.scale;
      float text_width = row.width - 16.f * layout.scale;
      if (ship_art) {
        const auto image = artwork(fleet);
        if (image) {
          const float side = 33.f * layout.scale;
          out.overlay.emplace_back(Image{
              image,
              {row.x + 4.f * layout.scale, row.y + 4.f * layout.scale, side,
               side},
              std::nullopt, {255, 255, 255, 255}, *clipped});
          ++last_ship_art_rows_;
          text_left = row.x + 42.f * layout.scale;
          text_width = std::max(0.f, row.width - 50.f * layout.scale);
        }
      }
      if (const auto name_clip = intersection(
              *clipped,
              {text_left - 2.f * layout.scale, row.y + 3.f * layout.scale,
               text_width, 18.f * layout.scale}))
        out.overlay.emplace_back(Text{
            {text_left, row.y + 5.f * layout.scale},
            fleet.name, bright, layout.body_font_pixels,
            text_width, *name_clip});
      if (const auto role_clip = intersection(
              *clipped,
              {text_left - 2.f * layout.scale, row.y + 21.f * layout.scale,
               text_width, 17.f * layout.scale}))
        out.overlay.emplace_back(Text{
            {text_left, row.y + 23.f * layout.scale},
            tr(role_key(fleet.role), role_name(fleet.role)) + "  |  " +
                (fleet.foreign_inspection
                     ? fleet.owner_name
                     : tr(transit_key(fleet.transit_phase),
                          transit_name(fleet.transit_phase))),
            muted, layout.small_font_pixels,
            text_width, *role_clip});
    }
    list_scroll_.sync(fleet_content_height(layout), layout.list.height);
    theme::scrollbar(out,
                     {layout.list.x + layout.list.width - 2.f * layout.scale,
                      layout.list.y, 2.f * layout.scale, layout.list.height},
                     list_scroll_, 20.f * layout.scale);
  }

  }
  const auto *fleet = selected_fleet();
  if(!view_||view_->own_fleets.empty())return;
  const auto action_button = [&](UiRect bounds,std::string label) {
    theme::button(out,bounds,std::move(label),pointer_,layout.small_font_pixels);
  };
  if (!fleet) {
    // Reference EmpireOverviewPanel empire mode: the selected-system home
    // reference plus the own-colony quick list fill the detail area.
    if (overview_) {
      const UiRect content{layout.details.x, layout.details.y,
                           layout.details.width,
                           layout.route.y + layout.route.height -
                               layout.details.y};
      native_overview::render_empire_overview(
          out, *overview_,
          native_overview::overview_layout_for(*overview_, content),
          pointer_, portraits, locale_);
    } else {
      text(out, layout.details,
           view_->developer_inspection
               ? tr("FLEET_SELECT_ANY",
                    "Select any fleet on the map or in the outliner.")
               : tr("FLEET_SELECT_OWNED",
                    "Select an owned fleet on the map or in the outliner."),
           muted,
           layout.body_font_pixels);
    }
  } else if (pending_return_) {
    // Use the entire details region: never truncate the paid-mission warning.
    const UiRect warning_bounds{layout.details.x, layout.details.y,
        layout.details.width, layout.feedback.y - layout.details.y - 8.f * layout.scale};
    text(out, warning_bounds,
         tr("FLEET_ABANDON_MISSION", "ABANDON COLONY MISSION?") + "\n\n" +
             return_warning_,
         warning, layout.body_font_pixels);
  } else {
    const bool armed_order = !preview_ && !pending_return_ &&
        fleet->military_order_quote.has_value();
    const bool recovery_locate = !preview_ && !pending_return_ &&
        fleet->recovery.has_value() && fleet->locate.has_value();
    auto details_bounds = layout.details;
    if (armed_order || recovery_locate)
      details_bounds.height = std::max(0.f, details_bounds.height - 36.f * layout.scale);
    if (ship_art) {
      const auto image = artwork(*fleet);
      const float side = std::min(details_bounds.height - 8.f * layout.scale,
                                  (details_bounds.width>300.f*layout.scale?96.f:52.f) * layout.scale);
      if (image && side >= 8.f * layout.scale) {
        out.overlay.emplace_back(Image{
            image,
            {details_bounds.x + details_bounds.width - side -
                 4.f * layout.scale,
             details_bounds.y + 4.f * layout.scale, side, side},
            std::nullopt, {255, 255, 255, 255}, details_bounds});
        details_bounds.width -= side + 10.f * layout.scale;
        if (!outliner) ++last_ship_art_rows_;
      }
    }
    // Name + identity line, then scannable stat rows instead of one text blob.
    const float row_height = std::max(16.f * layout.scale,
        static_cast<float>(layout.small_font_pixels) + 6.f * layout.scale);
    float row_y = details_bounds.y;
    text(out, {details_bounds.x, row_y, details_bounds.width, row_height},
         fleet->name, bright, layout.body_font_pixels, FontFace::Heading);
    row_y += row_height + 2.f * layout.scale;
    text(out, {details_bounds.x, row_y, details_bounds.width, row_height},
         tr(role_key(fleet->role), role_name(fleet->role)) + "  |  " +
             tr(transit_key(fleet->transit_phase),
                transit_name(fleet->transit_phase)),
         muted, layout.small_font_pixels);
    row_y += row_height + 4.f * layout.scale;
    const auto stat = [&](std::string label, std::string value) {
      theme::key_value(out, {details_bounds.x, row_y, details_bounds.width,
                             row_height},
                       std::move(label), std::move(value),
                       layout.small_font_pixels);
      row_y += row_height;
    };
    stat(tr("FLEET_STAT_STRENGTH", "Strength"), number(fleet->combat_power));
    stat(tr("FLEET_STAT_FUEL", "Fuel"),
         number(fleet->fuel_remaining_light_years, 2) + " / " +
             number(fleet->fuel_capacity_light_years, 2) + " ly");
    stat(tr("FLEET_STAT_RANGE", "Range"),
         number(fleet->maximum_leg_range_light_years, 2) + " ly");
    stat(tr("FLEET_STAT_SPEED", "Speed"),
         number(fleet->strategic_speed, 2) + " ly/day");
    if (fleet->military_order_quote)
      stat(tr("FLEET_STAT_ORDER", "Order"),
           tr(military_order_key(
                  fleet->military_order_quote->current_order),
              military_order_name(
                  fleet->military_order_quote->current_order)));
    if (armed_order) {
      // The selected fleet's quote is retained until release, and becomes
      // invalid as soon as selection, observer, campaign, or order changes.
      action_button(layout.order_hold,tr("FLEET_ORDER_HOLD_BTN","HOLD"));
      action_button(layout.order_defend,tr("FLEET_ORDER_DEFEND_BTN","DEFEND"));
      action_button(layout.order_retreat,tr("FLEET_ORDER_RETREAT_BTN","RETREAT"));
    } else if (recovery_locate) {
      // Recovery keeps both paid-mission controls in the confirm rail.
      action_button(layout.civilian_locate,tr("FLEET_LOCATE","LOCATE"));
    }
    std::string route;
    if (preview_) {
      route = trf("FLEET_ROUTE_PREVIEW",
                  {target_display_name_,
                   number(preview_->route_distance_light_years, 2),
                   preview_->route_authoritative
                       ? tr("FLEET_ROUTE_CONFIRMED", "Confirmed lane route")
                       : tr("FLEET_ROUTE_PENDING",
                            "Route awaiting confirmation")},
                  "ROUTE PREVIEW\nDestination {0}\nDistance {1} ly\n{2}");
      if (preview_->estimated_transit_days)
        route += trf("FLEET_ETA_SUFFIX",
                     {stellar::native_campaign::format_campaign_duration(
                         *preview_->estimated_transit_days)},
                     "\nEstimated ETA {0}");
    } else if (fleet->destination_system_id) {
      route = trf("FLEET_TRAVEL_STATUS",
                  {number(fleet->transit_progress * 100., 1)},
                  "TRAVEL STATUS\nTravel order active\nTransit progress {0}%");
    } else if (fleet->reconnaissance) {
      const auto &reconnaissance = *fleet->reconnaissance;
      if (reconnaissance.completed) {
        route = reconnaissance.fully_surveyed
                    ? tr("FLEET_RECON_SURVEYED",
                         "RECONNAISSANCE\nRapid reconnaissance complete\n"
                         "System fully surveyed")
                    : tr("FLEET_RECON_PARTIAL",
                         "RECONNAISSANCE\nRapid reconnaissance complete\nSend "
                         "a science vessel for a full survey.");
      } else {
        route = trf("FLEET_RECON_PROGRESS",
                    {reconnaissance.held
                         ? tr("FLEET_RECON_HELD", "Held; work paused")
                         : tr("FLEET_RECON_LOCAL", "Local work"),
                     number(reconnaissance.days_completed, 1),
                     number(reconnaissance.required_days, 1)},
                    "RECONNAISSANCE\n{0}\nWork {1} / {2} work-days");
        const auto progress = reconnaissance.required_days > 0.
                                  ? std::clamp(reconnaissance.days_completed /
                                                   reconnaissance.required_days,
                                               0., 1.)
                                  : 0.;
        const UiRect bar{layout.route.x, layout.route.y + layout.route.height -
                             7.f * layout.scale,
                         layout.route.width, 4.f * layout.scale};
        theme::progress(out, bar, progress,
                        reconnaissance.held ? theme::Tone::Unknown
                                            : theme::Tone::Success);
      }
    } else if (fleet->science_survey) {
      const auto &survey = *fleet->science_survey;
      if (survey.completed) {
        route = tr("FLEET_SURVEY_DONE",
                   "SCIENCE SURVEY\nSystem fully surveyed\nSelect a planet to "
                   "review findings.");
      } else {
        route = trf("FLEET_SURVEY_PROGRESS",
                    {survey.held
                         ? tr("FLEET_SURVEY_HELD", "Held; work paused")
                         : tr("FLEET_SURVEY_LOCAL", "Detailed local work"),
                     number(survey.progress * 100., 1)},
                    "SCIENCE SURVEY\n{0}\nFull survey {1}%");
        const UiRect bar{layout.route.x, layout.route.y + layout.route.height -
                             7.f * layout.scale,
                         layout.route.width, 4.f * layout.scale};
        theme::progress(out, bar, survey.progress,
                        survey.held ? theme::Tone::Unknown
                                    : theme::Tone::Success);
      }
    } else {
      route = fleet->foreign_inspection
                  ? tr("FLEET_LIVE_INSPECTION",
                       "LIVE INSPECTION\nFleet is stationed locally.\nSelect "
                       "LOCATE to view its position.")
                  : tr("FLEET_ROUTE_HINT",
                       "ROUTE PREVIEW\nRight-click a system to preview travel.");
    }
    text(out, {layout.route.x + 10.f * layout.scale,
               layout.route.y + 8.f * layout.scale,
               layout.route.width - 20.f * layout.scale,
               layout.route.height - 16.f * layout.scale},
         std::move(route), bright, layout.small_font_pixels);
    if (fleet->destination_system_id)
      native_ui::progress(
          out,
          {layout.route.x + 10.f * layout.scale,
           layout.route.y + layout.route.height - 10.f * layout.scale,
           layout.route.width - 20.f * layout.scale, 5.f * layout.scale},
          fleet->transit_progress, native_ui::Tone::Selected);
  }

  const auto* selected=selected_fleet();
  const auto tactical_help = [&]() -> std::string {
    if (!selected || !selected->military_order_quote || preview_ || pending_return_)
      return {};
    if (layout.order_hold.contains(pointer_))
      return tr("FLEET_HELP_HOLD",
                "Hold changes combat stance; it does not stop travel. No "
                "movement or resource charge now.");
    if (layout.order_defend.contains(pointer_))
      return tr("FLEET_HELP_DEFEND",
                "Defend protects this system in combat. No movement or "
                "resource charge now.");
    if (layout.order_retreat.contains(pointer_))
      return tr("FLEET_HELP_RETREAT",
                "Retreat requests combat disengagement; it does not route "
                "home. No movement or resource charge now.");
    return {};
  }();
  const auto feedback = !tactical_help.empty() ? tactical_help
                        : pending_return_ ? tr("FLEET_CANCEL_KEEP",
                                               "Cancel keeps the existing mission and its progress.")
                        : !notice_.empty()
                            ? notice_
                            : preview_ && !preview_->command_available
                                  ? preview_->message
                                  : selected && selected->military_order_quote
                                      ? tr("FLEET_HELP_ALL",
                                           "Hold changes stance; Defend protects this system; Retreat requests disengagement. No movement or resource charge now.")
                                  : selected ? selected->recovery_message : std::string{};
  if (!feedback.empty())
    text(out, layout.feedback, visible_message(feedback),
         notice_.empty() || notice_accepted_ ? muted : failure,
         layout.small_font_pixels);
  if (!preview_ && selected && selected->recovery) {
    const bool queued = selected->recovery->return_requested;
    for (const bool left : {true, false}) {
      const auto bounds = left ? layout.recovery_left : layout.recovery_right;
      const bool enabled = left || pending_return_ || !queued;
      const auto label = pending_return_ ? (left ? tr("FLEET_CONFIRM_RETURN","CONFIRM RETURN") : tr("SETTINGS_CANCEL","CANCEL"))
          : left ? tr(selected->recovery->hold_requested ? "FLEET_RESUME" : "FLEET_HOLD",
                      selected->recovery->hold_requested ? "RESUME" : "HOLD")
                 : tr(queued ? "FLEET_RETURN_QUEUED" : "FLEET_RETURN_BASE",
                      queued ? "RETURN QUEUED" : "RETURN TO BASE");
      theme::button(out, bounds, label, pointer_, layout.small_font_pixels,
                    theme::Tone::Selected, pending_return_ && left, enabled);
    }
  }
  const bool engage=!preview_&&!pending_return_&&selected&&!selected->foreign_inspection&&selected->role==stellar::core::FleetRole::Military&&
      selected->current_system_id&&!selected->destination_system_id&&
      selected->combat_status&&selected->combat_status->is_armed;
  const bool locate_on_rail=!preview_&&!pending_return_&&selected&&selected->locate&&
      !selected->recovery&&!selected->military_order_quote;
  if (preview_ && preview_->command_available) {
    theme::button(out, layout.confirm, tr("FLEET_CONFIRM_TRAVEL", "CONFIRM TRAVEL"),
                  pointer_, layout.body_font_pixels, theme::Tone::Success, true);
  } else if (selected && selected->military_order_quote && !preview_ && !pending_return_) {
    action_button(layout.military_locate,tr("FLEET_LOCATE","LOCATE"));
    if (engage) action_button(layout.engage,tr("FLEET_ENGAGE","ENGAGE HOSTILES"));
  } else if (locate_on_rail) {
    action_button(layout.locate,tr("FLEET_LOCATE","LOCATE"));
  } else if (engage) {
    action_button(layout.engage,tr("FLEET_ENGAGE","ENGAGE HOSTILES"));
  }
  if (view_)
    for (const auto &entry : fleet_rows(layout)) {
      if (entry.header) continue;
      const auto &candidate = view_->own_fleets[entry.fleet_index];
      const UiRect row{layout.list.x,
                       layout.list.y - list_scroll_.scroll_offset +
                           entry.top,
                       layout.list.width, entry.height - 4.f * layout.scale};
      if (!layout.list.contains(pointer_) || !row.contains(pointer_)) continue;
      native_ui::tooltip(
          out, {layout.panel.x - 330.f * layout.scale, row.y}, candidate.name,
          trf("FLEET_TOOLTIP",
              {tr(role_key(candidate.role), role_name(candidate.role)),
               tr(transit_key(candidate.transit_phase),
                  transit_name(candidate.transit_phase))},
              "{0} · {1}. Select for readiness, range, fuel and orders."),
          width, height, layout.scale, native_ui::Tone::Military);
      break;
    }
  if (focus_ >= 0) {
    const auto items = focusables(layout);
    if (focus_ < static_cast<int>(items.size()))
      theme::focus_ring(out, items[static_cast<std::size_t>(focus_)].bounds);
  }
}

const std::optional<NativeFleetMapView> &NativeFleetWorkspace::view() const noexcept {
  return view_;
}

const std::optional<NativeFleetRoutePreview> &
NativeFleetWorkspace::preview() const noexcept {
  return preview_;
}

std::optional<int> NativeFleetWorkspace::selected_fleet_id() const noexcept {
  return view_ ? view_->selected_fleet_id : std::nullopt;
}

const NativeOwnFleet *NativeFleetWorkspace::selected_fleet() const noexcept {
  if (!view_ || !view_->selected_fleet_id) return nullptr;
  const auto found = std::ranges::find(view_->own_fleets,
                                       *view_->selected_fleet_id,
                                       &NativeOwnFleet::id);
  return found == view_->own_fleets.end() ? nullptr : &*found;
}

} // namespace stellar::native_fleet_ui
