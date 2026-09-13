#include <stellar/core/diplomacy_simulation.hpp>

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

namespace {
Json simulation_projection(const DiplomacyState &state) {
  return {{"Snapshot", jsnapshot(state.snapshot())},
          {"View1", jview(state.build_view_for(1))},
          {"View2", jview(state.build_view_for(2))},
          {"View3", jview(state.build_view_for(3))}};
}
enum class Command {
  contact,
  lost,
  access,
  claim,
  communicate_claim,
  respond_claim,
  warning,
  trespass,
  send,
  respond_proposal,
  withdraw,
  expire,
  impact,
  hostile,
  war,
};
Command command(std::string_view value) {
  if (value == "contact")
    return Command::contact;
  if (value == "lost")
    return Command::lost;
  if (value == "access")
    return Command::access;
  if (value == "claim")
    return Command::claim;
  if (value == "communicate-claim")
    return Command::communicate_claim;
  if (value == "respond-claim")
    return Command::respond_claim;
  if (value == "warning")
    return Command::warning;
  if (value == "trespass")
    return Command::trespass;
  if (value == "send")
    return Command::send;
  if (value == "respond-proposal")
    return Command::respond_proposal;
  if (value == "withdraw")
    return Command::withdraw;
  if (value == "expire")
    return Command::expire;
  if (value == "impact")
    return Command::impact;
  if (value == "hostile")
    return Command::hostile;
  if (value == "war")
    return Command::war;
  throw std::runtime_error("bad operation");
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture and repository root.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(read(fixture_path));
    require(fingerprint(root) == fixture.at("SourceFingerprint"),
            "source fingerprint differed");

    std::size_t count = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto &input = row.at("Input");
      auto state = DiplomacyState::restore(snapshot(input.at("Snapshot")));
      DiplomacySimulation simulation(state);
      const auto operation = input.at("Operation").get<std::string>();
      const auto selected_command = command(operation);
      const auto &arguments = input.at("Arguments");

      std::optional<FirstContactOpportunity> contact_argument;
      int first = 0;
      int second = 0;
      int system_id = 0;
      int responder = 0;
      std::int64_t tick = 0;
      std::int64_t record_id = 0;
      bool accepted = false;
      AccessPermission permission{};
      TerritorialClaimResponse claim_response{};
      DiplomaticProposalKind proposal_kind{};
      std::optional<DiplomaticAgreementType> agreement_type;
      std::string text;
      std::optional<std::string> terms;
      std::optional<RelationshipImpact> relationship_impact;

      if (operation == "contact") {
        contact_argument = opportunity(arguments.at("Opportunity"));
      } else if (operation == "lost") {
        first = arguments.at("Observer").get<int>();
        text = arguments.at("ContactId").get<std::string>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "access") {
        first = arguments.at("Grantor").get<int>();
        second = arguments.at("Visitor").get<int>();
        permission = en<AccessPermission>(arguments, "Permission");
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "claim") {
        first = arguments.at("Claimant").get<int>();
        system_id = arguments.at("SystemId").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "communicate-claim") {
        record_id = arguments.at("ClaimId").get<std::int64_t>();
        second = arguments.at("Recipient").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "respond-claim") {
        record_id = arguments.at("ClaimId").get<std::int64_t>();
        responder = arguments.at("Responder").get<int>();
        claim_response = en<TerritorialClaimResponse>(arguments, "Response");
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "warning") {
        first = arguments.at("Issuer").get<int>();
        second = arguments.at("Recipient").get<int>();
        system_id = arguments.at("SystemId").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "trespass") {
        first = arguments.at("Territorial").get<int>();
        second = arguments.at("Intruder").get<int>();
        system_id = arguments.at("SystemId").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "send") {
        first = arguments.at("Proposer").get<int>();
        second = arguments.at("Recipient").get<int>();
        proposal_kind = en<DiplomaticProposalKind>(arguments, "Kind");
        tick = arguments.at("Tick").get<std::int64_t>();
        text = arguments.at("Summary").get<std::string>();
        if (!arguments.at("AgreementType").is_null())
          agreement_type =
              en<DiplomaticAgreementType>(arguments, "AgreementType");
        if (!arguments.at("Terms").is_null())
          terms = arguments.at("Terms").get<std::string>();
      } else if (operation == "respond-proposal") {
        record_id = arguments.at("ProposalId").get<std::int64_t>();
        responder = arguments.at("Responder").get<int>();
        accepted = arguments.at("Accept").get<bool>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "withdraw") {
        record_id = arguments.at("ProposalId").get<std::int64_t>();
        first = arguments.at("Proposer").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "expire") {
        record_id = arguments.at("ProposalId").get<std::int64_t>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "impact") {
        first = arguments.at("Observer").get<int>();
        second = arguments.at("Target").get<int>();
        const auto &value = arguments.at("Impact");
        relationship_impact =
            RelationshipImpact{number(value.at("TrustDelta")),
                               number(value.at("HostilityDelta")),
                               number(value.at("FearDelta")),
                               number(value.at("RespectDelta")),
                               number(value.at("CooperationDelta")),
                               number(value.at("GrievanceSeverity")),
                               value.at("Reason").get<std::string>()};
        tick = arguments.at("Tick").get<std::int64_t>();
      } else if (operation == "hostile") {
        first = arguments.at("A").get<int>();
        second = arguments.at("B").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
        text = arguments.at("Reason").get<std::string>();
      } else if (operation == "war") {
        first = arguments.at("Declarer").get<int>();
        second = arguments.at("Target").get<int>();
        tick = arguments.at("Tick").get<std::int64_t>();
      } else {
        throw std::runtime_error("bad operation");
      }

      const auto before = fingerprint(root);
      std::optional<DiplomaticContactSnapshot> returned_contact;
      std::optional<std::int64_t> returned_id;
      const auto terms_view =
          terms ? std::optional<std::string_view>(*terms) : std::nullopt;
      const auto actual_error = invoke([&] {
        switch (selected_command) {
        case Command::contact:
          returned_contact =
              simulation.process_contact_opportunity(*contact_argument);
          break;
        case Command::lost:
          simulation.mark_contact_lost(first, text, tick);
          break;
        case Command::access:
          simulation.set_access_permission(first, second, permission, tick);
          break;
        case Command::claim:
          returned_id =
              simulation.assert_territorial_claim(first, system_id, tick);
          break;
        case Command::communicate_claim:
          simulation.communicate_territorial_claim(record_id, second, tick);
          break;
        case Command::respond_claim:
          simulation.respond_to_territorial_claim(record_id, responder,
                                                  claim_response, tick);
          break;
        case Command::warning:
          simulation.issue_border_warning(first, second, system_id, tick);
          break;
        case Command::trespass:
          simulation.record_trespass(first, second, system_id, tick);
          break;
        case Command::send:
          returned_id =
              simulation.send_proposal(first, second, proposal_kind, tick, text,
                                       agreement_type, terms_view);
          break;
        case Command::respond_proposal:
          simulation.respond_to_proposal(record_id, responder, accepted, tick);
          break;
        case Command::withdraw:
          simulation.withdraw_proposal(record_id, first, tick);
          break;
        case Command::expire:
          simulation.expire_proposal(record_id, tick);
          break;
        case Command::impact:
          simulation.apply_relationship_impact(first, second,
                                               *relationship_impact, tick);
          break;
        case Command::hostile:
          simulation.set_hostile(first, second, tick, text);
          break;
        case Command::war:
          simulation.declare_war(first, second, tick);
          break;
        }
      });

      Json actual_return = nullptr;
      if (!actual_error) {
        if (returned_contact)
          actual_return = jcontact(*returned_contact);
        else if (returned_id)
          actual_return = *returned_id;
      }
      const auto actual_state = simulation_projection(state);
      require(before == row.at("BeforeFingerprint").get<std::string>() &&
                  fingerprint(root) ==
                      row.at("AfterFingerprint").get<std::string>(),
              name + " fingerprint differed");
      error(row.at("Error"), actual_error, name);
      require(actual_return == row.at("Return"), name + " return differed");
      require(actual_state == row.at("State"), name + " state differed");
      ++count;
    }

    require(count == fixture.at("RowCount").get<std::size_t>(),
            "row count differed");
    static_assert(
        !std::is_constructible_v<DiplomacySimulation, DiplomacyState &&>);
    DiplomacyState alias_state = DiplomacyState::restore(
        snapshot(fixture.at("Rows").at(4).at("Input").at("Snapshot")));
    DiplomacySimulation first(alias_state);
    const auto *stored_contact = detail::DiplomacyStateAccess::mutable_contact(
        alias_state, 1, "civilization:2");
    require(stored_contact != nullptr, "alias contact missing");
    const auto borrowed_id = std::string_view(stored_contact->contact_id);
    DiplomacySimulation moved(std::move(first));
    moved.mark_contact_lost(1, borrowed_id, 12);
    DiplomacySimulation assigned(alias_state);
    assigned = std::move(moved);
    assigned.set_hostile(1, 2, 13, "move reason");
    std::cout << "Diplomacy simulation parity passed " << count
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
