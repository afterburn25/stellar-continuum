#include <stellar/core/diplomacy_state.hpp>

#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/diplomacy_state_access.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

using Json = nlohmann::ordered_json;
using namespace stellar::core;

namespace {
void require(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}
std::string read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string hex(std::span<const std::uint8_t> value) {
  constexpr std::string_view digits = "0123456789ABCDEF";
  std::string result;
  for (auto byte : value) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}
std::string fingerprint(const std::filesystem::path &root) {
  const auto bytes =
      read(root / "src/Game/Simulation/Diplomacy/DiplomacySystem.cs");
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
}
bool operator==(const std::string &left, const Json &right) {
  return right.is_string() && left == right.get<std::string>();
}
double number(const Json &v) {
  if (v.is_number())
    return v.get<double>();
  const auto s = v.get<std::string>();
  if (s == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (s == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (s == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("bad number");
}
template <class T> std::optional<T> opt(const Json &j, std::string_view key) {
  const auto &v = j.at(key);
  return v.is_null() ? std::nullopt : std::optional<T>(v.get<T>());
}
template <class E> E en(const Json &j, std::string_view key) {
  return static_cast<E>(j.at(key).get<int>());
}

FirstContactOpportunity opportunity(const Json &j) {
  return {j.at("ObserverCivilizationId").get<int>(),
          j.at("ContactId").get<std::string>(),
          opt<int>(j, "TargetCivilizationId"),
          j.at("ObservedAtTick").get<std::int64_t>(),
          opt<int>(j, "ObservedSystemId"),
          en<ContactAwareness>(j, "Awareness"),
          en<ContactCondition>(j, "Condition"),
          j.at("CommunicationAvailable").get<bool>(),
          number(j.at("Confidence"))};
}

DiplomaticContactSnapshot contact(const Json &j) {
  return {j.at("ObserverCivilizationId").get<int>(),
          j.at("ContactId").get<std::string>(),
          opt<int>(j, "TargetCivilizationId"),
          j.at("FirstObservedTick").get<std::int64_t>(),
          j.at("LastObservedTick").get<std::int64_t>(),
          opt<int>(j, "LastObservedSystemId"),
          en<ContactAwareness>(j, "Awareness"),
          en<ContactCondition>(j, "Condition"),
          j.at("CommunicationAvailable").get<bool>(),
          number(j.at("Confidence"))};
}
DiplomaticGrievanceSnapshot grievance(const Json &j) {
  return {j.at("CreatedAtTick").get<std::int64_t>(),
          j.at("SourceCivilizationId").get<int>(), number(j.at("Severity")),
          j.at("Reason").get<std::string>()};
}
DiplomaticRelationshipSnapshot relationship(const Json &j) {
  DiplomaticRelationshipSnapshot v{
      j.at("CivilizationAId").get<int>(),
      j.at("CivilizationBId").get<int>(),
      en<DiplomaticPoliticalState>(j, "PoliticalState"),
      number(j.at("Trust")),
      number(j.at("Hostility")),
      number(j.at("Fear")),
      number(j.at("Respect")),
      number(j.at("Cooperation")),
      {}};
  if (!j.at("Grievances").is_null())
    for (const auto &x : j.at("Grievances"))
      v.grievances.push_back(grievance(x));
  return v;
}
DiplomaticAccessSnapshot access(const Json &j) {
  return {j.at("GrantorCivilizationId").get<int>(),
          j.at("VisitorCivilizationId").get<int>(),
          en<AccessPermission>(j, "Permission"),
          j.at("UpdatedAtTick").get<std::int64_t>()};
}
TerritorialClaimSnapshot claim(const Json &j) {
  TerritorialClaimSnapshot v{j.at("ClaimId").get<std::int64_t>(),
                             j.at("ClaimantCivilizationId").get<int>(),
                             j.at("SystemId").get<int>(),
                             j.at("AssertedAtTick").get<std::int64_t>(),
                             j.at("Active").get<bool>(),
                             {}};
  if (!j.at("KnownToCivilizationIds").is_null())
    v.known_to_civilization_ids =
        j.at("KnownToCivilizationIds").get<std::vector<int>>();
  return v;
}
TerritorialClaimResponseSnapshot response(const Json &j) {
  return {j.at("ClaimId").get<std::int64_t>(),
          j.at("RespondingCivilizationId").get<int>(),
          en<TerritorialClaimResponse>(j, "Response"),
          j.at("RespondedAtTick").get<std::int64_t>()};
}
DiplomaticAgreementSnapshot agreement(const Json &j) {
  return {j.at("AgreementId").get<std::int64_t>(),
          j.at("CivilizationAId").get<int>(),
          j.at("CivilizationBId").get<int>(),
          en<DiplomaticAgreementType>(j, "Type"),
          en<DiplomaticAgreementStatus>(j, "Status"),
          j.at("StartedAtTick").get<std::int64_t>(),
          opt<std::int64_t>(j, "EndedAtTick"),
          opt<std::string>(j, "ExternalTermsReference")};
}
DiplomaticProposalSnapshot proposal(const Json &j) {
  DiplomaticProposalSnapshot v;
  v.proposal_id = j.at("ProposalId").get<std::int64_t>();
  v.proposer_civilization_id = j.at("ProposerCivilizationId").get<int>();
  v.recipient_civilization_id = j.at("RecipientCivilizationId").get<int>();
  v.kind = en<DiplomaticProposalKind>(j, "Kind");
  if (!j.at("AgreementType").is_null())
    v.agreement_type =
        static_cast<DiplomaticAgreementType>(j.at("AgreementType").get<int>());
  v.status = en<DiplomaticProposalStatus>(j, "Status");
  v.created_at_tick = j.at("CreatedAtTick").get<std::int64_t>();
  v.resolved_at_tick = opt<std::int64_t>(j, "ResolvedAtTick");
  v.summary = j.at("Summary").get<std::string>();
  v.external_terms_reference = opt<std::string>(j, "ExternalTermsReference");
  return v;
}
DiplomaticHistoryEventSnapshot history(const Json &j) {
  DiplomaticHistoryEventSnapshot v;
  v.event_id = j.at("EventId").get<std::int64_t>();
  v.tick = j.at("Tick").get<std::int64_t>();
  v.kind = en<DiplomaticEventKind>(j, "Kind");
  v.primary_civilization_id = j.at("PrimaryCivilizationId").get<int>();
  v.secondary_civilization_id = opt<int>(j, "SecondaryCivilizationId");
  v.system_id = opt<int>(j, "SystemId");
  v.summary = j.at("Summary").get<std::string>();
  if (!j.at("KnownToCivilizationIds").is_null())
    v.known_to_civilization_ids =
        j.at("KnownToCivilizationIds").get<std::vector<int>>();
  return v;
}
DiplomacyStateSnapshot snapshot(const Json &j) {
  DiplomacyStateSnapshot v;
  auto load = [&](std::string_view key, auto fn, auto &out) {
    if (!j.at(key).is_null())
      for (const auto &x : j.at(key))
        out.push_back(fn(x));
  };
  load("Contacts", contact, v.contacts);
  load("Relationships", relationship, v.relationships);
  load("AccessPermissions", access, v.access_permissions);
  load("Claims", claim, v.claims);
  load("ClaimResponses", response, v.claim_responses);
  load("Agreements", agreement, v.agreements);
  load("Proposals", proposal, v.proposals);
  load("RecentHistory", history, v.recent_history);
  v.next_claim_id = j.at("NextClaimId").get<std::int64_t>();
  v.next_agreement_id = j.at("NextAgreementId").get<std::int64_t>();
  v.next_proposal_id = j.at("NextProposalId").get<std::int64_t>();
  v.next_event_id = j.at("NextEventId").get<std::int64_t>();
  return v;
}

Json jcontact(const DiplomaticContactSnapshot &v) {
  return {{"ObserverCivilizationId", v.observer_civilization_id},
          {"ContactId", v.contact_id},
          {"TargetCivilizationId", v.target_civilization_id
                                       ? Json(*v.target_civilization_id)
                                       : Json(nullptr)},
          {"FirstObservedTick", v.first_observed_tick},
          {"LastObservedTick", v.last_observed_tick},
          {"LastObservedSystemId", v.last_observed_system_id
                                       ? Json(*v.last_observed_system_id)
                                       : Json(nullptr)},
          {"Awareness", static_cast<int>(v.awareness)},
          {"Condition", static_cast<int>(v.condition)},
          {"CommunicationAvailable", v.communication_available},
          {"Confidence", v.confidence}};
}
Json jgrievance(const DiplomaticGrievanceSnapshot &v) {
  return {{"CreatedAtTick", v.created_at_tick},
          {"SourceCivilizationId", v.source_civilization_id},
          {"Severity", v.severity},
          {"Reason", v.reason}};
}
Json jrelationship(const DiplomaticRelationshipSnapshot &v) {
  Json g = Json::array();
  for (const auto &x : v.grievances)
    g.push_back(jgrievance(x));
  return {{"CivilizationAId", v.civilization_a_id},
          {"CivilizationBId", v.civilization_b_id},
          {"PoliticalState", static_cast<int>(v.political_state)},
          {"Trust", v.trust},
          {"Hostility", v.hostility},
          {"Fear", v.fear},
          {"Respect", v.respect},
          {"Cooperation", v.cooperation},
          {"Grievances", g}};
}
Json jaccess(const DiplomaticAccessSnapshot &v) {
  return {{"GrantorCivilizationId", v.grantor_civilization_id},
          {"VisitorCivilizationId", v.visitor_civilization_id},
          {"Permission", static_cast<int>(v.permission)},
          {"UpdatedAtTick", v.updated_at_tick}};
}
Json jclaim(const TerritorialClaimSnapshot &v) {
  return {{"ClaimId", v.claim_id},
          {"ClaimantCivilizationId", v.claimant_civilization_id},
          {"SystemId", v.system_id},
          {"AssertedAtTick", v.asserted_at_tick},
          {"Active", v.active},
          {"KnownToCivilizationIds", v.known_to_civilization_ids}};
}
Json jresponse(const TerritorialClaimResponseSnapshot &v) {
  return {{"ClaimId", v.claim_id},
          {"RespondingCivilizationId", v.responding_civilization_id},
          {"Response", static_cast<int>(v.response)},
          {"RespondedAtTick", v.responded_at_tick}};
}
Json jagreement(const DiplomaticAgreementSnapshot &v) {
  return {
      {"AgreementId", v.agreement_id},
      {"CivilizationAId", v.civilization_a_id},
      {"CivilizationBId", v.civilization_b_id},
      {"Type", static_cast<int>(v.type)},
      {"Status", static_cast<int>(v.status)},
      {"StartedAtTick", v.started_at_tick},
      {"EndedAtTick", v.ended_at_tick ? Json(*v.ended_at_tick) : Json(nullptr)},
      {"ExternalTermsReference", v.external_terms_reference
                                     ? Json(*v.external_terms_reference)
                                     : Json(nullptr)}};
}
Json jproposal(const DiplomaticProposalSnapshot &v) {
  return {{"ProposalId", v.proposal_id},
          {"ProposerCivilizationId", v.proposer_civilization_id},
          {"RecipientCivilizationId", v.recipient_civilization_id},
          {"Kind", static_cast<int>(v.kind)},
          {"AgreementType", v.agreement_type
                                ? Json(static_cast<int>(*v.agreement_type))
                                : Json(nullptr)},
          {"Status", static_cast<int>(v.status)},
          {"CreatedAtTick", v.created_at_tick},
          {"ResolvedAtTick",
           v.resolved_at_tick ? Json(*v.resolved_at_tick) : Json(nullptr)},
          {"Summary", v.summary},
          {"ExternalTermsReference", v.external_terms_reference
                                         ? Json(*v.external_terms_reference)
                                         : Json(nullptr)}};
}
Json jhistory(const DiplomaticHistoryEventSnapshot &v) {
  return {{"EventId", v.event_id},
          {"Tick", v.tick},
          {"Kind", static_cast<int>(v.kind)},
          {"PrimaryCivilizationId", v.primary_civilization_id},
          {"SecondaryCivilizationId", v.secondary_civilization_id
                                          ? Json(*v.secondary_civilization_id)
                                          : Json(nullptr)},
          {"SystemId", v.system_id ? Json(*v.system_id) : Json(nullptr)},
          {"Summary", v.summary},
          {"KnownToCivilizationIds", v.known_to_civilization_ids}};
}
template <class V, class F> Json array(const V &values, F fn) {
  Json r = Json::array();
  for (const auto &v : values)
    r.push_back(fn(v));
  return r;
}
Json jsnapshot(const DiplomacyStateSnapshot &v) {
  return {{"Contacts", array(v.contacts, jcontact)},
          {"Relationships", array(v.relationships, jrelationship)},
          {"AccessPermissions", array(v.access_permissions, jaccess)},
          {"Claims", array(v.claims, jclaim)},
          {"ClaimResponses", array(v.claim_responses, jresponse)},
          {"Agreements", array(v.agreements, jagreement)},
          {"Proposals", array(v.proposals, jproposal)},
          {"RecentHistory", array(v.recent_history, jhistory)},
          {"NextClaimId", v.next_claim_id},
          {"NextAgreementId", v.next_agreement_id},
          {"NextProposalId", v.next_proposal_id},
          {"NextEventId", v.next_event_id}};
}
Json jview(const DiplomaticStateView &v) {
  Json contacts = Json::array();
  for (const auto &x : v.contacts)
    contacts.push_back(
        {{"ContactId", x.contact_id},
         {"TargetCivilizationId", x.target_civilization_id
                                      ? Json(*x.target_civilization_id)
                                      : Json(nullptr)},
         {"Awareness", static_cast<int>(x.awareness)},
         {"Condition", static_cast<int>(x.condition)},
         {"CommunicationAvailable", x.communication_available},
         {"Confidence", x.confidence},
         {"LastObservedTick", x.last_observed_tick},
         {"LastObservedSystemId", x.last_observed_system_id
                                      ? Json(*x.last_observed_system_id)
                                      : Json(nullptr)}});
  Json relationships = Json::array();
  for (const auto &x : v.relationships) {
    Json g = array(x.grievances, jgrievance);
    relationships.push_back(
        {{"OtherCivilizationId", x.other_civilization_id},
         {"PoliticalState", static_cast<int>(x.political_state)},
         {"Trust", x.trust},
         {"Hostility", x.hostility},
         {"Fear", x.fear},
         {"Respect", x.respect},
         {"Cooperation", x.cooperation},
         {"Grievances", g}});
  }
  return {{"ObserverCivilizationId", v.observer_civilization_id},
          {"Contacts", contacts},
          {"Relationships", relationships},
          {"AccessPermissions", array(v.access_permissions, jaccess)},
          {"Claims", array(v.claims, jclaim)},
          {"ClaimResponses", array(v.claim_responses, jresponse)},
          {"Agreements", array(v.agreements, jagreement)},
          {"Proposals", array(v.proposals, jproposal)},
          {"RecentEvents", array(v.recent_events, jhistory)}};
}
Json projection(const DiplomacyState &state) {
  const auto by_id = state.get_contact(1, "known");
  const auto by_target = state.get_contact(1, 2);
  const auto rel = state.get_relationship(1, 2);
  return {{"Snapshot", jsnapshot(state.snapshot())},
          {"Observer1", jview(state.build_view_for(1))},
          {"Observer2", jview(state.build_view_for(2))},
          {"Observer3", jview(state.build_view_for(3))},
          {"ContactById", by_id ? jcontact(*by_id) : Json(nullptr)},
          {"ContactByTarget", by_target ? jcontact(*by_target) : Json(nullptr)},
          {"Relationship12", rel ? jrelationship(*rel) : Json(nullptr)},
          {"Access12", static_cast<int>(state.get_access_permission(1, 2))},
          {"Transit12", state.is_transit_authorized(1, 2)}};
}
Json primitive_projection(const DiplomacyState &state,
                          std::string_view watched_contact) {
  const auto value = state.snapshot();
  const auto watched = state.get_contact(1, watched_contact);
  Json proposal_ids = Json::array();
  for (const auto &item : value.proposals)
    proposal_ids.push_back(item.proposal_id);
  return {{"ContactCount", value.contacts.size()},
          {"FirstContact", value.contacts.empty()
                               ? Json(nullptr)
                               : jcontact(value.contacts.front())},
          {"LastContact", value.contacts.empty()
                              ? Json(nullptr)
                              : jcontact(value.contacts.back())},
          {"WatchedContact", watched ? jcontact(*watched) : Json(nullptr)},
          {"ProposalCount", value.proposals.size()},
          {"ProposalIds", proposal_ids},
          {"NextProposalId", value.next_proposal_id}};
}
struct Error {
  std::string type, message;
};
template <class F> std::optional<Error> invoke(F &&f) {
  try {
    f();
  } catch (const DiplomacyArgumentRangeError &e) {
    return Error{"ArgumentOutOfRangeException", e.what()};
  } catch (const DiplomacyArgumentError &e) {
    return Error{"ArgumentException", e.what()};
  } catch (const DiplomacyOperationError &e) {
    return Error{"InvalidOperationException", e.what()};
  } catch (const DiplomacyOverflowError &e) {
    return Error{"OverflowException", e.what()};
  }
  return std::nullopt;
}
void error(const Json &e, const std::optional<Error> &a,
           const std::string &name) {
  if (e.is_null()) {
    require(!a, name + " failed");
    return;
  }
  require(a.has_value(), name + " succeeded");
  require(a->type == e.at("Type").get<std::string>() &&
              a->message == e.at("Message").get<std::string>(),
          name + " error differed: " + a->message);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture and repository root.");
    const auto fixture_path = std::filesystem::absolute(argv[1]),
               root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(read(fixture_path));
    require(fingerprint(root) == fixture.at("SourceFingerprint"),
            "source fingerprint differed");
    std::size_t count = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>(),
                 kind = row.at("Kind").get<std::string>();
      const auto before = fingerprint(root);
      std::optional<Error> actual_error;
      Json result = nullptr;
      if (kind == "opportunity") {
        const auto value = opportunity(row.at("Input"));
        actual_error = invoke([&] { value.validate(); });
      } else if (kind == "impact") {
        const auto &i = row.at("Input");
        RelationshipImpact value{
            number(i.at("TrustDelta")),       number(i.at("HostilityDelta")),
            number(i.at("FearDelta")),        number(i.at("RespectDelta")),
            number(i.at("CooperationDelta")), number(i.at("GrievanceSeverity")),
            i.at("Reason").get<std::string>()};
        actual_error = invoke([&] { value.validate(); });
      } else if (kind == "restore") {
        const auto input = snapshot(row.at("Input"));
        std::optional<DiplomacyState> state;
        actual_error = invoke([&] { state = DiplomacyState::restore(input); });
        if (state) {
          if (row.at("Compact").get<bool>()) {
            const auto out = state->snapshot();
            result = {
                {"SnapshotContacts", out.contacts.size()},
                {"SnapshotProposals", out.proposals.size()},
                {"SnapshotHistory", out.recent_history.size()},
                {"FirstContact", out.contacts.empty()
                                     ? Json(nullptr)
                                     : Json(out.contacts.front().contact_id)},
                {"LastContact", out.contacts.empty()
                                    ? Json(nullptr)
                                    : Json(out.contacts.back().contact_id)}};
          } else
            result = projection(*state);
        }
      } else if (kind == "primitives") {
        const auto &i = row.at("Input");
        auto state = DiplomacyState{};
        const auto first = opportunity(i.at("First"));
        const auto second = opportunity(i.at("Second"));
        const auto grantor = i.at("Grantor").get<int>();
        const auto visitor = i.at("Visitor").get<int>();
        const auto permission = en<AccessPermission>(i, "Permission");
        const auto tick = i.at("Tick").get<std::int64_t>();
        const auto claimant = i.at("Claimant").get<int>();
        const auto system_id = i.at("SystemId").get<int>();
        const auto responder = i.at("Responder").get<int>();
        const auto claim_response =
            en<TerritorialClaimResponse>(i, "ClaimResponse");
        const auto proposal_kind =
            en<DiplomaticProposalKind>(i, "ProposalKind");
        const auto agreement_type =
            en<DiplomaticAgreementType>(i, "AgreementType");
        const auto summary = i.at("Summary").get<std::string>();
        const auto event_summary = i.at("EventSummary").get<std::string>();
        const auto audience = i.at("Audience").get<std::vector<int>>();
        actual_error = invoke([&] {
          (void)detail::DiplomacyStateAccess::upsert_contact(state, first);
          (void)detail::DiplomacyStateAccess::upsert_contact(state, second);
          (void)detail::DiplomacyStateAccess::relationship(state, 1, 2);
          detail::DiplomacyStateAccess::set_access(state, grantor, visitor,
                                                   permission, tick);
          auto &created = detail::DiplomacyStateAccess::create_claim(
              state, claimant, system_id, tick);
          detail::DiplomacyStateAccess::respond_to_claim(
              state, created.id, responder, claim_response, tick);
          (void)detail::DiplomacyStateAccess::create_proposal(
              state, 1, 2, proposal_kind, agreement_type, tick, summary,
              std::nullopt);
          (void)detail::DiplomacyStateAccess::activate_agreement(
              state, 1, 2, agreement_type, tick, std::nullopt);
          detail::DiplomacyStateAccess::record(
              state, tick, DiplomaticEventKind::contact_observed, 1, 2,
              system_id, event_summary, audience);
        });
        if (!actual_error)
          result = projection(state);
      } else if (kind == "primitive-boundary") {
        const auto &input = row.at("Input");
        auto state = DiplomacyState::restore(snapshot(input.at("Snapshot")));
        const auto before_state = jsnapshot(state.snapshot());
        const auto operation = row.at("Operation").get<std::string>();
        const auto &arguments = input.at("Arguments");
        const auto watched_contact =
            operation == "upsert-contact"
                ? arguments.at("Opportunity").at("ContactId").get<std::string>()
                : std::string("none");
        std::optional<FirstContactOpportunity> contact_input;
        int proposer = 0;
        int recipient = 0;
        DiplomaticProposalKind proposal_kind{};
        std::optional<DiplomaticAgreementType> agreement_type;
        std::int64_t tick = 0;
        std::string summary;
        std::optional<std::string> external_terms;
        if (operation == "upsert-contact") {
          contact_input = opportunity(arguments.at("Opportunity"));
        } else if (operation == "create-proposal") {
          proposer = arguments.at("Proposer").get<int>();
          recipient = arguments.at("Recipient").get<int>();
          proposal_kind = en<DiplomaticProposalKind>(arguments, "Kind");
          if (!arguments.at("AgreementType").is_null())
            agreement_type =
                en<DiplomaticAgreementType>(arguments, "AgreementType");
          tick = arguments.at("Tick").get<std::int64_t>();
          summary = arguments.at("Summary").get<std::string>();
          external_terms = opt<std::string>(arguments, "ExternalTerms");
        } else {
          throw std::runtime_error("bad primitive operation");
        }
        actual_error = invoke([&] {
          if (contact_input) {
            (void)detail::DiplomacyStateAccess::upsert_contact(state,
                                                               *contact_input);
          } else {
            (void)detail::DiplomacyStateAccess::create_proposal(
                state, proposer, recipient, proposal_kind, agreement_type, tick,
                summary, external_terms);
          }
        });
        result = {{"State", primitive_projection(state, watched_contact)},
                  {"Unchanged", before_state == jsnapshot(state.snapshot())}};
      } else if (kind == "query") {
        auto state = DiplomacyState::restore(snapshot(row.at("Input")));
        const auto operation = row.at("Operation").get<std::string>();
        const bool relationship_query = operation == "relationship-self";
        if (!relationship_query && operation != "missing-granted-transit")
          throw std::runtime_error("bad query operation");
        std::optional<DiplomaticRelationshipSnapshot> relationship_result;
        bool transit_result = false;
        actual_error = invoke([&] {
          if (relationship_query)
            relationship_result = state.get_relationship(1, 1);
          else
            transit_result = state.is_transit_authorized(4, 5);
        });
        if (!actual_error)
          result =
              relationship_query
                  ? (relationship_result ? jrelationship(*relationship_result)
                                         : Json(nullptr))
                  : Json(transit_result);
      } else
        throw std::runtime_error("bad kind");
      require(before == row.at("BeforeFingerprint").get<std::string>() &&
                  fingerprint(root) ==
                      row.at("AfterFingerprint").get<std::string>(),
              name + " fingerprint differed");
      error(row.at("Error"), actual_error, name);
      if (kind == "restore" || kind == "query" || kind == "primitives" ||
          kind == "primitive-boundary")
        require(result == row.at("Result"), name + " result differed");
      ++count;
    }
    require(count == fixture.at("RowCount").get<std::size_t>(),
            "row count differed");
    static_assert(!std::is_copy_constructible_v<DiplomacyState>);
    static_assert(std::is_nothrow_move_constructible_v<DiplomacyState>);
    static_assert(std::is_nothrow_move_assignable_v<DiplomacyState>);

    DiplomacyState moved;
    const FirstContactOpportunity populated_contact{
        1,
        "move-contact",
        2,
        10,
        7,
        ContactAwareness::identified,
        ContactCondition::active,
        false,
        0.75};
    (void)detail::DiplomacyStateAccess::upsert_contact(moved,
                                                       populated_contact);
    auto &held_relationship =
        detail::DiplomacyStateAccess::relationship(moved, 1, 2);
    held_relationship.hostility = 0.4;
    auto &created_claim =
        detail::DiplomacyStateAccess::create_claim(moved, 1, 7, 10);
    require(created_claim.id == 1, "move claim id differed");
    (void)detail::DiplomacyStateAccess::create_proposal(
        moved, 1, 2, DiplomaticProposalKind::trade_offer,
        DiplomaticAgreementType::trade, 10, "move proposal", std::nullopt);
    detail::DiplomacyStateAccess::record(moved, 10,
                                         DiplomaticEventKind::contact_observed,
                                         1, 2, 7, "move history", {1, 2});
    const auto *held_address = &held_relationship;
    for (int i = 0; i != 64; ++i) {
      (void)detail::DiplomacyStateAccess::relationship(moved, 100 + i,
                                                       1000 + i);
      const FirstContactOpportunity growth{
          9,
          "growth-" + std::to_string(i),
          std::nullopt,
          20 + i,
          std::nullopt,
          ContactAwareness::detected_unidentified,
          ContactCondition::active,
          false,
          0.5};
      (void)detail::DiplomacyStateAccess::upsert_contact(moved, growth);
    }
    require(&detail::DiplomacyStateAccess::relationship(moved, 1, 2) ==
                held_address,
            "growth invalidated relationship reference");

    const auto detached_snapshot = moved.snapshot();
    const auto detached_view = moved.build_view_for(1);
    const auto expected_snapshot = jsnapshot(detached_snapshot);
    const auto expected_view = jview(detached_view);
    DiplomacyState moved_again(std::move(moved));
    require(&detail::DiplomacyStateAccess::relationship(moved_again, 1, 2) ==
                held_address,
            "move construction invalidated relationship reference");
    DiplomacyState assigned;
    assigned = std::move(moved_again);
    require(&detail::DiplomacyStateAccess::relationship(assigned, 1, 2) ==
                held_address,
            "move assignment invalidated relationship reference");
    require(jsnapshot(assigned.snapshot()) == expected_snapshot,
            "move changed populated state");
    require(jview(assigned.build_view_for(1)) == expected_view,
            "move changed populated view");

    held_relationship.hostility = 0.9;
    detail::DiplomacyStateAccess::record(
        assigned, 11, DiplomaticEventKind::relationship_changed, 1, 2,
        std::nullopt, "after detached capture", {1});
    require(jsnapshot(detached_snapshot) == expected_snapshot &&
                jview(detached_view) == expected_view,
            "owned snapshot or view changed with live state");
    require(jsnapshot(assigned.snapshot()) != expected_snapshot,
            "live mutation did not differ from detached snapshot");
    std::cout << "Diplomacy state parity passed " << count
              << " actual-source rows.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "type=" << typeid(e).name() << '\n'
              << "message=" << e.what() << '\n'
              << "cwd=" << std::filesystem::current_path() << '\n'
              << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n'
              << "repoRoot=" << (argc > 2 ? argv[2] : "<missing>") << '\n';
    return 1;
  }
}
