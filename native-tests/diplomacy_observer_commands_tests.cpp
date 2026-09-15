#include <stellar/core/diplomacy_observer_commands.hpp>

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/knowledge.hpp>

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
  constexpr std::string_view names[] = {
      "ObserverDiplomacyCommandService.cs",
      "ObserverDiplomacyClaimCommands.cs",
      "ObserverDiplomacyActionAvailability.cs",
      "CampaignDiplomacyTerritorialClaimCommandService.cs",
      "CampaignDiplomacyBorderWarningCommandService.cs",
  };
  std::string bytes;
  for (const auto name : names) {
    bytes.append(name);
    bytes.push_back('\0');
    bytes.append(read(root / "src/Game/Simulation/Diplomacy" /
                      std::string(name)));
    bytes.push_back('\0');
  }
  return hex(detail::adaptive_research_sha256({
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()}));
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

Json state_projection(const DiplomacyState &state) {
  return {{"Snapshot", jsnapshot(state.snapshot())},
          {"View1", jview(state.build_view_for(1))},
          {"View2", jview(state.build_view_for(2))},
          {"View3", jview(state.build_view_for(3))},
          {"View4", jview(state.build_view_for(4))}};
}

Json command_result(const ObserverDiplomacyCommandResult &value) {
  return {{"Accepted", value.accepted},
          {"Status", static_cast<int>(value.status)},
          {"Message", value.message},
          {"ProposalId", value.proposal_id ? Json(*value.proposal_id)
                                             : Json(nullptr)},
          {"AgreementId", value.agreement_id ? Json(*value.agreement_id)
                                               : Json(nullptr)}};
}

Json claim_result(const CampaignTerritorialClaimCommandResult &value) {
  return {{"Accepted", value.accepted},
          {"Status", static_cast<int>(value.status)},
          {"Message", value.message},
          {"ClaimId", value.claim_id ? Json(*value.claim_id) : Json(nullptr)}};
}

Json availability(const ObserverDiplomacyActionAvailability &value) {
  return {{"CounterpartCivilizationId", value.counterpart_civilization_id},
          {"ContactAwareness", static_cast<int>(value.contact_awareness)},
          {"ContactCondition", static_cast<int>(value.contact_condition)},
          {"ContactConfidence", value.contact_confidence},
          {"LastObservedTick", value.last_observed_tick},
          {"PoliticalState", static_cast<int>(value.political_state)},
          {"CommunicationAvailable", value.communication_available},
          {"CanAttemptCommunication", value.can_attempt_communication},
          {"CanSendProposal", value.can_send_proposal},
          {"CanSetAccessPermission", value.can_set_access_permission},
          {"CanDeclareWar", value.can_declare_war},
          {"CanRespondToPendingProposal",
           value.can_respond_to_pending_proposal},
          {"CanWithdrawPendingProposal",
           value.can_withdraw_pending_proposal},
          {"CanTerminateActiveAgreement",
           value.can_terminate_active_agreement},
          {"PendingIncomingProposalCount",
           value.pending_incoming_proposal_count},
          {"PendingOutgoingProposalCount",
           value.pending_outgoing_proposal_count},
          {"ActiveAgreementCount", value.active_agreement_count}};
}

Json availability(const std::vector<ObserverDiplomacyActionAvailability> &values) {
  return array(values, [](const auto &value) { return availability(value); });
}

struct WorldStorage {
  std::vector<Civilization> civilizations;
  std::vector<StellarSystem> systems;
  std::vector<Colony> colonies;
  CivilizationKnowledgeState knowledge;

  [[nodiscard]] DiplomacyCampaignCommandWorldView view() const {
    return {civilizations, systems, colonies, knowledge};
  }
};

WorldStorage world(const Json &value) {
  WorldStorage result;
  for (const auto &id : value.at("CivilizationIds")) {
    Civilization civilization;
    civilization.id = id.get<int>();
    result.civilizations.push_back(std::move(civilization));
  }
  for (const auto &id : value.at("SystemIds")) {
    StellarSystem system;
    system.id = id.get<int>();
    result.systems.push_back(std::move(system));
  }
  for (const auto &entry : value.at("Colonies")) {
    Colony colony;
    colony.civilization_id = entry.at("CivilizationId").get<int>();
    colony.system_id = entry.at("SystemId").get<int>();
    result.colonies.push_back(std::move(colony));
  }
  for (const auto &entry : value.at("KnownSystems")) {
    const int civilization = entry.at("CivilizationId").get<int>();
    for (const auto &id : entry.at("SystemIds"))
      result.knowledge.reveal_system(civilization, id.get<int>());
  }
  return result;
}

