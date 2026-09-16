#include "native_surface_workspace.hpp"

#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace stellar::native_colony_ui {
namespace {
using namespace stellar::native_colony;
using namespace stellar::native_map;
using stellar::core::surface_area_half_size;
using stellar::core::surface_hub_radius;

constexpr Color panel{6, 16, 29, 252};
constexpr Color inset{8, 24, 40, 250};
constexpr Color row{12, 35, 57, 250};
constexpr Color hover{22, 65, 92, 252};
constexpr Color border{82, 148, 195, 245};
constexpr Color text_color{235, 244, 255, 255};
constexpr Color muted{151, 180, 207, 245};
constexpr Color good{94, 229, 157, 255};
constexpr Color warning{245, 177, 82, 255};
constexpr float palette_pitch = 78.f;

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
// Overlay lines carry no clip; Liang–Barsky against the terrain rect.
void clipped_line(DrawList &out, Point a, Point b, Color color,
                  const UiRect &clip) {
  const auto dx = b.x - a.x, dy = b.y - a.y;
  float t0 = 0.f, t1 = 1.f;
  const auto clip_edge = [&](float p, float q) {
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
  if (!clip_edge(-dx, a.x - clip.x) ||
      !clip_edge(dx, clip.x + clip.width - a.x) ||
      !clip_edge(-dy, a.y - clip.y) ||
      !clip_edge(dy, clip.y + clip.height - a.y))
    return;
  if (t0 >= t1) return;
  out.overlay.emplace_back(Line{{a.x + t0 * dx, a.y + t0 * dy},
                                {a.x + t1 * dx, a.y + t1 * dy}, color});
}
void clipped_ring(DrawList &out, Point center, float radius, Color color,
                  const UiRect &clip) {
  constexpr int segments = 48;
  Point previous{center.x + radius, center.y};
  for (int i = 1; i <= segments; ++i) {
    const auto angle =
        6.283185307179586f * static_cast<float>(i) / segments;
    const Point next{center.x + std::cos(angle) * radius,
                     center.y + std::sin(angle) * radius};
    clipped_line(out, previous, next, color, clip);
    previous = next;
  }
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
[[nodiscard]] std::optional<UiRect> intersection(UiRect left,
                                                 UiRect right) noexcept {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto right_edge = std::min(left.x + left.width, right.x + right.width);
  const auto bottom = std::min(left.y + left.height, right.y + right.height);
  if (right_edge <= x || bottom <= y) return std::nullopt;
  return UiRect{x, y, right_edge - x, bottom - y};
}
[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}
[[nodiscard]] float safe_progress(double value, float width) noexcept {
  return static_cast<float>(std::isfinite(value) ? std::clamp(value, 0., 1.)
                                                 : 0.) *
         width;
}
} // namespace

SurfaceWorkspaceLayout SurfaceWorkspaceLayout::for_viewport(
    const int width, const int height) noexcept {
  const auto w = static_cast<float>(width), h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1180.f, h / 680.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 12.f * scale, top = 60.f * scale;
  const UiRect surface{margin, top, std::max(1.f, w - 2.f * margin),
                       std::max(1.f, h - top - margin)};
  const auto header = 48.f * scale, gap = 9.f * scale;
  const auto palette_width = std::min(std::max(246.f * scale, 190.f),
                                      surface.width * .27f);
  const auto inspector_width = std::min(std::max(292.f * scale, 225.f),
                                        surface.width * .31f);
  const auto content_y = surface.y + header;
  const auto content_h = surface.height - header;
  const UiRect palette{surface.x, content_y, palette_width, content_h};
  const UiRect inspector{surface.x + surface.width - inspector_width,
                         content_y, inspector_width, content_h};
  const UiRect terrain{palette.x + palette.width + gap, content_y,
                       std::max(1.f, inspector.x - gap -
                                        (palette.x + palette.width + gap)),
                       content_h};
  const auto modal_w = std::min(540.f * scale, w - 30.f * scale);
  const auto modal_h = 286.f * scale;
  const UiRect confirmation{(w - modal_w) * .5f, (h - modal_h) * .5f,
                            modal_w, modal_h};
  return {scale,
          static_cast<int>(std::lround(22.f * scale)),
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(12.f * scale)),
          surface,
          {surface.x + 8.f * scale, surface.y + 8.f * scale, 92.f * scale,
           32.f * scale},
          {surface.x + 112.f * scale, surface.y + 10.f * scale,
           surface.width - 124.f * scale, 30.f * scale},
          palette,
          {palette.x + 8.f * scale, palette.y + 38.f * scale,
           palette.width - 16.f * scale, palette.height - 46.f * scale},
          terrain,
          inspector,
          {inspector.x + 10.f * scale,
           inspector.y + inspector.height - 86.f * scale,
           inspector.width - 20.f * scale, 32.f * scale},
          {inspector.x + 10.f * scale,
           inspector.y + inspector.height - 45.f * scale,
           inspector.width - 20.f * scale, 34.f * scale},
          confirmation,
          {confirmation.x + confirmation.width - 174.f * scale,
           confirmation.y + confirmation.height - 48.f * scale,
           154.f * scale, 32.f * scale},
          {confirmation.x + 20.f * scale,
           confirmation.y + confirmation.height - 48.f * scale,
           118.f * scale, 32.f * scale},
          {inspector.x + 10.f * scale,
           inspector.y + inspector.height - 141.f * scale,
           (inspector.width - 26.f * scale) * .5f, 34.f * scale},
          {inspector.x + 16.f * scale +
               (inspector.width - 26.f * scale) * .5f,
           inspector.y + inspector.height - 141.f * scale,
           (inspector.width - 26.f * scale) * .5f, 34.f * scale},
          {inspector.x + 10.f * scale,
           inspector.y + inspector.height - 101.f * scale,
           (inspector.width - 26.f * scale) * .5f, 34.f * scale},
          {inspector.x + 16.f * scale +
               (inspector.width - 26.f * scale) * .5f,
           inspector.y + inspector.height - 101.f * scale,
           (inspector.width - 26.f * scale) * .5f, 34.f * scale},
          {inspector.x + 10.f * scale,
           inspector.y + inspector.height - 45.f * scale,
           inspector.width - 20.f * scale, 34.f * scale}};
}

Point SurfaceViewport::world_to_screen(const double x, const double z,
                                       const UiRect terrain) const noexcept {
  return {static_cast<float>(terrain.x + terrain.width * .5 +
                             (x - center_x) * pixels_per_unit),
          static_cast<float>(terrain.y + terrain.height * .5 +
                             (z - center_z) * pixels_per_unit)};
}

std::pair<double, double> SurfaceViewport::screen_to_world(
    const Point point, const UiRect terrain) const noexcept {
  return {center_x + (point.x - terrain.x - terrain.width * .5) /
                         pixels_per_unit,
          center_z + (point.y - terrain.y - terrain.height * .5) /
                         pixels_per_unit};
}

SurfaceViewport SurfaceViewport::translated(const float dx,
                                             const float dy) const noexcept {
  auto result = *this;
  result.center_x -= static_cast<double>(dx) / pixels_per_unit;
  result.center_z -= static_cast<double>(dy) / pixels_per_unit;
  return result;
}

SurfaceViewport SurfaceViewport::zoomed_at(const float factor,
                                           const Point anchor,
                                           const UiRect terrain,
                                           const double minimum,
                                           const double maximum) const noexcept {
  const auto before = screen_to_world(anchor, terrain);
  auto result = *this;
  result.pixels_per_unit =
      std::clamp(pixels_per_unit * static_cast<double>(factor), minimum,
                 maximum);
  const auto after = result.screen_to_world(anchor, terrain);
  result.center_x += before.first - after.first;
  result.center_z += before.second - after.second;
  return result;
}

void NativeSurfaceWorkspace::fit(const int width, const int height) noexcept {
  const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
  viewport_.center_x = 0.;
  viewport_.center_z = 0.;
  viewport_.pixels_per_unit =
      .9 * std::min(static_cast<double>(layout.terrain.width),
                    static_cast<double>(layout.terrain.height)) /
      (2. * surface_area_half_size);
}

void NativeSurfaceWorkspace::open(NativeColonyView view, const int width,
                                  const int height) {
  visible_ = true;
  view_ = std::move(view);
  selected_type_id_.reset();
  selected_building_id_.reset();
  placement_quote_.reset();
  removal_quote_.reset();
  confirmation_ = std::monostate{};
  notice_.clear();
  palette_scroll_ = 0.f;
  rotation_degrees_ = 0.f;
  pending_preview_.reset();
  pressed_ = false;
  dragging_ = false;
  last_preview_position_.reset();
  fit(width, height);
}

void NativeSurfaceWorkspace::set_view(NativeColonyView view) {
  const auto changed = !view_ ||
                       view_->campaign_generation != view.campaign_generation ||
                       view_->revision != view.revision;
  view_ = std::move(view);
  if (changed) {
    placement_quote_.reset();
    removal_quote_.reset();
    confirmation_ = std::monostate{};
    last_preview_position_.reset();
    pending_preview_.reset();
  }
  reconcile();
}

void NativeSurfaceWorkspace::reconcile() {
  if (!view_) return;
  if (selected_type_id_ &&
      std::ranges::none_of(view_->available_buildings, [&](const auto &item) {
        return item.type_id == *selected_type_id_;
      }))
    selected_type_id_.reset();
  if (selected_building_id_ &&
      std::ranges::none_of(view_->construction_sites, [&](const auto &item) {
        return item.building_id == *selected_building_id_;
      }))
    selected_building_id_.reset();
}

void NativeSurfaceWorkspace::close() noexcept {
  visible_ = false;
  pressed_ = false;
  dragging_ = false;
  placement_quote_.reset();
  removal_quote_.reset();
  confirmation_ = std::monostate{};
  pending_preview_.reset();
  last_preview_position_.reset();
}

void NativeSurfaceWorkspace::discard_campaign() noexcept {
  visible_ = false;
  view_.reset();
  selected_type_id_.reset();
  selected_building_id_.reset();
  placement_quote_.reset();
  removal_quote_.reset();
  confirmation_ = std::monostate{};
  pending_preview_.reset();
  pressed_ = false;
  dragging_ = false;
  last_preview_position_.reset();
  notice_.clear();
}

void NativeSurfaceWorkspace::set_placement_quote(
    NativeSurfacePlacementQuote quote, const bool open_confirmation) {
  notice_ = quote.message;
  placement_quote_ = std::move(quote);
  if (open_confirmation) {
    pending_preview_.reset();
    last_preview_position_.reset();
    confirmation_ = *placement_quote_;
  }
}

void NativeSurfaceWorkspace::set_removal_quote(
    NativeSurfaceRemovalQuote quote) {
  notice_ = quote.message;
  removal_quote_ = std::move(quote);
  confirmation_ = *removal_quote_;
}

void NativeSurfaceWorkspace::complete_command(std::string notice) {
  placement_quote_.reset();
  removal_quote_.reset();
  confirmation_ = std::monostate{};
  last_preview_position_.reset();
  pending_preview_.reset();
  selected_type_id_.reset();
  selected_building_id_.reset();
  notice_ = std::move(notice);
}

std::optional<std::size_t> NativeSurfaceWorkspace::palette_hit(
    const Point point, const SurfaceWorkspaceLayout &layout) const noexcept {
  if (!view_ || !layout.palette_rows.contains(point)) return std::nullopt;
  const auto local = point.y - layout.palette_rows.y - palette_scroll_;
  if (local < 0.f) return std::nullopt;
  const auto index = static_cast<std::size_t>(
      std::floor(local / (palette_pitch * layout.scale)));
  return index < view_->available_buildings.size()
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

std::optional<int> NativeSurfaceWorkspace::site_hit(
    const Point point, const SurfaceWorkspaceLayout &layout) const noexcept {
  if (!view_ || !layout.terrain.contains(point)) return std::nullopt;
  for (auto it = view_->construction_sites.rbegin();
       it != view_->construction_sites.rend(); ++it) {
    const auto center = viewport_.world_to_screen(it->x, it->z, layout.terrain);
    const auto footprint = std::max(
        14.f, 34.f * static_cast<float>(viewport_.pixels_per_unit));
    // The sprite rises above the anchor while the ground plot stays below it;
    // cover both so clicks on either select the site.
    const auto side = footprint * 1.55f;
    const UiRect bounds{center.x - side * .5f, center.y - side * .8f, side,
                        side * .8f + std::max(footprint * .5f, 14.f)};
    const auto visible=intersection(bounds,layout.terrain);
    if (visible&&visible->contains(point))
      return it->building_id;
  }
  return std::nullopt;
}

SurfaceWorkspaceCommand NativeSurfaceWorkspace::placement_request(
    const Point point, const SurfaceWorkspaceLayout &layout,
    const bool confirm) {
  if (!selected_type_id_ || !layout.terrain.contains(point))
    return {SurfaceWorkspaceCommandKind::None, true};
  const auto world = viewport_.screen_to_world(point, layout.terrain);
  const auto x = static_cast<float>(world.first), z = static_cast<float>(world.second);
  last_preview_position_ = std::pair{x, z};
  return {SurfaceWorkspaceCommandKind::PreviewPlacement, true, confirm,
          *selected_type_id_, 0, x, z, rotation_degrees_};
}

std::optional<SurfaceWorkspaceCommand>
NativeSurfaceWorkspace::take_preview_request() noexcept {
  auto result = std::move(pending_preview_);
  pending_preview_.reset();
  return result;
}

SurfaceWorkspaceCommand NativeSurfaceWorkspace::handle(
    const InputEvent &event, const int width, const int height) {
  if (!visible_ || !view_) return {};
  pointer_ = event.position;
  const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
  if (!std::holds_alternative<std::monostate>(confirmation_)) {
    const auto revision = std::visit(
        [](const auto &value) -> std::uint64_t {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) return 0;
          else return value.quote_revision;
        }, confirmation_);
    if (event.type == InputEventType::EscapePressed ||
        (event.type == InputEventType::LeftPressed &&
         layout.cancel.contains(event.position))) {
      confirmation_ = std::monostate{};
      placement_quote_.reset();
      removal_quote_.reset();
      last_preview_position_.reset();
      pending_preview_.reset();
      return {SurfaceWorkspaceCommandKind::CancelQuote, true, false, {}, 0,
              0, 0, 0, revision};
    }
    if (event.type == InputEventType::LeftPressed &&
        layout.confirm.contains(event.position)) {
      const auto accepted = std::visit(
          [](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) return false;
            else return value.accepted;
          }, confirmation_);
      if (!accepted) return {SurfaceWorkspaceCommandKind::None, true};
      const auto kind = std::holds_alternative<NativeSurfacePlacementQuote>(confirmation_)
                            ? SurfaceWorkspaceCommandKind::ConfirmPlacement
                            : SurfaceWorkspaceCommandKind::ConfirmRemoval;
      return {kind, true, false, {}, 0, 0, 0, 0, revision};
    }
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::EscapePressed) {
    pending_preview_.reset();
    last_preview_position_.reset();
    pressed_ = false;
    dragging_ = false;
    if (placement_quote_) {
      const auto revision = placement_quote_->quote_revision;
      placement_quote_.reset();
      selected_type_id_.reset();
      return {SurfaceWorkspaceCommandKind::CancelQuote, true, false, {}, 0,
              0, 0, 0, revision};
    }
    if (selected_type_id_) {
      selected_type_id_.reset();
      last_preview_position_.reset();
      return {SurfaceWorkspaceCommandKind::None, true};
    }
    close();
    return {SurfaceWorkspaceCommandKind::Close, true};
  }
  if (event.type == InputEventType::LeftPressed &&
      layout.back.contains(event.position)) {
    close();
    return {SurfaceWorkspaceCommandKind::Close, true};
  }
  if (event.type == InputEventType::Wheel &&
      layout.palette.contains(event.position)) {
    const auto content = static_cast<float>(view_->available_buildings.size()) *
                         palette_pitch * layout.scale;
    palette_scroll_ = std::clamp(
        palette_scroll_ + event.wheel_y * 42.f * layout.scale,
        std::min(0.f, layout.palette_rows.height - content), 0.f);
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::Wheel &&
      layout.terrain.contains(event.position)) {
    viewport_ = viewport_.zoomed_at(std::pow(1.16f, event.wheel_y),
                                    event.position, layout.terrain, .08, 4.0);
    placement_quote_.reset();
    pending_preview_.reset();
    last_preview_position_.reset();
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::LeftPressed) {
    if (const auto index = palette_hit(event.position, layout)) {
      selected_type_id_ = view_->available_buildings[*index].type_id;
      selected_building_id_.reset();
      placement_quote_.reset();
      last_preview_position_.reset();
      pending_preview_.reset();
      return {SurfaceWorkspaceCommandKind::None, true};
    }
    if (layout.rotate.contains(event.position) && selected_type_id_) {
      rotation_degrees_ = std::fmod(rotation_degrees_ + 90.f, 360.f);
      last_preview_position_.reset();
      placement_quote_.reset();
      pending_preview_.reset();
      return {SurfaceWorkspaceCommandKind::None, true};
    }
    if (selected_building_id_) {
      const auto site = std::ranges::find(view_->construction_sites,
                                          *selected_building_id_,
                                          &NativeSurfaceSite::building_id);
      if (site != view_->construction_sites.end()) {
        if (layout.upgrade.contains(event.position) && site->can_upgrade &&
            site->can_afford_upgrade && site->upgrade_lock_reason.empty())
          return {SurfaceWorkspaceCommandKind::UpgradeBuilding, true, false,
                  {}, *selected_building_id_};
        if (layout.repair.contains(event.position) &&
            site->can_afford_repair && site->condition < .999999)
          return {SurfaceWorkspaceCommandKind::RepairBuilding, true, false,
                  {}, *selected_building_id_};
        if (layout.toggle_operation.contains(event.position))
          return {SurfaceWorkspaceCommandKind::SetBuildingEnabled, true,
                  false, {}, *selected_building_id_, {}, {}, {}, {},
                  !site->enabled};
        if (layout.priority.contains(event.position))
          return {SurfaceWorkspaceCommandKind::SetBuildingPriority, true,
                  false, {}, *selected_building_id_, {}, {}, {}, {},
                  !site->prioritized};
      }
    }
    if (layout.hub_upgrade.contains(event.position) && !selected_building_id_ &&
        !selected_type_id_ && view_->hub_upgrade_available &&
        view_->can_afford_hub_upgrade)
      return {SurfaceWorkspaceCommandKind::UpgradeHub, true};
    if (layout.remove.contains(event.position) && selected_building_id_)
      return {SurfaceWorkspaceCommandKind::PreviewRemoval, true, false, {},
              *selected_building_id_};
    if (layout.terrain.contains(event.position)) {
      press_ = event.position;
      pressed_ = true;
      dragging_ = false;
      return {SurfaceWorkspaceCommandKind::None, true};
    }
    return {SurfaceWorkspaceCommandKind::None,
            layout.surface.contains(event.position)};
  }
  if (event.type == InputEventType::PointerMove &&
      layout.terrain.contains(event.position)) {
    if (pressed_ && std::hypot(event.position.x - press_.x, event.position.y - press_.y) >
        5.f * layout.scale &&
        (event.delta.x != 0.f || event.delta.y != 0.f))
      dragging_ = true;
    if (dragging_) {
      viewport_ = viewport_.translated(event.delta.x, event.delta.y);
      placement_quote_.reset();
      pending_preview_.reset();
      last_preview_position_.reset();
    } else if (selected_type_id_) {
      const auto world = viewport_.screen_to_world(event.position, layout.terrain);
      const auto x = static_cast<float>(world.first), z = static_cast<float>(world.second);
      if (!last_preview_position_ ||
          std::abs(last_preview_position_->first - x) > .25f ||
          std::abs(last_preview_position_->second - z) > .25f)
        pending_preview_ = placement_request(event.position, layout, false);
    }
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::LeftReleased) {
    if (!pressed_) return {SurfaceWorkspaceCommandKind::None,
                           layout.surface.contains(event.position)};
    pressed_ = false;
    if (dragging_) {
      dragging_ = false;
      return {SurfaceWorkspaceCommandKind::None, true};
    }
    if (!layout.terrain.contains(event.position))
      return {SurfaceWorkspaceCommandKind::None, true};
    if (selected_type_id_) return placement_request(event.position, layout, true);
    selected_building_id_ = site_hit(event.position, layout);
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::PointerCancelled) {
    pressed_ = false;
    dragging_ = false;
    pending_preview_.reset();
    return {SurfaceWorkspaceCommandKind::None, true};
  }
  return {SurfaceWorkspaceCommandKind::None,
          layout.surface.contains(event.position)};
}

