#include "native_colony_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace stellar::native_colony_ui {
namespace {
using namespace stellar::native_colony;
using namespace stellar::native_map;

constexpr Color panel{7, 17, 32, 252};
constexpr Color inset{5, 14, 27, 250};
constexpr Color row{12, 31, 54, 248};
constexpr Color hover{24, 61, 94, 252};
constexpr Color border{91, 151, 205, 235};
constexpr Color bright{235, 244, 255, 255};
constexpr Color muted{154, 181, 211, 240};
constexpr Color good{102, 232, 164, 255};
constexpr Color warning{244, 189, 94, 255};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, TextAlign align = TextAlign::Left) {
  const auto x = align == TextAlign::Center
                     ? bounds.x + bounds.width * .5f
                     : align == TextAlign::Right ? bounds.x + bounds.width
                                                 : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align,
                                FontFace::Interface});
}
void clipped_text(DrawList &out, UiRect bounds, UiRect clip,
                  std::string value, Color color, int pixels) {
  out.overlay.emplace_back(Text{{bounds.x, bounds.y}, std::move(value), color,
                                pixels, bounds.width, clip, TextAlign::Left,
                                FontFace::Interface});
}
[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}
[[nodiscard]] std::string billions(double millions) {
  const auto magnitude = std::abs(millions);
  if (magnitude >= 1000.0)
    return number(millions / 1000.0, magnitude >= 10000.0 ? 1 : 2) + "B";
  if (magnitude >= 1.0)
    return number(millions, magnitude >= 100.0 ? 0 : 1) + "M";
  return number(millions * 1000.0, magnitude >= .1 ? 0 : 1) + "K";
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
[[nodiscard]] float progress_width(double fraction, float width) noexcept {
  const auto safe = std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0)
                                            : 0.0;
  return static_cast<float>(safe) * width;
}
void panel_title(DrawList &out, UiRect bounds, UiRect clip, std::string value,
                 int pixels) {
  if (const auto visible = intersection(bounds, clip)) {
    fill(out, *visible, inset);
    stroke(out, *visible, border);
    clipped_text(out,
                 {bounds.x + 10, bounds.y + 8, bounds.width - 20, 24},
                 *visible, std::move(value), bright, pixels);
  }
}
[[nodiscard]] bool can_authorize_build(const NativeColonyView &view,
                                       const NativeSurfaceBuildOption &option) {
  return view.treasury_budget_units + 1e-9 >= option.authorization_budget_units;
}
[[nodiscard]] bool can_staff_build(const NativeColonyView &view,
                                   const NativeSurfaceBuildOption &option) {
  return view.workforce_available_millions + 1e-9 >=
         view.workforce_demand_millions + option.workforce_required_millions;
}
[[nodiscard]] bool can_power_build(const NativeColonyView &view,
                                   const NativeSurfaceBuildOption &option) {
  return view.power_supply + option.power_supply + 1e-9 >=
         view.power_demand + option.power_demand;
}
} // namespace

