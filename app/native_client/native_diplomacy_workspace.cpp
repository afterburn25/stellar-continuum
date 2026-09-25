#include "native_diplomacy_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_style.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace stellar::native_diplomacy_ui {
namespace {
using namespace stellar::native_diplomacy;
using namespace stellar::native_map;

namespace theme = stellar::native_ui;
constexpr Color panel = theme::color::surface_opaque;
constexpr Color inset = theme::color::surface;
constexpr Color row = theme::color::surface_secondary;
constexpr Color hover = theme::color::surface_hover;
constexpr Color selected = theme::color::surface_raised;
constexpr Color border = theme::color::keyline_strong;
constexpr Color accent = theme::color::diplomacy;
constexpr Color gold = theme::color::economy;
constexpr Color bright = theme::color::text_primary;
constexpr Color muted = theme::color::text_secondary;
constexpr Color danger = theme::color::danger;

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void region(DrawList &out, UiRect bounds) {
  fill(out, bounds, theme::color::surface);
  stroke(out, bounds, theme::color::keyline);
}
void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, TextAlign align = TextAlign::Left) {
  const auto x = align == TextAlign::Center
                     ? bounds.x + bounds.width * .5f
                     : align == TextAlign::Right ? bounds.x + bounds.width
                                                 : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align});
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
[[nodiscard]] std::string visible_message(std::string value) {
  constexpr std::size_t limit = 200;
  if (value.size() <= limit) return value;
  auto end = limit - 3;
  while (end > 0 &&
         (static_cast<unsigned char>(value[end]) & 0xc0u) == 0x80u)
    --end;
  value.resize(end);
  value += "...";
  return value;
}

constexpr std::pair<NativeDiplomacyContactFilter, const char *>
    filter_labels[] = {
        {NativeDiplomacyContactFilter::all, "ALL"},
        {NativeDiplomacyContactFilter::identified, "IDENTIFIED"},
        {NativeDiplomacyContactFilter::unidentified, "UNIDENTIFIED"},
        {NativeDiplomacyContactFilter::cooperative, "COOPERATIVE"},
        {NativeDiplomacyContactFilter::neutral, "NEUTRAL"},
        {NativeDiplomacyContactFilter::hostile, "HOSTILE"},
        {NativeDiplomacyContactFilter::at_war, "AT WAR"},
        {NativeDiplomacyContactFilter::pending_proposal, "PENDING"},
        {NativeDiplomacyContactFilter::communication_available, "CHANNEL"},
};
constexpr const char *filter_keys[] = {
    "DIPLOMACY_FILTER_ALL",        "DIPLOMACY_FILTER_IDENTIFIED",
    "DIPLOMACY_FILTER_UNIDENTIFIED", "DIPLOMACY_FILTER_COOPERATIVE",
    "DIPLOMACY_FILTER_NEUTRAL",    "DIPLOMACY_FILTER_HOSTILE",
    "DIPLOMACY_FILTER_AT_WAR",     "DIPLOMACY_FILTER_PENDING",
    "DIPLOMACY_FILTER_CHANNEL",
};

constexpr std::pair<DiplomacyWorkspaceTab, const char *> tab_labels[] = {
    {DiplomacyWorkspaceTab::agreements, "AGREEMENTS"},
    {DiplomacyWorkspaceTab::proposals, "PROPOSALS"},
    {DiplomacyWorkspaceTab::history, "HISTORY"},
    {DiplomacyWorkspaceTab::intelligence, "INTELLIGENCE"},
    {DiplomacyWorkspaceTab::overview, "OVERVIEW"},
};
constexpr const char *tab_keys[] = {
    "DIPLOMACY_TAB_AGREEMENTS", "DIPLOMACY_TAB_PROPOSALS",
    "DIPLOMACY_TAB_HISTORY", "DIPLOMACY_TAB_INTELLIGENCE",
    "DIPLOMACY_TAB_OVERVIEW",
};

[[nodiscard]] UiRect filter_button(const DiplomacyWorkspaceLayout &layout,
                                   std::size_t index) noexcept {
  const auto s = layout.scale;
  const auto columns = 3;
  const auto gap = 4.f * s;
  const auto available =
      layout.contact_panel.width - 16.f * s - gap * (columns - 1);
  const auto first = (index / columns) * columns;
  const auto weight = [](std::size_t item) {
    return static_cast<float>(std::string_view(filter_labels[item].second).size() + 3);
  };
  float total_weight = 0.f, preceding_weight = 0.f;
  for (auto item = first; item < first + columns; ++item) {
    total_weight += weight(item);
    if (item < index) preceding_weight += weight(item);
  }
  // Longer filter labels receive more room without shrinking 720p text.
  const auto width = available * weight(index) / total_weight;
  const auto column = static_cast<float>(index % columns);
  const auto band = static_cast<float>(index / columns);
  return {layout.contact_panel.x + 8.f * s + available * preceding_weight / total_weight + column * gap,
          layout.contact_panel.y + 34.f * s + band * (26.f * s + gap), width,
          26.f * s};
}

[[nodiscard]] float filter_area_height(const DiplomacyWorkspaceLayout &layout) {
  const auto rows = (std::size(filter_labels) + 2) / 3;
  return 34.f * layout.scale +
         static_cast<float>(rows) * (26.f + 4.f) * layout.scale;
}

[[nodiscard]] UiRect contact_row(const DiplomacyWorkspaceLayout &layout,
                                 std::size_t index, float scroll) noexcept {
  const auto s = layout.scale;
  return {layout.contact_rows.x + 4.f * s,
          layout.contact_rows.y + 4.f * s +
              static_cast<float>(index) * 62.f * s - scroll,
          layout.contact_rows.width - 8.f * s, 58.f * s};
}

[[nodiscard]] UiRect tab_button(const DiplomacyWorkspaceLayout &layout,
                                std::size_t index) noexcept {
  const auto gap = 8.f * layout.scale;
  const auto width = (layout.tabs.width - gap * 4.f) / 5.f;
  return {layout.tabs.x + static_cast<float>(index) * (width + gap),
          layout.tabs.y, width, layout.tabs.height};
}

[[nodiscard]] UiRect action_button(const DiplomacyWorkspaceLayout &layout,
                                   std::size_t index) noexcept {
  const auto s = layout.scale;
  return {layout.actions.x + 8.f * s,
          layout.actions.y + 8.f * s + static_cast<float>(index) * 36.f * s,
          layout.actions.width - 16.f * s, 30.f * s};
}

[[nodiscard]] UiRect modal_term_button(const DiplomacyWorkspaceLayout &layout,
                                       std::size_t index) noexcept {
  const auto s = layout.scale;
  return {layout.modal_panel.x + 16.f * s,
          layout.modal_panel.y + 74.f * s + static_cast<float>(index) * 42.f * s,
          layout.modal_panel.width - 32.f * s, 36.f * s};
}

[[nodiscard]] UiRect modal_confirm_button(
    const DiplomacyWorkspaceLayout &layout) noexcept {
  const auto s = layout.scale;
  return {layout.modal_panel.x + 16.f * s,
          layout.modal_panel.y + layout.modal_panel.height - 92.f * s,
          layout.modal_panel.width - 32.f * s, 36.f * s};
}

[[nodiscard]] UiRect modal_cancel_button(
    const DiplomacyWorkspaceLayout &layout) noexcept {
  const auto s = layout.scale;
  return {layout.modal_panel.x + 16.f * s,
          layout.modal_panel.y + layout.modal_panel.height - 48.f * s,
          layout.modal_panel.width - 32.f * s, 36.f * s};
}

