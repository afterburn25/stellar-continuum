#include "native_shipyard_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_shipyard_ui {
namespace {
using namespace stellar::native_map;
using namespace stellar::native_shipyard;

constexpr Color panel{7, 17, 32, 252};
constexpr Color inset{5, 14, 27, 250};
constexpr Color row{12, 31, 54, 248};
constexpr Color hover{24, 61, 94, 252};
constexpr Color selected{19, 73, 68, 252};
constexpr Color border{91, 151, 205, 235};
constexpr Color good{102, 232, 164, 255};
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

[[nodiscard]] std::optional<UiRect> intersection(UiRect left,
                                                 UiRect right) noexcept {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto right_edge =
      std::min(left.x + left.width, right.x + right.width);
  const auto bottom = std::min(left.y + left.height, right.y + right.height);
  if (right_edge <= x || bottom <= y) return std::nullopt;
  return UiRect{x, y, right_edge - x, bottom - y};
}

[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
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
  return "Ship";
}

[[nodiscard]] std::string visible_message(std::string value) {
  constexpr std::size_t limit = 180;
  if (value.size() <= limit) return value;
  auto end = limit - 3;
  while (end > 0 &&
         (static_cast<unsigned char>(value[end]) & 0xc0u) == 0x80u)
    --end;
  value.resize(end);
  value += "...";
  return value;
}

} // namespace

ShipyardWorkspaceLayout
ShipyardWorkspaceLayout::for_viewport(int width, int height) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1040.f, h / 650.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 14.f * scale;
  const auto top = 60.f * scale;
  const UiRect surface{margin, top, std::max(1.f, w - margin * 2.f),
                       std::max(1.f, h - top - margin)};
  const auto inner_x = surface.x + 14.f * scale;
  const auto inner_y = surface.y + 54.f * scale;
  const auto inner_w = surface.width - 28.f * scale;
  const auto inner_h = surface.height - 68.f * scale;
  const auto gap = 10.f * scale;
  const auto left_w = std::clamp(inner_w * .25f, 230.f * scale,
                                 340.f * scale);
  const auto right_w = std::clamp(inner_w * .29f, 270.f * scale,
                                  390.f * scale);
  const auto center_w = std::max(220.f * scale,
                                 inner_w - left_w - right_w - gap * 2.f);
  const UiRect designs{inner_x, inner_y, left_w, inner_h};
  const UiRect details{designs.x + designs.width + gap, inner_y, center_w,
                       inner_h - 270.f * scale};
  const UiRect orders{details.x + details.width + gap, inner_y, right_w,
                      inner_h};
  const UiRect action{details.x, inner_y + inner_h - 42.f * scale,
                      details.width, 42.f * scale};
  const UiRect feedback{details.x, action.y - 62.f * scale, details.width,
                        54.f * scale};
  const UiRect readiness{details.x, details.y + details.height + 8.f * scale,
                         details.width,
                         std::max(0.f, feedback.y - details.y -
                                           details.height - 16.f * scale)};
  return {scale,
          static_cast<int>(std::lround(24.f * scale)),
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(12.f * scale)),
          surface,
          {inner_x, surface.y + 14.f * scale,
           surface.width - 92.f * scale, 32.f * scale},
          {surface.x + surface.width - 48.f * scale,
           surface.y + 12.f * scale, 34.f * scale, 34.f * scale},
          designs,
          details,
          orders,
          readiness,
          feedback,
          action};
}

void NativeShipyardWorkspace::open() noexcept { visible_ = true; }

void NativeShipyardWorkspace::close() noexcept {
  visible_ = false;
  cancel_confirmation_id_.reset();
}

bool NativeShipyardWorkspace::visible() const noexcept { return visible_; }

void NativeShipyardWorkspace::set_view(NativeShipyardView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  const auto revision_changed =
      view_ && view_->shipyard_revision != view.shipyard_revision;
  if (generation_changed) {
    selected_design_id_.reset();
    selected_order_id_.reset();
    cancel_confirmation_id_.reset();
    notice_.clear();
    design_scroll_ = 0.f;
    order_scroll_ = 0.f;
  }
  if (revision_changed) cancel_confirmation_id_.reset();
  view_ = std::move(view);
  reconcile_selection();
}

