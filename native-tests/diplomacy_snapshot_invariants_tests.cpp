#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/diplomacy_snapshot_invariants.hpp>

#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/diplomacy_state_access.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
      read(root / "src/Game/Simulation/Diplomacy/DiplomacySnapshotInvariantValidator.cs");
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
Json jnumber(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
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
          {"Confidence", jnumber(v.confidence)}};
}
Json jgrievance(const DiplomaticGrievanceSnapshot &v) {
  return {{"CreatedAtTick", v.created_at_tick},
          {"SourceCivilizationId", v.source_civilization_id},
          {"Severity", jnumber(v.severity)},
          {"Reason", v.reason}};
}
Json jrelationship(const DiplomaticRelationshipSnapshot &v) {
  Json g = Json::array();
  for (const auto &x : v.grievances)
    g.push_back(jgrievance(x));
  return {{"CivilizationAId", v.civilization_a_id},
          {"CivilizationBId", v.civilization_b_id},
          {"PoliticalState", static_cast<int>(v.political_state)},
          {"Trust", jnumber(v.trust)},
          {"Hostility", jnumber(v.hostility)},
          {"Fear", jnumber(v.fear)},
          {"Respect", jnumber(v.respect)},
          {"Cooperation", jnumber(v.cooperation)},
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

struct PreparedSnapshot {
  DiplomacyStateSnapshot owned;
  bool contacts_present{};
  bool relationships_present{};
  bool access_present{};
  bool claims_present{};
  bool responses_present{};
  bool agreements_present{};
  bool proposals_present{};
  bool history_present{};
  std::vector<bool> grievance_presence;
  std::vector<bool> claim_audience_presence;
  std::vector<bool> history_audience_presence;
  std::vector<DiplomaticRelationshipValidationView> relationship_views;
  std::vector<TerritorialClaimValidationView> claim_views;
  std::vector<DiplomaticHistoryEventValidationView> history_views;

  DiplomacySnapshotValidationView view() {
    relationship_views.clear();
    for (std::size_t index = 0; index < owned.relationships.size(); ++index) {
      const auto &value = owned.relationships[index];
      std::optional<std::span<const DiplomaticGrievanceSnapshot>> grievances;
      if (grievance_presence[index])
        grievances = std::span<const DiplomaticGrievanceSnapshot>(value.grievances);
      relationship_views.push_back({value.civilization_a_id, value.civilization_b_id,
                                    value.political_state, value.trust, value.hostility,
                                    value.fear, value.respect, value.cooperation, grievances});
    }
    claim_views.clear();
    for (std::size_t index = 0; index < owned.claims.size(); ++index) {
      const auto &value = owned.claims[index];
      std::optional<std::span<const int>> audience;
      if (claim_audience_presence[index])
        audience = std::span<const int>(value.known_to_civilization_ids);
      claim_views.push_back({value.claim_id, value.claimant_civilization_id, value.system_id,
                             value.asserted_at_tick, value.active, audience});
    }
    history_views.clear();
    for (std::size_t index = 0; index < owned.recent_history.size(); ++index) {
      const auto &value = owned.recent_history[index];
      std::optional<std::span<const int>> audience;
      if (history_audience_presence[index])
        audience = std::span<const int>(value.known_to_civilization_ids);
      history_views.push_back({value.event_id, value.tick, value.kind,
                               value.primary_civilization_id,
                               value.secondary_civilization_id, value.system_id,
                               value.summary, audience});
    }
    return {
        contacts_present
            ? std::optional(std::span<const DiplomaticContactSnapshot>(owned.contacts))
            : std::nullopt,
        relationships_present
            ? std::optional(std::span<const DiplomaticRelationshipValidationView>(relationship_views))
            : std::nullopt,
        access_present
            ? std::optional(std::span<const DiplomaticAccessSnapshot>(owned.access_permissions))
            : std::nullopt,
        claims_present
            ? std::optional(std::span<const TerritorialClaimValidationView>(claim_views))
            : std::nullopt,
        responses_present
            ? std::optional(std::span<const TerritorialClaimResponseSnapshot>(owned.claim_responses))
            : std::nullopt,
        agreements_present
            ? std::optional(std::span<const DiplomaticAgreementSnapshot>(owned.agreements))
            : std::nullopt,
        proposals_present
            ? std::optional(std::span<const DiplomaticProposalSnapshot>(owned.proposals))
            : std::nullopt,
        history_present
            ? std::optional(std::span<const DiplomaticHistoryEventValidationView>(history_views))
            : std::nullopt,
        owned.next_claim_id,
        owned.next_agreement_id,
        owned.next_proposal_id,
        owned.next_event_id,
    };
  }
};

PreparedSnapshot prepared_snapshot(const Json &input) {
  PreparedSnapshot result;
  result.owned = snapshot(input);
  result.contacts_present = !input.at("Contacts").is_null();
  result.relationships_present = !input.at("Relationships").is_null();
  result.access_present = !input.at("AccessPermissions").is_null();
  result.claims_present = !input.at("Claims").is_null();
  result.responses_present = !input.at("ClaimResponses").is_null();
  result.agreements_present = !input.at("Agreements").is_null();
  result.proposals_present = !input.at("Proposals").is_null();
  result.history_present = !input.at("RecentHistory").is_null();
  if (result.relationships_present)
    for (const auto &value : input.at("Relationships"))
      result.grievance_presence.push_back(!value.at("Grievances").is_null());
  if (result.claims_present)
    for (const auto &value : input.at("Claims"))
      result.claim_audience_presence.push_back(!value.at("KnownToCivilizationIds").is_null());
  if (result.history_present)
    for (const auto &value : input.at("RecentHistory"))
      result.history_audience_presence.push_back(!value.at("KnownToCivilizationIds").is_null());
  return result;
}

Json project(const PreparedSnapshot &value) {
  Json result = jsnapshot(value.owned);
  if (!value.contacts_present)
    result["Contacts"] = nullptr;
  if (!value.relationships_present)
    result["Relationships"] = nullptr;
  else
    for (std::size_t index = 0; index < value.grievance_presence.size(); ++index)
      if (!value.grievance_presence[index])
        result["Relationships"][index]["Grievances"] = nullptr;
  if (!value.access_present)
    result["AccessPermissions"] = nullptr;
  if (!value.claims_present)
    result["Claims"] = nullptr;
  else
    for (std::size_t index = 0; index < value.claim_audience_presence.size(); ++index)
      if (!value.claim_audience_presence[index])
        result["Claims"][index]["KnownToCivilizationIds"] = nullptr;
  if (!value.responses_present)
    result["ClaimResponses"] = nullptr;
  if (!value.agreements_present)
    result["Agreements"] = nullptr;
  if (!value.proposals_present)
    result["Proposals"] = nullptr;
  if (!value.history_present)
    result["RecentHistory"] = nullptr;
  else
    for (std::size_t index = 0; index < value.history_audience_presence.size(); ++index)
      if (!value.history_audience_presence[index])
        result["RecentHistory"][index]["KnownToCivilizationIds"] = nullptr;
  return result;
}

Json validation_result(const DiplomacySnapshotValidationResult &value) {
  return {{"ContactCount", value.contact_count},
          {"RelationshipCount", value.relationship_count},
          {"AccessPermissionCount", value.access_permission_count},
          {"ClaimCount", value.claim_count},
          {"ClaimResponseCount", value.claim_response_count},
          {"AgreementCount", value.agreement_count},
          {"ProposalCount", value.proposal_count},
          {"HistoryEventCount", value.history_event_count}};
}

Json validation_error(std::exception_ptr error) {
  if (!error)
    return nullptr;
  try {
    std::rethrow_exception(error);
  } catch (const DiplomacySnapshotValidationError &exception) {
    return {{"Type", "DiplomacySnapshotValidationException"}, {"Message", exception.what()}};
  } catch (const std::exception &exception) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", exception.what()}};
  }
}

int replay(const std::filesystem::path &source_root,
           const std::filesystem::path &fixture_path) {
  const auto before_fingerprint = fingerprint(source_root);
  const auto fixture_bytes = read(fixture_path);
  const auto fixture_fingerprint = hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(fixture_bytes.data()), fixture_bytes.size()}));
  if (fixture_fingerprint !=
      "E48E5645F039A0498A4BE9A58547ECCBA3CCEC1E044378122BB6E60E914D2EF6")
    throw std::runtime_error("Retained source fixture fingerprint changed.");
  const Json fixture = Json::parse(fixture_bytes);
  if (fixture.at("AuthorityFingerprint").get<std::string>() != before_fingerprint)
    throw std::runtime_error("Source authority fingerprint does not match fixture.");
  if (fixture.at("Rows").size() != 128)
    throw std::runtime_error("Retained source fixture must contain exactly 128 rows.");
  std::size_t count{};
  std::size_t source_only_count{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    if (row.at("InputBefore") != row.at("InputAfter"))
      throw std::runtime_error(name + ": managed validator mutated its input");
    if (!row.at("SourceOnlyReason").is_null()) {
      if (name != "null-snapshot" || !row.at("Input").is_null() || row.at("Error").is_null())
        throw std::runtime_error(name + ": invalid source-only metadata");
      ++source_only_count;
      continue;
    }
    PreparedSnapshot input = prepared_snapshot(row.at("Input"));
    const auto native_before = project(input);
    if (native_before != row.at("Input")) {
      std::cerr << name << ": typed input projection mismatch\n";
      return 1;
    }
    std::optional<DiplomacySnapshotValidationResult> result;
    std::exception_ptr error;
    auto view = input.view();
    try {
      result = DiplomacySnapshotInvariantValidator::validate(view);
    } catch (...) {
      error = std::current_exception();
    }
    const Json actual_result = result ? validation_result(*result) : Json(nullptr);
    if (validation_error(error) != row.at("Error") || actual_result != row.at("Result")) {
      std::cerr << name << ": result mismatch\n"
                << validation_error(error).dump(2) << "\n"
                << row.at("Error").dump(2) << "\n";
      return 1;
    }
    if (project(input) != native_before) {
      std::cerr << name << ": native validator mutated its input\n";
      return 1;
    }
    ++count;
  }
  if (count + source_only_count != fixture.at("Rows").size())
    throw std::runtime_error("Native replay did not account for every source row.");
  if (count != 127 || source_only_count != 1)
    throw std::runtime_error("Native/source-only row accounting changed.");
  {
    const auto complete = std::ranges::find_if(
        fixture.at("Rows"), [](const Json &row) {
          return row.at("Name") == "valid-complete";
        });
    if (complete == fixture.at("Rows").end())
      throw std::runtime_error("Retained complete valid source row is missing.");
    PreparedSnapshot input = prepared_snapshot(complete->at("Input"));
    const auto native_before = project(input);
    const auto result =
        DiplomacySnapshotInvariantValidator::validate(input.owned);
    if (validation_result(result) != complete->at("Result") ||
        project(input) != native_before) {
      throw std::runtime_error(
          "Native-owned complete snapshot overload mismatch.");
    }
  }
  {
    DiplomacyStateSnapshot empty;
    empty.next_claim_id = 1;
    empty.next_agreement_id = 1;
    empty.next_proposal_id = 1;
    empty.next_event_id = 1;
    const auto result = DiplomacySnapshotInvariantValidator::validate(empty);
    if (result.contact_count != 0 || result.relationship_count != 0 ||
        result.access_permission_count != 0 || result.claim_count != 0 ||
        result.claim_response_count != 0 || result.agreement_count != 0 ||
        result.proposal_count != 0 || result.history_event_count != 0)
      throw std::runtime_error("Native-owned empty snapshot overload mismatch.");
  }
  if (fingerprint(source_root) != before_fingerprint)
    throw std::runtime_error("Native replay changed the source authority input.");
  std::cout << "diplomacy_snapshot_invariants_tests: " << count
            << " actual-source rows passed; " << source_only_count
            << " null-source boundary recorded\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected source root and fixture path.");
    return replay(std::filesystem::absolute(argv[1]),
                  std::filesystem::absolute(argv[2]));
  } catch (const std::exception &exception) {
    std::cerr << "ExceptionType: " << typeid(exception).name() << "\nMessage: "
              << exception.what() << "\nCurrentDirectory: "
              << std::filesystem::current_path().string() << "\nSourceRoot: "
              << (argc > 1 ? argv[1] : "<missing>") << "\nFixturePath: "
              << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ExceptionType: unknown\nMessage: non-standard exception\nCurrentDirectory: "
              << std::filesystem::current_path().string() << "\nSourceRoot: "
              << (argc > 1 ? argv[1] : "<missing>") << "\nFixturePath: "
              << (argc > 2 ? argv[2] : "<missing>") << "\n";
    return 1;
  }
}