Json world_projection(const WorldStorage &value) {
  Json civilization_ids = Json::array();
  for (const auto &civilization : value.civilizations)
    civilization_ids.push_back(civilization.id);
  Json system_ids = Json::array();
  for (const auto &system : value.systems) system_ids.push_back(system.id);
  Json colonies = Json::array();
  for (const auto &colony : value.colonies)
    colonies.push_back({{"CivilizationId", colony.civilization_id},
                        {"SystemId", colony.system_id}});
  Json known = Json::array();
  for (const auto &civilization : value.civilizations)
    known.push_back({{"CivilizationId", civilization.id},
                     {"SystemIds",
                      value.knowledge.known_systems(civilization.id)}});
  return {{"CivilizationIds", civilization_ids},
          {"SystemIds", system_ids},
          {"Colonies", colonies},
          {"KnownSystems", known}};
}

enum class Operation {
  build_view,
  establish_communication,
  send_proposal,
  respond_to_proposal,
  withdraw_proposal,
  set_access,
  declare_war,
  terminate_agreement,
  communicate_claim,
  respond_claim,
  availability,
  assert_campaign_claim,
  issue_border_warning,
};

Operation operation(std::string_view kind, const Json &input) {
  if (kind == "Availability") return Operation::availability;
  const auto value = input.at("Operation").get<std::string>();
  if (value == "BuildView") return Operation::build_view;
  if (value == "EstablishCommunication")
    return Operation::establish_communication;
  if (value == "SendProposal") return Operation::send_proposal;
  if (value == "RespondToProposal") return Operation::respond_to_proposal;
  if (value == "WithdrawProposal") return Operation::withdraw_proposal;
  if (value == "SetAccessPermission") return Operation::set_access;
  if (value == "DeclareWar") return Operation::declare_war;
  if (value == "TerminateAgreement") return Operation::terminate_agreement;
  if (value == "CommunicateTerritorialClaim")
    return Operation::communicate_claim;
  if (value == "RespondToTerritorialClaim") return Operation::respond_claim;
  if (value == "AssertTerritorialClaim")
    return Operation::assert_campaign_claim;
  if (value == "IssueBorderWarning") return Operation::issue_border_warning;
  throw std::runtime_error("Unknown fixture operation.");
}

struct Arguments {
  int observer{};
  int target{};
  int recipient{};
  int system_id{};
  std::int64_t tick{};
  std::int64_t record_id{};
  bool accept{};
  DiplomaticProposalKind proposal_kind{};
  std::optional<DiplomaticAgreementType> agreement_type;
  AccessPermission permission{};
  TerritorialClaimResponse claim_response{};
  std::string text;
  std::optional<std::string> external_terms;
};

Arguments arguments(Operation selected, const Json &input) {
  Arguments value;
  if (selected == Operation::availability) {
    value.observer = input.at("Observer").get<int>();
    return value;
  }
  const auto &args = input.at("Arguments");
  switch (selected) {
  case Operation::build_view:
    value.observer = args.at("Observer").get<int>();
    break;
  case Operation::establish_communication:
    value.observer = args.at("Observer").get<int>();
    value.target = args.at("Target").get<int>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::send_proposal:
    value.observer = args.at("Observer").get<int>();
    value.target = args.at("Target").get<int>();
    value.proposal_kind = en<DiplomaticProposalKind>(args, "Kind");
    value.tick = args.at("Tick").get<std::int64_t>();
    value.text = args.at("Summary").get<std::string>();
    if (!args.at("AgreementType").is_null())
      value.agreement_type =
          en<DiplomaticAgreementType>(args, "AgreementType");
    if (!args.at("ExternalTermsReference").is_null())
      value.external_terms =
          args.at("ExternalTermsReference").get<std::string>();
    break;
  case Operation::respond_to_proposal:
    value.observer = args.at("Observer").get<int>();
    value.record_id = args.at("ProposalId").get<std::int64_t>();
    value.accept = args.at("Accept").get<bool>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::withdraw_proposal:
    value.observer = args.at("Observer").get<int>();
    value.record_id = args.at("ProposalId").get<std::int64_t>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::set_access:
    value.observer = args.at("Observer").get<int>();
    value.target = args.at("Target").get<int>();
    value.permission = en<AccessPermission>(args, "Permission");
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::declare_war:
    value.observer = args.at("Observer").get<int>();
    value.target = args.at("Target").get<int>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::terminate_agreement:
    value.observer = args.at("Observer").get<int>();
    value.record_id = args.at("AgreementId").get<std::int64_t>();
    value.tick = args.at("Tick").get<std::int64_t>();
    value.text = args.at("Reason").get<std::string>();
    break;
  case Operation::communicate_claim:
    value.observer = args.at("Observer").get<int>();
    value.record_id = args.at("ClaimId").get<std::int64_t>();
    value.recipient = args.at("Recipient").get<int>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::respond_claim:
    value.observer = args.at("Observer").get<int>();
    value.record_id = args.at("ClaimId").get<std::int64_t>();
    value.claim_response = en<TerritorialClaimResponse>(args, "Response");
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::assert_campaign_claim:
    value.observer = args.at("Observer").get<int>();
    value.system_id = args.at("SystemId").get<int>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::issue_border_warning:
    value.observer = args.at("Issuer").get<int>();
    value.recipient = args.at("Recipient").get<int>();
    value.system_id = args.at("SystemId").get<int>();
    value.tick = args.at("Tick").get<std::int64_t>();
    break;
  case Operation::availability:
    break;
  }
  return value;
}

