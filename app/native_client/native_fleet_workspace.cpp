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
  const auto fleet_height = detail_space * .43f;
  const UiRect details{inner_x, detail_y, inner_width, fleet_height};
  const UiRect route{inner_x, detail_y + fleet_height + 6.f * scale,
                     inner_width,
                     std::max(0.f, detail_space - fleet_height - 6.f * scale)};
  const UiRect engage{details.x + details.width - 96.f * scale,
                      details.y + details.height - 34.f * scale,
                      92.f * scale, 30.f * scale};
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
    if (const auto *fleet = selected_fleet();
        fleet && fleet->combat_status && fleet->combat_status->is_armed &&
        layout.engage.contains(event.position))
      return {FleetWorkspaceCommandKind::Engage, true, fleet->id};
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
  if (!fleet) {
    text(out, layout.details,
         "Select an owned fleet on the map or in the outliner.", muted,
         layout.body_font_pixels);
  } else {
    std::string details =
        fleet->name + "\n" + role_name(fleet->role) + "  |  " +
        transit_name(fleet->transit_phase) + "\nOwn strength " +
        number(fleet->combat_power) + "\nFuel " +
        number(fleet->fuel_remaining_light_years, 2) + " / " +
        number(fleet->fuel_capacity_light_years, 2) + " ly\nMaximum leg " +
        number(fleet->maximum_leg_range_light_years, 2) + " ly\nSpeed " +
        number(fleet->strategic_speed, 2) + " ly/day";
    // Reference ship inspector Recovery row: an earlier failed return reason
    // wins over the live preview.
    const std::string recovery =
        fleet->return_to_base_failure_reason
            ? *fleet->return_to_base_failure_reason
            : fleet->civilian_return_preview;
    if (native_fleet::is_civilian_role(fleet->role) && !recovery.empty())
      details += "\nRecovery " + recovery;
    auto details_bounds = layout.details;
    if (ship_art) {
      const auto image = artwork(*fleet);
      const float side = std::min(layout.details.height - 8.f * layout.scale,
                                  96.f * layout.scale);
      if (image && side >= 8.f * layout.scale) {
        out.overlay.emplace_back(Image{
            image,
            {layout.details.x + layout.details.width - side -
                 4.f * layout.scale,
             layout.details.y + 4.f * layout.scale, side, side},
            std::nullopt, {255, 255, 255, 255}, layout.details});
        details_bounds.width -= side + 10.f * layout.scale;
      }
    }
    text(out, details_bounds, details, bright, layout.small_font_pixels);
    if (fleet->combat_status && fleet->combat_status->is_armed) {
      fill(out, layout.engage,
           layout.engage.contains(pointer_) ? hover_color : row_color);
      stroke(out, layout.engage,
             layout.engage.contains(pointer_) ? bright : border_color);
      text(out,
           {layout.engage.x, layout.engage.y + layout.engage.height * .3f,
            layout.engage.width, layout.engage.height * .7f},
           "ENGAGE", bright, layout.small_font_pixels, FontFace::Interface,
           TextAlign::Center);
    }
    // Civilian recovery controls (reference CivilianHoldResume /
    // CivilianReturnToBase buttons).
    if (native_fleet::is_civilian_role(fleet->role)) {
      fill(out, layout.hold,
           layout.hold.contains(pointer_) ? hover_color : row_color);
      stroke(out, layout.hold,
             layout.hold.contains(pointer_) ? bright : border_color);
      text(out,
           {layout.hold.x, layout.hold.y + layout.hold.height * .3f,
            layout.hold.width, layout.hold.height * .7f},
           fleet->hold_requested ? "RESUME" : "HOLD", bright,
           layout.small_font_pixels, FontFace::Interface, TextAlign::Center);
      const bool return_disabled = fleet->return_to_base_requested;
      fill(out, layout.return_base,
           !return_disabled && layout.return_base.contains(pointer_)
               ? hover_color
               : row_color);
      stroke(out, layout.return_base,
             !return_disabled && layout.return_base.contains(pointer_)
                 ? bright
                 : border_color);
      text(out,
           {layout.return_base.x,
            layout.return_base.y + layout.return_base.height * .3f,
            layout.return_base.width, layout.return_base.height * .7f},
           return_disabled ? "RETURN QUEUED"
           : return_needs_confirmation_ ? "CONFIRM RETURN"
                                        : "RETURN TO BASE",
           return_disabled ? muted : bright, layout.small_font_pixels,
           FontFace::Interface, TextAlign::Center);
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
    text(out, layout.route, std::move(route), bright,
         layout.small_font_pixels);
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
    fill(out, layout.confirm,
         layout.confirm.contains(pointer_) ? hover_color : selected_color);
    stroke(out, layout.confirm, own_color);
    text(out, {layout.confirm.x + 6.f * layout.scale,
               layout.confirm.y + 9.f * layout.scale,
               layout.confirm.width - 12.f * layout.scale,
               layout.confirm.height - 12.f * layout.scale},
         "CONFIRM TRAVEL", bright, layout.body_font_pixels,
         FontFace::Interface, TextAlign::Center);
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