void NativeSurfaceWorkspace::render(DrawList &out, const int width,
                                    const int height) const {
  if (!visible_ || !view_) return;
  const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
  const auto &view = *view_;
  fill(out, layout.surface, panel);
  stroke(out, layout.surface, border);
  fill(out, layout.back, layout.back.contains(pointer_) ? hover : row);
  stroke(out, layout.back, border);
  text(out, layout.back, "BACK", text_color, layout.small_font,
       TextAlign::Center);
  text(out, layout.title,
       view.colony_name + "  /  OPERATIONAL SURFACE", text_color,
       layout.heading_font);

  fill(out, layout.palette, inset);
  stroke(out, layout.palette, border);
  text(out, {layout.palette.x + 10.f * layout.scale,
             layout.palette.y + 9.f * layout.scale,
             layout.palette.width - 20.f * layout.scale, 24.f * layout.scale},
       "BUILDING PALETTE", muted, layout.body_font);
  for (std::size_t index = 0; index < view.available_buildings.size(); ++index) {
    const auto &option = view.available_buildings[index];
    const UiRect original{layout.palette_rows.x,
                          layout.palette_rows.y + palette_scroll_ +
                              static_cast<float>(index) * palette_pitch *
                                  layout.scale,
                          layout.palette_rows.width,
                          (palette_pitch - 6.f) * layout.scale};
    const auto visible = intersection(original, layout.palette_rows);
    if (!visible) continue;
    const auto selected = selected_type_id_ &&
                          *selected_type_id_ == option.type_id;
    fill(out, *visible, selected ? hover : row);
    stroke(out, *visible, selected ? good : border);
    clipped_text(out, {original.x + 8.f * layout.scale,
                       original.y + 7.f * layout.scale,
                       original.width - 16.f * layout.scale,
                       20.f * layout.scale},
                 *visible, option.name, text_color, layout.body_font);
    clipped_text(out, {original.x + 8.f * layout.scale,
                       original.y + 31.f * layout.scale,
                       original.width - 16.f * layout.scale,
                       34.f * layout.scale},
                 *visible,
                 option.formatted_authorization + "  |  Industry " +
                     number(option.industry_cost, 0),
                 muted, layout.small_font);
  }

  const auto terrain_tint = [&] {
    using native_system::NativeSystemBodyVisualClass;
    switch (view.surface_visual_class) {
      case NativeSystemBodyVisualClass::rocky: return Color{38, 30, 24, 255};
      case NativeSystemBodyVisualClass::oceanic: return Color{10, 32, 44, 255};
      case NativeSystemBodyVisualClass::frozen: return Color{28, 44, 54, 255};
      case NativeSystemBodyVisualClass::hot_rocky: return Color{48, 24, 18, 255};
      case NativeSystemBodyVisualClass::moon: return Color{30, 30, 34, 255};
      case NativeSystemBodyVisualClass::unknown_moon:
        return Color{22, 30, 38, 255};
      default: return Color{3, 18, 20, 255};
    }
  }();
  fill(out, layout.terrain, terrain_tint);
  stroke(out, layout.terrain, border);
  const auto world_min = viewport_.world_to_screen(-surface_area_half_size,
                                                    -surface_area_half_size,
                                                    layout.terrain);
  const auto world_max = viewport_.world_to_screen(surface_area_half_size,
                                                    surface_area_half_size,
                                                    layout.terrain);
  if (const auto visible = intersection(
          {std::min(world_min.x, world_max.x),
           std::min(world_min.y, world_max.y), std::abs(world_max.x - world_min.x),
           std::abs(world_max.y - world_min.y)}, layout.terrain))
    stroke(out, *visible, {36, 91, 76, 220});
  for (int coordinate = -400; coordinate <= 400; coordinate += 100) {
    const auto a = viewport_.world_to_screen(coordinate, -surface_area_half_size,
                                             layout.terrain);
    const auto b = viewport_.world_to_screen(coordinate, surface_area_half_size,
                                             layout.terrain);
    if (a.x >= layout.terrain.x && a.x <= layout.terrain.x + layout.terrain.width)
      out.overlay.emplace_back(FilledRectangle{
          {a.x, layout.terrain.y, 1.f, layout.terrain.height},
          {20, 55, 51, 120}});
    const auto c = viewport_.world_to_screen(-surface_area_half_size, coordinate,
                                             layout.terrain);
    if (c.y >= layout.terrain.y && c.y <= layout.terrain.y + layout.terrain.height)
      out.overlay.emplace_back(FilledRectangle{
          {layout.terrain.x, c.y, layout.terrain.width, 1.f},
          {20, 55, 51, 120}});
    (void)b;
  }
  const auto hub = viewport_.world_to_screen(0., 0., layout.terrain);
  const auto hub_radius = static_cast<float>(surface_hub_radius *
                                              viewport_.pixels_per_unit);
  // Reference street layout: a civic ring road around the hub and a connector
  // from each established site back to the plaza.
  if (hub.x >= layout.terrain.x - hub_radius &&
      hub.x <= layout.terrain.x + layout.terrain.width + hub_radius &&
      hub.y >= layout.terrain.y - hub_radius &&
      hub.y <= layout.terrain.y + layout.terrain.height + hub_radius)
    clipped_ring(out, hub, hub_radius * 1.25f, {44, 68, 66, 190},
                 layout.terrain);
  for (const auto &site : view.construction_sites) {
    if (!site.complete) continue;
    const auto end =
        viewport_.world_to_screen(site.x, site.z, layout.terrain);
    clipped_line(out, hub, end, {44, 68, 66, 170}, layout.terrain);
  }
  if (const auto hub_sprite =
          scene_.hub_image(view.surface_hub_level, false, view.resource_outpost)) {
    const auto side = std::max(10.f, hub_radius * 3.4f);
    out.overlay.emplace_back(
        Image{hub_sprite,
              {hub.x - side * .5f, hub.y - side * .82f, side, side},
              std::nullopt, {255, 255, 255, 255}, layout.terrain});
  }
  // Buildings draw south-to-north so taller sprites overlap correctly.
  std::vector<const NativeSurfaceSite *> ordered;
  ordered.reserve(view.construction_sites.size());
  for (const auto &site : view.construction_sites) ordered.push_back(&site);
  std::ranges::sort(ordered, {}, &NativeSurfaceSite::z);
  for (const auto *site : ordered) {
    const auto center =
        viewport_.world_to_screen(site->x, site->z, layout.terrain);
    const auto footprint = std::max(
        14.f, 34.f * static_cast<float>(viewport_.pixels_per_unit));
    const auto side = footprint * 1.55f;
    const UiRect bounds{center.x - footprint * .5f,
                        center.y - footprint * .5f, footprint, footprint};
    if (!intersection(bounds, layout.terrain)) continue;
    const auto phase = site->complete
                           ? 3
                           : native_surface::NativeSurfaceSceneRenderer::
                                 phase_for_progress(site->progress_fraction);
    if (const auto sprite = scene_.image(site->type_id, phase, site->powered,
                                         site->prioritized))
      out.overlay.emplace_back(
          Image{sprite,
                {center.x - side * .5f, center.y - side * .8f, side, side},
                std::nullopt, {255, 255, 255, 255}, layout.terrain});
    stroke(out, bounds,
           selected_building_id_ && *selected_building_id_ == site->building_id
               ? Color{245, 221, 114, 255}
               : Color{82, 148, 195, 130});
    if (!site->complete) {
      const UiRect bar{bounds.x, bounds.y + bounds.height - 4.f,
                       bounds.width, 3.f};
      if (const auto track = intersection(bar, layout.terrain))
        fill(out, *track, {22, 31, 39, 255});
      if (const auto progress = intersection(
              {bar.x, bar.y, safe_progress(site->progress_fraction, bar.width),
               bar.height}, layout.terrain))
        fill(out, *progress, good);
    }
  }
  if (selected_type_id_ && placement_quote_) {
    const auto option = std::ranges::find(view.available_buildings,
                                          *selected_type_id_,
                                          &NativeSurfaceBuildOption::type_id);
    if (option != view.available_buildings.end()) {
      const auto center = viewport_.world_to_screen(
          placement_quote_->x, placement_quote_->z, layout.terrain);
      const auto radius = std::max(6.f, option->footprint_radius *
                                            static_cast<float>(viewport_.pixels_per_unit));
      if (const auto ghost =
              scene_.image(option->type_id, 2, true, false)) {
        const auto side = radius * 2.f * 1.55f;
        out.overlay.emplace_back(
            Image{ghost,
                  {center.x - side * .5f, center.y - side * .8f, side, side},
                  std::nullopt, {255, 255, 255, 110}, layout.terrain});
      }
      if (const auto visible = intersection(
              {center.x - radius, center.y - radius, 2.f * radius,
               2.f * radius}, layout.terrain)) {
        fill(out, *visible, placement_quote_->accepted
                                ? Color{48, 177, 112, 90}
                                : Color{224, 75, 67, 95});
        stroke(out, *visible, placement_quote_->accepted ? good
                                                          : Color{246, 103, 90, 255});
      }
    }
  }

  fill(out, layout.inspector, inset);
  stroke(out, layout.inspector, border);
  const auto ix = layout.inspector.x + 10.f * layout.scale;
  auto iy = layout.inspector.y + 10.f * layout.scale;
  const auto iw = layout.inspector.width - 20.f * layout.scale;
  const auto add = [&](std::string value, Color color, int pixels,
                       float step) {
    text(out, {ix, iy, iw, step * layout.scale}, std::move(value), color,
         pixels);
    iy += step * layout.scale;
  };
  add("SURFACE INSPECTOR", muted, layout.body_font, 30.f);
  if (selected_type_id_) {
    const auto option = std::ranges::find(view.available_buildings,
                                          *selected_type_id_,
                                          &NativeSurfaceBuildOption::type_id);
    if (option != view.available_buildings.end()) {
      add(option->name, text_color, layout.body_font, 25.f);
      add(option->description, muted, layout.small_font, 54.f);
      add("Authorization  " + option->formatted_authorization, text_color,
          layout.small_font, 23.f);
      add("Industry  " + number(option->industry_cost, 0), text_color,
          layout.small_font, 23.f);
      add("Workforce  " + number(option->workforce_required_millions, 2) +
              "M",
          muted, layout.small_font, 22.f);
      add("Power supply/demand  " + number(option->power_supply, 1) + " / " +
              number(option->power_demand, 1),
          muted, layout.small_font, 22.f);
      add("Footprint radius  " + number(option->footprint_radius, 0), muted,
          layout.small_font, 22.f);
      if (placement_quote_)
        add(placement_quote_->message,
            placement_quote_->accepted ? good : warning,
            layout.small_font, 56.f);
      fill(out, layout.rotate,
           layout.rotate.contains(pointer_) ? hover : row);
      stroke(out, layout.rotate, border);
      text(out, layout.rotate,
           "ROTATE  " + number(rotation_degrees_, 0) + " deg", text_color,
           layout.small_font, TextAlign::Center);
    }
  } else if (selected_building_id_) {
    const auto site = std::ranges::find(view.construction_sites,
                                        *selected_building_id_,
                                        &NativeSurfaceSite::building_id);
    if (site != view.construction_sites.end()) {
      add(site->name, text_color, layout.body_font, 26.f);
      if (site->upgrade_days_remaining > 0.)
        add("Upgrading  " + number(site->upgrade_days_remaining, 1) +
                " game days remaining at full funding",
            warning, layout.small_font, 23.f);
      else
        add(site->complete ? "Operational module" : site->construction_stage,
            site->complete ? good : warning, layout.small_font, 23.f);
      add("Position  " + number(site->x, 1) + ", " + number(site->z, 1),
          muted, layout.small_font, 22.f);
      add("Rotation  " + number(site->rotation_degrees, 0) + " deg", muted,
          layout.small_font, 22.f);
      add("Construction  " + number(site->progress_fraction * 100., 1) + "%",
          text_color, layout.small_font, 22.f);
      if (!site->complete)
        add("Materials remaining  " +
                number(site->remaining_construction_materials, 1),
            muted, layout.small_font, 22.f);
      else {
        add("Condition  " + number(site->condition * 100., 0) + "%" +
                "  ·  Efficiency  " + number(site->efficiency * 100., 0) + "%",
            site->condition <= .500001 ? warning : text_color,
            layout.small_font, 22.f);
        add(std::string(site->powered ? "Powered" : "Unpowered") + "  ·  " +
                (site->staffed ? "Staffed" : "Understaffed") + "  ·  " +
                (site->enabled ? "Operating" : "Shut down") +
                (site->prioritized ? "  ·  Priority" : "") +
                (site->essential_service ? "  ·  Essential" : ""),
            muted, layout.small_font, 22.f);
      }
      if (!site->upgrade_lock_reason.empty())
        add(site->upgrade_lock_reason, warning, layout.small_font, 44.f);
      else if (site->can_upgrade && !site->can_afford_upgrade)
        add(site->upgrade_name + " requires " +
                view.currency.format(site->upgrade_credit_budget_units) +
                " and " + number(site->upgrade_industry_cost, 0) +
                " available materials.",
            muted, layout.small_font, 44.f);
      const auto action = [&](const stellar::native_map::UiRect &rect,
                              std::string_view label, const bool enabled) {
        fill(out, rect,
             !enabled ? inset
                      : rect.contains(pointer_) ? hover : row);
        stroke(out, rect, enabled ? border : inset);
        text(out, rect, std::string(label),
             enabled ? text_color : muted, layout.small_font,
             TextAlign::Center);
      };
      if (site->can_upgrade)
        action(layout.upgrade,
               "UPGRADE" +
                   std::string(site->upgrade_name.empty()
                                   ? ""
                                   : "  " + site->upgrade_name),
               site->can_afford_upgrade && site->upgrade_lock_reason.empty());
      action(layout.repair,
             "REPAIR  " + number(site->repair_industry_cost, 0) + " MAT",
             site->can_afford_repair && site->condition < .999999);
      action(layout.toggle_operation,
             site->enabled ? "SHUT DOWN" : "RESTART", true);
      action(layout.priority,
             site->prioritized ? "NORMAL PRIORITY" : "PRIORITIZE", true);
      fill(out, layout.remove,
           layout.remove.contains(pointer_) ? hover : row);
      stroke(out, layout.remove, warning);
      text(out, layout.remove,
           site->complete ? "REVIEW DEMOLITION" : "REVIEW CANCELLATION",
           text_color, layout.small_font, TextAlign::Center);
    }
  } else {
    add("Choose a building or select an existing site.", muted,
        layout.body_font, 48.f);
    add("Drag to pan. Wheel zooms at the pointer.", muted,
        layout.small_font, 40.f);
    if (!view.hub_name.empty()) {
      add(view.hub_name + "  ·  level " +
              std::to_string(view.surface_hub_level),
          text_color, layout.small_font, 24.f);
      if (view.hub_upgrade_days_remaining > 0.)
        add("Upgrading  " + number(view.hub_upgrade_days_remaining, 1) +
                " game days remaining",
            warning, layout.small_font, 22.f);
      else if (!view.hub_upgrade_lock_reason.empty())
        add(view.hub_upgrade_lock_reason, warning, layout.small_font, 44.f);
      else if (view.hub_upgrade_available && !view.can_afford_hub_upgrade)
        add("Upgrade requires " +
                view.currency.format(view.hub_upgrade_credit_budget_units) +
                " and " + number(view.hub_upgrade_industry_cost, 0) +
                " available materials.",
            muted, layout.small_font, 44.f);
      if (view.hub_upgrade_available &&
          view.hub_upgrade_days_remaining <= 0.) {
        fill(out, layout.hub_upgrade,
             !view.can_afford_hub_upgrade
                 ? inset
                 : layout.hub_upgrade.contains(pointer_) ? hover : row);
        stroke(out, layout.hub_upgrade,
               view.can_afford_hub_upgrade ? good : inset);
        text(out, layout.hub_upgrade,
             "UPGRADE " + view.hub_name,
             view.can_afford_hub_upgrade ? text_color : muted,
             layout.small_font, TextAlign::Center);
      }
    }
  }
  if (!notice_.empty())
    text(out, {ix, layout.inspector.y + layout.inspector.height -
                       196.f * layout.scale,
               iw, 44.f * layout.scale},
         notice_, warning, layout.small_font);

  if (!std::holds_alternative<std::monostate>(confirmation_)) {
    fill(out, {0, 0, static_cast<float>(width), static_cast<float>(height)},
         {0, 0, 0, 168});
    fill(out, layout.confirmation, panel);
    stroke(out, layout.confirmation, warning);
    const auto cx = layout.confirmation.x + 18.f * layout.scale;
    auto cy = layout.confirmation.y + 17.f * layout.scale;
    const auto cw = layout.confirmation.width - 36.f * layout.scale;
    if (const auto *quote =
            std::get_if<NativeSurfacePlacementQuote>(&confirmation_)) {
      text(out, {cx, cy, cw, 28.f * layout.scale}, "CONFIRM CONSTRUCTION",
           text_color, layout.heading_font);
      cy += 38.f * layout.scale;
      text(out, {cx, cy, cw, 27.f * layout.scale}, quote->building_name,
           text_color, layout.body_font);
      cy += 31.f * layout.scale;
      text(out, {cx, cy, cw, 23.f * layout.scale},
           "Authorization  " + quote->formatted_authorization, muted,
           layout.body_font);
      cy += 27.f * layout.scale;
      text(out, {cx, cy, cw, 23.f * layout.scale},
           "Industry requirement  " + number(quote->industry_cost, 0), muted,
           layout.body_font);
      cy += 27.f * layout.scale;
      text(out, {cx, cy, cw, 48.f * layout.scale}, quote->message,
           quote->accepted ? good : warning, layout.small_font);
    } else if (const auto *removal =
                   std::get_if<NativeSurfaceRemovalQuote>(&confirmation_)) {
      text(out, {cx, cy, cw, 28.f * layout.scale},
           removal->cancellation ? "CANCEL CONSTRUCTION" : "CONFIRM DEMOLITION",
           text_color, layout.heading_font);
      cy += 40.f * layout.scale;
      text(out, {cx, cy, cw, 26.f * layout.scale}, removal->building_name,
           text_color, layout.body_font);
      cy += 32.f * layout.scale;
      text(out, {cx, cy, cw, 25.f * layout.scale},
           removal->cancellation
               ? "Authorization refund  " + removal->formatted_refund
               : "Completed demolition has no authorization refund.",
           muted, layout.body_font);
      cy += 34.f * layout.scale;
      text(out, {cx, cy, cw, 54.f * layout.scale}, removal->message,
           removal->accepted ? good : warning, layout.small_font);
    }
    fill(out, layout.cancel,
         layout.cancel.contains(pointer_) ? hover : row);
    stroke(out, layout.cancel, border);
    text(out, layout.cancel, "BACK", text_color, layout.small_font,
         TextAlign::Center);
    const auto accepted = std::visit(
        [](const auto &value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, std::monostate>) return false;
          else return value.accepted;
        }, confirmation_);
    fill(out, layout.confirm,
         accepted && layout.confirm.contains(pointer_) ? hover : row);
    stroke(out, layout.confirm, accepted ? good : muted);
    text(out, layout.confirm, accepted ? "CONFIRM" : "UNAVAILABLE",
         accepted ? text_color : muted, layout.small_font,
         TextAlign::Center);
  }
}

} // namespace stellar::native_colony_ui
