#include "native_diplomacy_workspace.hpp"

#include <array>
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
  selected.species_id = "terran_baseline";
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
  view.agreements.push_back({5, "Access", "ACTIVE", "2050-06-01", ""});
  view.history.push_back({9, "2050-03-01", "Contact Established",
                          "A channel opened with Nova Concord."});
  return view;
}
} // namespace

int main() try {
  for (const auto [width, height] :
       std::array{std::pair{640, 360}, std::pair{1280, 720},
                  std::pair{1920, 1080}, std::pair{3840, 2160}}) {
    const auto layout = DiplomacyWorkspaceLayout::for_viewport(width, height);
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
  workspace.render(draw, 1280, 720, nullptr);
  require(has_text(draw, "Nova Concord") && has_text(draw, "CONTACT DIRECTORY") &&
              has_text(draw, "RELATIONSHIP") &&
              has_text(draw, "COMMUNICATION CHANNEL AVAILABLE"),
          "Diplomacy workspace dropped its main panels.");

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
              accepted.action == DiplomacyWorkspaceAction::accept_proposal,
          "Accept button did not emit the proposal command.");

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
              declared.target_civilization_id == 7,
          "War confirmation emitted the wrong action.");

  // Selection survives a refreshed view keyed by civilization id.
  auto next = sample_view();
  next.contacts.insert(next.contacts.begin(), next.contacts[1]);
  next.contacts[0].source_index = 0;
  next.contacts[0].contact_id = "contact-beta";
  next.contacts[1].source_index = 1;
  next.contacts[2].source_index = 2;
  workspace.set_view(next);
  require(workspace.selected_contact_index() == 1,
          "Selection did not track the civilization across reordering.");

  // The close control emits Close.
  const auto closed = workspace.handle(
      {InputEventType::LeftPressed, center(layout.close)}, 1280, 720);
  require(closed.kind == DiplomacyWorkspaceCommandKind::Close &&
              closed.captured,
          "Return button did not emit Close.");

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
