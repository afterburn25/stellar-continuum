#include "native_ship_design_workspace.hpp"
#include "native_ui_theme.hpp"

#include <stellar/engine/native_ui_skin.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_ship_design_ui {
namespace {
using namespace stellar::native_map;
using namespace stellar::native_ship_design;

namespace theme = stellar::native_ui;
constexpr Color shade{0, 4, 10, 190};
constexpr Color row = theme::color::surface_secondary;
constexpr Color selected_row = theme::color::surface_raised;
constexpr Color bright = theme::color::text_primary;
constexpr Color muted = theme::color::text_secondary;
constexpr Color good = theme::color::success;
constexpr Color caution = theme::color::caution;
constexpr Color failure = theme::color::danger;

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void label(DrawList &out, UiRect bounds, std::string value, Color color,
           int pixels, TextAlign align = TextAlign::Left) {
  const auto x = align == TextAlign::Center
                     ? bounds.x + bounds.width * .5f
                     : align == TextAlign::Right ? bounds.x + bounds.width
                                                 : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color,
                                pixels, bounds.width, bounds, align,
                                FontFace::Interface});
}
[[nodiscard]] std::string number(double value, int precision = 0) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}
[[nodiscard]] bool hull_locked(
    const NativeHullOption &hull) noexcept {
  return hull.lock_reason.has_value();
}
} // namespace

ShipDesignWorkspaceLayout
ShipDesignWorkspaceLayout::for_viewport(int width, int height) noexcept {
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const float scale = std::max(.72f, std::min({h / 760.f, w / 1080.f, 1.5f}));
  const float pw = std::min(w - 28.f * scale, 980.f * scale);
  const float ph = std::min(h - 56.f * scale, 600.f * scale);
  const UiRect panel_rect{(w - pw) * .5f, (h - ph) * .5f, pw, ph};
  const float pad = 18.f * scale;
  const float top = panel_rect.y + 56.f * scale;
  const float columns_h = panel_rect.y + panel_rect.height - top -
                          74.f * scale;
  const float list_w = 300.f * scale, detail_w = 290.f * scale;
  const float editor_w = pw - pad * 4.f - list_w - detail_w;
  const float bottom = panel_rect.y + panel_rect.height - 56.f * scale;
  const float button_w = 140.f * scale, button_h = 40.f * scale;
  ShipDesignWorkspaceLayout layout;
  layout.scale = scale;
  layout.title_font_pixels = static_cast<int>(std::lround(22.f * scale));
  layout.body_font_pixels = static_cast<int>(std::lround(15.f * scale));
  layout.small_font_pixels = static_cast<int>(std::lround(12.f * scale));
  layout.shade = {0.f, 0.f, w, h};
  layout.panel = panel_rect;
  layout.list = {panel_rect.x + pad, top, list_w, columns_h};
  layout.editor = {layout.list.x + list_w + pad, top, editor_w, columns_h};
  layout.details = {layout.editor.x + editor_w + pad, top, detail_w,
                    columns_h};
  layout.slot_row_height = 34.f * scale;
  layout.hull_row = {layout.editor.x, top, editor_w,
                     layout.slot_row_height};
  layout.commit = {layout.details.x, bottom, button_w, button_h};
  layout.retire = {layout.details.x + detail_w - button_w, bottom, button_w,
                   button_h};
  layout.close_button = {panel_rect.x + panel_rect.width - pad - 30.f * scale,
                         panel_rect.y + 14.f * scale, 30.f * scale,
                         30.f * scale};
  return layout;
}

std::string NativeShipDesignWorkspace::tr(std::string_view key,
                                          std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}