ColonyWorkspaceLayout ColonyWorkspaceLayout::for_viewport(int width,
                                                           int height, bool outpost) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1020.f, h / 650.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 14.f * scale;
  const auto left_margin = native_navigation_content_left * scale;
  const auto top = 60.f * scale;
  const UiRect surface{left_margin, top,
                       std::max(1.f, w - left_margin - margin),
                       std::max(1.f, h - top - margin)};
  const auto inner_x = surface.x + 14.f * scale;
  const auto inner_y = surface.y + 54.f * scale;
  const auto inner_w = surface.width - 28.f * scale;
  const auto inner_h = surface.height - 68.f * scale;
  const auto gap = 10.f * scale;
  const auto left_w = std::max(360.f * scale, inner_w * .54f);
  const auto right_w = std::max(260.f * scale, inner_w - left_w - gap);
  const UiRect details{inner_x, inner_y, left_w, inner_h};
  const UiRect summary{inner_x, inner_y, left_w, 108.f * scale};
  const UiRect sustenance{inner_x, summary.y + summary.height + gap, left_w,
                          210.f * scale};
  const UiRect operations{inner_x, sustenance.y + sustenance.height + gap,
                          left_w, 260.f * scale};
  const UiRect sites{inner_x + left_w + gap, inner_y, right_w, inner_h};
  const UiRect site_rows{sites.x + 8.f * scale, sites.y + 36.f * scale,
                         sites.width - 16.f * scale,
                         sites.height - 44.f * scale};
  ColonyWorkspaceLayout result{scale,
          static_cast<int>(std::lround(24.f * scale)),
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(12.f * scale)),
          surface,
          {inner_x, surface.y + 14.f * scale,
           surface.width - 92.f * scale, 32.f * scale},
          {surface.x + surface.width - 48.f * scale,
           surface.y + 12.f * scale, 34.f * scale, 34.f * scale},
          {surface.x + surface.width - 208.f * scale,
           surface.y + 12.f * scale, 150.f * scale, 34.f * scale},
          details,
          summary,
          sustenance,
          operations,
          sites,
          site_rows};
  result.title.width = result.open_surface.x - result.title.x - 12.f * scale;
  result.collect_freight = {result.open_surface.x - 166.f * scale,
      result.open_surface.y, 156.f * scale, result.open_surface.height};
  if (outpost) {
    result.title.width = result.collect_freight.x - result.title.x - 12.f * scale;
    result.freight_notice = {site_rows.x, sites.y + sites.height - 100.f * scale,
                            site_rows.width, 88.f * scale};
    result.site_rows.height = std::max(1.f, result.freight_notice.y - site_rows.y - 12.f * scale);
  }
  const float mw = std::min(surface.width - 30.f * scale, 700.f * scale);
  const float mh = std::min(surface.height - 30.f * scale, 440.f * scale);
  result.freight_review = {surface.x + (surface.width - mw) * .5f,
      surface.y + (surface.height - mh) * .5f, mw, mh};
  const auto modal = result.freight_review;
  result.freight_text = {modal.x + 18.f * scale, modal.y + 56.f * scale,
      mw - 36.f * scale, mh - 124.f * scale};
  result.freight_cancel = {modal.x + 18.f * scale, modal.y + mh - 52.f * scale,
      (mw - 50.f * scale) * .5f, 34.f * scale};
  result.freight_confirm = {result.freight_cancel.x + result.freight_cancel.width + 14.f * scale,
      result.freight_cancel.y, result.freight_cancel.width, result.freight_cancel.height};
  return result;
}

void NativeColonyWorkspace::open(NativeColonyView view) {
  cancel_freight(); freight_notice_.clear();
  visible_ = true;
  site_scroll_ = 0.f;
  detail_scroll_ = 0.f;
  view_ = std::move(view);
}

void NativeColonyWorkspace::set_view(NativeColonyView view) {
  if (view_ && (view_->campaign_generation != view.campaign_generation ||
                view_->player_civilization_id != view.player_civilization_id ||
                view_->colony_id != view.colony_id || view_->body_id != view.body_id ||
                view_->system_id != view.system_id || view_->resource_outpost != view.resource_outpost)) {
    cancel_freight(); freight_notice_.clear();
    site_scroll_ = 0.f;
    detail_scroll_ = 0.f;
    pointer_ = {};
  }
  view_ = std::move(view);
}

void NativeColonyWorkspace::close() noexcept { cancel_freight(); visible_ = false; }

void NativeColonyWorkspace::discard_campaign() noexcept {
  cancel_freight(); freight_notice_.clear();
  visible_ = false;
  view_.reset();
  site_scroll_ = 0.f;
  detail_scroll_ = 0.f;
  pointer_ = {};
}

void NativeColonyWorkspace::cancel_freight() noexcept {
  freight_preview_.reset(); freight_text_.clear(); freight_scroll_ = 0;
  freight_pressed_ = ColonyWorkspaceCommandKind::None;
}