// Proposal card buttons inside the scrolled details region. Card heights are
// fixed so hit-testing and rendering share one geometry.
[[nodiscard]] float proposal_card_height(
    const DiplomacyWorkspaceLayout &layout) noexcept {
  return 108.f * layout.scale;
}
[[nodiscard]] UiRect proposal_button(const DiplomacyWorkspaceLayout &layout,
                                     std::size_t card, int which,
                                     float scroll) noexcept {
  const auto s = layout.scale;
  const auto top = layout.detail_rows.y + 8.f * s +
                   static_cast<float>(card) *
                       (proposal_card_height(layout) + 8.f * s) -
                   scroll;
  const auto gap = 6.f * s;
  const auto width =
      (layout.detail_rows.width - 32.f * s - gap * 2.f) / 3.f;
  return {layout.detail_rows.x + 16.f * s +
              static_cast<float>(which) * (width + gap),
          top + proposal_card_height(layout) - 40.f * s, width, 32.f * s};
}

[[nodiscard]] std::string species_portrait_path(std::string_view species_id) {
  auto asset_id = std::string(species_id);
  std::ranges::replace(asset_id, '_', '-');
  return "assets/visual/species/" + asset_id + "-communications-v2.png";
}

} // namespace

DiplomacyWorkspaceLayout DiplomacyWorkspaceLayout::for_viewport(
    int width, int height) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1280.f, h / 700.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 14.f * scale;
  const auto left_margin = native_navigation_content_left * scale;
  const auto top = native_workspace_top(width,height);
  const UiRect surface{left_margin, top,
                       std::max(1.f, w - left_margin - margin),
                       std::max(1.f, h - top - margin)};
  const auto inner_x = surface.x + 14.f * scale;
  const auto inner_y = surface.y + 54.f * scale;
  const auto inner_w = surface.width - 28.f * scale;
  const auto inner_h = surface.height - 68.f * scale;
  const auto gap = 10.f * scale;
  const auto left_w = std::clamp(inner_w * .22f, 216.f * scale, 300.f * scale);
  const auto right_w = std::clamp(inner_w * .24f, 244.f * scale, 330.f * scale);
  const auto center_w =
      std::max(240.f * scale, inner_w - left_w - right_w - gap * 2.f);
  const auto columns_h = inner_h * .56f;
  const UiRect contact_panel{inner_x, inner_y, left_w, columns_h};
  const UiRect stage{contact_panel.x + contact_panel.width + gap, inner_y,
                     center_w, columns_h};
  const UiRect meter_panel{stage.x + stage.width + gap, inner_y, right_w,
                           columns_h};
  const auto filter_h = [&] {
    const auto rows = (std::size(filter_labels) + 2) / 3;
    return 34.f * scale + static_cast<float>(rows) * 30.f * scale;
  }();
  const UiRect contact_rows{contact_panel.x,
                            contact_panel.y + filter_h, contact_panel.width,
                            std::max(20.f * scale,
                                     contact_panel.height - filter_h - 8.f * scale)};
  const UiRect stage_caption{stage.x + 12.f * scale,
                             stage.y + stage.height - 96.f * scale,
                             stage.width - 24.f * scale, 88.f * scale};
  const UiRect meters{meter_panel.x + 8.f * scale, meter_panel.y + 34.f * scale,
                      meter_panel.width - 16.f * scale, 152.f * scale};
  // Anchored to the panel bottom so action buttons can never overlap the tab
  // strip below the columns at any viewport scale.
  const auto action_area_h =
      std::max(30.f * scale,
               std::min(160.f * scale, meter_panel.height - 40.f * scale -
                                           meters.height - 6.f * scale));
  const UiRect actions{meter_panel.x,
                       meter_panel.y + meter_panel.height - action_area_h,
                       meter_panel.width, action_area_h};
  const auto tabs_y = inner_y + columns_h + gap;
  const UiRect tabs{inner_x, tabs_y, inner_w, 34.f * scale};
  const auto detail_y = tabs.y + tabs.height + gap;
  const auto feedback_h = 34.f * scale;
  const UiRect feedback{inner_x, inner_y + inner_h - feedback_h, inner_w,
                        feedback_h};
  const UiRect detail_rows{inner_x, detail_y, inner_w,
                           std::max(30.f * scale,
                                    feedback.y - detail_y - 6.f * scale)};
  const auto modal_w = std::min(560.f * scale, w - 40.f * scale);
  const auto modal_h = std::min(480.f * scale, h - 80.f * scale);
  const UiRect modal_panel{(w - modal_w) * .5f, (h - modal_h) * .5f, modal_w,
                           modal_h};
  return {scale,
          static_cast<int>(std::lround(26.f * scale)),
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(12.f * scale)),
          surface,
          {inner_x, surface.y + 14.f * scale, surface.width * .5f,
           32.f * scale},
          {inner_x + inner_w - 260.f * scale,
           surface.y + 18.f * scale, 160.f * scale, 24.f * scale},
          {surface.x + surface.width - 100.f * scale,
           surface.y + 14.f * scale, 86.f * scale, 32.f * scale},
          contact_panel,
          contact_rows,
          stage,
          stage_caption,
          meter_panel,
          meters,
          actions,
          tabs,
          detail_rows,
          feedback,
          modal_panel};
}

