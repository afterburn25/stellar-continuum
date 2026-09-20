#include "native_fleet_workspace.hpp"
#include "native_ui_theme.hpp"

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

constexpr Color row_color = native_ui::color::surface_secondary;
constexpr Color hover_color = native_ui::color::surface_hover;
constexpr Color selected_color = native_ui::color::surface_raised;
constexpr Color border_color = native_ui::color::keyline_strong;
constexpr Color own_color = native_ui::color::success;
constexpr Color bright = native_ui::color::text_primary;
constexpr Color muted = native_ui::color::text_secondary;
constexpr Color warning = native_ui::color::caution;
constexpr Color failure = native_ui::color::danger;

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
  const auto list_height = std::clamp(panel.height * .24f, 82.f * scale,
                                      190.f * scale);
  const UiRect list{inner_x, heading.y + heading.height + 8.f * scale,
                    inner_width, list_height};
  const UiRect confirm{inner_x, panel.y + panel.height - 48.f * scale,
                       inner_width, 36.f * scale};
  const UiRect feedback{inner_x, confirm.y - 54.f * scale, inner_width,
                        46.f * scale};
  const auto detail_y = list.y + list.height + 10.f * scale;
  const auto detail_space = std::max(0.f, feedback.y - detail_y - 6.f * scale);
  const auto fleet_height = detail_space * .58f;
  const UiRect details{inner_x, detail_y, inner_width, fleet_height};
  const UiRect route{inner_x, detail_y + fleet_height + 6.f * scale,
                     inner_width,
                     std::max(0.f, detail_space - fleet_height - 6.f * scale)};
  const UiRect engage{details.x + details.width - 96.f * scale,
                      details.y + details.height - 34.f * scale,
                      92.f * scale, 30.f * scale};
  // Strategic military orders sit at the details bottom-left for armed
  // fleets (reference UiIssueMilitaryOrder Hold/Defend/Retreat buttons);
  // LOCATE hugs the right edge ahead of ENGAGE and reuses the ENGAGE slot
  // for unarmed selections (reference UiFocusOwnedFleet).
  const float order_button_width = 76.f * scale;
  const UiRect order_hold{details.x,
                          details.y + details.height - 34.f * scale,
                          order_button_width, 30.f * scale};
  const UiRect order_defend{order_hold.x + order_button_width + 8.f * scale,
                            order_hold.y, order_button_width, 30.f * scale};
  const UiRect order_retreat{order_defend.x + order_button_width +
                                 8.f * scale,
                             order_hold.y, order_button_width, 30.f * scale};
  const UiRect locate{engage.x - order_button_width - 8.f * scale, engage.y,
                      order_button_width, 30.f * scale};
  // Civilian recovery buttons sit at the details bottom edge, left of ENGAGE
  // (which never coexists with them — armed fleets are military).
  const UiRect hold{details.x, details.y + details.height - 34.f * scale,
                    76.f * scale, 30.f * scale};
  const UiRect return_base{hold.x + hold.width + 8.f * scale, hold.y,
                           138.f * scale, 30.f * scale};
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
          engage,
          order_hold,
          order_defend,
          order_retreat,
          locate,
          hold,
          return_base};
}

void NativeFleetWorkspace::set_view(NativeFleetMapView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  const auto selection_changed =
      !view_ || view_->selected_fleet_id != view.selected_fleet_id;
  if (generation_changed) {
    preview_.reset();
    target_display_name_.clear();
    notice_.clear();
    list_scroll_ = 0.f;
  }
  if (generation_changed || selection_changed)
    return_needs_confirmation_ = false;
  view_ = std::move(view);
  const auto selected = view_->selected_fleet_id
                            ? std::ranges::find(view_->own_fleets,
                                                *view_->selected_fleet_id,
                                                &NativeOwnFleet::id)
                            : view_->own_fleets.end();
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
  view_.reset();
  overview_.reset();
  preview_.reset();
  target_display_name_.clear();
  notice_.clear();
  list_scroll_ = 0.f;
  return_needs_confirmation_ = false;
}

void NativeFleetWorkspace::set_preview(NativeFleetRoutePreview preview,
                                       std::string target_display_name) {
  preview_ = std::move(preview);
  target_display_name_ = std::move(target_display_name);
  notice_.clear();
}

void NativeFleetWorkspace::clear_preview() {
  preview_.reset();
  target_display_name_.clear();
}

void NativeFleetWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
}