void NativeColonyWorkspace::set_freight_preview(NativeOutpostFreightPreview preview) {
  cancel_freight();
  if (!visible_ || !view_ || !view_->resource_outpost ||
      preview.campaign_generation != view_->campaign_generation ||
      preview.player_civilization_id != view_->player_civilization_id ||
      preview.colony_id != view_->colony_id || preview.body_id != view_->body_id ||
      preview.system_id != view_->system_id) return;
  if (preview.accepted) {
    freight_text_ = "Ship: " + preview.fleet_name + "\nDeparture: " + preview.home_name +
        "\nCollection: " + preview.outpost_name + "\nCargo capacity: " +
        number(preview.cargo_capacity, 1) + " material units\nStored for pickup: " +
        number(preview.stored_materials, 1) + " | Extraction: " +
        number(preview.extraction_per_day, 2) + " / day\n\n" +
        "No upfront dispatch fee. Ongoing ship upkeep still applies.\n" +
        "The ship travels, loads cargo over time, then returns to its departure colony. "
        "Materials reach your stores only after unloading. Unpause to begin.";
  } else freight_text_ = preview.message;
  freight_preview_ = std::move(preview);
}

float NativeColonyWorkspace::freight_content_height(const ColonyWorkspaceLayout& layout) const {
  const Text label{{}, freight_text_, bright, layout.body_font_pixels, layout.freight_text.width};
  if (measure_) return static_cast<float>(std::max(1, measure_(label).height));
  const auto columns = std::max(1.f, layout.freight_text.width / (layout.body_font_pixels * .56f));
  float lines = 1.f, column = 0.f;
  for (const unsigned char c : freight_text_) {
    if (c == '\n') { ++lines; column = 0.f; }
    else if (++column > columns) { ++lines; column = 1.f; }
  }
  return lines * (layout.body_font_pixels + 4.f * layout.scale);
}

void NativeColonyWorkspace::clamp_scroll(
    const ColonyWorkspaceLayout &layout) noexcept {
  const auto count = view_ ? view_->construction_sites.size() : 0;
  const auto content = static_cast<float>(count) * 82.f * layout.scale;
  site_scroll_ = std::clamp(site_scroll_,
                            std::min(0.f, layout.site_rows.height - content),
                            0.f);
  const auto operation_height =
      (view_ && view_->resource_outpost ? 308.f : 260.f) * layout.scale;
  const auto detail_height = layout.summary.height + layout.sustenance.height +
                             operation_height + 20.f * layout.scale;
  detail_scroll_ =
      std::clamp(detail_scroll_,
                 std::min(0.f, layout.details.height - detail_height), 0.f);
}