std::string NativeDiplomacyWorkspace::tr(std::string_view key,
                                         std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeDiplomacyWorkspace::trf(
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

void NativeDiplomacyWorkspace::open() noexcept {
  visible_ = true;
  focus_ = -1;
}
void NativeDiplomacyWorkspace::close() noexcept {
  visible_ = false;
  modal_.reset();
  focus_ = -1;
}
bool NativeDiplomacyWorkspace::visible() const noexcept { return visible_; }
void NativeDiplomacyWorkspace::set_view(NativeDiplomacyView view) {
  auto previous_selection = selected_contact_id_;
  if (!previous_selection && view_ && view_->selected.present &&
      view_->selected.contact_index < view_->contacts.size())
    previous_selection =
        view_->contacts[view_->selected.contact_index].contact_id;
  const auto quote_changed =
      modal_ && view_ &&
      (view_->campaign_generation != view.campaign_generation ||
       view_->diplomacy_revision != view.diplomacy_revision);
  view_ = std::move(view);
  if (previous_selection) {
    const auto found = std::ranges::find_if(
        view_->contacts, [&](const auto &contact) {
          return contact.contact_id == *previous_selection;
        });
    if (found != view_->contacts.end()) {
      selected_contact_index_ = found->source_index;
      selected_contact_id_ = found->contact_id;
    }
  }
  reconcile_selection();
  if (quote_changed) {
    modal_.reset();
    set_notice(tr("DIPLOMACY_STATE_CHANGED",
                  "Diplomacy state changed. Reopen the action to review "
                  "current terms."),
               false);
  }
}
void NativeDiplomacyWorkspace::discard_campaign() {
  view_.reset();
  selected_contact_index_ = 0;
  selected_contact_id_.reset();
  filter_ = NativeDiplomacyContactFilter::all;
  tab_ = DiplomacyWorkspaceTab::agreements;
  modal_.reset();
  notice_.clear();
  contact_scroll_ = {}; detail_scroll_ = {};
  focus_ = -1;
}
void NativeDiplomacyWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = visible_message(std::move(message));
  notice_accepted_ = accepted;
}
bool NativeDiplomacyWorkspace::modal_open() const noexcept {
  return modal_.has_value();
}
void NativeDiplomacyWorkspace::dismiss_modal() noexcept { modal_.reset(); }
const std::optional<NativeDiplomacyView> &
NativeDiplomacyWorkspace::view() const noexcept {
  return view_;
}
std::size_t NativeDiplomacyWorkspace::selected_contact_index() const noexcept {
  return selected_contact_index_;
}
bool NativeDiplomacyWorkspace::select_contact_civilization(int civilization_id) {
  if (!view_) return false;
  const auto found = std::ranges::find_if(view_->contacts, [&](const auto& contact) {
    return contact.civilization_id == civilization_id;
  });
  if (found == view_->contacts.end()) return false;
  filter_ = NativeDiplomacyContactFilter::all;
  selected_contact_index_ = found->source_index;
  reconcile_selection();
  return true;
}
const std::string &NativeDiplomacyWorkspace::notice() const noexcept {
  return notice_;
}

void NativeDiplomacyWorkspace::reconcile_selection() {
  if (!view_ || view_->contacts.empty()) {
    selected_contact_index_ = 0;
    selected_contact_id_.reset();
    return;
  }
  const auto selected_contact = std::ranges::find_if(
      view_->contacts, [&](const auto &contact) {
        return contact.source_index == selected_contact_index_;
      });
  if (selected_contact != view_->contacts.end()) {
    selected_contact_id_ = selected_contact->contact_id;
    return;
  }
  const auto &fallback = view_->contacts.front();
  selected_contact_index_ = fallback.source_index;
  selected_contact_id_ = fallback.contact_id;
}
float NativeDiplomacyWorkspace::detail_content_height(
    const DiplomacyWorkspaceLayout &layout) const noexcept {
  if (!view_) return 0.f;
  const auto s = layout.scale;
  const auto card_bottom = [s](std::size_t index, float height) {
    return 8.f * s + static_cast<float>(index) * (height + 8.f * s) +
           height;
  };
  float content = 0.f;
  switch (tab_) {
  case DiplomacyWorkspaceTab::agreements: {
    std::size_t index = 0;
    if (view_->selected.present && view_->selected.target_civilization_id)
      content = std::max(content, card_bottom(index++, 52.f * s));
    if (view_->agreements.empty())
      content = std::max(content, card_bottom(index, 64.f * s));
    else
      for (; index < view_->agreements.size() +
                           (view_->selected.present &&
                                    view_->selected.target_civilization_id
                                ? 1u
                                : 0u);
           ++index)
        content = std::max(content, card_bottom(index, 64.f * s));
    break;
  }
  case DiplomacyWorkspaceTab::proposals:
    content = card_bottom(view_->proposals.empty() ? 0 :
                                                    view_->proposals.size() - 1,
                          view_->proposals.empty() ? 64.f * s : 108.f * s);
    break;
  case DiplomacyWorkspaceTab::history:
    content = card_bottom(view_->history.empty() ? 0 : view_->history.size() - 1,
                          view_->history.empty() ? 64.f * s : 62.f * s);
    break;
  case DiplomacyWorkspaceTab::intelligence:
    if (view_->contacts.empty())
      content = card_bottom(0, 64.f * s);
    else if (view_->selected.present) {
      const auto &contact = view_->contacts[view_->selected.contact_index];
      content = 8.f * s + 62.f * s + 8.f * s +
                (contact.last_observed_system_id ? 34.f * s + 8.f * s : 0.f) +
                70.f * s;
    }
    break;
  case DiplomacyWorkspaceTab::overview:
    content = card_bottom(view_->contacts.empty() ? 0 : view_->contacts.size() - 1,
                          view_->contacts.empty() ? 64.f * s : 44.f * s);
    break;
  }
  return content + 8.f * s;
}
std::vector<const NativeDiplomacyContact *>
NativeDiplomacyWorkspace::filtered_contacts() const {
  if (!view_) return {};
  return filter_native_diplomacy_contacts(*view_, filter_);
}

// Focusables walk actionable rects in (y,x) order: header close, the filter
// grid, contact rows clipped to their viewport, the conditional action
// buttons, the tab strip, and the detail region's buttons (proposal card
// actions, the intelligence FOCUS link) — narrowed to the modal's own
// controls while one is open. Inert surfaces never focus.
std::vector<NativeDiplomacyWorkspace::FocusRect>
NativeDiplomacyWorkspace::focusables(
    const DiplomacyWorkspaceLayout &layout) const {
  std::vector<FocusRect> out;
  if (modal_) {
    if (modal_->negotiation)
      for (std::size_t index = 0; index < modal_->terms.size(); ++index)
        out.push_back({modal_term_button(layout, index),
                       modal_->terms[index].first});
    else
      out.push_back({modal_confirm_button(layout), modal_->confirm_label});
    out.push_back({modal_cancel_button(layout),
                   tr("SETTINGS_CANCEL", "Cancel")});
  } else {
    out.push_back({layout.close, tr("DIPLOMACY_RETURN", "RETURN")});
    for (std::size_t index = 0; index < std::size(filter_labels); ++index)
      out.push_back({filter_button(layout, index),
                     tr(filter_keys[index], filter_labels[index].second)});
    const auto rows = filtered_contacts();
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto row_rect =
          contact_row(layout, index, contact_scroll_.scroll_offset);
      if (const auto clipped = intersection(row_rect, layout.contact_rows))
        out.push_back(
            {*clipped, rows[index]->display_name, row_rect, /*scroll_lane=*/1});
    }
    const auto &sel = view_->selected;
    if (sel.present) {
      // Same enumeration order as the click dispatch and renderer.
      std::size_t action_index = 0;
      const bool transmission =
          sel.has_visible_communication || sel.can_attempt_communication;
      const bool negotiate = sel.can_offer_non_aggression ||
                             sel.can_request_access || sel.can_offer_peace ||
                             sel.can_offer_ceasefire || sel.can_set_access;
      const std::string action_names[] = {
          sel.has_visible_communication
              ? tr("DIPLOMACY_OPEN_TRANSMISSION", "Open transmission")
              : tr("DIPLOMACY_ESTABLISH_COMMUNICATION",
                   "Establish communication"),
          tr("DIPLOMACY_NEGOTIATE", "Negotiate"),
          tr("DIPLOMACY_DECLARE_WAR_ACTION", "Declare war")};
      std::size_t which = 0;
      for (const bool enabled : {transmission, negotiate, sel.can_declare_war}) {
        if (enabled)
          out.push_back({action_button(layout, action_index),
                         action_names[which]});
        ++which;
        ++action_index;
      }
    }
    for (std::size_t index = 0; index < std::size(tab_labels); ++index)
      out.push_back({tab_button(layout, index),
                     tr(tab_keys[index], tab_labels[index].second)});
    if (tab_ == DiplomacyWorkspaceTab::proposals) {
      const char *const proposal_names[] = {"DIPLOMACY_ACCEPT",
                                            "DIPLOMACY_REJECT",
                                            "DIPLOMACY_WITHDRAW"};
      const char *const proposal_fallbacks[] = {"Accept", "Reject",
                                                "Withdraw"};
      for (std::size_t card = 0; card < view_->proposals.size(); ++card) {
        const auto &proposal = view_->proposals[card];
        const bool legal[] = {proposal.can_accept, proposal.can_reject,
                              proposal.can_withdraw};
        for (int which = 0; which < 3; ++which)
          if (legal[which]) {
            const auto button = proposal_button(layout, card, which,
                                                detail_scroll_.scroll_offset);
            if (const auto clipped =
                    intersection(button, layout.detail_rows))
              out.push_back(
                  {*clipped,
                   tr(proposal_names[which], proposal_fallbacks[which]),
                   button, /*scroll_lane=*/2});
          }
      }
    }
    if (tab_ == DiplomacyWorkspaceTab::intelligence && sel.present) {
      const auto &contact = view_->contacts[sel.contact_index];
      if (contact.last_observed_system_id) {
        const auto s = layout.scale;
        const UiRect link{layout.detail_rows.x + 16.f * s,
                          layout.detail_rows.y + 78.f * s - detail_scroll_.scroll_offset,
                          layout.detail_rows.width - 32.f * s, 34.f * s};
        if (const auto clipped = intersection(link, layout.detail_rows))
          out.push_back(
              {*clipped,
               trf("DIPLOMACY_LAST_OBSERVATION",
                   {contact.last_observed_system_name},
                   "Last observation · {0}"),
               link, /*scroll_lane=*/2});
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const FocusRect &a, const FocusRect &b) {
    return a.bounds.y == b.bounds.y ? a.bounds.x < b.bounds.x
                                    : a.bounds.y < b.bounds.y;
  });
  return out;
}

std::string NativeDiplomacyWorkspace::focused_label(int width,
                                                    int height) const {
  if (focus_ < 0) return {};
  const auto items =
      focusables(DiplomacyWorkspaceLayout::for_viewport(width, height));
  return focus_ < static_cast<int>(items.size())
             ? items[static_cast<std::size_t>(focus_)].label
             : std::string{};
}
std::optional<stellar::native_map::UiRect>
NativeDiplomacyWorkspace::focused_bounds(int width, int height) const {
  if (focus_ < 0) return std::nullopt;
  const auto items =
      focusables(DiplomacyWorkspaceLayout::for_viewport(width, height));
  return focus_ < static_cast<int>(items.size())
             ? std::optional<stellar::native_map::UiRect>{
                   items[static_cast<std::size_t>(focus_)].bounds}
             : std::nullopt;
}

DiplomacyWorkspaceCommand NativeDiplomacyWorkspace::handle(
    const InputEvent &event, int width, int height) {
  if (!visible_ || !view_) return {};
  pointer_ = event.position;
  const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    focus_ = -1;
    return {};
  }

  if (event.type == InputEventType::KeyPressed && event.key) {
    // SDL_Keycode: Tab/arrows walk the (y,x)-ordered focusables, Home/End
    // jump to the ends, and Return/Space replay the click at the focused
    // rect through the same dispatch pointer input takes.
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
    // Rows clipped by a scroll viewport stay in the ring; when focus lands
    // on a clipped row, snap its lane so the row is fully visible — which
    // exposes the next row and keeps the whole list keyboard-reachable.
    const auto snap_focused = [&] {
      if (focus_ < 0 || focus_ >= count) return;
      const auto &target = rects[static_cast<std::size_t>(focus_)];
      if (!target.unclipped) return;
      if (target.scroll_lane == 1) {
        contact_scroll_.sync(filtered_contacts().size() * 62.f *
                                     layout.scale +
                                 12.f * layout.scale,
                             layout.contact_rows.height);
        contact_scroll_.scroll_interval_into_view(
            target.unclipped->y, target.unclipped->y + target.unclipped->height,
            layout.contact_rows.y,
            layout.contact_rows.y + layout.contact_rows.height);
      } else if (target.scroll_lane == 2) {
        detail_scroll_.sync(detail_content_height(layout),
                            layout.detail_rows.height);
        detail_scroll_.scroll_interval_into_view(
            target.unclipped->y, target.unclipped->y + target.unclipped->height,
            layout.detail_rows.y,
            layout.detail_rows.y + layout.detail_rows.height);
      }
    };
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      snap_focused();
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (count > 0 && (fwd || bwd)) {
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      snap_focused();
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &rect = rects[static_cast<std::size_t>(focus_)].bounds;
      InputEvent press{InputEventType::LeftPressed};
      press.position = {rect.x + rect.width * .5f,
                        rect.y + rect.height * .5f};
      const int keep = focus_;
      auto command = handle(press, width, height);
      if (visible_ && !modal_) focus_ = keep;
      command.captured = true;
      return command;
    }
    // Unhandled keys keep falling through to global shortcuts.
    return {};
  }

  if (event.type == InputEventType::Wheel) {
    if (layout.contact_rows.contains(event.position)) {
      const auto rows = filtered_contacts();
      contact_scroll_.sync(
          rows.size() * 62.f * layout.scale + 12.f * layout.scale,
          layout.contact_rows.height);
      contact_scroll_.scroll_by(-event.wheel_y * 26.f);
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (layout.detail_rows.contains(event.position)) {
      detail_scroll_.sync(detail_content_height(layout),
                          layout.detail_rows.height);
      detail_scroll_.scroll_by(-event.wheel_y * 26.f);
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (layout.surface.contains(event.position))
      return {DiplomacyWorkspaceCommandKind::None, true};
    return {};
  }

  if (event.type != InputEventType::LeftPressed) return {};

  if (layout.surface.contains(event.position) ||
      layout.modal_panel.contains(event.position))
    focus_ = -1;

  if (modal_) {
    const auto &modal = *modal_;
    if (modal.negotiation) {
      if (modal_cancel_button(layout).contains(event.position)) {
        modal_.reset();
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
      for (std::size_t index = 0; index < modal.terms.size(); ++index) {
        if (modal_term_button(layout, index).contains(event.position)) {
          auto next = ModalState{};
          next.title = modal.terms[index].first;
          next.description = trf("DIPLOMACY_COUNTERPART",
                                 {view_->selected.contact_name},
                                 "Counterpart: {0}");
          next.action = modal.terms[index].second;
          next.target_civilization_id = modal.target_civilization_id;
          next.campaign_generation = modal.campaign_generation;
          next.diplomacy_revision = modal.diplomacy_revision;
          next.confirm_label = next.action ==
                                           DiplomacyWorkspaceAction::grant_access ||
                                       next.action ==
                                           DiplomacyWorkspaceAction::deny_access
                                   ? tr("DIPLOMACY_APPLY_ACCESS", "APPLY ACCESS")
                                   : tr("DIPLOMACY_SEND_PROPOSAL",
                                        "SEND PROPOSAL");
          modal_ = std::move(next);
          return {DiplomacyWorkspaceCommandKind::None, true};
        }
      }
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (modal_confirm_button(layout).contains(event.position)) {
      DiplomacyWorkspaceCommand command{DiplomacyWorkspaceCommandKind::Action,
                                        true};
      command.action = modal.action;
      command.target_civilization_id = modal.target_civilization_id;
      command.campaign_generation = modal.campaign_generation;
      command.diplomacy_revision = modal.diplomacy_revision;
      modal_.reset();
      return command;
    }
    if (modal_cancel_button(layout).contains(event.position)) {
      modal_.reset();
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    return {DiplomacyWorkspaceCommandKind::None, true};
  }

  if (layout.close.contains(event.position))
    return {DiplomacyWorkspaceCommandKind::Close, true};

  for (std::size_t index = 0; index < std::size(filter_labels); ++index) {
    if (filter_button(layout, index).contains(event.position)) {
      filter_ = filter_labels[index].first;
      contact_scroll_ = {};
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
  }
  for (std::size_t index = 0; index < std::size(tab_labels); ++index) {
    if (tab_button(layout, index).contains(event.position)) {
      tab_ = tab_labels[index].first;
      detail_scroll_ = {};
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
  }

  const auto rows = filtered_contacts();
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto bounds = contact_row(layout, index, contact_scroll_.scroll_offset);
    const auto clipped = intersection(bounds, layout.contact_rows);
    if (clipped && clipped->contains(event.position)) {
      selected_contact_index_ = rows[index]->source_index;
      selected_contact_id_ = rows[index]->contact_id;
      return {DiplomacyWorkspaceCommandKind::SelectContact, true,
              selected_contact_index_};
    }
  }

  const auto &s = view_->selected;
  std::size_t action_index = 0;
  const auto action_hit = [&](std::size_t index) {
    return s.present && index < 8 &&
           action_button(layout, index).contains(event.position);
  };
  if (s.present) {
    const bool transmission =
        s.has_visible_communication || s.can_attempt_communication;
    const bool negotiate =
        s.can_offer_non_aggression || s.can_request_access ||
        s.can_offer_peace || s.can_offer_ceasefire || s.can_set_access;
    if (transmission && action_hit(action_index)) {
      if (s.has_visible_communication) {
        set_notice(tr("DIPLOMACY_CHANNEL_OPEN",
                      "Channel open. Select a proposal to begin negotiations."),
                   true);
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
      DiplomacyWorkspaceCommand command{
          DiplomacyWorkspaceCommandKind::Action, true};
      command.action = DiplomacyWorkspaceAction::establish_communication;
      command.target_civilization_id = s.target_civilization_id;
      command.campaign_generation = view_->campaign_generation;
      command.diplomacy_revision = view_->diplomacy_revision;
      return command;
    }
    ++action_index;
    if (negotiate) {
      if (action_hit(action_index)) {
        ModalState modal;
        modal.negotiation = true;
        modal.title = tr("DIPLOMACY_NEGOTIATION", "NEGOTIATION");
        modal.description = tr("DIPLOMACY_NEGOTIATION_HINT",
                               "Choose the agreement you want to propose.");
        modal.target_civilization_id = s.target_civilization_id;
        modal.campaign_generation = view_->campaign_generation;
        modal.diplomacy_revision = view_->diplomacy_revision;
        const auto add = [&](const char *name,
                             DiplomacyWorkspaceAction action, bool legal) {
          if (legal) modal.terms.emplace_back(name, action);
        };
        add(tr("DIPLOMACY_TERM_NON_AGGRESSION", "Non-aggression").c_str(),
            DiplomacyWorkspaceAction::propose_non_aggression,
            s.can_offer_non_aggression);
        add(tr("DIPLOMACY_TERM_REQUEST_ACCESS", "Request transit access")
                .c_str(),
            DiplomacyWorkspaceAction::request_access, s.can_request_access);
        add(tr("DIPLOMACY_TERM_CEASEFIRE", "Ceasefire").c_str(),
            DiplomacyWorkspaceAction::offer_ceasefire, s.can_offer_ceasefire);
        add(tr("DIPLOMACY_TERM_PEACE", "Peace").c_str(),
            DiplomacyWorkspaceAction::offer_peace, s.can_offer_peace);
        add(tr("DIPLOMACY_TERM_GRANT_ACCESS", "Grant transit access").c_str(),
            DiplomacyWorkspaceAction::grant_access, s.can_set_access);
        add(tr("DIPLOMACY_TERM_DENY_ACCESS", "Deny transit access").c_str(),
            DiplomacyWorkspaceAction::deny_access, s.can_set_access);
        modal_ = std::move(modal);
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
    }
    ++action_index;
    if (s.can_declare_war) {
      if (action_hit(action_index)) {
        ModalState modal;
        modal.title = trf("DIPLOMACY_DECLARE_WAR_ON", {s.contact_name},
                          "DECLARE WAR ON {0}");
        modal.description = tr(
            "DIPLOMACY_DECLARE_WAR_WARNING",
            "Your civilizations will enter a state of war. Active agreements "
            "may be affected.");
        modal.action = DiplomacyWorkspaceAction::declare_war;
        modal.target_civilization_id = s.target_civilization_id;
        modal.campaign_generation = view_->campaign_generation;
        modal.diplomacy_revision = view_->diplomacy_revision;
        modal.danger = true;
        modal.confirm_label = tr("DIPLOMACY_DECLARE_WAR", "DECLARE WAR");
        modal_ = std::move(modal);
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
    }
  }

  if (tab_ == DiplomacyWorkspaceTab::proposals) {
    for (std::size_t card = 0; card < view_->proposals.size(); ++card) {
      const auto &proposal = view_->proposals[card];
      const std::pair<bool, DiplomacyWorkspaceAction> buttons[] = {
          {proposal.can_accept, DiplomacyWorkspaceAction::accept_proposal},
          {proposal.can_reject, DiplomacyWorkspaceAction::reject_proposal},
          {proposal.can_withdraw, DiplomacyWorkspaceAction::withdraw_proposal}};
      for (int which = 0; which < 3; ++which) {
        if (!buttons[which].first) continue;
        const auto clipped = intersection(
            proposal_button(layout, card, which, detail_scroll_.scroll_offset),
            layout.detail_rows);
        if (clipped && clipped->contains(event.position)) {
          DiplomacyWorkspaceCommand command{
              DiplomacyWorkspaceCommandKind::ProposalAction, true};
          command.action = buttons[which].second;
          command.proposal_id = proposal.proposal_id;
          command.campaign_generation = view_->campaign_generation;
          command.diplomacy_revision = view_->diplomacy_revision;
          return command;
        }
      }
    }
  }
  if (tab_ == DiplomacyWorkspaceTab::intelligence && s.present) {
    const auto &contact = view_->contacts[s.contact_index];
    if (contact.last_observed_system_id) {
      const auto s2 = layout.scale;
      const UiRect focus{layout.detail_rows.x + 16.f * s2,
                         layout.detail_rows.y + 78.f * s2 - detail_scroll_.scroll_offset,
                         layout.detail_rows.width - 32.f * s2, 34.f * s2};
      if (const auto clipped = intersection(focus, layout.detail_rows);
          clipped && clipped->contains(event.position))
        return {DiplomacyWorkspaceCommandKind::FocusSystem, true, 0,
                std::nullopt, std::nullopt,
                DiplomacyWorkspaceAction::establish_communication,
                *contact.last_observed_system_id};
    }
  }

  if (layout.surface.contains(event.position))
    return {DiplomacyWorkspaceCommandKind::None, true};
  return {};
}

void NativeDiplomacyWorkspace::render(
    DrawList &out, int width, int height,
    const PortraitProvider *portrait_provider) const {
  if (!visible_ || !view_) return;
  const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
  const auto s = layout.scale;
  stellar::native_ui_style::menu_panel(out, layout.surface);
  text(out, layout.title, tr("DIPLOMACY_TITLE", "RELATIONS"), bright,
       layout.title_font_pixels);
  text(out, layout.date, view_->date, muted, layout.body_font_pixels,
       TextAlign::Right);
  theme::button(out, layout.close, tr("DIPLOMACY_RETURN", "RETURN"), pointer_,
                layout.small_font_pixels);

  // Contact directory
  region(out, layout.contact_panel);
  theme::section_header(out,
                        {layout.contact_panel.x + 8.f * s,
                         layout.contact_panel.y + 8.f * s,
                         layout.contact_panel.width - 16.f * s, 22.f * s},
                        tr("DIPLOMACY_CONTACT_DIRECTORY", "CONTACT DIRECTORY"),
                        layout.small_font_pixels, theme::Tone::Diplomacy);
  for (std::size_t index = 0; index < std::size(filter_labels); ++index) {
    const auto bounds = filter_button(layout, index);
    const bool active = filter_ == filter_labels[index].first;
    theme::button(out, bounds, tr(filter_keys[index], filter_labels[index].second),
                  pointer_, layout.small_font_pixels, theme::Tone::Neutral,
                  active);
  }
  region(out, layout.contact_rows);
  const auto rows = filtered_contacts();
  if (rows.empty()) {
    text(out, layout.contact_rows,
         view_->contacts.empty()
             ? tr("DIPLOMACY_NO_CONTACTS",
                  "No contacts yet. Send scout ships into unexplored systems "
                  "to discover other civilizations.")
             : tr("DIPLOMACY_NO_FILTER_MATCH",
                  "No contacts match this filter."),
         muted, layout.body_font_pixels);
  }
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto bounds = contact_row(layout, index, contact_scroll_.scroll_offset);
    if (bounds.y >= layout.contact_rows.y + layout.contact_rows.height ||
        bounds.y + bounds.height <= layout.contact_rows.y)
      continue;
    const auto &contact = *rows[index];
    const bool chosen = contact.source_index == selected_contact_index_;
    const auto clip = intersection(bounds, layout.contact_rows);
    if (!clip) continue;
    fill(out, *clip,
         chosen ? selected : bounds.contains(pointer_) ? hover : row);
    if (chosen)
      fill(out, {clip->x, clip->y, 3.f * s, clip->height},
           theme::color::selected);
    const auto clipped_text = [&](float y, std::string value, Color color,
                                  int pixels) {
      out.overlay.emplace_back(Text{{bounds.x + 8.f * s, y}, std::move(value),
                                    color, pixels, bounds.width - 16.f * s,
                                    *clip});
    };
    clipped_text(bounds.y + 5.f * s, contact.display_name, bright,
                 layout.body_font_pixels);
    clipped_text(bounds.y + 23.f * s, contact.status, gold,
                 layout.small_font_pixels);
    clipped_text(bounds.y + 39.f * s,
                 contact.identified
                     ? contact.communication
                     : trf("DIPLOMACY_IDENTITY_CONFIDENCE",
                           {std::to_string(static_cast<int>(std::lround(
                               contact.confidence * 100.)))},
                           "Identity confidence {0}%"),
                 muted, layout.small_font_pixels);
  }

  const auto &sel = view_->selected;
  // Transmission stage: portrait for identified contacts, signal arcs otherwise.
  region(out, layout.stage);
  text(out,
       {layout.stage.x + 10.f * s, layout.stage.y + 6.f * s,
        layout.stage.width - 20.f * s, 20.f * s},
       sel.has_visible_communication
           ? tr("DIPLOMACY_CHANNEL_AVAILABLE", "COMMUNICATION CHANNEL AVAILABLE")
           : tr("DIPLOMACY_CHANNEL_UNAVAILABLE", "COMMUNICATION UNAVAILABLE"),
       sel.has_visible_communication ? accent : muted,
       layout.small_font_pixels);
  const UiRect portrait_frame{layout.stage.x + 10.f * s,
                              layout.stage.y + 28.f * s,
                              layout.stage.width - 20.f * s,
                              layout.stage_caption.y - layout.stage.y -
                                  36.f * s};
  bool portrait_shown = false;
  if (portrait_provider && sel.species_id) {
    if (const auto image =
            (*portrait_provider)(species_portrait_path(*sel.species_id))) {
      const float ratio = std::min(
          portrait_frame.width / static_cast<float>(image->width()),
          portrait_frame.height / static_cast<float>(image->height()));
      const UiRect destination{
          portrait_frame.x +
              (portrait_frame.width - image->width() * ratio) * .5f,
          portrait_frame.y +
              (portrait_frame.height - image->height() * ratio) * .5f,
          image->width() * ratio, image->height() * ratio};
      out.overlay.emplace_back(Image{image, destination, std::nullopt,
                                     {255, 255, 255, 255}, portrait_frame});
      portrait_shown = true;
    }
  }
  if (!portrait_shown) {
    const auto cx = portrait_frame.x + portrait_frame.width * .5f;
    const auto cy = portrait_frame.y + portrait_frame.height * .5f;
    const auto base = std::min(portrait_frame.width, portrait_frame.height);
    for (int ring = 1; ring <= 5; ++ring) {
      const auto radius = base * (.06f + ring * .066f);
      Point previous{cx + radius, cy};
      for (int step = 1; step <= 48; ++step) {
        const auto angle = step * 6.28318530718f / 48.f;
        const Point next{cx + std::cos(angle) * radius,
                         cy + std::sin(angle) * radius};
        out.overlay.emplace_back(
            Line{previous, next, Color{77, 153, 179, 30}});
        previous = next;
      }
    }
    Point previous{cx - portrait_frame.width * .5f, cy};
    for (int step = 1; step <= 32; ++step) {
      const auto x = static_cast<float>(step) / 32.f;
      const Point next{
          portrait_frame.x + x * portrait_frame.width,
          cy + std::sin(x * 66.f) *
                  std::exp(-std::pow((x - .5f) * 7.f, 2.f)) *
                  portrait_frame.height * .12f};
      out.overlay.emplace_back(Line{previous, next, Color{83, 139, 154, 200}});
      previous = next;
    }
  }
  fill(out, layout.stage_caption, {5, 9, 14, 240});
  text(out,
       {layout.stage_caption.x + 10.f * s, layout.stage_caption.y + 8.f * s,
        layout.stage_caption.width - 20.f * s, 22.f * s},
       sel.political_status,
       sel.political_status == "AtWar" || sel.political_status == "Hostile"
           ? danger
           : bright,
       layout.body_font_pixels);
  text(out,
       {layout.stage_caption.x + 10.f * s, layout.stage_caption.y + 30.f * s,
        layout.stage_caption.width - 20.f * s, 30.f * s},
       sel.present ? sel.contact_name
                   : tr("DIPLOMACY_UNDISCOVERED", "THE UNDISCOVERED"),
       bright, layout.title_font_pixels);
  text(out,
       {layout.stage_caption.x + 10.f * s, layout.stage_caption.y + 62.f * s,
        layout.stage_caption.width - 20.f * s, 20.f * s},
       sel.present ? sel.contact_status
                   : tr("DIPLOMACY_FIRST_CONTACT_HINT",
                        "Explore beyond your borders to make first contact."),
       muted, layout.body_font_pixels);

  // Relationship meters and actions.
  region(out, layout.meter_panel);
  theme::section_header(out,
                        {layout.meter_panel.x + 8.f * s,
                         layout.meter_panel.y + 8.f * s,
                         layout.meter_panel.width - 16.f * s, 20.f * s},
                        tr("DIPLOMACY_RELATIONSHIP", "RELATIONSHIP"),
                        layout.small_font_pixels, theme::Tone::Diplomacy);
  const std::pair<std::string, std::optional<double>> meter_rows[] = {
      {tr("DIPLOMACY_TRUST", "TRUST"), sel.trust},
      {tr("DIPLOMACY_RESPECT", "RESPECT"), sel.respect},
      {tr("DIPLOMACY_FEAR", "FEAR"), sel.fear},
      {tr("DIPLOMACY_HOSTILITY", "HOSTILITY"), sel.hostility},
      {tr("DIPLOMACY_COOPERATION", "COOPERATION"), sel.cooperation}};
  const Color meter_colors[] = {theme::color::diplomacy, theme::color::selected,
                                theme::color::economy, theme::color::danger,
                                theme::color::science};
  for (std::size_t index = 0; index < 5; ++index) {
    const auto y = layout.meters.y + static_cast<float>(index) * 30.f * s;
    text(out, {layout.meters.x, y, layout.meters.width * .62f, 18.f * s},
         meter_rows[index].first, muted, layout.small_font_pixels);
    const auto &value = meter_rows[index].second;
    text(out,
         {layout.meters.x + layout.meters.width * .62f, y,
          layout.meters.width * .38f, 18.f * s},
         value ? std::to_string(static_cast<int>(
                     std::lround(std::clamp(*value, 0., 1.) * 100.))) +
                     "%"
               : tr("DIPLOMACY_UNKNOWN", "UNKNOWN"),
         value ? meter_colors[index] : muted, layout.small_font_pixels,
         TextAlign::Right);
    const UiRect bar{layout.meters.x, y + 20.f * s, layout.meters.width,
                     8.f * s};
    fill(out, bar, theme::color::canvas);
    if (value)
      fill(out, {bar.x, bar.y,
                 bar.width * static_cast<float>(std::clamp(*value, 0., 1.)),
                 bar.height},
           meter_colors[index]);
  }
  std::size_t action_index = 0;
  const auto draw_action = [&](std::string label, bool enabled,
                               std::string disabled_tip, bool danger_button) {
    const auto bounds = action_button(layout, action_index++);
    theme::button(out, bounds, label, pointer_, layout.body_font_pixels,
                  danger_button ? theme::Tone::Danger : theme::Tone::Diplomacy,
                  danger_button && enabled, enabled);
    if (danger_button && enabled) stroke(out, bounds, danger);
    // Disabled actions stay visible with the authoritative status as the
    // why — hiding illegal actions made the available set undiscoverable.
    theme::hover_tooltip(out, bounds, pointer_, label, disabled_tip, width,
                         height, s);
  };
  if (sel.present) {
    const bool transmission =
        sel.has_visible_communication || sel.can_attempt_communication;
    const bool negotiate =
        sel.can_offer_non_aggression || sel.can_request_access ||
        sel.can_offer_peace || sel.can_offer_ceasefire || sel.can_set_access;
    draw_action(
        sel.has_visible_communication
            ? tr("DIPLOMACY_OPEN_TRANSMISSION", "Open transmission")
            : tr("DIPLOMACY_ESTABLISH_COMMUNICATION",
                 "Establish communication"),
        transmission, sel.communication_status, false);
    draw_action(tr("DIPLOMACY_NEGOTIATE", "Negotiate"), negotiate,
                sel.communication_status, false);
    draw_action(tr("DIPLOMACY_DECLARE_WAR_ACTION", "Declare war"),
                sel.can_declare_war, sel.political_status, true);
    // The discovery hint only renders where the three action slots leave room;
    // at tight viewports the disabled buttons + tooltips carry the same why.
    const auto hint_y = layout.actions.y + 3.f * 36.f * s + 4.f * s;
    if (!transmission &&
        hint_y + 2.f * layout.small_font_pixels <=
            layout.actions.y + layout.actions.height)
      text(out, {layout.actions.x + 8.f * s, hint_y,
                 layout.actions.width - 16.f * s,
                 layout.actions.y + layout.actions.height - hint_y},
           view_->contacts.empty()
               ? tr("DIPLOMACY_DISCOVERY_HINT",
                    "Discovery opens diplomatic options.")
               : tr("DIPLOMACY_IDENTIFY_HINT",
                    "Identify this contact and recover communication to "
                    "negotiate."),
           muted, layout.small_font_pixels);
  }

  // Tabs.
  for (std::size_t index = 0; index < std::size(tab_labels); ++index) {
    const auto bounds = tab_button(layout, index);
    const bool active = tab_ == tab_labels[index].first;
    theme::tab(out, bounds, tr(tab_keys[index], tab_labels[index].second),
               pointer_, layout.small_font_pixels, active);
  }

  // Detail region.
  region(out, layout.detail_rows);
  const auto card = [&](std::size_t index, float card_h) {
    const auto top = layout.detail_rows.y + 8.f * s +
                     static_cast<float>(index) * (card_h + 8.f * s) -
                     detail_scroll_.scroll_offset;
    return UiRect{layout.detail_rows.x + 8.f * s, top,
                  layout.detail_rows.width - 16.f * s, card_h};
  };
  const auto detail_clip = [&](UiRect bounds) {
    return intersection(bounds, layout.detail_rows);
  };
  const auto detail_fill = [&](UiRect bounds, Color color) {
    if (const auto clipped = detail_clip(bounds)) fill(out, *clipped, color);
  };
  const auto detail_stroke = [&](UiRect bounds, Color color) {
    if (const auto clipped = detail_clip(bounds)) stroke(out, *clipped, color);
  };
  const auto card_text = [&](UiRect bounds, float dy, std::string value,
                             Color color, int pixels) {
    const auto clipped =
        detail_clip({bounds.x + 8.f * s, bounds.y + dy, bounds.width - 16.f * s,
                     bounds.height});
    if (clipped)
      out.overlay.emplace_back(Text{
          {bounds.x + 16.f * s, bounds.y + dy}, std::move(value), color, pixels,
          bounds.width - 32.f * s, *clipped});
  };
  switch (tab_) {
  case DiplomacyWorkspaceTab::agreements: {
    std::size_t index = 0;
    if (sel.present && sel.target_civilization_id) {
      const auto bounds = card(index, 52.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                trf("DIPLOMACY_TRANSIT_SUMMARY",
                    {sel.their_access, sel.our_access},
                    "OUR FLEETS → THEIR SPACE   {0}      THEIR FLEETS → OUR "
                    "SPACE   {1}"),
                muted, layout.body_font_pixels);
      ++index;
    }
    if (view_->agreements.empty()) {
      const auto bounds = card(index++, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s, tr("DIPLOMACY_NO_AGREEMENTS", "NO AGREEMENTS"),
                accent, layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                tr("DIPLOMACY_NO_AGREEMENTS_HINT",
                   "Your active agreements will appear here once accepted."),
                muted, layout.body_font_pixels);
    }
    for (const auto &agreement : view_->agreements) {
      const auto bounds = card(index++, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s, agreement.type, accent,
                layout.body_font_pixels);
      card_text(bounds, 32.f * s,
                trf("DIPLOMACY_AGREEMENT_SINCE",
                    {agreement.status, agreement.started}, "{0} · Since {1}") +
                    (agreement.ended.empty()
                         ? ""
                         : trf("DIPLOMACY_AGREEMENT_ENDED", {agreement.ended},
                               " · Ended {0}")),
                bright, layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::proposals: {
    if (view_->proposals.empty()) {
      const auto bounds = card(0, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                tr("DIPLOMACY_NO_PROPOSALS", "NO PENDING PROPOSALS"), accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                tr("DIPLOMACY_NO_PROPOSALS_HINT",
                   "Use Negotiate to propose a supported agreement."),
                muted, layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < view_->proposals.size(); ++index) {
      const auto &proposal = view_->proposals[index];
      const auto bounds =
          card(index, proposal_card_height(layout));
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                proposal.direction + " · " + proposal.kind +
                    (proposal.agreement_type.empty()
                         ? ""
                         : " · " + proposal.agreement_type),
                accent, layout.body_font_pixels);
      card_text(bounds, 30.f * s, proposal.summary, bright,
                layout.body_font_pixels);
      const std::pair<bool, std::string> buttons[] = {
          {proposal.can_accept, tr("DIPLOMACY_ACCEPT", "Accept")},
          {proposal.can_reject, tr("DIPLOMACY_REJECT", "Reject")},
          {proposal.can_withdraw, tr("DIPLOMACY_WITHDRAW", "Withdraw")}};
      for (int which = 0; which < 3; ++which) {
        if (!buttons[which].first) continue;
        const auto button = proposal_button(layout, index, which, detail_scroll_.scroll_offset);
        const auto clipped = detail_clip(button);
        if (!clipped) continue;
        detail_fill(button, button.contains(pointer_) ? hover : inset);
        detail_stroke(button, border);
        out.overlay.emplace_back(Text{{button.x + button.width * .5f, button.y},
                                      buttons[which].second, bright,
                                      layout.small_font_pixels, button.width,
                                      *clipped, TextAlign::Center});
      }
    }
    break;
  }
  case DiplomacyWorkspaceTab::history: {
    if (view_->history.empty()) {
      const auto bounds = card(0, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                tr("DIPLOMACY_EMPTY_HISTORY", "A HISTORY YET TO BE WRITTEN"),
                accent, layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                tr("DIPLOMACY_EMPTY_HISTORY_HINT",
                   "Observer-visible contact, agreements and conflicts are "
                   "recorded here."),
                muted, layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < view_->history.size(); ++index) {
      const auto &event = view_->history[index];
      const auto bounds = card(index, 62.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s, event.date + " · " + event.kind, accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s, event.summary, bright,
                layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::intelligence: {
    if (view_->contacts.empty()) {
      const auto bounds = card(0, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                tr("DIPLOMACY_NO_INTELLIGENCE", "NO INTELLIGENCE"), accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                tr("DIPLOMACY_NO_INTELLIGENCE_HINT",
                   "Explore to acquire legitimate observations."),
                muted, layout.body_font_pixels);
      break;
    }
    if (sel.present) {
      const auto &contact = view_->contacts[sel.contact_index];
      const auto bounds = card(0, 62.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                tr("DIPLOMACY_CONTACT_EVIDENCE", "CONTACT EVIDENCE"), accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                sel.contact_status +
                    (contact.last_observed_system_id
                         ? trf("DIPLOMACY_LAST_OBSERVED",
                               {contact.last_observed_system_name},
                               " · Last observed {0}")
                         : ""),
                bright, layout.body_font_pixels);
      if (contact.last_observed_system_id) {
        const UiRect focus{layout.detail_rows.x + 16.f * s,
                           layout.detail_rows.y + 78.f * s - detail_scroll_.scroll_offset,
                           layout.detail_rows.width - 32.f * s, 34.f * s};
        if (const auto clipped = detail_clip(focus)) {
          detail_fill(focus, focus.contains(pointer_) ? hover : row);
          detail_stroke(focus, border);
          out.overlay.emplace_back(Text{{focus.x + focus.width * .5f, focus.y},
                                        trf("DIPLOMACY_LAST_OBSERVATION",
                                            {contact.last_observed_system_name},
                                            "Last observation · {0}"),
                                        bright, layout.small_font_pixels,
                                        focus.width, *clipped,
                                        TextAlign::Center});
        }
      }
      const auto unresolved_y = bounds.y + bounds.height + 8.f * s +
          (contact.last_observed_system_id ? 34.f * s + 8.f * s : 0.f);
      const UiRect unresolved{layout.detail_rows.x + 8.f * s, unresolved_y,
                              layout.detail_rows.width - 16.f * s, 70.f * s};
      detail_fill(unresolved, row);
      detail_stroke(unresolved, border);
      card_text(unresolved, 8.f * s,
                tr("DIPLOMACY_UNRESOLVED", "UNRESOLVED INFORMATION"), accent,
                layout.body_font_pixels);
      card_text(unresolved, 30.f * s,
                tr("DIPLOMACY_UNRESOLVED_DETAIL",
                   "Representative: Unknown   ·   Government: Unknown   ·   "
                   "Military strength: Unknown   ·   Technology and intentions: "
                   "Unknown"),
                muted, layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::overview: {
    if (rows.empty()) {
      const auto bounds = card(0, 64.f * s);
      detail_fill(bounds, row);
      detail_stroke(bounds, border);
      card_text(bounds, 8.f * s,
                tr("DIPLOMACY_GALACTIC_COMMUNITY", "THE GALACTIC COMMUNITY"),
                accent, layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                tr("DIPLOMACY_NO_OBSERVED_CIVS",
                   "No foreign civilization has been observed."),
                muted, layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto &contact = *rows[index];
      const auto bounds = card(index, 44.f * s);
      detail_fill(bounds, contact.source_index == selected_contact_index_
                              ? selected
                              : bounds.contains(pointer_) ? hover : row);
      detail_stroke(bounds, border);
      card_text(bounds, 12.f * s,
                contact.display_name + "   ·   " + contact.status + "   ·   " +
                    contact.communication,
                bright, layout.body_font_pixels);
    }
    break;
  }
  }

  // Result line.
  if (!notice_.empty())
    text(out, layout.feedback, notice_,
         notice_accepted_ ? accent : danger, layout.body_font_pixels);
  else if (sel.present)
    text(out, layout.feedback, sel.access_summary, muted,
         layout.small_font_pixels);

  // Modal.
  if (modal_) {
    fill(out, {0, 0, static_cast<float>(width), static_cast<float>(height)},
         {0, 0, 0, 199});
    fill(out, layout.modal_panel, panel);
    stroke(out, layout.modal_panel,
           modal_->danger ? danger : theme::color::keyline_strong);
    text(out,
         {layout.modal_panel.x + 16.f * s, layout.modal_panel.y + 14.f * s,
          layout.modal_panel.width - 32.f * s, 30.f * s},
         modal_->title, modal_->danger ? danger : gold,
         layout.title_font_pixels);
    text(out,
         {layout.modal_panel.x + 16.f * s, layout.modal_panel.y + 48.f * s,
          layout.modal_panel.width - 32.f * s, 44.f * s},
         modal_->description, bright, layout.body_font_pixels);
    if (modal_->negotiation) {
      for (std::size_t index = 0; index < modal_->terms.size(); ++index) {
        const auto bounds = modal_term_button(layout, index);
        theme::button(out, bounds, modal_->terms[index].first, pointer_,
                      layout.body_font_pixels, theme::Tone::Diplomacy);
      }
    } else {
      theme::button(out, modal_confirm_button(layout), modal_->confirm_label,
                    pointer_, layout.body_font_pixels,
                    modal_->danger ? theme::Tone::Danger
                                   : theme::Tone::Diplomacy,
                    true);
      if (modal_->danger)
        stroke(out, modal_confirm_button(layout), danger);
    }
    theme::button(out, modal_cancel_button(layout),
                  tr("SETTINGS_CANCEL", "Cancel"), pointer_,
                  layout.body_font_pixels);
  }
  if (focus_ >= 0) {
    const auto rects = focusables(layout);
    if (focus_ < static_cast<int>(rects.size()))
      theme::focus_ring(out,
                        rects[static_cast<std::size_t>(focus_)].bounds);
  }
}

} // namespace stellar::native_diplomacy_ui