void NativeShipyardWorkspace::discard_campaign() {
  view_.reset();
  selected_design_id_.reset();
  selected_order_id_.reset();
  cancel_confirmation_id_.reset();
  notice_.clear();
  design_scroll_ = 0.f;
  order_scroll_ = 0.f;
}

void NativeShipyardWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
  cancel_confirmation_id_.reset();
}

bool NativeShipyardWorkspace::arm_cancel_confirmation(
    std::string_view order_id) {
  if (!view_) return false;
  const auto found = std::ranges::find(view_->orders, order_id,
                                       &NativeShipyardOrder::order_id);
  if (found == view_->orders.end() || !found->can_cancel) return false;
  selected_design_id_.reset();
  selected_order_id_ = found->order_id;
  cancel_confirmation_id_ = found->order_id;
  notice_ = "Confirm cancellation to return " + found->formatted_refund + ".";
  notice_accepted_ = true;
  return true;
}

const std::optional<NativeShipyardView> &
NativeShipyardWorkspace::view() const noexcept {
  return view_;
}

const std::optional<std::string> &
NativeShipyardWorkspace::selected_design_id() const noexcept {
  return selected_design_id_;
}

const std::optional<std::string> &
NativeShipyardWorkspace::selected_order_id() const noexcept {
  return selected_order_id_;
}

void NativeShipyardWorkspace::reconcile_selection() {
  if (!view_) return;
  if (selected_design_id_ &&
      std::ranges::none_of(view_->available_designs, [&](const auto &design) {
        return design.id == *selected_design_id_;
      }))
    selected_design_id_.reset();
  if (!selected_design_id_ && !selected_order_id_ &&
      !view_->available_designs.empty())
    selected_design_id_ = view_->available_designs.front().id;
  if (selected_order_id_ &&
      std::ranges::none_of(view_->orders, [&](const auto &order) {
        return order.order_id == *selected_order_id_;
      })) {
    selected_order_id_.reset();
    cancel_confirmation_id_.reset();
  }
  if (!selected_order_id_ && !selected_design_id_ && !view_->orders.empty())
    selected_order_id_ = view_->orders.front().order_id;
  if (cancel_confirmation_id_ &&
      cancel_confirmation_id_ != selected_order_id_)
    cancel_confirmation_id_.reset();
}