FleetWorkspaceCommand NativeFleetWorkspace::handle(
    const InputEvent &event, int width, int height,
    std::span<const FleetScreenMarker> markers,
    std::optional<int> target_system_id) {
  pointer_ = event.position;
  const auto layout = FleetWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) return {};
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
    if (preview_ && preview_->command_available &&
        layout.confirm.contains(event.position))
      return {FleetWorkspaceCommandKind::Confirm, true};
    if (const auto *fleet = selected_fleet(); fleet) {
      const bool armed =
          fleet->combat_status && fleet->combat_status->is_armed;
      if (armed) {
        if (layout.order_hold.contains(event.position))
          return {FleetWorkspaceCommandKind::MilitaryHold, true, fleet->id};
        if (layout.order_defend.contains(event.position))
          return {FleetWorkspaceCommandKind::MilitaryDefend, true, fleet->id};
        if (layout.order_retreat.contains(event.position))
          return {FleetWorkspaceCommandKind::MilitaryRetreat, true, fleet->id};
        if (layout.engage.contains(event.position))
          return {FleetWorkspaceCommandKind::Engage, true, fleet->id};
        if (layout.locate.contains(event.position))
          return {FleetWorkspaceCommandKind::Locate, true, fleet->id};
      } else if (layout.engage.contains(event.position))
        return {FleetWorkspaceCommandKind::Locate, true, fleet->id};
    }
    if (const auto *fleet = selected_fleet();
        fleet && native_fleet::is_civilian_role(fleet->role)) {
      if (layout.hold.contains(event.position))
        return {FleetWorkspaceCommandKind::HoldResume, true, fleet->id};
      if (!fleet->return_to_base_requested &&
          layout.return_base.contains(event.position))
        return {FleetWorkspaceCommandKind::ReturnToBase, true, fleet->id};
    }
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

  native_ui::panel(out, layout.panel, native_ui::Tone::Military);
  text(out, layout.heading, "PLAYER FLEETS", bright,
       layout.title_font_pixels, FontFace::Heading);
  fill(out, layout.list, native_ui::color::surface_opaque);
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
            role_name(fleet.role) + "  |  " + transit_name(fleet.transit_phase),
            muted, layout.small_font_pixels,
            text_width, *role_clip});
    }
  }

  const auto *fleet = selected_fleet();
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
          pointer_, portraits);
    } else {
      text(out, layout.details,
           "Select an owned fleet on the map or in the outliner.", muted,
           layout.body_font_pixels);
    }
  } else {
    fill(out, layout.details, native_ui::color::surface_opaque);
    stroke(out, layout.details, native_ui::color::keyline);
    fill(out, layout.route, native_ui::color::surface_opaque);
    stroke(out, layout.route, native_ui::color::keyline);
    auto details_bounds = layout.details;
    if (ship_art) {
      const auto image = artwork(*fleet);
      const float side = std::min(layout.details.height - 16.f * layout.scale,
                                  96.f * layout.scale);
      if (image && side >= 8.f * layout.scale) {
        out.overlay.emplace_back(Image{
            image,
            {layout.details.x + layout.details.width - side -
                 8.f * layout.scale,
             layout.details.y + 8.f * layout.scale, side, side},
            std::nullopt, {255, 255, 255, 255}, layout.details});
        details_bounds.width -= side + 14.f * layout.scale;
      }
    }
    const auto detail_x = details_bounds.x + 10.f * layout.scale;
    const auto detail_w = details_bounds.width - 20.f * layout.scale;
    text(out, {detail_x, details_bounds.y + 8.f * layout.scale, detail_w,
               24.f * layout.scale},
         fleet->name, bright, layout.body_font_pixels, FontFace::Heading);
    text(out, {detail_x, details_bounds.y + 31.f * layout.scale, detail_w,
               18.f * layout.scale},
         role_name(fleet->role) + "  ·  " +
             transit_name(fleet->transit_phase),
         own_color, layout.small_font_pixels);
    fill(out, {detail_x, details_bounds.y + 53.f * layout.scale, detail_w, 1.f},
         native_ui::color::keyline);
    text(out, {detail_x, details_bounds.y + 62.f * layout.scale, detail_w,
               18.f * layout.scale},
         "COMBAT POWER", muted, layout.small_font_pixels);
    text(out, {detail_x, details_bounds.y + 79.f * layout.scale, detail_w,
               22.f * layout.scale},
         number(fleet->combat_power), bright, layout.body_font_pixels);
    text(out, {detail_x, details_bounds.y + 107.f * layout.scale, detail_w,
               18.f * layout.scale},
         "FUEL  " + number(fleet->fuel_remaining_light_years, 2) + " / " +
             number(fleet->fuel_capacity_light_years, 2) + " ly",
         muted, layout.small_font_pixels);
    native_ui::progress(
        out,
        {detail_x, details_bounds.y + 127.f * layout.scale, detail_w,
         5.f * layout.scale},
        fleet->fuel_capacity_light_years <= 0.
            ? 0.
            : fleet->fuel_remaining_light_years /
                  fleet->fuel_capacity_light_years,
        fleet->fuel_remaining_light_years + 1e-9 >=
                fleet->maximum_leg_range_light_years
            ? native_ui::Tone::Success
            : native_ui::Tone::Caution);
    text(out, {detail_x, details_bounds.y + 141.f * layout.scale, detail_w,
               18.f * layout.scale},
         "Maximum leg " + number(fleet->maximum_leg_range_light_years, 2) +
             " ly  ·  Speed " + number(fleet->strategic_speed, 2) + " ly/day",
         muted, layout.small_font_pixels);
    // Reference ship inspector Recovery row: an earlier failed return reason
    // wins over the live preview.
    const std::string recovery =
        fleet->return_to_base_failure_reason
            ? *fleet->return_to_base_failure_reason
            : fleet->civilian_return_preview;
    if (native_fleet::is_civilian_role(fleet->role) && !recovery.empty() &&
        details_bounds.height >= 250.f * layout.scale)
      text(out, {detail_x, details_bounds.y + 176.f * layout.scale, detail_w,
                 42.f * layout.scale},
           "RECOVERY  " + recovery, warning, layout.small_font_pixels);
    const bool armed =
        fleet->combat_status && fleet->combat_status->is_armed;
    if (armed) {
      const auto order_button = [&](UiRect bounds, const char *label,
                                    native_ui::Tone tone) {
        native_ui::button(out, bounds, label, pointer_,
                          layout.small_font_pixels, tone);
      };
      // Reference UiIssueMilitaryOrder row: Hold / Defend / Retreat.
      order_button(layout.order_hold, "HOLD", native_ui::Tone::Military);
      order_button(layout.order_defend, "DEFEND", native_ui::Tone::Military);
      order_button(layout.order_retreat, "RETREAT", native_ui::Tone::Danger);
      order_button(layout.locate, "LOCATE", native_ui::Tone::Selected);
      order_button(layout.engage, "ENGAGE", native_ui::Tone::Danger);
    } else {
      // Reference UiFocusOwnedFleet: unarmed selections keep the Locate
      // action in the right-edge command slot.
      native_ui::button(out, layout.engage, "LOCATE", pointer_,
                        layout.small_font_pixels, native_ui::Tone::Selected);
    }
    // Civilian recovery controls (reference CivilianHoldResume /
    // CivilianReturnToBase buttons).
    if (native_fleet::is_civilian_role(fleet->role)) {
      native_ui::button(out, layout.hold,
                        fleet->hold_requested ? "RESUME" : "HOLD", pointer_,
                        layout.small_font_pixels, native_ui::Tone::Caution);
      const bool return_disabled = fleet->return_to_base_requested;
      native_ui::button(
          out, layout.return_base,
          return_disabled ? "RETURN QUEUED"
          : return_needs_confirmation_ ? "CONFIRM RETURN"
                                       : "RETURN TO BASE",
          pointer_, layout.small_font_pixels,
          return_needs_confirmation_ ? native_ui::Tone::Caution
                                     : native_ui::Tone::Selected,
          false, !return_disabled);
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
    } else {
      route = "ROUTE PREVIEW\nRight-click a system to preview travel.";
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

  const auto feedback = !notice_.empty()
                            ? notice_
                            : preview_ && !preview_->command_available
                                  ? preview_->message
                                  : std::string{};
  if (!feedback.empty())
    text(out, layout.feedback, visible_message(feedback),
         notice_.empty() || notice_accepted_ ? muted : failure,
         layout.small_font_pixels);
  if (preview_ && preview_->command_available) {
    native_ui::button(out, layout.confirm, "CONFIRM TRAVEL", pointer_,
                      layout.body_font_pixels, native_ui::Tone::Success,
                      true);
  }
  if (view_)
    for (std::size_t index = 0; index < view_->own_fleets.size(); ++index) {
      const auto &candidate = view_->own_fleets[index];
      const UiRect row{layout.list.x,
                       layout.list.y + list_scroll_ +
                           static_cast<float>(index) * 45.f * layout.scale,
                       layout.list.width, 41.f * layout.scale};
      if (!layout.list.contains(pointer_) || !row.contains(pointer_)) continue;
      native_ui::tooltip(
          out, {layout.panel.x - 330.f * layout.scale, row.y}, candidate.name,
          role_name(candidate.role) + " · " +
              transit_name(candidate.transit_phase) +
              ". Select for readiness, range, fuel and orders.",
          width, height, layout.scale, native_ui::Tone::Military);
      break;
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