Json arguments_projection(Operation selected, const Arguments &value) {
  switch (selected) {
  case Operation::build_view:
    return {{"Observer", value.observer}};
  case Operation::establish_communication:
    return {{"Observer", value.observer},
            {"Target", value.target},
            {"Tick", value.tick}};
  case Operation::send_proposal:
    return {{"Observer", value.observer},
            {"Target", value.target},
            {"Kind", static_cast<int>(value.proposal_kind)},
            {"Tick", value.tick},
            {"Summary", value.text},
            {"AgreementType",
             value.agreement_type
                 ? Json(static_cast<int>(*value.agreement_type))
                 : Json(nullptr)},
            {"ExternalTermsReference",
             value.external_terms ? Json(*value.external_terms) : Json(nullptr)}};
  case Operation::respond_to_proposal:
    return {{"Observer", value.observer},
            {"ProposalId", value.record_id},
            {"Accept", value.accept},
            {"Tick", value.tick}};
  case Operation::withdraw_proposal:
    return {{"Observer", value.observer},
            {"ProposalId", value.record_id},
            {"Tick", value.tick}};
  case Operation::set_access:
    return {{"Observer", value.observer},
            {"Target", value.target},
            {"Permission", static_cast<int>(value.permission)},
            {"Tick", value.tick}};
  case Operation::declare_war:
    return {{"Observer", value.observer},
            {"Target", value.target},
            {"Tick", value.tick}};
  case Operation::terminate_agreement:
    return {{"Observer", value.observer},
            {"AgreementId", value.record_id},
            {"Tick", value.tick},
            {"Reason", value.text}};
  case Operation::communicate_claim:
    return {{"Observer", value.observer},
            {"ClaimId", value.record_id},
            {"Recipient", value.recipient},
            {"Tick", value.tick}};
  case Operation::respond_claim:
    return {{"Observer", value.observer},
            {"ClaimId", value.record_id},
            {"Response", static_cast<int>(value.claim_response)},
            {"Tick", value.tick}};
  case Operation::assert_campaign_claim:
    return {{"Observer", value.observer},
            {"SystemId", value.system_id},
            {"Tick", value.tick}};
  case Operation::issue_border_warning:
    return {{"Issuer", value.observer},
            {"Recipient", value.recipient},
            {"SystemId", value.system_id},
            {"Tick", value.tick}};
  case Operation::availability:
    return Json(nullptr);
  }
  throw std::runtime_error("Unhandled fixture operation projection.");
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected repository root and fixture path.");
    const auto root = std::filesystem::absolute(argv[1]);
    const auto fixture_path = std::filesystem::absolute(argv[2]);
    const auto fixture_bytes = read(fixture_path);
    const auto fixture = Json::parse(fixture_bytes);
    require(fingerprint(root) == fixture.at("SourceFingerprint"),
            "Source fingerprint differed.");
    require(fixture.at("Rows").size() == 46,
            "Retained fixture row count differed.");

    std::size_t count{};
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto kind = row.at("Kind").get<std::string>();
      const auto &input = row.at("Input");
      const auto selected = operation(kind, input);
      const auto args = arguments(selected, input);
      auto initial = snapshot(input.at("Snapshot"));
      auto state = DiplomacyState::restore(initial);
      std::optional<WorldStorage> campaign_world;
      if (kind == "Campaign") campaign_world = world(input.at("World"));
      const auto native_input_before = jsnapshot(initial);
      const auto native_world_before =
          campaign_world ? world_projection(*campaign_world) : Json(nullptr);
      require(native_input_before == input.at("Snapshot"),
              name + ": typed snapshot projection differed");
      if (campaign_world)
        require(native_world_before == input.at("World"),
                name + ": typed world projection differed");
      if (selected != Operation::availability)
        require(arguments_projection(selected, args) == input.at("Arguments"),
                name + ": typed argument projection differed");

      ObserverDiplomacyCommandService service(state);
      std::optional<DiplomaticStateView> availability_view;
      if (selected == Operation::availability)
        availability_view = state.build_view_for(args.observer);
      std::optional<DiplomacyCampaignCommandWorldView> campaign_view;
      if (campaign_world) campaign_view.emplace(campaign_world->view());
      std::optional<ObserverDiplomacyCommandResult> observer_result;
      std::optional<CampaignTerritorialClaimCommandResult> campaign_result;
      std::optional<DiplomaticStateView> returned_view;
      std::optional<std::vector<ObserverDiplomacyActionAvailability>>
          availability_result;
      const auto terms_view =
          args.external_terms
              ? std::optional<std::string_view>(*args.external_terms)
              : std::nullopt;
      const auto actual_error = invoke([&] {
        switch (selected) {
        case Operation::build_view:
          returned_view = service.build_view(args.observer);
          break;
        case Operation::establish_communication:
          observer_result = service.establish_communication(
              args.observer, args.target, args.tick);
          break;
        case Operation::send_proposal:
          observer_result = service.send_proposal(
              args.observer, args.target, args.proposal_kind, args.tick,
              args.text, args.agreement_type, terms_view);
          break;
        case Operation::respond_to_proposal:
          observer_result = service.respond_to_proposal(
              args.observer, args.record_id, args.accept, args.tick);
          break;
        case Operation::withdraw_proposal:
          observer_result = service.withdraw_proposal(
              args.observer, args.record_id, args.tick);
          break;
        case Operation::set_access:
          observer_result = service.set_access_permission(
              args.observer, args.target, args.permission, args.tick);
          break;
        case Operation::declare_war:
          observer_result =
              service.declare_war(args.observer, args.target, args.tick);
          break;
        case Operation::terminate_agreement:
          observer_result = service.terminate_agreement(
              args.observer, args.record_id, args.tick, args.text);
          break;
        case Operation::communicate_claim:
          observer_result = service.communicate_territorial_claim(
              args.observer, args.record_id, args.recipient, args.tick);
          break;
        case Operation::respond_claim:
          observer_result = service.respond_to_territorial_claim(
              args.observer, args.record_id, args.claim_response, args.tick);
          break;
        case Operation::availability:
          availability_result = build_observer_diplomacy_action_availability(
              *availability_view);
          break;
        case Operation::assert_campaign_claim:
          campaign_result =
              CampaignDiplomacyTerritorialClaimCommandService(state)
                  .assert_territorial_claim(*campaign_view, args.observer,
                                            args.system_id, args.tick);
          break;
        case Operation::issue_border_warning:
          observer_result = CampaignDiplomacyBorderWarningCommandService(state)
                                .issue_border_warning(
                                    *campaign_view, args.observer,
                                    args.recipient, args.system_id, args.tick);
          break;
        }
      });

      Json actual_result = nullptr;
      if (!actual_error) {
        if (observer_result)
          actual_result = command_result(*observer_result);
        else if (campaign_result)
          actual_result = claim_result(*campaign_result);
        else if (returned_view)
          actual_result = jview(*returned_view);
        else if (availability_result)
          actual_result = availability(*availability_result);
      }
      require(row.at("InputBefore") == row.at("InputAfter"),
              name + ": source input changed");
      require(fingerprint(root) ==
                      row.at("BeforeFingerprint").get<std::string>() &&
                  fingerprint(root) ==
                      row.at("AfterFingerprint").get<std::string>(),
              name + ": source fingerprint differed");
      error(row.at("Error"), actual_error, name);
      require(actual_result == row.at("Result"), name + ": result differed");
      require(state_projection(state) == row.at("State"),
              name + ": state differed");
      require(jsnapshot(initial) == native_input_before,
              name + ": native owned input changed");
      if (campaign_world)
        require(world_projection(*campaign_world) == native_world_before,
                name + ": native world input changed");
      ++count;
    }

    require(count == 46, "Native replay row accounting differed.");
    require(read(fixture_path) == fixture_bytes,
            "Native replay changed retained fixture bytes.");
    static_assert(!std::is_constructible_v<ObserverDiplomacyCommandService,
                                           DiplomacyState &&>);
    static_assert(!std::is_constructible_v<
                  CampaignDiplomacyTerritorialClaimCommandService,
                  DiplomacyState &&>);
    static_assert(!std::is_constructible_v<
                  CampaignDiplomacyBorderWarningCommandService,
                  DiplomacyState &&>);
    std::cout << "diplomacy_observer_commands_tests: " << count
              << " actual-source rows passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "ExceptionType: " << typeid(exception).name()
              << "\nMessage: " << exception.what()
              << "\nCurrentDirectory: " << std::filesystem::current_path()
              << "\nSourceRoot: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nFixturePath: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  } catch (...) {
    std::cerr << "ExceptionType: unknown\nMessage: non-standard exception"
              << "\nCurrentDirectory: " << std::filesystem::current_path()
              << "\nSourceRoot: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nFixturePath: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