std::string NativeShipDesignWorkspace::trf(
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

void NativeShipDesignWorkspace::open() noexcept {
  visible_ = true;
  focus_ = -1;
}

void NativeShipDesignWorkspace::close() noexcept {
  visible_ = false;
  retire_confirmation_id_.reset();
  reset_gesture();
  focus_ = -1;
}

void NativeShipDesignWorkspace::set_view(NativeShipDesignView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  view_ = std::move(view);
  if (generation_changed) {
    selected_design_id_.reset();
    retire_confirmation_id_.reset();
    notice_.clear();
    hull_index_ = 0;
    draft_components_.clear();
  }
  reconcile_draft();
  if (selected_design_id_ &&
      std::ranges::none_of(view_->designs, [&](const auto &design) {
        return design.id == *selected_design_id_;
      }))
    selected_design_id_.reset();
}

void NativeShipDesignWorkspace::discard_campaign() {
  view_.reset();
  selected_design_id_.reset();
  retire_confirmation_id_.reset();
  notice_.clear();
  hull_index_ = 0;
  draft_components_.clear();
  focus_ = -1;
}

void NativeShipDesignWorkspace::set_notice(std::string message,
                                           bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
  retire_confirmation_id_.reset();
}

void NativeShipDesignWorkspace::reset_gesture() noexcept {
  pointer_owned_ = false;
  pressed_ = PressTarget::None;
}

const NativeHullOption *NativeShipDesignWorkspace::draft_hull() const noexcept {
  if (!view_ || view_->hulls.empty()) return nullptr;
  const auto index = std::min(hull_index_, view_->hulls.size() - 1);
  return &view_->hulls[index];
}

const NativeHullOption *
NativeShipDesignWorkspace::unlocked_hull(std::size_t index) const noexcept {
  if (!view_ || index >= view_->hulls.size()) return nullptr;
  return &view_->hulls[index];
}

void NativeShipDesignWorkspace::reconcile_draft() {
  const auto *hull = draft_hull();
  const auto slots =
      hull ? static_cast<int>(hull->slots.size()) : 0;
  draft_components_.resize(slots);
  for (int slot = 0; slot < slots; ++slot) {
    const auto options = slot_options(slot);
    auto &current = draft_components_[slot];
    if (current &&
        std::ranges::none_of(options, [&](const auto *option) {
          return option->id == *current;
        }))
      current.reset();
    // Required slots get a sensible default so fresh drafts stay valid.
    if (!current && hull && !options.empty() &&
        std::ranges::count(hull->required_slots, hull->slots[slot]) > 0)
      current = options.front()->id;
  }
}

void NativeShipDesignWorkspace::select_hull(std::size_t index) {
  hull_index_ = index;
  draft_components_.clear();
  retire_confirmation_id_.reset();
  reconcile_draft();
}

void NativeShipDesignWorkspace::cycle_hull(int direction) {
  if (!view_ || view_->hulls.empty()) return;
  const auto count = static_cast<int>(view_->hulls.size());
  auto next = static_cast<int>(hull_index_);
  for (int step = 0; step < count; ++step) {
    next = (next + direction + count) % count;
    if (!hull_locked(*unlocked_hull(static_cast<std::size_t>(next)))) {
      select_hull(static_cast<std::size_t>(next));
      return;
    }
  }
}

std::vector<const NativeComponentOption *>
NativeShipDesignWorkspace::slot_options(int slot_index) const {
  std::vector<const NativeComponentOption *> options;
  const auto *hull = draft_hull();
  if (!view_ || !hull ||
      slot_index >= static_cast<int>(hull->slots.size()))
    return options;
  const auto slot = hull->slots[slot_index];
  for (const auto &component : view_->components)
    if (component.slot == slot && !component.lock_reason)
      options.push_back(&component);
  return options;
}

void NativeShipDesignWorkspace::cycle_component(int slot_index,
                                                int direction) {
  const auto *hull = draft_hull();
  if (!hull || slot_index >= static_cast<int>(draft_components_.size()))
    return;
  const auto options = slot_options(slot_index);
  if (options.empty()) return;
  const bool required =
      std::ranges::count(hull->required_slots, hull->slots[slot_index]) > 0;
  const int option_count = static_cast<int>(options.size());
  auto &current = draft_components_[slot_index];
  int position = -1;
  for (int i = 0; i < option_count; ++i)
    if (current && options[i]->id == *current) position = i;
  position += direction;
  if (!required) {
    // Position -1 is the empty choice; wrap through it.
    if (position < -1) position = option_count - 1;
    if (position >= option_count) position = -1;
    if (position < 0)
      current.reset();
    else
      current = options[position]->id;
  } else {
    position = ((position % static_cast<int>(options.size())) +
                static_cast<int>(options.size())) %
               static_cast<int>(options.size());
    current = options[position]->id;
  }
  retire_confirmation_id_.reset();
}

void NativeShipDesignWorkspace::cycle_hull_command(int direction) {
  cycle_hull(direction);
}

void NativeShipDesignWorkspace::cycle_component_command(int slot_index,
                                                      int direction) {
  cycle_component(slot_index, direction);
}

std::string NativeShipDesignWorkspace::suggest_draft_name() const {
  const auto count = view_ ? view_->designs.size() : 0;
  return trf("DESIGN_DEFAULT_NAME", {std::to_string(count + 1)},
             "Custom Design {0}");
}

std::string NativeShipDesignWorkspace::draft_hull_id() const {
  const auto *hull = draft_hull();
  return hull ? hull->id : std::string{};
}

std::vector<std::string>
NativeShipDesignWorkspace::draft_component_ids() const {
  std::vector<std::string> ids;
  for (const auto &component : draft_components_)
    if (component) ids.push_back(*component);
  return ids;
}

std::vector<std::string> NativeShipDesignWorkspace::draft_issues() const {
  std::vector<std::string> issues;
  const auto *hull = draft_hull();
  if (!hull) {
    issues.push_back(tr("DESIGN_ISSUE_NO_HULL", "Select a hull."));
    return issues;
  }
  for (const auto required : hull->required_slots) {
    bool satisfied = false;
    for (std::size_t i = 0; i < draft_components_.size(); ++i)
      if (draft_components_[i] && hull->slots[i] == required)
        satisfied = true;
    if (!satisfied)
      issues.push_back(
          trf("DESIGN_ISSUE_MISSING_SLOT",
              {std::string(
                  stellar::core::ship_component_slot_name(required))},
              "Missing required {0} component."));
  }
  return issues;
}

bool NativeShipDesignWorkspace::draft_committable() const {
  const auto *hull = draft_hull();
  return hull && !hull_locked(*hull) && draft_issues().empty();
}

std::vector<NativeShipDesignWorkspace::SlotRow>
NativeShipDesignWorkspace::slot_rows(
    const ShipDesignWorkspaceLayout &layout) const {
  std::vector<SlotRow> rows;
  const auto *hull = draft_hull();
  if (!hull) return rows;
  const float pad = 6.f * layout.scale;
  float y = layout.hull_row.y + layout.hull_row.height +
            14.f * layout.scale;
  const float arrow = 30.f * layout.scale;
  for (std::size_t i = 0; i < hull->slots.size(); ++i) {
    SlotRow slot_row;
    slot_row.bounds = {layout.editor.x, y, layout.editor.width,
                       layout.slot_row_height};
    slot_row.next = {slot_row.bounds.x + slot_row.bounds.width - arrow -
                         pad,
                     slot_row.bounds.y + 2.f * layout.scale, arrow,
                     slot_row.bounds.height - 4.f * layout.scale};
    slot_row.previous = {slot_row.next.x - arrow - pad, slot_row.next.y,
                         arrow, slot_row.next.height};
    rows.push_back(slot_row);
    y += layout.slot_row_height + 4.f * layout.scale;
  }
  return rows;
}

std::string NativeShipDesignWorkspace::focused_label(
    const ShipDesignWorkspaceLayout &) const {
  if (focus_ < 0) return {};
  switch (focus_) {
  case 0: return tr("DESIGN_COMMIT", "Commit design");
  case 1: return tr("DESIGN_RETIRE", "Retire selected design");
  default: return tr("DESIGN_CLOSE", "Close design bureau");
  }
}

ShipDesignWorkspaceCommand NativeShipDesignWorkspace::handle(
    const InputEvent &event, int width, int height) {
  if (!visible_) return {};
  // Capture everything while the first projection is still loading so
  // clicks cannot leak through the shade to the map beneath.
  if (!view_) return {ShipDesignWorkspaceCommandKind::None, true};
  pointer_ = event.position;
  if (event.type == InputEventType::EscapePressed) {
    if (retire_confirmation_id_) {
      retire_confirmation_id_.reset();
      return {ShipDesignWorkspaceCommandKind::None, true};
    }
    close();
    return {ShipDesignWorkspaceCommandKind::Close, true};
  }
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    reset_gesture();
    return {ShipDesignWorkspaceCommandKind::None, true};
  }
  if (pointer_owned_ &&
      (width != press_width_ || height != press_height_))
    reset_gesture();
  if (event.type == InputEventType::KeyPressed && event.key) {
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u;
    constexpr std::uint32_t kDown = 0x40000051u, kUp = 0x40000052u;
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    if (fwd || bwd) focus_ = focus_ < 0 ? (bwd ? 2 : 0)
                                        : (focus_ + (bwd ? -1 : 1) + 3) % 3;
    else if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0) {
      const auto &rect = focus_ == 0   ? layout.commit
                         : focus_ == 1 ? layout.retire
                                       : layout.close_button;
      InputEvent press{InputEventType::LeftPressed},
          release{InputEventType::LeftReleased};
      press.position = release.position =
          {rect.x + rect.width * .5f, rect.y + rect.height * .5f};
      static_cast<void>(handle(press, width, height));
      return handle(release, width, height);
    }
    return {ShipDesignWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::LeftPressed) {
    focus_ = -1;
    if (!layout.panel.contains(event.position)) {
      close();
      return {ShipDesignWorkspaceCommandKind::Close, true};
    }
    pointer_owned_ = true;
    press_width_ = width;
    press_height_ = height;
    pressed_ = PressTarget::None;
    if (layout.close_button.contains(event.position))
      pressed_ = PressTarget::Close;
    else if (layout.commit.contains(event.position))
      pressed_ = PressTarget::Commit;
    else if (layout.retire.contains(event.position))
      pressed_ = PressTarget::Retire;
    return {ShipDesignWorkspaceCommandKind::None, true};
  }
  if (event.type == InputEventType::PointerMove)
    return {ShipDesignWorkspaceCommandKind::None, true};
  if (event.type == InputEventType::Wheel)
    return {ShipDesignWorkspaceCommandKind::None, true};
  if (event.type != InputEventType::LeftReleased)
    return {ShipDesignWorkspaceCommandKind::None, false};
  const auto target = pressed_;
  const bool owned = pointer_owned_;
  reset_gesture();
  if (!owned || !layout.panel.contains(event.position))
    return {ShipDesignWorkspaceCommandKind::None, true};
  if (target == PressTarget::Close &&
      layout.close_button.contains(event.position)) {
    close();
    return {ShipDesignWorkspaceCommandKind::Close, true};
  }
  if (target == PressTarget::Commit &&
      layout.commit.contains(event.position)) {
    retire_confirmation_id_.reset();
    return {ShipDesignWorkspaceCommandKind::Commit, true};
  }
  if (target == PressTarget::Retire &&
      layout.retire.contains(event.position)) {
    if (!selected_design_id_)
      return {ShipDesignWorkspaceCommandKind::None, true};
    if (retire_confirmation_id_ == selected_design_id_) {
      ShipDesignWorkspaceCommand command;
      command.kind = ShipDesignWorkspaceCommandKind::Retire;
      command.captured = true;
      command.design_id = *retire_confirmation_id_;
      retire_confirmation_id_.reset();
      return command;
    }
    retire_confirmation_id_ = selected_design_id_;
    notice_ = tr("DESIGN_CONFIRM_RETIRE",
                 "Select RETIRE again to confirm. This cannot be undone.");
    notice_accepted_ = true;
    return {ShipDesignWorkspaceCommandKind::None, true};
  }
  // Row interactions below the buttons.
  const auto rows = slot_rows(layout);
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].previous.contains(event.position))
      return {ShipDesignWorkspaceCommandKind::CycleComponent, true, {},
              i, -1};
    if (rows[i].next.contains(event.position))
      return {ShipDesignWorkspaceCommandKind::CycleComponent, true, {},
              i, 1};
    if (rows[i].bounds.contains(event.position))
      return {ShipDesignWorkspaceCommandKind::CycleComponent, true, {},
              i, 1};
  }
  // Hull row arrows reuse the commit/retire-safe press targets implicitly.
  const auto *hull = draft_hull();
  if (hull) {
    const float arrow = 30.f * layout.scale;
    const UiRect hull_next{layout.hull_row.x + layout.hull_row.width -
                               arrow - 6.f * layout.scale,
                           layout.hull_row.y + 2.f * layout.scale, arrow,
                           layout.hull_row.height - 4.f * layout.scale};
    const UiRect hull_prev{hull_next.x - arrow - 6.f * layout.scale,
                           hull_next.y, arrow, hull_next.height};
    if (hull_prev.contains(event.position))
      return {ShipDesignWorkspaceCommandKind::CycleHull, true, {}, 0, -1};
    if (hull_next.contains(event.position))
      return {ShipDesignWorkspaceCommandKind::CycleHull, true, {}, 0, 1};
  }
  // Design list selection.
  if (layout.list.contains(event.position)) {
    const float row_height = 46.f * layout.scale;
    float y = layout.list.y + 34.f * layout.scale;
    for (const auto &design : view_->designs) {
      const UiRect bounds{layout.list.x, y, layout.list.width,
                          row_height};
      if (bounds.contains(event.position)) {
        selected_design_id_ = design.id;
        retire_confirmation_id_.reset();
        ShipDesignWorkspaceCommand command;
        command.kind = ShipDesignWorkspaceCommandKind::SelectDesign;
        command.captured = true;
        command.design_id = design.id;
        return command;
      }
      y += row_height + 4.f * layout.scale;
      if (y > layout.list.y + layout.list.height) break;
    }
  }
  return {ShipDesignWorkspaceCommandKind::None, true};
}

