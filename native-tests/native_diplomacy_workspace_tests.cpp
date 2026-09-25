#include "native_diplomacy_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

using namespace stellar::native_diplomacy;
using namespace stellar::native_diplomacy_ui;
using namespace stellar::native_map;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] bool contained(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

[[nodiscard]] bool overlaps(UiRect left, UiRect right) noexcept {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

void require_scrolled_draw_clipped(const DrawList &draw, UiRect region,
                                  const char *message) {
  for (const auto &command : draw.overlay) {
    const auto check_rectangle = [&](UiRect bounds) {
      if (overlaps(bounds, region) && bounds.height <= region.height &&
          bounds.x >= region.x &&
          bounds.x + bounds.width <= region.x + region.width)
        require(contained(region, bounds), message);
    };
    if (const auto *filled = std::get_if<FilledRectangle>(&command))
      check_rectangle(filled->bounds);
    else if (const auto *stroked = std::get_if<StrokedRectangle>(&command))
      check_rectangle(stroked->bounds);
    else if (const auto *label = std::get_if<Text>(&command);
             label && label->clip && overlaps(*label->clip, region))
      require(contained(region, *label->clip), message);
  }
}

[[nodiscard]] Point center(UiRect bounds) noexcept {
  return {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f};
}

[[nodiscard]] bool has_text(const DrawList &draw, std::string_view value) {
  for (const auto &command : draw.overlay)
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.contains(value))
      return true;
  return false;
}

[[nodiscard]] NativeDiplomacyView sample_view() {
  NativeDiplomacyView view;
  view.campaign_generation = 4;
  view.diplomacy_revision = 3;
  view.observer_civilization_id = 0;
  view.date = "2051-04-12";

  NativeDiplomacyContact alpha;
  alpha.contact_id = "contact-alpha";
  alpha.source_index = 0;
  alpha.identified = true;
  alpha.civilization_id = 7;
  alpha.display_name = "Nova Concord";
  alpha.status = "Peace";
  alpha.communication = "CHANNEL AVAILABLE";
  alpha.communication_available = true;
  alpha.confidence = .9;
  alpha.species_name = "Terran";
  alpha.cooperation = .7;
  alpha.pending_proposal_count = 1;
  alpha.last_observed_system_id = 42;
  alpha.last_observed_system_name = "Sol";

  NativeDiplomacyContact beta;
  beta.contact_id = "contact-beta";
  beta.source_index = 1;
  beta.identified = false;
  beta.display_name = "UNKNOWN CONTACT";
  beta.status = "IDENTITY UNKNOWN";
  beta.communication = "CHANNEL UNAVAILABLE";
  beta.confidence = .4;

  view.contacts = {alpha, beta};

  auto &selected = view.selected;
  selected.present = true;
  selected.contact_index = 0;
  selected.target_civilization_id = 7;
  selected.contact_name = "Nova Concord";
  selected.contact_status = "identified · active · 90% confidence";
  selected.communication_status = "Channel available";
  selected.political_status = "Peace";
  selected.has_visible_communication = true;
  selected.trust = .6;
  selected.respect = .5;
  selected.fear = .1;
  selected.hostility = .2;
  selected.cooperation = .7;
  selected.species_id = "pelagic_high_pressure";
  selected.our_access = "UNSPECIFIED";
  selected.their_access = "GRANTED";
  selected.access_summary = "Your access: UNSPECIFIED · Their access: GRANTED";
  selected.agreements_summary = "No active agreements";
  selected.proposal_summary = "1 pending proposal(s)";
  selected.recent_events = {"Contact established."};
  selected.can_offer_non_aggression = true;
  selected.can_request_access = true;
  selected.can_set_access = true;
  selected.can_declare_war = true;

  view.proposals.push_back({11, "INCOMING", "Agreement", "Non Aggression",
                            "Join a pact of non-aggression.", true, true,
                            false});
  view.agreements.push_back({5, "Access", "ACTIVE",
                             stellar::core::DiplomaticAgreementStatus::active,
                             "2050-06-01", ""});
  view.history.push_back({9, "2050-03-01", "Contact Established",
                          "A channel opened with Nova Concord."});
  return view;
}
} // namespace

