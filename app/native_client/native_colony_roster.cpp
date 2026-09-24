#include "native_colony_roster.hpp"
#include <stellar/core/campaign_observation.hpp>

#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <unordered_set>

namespace stellar::native_colony_roster {
using namespace stellar::native_map;
namespace {
constexpr Color ink{228, 240, 250, 255}, muted{161, 187, 210, 255},
    cyan{108, 218, 234, 255}, amber{255, 194, 106, 255};

View unavailable(std::uint64_t generation, int player, std::string message) {
  return {generation, player, {}, std::move(message), false};
}

template <class Range, class Projection>
bool unique_ids(const Range &items, Projection projection) {
  std::unordered_set<int> ids;
  for (const auto &item : items)
    if (!ids.insert(std::invoke(projection, item)).second)
      return false;
  return true;
}

std::string translate(const stellar::engine::LocalizationTable *locale,
                      std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string kind(const stellar::core::SettlementKind value,
                 const stellar::engine::LocalizationTable *locale) {
  return value == stellar::core::SettlementKind::ResourceOutpost
             ? translate(locale, "ROSTER_KIND_OUTPOST", "Resource outpost")
             : translate(locale, "ROSTER_KIND_COLONY", "Colony");
}

std::string population(double value,
                       const stellar::engine::LocalizationTable *locale) {
  if (!std::isfinite(value) || value < 0.)
    return translate(locale, "ROSTER_UNCONFIRMED", "Unconfirmed");
  std::ostringstream out;
  if (value >= 1000.)
    out << std::fixed << std::setprecision(value >= 10000. ? 0 : 1)
        << value / 1000. << translate(locale, "ROSTER_POP_BILLIONS", "B");
  else if (value >= 1.)
    out << std::fixed << std::setprecision(value >= 100. ? 0 : 1) << value
        << translate(locale, "ROSTER_POP_MILLIONS", "M");
  else
    out << std::fixed << std::setprecision(0) << value * 1000.
        << translate(locale, "ROSTER_POP_THOUSANDS", "K");
  return out.str();
}

UiRect clip_intersection(UiRect a, UiRect b) {
  const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  return {x, y, std::max(0.f, std::min(a.x + a.width, b.x + b.width) - x),
          std::max(0.f, std::min(a.y + a.height, b.y + b.height) - y)};
}

void text(DrawList &out, UiRect box, std::string value, int font, Color color,
          UiRect clip) {
  const auto visible = clip_intersection(box, clip);
  if (visible.width > 0 && visible.height > 0)
    out.overlay.emplace_back(Text{
        {box.x, box.y}, std::move(value), color, font, box.width, visible});
}

bool pointer(InputEventType type) {
  return type == InputEventType::LeftPressed ||
         type == InputEventType::LeftReleased ||
         type == InputEventType::RightPressed ||
         type == InputEventType::RightReleased ||
         type == InputEventType::PointerMove || type == InputEventType::Wheel;
}
} // namespace

View build(const stellar::core::FreshCampaignState &campaign,
           std::uint64_t generation,
           const stellar::engine::LocalizationTable *locale) {
  try {
    const int player = campaign.player_civilization_id;
    const auto player_count = std::ranges::count(
        campaign.civilizations, player, &stellar::core::Civilization::id);
    const auto flagged_players = std::ranges::count_if(
        campaign.civilizations, [](const auto &c) { return c.is_player; });
    const auto player_it = std::ranges::find(campaign.civilizations, player,
                                             &stellar::core::Civilization::id);
    if (player < 0 || player_count != 1 || flagged_players != 1 ||
        player_it == campaign.civilizations.end() || !player_it->is_player)
      return unavailable(
          generation, player,
          translate(locale, "ROSTER_ERROR_PLAYER",
                    "Colony roster unavailable: the active player identity is invalid."));
    if (!unique_ids(campaign.systems, &stellar::core::StellarSystem::id) ||
        !unique_ids(campaign.bodies, &stellar::core::PlanetaryBody::id) ||
        !unique_ids(campaign.colonies, &stellar::core::Colony::id))
      return unavailable(generation, player,
                         translate(locale, "ROSTER_ERROR_DUPLICATE",
                                   "Colony roster unavailable: duplicate canonical colony, body, or system records were found."));

    const auto invalid_system = std::ranges::any_of(
        campaign.systems, [](const auto &item) { return item.id < 0; });
    const auto invalid_body =
        std::ranges::any_of(campaign.bodies, [](const auto &item) {
          return item.id < 0 || item.system_id < 0;
        });
    const auto invalid_colony =
        std::ranges::any_of(campaign.colonies, [](const auto &item) {
          return item.id < 0 || item.civilization_id < 0 ||
                 item.system_id < 0 ||
                 (item.planetary_body_id && *item.planetary_body_id < 0);
        });
    if (invalid_system || invalid_body || invalid_colony)
      return unavailable(generation, player,
                         translate(locale, "ROSTER_ERROR_IDENTITY",
                                   "Colony roster unavailable: a canonical record has an invalid identity."));

    View result{
        generation, player, {},
        translate(locale, "ROSTER_EMPTY", "No owned colonies are available."),
        true};
    result.developer_inspection = stellar::core::developer_observation(campaign, player);
    for (const auto &colony : campaign.colonies) {
      if (!stellar::core::can_inspect_settlement(campaign, player, colony))
        continue;
      Row row;
      row.colony_id = colony.id;
      row.name = colony.name;
      row.kind_label = kind(colony.kind, locale);
      if (stellar::core::developer_observation(campaign, player)) {
        const auto owner = std::ranges::find(campaign.civilizations, colony.civilization_id,
                                            &stellar::core::Civilization::id);
        if (owner != campaign.civilizations.end()) row.kind_label += " · " + owner->name;
      }
      row.population = population(colony.population_millions, locale);
      row.population_millions = colony.population_millions;
      if (!colony.planetary_body_id) {
        row.body_name = row.system_name =
            translate(locale, "ROSTER_UNCONFIRMED", "Unconfirmed");
        row.reason = translate(locale, "ROSTER_REASON_NO_WORLD",
                               "This owned colony has no canonical world reference.");
        result.rows.push_back(std::move(row));
        continue;
      }
      const auto body =
          std::ranges::find(campaign.bodies, *colony.planetary_body_id,
                            &stellar::core::PlanetaryBody::id);
      if (body == campaign.bodies.end() ||
          body->system_id != colony.system_id) {
        row.body_name = row.system_name =
            translate(locale, "ROSTER_UNCONFIRMED", "Unconfirmed");
        row.reason = translate(locale, "ROSTER_REASON_WORLD_GONE",
                               "This owned colony's world reference is unavailable.");
        result.rows.push_back(std::move(row));
        continue;
      }
      row.body_id = body->id;
      row.system_id = body->system_id;
      const auto system = std::ranges::find(campaign.systems, body->system_id,
                                            &stellar::core::StellarSystem::id);
      if (system == campaign.systems.end()) {
        row.body_name = row.system_name =
            translate(locale, "ROSTER_UNCONFIRMED", "Unconfirmed");
        row.reason = translate(locale, "ROSTER_REASON_SYSTEM_GONE",
                               "This owned colony's system reference is unavailable.");
      } else if (stellar::core::observation_survey_level(campaign, player, body->system_id) <
                 stellar::core::SystemSurveyLevel::fully_surveyed) {
        row.body_name = row.system_name =
            translate(locale, "ROSTER_UNCONFIRMED", "Unconfirmed");
        row.reason =
            translate(locale, "ROSTER_REASON_SURVEY",
                      "A full survey is required before this world can be opened.");
      } else {
        row.body_name = body->name;
        row.system_name = system->name;
        row.can_open = true;
      }
      result.rows.push_back(std::move(row));
    }
    std::ranges::sort(result.rows, {}, &Row::colony_id);
    result.message = result.rows.empty()
                         ? translate(locale, "ROSTER_MESSAGE_EMPTY",
                                     "No colonies belong to the active player.")
                         : stellar::core::developer_observation(campaign, player)
                             ? translate(locale, "ROSTER_MESSAGE_DEVELOPER",
                                         "Developer inspection · All empires · Select a colony for live statistics.")
                             : translate(locale, "ROSTER_MESSAGE_READY",
                                         "Select an owned colony to inspect its world.");
    return result;
  } catch (const std::exception &) {
    return unavailable(generation, campaign.player_civilization_id,
                       translate(locale, "ROSTER_ERROR_PROJECTION",
                                 "Colony roster could not be projected. Refresh when the campaign is ready."));
  }
}

RosterLayout RosterLayout::for_viewport(int width, int height) noexcept {
  const float s = NativeUiLayout::for_viewport(width, height).scale;
  const float panel_width=std::min(920.f*s,static_cast<float>(width)-100.f*s);
  const float panel_height=std::min(590.f*s,static_cast<float>(height)-108.f*s);
  const UiRect panel{(width-panel_width)*.5f,(height-panel_height)*.5f+20.f*s,
                     panel_width,panel_height};
  const float header = 124.f * s;
  return {panel,
          {panel.x + 14.f * s, panel.y + header, panel.width - 36.f * s,
           std::max(0.f, panel.height - header - 14.f * s)},
          {panel.x + panel.width - 42.f * s, panel.y + 12.f * s, 28.f * s,
           28.f * s},
          {panel.x + panel.width - 166.f * s, panel.y + 12.f * s, 112.f * s,
           28.f * s},
          {panel.x + panel.width - 500.f * s, panel.y + 12.f * s, 320.f * s,
           28.f * s},
          s,
          std::max(58.f * s, height <= 800 ? 66.f * s : 60.f * s)};
}

std::string RosterWorkspace::tr(std::string_view key,
                                std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

void RosterWorkspace::clear_press() noexcept {
  pressed_row_.reset();
  pressed_target_ = PressTarget::none;
  pointer_owned_ = false;
}
void RosterWorkspace::set_view(View value) {
  const bool identity_changed = value.generation != view_.generation ||
                                value.player_id != view_.player_id;
  const bool content_changed =
      value.available != view_.available || value.rows != view_.rows;
  view_ = std::move(value);
  rebuild_table();
  if (identity_changed) {
    list_.scroll_offset = 0;
    notice_.clear();
    clear_press();
  } else if (content_changed) {
    clear_press();
  }
}
void RosterWorkspace::rebuild_table() {
  if (table_.columns().empty())
    table_.set_columns({{"name", "ROSTER_COL_COLONY"},
                        {"world", "ROSTER_COL_WORLD"},
                        {"population", "ROSTER_COL_POPULATION"}});
  std::vector<stellar::engine::TableModel::Row> rows;
  rows.reserve(view_.rows.size());
  for (std::size_t i = 0; i < view_.rows.size(); ++i) {
    const auto &r = view_.rows[i];
    const bool numeric =
        std::isfinite(r.population_millions) && r.population_millions >= 0.;
    rows.push_back(
        {std::to_string(i),
         {{r.name + " " + r.kind_label, 0., false},
          {r.body_name + " " + r.system_name, 0., false},
          {r.population, numeric ? r.population_millions : 0., numeric}}});
  }
  table_.set_rows(std::move(rows));
  table_.refilter(search_); // filter survives live refresh like sort does
  apply_display_order();
}
void RosterWorkspace::apply_filter() {
  table_.refilter(search_);
  apply_display_order();
  list_.scroll_offset = 0;
}
void RosterWorkspace::apply_display_order() {
  display_order_.clear();
  display_order_.reserve(table_.display_rows().size());
  for (const auto *row : table_.display_rows())
    display_order_.push_back(std::stoi(row->first));
}
// Focusables walk actionable rects in (y,x) order: the search field,
// refresh and close controls, the sort-column headers (the same hit zones
// header_column() answers), and each list row clipped to its viewport.
std::vector<UiRect> RosterWorkspace::focusables(
    const RosterLayout &layout) const {
  std::vector<UiRect> out;
  out.push_back(layout.search);
  out.push_back(layout.refresh);
  out.push_back(layout.close);
  const float s = layout.scale;
  const bool compact =
      viewport_height_ <= 800 || layout.list.width < 650.f * s;
  if (compact) {
    out.push_back({layout.list.x + layout.list.width * .70f,
                   layout.list.y - 20.f * s, layout.list.width * .28f,
                   20.f * s});
  } else {
    out.push_back({layout.list.x + 8.f * s, layout.list.y - 26.f * s,
                   layout.list.width * .37f, 24.f * s});
    out.push_back({layout.list.x + layout.list.width * .39f,
                   layout.list.y - 26.f * s, layout.list.width * .38f,
                   24.f * s});
    out.push_back({layout.list.x + layout.list.width * .79f,
                   layout.list.y - 26.f * s, layout.list.width * .18f,
                   24.f * s});
  }
  const float pitch = layout.row_height + 5.f * s;
  for (std::size_t i = 0; i < display_order_.size(); ++i) {
    const auto visible = clip_intersection(
        {layout.list.x,
         layout.list.y + static_cast<float>(i) * pitch - list_.scroll_offset,
         layout.list.width, layout.row_height},
        layout.list);
    if (visible.height > 0.f)
      out.push_back(visible);
  }
  std::ranges::sort(out, [](const UiRect &a, const UiRect &b) {
    return a.y == b.y ? a.x < b.x : a.y < b.y;
  });
  return out;
}
int RosterWorkspace::header_column(Point point,
                                   const RosterLayout &layout) const noexcept {
  const float s = layout.scale;
  const bool compact =
      viewport_height_ <= 800 || layout.list.width < 650.f * s;
  if (compact)
    return UiRect{layout.list.x + layout.list.width * .70f,
                  layout.list.y - 20.f * s, layout.list.width * .28f,
                  20.f * s}
                   .contains(point)
               ? 2
               : -1;
  if (UiRect{layout.list.x + 8.f * s, layout.list.y - 26.f * s,
             layout.list.width * .37f, 24.f * s}
          .contains(point))
    return 0;
  if (UiRect{layout.list.x + layout.list.width * .39f,
             layout.list.y - 26.f * s, layout.list.width * .38f, 24.f * s}
          .contains(point))
    return 1;
  if (UiRect{layout.list.x + layout.list.width * .79f,
             layout.list.y - 26.f * s, layout.list.width * .18f, 24.f * s}
          .contains(point))
    return 2;
  return -1;
}
void RosterWorkspace::open() noexcept {
  visible_ = true;
  list_.scroll_offset = 0;
  search_.clear();
  search_focused_ = false;
  focus_ = -1;
  apply_filter();
  clear_press();
}
void RosterWorkspace::close() noexcept {
  visible_ = false;
  search_focused_ = false;
  focus_ = -1;
  clear_press();
}
void RosterWorkspace::discard_campaign() noexcept {
  close();
  view_ = {};
  notice_.clear();
  search_.clear();
  list_.scroll_offset = 0;
  display_order_.clear();
}
void RosterWorkspace::sync_scroll(const RosterLayout &layout) const noexcept {
  list_.configure(display_order_.size(),
                  layout.row_height + 5.f * layout.scale,
                  layout.list.height);
}
float RosterWorkspace::maximum_scroll(
    const RosterLayout &layout) const noexcept {
  sync_scroll(layout);
  return list_.max_scroll();
}
UiRect RosterWorkspace::row_button(int index, int width,
                                   int height) const noexcept {
  const auto layout = RosterLayout::for_viewport(width, height);
  if (index < 0 || static_cast<std::size_t>(index) >= display_order_.size())
    return {};
  return {layout.list.x,
          layout.list.y +
              static_cast<float>(index) *
                  (layout.row_height + 5.f * layout.scale) -
              list_.scroll_offset,
          layout.list.width, layout.row_height};
}
RosterCommand RosterWorkspace::handle(const InputEvent &event, int width,
                                      int height) {
  if (!visible_)
    return {};
  if (event.type == InputEventType::PointerMove)
    pointer_ = event.position;
  if (width != viewport_width_ || height != viewport_height_) {
    viewport_width_ = width;
    viewport_height_ = height;
    clear_press();
  }
  const auto layout = RosterLayout::for_viewport(width, height);
  sync_scroll(layout);
  if (event.type == InputEventType::PointerCancelled) {
    focus_ = -1;
    clear_press();
    return {true};
  }
  if (event.type == InputEventType::EscapePressed) {
    if (search_focused_) {
      search_focused_ = false;
      return {true};
    }
    close();
    return {true};
  }
  if (search_focused_ &&
      (event.type == InputEventType::TextEntered ||
       event.type == InputEventType::BackspacePressed)) {
    if (event.type == InputEventType::TextEntered &&
        search_.size() + event.text.size() <= 64) {
      search_ += event.text;
    } else if (event.type == InputEventType::BackspacePressed &&
               !search_.empty()) {
      // Drop whole UTF-8 sequences, not single bytes.
      auto n = search_.size() - 1;
      while (n > 0 &&
             (static_cast<unsigned char>(search_[n]) & 0xc0) == 0x80)
        --n;
      search_.resize(n);
    }
    apply_filter();
    return {true};
  }
  if (search_focused_ && event.type == InputEventType::KeyPressed) {
    // While editing, the search field owns the keyboard — Tab or Return
    // commit out of it; every other key stays captured.
    if (event.key == 9u || event.key == 13u)
      search_focused_ = false;
    return {true};
  }
  if (event.type == InputEventType::KeyPressed && event.key) {
    // SDL_Keycode: Tab/arrows walk the (y,x)-ordered focusables — search,
    // refresh, close, the sort headers and each visible row — Home/End
    // jump to the ends, and Return/Space replay the matched press/release
    // pair through the same dispatch a click takes.
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    const auto rects = focusables(layout);
    const int count = static_cast<int>(rects.size());
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      return {true};
    }
    if (count > 0 && (fwd || bwd)) {
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      return {true};
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &rect = rects[static_cast<std::size_t>(focus_)];
      InputEvent press{InputEventType::LeftPressed};
      press.position = {rect.x + rect.width * .5f,
                        rect.y + rect.height * .5f};
      InputEvent release = press;
      release.type = InputEventType::LeftReleased;
      const int keep = focus_;
      (void)handle(press, width, height);
      auto command = handle(release, width, height);
      if (visible_)
        focus_ = keep;
      command.captured = true;
      return command;
    }
    // Unhandled keys keep falling through to global shortcuts.
    return {};
  }
  if (!pointer(event.type))
    return {};
  if (event.type == InputEventType::LeftReleased ||
      event.type == InputEventType::RightReleased) {
    RosterCommand command;
    command.captured = pointer_owned_ || layout.panel.contains(event.position);
    if (event.type == InputEventType::LeftReleased &&
        pressed_target_ == PressTarget::close &&
        layout.close.contains(event.position)) {
      close();
      command.captured = true;
      return command;
    }
    if (event.type == InputEventType::LeftReleased &&
        pressed_target_ == PressTarget::refresh &&
        layout.refresh.contains(event.position)) {
      command.refresh = true;
      clear_press();
      return command;
    }
    if (event.type == InputEventType::LeftReleased &&
        pressed_target_ == PressTarget::row && pressed_row_ &&
        pressed_generation_ == view_.generation &&
        pressed_player_id_ == view_.player_id &&
        static_cast<std::size_t>(*pressed_row_) < display_order_.size() &&
        layout.list.contains(event.position) &&
        row_button(*pressed_row_, width, height).contains(event.position)) {
      const auto &row =
          view_.rows[static_cast<std::size_t>(display_order_[static_cast<std::size_t>(*pressed_row_)])];
      if (row.can_open) {
        command.open_colony_id = row.colony_id;
        command.generation = view_.generation;
        command.player_id = view_.player_id;
        command.body_id = row.body_id;
        command.system_id = row.system_id;
      } else
        notice_ = row.reason.empty()
                      ? tr("ROSTER_CANNOT_OPEN", "This colony cannot be opened yet.")
                                     : row.reason;
    }
    clear_press();
    return command;
  }
  if (pointer_owned_)
    return {true};
  if (!layout.panel.contains(event.position))
    return {};
  if (event.type == InputEventType::Wheel) {
    if (layout.list.contains(event.position))
      list_.scroll_to(list_.scroll_offset - event.wheel_y * 48.f * layout.scale);
    return {true};
  }
  if (event.type == InputEventType::LeftPressed) {
    pointer_owned_ = true;
    focus_ = -1;
    search_focused_ = layout.search.contains(event.position);
    if (search_focused_)
      return {true};
    if (layout.close.contains(event.position)) {
      pressed_target_ = PressTarget::close;
      return {true};
    }
    if (layout.refresh.contains(event.position)) {
      pressed_target_ = PressTarget::refresh;
      return {true};
    }
    if (const int column = header_column(event.position, layout); column >= 0) {
      static constexpr std::string_view ids[]{"name", "world", "population"};
      const auto &state = table_.sort_state();
      const bool ascending =
          !(state && state->first == ids[static_cast<std::size_t>(column)] &&
            state->second);
      table_.sort_by(ids[static_cast<std::size_t>(column)], ascending);
      apply_display_order();
      list_.scroll_offset = 0;
      return {true};
    }
    if (layout.list.contains(event.position))
      for (std::size_t i = 0; i < display_order_.size(); ++i)
        if (row_button(static_cast<int>(i), width, height)
                .contains(event.position)) {
          pressed_target_ = PressTarget::row;
          pressed_row_ = static_cast<int>(i);
          pressed_generation_ = view_.generation;
          pressed_player_id_ = view_.player_id;
          break;
        }
    return {true};
  }
  if (event.type == InputEventType::RightPressed)
    pointer_owned_ = true;
  return {true};
}

void RosterWorkspace::render(DrawList &out, int width, int height) const {
  if (!visible_)
    return;
  const auto layout = RosterLayout::for_viewport(width, height);
  const auto p = layout.panel;
  const int font = std::max(11, static_cast<int>(15.f * layout.scale));
  stellar::native_ui_style::menu_panel(out, p);
  text(out,
       {p.x + 16.f * layout.scale, p.y + 12.f * layout.scale,
        p.width - 520.f * layout.scale, 29.f * layout.scale},
       view_.developer_inspection
           ? tr("ROSTER_TITLE_ALL", "ALL COLONIES")
           : tr("ROSTER_TITLE_OWNED", "OWNED COLONIES"),
       std::max(15, static_cast<int>(23.f * layout.scale)),
       ink, p);
  out.overlay.emplace_back(FilledRectangle{layout.search, {3, 13, 22, 245}});
  out.overlay.emplace_back(StrokedRectangle{
      layout.search,
      search_focused_ ? cyan : Color{54, 111, 140, 255}});
  text(out,
       {layout.search.x + 8.f * layout.scale, layout.search.y + 5.f * layout.scale,
        layout.search.width - 16.f * layout.scale, 20.f * layout.scale},
       search_.empty()
           ? tr("ROSTER_SEARCH", "Search colonies…")
           : search_,
       font, search_.empty() ? muted : ink, layout.search);
  stellar::native_ui_style::panel(out, layout.refresh,
                                  layout.refresh.contains(pointer_), false);
  text(out,
       {layout.refresh.x + 8.f * layout.scale,
        layout.refresh.y + 5.f * layout.scale,
        layout.refresh.width - 16.f * layout.scale, 20.f * layout.scale},
       tr("ROSTER_REFRESH", "REFRESH"), font, cyan, layout.refresh);
  stellar::native_ui_style::panel(out, layout.close,
                                  layout.close.contains(pointer_), false);
  text(out,
       {layout.close.x + 8.f * layout.scale,
        layout.close.y + 4.f * layout.scale, 16.f * layout.scale,
        21.f * layout.scale},
       "X", font, ink, layout.close);
  text(out,
       {p.x + 16.f * layout.scale, p.y + 46.f * layout.scale,
        p.width - 32.f * layout.scale, 22.f * layout.scale},
       view_.message, font, view_.available ? muted : amber, p);
  if (!notice_.empty())
    text(out,
         {p.x + 16.f * layout.scale, p.y + 70.f * layout.scale,
          p.width - 32.f * layout.scale, 20.f * layout.scale},
         notice_, font - 1, amber, p);
  const bool compact =
      height <= 800 || layout.list.width < 650.f * layout.scale;
  const auto sort_mark = [&](std::string_view column) {
    const auto &state = table_.sort_state();
    return state && state->first == column ? (state->second ? " ^" : " v")
                                           : "";
  };
  if (compact) {
    text(out,
         {layout.list.x + layout.list.width * .70f,
          layout.list.y - 20.f * layout.scale, layout.list.width * .28f,
          17.f * layout.scale},
         tr("ROSTER_COL_POPULATION", "POPULATION") +
             sort_mark("population"),
         font - 2, muted, p);
  } else {
    text(out,
         {layout.list.x + 8.f * layout.scale,
          layout.list.y - 24.f * layout.scale, layout.list.width * .37f,
          20.f * layout.scale},
         tr("ROSTER_COL_COLONY", "COLONY / KIND") + sort_mark("name"),
         font - 2, muted, p);
    text(out,
         {layout.list.x + layout.list.width * .39f,
          layout.list.y - 24.f * layout.scale, layout.list.width * .38f,
          20.f * layout.scale},
         tr("ROSTER_COL_WORLD", "WORLD / SYSTEM") + sort_mark("world"),
         font - 2, muted, p);
    text(out,
         {layout.list.x + layout.list.width * .79f,
          layout.list.y - 24.f * layout.scale, layout.list.width * .18f,
          20.f * layout.scale},
         tr("ROSTER_COL_ACTION", "POPULATION / ACTION") +
             sort_mark("population"),
         font - 2, muted, p);
  }
  const float maximum = maximum_scroll(layout);
  for (std::size_t i = 0; i < display_order_.size(); ++i) {
    const auto box = row_button(static_cast<int>(i), width, height);
    const auto visible = clip_intersection(box, layout.list);
    if (visible.height <= 0)
      continue;
    const auto &row =
        view_.rows[static_cast<std::size_t>(display_order_[i])];
    const bool hover = layout.list.contains(pointer_) && box.contains(pointer_);
    out.overlay.emplace_back(FilledRectangle{
        visible, hover ? Color{16, 57, 76, 248} : Color{11, 29, 46, 246}});
    if (hover)
      out.overlay.emplace_back(StrokedRectangle{visible, {82, 155, 194, 230}});
    const Point glyph{box.x + 15.f * layout.scale, box.y + 17.f * layout.scale};
    const float radius = 5.f * layout.scale;
    if (glyph.y - radius >= layout.list.y &&
        glyph.y + radius <= layout.list.y + layout.list.height)
      out.overlay.emplace_back(FilledRectangle{
          {glyph.x - radius, glyph.y - radius, radius * 2.f, radius * 2.f},
          row.can_open ? cyan : amber});
    const float left = box.x + 28.f * layout.scale;
    if (compact) {
      text(out,
           {left, box.y + 6.f * layout.scale, box.width * .48f,
            20.f * layout.scale},
           row.name + "  /  " + row.kind_label, font, ink, layout.list);
      text(out,
           {left, box.y + 30.f * layout.scale, box.width * .57f,
            20.f * layout.scale},
           row.body_name + "  /  " + row.system_name, font - 1, muted,
           layout.list);
      text(out,
           {box.x + box.width * .70f, box.y + 30.f * layout.scale,
            box.width * .28f, 20.f * layout.scale},
           row.population + "  " +
               (row.can_open ? tr("ROSTER_ACTION_VIEW", "VIEW")
                             : tr("ROSTER_ACTION_UNAVAILABLE", "UNAVAILABLE")),
           font - 1, row.can_open ? cyan : amber, layout.list);
    } else {
      text(out,
           {left, box.y + 8.f * layout.scale, box.width * .34f,
            20.f * layout.scale},
           row.name, font, ink, layout.list);
      text(out,
           {left, box.y + 31.f * layout.scale, box.width * .34f,
            18.f * layout.scale},
           row.kind_label, font - 2, muted, layout.list);
      text(out,
           {box.x + box.width * .39f, box.y + 8.f * layout.scale,
            box.width * .36f, 20.f * layout.scale},
           row.body_name, font, ink, layout.list);
      text(out,
           {box.x + box.width * .39f, box.y + 31.f * layout.scale,
            box.width * .36f, 18.f * layout.scale},
           row.system_name, font - 2, muted, layout.list);
      text(out,
           {box.x + box.width * .79f, box.y + 8.f * layout.scale,
            box.width * .18f, 20.f * layout.scale},
           row.population, font, ink, layout.list);
      text(out,
           {box.x + box.width * .79f, box.y + 31.f * layout.scale,
            box.width * .18f, 18.f * layout.scale},
           row.can_open ? tr("ROSTER_ACTION_VIEW", "VIEW")
                        : tr("ROSTER_ACTION_UNAVAILABLE", "UNAVAILABLE"),
           font - 2,
           row.can_open ? cyan : amber, layout.list);
    }
  }
  if (view_.rows.empty())
    text(out,
         {layout.list.x + 8.f * layout.scale,
          layout.list.y + 12.f * layout.scale,
          layout.list.width - 16.f * layout.scale, 40.f * layout.scale},
         view_.available ? tr("ROSTER_LIST_EMPTY", "No owned colonies.")
                         : tr("ROSTER_LIST_UNAVAILABLE", "Roster unavailable."),
         font,
         muted, layout.list);
  if (maximum > 0) {
    const float thumb =
        std::max(24.f * layout.scale, layout.list.height * layout.list.height /
                                          (maximum + layout.list.height));
    const float y =
        layout.list.y + (layout.list.height - thumb) * list_.scroll_offset / maximum;
    out.overlay.emplace_back(
        FilledRectangle{{layout.list.x + layout.list.width + 5.f * layout.scale,
                         layout.list.y, 3.f * layout.scale, layout.list.height},
                        {29, 61, 78, 255}});
    out.overlay.emplace_back(
        FilledRectangle{{layout.list.x + layout.list.width + 5.f * layout.scale,
                         y, 3.f * layout.scale, thumb},
                        cyan});
  }
  if (focus_ >= 0) {
    const auto rects = focusables(layout);
    if (focus_ < static_cast<int>(rects.size()))
      out.overlay.emplace_back(StrokedRectangle{
          rects[static_cast<std::size_t>(focus_)], cyan});
  }
}
} // namespace stellar::native_colony_roster
