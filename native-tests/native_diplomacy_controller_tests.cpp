#include "native_diplomacy_controller.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <thread>

using namespace stellar::core;
using namespace stellar::native_diplomacy;
namespace fs = std::filesystem;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

CampaignFrame fresh_frame(const fs::path &research_root,
                          const fs::path &catalog_path) {
  auto world = seed_persistable_fresh_campaign(
      113500, load_nearby_catalog(catalog_path),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  return {IntegratedAdaptiveCampaignRuntime::create_fresh(
              load_adaptive_research_strategic_runtime(research_root),
              std::move(world)),
          StrategicClock{}, CampaignFramePolicy::Player};
}

void projections(const fs::path &research_root, const fs::path &catalog_path) {
  auto frame = fresh_frame(research_root, catalog_path);
  auto &world = frame.runtime().world().campaign();
  const auto observer = world.player_civilization_id;
  const auto foreign = std::ranges::find_if(
      world.civilizations,
      [&](const auto &c) { return c.id != observer; });
  require(foreign != world.civilizations.end(),
          "fresh campaign produced no foreign civilization");
  const auto second = std::ranges::find_if(
      world.civilizations,
      [&](const auto &c) { return c.id != observer && c.id != foreign->id; });
  require(second != world.civilizations.end(),
          "fresh campaign produced no second foreign civilization");
  const auto known = world.knowledge.known_systems(observer);
  require(!known.empty(), "observer knows no systems");
  const auto home = known.front();
  auto &diplomacy = frame.runtime().diplomacy();
  DiplomacySimulation simulation(diplomacy);

  NativeDiplomacyController controller;
  const auto empty = controller.build(frame, 7, 0);
  require(empty.contacts.empty() && !empty.selected.present,
          "empty campaign projected a diplomatic contact");
  require(empty.date == "2050-01-01",
          "campaign epoch date diverged from the calendar");
  require(empty.proposals.empty() && empty.agreements.empty() &&
              empty.history.empty(),
          "empty campaign leaked diplomatic history");

  // Alpha: identified contact with a communication channel and a relationship.
  const auto alpha_contact = simulation.process_contact_opportunity(
      {observer, "contact-alpha", foreign->id, 1000, home,
       ContactAwareness::communication_available, ContactCondition::active,
       true, .9});
  const auto reciprocal = simulation.process_contact_opportunity(
      {foreign->id, "contact-of-us", observer, 1000, std::nullopt,
       ContactAwareness::communication_available, ContactCondition::active,
       true, .9});
  // Beta: detected but unidentified; nothing may leak beyond the observation.
  const auto beta_contact = simulation.process_contact_opportunity(
      {observer, "contact-beta", std::nullopt, 1100, std::nullopt,
       ContactAwareness::detected_unidentified, ContactCondition::active,
       false, .4});
  // Gamma: identified without a channel.
  const auto gamma_contact = simulation.process_contact_opportunity(
      {observer, "contact-gamma", second->id, 1200, home,
       ContactAwareness::identified, ContactCondition::active, false, .8});
  require(alpha_contact.target_civilization_id == foreign->id &&
              reciprocal.target_civilization_id == observer &&
              !beta_contact.target_civilization_id &&
              gamma_contact.target_civilization_id == second->id,
          "authored contact opportunities diverged");

  RelationshipImpact impact;
  impact.trust_delta = .6;
  impact.respect_delta = .5;
  impact.cooperation_delta = .7;
  impact.fear_delta = -.2;
  impact.reason = "shared survey data";
  simulation.apply_relationship_impact(observer, foreign->id, impact, 1300);
  simulation.set_access_permission(foreign->id, observer,
                                   AccessPermission::granted, 1400);
  const auto incoming_id = simulation.send_proposal(
      foreign->id, observer, DiplomaticProposalKind::agreement, 1500,
      "Join a pact of non-aggression.", DiplomaticAgreementType::non_aggression);
  const auto outgoing_id = simulation.send_proposal(
      observer, foreign->id, DiplomaticProposalKind::access_request, 1600,
      "Request transit through your systems.");

  auto view = controller.build(frame, 7, 0);
  require(view.contacts.size() == 3, "projected contacts diverged");
  const auto &alpha = view.contacts[0];
  require(alpha.identified && alpha.civilization_id == foreign->id &&
              alpha.display_name == foreign->name &&
              alpha.communication_available &&
              alpha.status == "Peace",
          "identified contact projection diverged");
  require(alpha.species_name && !alpha.species_name->empty(),
          "identified contact lost its species identity");
  const auto home_system =
      std::ranges::find(world.systems, home, &StellarSystem::id);
  require(home_system != world.systems.end() &&
              alpha.last_observed_system_name == home_system->name,
          "known last-observation system lost its label");
  const auto &beta = view.contacts[1];
  require(!beta.identified && !beta.civilization_id &&
              beta.display_name == "UNKNOWN CONTACT" &&
              beta.status == "IDENTITY UNKNOWN" && !beta.species_name &&
              !beta.cooperation && beta.pending_proposal_count == 0,
          "unidentified contact leaked identity or relationship data");
  const auto &gamma = view.contacts[2];
  require(gamma.identified && !gamma.communication_available,
          "contact without a channel reported one");

  const auto &selected = view.selected;
  require(selected.present && selected.contact_index == 0 &&
              selected.target_civilization_id == foreign->id &&
              selected.has_visible_communication &&
              selected.species_id == foreign->species_id,
          "selected identified contact projection diverged");
  require(selected.trust && *selected.trust > .59 && *selected.trust < .61 &&
              selected.cooperation && *selected.cooperation > .69 &&
              selected.fear && *selected.fear < .01,
          "relationship metrics leaked or diverged");
  require(selected.their_access == "GRANTED" &&
              selected.our_access == "UNSPECIFIED",
          "access permissions diverged");
  require(view.agreements.empty(),
          "no accepted agreement appeared as active");
  require(view.proposals.size() == 2, "pending proposals diverged");
  const auto &incoming = view.proposals.front();
  require(incoming.direction == "INCOMING" && incoming.can_accept &&
              incoming.can_reject && !incoming.can_withdraw &&
              incoming.agreement_type == "Non Aggression",
          "incoming proposal projection diverged");
  const auto &outgoing = view.proposals.back();
  require(outgoing.direction == "OUTGOING" && outgoing.can_withdraw &&
              !outgoing.can_accept,
          "outgoing proposal projection diverged");
  require(!view.history.empty() && view.history.size() <= 16,
          "observer history diverged");
  require(!selected.recent_events.empty(),
          "recent pair history was lost");

  // Selection-independent signature: rebuilding for another index keeps the
  // same revision until the world changes.
  const auto clamped = controller.build(frame, 7, 99);
  require(clamped.selected.contact_index == view.contacts.size() - 1 &&
              clamped.diplomacy_revision == view.diplomacy_revision,
          "contact index failed to clamp or selection changed the revision");
  require(clamped.selected.target_civilization_id == second->id &&
              !clamped.selected.has_visible_communication &&
              clamped.selected.political_status == "No formal relationship",
          "no-channel contact selection diverged");

  const auto unidentified =
      controller.build(frame, 7, beta.source_index).selected;
  require(unidentified.present && !unidentified.target_civilization_id &&
              !unidentified.trust && !unidentified.species_id &&
              unidentified.political_status == "Identity unknown" &&
              unidentified.access_summary.find("unavailable") !=
                  std::string::npos,
          "unidentified selection leaked counterpart state");

  // Command path: accept the incoming non-aggression proposal.
  const auto accepted = controller.execute(
      frame, 7, view.diplomacy_revision,
      DiplomacyWorkspaceAction::accept_proposal, std::nullopt, incoming_id);
  require(accepted.accepted,
          "visible incoming proposal could not be accepted");
  auto after = controller.build(frame, 7, 0);
  require(after.diplomacy_revision != view.diplomacy_revision,
          "accepted command did not invalidate the projection");
  require(after.proposals.size() == 1 &&
              after.proposals.front().proposal_id == outgoing_id,
          "resolved proposal remained pending");
  const auto pact = std::ranges::find_if(
      after.agreements,
      [](const auto &a) { return a.type == "Non Aggression"; });
  require(pact != after.agreements.end() && pact->status == "ACTIVE" &&
              !pact->started.empty(),
          "accepted proposal did not activate the agreement");

  // Stale-state protection: an external mutation rejects the quoted revision.
  const auto quoted = after.diplomacy_revision;
  RelationshipImpact drift;
  drift.trust_delta = .1;
  drift.reason = "border skirmish";
  simulation.apply_relationship_impact(observer, second->id, drift, 1700);
  const auto stale = controller.execute(
      frame, 7, quoted, DiplomacyWorkspaceAction::withdraw_proposal,
      std::nullopt, outgoing_id);
  require(!stale.accepted, "changed diplomacy state accepted a stale quote");
  auto renewed = controller.build(frame, 7, 0);
  require(renewed.diplomacy_revision != quoted,
          "external war declaration did not bump the revision");

  const auto withdrawn = controller.execute(
      frame, 7, renewed.diplomacy_revision,
      DiplomacyWorkspaceAction::withdraw_proposal, std::nullopt, outgoing_id);
  require(withdrawn.accepted,
          "current outgoing proposal could not be withdrawn");
  const auto foreign_generation = controller.execute(
      frame, 9, renewed.diplomacy_revision,
      DiplomacyWorkspaceAction::declare_war, foreign->id, std::nullopt);
  require(!foreign_generation.accepted,
          "foreign campaign generation accepted an order");

  bool rejected = false;
  std::thread worker([&] {
    try {
      (void)controller.build(frame, 7, 0);
    } catch (const std::logic_error &) {
      rejected = true;
    }
  });
  worker.join();
  require(rejected, "foreign thread was accepted by the controller");

  // War declaration against an identified counterpart changes the political
  // row and history.
  auto before_war = controller.build(frame, 7, 0);
  const auto war = controller.execute(
      frame, 7, before_war.diplomacy_revision,
      DiplomacyWorkspaceAction::declare_war, foreign->id, std::nullopt);
  require(war.accepted, "visible war declaration was rejected");
  const auto at_war = controller.build(frame, 7, 0);
  require(at_war.contacts.front().status == "AtWar" &&
              at_war.selected.political_status == "AtWar",
          "war declaration did not update the projected political state");
  require(std::ranges::any_of(at_war.history, [](const auto &event) {
            return event.kind == "War Declared";
          }),
          "war declaration missing from observer history");

  // Contact filters.
  const auto all = filter_native_diplomacy_contacts(
      at_war, NativeDiplomacyContactFilter::all);
  require(all.size() == 3, "all filter diverged");
  require(filter_native_diplomacy_contacts(
              at_war, NativeDiplomacyContactFilter::identified)
              .size() == 2,
          "identified filter diverged");
  require(filter_native_diplomacy_contacts(
              at_war, NativeDiplomacyContactFilter::unidentified)
              .size() == 1,
          "unidentified filter diverged");
  require(filter_native_diplomacy_contacts(
              at_war, NativeDiplomacyContactFilter::at_war)
              .size() == 1,
          "at-war filter diverged");
  require(filter_native_diplomacy_contacts(
              at_war, NativeDiplomacyContactFilter::communication_available)
              .size() == 1,
          "channel filter diverged");
  require(filter_native_diplomacy_contacts(
              at_war, NativeDiplomacyContactFilter::pending_proposal)
              .empty(),
          "pending filter diverged after resolutions");
}
} // namespace

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument("Usage: test research-root catalog");
  projections(fs::absolute(argv[1]), fs::absolute(argv[2]));
  std::cout << "Native diplomacy observer projection, commands, staleness and "
               "secrecy tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << "native diplomacy controller test failed: " << e.what() << '\n';
  return 1;
}