int main() try {
  for (const auto [width, height] :
       std::array{std::pair{640, 360}, std::pair{1280, 720},
                  std::pair{1920, 1080}, std::pair{2560, 1440},
                  std::pair{3840, 2160}}) {
    const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
    const auto navigation = NativeUiLayout::for_viewport(width, height);
    const UiRect viewport{0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
    require(contained(viewport, layout.surface) &&
                contained(layout.surface, layout.contact_panel) &&
                contained(layout.surface, layout.stage) &&
                contained(layout.surface, layout.meter_panel) &&
                contained(layout.surface, layout.tabs) &&
                contained(layout.surface, layout.detail_rows) &&
                contained(layout.surface, layout.feedback) &&
                contained(viewport, layout.modal_panel),
            "Diplomacy workspace escaped its viewport.");
    if(height>=720)
      require(layout.surface.x >=
                  navigation.inspect.x + navigation.inspect.width,
              "Diplomacy content overlaps the navigation rail.");
  }

  NativeDiplomacyWorkspace empty;
  empty.open();
  NativeDiplomacyView empty_view;
  empty_view.date = "2050-01-01";
  empty.set_view(empty_view);
  DrawList empty_draw;
  empty.render(empty_draw, 1280, 720, nullptr);
  require(has_text(empty_draw, "RELATIONS") &&
              has_text(empty_draw, "No contacts yet") &&
              has_text(empty_draw, "THE UNDISCOVERED"),
          "Empty diplomacy view invented a contact or hid its empty state.");

  NativeDiplomacyWorkspace workspace;
  workspace.open();
  workspace.set_view(sample_view());
  const auto layout = DiplomacyWorkspaceLayout::for_viewport(1280, 720);
  const auto s = layout.scale;

  DrawList draw;
  std::string portrait_path;
  const NativeDiplomacyWorkspace::PortraitProvider portrait_provider =
      [&portrait_path](std::string_view path) {
        portrait_path = path;
        return std::shared_ptr<const RgbaImage>{};
      };
  workspace.render(draw, 1280, 720, &portrait_provider);
  require(has_text(draw, "Nova Concord") && has_text(draw, "CONTACT DIRECTORY") &&
              has_text(draw, "RELATIONSHIP") &&
              has_text(draw, "COMMUNICATION CHANNEL AVAILABLE"),
          "Diplomacy workspace dropped its main panels.");
  require(portrait_path ==
              "assets/visual/species/pelagic-high-pressure-communications-v2.png",
          "Diplomacy workspace requested an obsolete portrait asset.");

  // Contact selection emits the source index.
  const UiRect second_row{layout.contact_rows.x + 4.f * s,
                          layout.contact_rows.y + 4.f * s + 62.f * s,
                          layout.contact_rows.width - 8.f * s, 58.f * s};
  const auto select = workspace.handle(
      {InputEventType::LeftPressed, center(second_row)}, 1280, 720);
  require(select.kind == DiplomacyWorkspaceCommandKind::SelectContact &&
              select.captured && select.contact_index == 1,
          "Contact row click did not emit a selection.");

  // Tab switching captures input without issuing a command.
  const auto tab = workspace.handle(
      {InputEventType::LeftPressed, center(layout.tabs)}, 1280, 720);
  require(tab.captured && tab.kind == DiplomacyWorkspaceCommandKind::None,
          "Tab bar click was not captured.");

  // Proposals tab: the Accept button emits a proposal command.
  const UiRect proposals_tab{layout.tabs.x + (layout.tabs.width - 8.f * s * 4.f) /
                                                   5.f +
                                 8.f * s,
                             layout.tabs.y,
                             (layout.tabs.width - 8.f * s * 4.f) / 5.f,
                             layout.tabs.height};
  (void)workspace.handle({InputEventType::LeftPressed, center(proposals_tab)},
                         1280, 720);
  const auto card_height = 108.f * s;
  const UiRect accept{layout.detail_rows.x + 16.f * s,
                      layout.detail_rows.y + 8.f * s + card_height - 40.f * s,
                      (layout.detail_rows.width - 32.f * s - 12.f * s) / 3.f,
                      32.f * s};
  const auto accepted = workspace.handle(
      {InputEventType::LeftPressed, center(accept)}, 1280, 720);
  require(accepted.kind == DiplomacyWorkspaceCommandKind::ProposalAction &&
              accepted.captured && accepted.proposal_id == 11 &&
              accepted.action == DiplomacyWorkspaceAction::accept_proposal &&
              accepted.campaign_generation == 4 &&
              accepted.diplomacy_revision == 3,
          "Accept button did not emit the proposal command.");

  auto communication_pending = sample_view();
  communication_pending.selected.has_visible_communication = false;
  communication_pending.selected.can_attempt_communication = true;
  NativeDiplomacyWorkspace immediate_action;
  immediate_action.open();
  immediate_action.set_view(communication_pending);
  const UiRect establish{layout.actions.x + 8.f * s, layout.actions.y + 8.f * s,
                         layout.actions.width - 16.f * s, 30.f * s};
  const auto established = immediate_action.handle(
      {InputEventType::LeftPressed, center(establish)}, 1280, 720);
  require(established.kind == DiplomacyWorkspaceCommandKind::Action &&
              established.action ==
                  DiplomacyWorkspaceAction::establish_communication &&
              established.campaign_generation == 4 &&
              established.diplomacy_revision == 3,
          "Immediate diplomacy action lost its state quote.");

  // Negotiate opens the term picker; a term opens the confirmation; confirm
  // emits the workspace action against the selected counterpart.
  const UiRect negotiate{layout.actions.x + 8.f * s,
                         layout.actions.y + 8.f * s + 36.f * s,
                         layout.actions.width - 16.f * s, 30.f * s};
  const auto opened = workspace.handle(
      {InputEventType::LeftPressed, center(negotiate)}, 1280, 720);
  require(opened.captured && workspace.modal_open(),
          "Negotiate did not open the term modal.");
  const UiRect first_term{layout.modal_panel.x + 16.f * s,
                          layout.modal_panel.y + 74.f * s,
                          layout.modal_panel.width - 32.f * s, 36.f * s};
  (void)workspace.handle({InputEventType::LeftPressed, center(first_term)},
                         1280, 720);
  const UiRect confirm{layout.modal_panel.x + 16.f * s,
                       layout.modal_panel.y + layout.modal_panel.height -
                           92.f * s,
                       layout.modal_panel.width - 32.f * s, 36.f * s};
  const auto sent = workspace.handle(
      {InputEventType::LeftPressed, center(confirm)}, 1280, 720);
  require(sent.kind == DiplomacyWorkspaceCommandKind::Action &&
              sent.captured && sent.target_civilization_id == 7 &&
              sent.action == DiplomacyWorkspaceAction::propose_non_aggression &&
              sent.campaign_generation == 4 && sent.diplomacy_revision == 3 &&
              !workspace.modal_open(),
          "Confirmed negotiation did not emit the proposal action.");

  // Declare war goes through the danger confirmation.
  const UiRect war{layout.actions.x + 8.f * s,
                   layout.actions.y + 8.f * s + 72.f * s,
                   layout.actions.width - 16.f * s, 30.f * s};
  (void)workspace.handle({InputEventType::LeftPressed, center(war)}, 1280, 720);
  require(workspace.modal_open(), "Declare war skipped its confirmation.");
  const auto declared = workspace.handle(
      {InputEventType::LeftPressed, center(confirm)}, 1280, 720);
  require(declared.kind == DiplomacyWorkspaceCommandKind::Action &&
              declared.action == DiplomacyWorkspaceAction::declare_war &&
              declared.target_civilization_id == 7 &&
              declared.campaign_generation == 4 &&
              declared.diplomacy_revision == 3,
          "War confirmation emitted the wrong action.");

  // Explicit selection survives refresh and reordering by contact id, even for
  // an unidentified contact and an incoming view that still selects Alpha.
  auto next = sample_view();
  std::swap(next.contacts[0], next.contacts[1]);
  next.contacts[0].source_index = 0;
  next.contacts[1].source_index = 1;
  workspace.set_view(next);
  require(workspace.selected_contact_index() == 0,
          "Explicit contact selection did not survive reordering by contact id.");

  // A changed diplomacy quote invalidates a modal before old terms can issue
  // an action under the refreshed state.
  workspace.set_view(sample_view());
  (void)workspace.handle({InputEventType::LeftPressed, center(negotiate)},
                         1280, 720);
  require(workspace.modal_open(), "Negotiation modal did not reopen.");
  auto stale = sample_view();
  ++stale.diplomacy_revision;
  workspace.set_view(stale);
  require(!workspace.modal_open() &&
              workspace.notice().contains("Diplomacy state changed"),
          "A changed diplomacy revision left an old modal actionable.");
  const auto stale_confirm = workspace.handle(
      {InputEventType::LeftPressed, center(confirm)}, 1280, 720);
  require(stale_confirm.kind != DiplomacyWorkspaceCommandKind::Action,
          "A dismissed stale modal still issued an action.");

  // The negotiation modal lists every term; unavailable terms render disabled
  // with the domain's authoritative status as the hover why, and clicking one
  // is a captured no-op.
  auto war_view = sample_view();
  auto &war_sel = war_view.selected;
  war_sel.can_declare_war = false; // at war
  war_sel.can_offer_non_aggression = false;
  war_sel.can_request_access = false;
  war_sel.can_offer_ceasefire = true;
  war_sel.can_offer_peace = true;
  war_sel.political_status = "At war";
  NativeDiplomacyWorkspace war_workspace;
  war_workspace.open();
  war_workspace.set_view(war_view);
  (void)war_workspace.handle(
      {InputEventType::LeftPressed, center(negotiate)}, 1280, 720);
  require(war_workspace.modal_open(),
          "At-war negotiation modal did not open.");
  (void)war_workspace.handle({InputEventType::PointerMove,
                              center({layout.modal_panel.x + 16.f * s,
                                      layout.modal_panel.y + 74.f * s,
                                      layout.modal_panel.width - 32.f * s,
                                      36.f * s})},
                             1280, 720);
  DrawList terms_draw;
  war_workspace.render(terms_draw, 1280, 720, nullptr);
  require(has_text(terms_draw, "Non-aggression") &&
              has_text(terms_draw, "Request transit access") &&
              has_text(terms_draw, "Ceasefire") &&
              has_text(terms_draw, "Peace") &&
              has_text(terms_draw, "Grant transit access") &&
              has_text(terms_draw, "Deny transit access"),
          "Unavailable negotiation terms were hidden instead of disabled.");
  const UiRect first_term_rect{layout.modal_panel.x + 16.f * s,
                               layout.modal_panel.y + 74.f * s,
                               layout.modal_panel.width - 32.f * s,
                               36.f * s};
  const auto disabled_term = war_workspace.handle(
      {InputEventType::LeftPressed, center(first_term_rect)}, 1280, 720);
  require(disabled_term.captured &&
              disabled_term.kind == DiplomacyWorkspaceCommandKind::None &&
              war_workspace.modal_open(),
          "A disabled negotiation term dispatched a transition.");

  // Contact rows and proposal buttons cannot be activated through a clipped
  // edge after scrolling.
  auto crowded = sample_view();
  for (int index = 0; index < 8; ++index) {
    auto contact = crowded.contacts.front();
    contact.contact_id = "contact-extra-" + std::to_string(index);
    contact.source_index = static_cast<std::size_t>(index + 2);
    crowded.contacts.push_back(std::move(contact));
  }
  NativeDiplomacyWorkspace clipped_contacts;
  clipped_contacts.open();
  clipped_contacts.set_view(crowded);
  (void)clipped_contacts.handle(
      {InputEventType::Wheel, center(layout.contact_rows), {}, -1.f}, 1280,
      720);
  const auto below_contact_rows = Point{layout.contact_rows.x +
                                            layout.contact_rows.width * .5f,
                                        layout.contact_rows.y +
                                            layout.contact_rows.height + 2.f};
  const auto clipped_contact = clipped_contacts.handle(
      {InputEventType::LeftPressed, below_contact_rows}, 1280, 720);
  require(clipped_contact.kind != DiplomacyWorkspaceCommandKind::SelectContact,
          "A contact row accepted a click outside its visible region.");
  DrawList clipped_contact_draw;
  clipped_contacts.render(clipped_contact_draw, 1280, 720, nullptr);
  require_scrolled_draw_clipped(clipped_contact_draw, layout.contact_rows,
                                "A contact row rendered beyond its visible region.");

  auto many_proposals = sample_view();
  for (int index = 0; index < 5; ++index) {
    auto proposal = many_proposals.proposals.front();
    proposal.proposal_id = 100 + index;
    many_proposals.proposals.push_back(std::move(proposal));
  }
  NativeDiplomacyWorkspace clipped_proposals;
  clipped_proposals.open();
  clipped_proposals.set_view(many_proposals);
  (void)clipped_proposals.handle(
      {InputEventType::LeftPressed, center(proposals_tab)}, 1280, 720);
  (void)clipped_proposals.handle(
      {InputEventType::Wheel, center(layout.detail_rows), {}, -3.f}, 1280,
      720);
  const auto clipped_proposal = clipped_proposals.handle(
      {InputEventType::LeftPressed,
       {accept.x + accept.width * .5f, layout.detail_rows.y - 1.f}},
      1280, 720);
  require(clipped_proposal.kind != DiplomacyWorkspaceCommandKind::ProposalAction,
          "A proposal button accepted a click outside its visible region.");
  DrawList clipped_proposal_draw;
  clipped_proposals.render(clipped_proposal_draw, 1280, 720, nullptr);
  require_scrolled_draw_clipped(clipped_proposal_draw, layout.detail_rows,
                                "A proposal card rendered beyond its detail region.");
  (void)clipped_proposals.handle(
      {InputEventType::Wheel, center(layout.detail_rows), {}, 10000.f}, 1280,
      720);
  const auto restored_proposal = clipped_proposals.handle(
      {InputEventType::LeftPressed, center(accept)}, 1280, 720);
  require(restored_proposal.kind == DiplomacyWorkspaceCommandKind::ProposalAction &&
              restored_proposal.proposal_id == 11,
          "Detail scrolling did not stay within active proposal content.");

  // The intelligence control sits between its evidence and unresolved cards,
  // and both drawing and hit testing respect the detail viewport.
  NativeDiplomacyWorkspace intelligence;
  intelligence.open();
  intelligence.set_view(sample_view());
  const UiRect intelligence_tab{
      layout.tabs.x + 3.f * ((layout.tabs.width - 8.f * s * 4.f) / 5.f +
                              8.f * s),
      layout.tabs.y, (layout.tabs.width - 8.f * s * 4.f) / 5.f,
      layout.tabs.height};
  (void)intelligence.handle({InputEventType::LeftPressed, center(intelligence_tab)},
                            1280, 720);
  // The detail viewport is shorter than the intelligence content at 720p;
  // scroll down enough to bring the unresolved card into view.
  (void)intelligence.handle(
      {InputEventType::Wheel, center(layout.detail_rows), {}, -2.f}, 1280, 720);
  DrawList intelligence_draw;
  intelligence.render(intelligence_draw, 1280, 720, nullptr);
  require_scrolled_draw_clipped(intelligence_draw, layout.detail_rows,
                                "An intelligence control rendered beyond its detail region.");
  std::optional<Point> focus_label;
  std::optional<Point> unresolved_label;
  for (const auto &command : intelligence_draw.overlay)
    if (const auto *label = std::get_if<Text>(&command)) {
      if (label->value.contains("Last observation")) focus_label = label->at;
      if (label->value == "UNRESOLVED INFORMATION") unresolved_label = label->at;
    }
  require(focus_label && unresolved_label &&
              focus_label->y + 34.f * s <= unresolved_label->y - 8.f * s,
          "The intelligence focus control overlaps unresolved information.");

  const auto compact_layout = DiplomacyWorkspaceLayout::for_viewport(640, 360);
  const auto compact_s = compact_layout.scale;
  const UiRect compact_intelligence_tab{
      compact_layout.tabs.x +
          3.f * ((compact_layout.tabs.width - 8.f * compact_s * 4.f) / 5.f +
                 8.f * compact_s),
      compact_layout.tabs.y,
      (compact_layout.tabs.width - 8.f * compact_s * 4.f) / 5.f,
      compact_layout.tabs.height};
  NativeDiplomacyWorkspace clipped_focus;
  clipped_focus.open();
  clipped_focus.set_view(sample_view());
  (void)clipped_focus.handle(
      {InputEventType::LeftPressed, center(compact_intelligence_tab)}, 640, 360);
  (void)clipped_focus.handle(
      {InputEventType::Wheel, center(compact_layout.detail_rows), {}, -2.f},
      640, 360);
  const auto hidden_focus = clipped_focus.handle(
      {InputEventType::LeftPressed,
       {compact_layout.detail_rows.x + compact_layout.detail_rows.width * .5f,
        compact_layout.detail_rows.y - 1.f}},
      640, 360);
  require(hidden_focus.kind != DiplomacyWorkspaceCommandKind::FocusSystem,
          "A clipped intelligence control accepted an off-region click.");

  // Keyboard-focus contract: Tab/arrows ring every actionable rect in (y,x)
  // order, Home/End jump to the ends, Return/Space replay the click dispatch
  // at the focused rect, and the modal narrows the ring to its own controls.
  {
    NativeDiplomacyWorkspace keys;
    keys.open();
    keys.set_view(sample_view());
    const auto key = [&](std::uint32_t code, bool shift = false) {
      InputEvent event{};
      event.type = InputEventType::KeyPressed;
      event.key = code;
      event.shift = shift;
      return keys.handle(event, 1280, 720);
    };
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kDown = 0x40000051u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    constexpr std::uint32_t kF5 = 0x4000003fu;
    const auto focused_rect = [&] {
      DrawList ring_draw;
      keys.render(ring_draw, 1280, 720, nullptr);
      const auto *ring =
          std::get_if<StrokedRectangle>(&ring_draw.overlay.back());
      require(ring && ring->color.r == stellar::native_ui::color::focus.r &&
                  ring->color.g == stellar::native_ui::color::focus.g,
              "Focused diplomacy control rendered no accent ring.");
      return ring->bounds;
    };
    require(keys.focus() < 0, "Diplomacy focus ring present before any key.");
    require(key(kTab).captured && keys.focus() == 0 &&
                overlaps(focused_rect(), layout.close),
            "First Tab did not ring the close control.");
    require(key(kTab, true).captured && keys.focus() > 0,
            "Shift+Tab did not wrap to the last control.");
    const int last = keys.focus();
    require(key(kHome).captured && keys.focus() == 0 &&
                key(kEnd).captured && keys.focus() == last,
            "Home/End did not jump between the ring ends.");
    require(key(kDown).captured && keys.focus() == 0,
            "Down did not wrap from the last control to the first.");
    require(!key(kF5).captured, "An unrelated key was captured.");

    // The ring carries announcement labels for screen-reader consumers.
    require(keys.focused_label(1280, 720) == "RETURN",
            "Focused close control announced the wrong label.");
    require(key(kEnd).captured &&
                !keys.focused_label(1280, 720).empty(),
            "Last ring control announced an empty label.");
    (void)key(kHome);

    // Activate the negotiate action: walk the ring until it lands on the
    // negotiate button, then Return replays the click dispatch.
    const UiRect negotiate_rect{layout.actions.x + 8.f * s,
                                layout.actions.y + 8.f * s + 36.f * s,
                                layout.actions.width - 16.f * s, 30.f * s};
    bool found = false;
    for (int step = 0; step < 60 && !found; ++step) {
      if (overlaps(focused_rect(), negotiate_rect)) found = true;
      else (void)key(kDown);
    }
    require(found, "Ring never reached the negotiate action.");
    auto command = key(kReturn);
    require(command.captured && keys.modal_open() && keys.focus() < 0,
            "Return on the negotiate action did not open the term modal.");
    // The modal narrows the ring to its terms plus cancel.
    require(key(kTab).captured && keys.focus() == 0,
            "Modal ring did not restart on the first term.");
    command = key(kReturn); // first term reaches the confirmation modal
    require(command.captured && keys.modal_open(),
            "Term activation did not reach the confirmation modal.");
    require(key(kTab).captured && keys.focus() == 0,
            "Confirmation modal ring did not restart.");
    command = key(kReturn); // confirm button
    require(command.kind == DiplomacyWorkspaceCommandKind::Action &&
                command.action ==
                    DiplomacyWorkspaceAction::propose_non_aggression &&
                command.target_civilization_id == 7 &&
                command.campaign_generation == 4 &&
                command.diplomacy_revision == 3 && command.captured,
            "Keyboard-confirmed negotiation did not emit the action.");

    // A contact row selects by keyboard through the same dispatch.
    const UiRect second_contact{layout.contact_rows.x + 4.f * s,
                                layout.contact_rows.y + 4.f * s + 62.f * s,
                                layout.contact_rows.width - 8.f * s,
                                58.f * s};
    (void)key(kHome);
    found = false;
    for (int step = 0; step < 60 && !found; ++step) {
      if (overlaps(focused_rect(), second_contact)) found = true;
      else (void)key(kDown);
    }
    require(found, "Ring never reached a contact row.");
    command = key(kSpace);
    require(command.kind == DiplomacyWorkspaceCommandKind::SelectContact &&
                command.contact_index == 1 && command.captured,
            "Space on a contact row did not select it.");

    // A pointer press hands ownership back to the pointer.
    require(keys.handle({InputEventType::LeftPressed, center(layout.surface)},
                        1280, 720)
                    .captured &&
                keys.focus() < 0,
            "Pointer press did not clear the diplomacy ring.");
  }

  // A contact with no legal actions still shows all three action slots as
  // disabled; hover surfaces the authoritative status as the why, and clicks
  // are captured without dispatching.
  auto dark = sample_view();
  auto &dsel = dark.selected;
  dsel.has_visible_communication = false;
  dsel.can_attempt_communication = false;
  dsel.can_offer_non_aggression = false;
  dsel.can_request_access = false;
  dsel.can_offer_peace = false;
  dsel.can_offer_ceasefire = false;
  dsel.can_set_access = false;
  dsel.can_declare_war = false;
  dsel.communication_status = "Channel lost";
  dsel.political_status = "At war";
  NativeDiplomacyWorkspace disabled_actions;
  disabled_actions.open();
  disabled_actions.set_view(dark);
  (void)disabled_actions.handle({InputEventType::PointerMove,
                                 {negotiate.x + negotiate.width * .5f,
                                  negotiate.y + negotiate.height * .5f}},
                                1280, 720);
  DrawList disabled_draw;
  disabled_actions.render(disabled_draw, 1280, 720, nullptr);
  require(has_text(disabled_draw, "Establish communication") &&
              has_text(disabled_draw, "Negotiate") &&
              has_text(disabled_draw, "Declare war"),
          "Unavailable diplomacy actions were hidden instead of disabled.");
  require(has_text(disabled_draw, "Channel lost"),
          "A disabled diplomacy action did not explain its blocker.");
  for (int index = 0; index < 3; ++index) {
    const UiRect slot{layout.actions.x + 8.f * s,
                      layout.actions.y + 8.f * s +
                          static_cast<float>(index) * 36.f * s,
                      layout.actions.width - 16.f * s, 30.f * s};
    const auto hit = disabled_actions.handle(
        {InputEventType::LeftPressed, center(slot)}, 1280, 720);
    require(hit.captured &&
                hit.kind == DiplomacyWorkspaceCommandKind::None &&
                !disabled_actions.modal_open(),
            "A disabled diplomacy action dispatched a command.");
  }

  // Hovering a relationship meter explains what it measures.
  NativeDiplomacyWorkspace meters;
  meters.open();
  meters.set_view(dark);
  (void)meters.handle({InputEventType::PointerMove,
                       {layout.meters.x + 4.f * s,
                        layout.meters.y + 4.f * s}},
                      1280, 720);
  DrawList meters_draw;
  meters.render(meters_draw, 1280, 720, nullptr);
  require(has_text(meters_draw, "TRUST") &&
              has_text(meters_draw,
                       "How reliably this contact honors its agreements."),
          "A relationship meter did not explain itself on hover.");

  // The close control emits Close.
  const auto closed = workspace.handle(
      {InputEventType::LeftPressed, center(layout.close)}, 1280, 720);
  require(closed.kind == DiplomacyWorkspaceCommandKind::Close &&
              closed.captured,
          "Return button did not emit Close.");

  // Closing hides all workspace paint work but retains the latest view for reopen.
  workspace.close();
  portrait_path.clear();
  DrawList closed_draw;
  workspace.render(closed_draw, 1280, 720, &portrait_provider);
  require(closed_draw.overlay.empty() && portrait_path.empty() && workspace.view().has_value(),
          "Closed diplomacy workspace still rendered or discarded its retained view.");
  workspace.open();
  DrawList reopened_draw;
  workspace.render(reopened_draw, 1280, 720, &portrait_provider);
  require(has_text(reopened_draw, "RELATIONS") && !portrait_path.empty(),
          "Reopened diplomacy workspace did not render its retained view.");

  workspace.set_notice("The diplomacy state changed; review the current terms.",
                       false);
  DrawList notice_draw;
  workspace.render(notice_draw, 1280, 720, nullptr);
  require(has_text(notice_draw, "review the current terms"),
          "Rejection notice did not reach the result line.");

  std::cout << "Native diplomacy workspace input, modal, selection and render "
               "tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << "native diplomacy workspace test failed: " << e.what() << '\n';
  return 1;
}