ColonyWorkspaceCommand NativeColonyWorkspace::handle(const InputEvent &event,
                                                       int width, int height) {
  if (!visible_) return {};
  pointer_ = event.position;
  const auto layout = ColonyWorkspaceLayout::for_viewport(width, height, view_ && view_->resource_outpost);
  clamp_scroll(layout);
  if (event.type == InputEventType::PointerCancelled) {
    const bool reviewing = freight_preview_.has_value(); cancel_freight();
    return {reviewing ? ColonyWorkspaceCommandKind::CancelFreight : ColonyWorkspaceCommandKind::None, reviewing};
  }
  if (freight_preview_) {
    if (event.type == InputEventType::EscapePressed) {
      cancel_freight(); return {ColonyWorkspaceCommandKind::CancelFreight, true};
    }
    const auto target = layout.freight_cancel.contains(event.position)
        ? ColonyWorkspaceCommandKind::CancelFreight
        : freight_preview_->accepted && layout.freight_confirm.contains(event.position)
        ? ColonyWorkspaceCommandKind::ConfirmFreight : ColonyWorkspaceCommandKind::None;
    if (event.type == InputEventType::LeftPressed) freight_pressed_ = target;
    if (event.type == InputEventType::LeftReleased) {
      const auto pressed = std::exchange(freight_pressed_, ColonyWorkspaceCommandKind::None);
      if (pressed == target && target != ColonyWorkspaceCommandKind::None) {
        const auto revision = freight_preview_->revision;
        if (target == ColonyWorkspaceCommandKind::CancelFreight) cancel_freight();
        return {target, true, revision};
      }
    }
    if (event.type == InputEventType::Wheel && layout.freight_text.contains(event.position)) {
      freight_scroll_ = std::clamp(freight_scroll_ + event.wheel_y * 44.f * layout.scale,
          std::min(0.f, layout.freight_text.height - freight_content_height(layout)), 0.f);
    }
    return {ColonyWorkspaceCommandKind::None, true};
  }
  if (view_->resource_outpost) {
    if (event.type == InputEventType::LeftPressed)
      freight_pressed_ = layout.collect_freight.contains(event.position)
          ? ColonyWorkspaceCommandKind::ReviewFreight : ColonyWorkspaceCommandKind::None;
    if (event.type == InputEventType::LeftReleased) {
      const auto pressed = std::exchange(freight_pressed_, ColonyWorkspaceCommandKind::None);
      if (pressed == ColonyWorkspaceCommandKind::ReviewFreight && layout.collect_freight.contains(event.position))
        return {ColonyWorkspaceCommandKind::ReviewFreight, true};
    }
  }
  if (event.type == InputEventType::EscapePressed ||
      (event.type == InputEventType::LeftPressed &&
       layout.close.contains(event.position))) {
    close();
    return {ColonyWorkspaceCommandKind::Close, true};
  }
  if (event.type == InputEventType::LeftPressed && view_->solid_surface &&
      layout.open_surface.contains(event.position))
    return {ColonyWorkspaceCommandKind::OpenSurface, true};
  if (event.type == InputEventType::Wheel &&
      layout.sites.contains(event.position)) {
    site_scroll_ += event.wheel_y * 44.f * layout.scale;
    clamp_scroll(layout);
    return {ColonyWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::Wheel &&
      layout.details.contains(event.position)) {
    detail_scroll_ += event.wheel_y * 44.f * layout.scale;
    clamp_scroll(layout);
    return {ColonyWorkspaceCommandKind::None, true};
  }
  return {ColonyWorkspaceCommandKind::None,
          layout.surface.contains(event.position)};
}

void NativeColonyWorkspace::render(DrawList &out, int width, int height) const {
  if (!visible_ || !view_) return;
  const auto layout = ColonyWorkspaceLayout::for_viewport(width, height, view_ && view_->resource_outpost);
  const auto &view = *view_;
  const auto operation_height =
      (view.resource_outpost ? 308.f : 260.f) * layout.scale;
  const auto detail_height = layout.summary.height + layout.sustenance.height +
                             operation_height + 20.f * layout.scale;
  const auto detail_scroll =
      std::clamp(detail_scroll_,
                 std::min(0.f, layout.details.height - detail_height), 0.f);
  const auto moved = [detail_scroll](UiRect bounds) {
    bounds.y += detail_scroll;
    return bounds;
  };
  const auto summary = moved(layout.summary);
  const auto sustenance = moved(layout.sustenance);
  auto operations = moved(layout.operations);
  operations.height = operation_height;
  const auto summary_clip = intersection(summary, layout.details);
  const auto sustenance_clip = intersection(sustenance, layout.details);
  const auto operations_clip = intersection(operations, layout.details);
  const auto site_content =
      static_cast<float>(view.construction_sites.size()) * 82.f * layout.scale;
  const auto site_scroll =
      std::clamp(site_scroll_,
                 std::min(0.f, layout.site_rows.height - site_content), 0.f);
  fill(out, layout.surface, panel);
  stroke(out, layout.surface, border);
  text(out, layout.title,
       view.colony_name + "  /  " + view.body_display_name,
       bright, layout.title_font_pixels);
  fill(out, layout.close,
       layout.close.contains(pointer_) ? hover : row);
  stroke(out, layout.close, border);
  text(out, layout.close, "BACK", bright, layout.small_font_pixels,
       TextAlign::Center);
  if (view.solid_surface) {
    fill(out, layout.open_surface,
         layout.open_surface.contains(pointer_) ? hover : row);
    stroke(out, layout.open_surface, good);
    text(out, layout.open_surface, "OPEN SURFACE", bright,
         layout.small_font_pixels, TextAlign::Center);
  }
  if (view.resource_outpost) {
    fill(out, layout.collect_freight, layout.collect_freight.contains(pointer_) ? hover : row);
    stroke(out, layout.collect_freight, good);
    text(out, layout.collect_freight, "COLLECT MATERIALS", bright,
         layout.small_font_pixels, TextAlign::Center);
  }

  panel_title(out, summary, layout.details,
              view.resource_outpost ? "RESOURCE OUTPOST" : "COLONY",
              layout.body_font_pixels);
  const auto sx = summary.x + 10.f * layout.scale;
  auto sy = summary.y + 38.f * layout.scale;
  clipped_text(out, {sx, sy, summary.width - 20.f * layout.scale,
             20.f * layout.scale},
       summary_clip.value_or(UiRect{}),
       "Population " + billions(view.population_millions) +
           "  |  Stability " +
           number(view.stability * 100.0, 0) + "%",
       bright, layout.body_font_pixels);
  sy += 25.f * layout.scale;
  clipped_text(out, {sx, sy, summary.width - 20.f * layout.scale,
             20.f * layout.scale},
       summary_clip.value_or(UiRect{}),
       "Infrastructure " + number(view.infrastructure * 100.0, 0) +
           "%  |  Hub level " + std::to_string(view.surface_hub_level) +
           "  |  Modules " +
           std::to_string(view.construction_sites.size()) + "/" +
           std::to_string(view.building_capacity),
       muted, layout.small_font_pixels);

  panel_title(out, sustenance, layout.details, "SUPPORT & LABOR",
              layout.body_font_pixels);
  const auto ux = sustenance.x + 10.f * layout.scale;
  auto uy = sustenance.y + 38.f * layout.scale;
  const auto line_height = 24.f * layout.scale;
  const auto support_color = view.sustenance_support_ratio >= .999 ? good
                                                                   : warning;
  const auto support = [&](std::string value, Color color = muted) {
    clipped_text(out, {ux, uy, sustenance.width - 20.f * layout.scale,
               20.f * layout.scale},
         sustenance_clip.value_or(UiRect{}),
         std::move(value), color, layout.small_font_pixels);
    uy += line_height;
  };
  support("Food " + billions(view.food_capacity_millions) + " capacity  |  " +
          number(view.food_reserve_days, 1) + " reserve days");
  support("Water " + billions(view.water_capacity_millions) +
          " capacity  |  " + number(view.water_reserve_days, 1) +
          " reserve days");
  support("Housing " + billions(view.housing_capacity_millions) +
          "  |  Supported " + billions(view.supported_population_millions));
  support("Support " + number(view.sustenance_support_ratio * 100.0, 1) +
              "%  |  Limiter " + view.limiting_sustenance_supply,
          support_color);
  support("Employment " + number(view.employment_rate * 100.0, 1) +
          "%  |  Working age " +
          billions(view.working_age_population_millions) +
          "  |  Unemployed " +
          billions(view.unemployed_population_millions));
  support("Surface workforce demand " +
          billions(view.workforce_demand_millions) + " / available " +
          billions(view.workforce_available_millions));

  panel_title(out, operations, layout.details, "ECONOMY & SURFACE OPERATIONS",
              layout.body_font_pixels);
  const auto ox = operations.x + 10.f * layout.scale;
  auto oy = operations.y + 38.f * layout.scale;
  const auto operation = [&](std::string value, Color color = muted) {
    clipped_text(out, {ox, oy, operations.width - 20.f * layout.scale,
               20.f * layout.scale},
         operations_clip.value_or(UiRect{}),
         std::move(value), color, layout.small_font_pixels);
    oy += line_height;
  };
  operation("Treasury " + view.formatted_treasury + "  |  Stored materials " +
            number(view.stored_industry, 1));
  operation("Projected surface production  " +
            view.currency.format_rate(view.credits_per_day));
  operation("Industry " + number(view.industry_per_day, 2) +
            "/day  |  Research " + number(view.science_per_day, 2) +
            "/day");
  operation("Surface upkeep " +
            view.currency.format_rate(view.upkeep_credits_per_day) +
            "  |  Habitat systems " +
            std::to_string(view.required_habitat_systems));
  operation("Power " + number(view.power_supply, 1) + " supplied / " +
                number(view.power_demand, 1) + " required  |  Stored " +
                number(view.stored_power_days, 1) + " days",
            view.power_supply + 1e-9 >= view.power_demand ? good : warning);
  operation("Power storage " + number(view.stored_power_days, 1) + " / " +
            number(view.power_storage_capacity_days, 1) +
            " days  |  Charge " + number(view.storage_charge_per_day, 1) +
            "  Discharge " + number(view.storage_discharge_per_day, 1));
  operation("Habitat support reduction " +
            number(view.habitat_support_reduction * 100.0, 1) +
            "%  |  Environmental wear x" +
            number(view.environmental_wear_multiplier, 2));
  operation("Specialization " + view.specialization_name + "  |  " +
            view.specialization_description);
  if (view.resource_outpost) {
    operation(view.outpost_status + "  |  " + view.deposit_material_name +
                  " " + view.deposit_grade + "  |  Extraction " +
                  number(view.extraction_per_day, 2) + "/day",
              view.has_confirmed_deposit ? good : warning);
    operation("Extracted stores " +
              number(view.stored_extracted_materials, 1) + " / " +
              number(view.extracted_material_capacity, 1) +
              "  |  Cargo transfer " +
              number(view.cargo_transfer_capacity_per_day, 1) + "/day");
  }

  fill(out, layout.sites, inset);
  stroke(out, layout.sites, border);
  text(out, {layout.sites.x + 10.f * layout.scale,
             layout.sites.y + 8.f * layout.scale,
             layout.sites.width - 20.f * layout.scale, 22.f * layout.scale},
       "SURFACE MODULES & CONSTRUCTION", bright, layout.body_font_pixels);
  if (view.construction_sites.empty()) {
    const UiRect guidance{layout.site_rows.x, layout.site_rows.y,
                          layout.site_rows.width,
                          std::min(layout.site_rows.height, 184.f * layout.scale)};
    fill(out, guidance, row);
    stroke(out, guidance, border);
    const auto gx = guidance.x + 10.f * layout.scale;
    auto gy = guidance.y + 9.f * layout.scale;
    const auto guide = [&](std::string value, Color color = muted) {
      clipped_text(out, {gx, gy, guidance.width - 20.f * layout.scale,
                         19.f * layout.scale}, guidance, std::move(value), color,
                   layout.small_font_pixels);
      gy += 22.f * layout.scale;
    };
    guide("DEVELOP THIS COLONY", bright);
    guide("No modules are active yet. Open Surface to choose a site.");
    guide("Power " + number(view.power_supply, 1) + " / " +
              number(view.power_demand, 1) + "  |  Materials " +
              number(view.stored_industry, 1));
    guide("Workforce " + billions(view.workforce_available_millions) +
              " available  |  Treasury " + view.formatted_treasury);
    if (view.available_buildings.empty()) {
      guide("No eligible surface modules are currently available.", warning);
    } else {
      const auto power = std::ranges::find_if(
          view.available_buildings,
          [](const NativeSurfaceBuildOption &option) { return option.power_supply > 0.; });
      const auto fabricator = std::ranges::find_if(
          view.available_buildings,
          [](const NativeSurfaceBuildOption &option) { return option.type_id == "fabricator"; });
      const auto fabricator_ready = fabricator != view.available_buildings.end() &&
                                    can_authorize_build(view, *fabricator) &&
                                    can_staff_build(view, *fabricator) &&
                                    can_power_build(view, *fabricator);
      const auto choice = view.power_supply + 1e-9 < view.power_demand &&
                              power != view.available_buildings.end()
                          ? power
                          : fabricator_ready ? fabricator
                                             : view.available_buildings.begin();
      const auto authorized = can_authorize_build(view, *choice);
      const auto checks = authorized &&
                          can_staff_build(view, *choice) && can_power_build(view, *choice);
      guide("Available next: " + choice->name, bright);
      guide(choice->formatted_authorization + " authorization  |  " +
                number(choice->industry_cost, 1) + " materials  |  " +
                billions(choice->workforce_required_millions) + " workforce",
            checks ? good : muted);
      guide(authorized ? "Construction uses incoming materials over time."
                       : "More treasury is needed to authorize construction.",
            authorized ? muted : warning);
      if (view.power_supply + 1e-9 < view.power_demand && power != view.available_buildings.end())
        guide("It adds power; review placement on the surface.", muted);
      else if (choice == fabricator)
        guide("It is available for local industry; review placement on the surface.", muted);
      else
        guide("Review its placement requirements on the surface.", muted);
    }
  }
  for (std::size_t index = 0; index < view.construction_sites.size(); ++index) {
    const auto &site = view.construction_sites[index];
    const UiRect original{layout.site_rows.x,
                          layout.site_rows.y + site_scroll +
                              static_cast<float>(index) * 82.f * layout.scale,
                          layout.site_rows.width, 76.f * layout.scale};
    const auto clipped = intersection(original, layout.site_rows);
    if (!clipped) continue;
    fill(out, *clipped, clipped->contains(pointer_) ? hover : row);
    stroke(out, *clipped, border);
    const auto clip = *clipped;
    const auto tx = original.x + 8.f * layout.scale;
    clipped_text(out,
                 {tx, original.y + 7.f * layout.scale,
                  original.width - 16.f * layout.scale, 18.f * layout.scale},
                 clip, site.name, bright, layout.body_font_pixels);
    const auto status = site.complete
                            ? (site.enabled ? "OPERATING" : "SHUT DOWN")
                            : site.construction_stage + "  " +
                                  number(site.progress_fraction * 100.0, 1) +
                                  "%";
    clipped_text(
        out,
        {tx, original.y + 29.f * layout.scale,
         original.width - 16.f * layout.scale, 16.f * layout.scale},
        clip, status,
        site.complete && site.powered && site.staffed ? good : muted,
        layout.small_font_pixels);
    const UiRect track{tx, original.y + 51.f * layout.scale,
                       original.width - 16.f * layout.scale,
                       5.f * layout.scale};
    if (const auto visible_track = intersection(track, clip)) {
      fill(out, *visible_track, {30, 52, 73, 255});
      const UiRect amount{track.x, track.y,
                          progress_width(site.complete ? 1.0
                                                       : site.progress_fraction,
                                         track.width),
                          track.height};
      if (const auto visible_amount = intersection(amount, clip))
        fill(out, *visible_amount, good);
    }
    clipped_text(
        out,
        {tx, original.y + 61.f * layout.scale,
         original.width - 16.f * layout.scale, 13.f * layout.scale},
        clip,
        "Condition " + number(site.condition * 100.0, 0) +
            "%  |  Efficiency " + number(site.efficiency * 100.0, 0) + "%",
        muted, layout.small_font_pixels);
  }
  if (view.resource_outpost) {
    fill(out, layout.freight_notice, row);
    text(out, {layout.freight_notice.x + 8.f * layout.scale, layout.freight_notice.y + 8.f * layout.scale,
        layout.freight_notice.width - 16.f * layout.scale, layout.freight_notice.height - 16.f * layout.scale},
        freight_notice_.empty() ? "FREIGHT COLLECTION\nReview an idle bulk freighter before dispatch. Travel, loading and delivery take time." : freight_notice_,
        muted, layout.small_font_pixels);
  }
  if (freight_preview_) {
    fill(out, layout.surface, {0, 4, 10, 190});
    fill(out, layout.freight_review, panel); stroke(out, layout.freight_review, border);
    text(out, {layout.freight_text.x, layout.freight_review.y + 15.f * layout.scale,
        layout.freight_text.width, 30.f * layout.scale},
        freight_preview_->accepted ? "REVIEW FREIGHT RUN" : "FREIGHT UNAVAILABLE",
        bright, layout.title_font_pixels);
    freight_scroll_ = std::clamp(freight_scroll_,
        std::min(0.f, layout.freight_text.height - freight_content_height(layout)), 0.f);
    clipped_text(out, {layout.freight_text.x, layout.freight_text.y + freight_scroll_,
        layout.freight_text.width, freight_content_height(layout)}, layout.freight_text,
        freight_text_, bright, layout.body_font_pixels);
    if (freight_content_height(layout) > layout.freight_text.height)
      text(out, {layout.freight_text.x, layout.freight_cancel.y - 17.f * layout.scale,
          layout.freight_text.width, 15.f * layout.scale}, "Scroll to read all details", muted,
          layout.small_font_pixels, TextAlign::Right);
    fill(out, layout.freight_cancel, layout.freight_cancel.contains(pointer_) ? hover : row);
    stroke(out, layout.freight_cancel, border);
    text(out, layout.freight_cancel, "CANCEL", bright, layout.body_font_pixels, TextAlign::Center);
    if (freight_preview_->accepted) {
      fill(out, layout.freight_confirm, layout.freight_confirm.contains(pointer_) ? hover : row);
      stroke(out, layout.freight_confirm, good);
      text(out, layout.freight_confirm, "DISPATCH FREIGHTER", bright, layout.body_font_pixels, TextAlign::Center);
    }
  }
}

} // namespace stellar::native_colony_ui
