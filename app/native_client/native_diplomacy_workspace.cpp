#include "native_diplomacy_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace stellar::native_diplomacy_ui {
namespace {
using namespace stellar::native_diplomacy;
using namespace stellar::native_map;

constexpr Color panel{7, 17, 32, 252};
constexpr Color inset{5, 14, 27, 250};
constexpr Color row{12, 31, 54, 248};
constexpr Color hover{24, 61, 94, 252};
constexpr Color selected{19, 73, 68, 252};
constexpr Color border{91, 151, 205, 235};
constexpr Color accent{120, 197, 165, 255};
constexpr Color gold{217, 182, 119, 255};
constexpr Color bright{235, 244, 255, 255};
constexpr Color muted{154, 181, 211, 240};
constexpr Color danger{243, 153, 130, 255};

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

constexpr std::pair<DiplomacyWorkspaceTab, const char *> tab_labels[] = {
    {DiplomacyWorkspaceTab::agreements, "AGREEMENTS"},
    {DiplomacyWorkspaceTab::proposals, "PROPOSALS"},
    {DiplomacyWorkspaceTab::history, "HISTORY"},
    {DiplomacyWorkspaceTab::intelligence, "INTELLIGENCE"},
    {DiplomacyWorkspaceTab::overview, "OVERVIEW"},
};

[[nodiscard]] UiRect filter_button(const DiplomacyWorkspaceLayout &layout,
                                   std::size_t index) noexcept {
  const auto s = layout.scale;
  const auto columns = 3;
  const auto gap = 4.f * s;
  const auto width =
      (layout.contact_panel.width - 16.f * s - gap * (columns - 1)) / columns;
  const auto column = static_cast<float>(index % columns);
  const auto band = static_cast<float>(index / columns);
  return {layout.contact_panel.x + 8.f * s + column * (width + gap),
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
  std::string id(species_id);
  std::ranges::replace(id, '_', '-');
  return "assets/visual/species/" + id + "-communications-v2.png";
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
  const auto top = 60.f * scale;
  const UiRect surface{margin, top, std::max(1.f, w - margin * 2.f),
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
               std::min(128.f * scale, meter_panel.height - 40.f * scale -
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

void NativeDiplomacyWorkspace::open() noexcept { visible_ = true; }
void NativeDiplomacyWorkspace::close() noexcept {
  visible_ = false;
  modal_.reset();
}
bool NativeDiplomacyWorkspace::visible() const noexcept { return visible_; }
void NativeDiplomacyWorkspace::set_view(NativeDiplomacyView view) {
  const auto previous_selection =
      view_ && view_->selected.present
          ? view_->selected.target_civilization_id
          : std::optional<int>{};
  view_ = std::move(view);
  if (previous_selection) {
    const auto found = std::ranges::find_if(
        view_->contacts, [&](const auto &contact) {
          return contact.civilization_id == previous_selection;
        });
    if (found != view_->contacts.end())
      selected_contact_index_ = found->source_index;
  }
  reconcile_selection();
}
void NativeDiplomacyWorkspace::discard_campaign() {
  view_.reset();
  selected_contact_index_ = 0;
  filter_ = NativeDiplomacyContactFilter::all;
  tab_ = DiplomacyWorkspaceTab::agreements;
  modal_.reset();
  notice_.clear();
  contact_scroll_ = detail_scroll_ = 0;
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
const std::string &NativeDiplomacyWorkspace::notice() const noexcept {
  return notice_;
}

void NativeDiplomacyWorkspace::reconcile_selection() {
  if (!view_ || view_->contacts.empty()) {
    selected_contact_index_ = 0;
    return;
  }
  selected_contact_index_ =
      std::min(selected_contact_index_, view_->contacts.size() - 1);
}
std::vector<const NativeDiplomacyContact *>
NativeDiplomacyWorkspace::filtered_contacts() const {
  if (!view_) return {};
  return filter_native_diplomacy_contacts(*view_, filter_);
}

DiplomacyWorkspaceCommand NativeDiplomacyWorkspace::handle(
    const InputEvent &event, int width, int height) {
  if (!visible_ || !view_) return {};
  pointer_ = event.position;
  const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) return {};

  if (event.type == InputEventType::Wheel) {
    if (layout.contact_rows.contains(event.position)) {
      const auto rows = filtered_contacts();
      const auto content = rows.size() * 62.f * layout.scale;
      const auto limit = std::max(
          0.f, content - layout.contact_rows.height + 12.f * layout.scale);
      contact_scroll_ = std::clamp(contact_scroll_ - event.wheel_y * 26.f,
                                   0.f, limit);
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (layout.detail_rows.contains(event.position)) {
      detail_scroll_ = std::max(0.f, detail_scroll_ - event.wheel_y * 26.f);
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
    if (layout.surface.contains(event.position))
      return {DiplomacyWorkspaceCommandKind::None, true};
    return {};
  }

  if (event.type != InputEventType::LeftPressed) return {};

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
          next.description =
              "Counterpart: " + view_->selected.contact_name;
          next.action = modal.terms[index].second;
          next.target_civilization_id =
              view_->selected.target_civilization_id;
          next.confirm_label = next.action ==
                                           DiplomacyWorkspaceAction::grant_access ||
                                       next.action ==
                                           DiplomacyWorkspaceAction::deny_access
                                   ? "APPLY ACCESS"
                                   : "SEND PROPOSAL";
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
      contact_scroll_ = 0;
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
  }
  for (std::size_t index = 0; index < std::size(tab_labels); ++index) {
    if (tab_button(layout, index).contains(event.position)) {
      tab_ = tab_labels[index].first;
      detail_scroll_ = 0;
      return {DiplomacyWorkspaceCommandKind::None, true};
    }
  }

  const auto rows = filtered_contacts();
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto bounds = contact_row(layout, index, contact_scroll_);
    if (bounds.y >= layout.contact_rows.y + layout.contact_rows.height ||
        bounds.y + bounds.height <= layout.contact_rows.y)
      continue;
    if (bounds.contains(event.position)) {
      selected_contact_index_ = rows[index]->source_index;
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
    if (transmission) {
      if (action_hit(action_index)) {
        if (s.has_visible_communication) {
          set_notice(
              "Channel open. Select a proposal to begin negotiations.", true);
          return {DiplomacyWorkspaceCommandKind::None, true};
        }
        DiplomacyWorkspaceCommand command{
            DiplomacyWorkspaceCommandKind::Action, true};
        command.action = DiplomacyWorkspaceAction::establish_communication;
        command.target_civilization_id = s.target_civilization_id;
        return command;
      }
      ++action_index;
    }
    if (negotiate) {
      if (action_hit(action_index)) {
        ModalState modal;
        modal.negotiation = true;
        modal.title = "NEGOTIATION";
        modal.description = "Choose the agreement you want to propose.";
        const auto add = [&](const char *name,
                             DiplomacyWorkspaceAction action, bool legal) {
          if (legal) modal.terms.emplace_back(name, action);
        };
        add("Non-aggression", DiplomacyWorkspaceAction::propose_non_aggression,
            s.can_offer_non_aggression);
        add("Request transit access", DiplomacyWorkspaceAction::request_access,
            s.can_request_access);
        add("Ceasefire", DiplomacyWorkspaceAction::offer_ceasefire,
            s.can_offer_ceasefire);
        add("Peace", DiplomacyWorkspaceAction::offer_peace, s.can_offer_peace);
        add("Grant transit access", DiplomacyWorkspaceAction::grant_access,
            s.can_set_access);
        add("Deny transit access", DiplomacyWorkspaceAction::deny_access,
            s.can_set_access);
        modal_ = std::move(modal);
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
      ++action_index;
    }
    if (s.can_declare_war) {
      if (action_hit(action_index)) {
        ModalState modal;
        modal.title = "DECLARE WAR ON " + s.contact_name;
        modal.description =
            "Your civilizations will enter a state of war. Active agreements "
            "may be affected.";
        modal.action = DiplomacyWorkspaceAction::declare_war;
        modal.target_civilization_id = s.target_civilization_id;
        modal.danger = true;
        modal.confirm_label = "DECLARE WAR";
        modal_ = std::move(modal);
        return {DiplomacyWorkspaceCommandKind::None, true};
      }
      ++action_index;
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
        if (proposal_button(layout, card, which, detail_scroll_)
                .contains(event.position)) {
          DiplomacyWorkspaceCommand command{
              DiplomacyWorkspaceCommandKind::ProposalAction, true};
          command.action = buttons[which].second;
          command.proposal_id = proposal.proposal_id;
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
                         layout.detail_rows.y + 78.f * s2 - detail_scroll_,
                         layout.detail_rows.width - 32.f * s2, 34.f * s2};
      if (focus.contains(event.position))
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
  if (!view_) return;
  const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
  const auto s = layout.scale;
  fill(out, layout.surface, panel);
  stroke(out, layout.surface, border);
  text(out, layout.title, "RELATIONS", bright, layout.title_font_pixels);
  text(out, layout.date, view_->date, muted, layout.body_font_pixels,
       TextAlign::Right);
  fill(out, layout.close, layout.close.contains(pointer_) ? hover : row);
  stroke(out, layout.close, border);
  text(out, layout.close, "RETURN", bright, layout.small_font_pixels,
       TextAlign::Center);

  // Contact directory
  fill(out, layout.contact_panel, inset);
  stroke(out, layout.contact_panel, border);
  text(out,
       {layout.contact_panel.x + 8.f * s, layout.contact_panel.y + 8.f * s,
        layout.contact_panel.width - 16.f * s, 22.f * s},
       "CONTACT DIRECTORY", accent, layout.small_font_pixels);
  for (std::size_t index = 0; index < std::size(filter_labels); ++index) {
    const auto bounds = filter_button(layout, index);
    const bool active = filter_ == filter_labels[index].first;
    fill(out, bounds, active ? selected
                             : bounds.contains(pointer_) ? hover : row);
    stroke(out, bounds, border);
    text(out, bounds, filter_labels[index].second,
         active ? bright : muted, layout.small_font_pixels, TextAlign::Center);
  }
  fill(out, layout.contact_rows, inset);
  stroke(out, layout.contact_rows, border);
  const auto rows = filtered_contacts();
  if (rows.empty()) {
    text(out, layout.contact_rows,
         view_->contacts.empty()
             ? "No contacts yet. Send scout ships into unexplored systems to "
               "discover other civilizations."
             : "No contacts match this filter.",
         muted, layout.body_font_pixels);
  }
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto bounds = contact_row(layout, index, contact_scroll_);
    if (bounds.y >= layout.contact_rows.y + layout.contact_rows.height ||
        bounds.y + bounds.height <= layout.contact_rows.y)
      continue;
    const auto &contact = *rows[index];
    const bool chosen = contact.source_index == selected_contact_index_;
    fill(out, bounds, chosen ? selected
                             : bounds.contains(pointer_) ? hover : row);
    stroke(out, bounds, chosen ? accent : border);
    const auto clip = intersection(bounds, layout.contact_rows);
    if (!clip) continue;
    text(out, {bounds.x + 8.f * s, bounds.y + 5.f * s, bounds.width - 16.f * s,
               18.f * s},
         contact.display_name, bright, layout.body_font_pixels);
    out.overlay.back() = Text{{bounds.x + 8.f * s, bounds.y + 5.f * s},
                              contact.display_name,
                              bright,
                              layout.body_font_pixels,
                              bounds.width - 16.f * s,
                              *clip};
    text(out, {bounds.x + 8.f * s, bounds.y + 23.f * s,
               bounds.width - 16.f * s, 16.f * s},
         contact.status, gold, layout.small_font_pixels);
    text(out, {bounds.x + 8.f * s, bounds.y + 39.f * s,
               bounds.width - 16.f * s, 16.f * s},
         contact.identified
             ? contact.communication
             : "Identity confidence " +
                   std::to_string(
                       static_cast<int>(std::lround(contact.confidence * 100.))) +
                   "%",
         muted, layout.small_font_pixels);
  }

  const auto &sel = view_->selected;
  // Transmission stage: portrait for identified contacts, signal arcs otherwise.
  fill(out, layout.stage, inset);
  stroke(out, layout.stage, border);
  text(out,
       {layout.stage.x + 10.f * s, layout.stage.y + 6.f * s,
        layout.stage.width - 20.f * s, 20.f * s},
       sel.has_visible_communication ? "COMMUNICATION CHANNEL AVAILABLE"
                                     : "COMMUNICATION UNAVAILABLE",
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
       sel.present ? sel.contact_name : "THE UNDISCOVERED", bright,
       layout.title_font_pixels);
  text(out,
       {layout.stage_caption.x + 10.f * s, layout.stage_caption.y + 62.f * s,
        layout.stage_caption.width - 20.f * s, 20.f * s},
       sel.present ? sel.contact_status
                   : "Explore beyond your borders to make first contact.",
       muted, layout.body_font_pixels);

  // Relationship meters and actions.
  fill(out, layout.meter_panel, inset);
  stroke(out, layout.meter_panel, border);
  text(out,
       {layout.meter_panel.x + 8.f * s, layout.meter_panel.y + 8.f * s,
        layout.meter_panel.width - 16.f * s, 20.f * s},
       "RELATIONSHIP", accent, layout.small_font_pixels);
  const std::pair<const char *, std::optional<double>> meter_rows[] = {
      {"TRUST", sel.trust},         {"RESPECT", sel.respect},
      {"FEAR", sel.fear},           {"HOSTILITY", sel.hostility},
      {"COOPERATION", sel.cooperation}};
  const Color meter_colors[] = {{120, 197, 165, 255}, {119, 185, 211, 255},
                                {217, 182, 119, 255}, {214, 124, 114, 255},
                                {167, 150, 206, 255}};
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
               : "UNKNOWN",
         value ? meter_colors[index] : muted, layout.small_font_pixels,
         TextAlign::Right);
    const UiRect bar{layout.meters.x, y + 20.f * s, layout.meters.width,
                     8.f * s};
    fill(out, bar, {24, 39, 51, 255});
    if (value)
      fill(out, {bar.x, bar.y,
                 bar.width * static_cast<float>(std::clamp(*value, 0., 1.)),
                 bar.height},
           meter_colors[index]);
  }
  std::size_t action_index = 0;
  const auto draw_action = [&](const char *label, bool danger_button) {
    const auto bounds = action_button(layout, action_index++);
    fill(out, bounds, bounds.contains(pointer_) ? hover : row);
    stroke(out, bounds, danger_button ? danger : border);
    text(out, bounds, label, danger_button ? danger : bright,
         layout.body_font_pixels, TextAlign::Center);
  };
  if (sel.present) {
    if (sel.has_visible_communication || sel.can_attempt_communication)
      draw_action(sel.has_visible_communication ? "Open transmission"
                                                : "Establish communication",
                  false);
    if (sel.can_offer_non_aggression || sel.can_request_access ||
        sel.can_offer_peace || sel.can_offer_ceasefire || sel.can_set_access)
      draw_action("Negotiate", false);
    if (sel.can_declare_war) draw_action("Declare war", true);
    if (!sel.has_visible_communication && !sel.can_attempt_communication)
      text(out, {layout.actions.x + 8.f * s,
                 layout.actions.y + static_cast<float>(action_index) * 36.f * s +
                     8.f * s,
                 layout.actions.width - 16.f * s, 60.f * s},
           view_->contacts.empty()
               ? "Discovery opens diplomatic options."
               : "Identify this contact and recover communication to negotiate.",
           muted, layout.small_font_pixels);
  }

  // Tabs.
  for (std::size_t index = 0; index < std::size(tab_labels); ++index) {
    const auto bounds = tab_button(layout, index);
    const bool active = tab_ == tab_labels[index].first;
    fill(out, bounds, active ? selected
                             : bounds.contains(pointer_) ? hover : row);
    stroke(out, bounds, border);
    text(out, bounds, tab_labels[index].second, active ? accent : bright,
         layout.small_font_pixels, TextAlign::Center);
  }

  // Detail region.
  fill(out, layout.detail_rows, inset);
  stroke(out, layout.detail_rows, border);
  const auto card = [&](std::size_t index, float card_h) {
    const auto top = layout.detail_rows.y + 8.f * s +
                     static_cast<float>(index) * (card_h + 8.f * s) -
                     detail_scroll_;
    return UiRect{layout.detail_rows.x + 8.f * s, top,
                  layout.detail_rows.width - 16.f * s, card_h};
  };
  const auto detail_clip = [&](UiRect bounds) {
    return intersection(bounds, layout.detail_rows);
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
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s,
                "OUR FLEETS → THEIR SPACE   " + sel.their_access +
                    "      THEIR FLEETS → OUR SPACE   " + sel.our_access,
                muted, layout.body_font_pixels);
      ++index;
    }
    if (view_->agreements.empty()) {
      const auto bounds = card(index++, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "NO AGREEMENTS", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                "Your active agreements will appear here once accepted.", muted,
                layout.body_font_pixels);
    }
    for (const auto &agreement : view_->agreements) {
      const auto bounds = card(index++, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, agreement.type, accent,
                layout.body_font_pixels);
      card_text(bounds, 32.f * s,
                agreement.status + " · Since " + agreement.started +
                    (agreement.ended.empty() ? "" : " · Ended " + agreement.ended),
                bright, layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::proposals: {
    if (view_->proposals.empty()) {
      const auto bounds = card(0, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "NO PENDING PROPOSALS", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                "Use Negotiate to propose a supported agreement.", muted,
                layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < view_->proposals.size(); ++index) {
      const auto &proposal = view_->proposals[index];
      const auto bounds =
          card(index, proposal_card_height(layout));
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s,
                proposal.direction + " · " + proposal.kind +
                    (proposal.agreement_type.empty()
                         ? ""
                         : " · " + proposal.agreement_type),
                accent, layout.body_font_pixels);
      card_text(bounds, 30.f * s, proposal.summary, bright,
                layout.body_font_pixels);
      const std::pair<bool, const char *> buttons[] = {
          {proposal.can_accept, "Accept"},
          {proposal.can_reject, "Reject"},
          {proposal.can_withdraw, "Withdraw"}};
      for (int which = 0; which < 3; ++which) {
        if (!buttons[which].first) continue;
        const auto button = proposal_button(layout, index, which, detail_scroll_);
        const auto clipped = detail_clip(button);
        if (!clipped) continue;
        fill(out, button, button.contains(pointer_) ? hover : inset);
        stroke(out, button, border);
        text(out, button, buttons[which].second, bright,
             layout.small_font_pixels, TextAlign::Center);
      }
    }
    break;
  }
  case DiplomacyWorkspaceTab::history: {
    if (view_->history.empty()) {
      const auto bounds = card(0, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "A HISTORY YET TO BE WRITTEN", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                "Observer-visible contact, agreements and conflicts are "
                "recorded here.",
                muted, layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < view_->history.size(); ++index) {
      const auto &event = view_->history[index];
      const auto bounds = card(index, 62.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, event.date + " · " + event.kind, accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s, event.summary, bright,
                layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::intelligence: {
    std::size_t index = 0;
    if (view_->contacts.empty()) {
      const auto bounds = card(index++, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "NO INTELLIGENCE", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                "Explore to acquire legitimate observations.", muted,
                layout.body_font_pixels);
      break;
    }
    if (sel.present) {
      const auto &contact = view_->contacts[sel.contact_index];
      const auto bounds = card(index++, 62.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "CONTACT EVIDENCE", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                sel.contact_status +
                    (contact.last_observed_system_id
                         ? " · Last observed " +
                               contact.last_observed_system_name
                         : ""),
                bright, layout.body_font_pixels);
      if (contact.last_observed_system_id) {
        const UiRect focus{layout.detail_rows.x + 16.f * s,
                           layout.detail_rows.y + 78.f * s - detail_scroll_,
                           layout.detail_rows.width - 32.f * s, 34.f * s};
        if (detail_clip(focus)) {
          fill(out, focus, focus.contains(pointer_) ? hover : row);
          stroke(out, focus, border);
          text(out, focus,
               "Last observation · " + contact.last_observed_system_name,
               bright, layout.small_font_pixels, TextAlign::Center);
        }
      }
      const auto unresolved = card(index++, 70.f * s);
      fill(out, unresolved, row);
      stroke(out, unresolved, border);
      card_text(unresolved, 8.f * s, "UNRESOLVED INFORMATION", accent,
                layout.body_font_pixels);
      card_text(unresolved, 30.f * s,
                "Representative: Unknown   ·   Government: Unknown   ·   "
                "Military strength: Unknown   ·   Technology and intentions: "
                "Unknown",
                muted, layout.body_font_pixels);
    }
    break;
  }
  case DiplomacyWorkspaceTab::overview: {
    if (rows.empty()) {
      const auto bounds = card(0, 64.f * s);
      fill(out, bounds, row);
      stroke(out, bounds, border);
      card_text(bounds, 8.f * s, "THE GALACTIC COMMUNITY", accent,
                layout.body_font_pixels);
      card_text(bounds, 30.f * s,
                "No foreign civilization has been observed.", muted,
                layout.body_font_pixels);
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
      const auto &contact = *rows[index];
      const auto bounds = card(index, 44.f * s);
      fill(out, bounds,
           contact.source_index == selected_contact_index_
               ? selected
               : bounds.contains(pointer_) ? hover : row);
      stroke(out, bounds, border);
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
           modal_->danger ? danger : border);
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
        fill(out, bounds, bounds.contains(pointer_) ? hover : row);
        stroke(out, bounds, border);
        text(out, bounds, modal_->terms[index].first, bright,
             layout.body_font_pixels, TextAlign::Center);
      }
    } else {
      const auto bounds = modal_confirm_button(layout);
      fill(out, bounds, bounds.contains(pointer_) ? hover : row);
      stroke(out, bounds, modal_->danger ? danger : accent);
      text(out, bounds, modal_->confirm_label,
           modal_->danger ? danger : bright, layout.body_font_pixels,
           TextAlign::Center);
    }
    const auto cancel = modal_cancel_button(layout);
    fill(out, cancel, cancel.contains(pointer_) ? hover : row);
    stroke(out, cancel, border);
    text(out, cancel, "Cancel", muted, layout.body_font_pixels,
         TextAlign::Center);
  }
}

} // namespace stellar::native_diplomacy_ui
