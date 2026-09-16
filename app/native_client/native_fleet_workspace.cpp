#include "native_fleet_workspace.hpp"

#include <algorithm>
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

constexpr Color panel_color{7, 17, 32, 242};
constexpr Color row_color{12, 31, 54, 248};
constexpr Color hover_color{24, 61, 94, 252};
constexpr Color selected_color{19, 73, 68, 252};
constexpr Color border_color{91, 151, 205, 235};
constexpr Color own_color{102, 232, 164, 255};
constexpr Color bright{235, 244, 255, 255};
constexpr Color muted{154, 181, 211, 240};
constexpr Color warning{255, 190, 112, 255};
constexpr Color failure{255, 133, 123, 255};

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
                                                         int height) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto scale = std::min(std::max(1.f, h / 900.f),
                              std::max(1.f, w / 900.f));
  const auto inset = 18.f * scale;
  const auto top = 64.f * scale;
  const auto panel_width = std::clamp(w * .255f, 270.f * scale,
                                      360.f * scale);
  const UiRect panel{w - inset - panel_width, top, panel_width,
                     std::max(260.f * scale, h - top - inset)};
  const auto inner_x = panel.x + 12.f * scale;
  const auto inner_width = panel.width - 24.f * scale;
  const UiRect heading{inner_x, panel.y + 12.f * scale, inner_width,
                       28.f * scale};
  const auto list_height = std::clamp(panel.height * .31f, 82.f * scale,
                                      190.f * scale);
  const UiRect list{inner_x, heading.y + heading.height + 8.f * scale,
                    inner_width, list_height};
  const UiRect confirm{inner_x, panel.y + panel.height - 48.f * scale,
                       inner_width, 36.f * scale};
  const UiRect feedback{inner_x, confirm.y - 54.f * scale, inner_width,
                        46.f * scale};
  const auto detail_y = list.y + list.height + 10.f * scale;
  const auto detail_space = std::max(0.f, feedback.y - detail_y - 6.f * scale);
  // Keep seven telemetry lines plus the pinned order rail legible at 720p.
  const auto fleet_height = std::min(detail_space * .72f,
                                      std::max(detail_space * .43f, 180.f * scale));
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
          static_cast<int>(std::lround(20.f * scale)),
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