ShipDesignWorkspaceLayout
NativeShipDesignWorkspace::layout(int width, int height) const noexcept {
  return ShipDesignWorkspaceLayout::for_viewport(width, height);
}

void NativeShipDesignWorkspace::render(DrawList &out, int width,
                                       int height) const {
  if (!visible_ || !view_) return;
  const auto layout = ShipDesignWorkspaceLayout::for_viewport(width, height);
  const auto &view = *view_;
  fill(out, layout.shade, shade);
  stellar::engine::ui_skin::surface(out, layout.panel, layout.scale);
  const float pad = 18.f * layout.scale;
  const float line = 22.f * layout.scale;

  label(out, {layout.panel.x + pad, layout.panel.y + 16.f * layout.scale,
              layout.panel.width - pad * 2.f, 30.f * layout.scale},
        tr("DESIGN_BUREAU_TITLE", "SHIP DESIGN BUREAU"), bright,
        layout.title_font_pixels);
  stellar::engine::ui_skin::control(
      out, layout.close_button,
      layout.close_button.contains(pointer_), false, true, layout.scale);
  label(out, layout.close_button, "X", bright, layout.body_font_pixels,
        TextAlign::Center);

  // Authored design list.
  label(out, {layout.list.x, layout.list.y, layout.list.width, line},
        trf("DESIGN_LIST_HEADING", {std::to_string(view.designs.size())},
            "CIVILIZATION DESIGNS ({0})"),
        muted, layout.small_font_pixels);
  const float row_height = 46.f * layout.scale;
  float y = layout.list.y + 34.f * layout.scale;
  if (view.designs.empty())
    label(out,
          {layout.list.x, y, layout.list.width, line},
          tr("DESIGN_LIST_EMPTY",
             "No authored designs yet. Compose one at right."),
          muted, layout.small_font_pixels);
  for (const auto &design : view.designs) {
    const UiRect bounds{layout.list.x, y, layout.list.width, row_height};
    if (y > layout.list.y + layout.list.height) break;
    const bool is_selected =
        selected_design_id_ && *selected_design_id_ == design.id;
    fill(out, bounds, is_selected ? selected_row : row);
    if (is_selected) stroke(out, bounds, good);
    label(out, {bounds.x + 8.f * layout.scale, bounds.y + 5.f * layout.scale,
                bounds.width - 16.f * layout.scale, line},
          design.name, design.valid ? bright : caution,
          layout.body_font_pixels);
    label(out,
          {bounds.x + 8.f * layout.scale,
           bounds.y + 5.f * layout.scale + line, bounds.width, line},
          design.hull_name +
              (design.valid ? "" : "  ·  " +
                                     tr("DESIGN_ROW_INVALID", "invalid")),
          muted, layout.small_font_pixels);
    y += row_height + 4.f * layout.scale;
  }

  // Draft editor.
  const auto *hull = draft_hull();
  label(out, {layout.editor.x, layout.editor.y - 26.f * layout.scale,
              layout.editor.width, line},
        tr("DESIGN_DRAFT_HEADING", "DRAFT COMPOSITION"), muted,
        layout.small_font_pixels);
  const auto hull_rows = slot_rows(layout);
  const float arrow = 30.f * layout.scale;
  const UiRect hull_next{layout.hull_row.x + layout.hull_row.width - arrow -
                             6.f * layout.scale,
                         layout.hull_row.y + 2.f * layout.scale, arrow,
                         layout.hull_row.height - 4.f * layout.scale};
  const UiRect hull_prev{hull_next.x - arrow - 6.f * layout.scale,
                         hull_next.y, arrow, hull_next.height};
  fill(out, layout.hull_row, row);
  label(out,
        {layout.hull_row.x + 8.f * layout.scale, layout.hull_row.y,
         layout.hull_row.width, layout.hull_row.height},
        hull ? hull->name + "  ·  " +
                   tr("DESIGN_HULL_TAG", "Hull")
             : tr("DESIGN_HULL_NONE", "No hull available"),
        hull && hull_locked(*hull) ? caution : bright,
        layout.body_font_pixels);
  for (const auto &arrow_bounds : {hull_prev, hull_next}) {
    stellar::engine::ui_skin::control(out, arrow_bounds,
                                      arrow_bounds.contains(pointer_), false,
                                      true, layout.scale);
    label(out, arrow_bounds,
          &arrow_bounds == &hull_prev ? "<" : ">", bright,
          layout.body_font_pixels, TextAlign::Center);
  }
  if (hull && hull_locked(*hull))
    label(out,
          {layout.hull_row.x, layout.hull_row.y + layout.hull_row.height +
                                  2.f * layout.scale,
           layout.hull_row.width, line},
          *hull->lock_reason, caution, layout.small_font_pixels);

  const auto issues = draft_issues();
  for (std::size_t i = 0; i < hull_rows.size(); ++i) {
    const auto &slot_row = hull_rows[i];
    fill(out, slot_row.bounds, row);
    const auto slot_name =
        hull ? std::string(
                   stellar::core::ship_component_slot_name(hull->slots[i]))
             : std::string{};
    const auto &current = draft_components_[i];
    std::string component_name =
        current ? *current
                : tr("DESIGN_SLOT_EMPTY", "(empty)");
    if (current)
      for (const auto &component : view.components)
        if (component.id == *current) component_name = component.name;
    label(out,
          {slot_row.bounds.x + 8.f * layout.scale, slot_row.bounds.y,
           slot_row.bounds.width - 80.f * layout.scale,
           slot_row.bounds.height},
          slot_name + "  ·  " + component_name,
          current ? bright : muted, layout.body_font_pixels);
    for (const auto &arrow_bounds : {slot_row.previous, slot_row.next}) {
      stellar::engine::ui_skin::control(
          out, arrow_bounds, arrow_bounds.contains(pointer_), false, true,
          layout.scale);
      label(out, arrow_bounds,
            &arrow_bounds == &slot_row.previous ? "<" : ">", bright,
            layout.body_font_pixels, TextAlign::Center);
    }
  }

  // Details: resolved stats for the draft, issues, actions.
  label(out, {layout.details.x, layout.details.y - 26.f * layout.scale,
              layout.details.width, line},
        tr("DESIGN_DETAILS_HEADING", "DRAFT ASSESSMENT"), muted,
        layout.small_font_pixels);
  float detail_y = layout.details.y;
  auto detail = [&](std::string value, Color color = muted) {
    label(out,
          {layout.details.x, detail_y, layout.details.width, line},
          std::move(value), color, layout.small_font_pixels);
    detail_y += line;
  };
  if (hull) {
    detail(tr("DESIGN_STATS_HEADER", "ESTIMATED PERFORMANCE"), bright);
    detail(trf("DESIGN_STAT_SPEED", {number(hull->strategic_speed, 1)},
               "Speed  {0}"));
    detail(trf("DESIGN_STAT_SLOTS",
               {std::to_string(hull->slot_count)},
               "Slots  {0}"));
    detail(trf("DESIGN_STAT_COST",
               {number(hull->industry_cost), number(hull->credit_cost)},
               "Base cost  {0} industry  ·  {1} credits"));
    for (const auto &issue : issues) detail(issue, caution);
    if (issues.empty() && !hull_locked(*hull))
      detail(tr("DESIGN_DRAFT_READY", "Draft is structurally valid."),
             good);
  }

  const auto *selected =
      selected_design_id_
          ? &*std::ranges::find(view.designs, *selected_design_id_,
                                &NativeAuthoredDesignRow::id)
          : nullptr;
  if (selected) {
    detail("");
    detail(selected->name, bright);
    detail(trf("DESIGN_SELECTED_STATS",
               {number(selected->max_hull), number(selected->max_shields),
                number(selected->weapon_damage)},
               "Hull {0}  ·  Shields {1}  ·  Damage {2}"));
    if (!selected->valid)
      for (const auto &issue : selected->issues) detail(issue, caution);
    if (!selected->can_retire && selected->retire_blocker)
      detail(*selected->retire_blocker, caution);
  }

  // Notice + actions.
  if (!notice_.empty())
    label(out,
          {layout.details.x,
           layout.commit.y - 30.f * layout.scale, layout.details.width,
           line},
          notice_, notice_accepted_ ? good : failure,
          layout.small_font_pixels);
  const bool can_commit = draft_committable();
  stellar::engine::ui_skin::control(
      out, layout.commit, layout.commit.contains(pointer_), true,
      can_commit, layout.scale);
  label(out, layout.commit, tr("DESIGN_COMMIT", "COMMIT"),
        can_commit ? bright : muted, layout.body_font_pixels,
        TextAlign::Center);
  const auto *retireable =
      selected && selected->can_retire ? selected : nullptr;
  const bool can_retire = retireable != nullptr;
  stellar::engine::ui_skin::control(
      out, layout.retire, layout.retire.contains(pointer_), true,
      can_retire, layout.scale);
  label(out, layout.retire,
        retire_confirmation_id_ ? tr("DESIGN_RETIRE_CONFIRM", "CONFIRM?")
                                : tr("DESIGN_RETIRE", "RETIRE"),
        can_retire ? bright : muted, layout.body_font_pixels,
        TextAlign::Center);
  if (focus_ >= 0) {
    const auto &ring = focus_ == 0   ? layout.commit
                       : focus_ == 1 ? layout.retire
                                     : layout.close_button;
    stellar::native_ui::focus_ring(out, ring);
  }
}

} // namespace stellar::native_ship_design_ui
