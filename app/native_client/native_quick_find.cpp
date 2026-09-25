#include "native_quick_find.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace stellar::native_quick_find {
using namespace stellar::native_map;
namespace theme = stellar::native_ui;
namespace {

std::string folded(std::string value) {
  for (auto &c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

void truncate_codepoint(std::string &value) {
  if (value.empty()) return;
  auto n = value.size() - 1;
  while (n > 0 && (static_cast<unsigned char>(value[n]) & 0xc0u) == 0x80u) --n;
  value.resize(n);
}

constexpr int kMaxRows = 8;
constexpr std::uint32_t kTab = 9u, kReturn = 13u;
constexpr std::uint32_t kDown = 0x40000051u, kUp = 0x40000052u;
constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;

} // namespace

QuickFindLayout QuickFindLayout::make(int width, int height) {
  const float s =
      stellar::native_map::NativeUiLayout::for_viewport(width, height).scale;
  const float text = stellar::native_map::NativeUiLayout::text_scale();
  const float panel_width = std::min(560.f * s, static_cast<float>(width) - 40.f * s);
  const float row = 30.f * s;
  const float panel_height = 14.f * s + 20.f * s + 8.f * s + 32.f * s +
                             8.f * s + kMaxRows * row + 8.f * s + 18.f * s +
                             12.f * s;
  const UiRect panel{(static_cast<float>(width) - panel_width) * .5f,
                     static_cast<float>(height) * .16f, panel_width,
                     panel_height};
  QuickFindLayout layout;
  layout.scale = s;
  layout.panel = panel;
  layout.title = {panel.x + 14.f * s, panel.y + 12.f * s,
                  panel.width - 28.f * s, 20.f * s};
  layout.field = {panel.x + 14.f * s,
                  layout.title.y + layout.title.height + 8.f * s,
                  panel.width - 28.f * s, 32.f * s};
  layout.list = {panel.x + 14.f * s,
                 layout.field.y + layout.field.height + 8.f * s,
                 panel.width - 28.f * s, kMaxRows * row};
  layout.hint = {panel.x + 14.f * s,
                 layout.list.y + layout.list.height + 6.f * s,
                 panel.width - 28.f * s, 16.f * s};
  layout.body_pixels = static_cast<int>(std::lround(15.f * s * text));
  layout.small_pixels = static_cast<int>(std::lround(12.f * s * text));
  layout.row_pixels = static_cast<int>(std::lround(row));
  return layout;
}

void QuickFind::open(std::vector<Entry> entries) {
  entries_ = std::move(entries);
  query_.clear();
  // Nothing highlighted on open — the field reports the Edit focus until
  // the player arrows into the results.
  selected_ = -1;
  pressed_.reset();
  scroll_ = 0.f;
  refilter();
  visible_ = true;
}

void QuickFind::close() noexcept {
  visible_ = false;
  entries_.clear();
  matches_.clear();
  query_.clear();
  selected_ = -1;
  pressed_.reset();
  scroll_ = 0.f;
}

void QuickFind::refilter() {
  matches_.clear();
  const auto query = folded(query_);
  for (std::size_t index = 0; index < entries_.size(); ++index) {
    const auto &entry = entries_[index];
    if (query.empty() ||
        folded(entry.label + " " + entry.detail + " " +
               kind_label(entry.kind))
            .find(query) != std::string::npos)
      matches_.push_back(index);
  }
  if (matches_.empty())
    selected_ = -1;
  else if (selected_ >= static_cast<int>(matches_.size()))
    selected_ = 0;
}

UiRect QuickFind::row_bounds(const QuickFindLayout &layout,
                             std::size_t match) const {
  return {layout.list.x,
          layout.list.y + static_cast<float>(match) *
                              static_cast<float>(layout.row_pixels) -
                          scroll_,
          layout.list.width, static_cast<float>(layout.row_pixels)};
}

int QuickFind::focus() const noexcept {
  return selected_ + 1;  // 0 is the field; result rows start at 1
}

std::string QuickFind::tr(std::string_view key,
                          std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string QuickFind::kind_label(EntryKind kind) const {
  switch (kind) {
  case EntryKind::System: return tr("QUICK_FIND_KIND_SYSTEM", "SYSTEM");
  case EntryKind::Colony: return tr("QUICK_FIND_KIND_COLONY", "COLONY");
  case EntryKind::Fleet: return tr("QUICK_FIND_KIND_FLEET", "FLEET");
  case EntryKind::Contact: return tr("QUICK_FIND_KIND_CONTACT", "CONTACT");
  case EntryKind::Mission: return tr("QUICK_FIND_KIND_MISSION", "MISSION");
  case EntryKind::Workspace: return tr("QUICK_FIND_KIND_WORKSPACE", "WORKSPACE");
  }
  return {};
}

std::string QuickFind::focused_label(int, int) const {
  if (!visible_) return {};
  if (selected_ >= 0 && selected_ < static_cast<int>(matches_.size())) {
    const auto &entry = entries_[matches_[static_cast<std::size_t>(selected_)]];
    return kind_label(entry.kind) + " · " + entry.label +
           (entry.detail.empty() ? std::string{} : " · " + entry.detail);
  }
  return tr("QUICK_FIND_FIELD", "Quick find");
}

std::optional<UiRect> QuickFind::focused_bounds(int width, int height) const {
  if (!visible_) return std::nullopt;
  const auto layout = QuickFindLayout::make(width, height);
  if (selected_ >= 0 && selected_ < static_cast<int>(matches_.size()))
    return row_bounds(layout, static_cast<std::size_t>(selected_));
  return layout.field;
}

stellar::engine::AnnouncementControl
QuickFind::focused_control(int, int) const {
  if (selected_ >= 0 && selected_ < static_cast<int>(matches_.size()))
    return stellar::engine::AnnouncementControl::Custom;
  return stellar::engine::AnnouncementControl::Edit;
}

std::optional<stellar::engine::AnnouncementValue>
QuickFind::focused_value(int width, int height) const {
  if (!visible_ || focused_control(width, height) !=
                       stellar::engine::AnnouncementControl::Edit)
    return std::nullopt;
  return stellar::engine::AnnouncementValue{query_};
}

bool QuickFind::set_focused_text(std::string text, int, int) {
  if (!visible_) return false;
  if (text.size() > 80) {
    auto n = 80;
    while (n > 0 &&
           (static_cast<unsigned char>(text[static_cast<std::size_t>(n)]) &
            0xc0u) == 0x80u)
      --n;
    text.resize(static_cast<std::size_t>(n));
  }
  query_ = std::move(text);
  refilter();
  return true;
}

Command QuickFind::handle(const InputEvent &event, int width, int height) {
  Command out;
  if (!visible_) return out;
  out.captured = true;
  const auto layout = QuickFindLayout::make(width, height);
  pointer_ = event.position;
  const auto activate = [&](int match) {
    if (match < 0 || match >= static_cast<int>(matches_.size())) return;
    out.activated = entries_[matches_[static_cast<std::size_t>(match)]];
    close();
  };
  switch (event.type) {
  case InputEventType::EscapePressed:
    close();
    return out;
  case InputEventType::PointerCancelled:
    pressed_.reset();
    return out;
  case InputEventType::TextEntered:
    if (query_.size() + event.text.size() <= 80) {
      query_ += event.text;
      refilter();
    }
    return out;
  case InputEventType::BackspacePressed:
    truncate_codepoint(query_);
    refilter();
    return out;
  case InputEventType::KeyPressed: {
    const int count = static_cast<int>(matches_.size());
    // Space stays a query character (it arrives via TextEntered); only
    // Return activates — pad A is translated to Return upstream.
    if (event.key == kReturn) {
      activate(selected_ >= 0 ? selected_ : 0);
      return out;
    }
    if (count == 0) return out;
    if (event.key == kHome) selected_ = 0;
    else if (event.key == kEnd) selected_ = count - 1;
    else if (event.key == kDown || (event.key == kTab && !event.shift))
      selected_ = selected_ < 0 ? 0 : (selected_ + 1) % count;
    else if (event.key == kUp || (event.key == kTab && event.shift))
      selected_ = selected_ <= 0 ? count - 1 : selected_ - 1;
    // Keep the highlight inside the scroll window.
    const float top = static_cast<float>(selected_) * layout.row_pixels;
    if (top < scroll_)
      scroll_ = top;
    else if (top + layout.row_pixels > scroll_ + layout.list.height)
      scroll_ = top + layout.row_pixels - layout.list.height;
    return out;
  }
  case InputEventType::Wheel:
    if (layout.list.contains(event.position)) {
      const float extent =
          static_cast<float>(matches_.size()) * layout.row_pixels;
      scroll_ = std::clamp(scroll_ - event.wheel_y * layout.row_pixels, 0.f,
                           std::max(0.f, extent - layout.list.height));
    }
    return out;
  case InputEventType::LeftPressed:
    if (!layout.panel.contains(event.position)) {
      close();
      return out;
    }
    if (layout.list.contains(event.position)) {
      const auto match = static_cast<std::size_t>(std::floor(
          (event.position.y - layout.list.y + scroll_) / layout.row_pixels));
      if (match < matches_.size()) {
        pressed_ = match;
        selected_ = static_cast<int>(match);
      }
    }
    return out;
  case InputEventType::LeftReleased: {
    const auto pressed = pressed_;
    pressed_.reset();
    if (!layout.panel.contains(event.position)) {
      if (!pressed) close();
      return out;
    }
    if (pressed && layout.list.contains(event.position)) {
      const auto match = static_cast<std::size_t>(std::floor(
          (event.position.y - layout.list.y + scroll_) / layout.row_pixels));
      if (match == *pressed) activate(static_cast<int>(match));
    }
    return out;
  }
  default:
    return out;
  }
}

void QuickFind::render(DrawList &out, int width, int height) const {
  if (!visible_) return;
  const auto layout = QuickFindLayout::make(width, height);
  const float s = layout.scale;
  theme::fill(out,
              {0.f, 0.f, static_cast<float>(width),
               static_cast<float>(height)},
              {0, 0, 0, 140});
  theme::panel(out, layout.panel);
  theme::section_header(out, layout.title,
                        tr("QUICK_FIND_TITLE", "QUICK FIND"),
                        layout.body_pixels, theme::Tone::Selected);
  // Field — always the text target while the palette is open.
  theme::fill(out, layout.field, theme::color::surface_secondary);
  theme::stroke(out, layout.field, theme::color::selected);
  theme::text(out, {layout.field.x + 10.f * s,
                    layout.field.y + (layout.field.height -
                                      layout.body_pixels) * .5f},
              query_.empty()
                  ? tr("QUICK_FIND_PLACEHOLDER",
                       "Search systems, colonies, fleets, missions, screens…")
                  : query_ + "|",
              query_.empty() ? theme::color::text_muted
                             : theme::color::text_primary,
              layout.body_pixels, layout.field.width - 20.f * s,
              TextAlign::Left, FontFace::Interface, layout.field);
  // Results.
  if (matches_.empty()) {
    theme::empty_state(out, layout.list,
                       tr("QUICK_FIND_EMPTY", "No matching destinations"),
                       tr("QUICK_FIND_EMPTY_HINT",
                          "Charted systems and known assets are searchable."),
                       layout.body_pixels);
  }
  for (std::size_t match = 0; match < matches_.size(); ++match) {
    const auto bounds = row_bounds(layout, match);
    const auto clip = theme::clipped(bounds, layout.list);
    if (!clip) continue;
    const auto &entry = entries_[matches_[match]];
    const bool highlighted = static_cast<int>(match) == selected_;
    theme::fill(out, *clip,
                highlighted ? theme::color::surface_raised
                            : bounds.contains(pointer_)
                                ? theme::color::surface_hover
                                : theme::color::surface);
    if (highlighted)
      theme::fill(out, {clip->x, clip->y, 3.f * s, clip->height},
                  theme::color::selected);
    const UiRect badge_bounds{clip->x + 8.f * s, clip->y + 5.f * s,
                              78.f * s, clip->height - 10.f * s};
    theme::badge(out, badge_bounds, kind_label(entry.kind),
                 layout.small_pixels,
                 entry.kind == EntryKind::System    ? theme::Tone::Selected
                 : entry.kind == EntryKind::Colony  ? theme::Tone::Economy
                 : entry.kind == EntryKind::Fleet   ? theme::Tone::Military
                 : entry.kind == EntryKind::Mission ? theme::Tone::Science
                 : entry.kind == EntryKind::Workspace ? theme::Tone::Neutral
                                                    : theme::Tone::Diplomacy);
    theme::text(out,
                {badge_bounds.x + badge_bounds.width + 10.f * s,
                 clip->y + (clip->height - layout.body_pixels) * .5f},
                entry.label, theme::color::text_primary, layout.body_pixels,
                clip->x + clip->width - badge_bounds.x - badge_bounds.width -
                    18.f * s,
                TextAlign::Left, FontFace::Interface, *clip);
    if (!entry.detail.empty())
      theme::text(out, {clip->x + clip->width - 8.f * s,
                        clip->y + (clip->height - layout.small_pixels) * .5f},
                  entry.detail, theme::color::text_secondary,
                  layout.small_pixels, clip->width * .45f, TextAlign::Right,
                  FontFace::Interface, *clip);
  }
  theme::text(out, {layout.hint.x, layout.hint.y},
              tr("QUICK_FIND_HINT", "Enter to open · Esc to close"),
              theme::color::text_muted, layout.small_pixels, layout.hint.width);
}

} // namespace stellar::native_quick_find