void NativeFleetWorkspace::set_view(NativeFleetMapView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  if (generation_changed) {
    preview_.reset();
    target_display_name_.clear();
    notice_.clear();
    list_scroll_ = 0.f;
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
    notice_ = "Mission changed. Review Return to Base again before confirming.";
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
  preview_.reset();
  target_display_name_.clear();
  notice_.clear();
  list_scroll_ = 0.f;
  clear_pressed_action();
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

FleetWorkspaceCommand NativeFleetWorkspace::handle(
    const InputEvent &event, int width, int height,
    std::span<const FleetScreenMarker> markers,
    std::optional<int> target_system_id) {
  pointer_ = event.position;
  const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    clear_pressed_action();
    cancel_recovery();
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
  if (event.type == InputEventType::Wheel && layout.list.contains(event.position)) {
    const auto count = view_ ? view_->own_fleets.size() : 0;
    const auto content = static_cast<float>(count) * 45.f * layout.scale;
    const auto minimum = std::min(0.f, layout.list.height - content);
    list_scroll_ = std::clamp(list_scroll_ + event.wheel_y * 36.f * layout.scale,
                              minimum, 0.f);
    return {FleetWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::RightPressed) {
    if (layout.panel.contains(event.position))
      return {FleetWorkspaceCommandKind::None, true};
    if (target_system_id && selected_fleet_id())
      return {FleetWorkspaceCommandKind::Preview, true, 0,
              *target_system_id};
    return {};
  }
  if (event.type != InputEventType::LeftPressed) return {};
  if (layout.panel.contains(event.position)) {
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
        set_notice("Return cancelled. The existing mission is unchanged.", true);
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
    if (const auto* fleet=selected_fleet(); !preview_&&fleet&&
        fleet->role==stellar::core::FleetRole::Military&&fleet->current_system_id&&
        !fleet->destination_system_id&&fleet->combat_status&&fleet->combat_status->is_armed&&
        layout.engage.contains(event.position))
      return {FleetWorkspaceCommandKind::Engage,true,fleet->id};
    if (view_) {
      for (std::size_t index = 0; index < view_->own_fleets.size(); ++index) {
        const UiRect row{layout.list.x,
                         layout.list.y + list_scroll_ +
                             static_cast<float>(index) * 45.f * layout.scale,
                         layout.list.width, 41.f * layout.scale};
        const auto clipped = intersection(row, layout.list);
        if (clipped && clipped->contains(event.position))
          return {FleetWorkspaceCommandKind::Select, true,
                  view_->own_fleets[index].id};
      }
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

void NativeFleetWorkspace::render(DrawList &out, int width, int height,
                                  std::span<const FleetScreenMarker> markers,
                                  stellar::native_ship_ui::NativeShipArtAssets *ship_art) const {
  last_ship_art_rows_ = 0;
  const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
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

  fill(out, layout.panel, panel_color);
  stroke(out, layout.panel, border_color);
  text(out, layout.heading, "PLAYER FLEETS", bright,
       layout.title_font_pixels, FontFace::Heading);
  fill(out, layout.list, {5, 14, 27, 250});
  stroke(out, layout.list, border_color);
  if (!view_ || view_->own_fleets.empty()) {
    text(out,
         {layout.list.x + 10.f * layout.scale,
          layout.list.y + 12.f * layout.scale,
          layout.list.width - 20.f * layout.scale,
          layout.list.height - 24.f * layout.scale},
         "No active player fleets. Complete construction before issuing "
         "travel orders.",
         muted, layout.body_font_pixels);
  } else {
    for (std::size_t index = 0; index < view_->own_fleets.size(); ++index) {
      const auto &fleet = view_->own_fleets[index];
      const UiRect row{layout.list.x,
                       layout.list.y + list_scroll_ +
                           static_cast<float>(index) * 45.f * layout.scale,
                       layout.list.width, 41.f * layout.scale};
      const auto clipped = intersection(row, layout.list);
      if (!clipped) continue;
      const auto selected = view_->selected_fleet_id == fleet.id;
      fill(out, *clipped,
           selected ? selected_color
                    : clipped->contains(pointer_) ? hover_color : row_color);
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
            role_name(fleet.role) + "  |  " + transit_name(fleet.transit_phase),
            muted, layout.small_font_pixels,
            text_width, *role_clip});
    }
  }

  const auto *fleet = selected_fleet();
  const auto action_button = [&](UiRect bounds,const char *label) {
    fill(out,bounds,bounds.contains(pointer_)?hover_color:row_color);
    stroke(out,bounds,bounds.contains(pointer_)?bright:border_color);
    text(out,{bounds.x,bounds.y+bounds.height*.3f,bounds.width,bounds.height*.7f},
         label,bright,layout.small_font_pixels,FontFace::Interface,TextAlign::Center);
  };
  if (!fleet) {
    text(out, layout.details,
         "Select an owned fleet on the map or in the outliner.", muted,
         layout.body_font_pixels);
  } else if (pending_return_) {
    // Use the entire details region: never truncate the paid-mission warning.
    const UiRect warning_bounds{layout.details.x, layout.details.y,
        layout.details.width, layout.feedback.y - layout.details.y - 8.f * layout.scale};
    text(out, warning_bounds, "ABANDON COLONY MISSION?\n\n" + return_warning_,
         warning, layout.body_font_pixels);
  } else {
    std::string details =
        fleet->name + "\n" + role_name(fleet->role) + "  |  " +
        transit_name(fleet->transit_phase) + "\nOwn strength " +
        number(fleet->combat_power) + "\nFuel " +
        number(fleet->fuel_remaining_light_years, 2) + " / " +
        number(fleet->fuel_capacity_light_years, 2) + " ly\nMaximum leg " +
        number(fleet->maximum_leg_range_light_years, 2) + " ly\nSpeed " +
        number(fleet->strategic_speed, 2) + " ly/day";
    if (fleet->military_order_quote)
      details += "\nCurrent tactical order " +
          military_order_name(fleet->military_order_quote->current_order);
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
                                  96.f * layout.scale);
      if (image && side >= 8.f * layout.scale) {
        out.overlay.emplace_back(Image{
            image,
            {details_bounds.x + details_bounds.width - side -
                 4.f * layout.scale,
             details_bounds.y + 4.f * layout.scale, side, side},
            std::nullopt, {255, 255, 255, 255}, details_bounds});
        details_bounds.width -= side + 10.f * layout.scale;
      }
    }
    text(out, details_bounds, details, bright, layout.small_font_pixels);
    if (armed_order) {
      // The selected fleet's quote is retained until release, and becomes
      // invalid as soon as selection, observer, campaign, or order changes.
      action_button(layout.order_hold,"HOLD");
      action_button(layout.order_defend,"DEFEND");
      action_button(layout.order_retreat,"RETREAT");
    } else if (recovery_locate) {
      // Recovery keeps both paid-mission controls in the confirm rail.
      action_button(layout.civilian_locate,"LOCATE");
    }
    std::string route;
    if (preview_) {
      route = "ROUTE PREVIEW\nDestination " + target_display_name_ +
              "\nDistance " +
              number(preview_->route_distance_light_years, 2) + " ly\n" +
              (preview_->route_authoritative ? "Confirmed lane route"
                                             : "Route awaiting confirmation");
      if (preview_->estimated_transit_days)
        route += "\nEstimated ETA " +
                 number(*preview_->estimated_transit_days, 2) + " days";
    } else if (fleet->destination_system_id) {
      route = "TRAVEL STATUS\nTravel order active\nTransit progress " +
              number(fleet->transit_progress * 100., 1) + "%";
    } else if (fleet->reconnaissance) {
      const auto &reconnaissance = *fleet->reconnaissance;
      if (reconnaissance.completed) {
        route = reconnaissance.fully_surveyed
                    ? "RECONNAISSANCE\nRapid reconnaissance complete\nSystem fully surveyed"
                    : "RECONNAISSANCE\nRapid reconnaissance complete\nSend a science vessel for a full survey.";
      } else {
        route = "RECONNAISSANCE\n" +
                std::string(reconnaissance.held ? "Held; work paused\nWork "
                                                : "Local work\nWork ") +
                number(reconnaissance.days_completed, 1) + " / " +
                number(reconnaissance.required_days, 1) + " work-days";
        const auto progress = reconnaissance.required_days > 0.
                                  ? std::clamp(reconnaissance.days_completed /
                                                   reconnaissance.required_days,
                                               0., 1.)
                                  : 0.;
        const UiRect bar{layout.route.x, layout.route.y + layout.route.height -
                             7.f * layout.scale,
                         layout.route.width, 4.f * layout.scale};
        fill(out, bar, row_color);
        fill(out, {bar.x, bar.y, bar.width * static_cast<float>(progress),
                   bar.height},
             reconnaissance.held ? muted : own_color);
        stroke(out, bar, border_color);
      }
    } else if (fleet->science_survey) {
      const auto &survey = *fleet->science_survey;
      if (survey.completed) {
        route = "SCIENCE SURVEY\nSystem fully surveyed\nSelect a planet to review findings.";
      } else {
        route = "SCIENCE SURVEY\n" +
                std::string(survey.held ? "Held; work paused\nFull survey "
                                         : "Detailed local work\nFull survey ") +
                number(survey.progress * 100., 1) + "%";
        const UiRect bar{layout.route.x, layout.route.y + layout.route.height -
                             7.f * layout.scale,
                         layout.route.width, 4.f * layout.scale};
        fill(out, bar, row_color);
        fill(out, {bar.x, bar.y, bar.width * static_cast<float>(survey.progress),
                   bar.height}, survey.held ? muted : own_color);
        stroke(out, bar, border_color);
      }
    } else {
      route = "ROUTE PREVIEW\nRight-click a system to preview travel.";
    }
    text(out, layout.route, std::move(route), bright,
         layout.small_font_pixels);
  }

  const auto* selected=selected_fleet();
  const auto tactical_help = [&]() -> std::string {
    if (!selected || !selected->military_order_quote || preview_ || pending_return_)
      return {};
    if (layout.order_hold.contains(pointer_))
      return "Hold changes combat stance; it does not stop travel. No movement or resource charge now.";
    if (layout.order_defend.contains(pointer_))
      return "Defend protects this system in combat. No movement or resource charge now.";
    if (layout.order_retreat.contains(pointer_))
      return "Retreat requests combat disengagement; it does not route home. No movement or resource charge now.";
    return {};
  }();
  const auto feedback = !tactical_help.empty() ? tactical_help
                        : pending_return_ ? "Cancel keeps the existing mission and its progress."
                        : !notice_.empty()
                            ? notice_
                            : preview_ && !preview_->command_available
                                  ? preview_->message
                                  : selected && selected->military_order_quote
                                      ? "Hold changes stance; Defend protects this system; Retreat requests disengagement. No movement or resource charge now."
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
      fill(out, bounds, enabled && bounds.contains(pointer_) ? hover_color : row_color);
      stroke(out, bounds, pending_return_ && left ? warning : border_color);
      const auto label = pending_return_ ? (left ? "CONFIRM RETURN" : "CANCEL")
          : left ? (selected->recovery->hold_requested ? "RESUME" : "HOLD")
                 : queued ? "RETURN QUEUED" : "RETURN TO BASE";
      text(out, {bounds.x, bounds.y + 10.f * layout.scale, bounds.width,
                 bounds.height - 10.f * layout.scale}, label,
           enabled ? bright : muted, layout.small_font_pixels, FontFace::Interface,
           TextAlign::Center);
    }
  }
  const bool engage=!preview_&&!pending_return_&&selected&&selected->role==stellar::core::FleetRole::Military&&
      selected->current_system_id&&!selected->destination_system_id&&
      selected->combat_status&&selected->combat_status->is_armed;
  const bool locate_on_rail=!preview_&&!pending_return_&&selected&&selected->locate&&
      !selected->recovery&&!selected->military_order_quote;
  if (preview_ && preview_->command_available) {
    fill(out, layout.confirm,
         layout.confirm.contains(pointer_) ? hover_color : selected_color);
    stroke(out, layout.confirm, own_color);
    text(out, {layout.confirm.x + 6.f * layout.scale,
               layout.confirm.y + 9.f * layout.scale,
               layout.confirm.width - 12.f * layout.scale,
               layout.confirm.height - 12.f * layout.scale},
         "CONFIRM TRAVEL", bright, layout.body_font_pixels,
         FontFace::Interface, TextAlign::Center);
  } else if (selected && selected->military_order_quote && !preview_ && !pending_return_) {
    action_button(layout.military_locate,"LOCATE");
    if (engage) action_button(layout.engage,"ENGAGE HOSTILES");
  } else if (locate_on_rail) {
    action_button(layout.locate,"LOCATE");
  } else if (engage) {
    action_button(layout.engage,"ENGAGE HOSTILES");
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