ShipyardWorkspaceCommand NativeShipyardWorkspace::handle(
    const InputEvent &event, int width, int height) {
  if (!visible_) return {};
  pointer_ = event.position;
  const auto layout = ShipyardWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    cancel_confirmation_id_.reset();
    return {ShipyardWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::Wheel) {
    auto scroll = [&](float &offset, UiRect bounds, std::size_t count) {
      const auto content = static_cast<float>(count) * 58.f * layout.scale;
      offset = std::clamp(offset + event.wheel_y * 40.f * layout.scale,
                          std::min(0.f, bounds.height - content), 0.f);
    };
    const UiRect design_rows{layout.designs.x,
                             layout.designs.y + 27.f * layout.scale,
                             layout.designs.width,
                             layout.designs.height - 27.f * layout.scale};
    const UiRect order_rows{layout.orders.x,
                            layout.orders.y + 27.f * layout.scale,
                            layout.orders.width,
                            layout.orders.height - 27.f * layout.scale};
    if (layout.designs.contains(event.position)) {
      scroll(design_scroll_, design_rows,
             view_ ? view_->available_designs.size() : 0);
      return {ShipyardWorkspaceCommandKind::None, true};
    }
    if (layout.orders.contains(event.position)) {
      scroll(order_scroll_, order_rows, view_ ? view_->orders.size() : 0);
      return {ShipyardWorkspaceCommandKind::None, true};
    }
    return {ShipyardWorkspaceCommandKind::None,
            layout.surface.contains(event.position)};
  }
  if (event.type != InputEventType::LeftPressed)
    return {ShipyardWorkspaceCommandKind::None,
            layout.surface.contains(event.position)};
  if (layout.close.contains(event.position)) {
    close();
    return {ShipyardWorkspaceCommandKind::None, true};
  }
  if (!layout.surface.contains(event.position)) return {};
  if (view_) {
    const UiRect design_rows{layout.designs.x,
                             layout.designs.y + 27.f * layout.scale,
                             layout.designs.width,
                             layout.designs.height - 27.f * layout.scale};
    for (std::size_t index = 0; index < view_->available_designs.size(); ++index) {
      const UiRect bounds{design_rows.x,
                          design_rows.y + design_scroll_ +
                              static_cast<float>(index) * 58.f * layout.scale,
                          design_rows.width, 54.f * layout.scale};
      const auto clipped = intersection(bounds, design_rows);
      if (clipped && clipped->contains(event.position)) {
        selected_design_id_ = view_->available_designs[index].id;
        selected_order_id_.reset();
        cancel_confirmation_id_.reset();
        notice_.clear();
        return {ShipyardWorkspaceCommandKind::None, true};
      }
    }
    const UiRect order_rows{layout.orders.x,
                            layout.orders.y + 27.f * layout.scale,
                            layout.orders.width,
                            layout.orders.height - 27.f * layout.scale};
    for (std::size_t index = 0; index < view_->orders.size(); ++index) {
      const UiRect bounds{order_rows.x,
                          order_rows.y + order_scroll_ +
                              static_cast<float>(index) * 72.f * layout.scale,
                          order_rows.width, 68.f * layout.scale};
      const auto clipped = intersection(bounds, order_rows);
      if (clipped && clipped->contains(event.position)) {
        selected_order_id_ = view_->orders[index].order_id;
        selected_design_id_.reset();
        cancel_confirmation_id_.reset();
        notice_.clear();
        return {ShipyardWorkspaceCommandKind::None, true};
      }
    }
  }
  if (layout.action.contains(event.position)) {
    if (const auto *order = selected_order()) {
      if (!order->can_cancel) return {ShipyardWorkspaceCommandKind::None, true};
      if (cancel_confirmation_id_ != order->order_id) {
        return {ShipyardWorkspaceCommandKind::PrepareCancel, true,
                order->order_id};
      }
      return {ShipyardWorkspaceCommandKind::Cancel, true, order->order_id};
    }
    if (const auto *design = selected_design(); design && design->can_start)
      return {ShipyardWorkspaceCommandKind::Start, true, design->id};
  }
  return {ShipyardWorkspaceCommandKind::None, true};
}

