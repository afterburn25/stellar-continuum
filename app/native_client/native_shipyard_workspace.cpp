#include "native_campaign_calendar.hpp"
#include "native_shipyard_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"
#include <stellar/core/fleet_reach.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <array>
#include <cctype>
#include <fstream>

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

[[nodiscard]] UiRect cover_source(const RgbaImage &image,
                                  UiRect destination) noexcept {
  const auto width = static_cast<float>(image.width());
  const auto height = static_cast<float>(image.height());
  if (destination.width <= 0.f || destination.height <= 0.f)
    return {0.f, 0.f, width, height};
  const auto source_aspect = width / height;
  const auto destination_aspect = destination.width / destination.height;
  if (source_aspect > destination_aspect) {
    const auto crop = height * destination_aspect;
    return {(width - crop) * .5f, 0.f, crop, height};
  }
  const auto crop = width / destination_aspect;
  return {0.f, (height - crop) * .5f, width, crop};
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

[[nodiscard]] float progress_width(double fraction, float width) noexcept {
  const auto progress = std::isfinite(fraction)
                            ? std::clamp(fraction, 0., 1.)
                            : 0.;
  return width * static_cast<float>(progress);
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

#include "native_shipyard_dashboard.inl"

void NativeShipyardWorkspace::open() noexcept { visible_ = true; }

void NativeShipyardWorkspace::close() noexcept {
  visible_ = false;
  search_focused_=false;dropdown_.close();
  cancel_confirmation_id_.reset();
}

bool NativeShipyardWorkspace::visible() const noexcept { return visible_; }

void NativeShipyardWorkspace::set_view(NativeShipyardView view) {
  const bool had_confirmation = cancel_confirmation_id_.has_value();
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  const auto revision_changed =
      view_ && view_->shipyard_revision != view.shipyard_revision;
  const auto orders_changed = view_ && (view_->orders.size() != view.orders.size() ||
      !std::ranges::equal(view_->orders,view.orders,{},&NativeShipyardOrder::order_id,&NativeShipyardOrder::order_id) ||
      !std::ranges::equal(view_->orders,view.orders,[](const auto& left,const auto& right){return left.active==right.active;}));
  if (generation_changed) {
    selected_design_id_.reset();
    selected_order_id_.reset();
    cancel_confirmation_id_.reset();
    notice_.clear();
    design_scroll_ = 0.f;detail_scroll_=0;quantity_=1;search_.clear();category_=0;filter_=0;dropdown_.close();
    order_scroll_ = 0.f;
  }
  if (revision_changed) {
    cancel_confirmation_id_.reset();
    if (had_confirmation) notice_.clear();
  }
  // Preserve immediate command feedback while its canonical order continues,
  // but never leave a completed or changed queue described as pending.
  if (orders_changed && notice_accepted_) notice_.clear();
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
  detail_scroll_=0;detail_limit_=0;quantity_=1;search_.clear();search_focused_=false;
  category_=0;sort_=0;filter_=0;dropdown_.close();
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