void NativeShipyardWorkspace::render(DrawList &out, int width,
                                     int height) const {
  if (!visible_) return;
  const auto layout = ShipyardWorkspaceLayout::for_viewport(width, height);
  fill(out, layout.surface, panel);
  stroke(out, layout.surface, border);
  text(out, layout.title, "PLAYER SHIPYARD", bright,
       layout.title_font_pixels, FontFace::Heading);
  fill(out, layout.close,
       layout.close.contains(pointer_) ? hover : row);
  stroke(out, layout.close, border);
  text(out, {layout.close.x, layout.close.y + 7.f * layout.scale,
             layout.close.width, layout.close.height - 8.f * layout.scale},
       "X", bright, layout.body_font_pixels, FontFace::Interface,
       TextAlign::Center);

  const auto section = [&](UiRect bounds, std::string heading) {
    fill(out, bounds, inset);
    stroke(out, bounds, border);
    text(out, {bounds.x + 8.f * layout.scale,
               bounds.y + 6.f * layout.scale,
               bounds.width - 16.f * layout.scale, 20.f * layout.scale},
         std::move(heading), muted, layout.small_font_pixels,
         FontFace::Heading);
  };
  section(layout.designs, "KNOWN DESIGNS");
  section(layout.design_details, "DESIGN DETAILS");
  section(layout.orders, "BUILD ORDERS");

  const UiRect design_rows{layout.designs.x,
                           layout.designs.y + 27.f * layout.scale,
                           layout.designs.width,
                           layout.designs.height - 27.f * layout.scale};
  if (!view_ || view_->available_designs.empty()) {
    text(out, {design_rows.x + 10.f * layout.scale,
               design_rows.y + 8.f * layout.scale,
               design_rows.width - 20.f * layout.scale,
               design_rows.height - 16.f * layout.scale},
         "No known designs are available. Research or shipyard "
         "prerequisites remain locked.",
         muted, layout.body_font_pixels);
  } else {
    for (std::size_t index = 0; index < view_->available_designs.size(); ++index) {
      const auto &design = view_->available_designs[index];
      const UiRect bounds{design_rows.x,
                          design_rows.y + design_scroll_ +
                              static_cast<float>(index) * 58.f * layout.scale,
                          design_rows.width, 54.f * layout.scale};
      const auto clipped = intersection(bounds, design_rows);
      if (!clipped) continue;
      fill(out, *clipped,
           selected_design_id_ == design.id
               ? selected
               : clipped->contains(pointer_) ? hover : row);
      if (const auto line = intersection(
              *clipped,
              {bounds.x + 8.f * layout.scale,
               bounds.y + 5.f * layout.scale,
               bounds.width - 16.f * layout.scale, 20.f * layout.scale}))
        text(out, *line, design.name, bright, layout.body_font_pixels);
      if (const auto line = intersection(
              *clipped,
              {bounds.x + 8.f * layout.scale,
               bounds.y + 29.f * layout.scale,
               bounds.width - 16.f * layout.scale, 18.f * layout.scale}))
        text(out, *line,
             role_name(design.role) + "  |  " +
                 design.formatted_credit_cost,
             muted, layout.small_font_pixels);
    }
  }

  const auto *design = selected_design();
  if (!design) {
    text(out, {layout.design_details.x + 10.f * layout.scale,
               layout.design_details.y + 34.f * layout.scale,
               layout.design_details.width - 20.f * layout.scale,
               layout.design_details.height - 44.f * layout.scale},
         "Select an available design to review its requirements.", muted,
         layout.body_font_pixels);
  } else {
    const UiRect body{layout.design_details.x + 10.f * layout.scale,
                      layout.design_details.y + 32.f * layout.scale,
                      layout.design_details.width - 20.f * layout.scale,
                      layout.design_details.height - 42.f * layout.scale};
    std::string details = design->name + "\n" + role_name(design->role) +
                          "\n\n" + design->description +
                          "\n\nSpeed  " + number(design->strategic_speed, 2) +
                          " ly/day\nMaximum leg  " +
                          number(design->maximum_leg_range_light_years, 2) +
                          " ly\nFuel endurance  " +
                          number(design->fuel_endurance_light_years, 2) +
                          " ly\nSensors  " + number(design->sensor_range, 1) +
                          "\nPropulsion  " + design->propulsion_generation;
    text(out, body, std::move(details), bright, layout.small_font_pixels);
  }

  const UiRect order_rows{layout.orders.x,
                          layout.orders.y + 27.f * layout.scale,
                          layout.orders.width,
                          layout.orders.height - 27.f * layout.scale};
  if (!view_ || view_->orders.empty()) {
    text(out, {order_rows.x + 10.f * layout.scale,
               order_rows.y + 8.f * layout.scale,
               order_rows.width - 20.f * layout.scale,
               order_rows.height - 16.f * layout.scale},
         "No ships are under construction.", muted, layout.body_font_pixels);
  } else {
    for (std::size_t index = 0; index < view_->orders.size(); ++index) {
      const auto &order_value = view_->orders[index];
      const UiRect bounds{order_rows.x,
                          order_rows.y + order_scroll_ +
                              static_cast<float>(index) * 72.f * layout.scale,
                          order_rows.width, 68.f * layout.scale};
      const auto clipped = intersection(bounds, order_rows);
      if (!clipped) continue;
      fill(out, *clipped,
           selected_order_id_ == order_value.order_id
               ? selected
               : clipped->contains(pointer_) ? hover : row);
      if (const auto line = intersection(
              *clipped,
              {bounds.x + 8.f * layout.scale,
               bounds.y + 5.f * layout.scale,
               bounds.width - 16.f * layout.scale, 19.f * layout.scale}))
        text(out, *line, order_value.design_name, bright,
             layout.body_font_pixels);
      if (const auto line = intersection(
              *clipped,
              {bounds.x + 8.f * layout.scale,
               bounds.y + 28.f * layout.scale,
               bounds.width - 16.f * layout.scale, 17.f * layout.scale}))
        text(out, *line,
             order_value.active ? "ACTIVE" : "QUEUED", good,
             layout.small_font_pixels);
      if (const auto line = intersection(
              *clipped,
              {bounds.x + 8.f * layout.scale,
               bounds.y + 47.f * layout.scale,
               bounds.width - 16.f * layout.scale, 17.f * layout.scale}))
        text(out, *line,
             "Progress " + number(order_value.progress_fraction * 100., 1) +
                 "%  |  Remaining " +
                 number(order_value.industry_remaining, 1),
             muted, layout.small_font_pixels);
    }
  }

  std::string readiness;
  if (const auto *order_value = selected_order()) {
    readiness = "ORDER\nPopulation reserved " +
                number(order_value->reserved_population_millions, 3) +
                " million\nRefund if cancelled now " +
                order_value->formatted_refund;
    if (order_value->cancellation_blocker)
      readiness += "\n" + *order_value->cancellation_blocker;
  } else if (design && view_) {
    readiness = "COST AND READINESS\nAuthorization " +
                design->formatted_credit_cost + "  |  Industry " +
                number(design->industry_cost, 1) + "  |  Population " +
                number(design->population_cost_millions, 3) +
                " million\nMinimum build time " +
                number(design->minimum_build_days_at_full_shipyard_rate, 2) +
                " days\nTreasury " + view_->formatted_treasury +
                "  |  Available industry " +
                number(view_->available_industry, 1) +
                "\nOrders " +
                std::to_string(view_->pending_build_count) + " / " +
                std::to_string(view_->maximum_pending_builds) +
                (design->will_queue ? "  |  Will queue" : "  |  Builds next");
    if (design->population_source_current_millions)
      readiness += "\nPopulation source " +
                   number(*design->population_source_current_millions, 3) +
                   " million; required before reservation " +
                   number(design->minimum_source_population_millions, 3) +
                   " million";
    readiness += design->can_start
                     ? "\nReady to build."
                     : "\n" + design->start_blocker.value_or(
                                   "Build authorization is unavailable.");
  }
  text(out, layout.readiness, std::move(readiness), muted,
       layout.small_font_pixels);

  if (!notice_.empty())
    text(out, layout.feedback, visible_message(notice_),
         notice_accepted_ ? muted : failure, layout.small_font_pixels);

  std::string action_text;
  bool action_enabled{};
  if (const auto *order_value = selected_order()) {
    action_enabled = order_value->can_cancel;
    action_text = cancel_confirmation_id_ == order_value->order_id
                      ? "CONFIRM CANCEL + REFUND"
                      : "CANCEL ORDER";
    if (!order_value->can_cancel && order_value->cancellation_blocker)
      action_text = "CANCELLATION UNAVAILABLE";
  } else if (design) {
    action_enabled = design->can_start;
    action_text = design->will_queue ? "QUEUE BUILD" : "START BUILD";
    if (!action_enabled) action_text = "BUILD UNAVAILABLE";
  }
  if (!action_text.empty()) {
    fill(out, layout.action,
         action_enabled && layout.action.contains(pointer_) ? hover : row);
    stroke(out, layout.action, action_enabled ? good : border);
    text(out, {layout.action.x + 8.f * layout.scale,
               layout.action.y + 10.f * layout.scale,
               layout.action.width - 16.f * layout.scale,
               layout.action.height - 12.f * layout.scale},
         std::move(action_text), action_enabled ? bright : muted,
         layout.body_font_pixels, FontFace::Interface, TextAlign::Center);
  }
}

const NativeShipDesign *NativeShipyardWorkspace::selected_design() const noexcept {
  if (!view_ || !selected_design_id_) return nullptr;
  const auto found =
      std::ranges::find(view_->available_designs, *selected_design_id_,
                        &NativeShipDesign::id);
  return found == view_->available_designs.end() ? nullptr : &*found;
}

const NativeShipyardOrder *NativeShipyardWorkspace::selected_order() const noexcept {
  if (!view_ || !selected_order_id_) return nullptr;
  const auto found = std::ranges::find(view_->orders, *selected_order_id_,
                                       &NativeShipyardOrder::order_id);
  return found == view_->orders.end() ? nullptr : &*found;
}

} // namespace stellar::native_shipyard_ui
