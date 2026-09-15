#include <stellar/core/player_campaign_json.hpp>

#include <stellar/core/detail/player_campaign_persistence_restore.hpp>
#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/galaxy_payload_json.hpp>

#include "galaxy_payload_json_internal.hpp"
#include "player_campaign_json_diplomacy.hpp"
#include "player_campaign_json_research.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace stellar::core {
namespace {

using OrderedValue = json_detail::Value;
using OrderedObject = OrderedValue::Object;
using Json = nlohmann::ordered_json;

class RestoreProgressCallbackFailure final : public std::exception {
public:
  explicit RestoreProgressCallbackFailure(std::exception_ptr error)
      : error_(std::move(error)) {}
  [[nodiscard]] const std::exception_ptr &error() const noexcept { return error_; }
  [[nodiscard]] const char *what() const noexcept override {
    return "Player campaign restore progress callback failed.";
  }
private:
  std::exception_ptr error_;
};

void report_restore_stage(const PlayerCampaignJsonRestoreHooks &hooks,
                          PlayerCampaignJsonStage stage) {
  if (!hooks.on_stage)
    return;
  try { hooks.on_stage(stage); }
  catch (...) { throw RestoreProgressCallbackFailure(std::current_exception()); }
}

[[noreturn]] void fail(PlayerCampaignJsonStage stage, std::string source_type,
                       std::string message,
                       std::optional<std::string> inner_type = std::nullopt,
                       std::optional<std::string> inner_message = std::nullopt,
                       std::string path = {},
                       std::optional<std::size_t> byte = std::nullopt) {
  throw PlayerCampaignJsonError(
      stage, std::move(source_type), std::move(message), std::move(inner_type),
      std::move(inner_message), std::move(path), byte);
}

void reject_root_duplicates(const OrderedObject &object) {
  std::set<std::string, std::less<>> names;
  for (const auto &[name, value] : object) {
    if (!names.emplace(name).second) {
      fail(PlayerCampaignJsonStage::Parse, "ArgumentException",
           "An item with the same key has already been added. Key: " + name +
               " (Parameter 'propertyName')",
           std::nullopt, std::nullopt, name, value.byte);
    }
  }
}

const OrderedValue *member(const OrderedObject &object, std::string_view name) {
  for (const auto &[candidate, value] : object) {
    if (candidate == name)
      return &value;
  }
  return nullptr;
}

std::string quote(std::string_view value) {
  return Json(std::string(value)).dump();
}

void append_json(std::string &output, const OrderedValue &value);

void append_object(std::string &output, const OrderedObject &object,
                   const std::set<std::string, std::less<>> &excluded = {},
                   std::optional<int> format_override = std::nullopt) {
  output.push_back('{');
  bool first = true;
  for (const auto &[name, value] : object) {
    if (excluded.contains(name) || (format_override && name == "FormatVersion"))
      continue;
    if (!std::exchange(first, false))
      output.push_back(',');
    output += quote(name);
    output.push_back(':');
    append_json(output, value);
  }
  if (format_override) {
    if (!std::exchange(first, false))
      output.push_back(',');
    output += "\"FormatVersion\":" + std::to_string(*format_override);
  }
  output.push_back('}');
}

void append_json(std::string &output, const OrderedValue &value) {
  if (std::holds_alternative<std::nullptr_t>(value.data)) {
    output += "null";
  } else if (const auto *boolean = std::get_if<bool>(&value.data)) {
    output += *boolean ? "true" : "false";
  } else if (const auto *number =
                 std::get_if<OrderedValue::Number>(&value.data)) {
    output += number->text;
  } else if (const auto *text = std::get_if<std::string>(&value.data)) {
    output += quote(*text);
  } else if (const auto *array =
                 std::get_if<OrderedValue::Array>(&value.data)) {
    output.push_back('[');
    bool first = true;
    for (const auto &item : *array) {
      if (!std::exchange(first, false))
        output.push_back(',');
      append_json(output, item);
    }
    output.push_back(']');
  } else {
    append_object(output, std::get<OrderedObject>(value.data));
  }
}

std::string raw_json(const OrderedValue &value) {
  std::string output;
  append_json(output, value);
  return output;
}

int integer(const OrderedValue &value, std::string_view path) {
  const auto *number = std::get_if<OrderedValue::Number>(&value.data);
  if (!number)
    fail(PlayerCampaignJsonStage::Envelope, "InvalidOperationException",
         "The native ordered JSON value could not be converted to Int32.",
         std::nullopt, std::nullopt, std::string(path), value.byte);
  int result{};
  const auto converted = std::from_chars(
      number->text.data(), number->text.data() + number->text.size(), result);
  if (converted.ec != std::errc{} ||
      converted.ptr != number->text.data() + number->text.size())
    fail(PlayerCampaignJsonStage::Envelope, "InvalidOperationException",
         "A value could not be converted to System.Int32.", std::nullopt,
         std::nullopt, std::string(path), value.byte);
  return result;
}

struct OrderedTypeError final : std::runtime_error {
  OrderedTypeError(std::string message, std::string path, std::size_t byte)
      : std::runtime_error(std::move(message)), path(std::move(path)),
        byte(byte) {}
  std::string path;
  std::size_t byte;
};

[[noreturn]] void type_error(const OrderedValue &value, const std::string &path,
                             std::string_view expected) {
  throw OrderedTypeError("Native ordered JSON value at " + path +
                             " could not be converted to " +
                             std::string(expected) + ".",
                         path, value.byte);
}

const OrderedObject &typed_object(const OrderedValue &value,
                                  const std::string &path) {
  if (const auto *found = std::get_if<OrderedObject>(&value.data))
    return *found;
  type_error(value, path, "an object");
}

const OrderedValue::Array &typed_array(const OrderedValue &value,
                                       const std::string &path) {
  if (const auto *found = std::get_if<OrderedValue::Array>(&value.data))
    return *found;
  type_error(value, path, "an array");
}

std::string typed_string(const OrderedValue &value, const std::string &path) {
  if (const auto *found = std::get_if<std::string>(&value.data))
    return *found;
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return {};
  type_error(value, path, "String");
}

bool typed_bool(const OrderedValue &value, const std::string &path) {
  if (const auto *found = std::get_if<bool>(&value.data))
    return *found;
  type_error(value, path, "Boolean");
}

template <class Integer>
Integer typed_integer(const OrderedValue &value, const std::string &path) {
  const auto *number = std::get_if<OrderedValue::Number>(&value.data);
  if (!number || number->text.find_first_of(".eE") != std::string::npos)
    type_error(value, path, sizeof(Integer) == 4 ? "Int32" : "Int64");
  Integer result{};
  const auto converted = std::from_chars(
      number->text.data(), number->text.data() + number->text.size(), result);
  if (converted.ec != std::errc{} ||
      converted.ptr != number->text.data() + number->text.size())
    type_error(value, path, sizeof(Integer) == 4 ? "Int32" : "Int64");
  return result;
}

double typed_double(const OrderedValue &value, const std::string &path) {
  const auto *number = std::get_if<OrderedValue::Number>(&value.data);
  if (!number)
    type_error(value, path, "Double");
  double result{};
  const auto converted = std::from_chars(
      number->text.data(), number->text.data() + number->text.size(), result,
      std::chars_format::general);
  if (converted.ec == std::errc{} &&
      converted.ptr == number->text.data() + number->text.size())
    return result;
  if (converted.ec != std::errc::result_out_of_range)
    type_error(value, path, "Double");

  const auto sign_offset =
      !number->text.empty() && number->text.front() == '-' ? 1U : 0U;
  const auto exponent_position = number->text.find_first_of("eE");
  const auto significand_end = exponent_position == std::string::npos
                                   ? number->text.size()
                                   : exponent_position;
  const auto decimal_position = number->text.find('.', sign_offset);
  const auto integral_digits = decimal_position == std::string::npos ||
                                       decimal_position > significand_end
                                   ? significand_end - sign_offset
                                   : decimal_position - sign_offset;
  std::size_t first_nonzero{};
  bool found_nonzero{};
  for (std::size_t index = sign_offset; index < significand_end; ++index) {
    if (number->text[index] == '.')
      continue;
    if (number->text[index] != '0') {
      found_nonzero = true;
      break;
    }
    ++first_nonzero;
  }
  const bool negative = !number->text.empty() && number->text.front() == '-';
  if (!found_nonzero)
    return negative ? -0.0 : 0.0;

  constexpr std::int64_t order_limit = 1'000'000;
  std::int64_t explicit_exponent{};
  if (exponent_position != std::string::npos) {
    auto exponent_text =
        std::string_view(number->text).substr(exponent_position + 1);
    const bool exponent_negative =
        !exponent_text.empty() && exponent_text.front() == '-';
    if (!exponent_text.empty() &&
        (exponent_text.front() == '+' || exponent_text.front() == '-'))
      exponent_text.remove_prefix(1);
    for (const auto digit : exponent_text) {
      if (explicit_exponent > (order_limit - (digit - '0')) / 10) {
        explicit_exponent = order_limit;
        break;
      }
      explicit_exponent = explicit_exponent * 10 + (digit - '0');
    }
    if (exponent_negative)
      explicit_exponent = -explicit_exponent;
  }
  std::int64_t significand_order{};
  if (integral_digits > first_nonzero) {
    significand_order = static_cast<std::int64_t>(std::min<std::size_t>(
        integral_digits - first_nonzero - 1, order_limit));
  } else {
    significand_order = -static_cast<std::int64_t>(std::min<std::size_t>(
        first_nonzero - integral_digits + 1, order_limit));
  }
  const auto decimal_order =
      explicit_exponent > order_limit - significand_order ? order_limit
      : explicit_exponent < -order_limit - significand_order
          ? -order_limit
          : explicit_exponent + significand_order;
  if (decimal_order < 0)
    return negative ? -0.0 : 0.0;
  return negative ? -std::numeric_limits<double>::infinity()
                  : std::numeric_limits<double>::infinity();
}

template <class T, class Decode>
std::optional<T> typed_optional(const OrderedValue &value,
                                const std::string &path, Decode decode) {
  if (std::holds_alternative<std::nullptr_t>(value.data))
    return std::nullopt;
  return decode(value, path);
}

template <class T, class Decode>
std::vector<T> typed_list(const OrderedValue &value, const std::string &path,
                          Decode decode) {
  std::vector<T> result;
  const auto &items = typed_array(value, path);
  result.reserve(items.size());
  for (std::size_t index = 0; index < items.size(); ++index)
    result.push_back(decode(items[index], path + "/" + std::to_string(index)));
  return result;
}

template <class Enum>
Enum typed_enum(const OrderedValue &value, const std::string &path) {
  return static_cast<Enum>(typed_integer<int>(value, path));
}

DiplomaticContactSnapshot decode_contact(const OrderedValue &value,
                                         const std::string &path) {
  DiplomaticContactSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "ObserverCivilizationId")
      result.observer_civilization_id = typed_integer<int>(field, p);
    else if (name == "ContactId")
      result.contact_id = typed_string(field, p);
    else if (name == "TargetCivilizationId")
      result.target_civilization_id =
          typed_optional<int>(field, p, typed_integer<int>);
    else if (name == "FirstObservedTick")
      result.first_observed_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "LastObservedTick")
      result.last_observed_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "LastObservedSystemId")
      result.last_observed_system_id =
          typed_optional<int>(field, p, typed_integer<int>);
    else if (name == "Awareness")
      result.awareness = typed_enum<ContactAwareness>(field, p);
    else if (name == "Condition")
      result.condition = typed_enum<ContactCondition>(field, p);
    else if (name == "CommunicationAvailable")
      result.communication_available = typed_bool(field, p);
    else if (name == "Confidence")
      result.confidence = typed_double(field, p);
  }
  return result;
}

DiplomaticGrievanceSnapshot decode_grievance(const OrderedValue &value,
                                             const std::string &path) {
  DiplomaticGrievanceSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "CreatedAtTick")
      result.created_at_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "SourceCivilizationId")
      result.source_civilization_id = typed_integer<int>(field, p);
    else if (name == "Severity")
      result.severity = typed_double(field, p);
    else if (name == "Reason")
      result.reason = typed_string(field, p);
  }
  return result;
}

DiplomaticRelationshipSnapshot decode_relationship(const OrderedValue &value,
                                                   const std::string &path) {
  DiplomaticRelationshipSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "CivilizationAId")
      result.civilization_a_id = typed_integer<int>(field, p);
    else if (name == "CivilizationBId")
      result.civilization_b_id = typed_integer<int>(field, p);
    else if (name == "PoliticalState")
      result.political_state = typed_enum<DiplomaticPoliticalState>(field, p);
    else if (name == "Trust")
      result.trust = typed_double(field, p);
    else if (name == "Hostility")
      result.hostility = typed_double(field, p);
    else if (name == "Fear")
      result.fear = typed_double(field, p);
    else if (name == "Respect")
      result.respect = typed_double(field, p);
    else if (name == "Cooperation")
      result.cooperation = typed_double(field, p);
    else if (name == "Grievances")
      result.grievances =
          typed_list<DiplomaticGrievanceSnapshot>(field, p, decode_grievance);
  }
  return result;
}

DiplomaticAccessSnapshot decode_access(const OrderedValue &value,
                                       const std::string &path) {
  DiplomaticAccessSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "GrantorCivilizationId")
      result.grantor_civilization_id = typed_integer<int>(field, p);
    else if (name == "VisitorCivilizationId")
      result.visitor_civilization_id = typed_integer<int>(field, p);
    else if (name == "Permission")
      result.permission = typed_enum<AccessPermission>(field, p);
    else if (name == "UpdatedAtTick")
      result.updated_at_tick = typed_integer<std::int64_t>(field, p);
  }
  return result;
}

TerritorialClaimSnapshot decode_claim(const OrderedValue &value,
                                      const std::string &path) {
  TerritorialClaimSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "ClaimId")
      result.claim_id = typed_integer<std::int64_t>(field, p);
    else if (name == "ClaimantCivilizationId")
      result.claimant_civilization_id = typed_integer<int>(field, p);
    else if (name == "SystemId")
      result.system_id = typed_integer<int>(field, p);
    else if (name == "AssertedAtTick")
      result.asserted_at_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "Active")
      result.active = typed_bool(field, p);
    else if (name == "KnownToCivilizationIds")
      result.known_to_civilization_ids =
          typed_list<int>(field, p, typed_integer<int>);
  }
  return result;
}

TerritorialClaimResponseSnapshot decode_response(const OrderedValue &value,
                                                 const std::string &path) {
  TerritorialClaimResponseSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "ClaimId")
      result.claim_id = typed_integer<std::int64_t>(field, p);
    else if (name == "RespondingCivilizationId")
      result.responding_civilization_id = typed_integer<int>(field, p);
    else if (name == "Response")
      result.response = typed_enum<TerritorialClaimResponse>(field, p);
    else if (name == "RespondedAtTick")
      result.responded_at_tick = typed_integer<std::int64_t>(field, p);
  }
  return result;
}

DiplomaticAgreementSnapshot decode_agreement(const OrderedValue &value,
                                             const std::string &path) {
  DiplomaticAgreementSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "AgreementId")
      result.agreement_id = typed_integer<std::int64_t>(field, p);
    else if (name == "CivilizationAId")
      result.civilization_a_id = typed_integer<int>(field, p);
    else if (name == "CivilizationBId")
      result.civilization_b_id = typed_integer<int>(field, p);
    else if (name == "Type")
      result.type = typed_enum<DiplomaticAgreementType>(field, p);
    else if (name == "Status")
      result.status = typed_enum<DiplomaticAgreementStatus>(field, p);
    else if (name == "StartedAtTick")
      result.started_at_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "EndedAtTick")
      result.ended_at_tick =
          typed_optional<std::int64_t>(field, p, typed_integer<std::int64_t>);
    else if (name == "ExternalTermsReference")
      result.external_terms_reference =
          typed_optional<std::string>(field, p, typed_string);
  }
  return result;
}

DiplomaticProposalSnapshot decode_proposal(const OrderedValue &value,
                                           const std::string &path) {
  DiplomaticProposalSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "ProposalId")
      result.proposal_id = typed_integer<std::int64_t>(field, p);
    else if (name == "ProposerCivilizationId")
      result.proposer_civilization_id = typed_integer<int>(field, p);
    else if (name == "RecipientCivilizationId")
      result.recipient_civilization_id = typed_integer<int>(field, p);
    else if (name == "Kind")
      result.kind = typed_enum<DiplomaticProposalKind>(field, p);
    else if (name == "AgreementType")
      result.agreement_type = typed_optional<DiplomaticAgreementType>(
          field, p, typed_enum<DiplomaticAgreementType>);
    else if (name == "Status")
      result.status = typed_enum<DiplomaticProposalStatus>(field, p);
    else if (name == "CreatedAtTick")
      result.created_at_tick = typed_integer<std::int64_t>(field, p);
    else if (name == "ResolvedAtTick")
      result.resolved_at_tick =
          typed_optional<std::int64_t>(field, p, typed_integer<std::int64_t>);
    else if (name == "Summary")
      result.summary = typed_string(field, p);
    else if (name == "ExternalTermsReference")
      result.external_terms_reference =
          typed_optional<std::string>(field, p, typed_string);
  }
  return result;
}

DiplomaticHistoryEventSnapshot decode_history(const OrderedValue &value,
                                              const std::string &path) {
  DiplomaticHistoryEventSnapshot result;
  for (const auto &[name, field] : typed_object(value, path)) {
    const auto p = path + "/" + name;
    if (name == "EventId")
      result.event_id = typed_integer<std::int64_t>(field, p);
    else if (name == "Tick")
      result.tick = typed_integer<std::int64_t>(field, p);
    else if (name == "Kind")
      result.kind = typed_enum<DiplomaticEventKind>(field, p);
    else if (name == "PrimaryCivilizationId")
      result.primary_civilization_id = typed_integer<int>(field, p);
    else if (name == "SecondaryCivilizationId")
      result.secondary_civilization_id =
          typed_optional<int>(field, p, typed_integer<int>);
    else if (name == "SystemId")
      result.system_id = typed_optional<int>(field, p, typed_integer<int>);
    else if (name == "Summary")
      result.summary = typed_string(field, p);
    else if (name == "KnownToCivilizationIds")
      result.known_to_civilization_ids =
          typed_list<int>(field, p, typed_integer<int>);
  }
  return result;
}

DiplomacyStateSnapshot decode_diplomacy(const OrderedValue &value) {
  try {
    DiplomacyStateSnapshot result;
    std::array<bool, 8> present{};
    for (const auto &[name, field] : typed_object(value, "$.Diplomacy")) {
      const auto path = "$.Diplomacy/" + name;
      auto collection = [&](std::size_t index, auto decode, auto &target) {
        target.clear();
        present[index] = !std::holds_alternative<std::nullptr_t>(field.data);
        if (present[index]) {
          using Item = typename std::decay_t<decltype(target)>::value_type;
          target = typed_list<Item>(field, path, decode);
        }
      };
      if (name == "Contacts")
        collection(0, decode_contact, result.contacts);
      else if (name == "Relationships")
        collection(1, decode_relationship, result.relationships);
      else if (name == "AccessPermissions")
        collection(2, decode_access, result.access_permissions);
      else if (name == "Claims")
        collection(3, decode_claim, result.claims);
      else if (name == "ClaimResponses")
        collection(4, decode_response, result.claim_responses);
      else if (name == "Agreements")
        collection(5, decode_agreement, result.agreements);
      else if (name == "Proposals")
        collection(6, decode_proposal, result.proposals);
      else if (name == "RecentHistory")
        collection(7, decode_history, result.recent_history);
      else if (name == "NextClaimId")
        result.next_claim_id = typed_integer<std::int64_t>(field, path);
      else if (name == "NextAgreementId")
        result.next_agreement_id = typed_integer<std::int64_t>(field, path);
      else if (name == "NextProposalId")
        result.next_proposal_id = typed_integer<std::int64_t>(field, path);
      else if (name == "NextEventId")
        result.next_event_id = typed_integer<std::int64_t>(field, path);
    }
    constexpr std::array<std::string_view, 8> labels{
        "Contacts",  "Relationships",  "AccessPermissions",
        "Claims",    "ClaimResponses", "Agreements",
        "Proposals", "RecentHistory"};
    for (std::size_t index = 0; index < labels.size(); ++index) {
      if (!present[index]) {
        fail(PlayerCampaignJsonStage::DiplomacyValidation,
             "InvalidDataException",
             "Format v17 Diplomacy snapshot failed strict invariant "
             "validation.",
             "DiplomacySnapshotValidationException",
             std::string(labels[index]) +
                 " cannot be null in a current Diplomacy snapshot.");
      }
    }
    return result;
  } catch (const PlayerCampaignJsonError &) {
    throw;
  } catch (const OrderedTypeError &error) {
    fail(PlayerCampaignJsonStage::DiplomacyDecode, "InvalidDataException",
         "Format v17 Diplomacy snapshot could not be decoded.", "JsonException",
         error.what(), error.path, error.byte);
  }
}

#include "player_campaign_json_research_ordered.inc"

AdaptiveResearchCampaignSnapshot decode_research(const OrderedValue &value) {
  try {
    if (std::holds_alternative<std::nullptr_t>(value.data))
      fail(PlayerCampaignJsonStage::ResearchDecode, "InvalidDataException",
           "Format v17 Adaptive Research state was empty.");
    validate_research_record(value, "$.AdaptiveResearch",
                             ResearchRecord::campaign);
    check_research_representability(value, "$.AdaptiveResearch",
                                    ResearchRecord::campaign);
    return player_json_detail::decode_research(Json::parse(raw_json(value)));
  } catch (const PlayerCampaignJsonError &) {
    throw;
  } catch (const OrderedTypeError &error) {
    fail(PlayerCampaignJsonStage::ResearchDecode, "InvalidDataException",
         "Format v17 Adaptive Research state could not be decoded.",
         "JsonException", error.what(), error.path, error.byte);
  } catch (const nlohmann::json::exception &error) {
    fail(PlayerCampaignJsonStage::Representability,
         "NativeRepresentabilityException",
         "The source-compatible Adaptive Research value cannot be represented "
         "by the current typed native DTO.",
         "NativeJsonConversionException", error.what());
  }
}

void reject_research_numeric_nulls(const Json &value, const std::string &path) {
  static const std::set<std::string, std::less<>> numeric_names{
      "StageResearchPoints",
      "TotalResearchPoints",
      "Value",
      "Quality",
      "Confidence",
      "AssignedEffectiveLabs",
      "ReadinessEfficiency",
      "TotalEffectiveResearchLabs",
      "ReservedMilestoneCredits",
      "ConsumedMilestoneCredits",
      "AuthorizationCredits",
      "LastTheoreticalActivityYear",
      "LastExperimentalActivityYear",
      "LastEngineeringActivityYear",
      "Depth",
      "Availability",
      "TranslationContextQuality",
      "TrainingContinuity",
      "LastAssessmentYear",
      "Integrity"};
  if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index)
      reject_research_numeric_nulls(value[index],
                                    path + "/" + std::to_string(index));
    return;
  }
  if (!value.is_object())
    return;
  for (const auto &[name, member] : value.items()) {
    const auto child_path = path + "/" + name;
    if (numeric_names.contains(name) &&
        (member.is_null() ||
         (member.is_number_float() && !std::isfinite(member.get<double>()))))
      fail(PlayerCampaignJsonStage::Encode, "JsonException",
           "The JSON encoder rejected a non-finite Adaptive Research numeric "
           "value.",
           std::nullopt, std::nullopt, child_path);
    reject_research_numeric_nulls(member, child_path);
  }
}

void reject_nonfinite_numbers(const Json &value, const std::string &path,
                              std::string_view subsystem) {
  if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index)
      reject_nonfinite_numbers(value[index], path + "/" + std::to_string(index),
                               subsystem);
    return;
  }
  if (!value.is_object())
    return;
  for (const auto &[name, member] : value.items()) {
    const auto child_path = path + "/" + name;
    if (member.is_number_float() && !std::isfinite(member.get<double>()))
      fail(PlayerCampaignJsonStage::Encode, "JsonException",
           "The JSON encoder rejected a non-finite " + std::string(subsystem) +
               " numeric value.",
           std::nullopt, std::nullopt, child_path);
    reject_nonfinite_numbers(member, child_path, subsystem);
  }
}

void validate_research_finite(
    const AdaptiveResearchCampaignSnapshot &campaign) {
  const auto finite = [](double value, std::string path) {
    if (!std::isfinite(value))
      fail(PlayerCampaignJsonStage::Encode, "JsonException",
           "The JSON encoder rejected a non-finite Adaptive Research numeric "
           "value.",
           std::nullopt, std::nullopt, std::move(path));
  };
  for (std::size_t civilization_index = 0;
       civilization_index < campaign.civilizations.size();
       ++civilization_index) {
    const auto &civilization = campaign.civilizations[civilization_index];
    const auto base = "$/AdaptiveResearch/Civilizations/" +
                      std::to_string(civilization_index);
    for (std::size_t index = 0; index < civilization.project_funding.size();
         ++index) {
      const auto path = base + "/ProjectFunding/" + std::to_string(index);
      const auto &entry = civilization.project_funding[index];
      finite(entry.reserved_milestone_credits,
             path + "/ReservedMilestoneCredits");
      finite(entry.consumed_milestone_credits,
             path + "/ConsumedMilestoneCredits");
      finite(entry.authorization_credits, path + "/AuthorizationCredits");
    }

    const auto &v5 = civilization.research;
    const auto &v4 = v5.research;
    const auto &v3 = v4.research;
    const auto &v2 = v3.research;
    const auto &core = v2.core;
    const auto research = base + "/Research";
    finite(core.total_effective_research_labs,
           research + "/Research/Research/Research/Core/"
                      "TotalEffectiveResearchLabs");
    for (std::size_t index = 0; index < core.nodes.size(); ++index) {
      const auto path = research + "/Research/Research/Research/Core/Nodes/" +
                        std::to_string(index);
      finite(core.nodes[index].stage_research_points,
             path + "/StageResearchPoints");
      finite(core.nodes[index].total_research_points,
             path + "/TotalResearchPoints");
    }
    for (std::size_t index = 0; index < core.pressures.size(); ++index)
      finite(core.pressures[index].value,
             research + "/Research/Research/Research/Core/Pressures/" +
                 std::to_string(index));
    for (std::size_t index = 0; index < core.evidence.size(); ++index) {
      const auto path = research +
                        "/Research/Research/Research/Core/Evidence/" +
                        std::to_string(index);
      finite(core.evidence[index].quality, path + "/Quality");
      finite(core.evidence[index].confidence, path + "/Confidence");
    }
    for (std::size_t index = 0; index < core.active_projects.size(); ++index) {
      const auto path = research +
                        "/Research/Research/Research/Core/ActiveProjects/" +
                        std::to_string(index);
      const auto &project = core.active_projects[index];
      finite(project.assigned_effective_labs, path + "/AssignedEffectiveLabs");
      finite(project.readiness_efficiency, path + "/ReadinessEfficiency");
      finite(project.stage_research_points, path + "/StageResearchPoints");
      finite(project.total_research_points, path + "/TotalResearchPoints");
    }
    for (std::size_t index = 0; index < v2.expertise.fields.size(); ++index) {
      const auto path = research +
                        "/Research/Research/Research/Expertise/Fields/" +
                        std::to_string(index);
      const auto &field = v2.expertise.fields[index];
      finite(field.current.theoretical, path + "/Current/Theoretical");
      finite(field.current.experimental, path + "/Current/Experimental");
      finite(field.current.engineering, path + "/Current/Engineering");
      finite(field.historical_peak.theoretical,
             path + "/HistoricalPeak/Theoretical");
      finite(field.historical_peak.experimental,
             path + "/HistoricalPeak/Experimental");
      finite(field.historical_peak.engineering,
             path + "/HistoricalPeak/Engineering");
      finite(field.last_theoretical_activity_year,
             path + "/LastTheoreticalActivityYear");
      finite(field.last_experimental_activity_year,
             path + "/LastExperimentalActivityYear");
      finite(field.last_engineering_activity_year,
             path + "/LastEngineeringActivityYear");
    }
    for (std::size_t index = 0; index < v2.expertise.tacit_assets.size();
         ++index) {
      const auto path = research +
                        "/Research/Research/Research/Expertise/TacitAssets/" +
                        std::to_string(index);
      const auto &asset = v2.expertise.tacit_assets[index];
      finite(asset.depth, path + "/Depth");
      finite(asset.availability, path + "/Availability");
      finite(asset.translation_context_quality,
             path + "/TranslationContextQuality");
      finite(asset.training_continuity, path + "/TrainingContinuity");
    }
    for (std::size_t index = 0;
         index < v3.pressure_support.metric_signals.size(); ++index)
      finite(v3.pressure_support.metric_signals[index].value,
             research + "/Research/Research/PressureSupport/MetricSignals/" +
                 std::to_string(index));
    const auto &agenda = v3.agenda;
    finite(agenda.orientations.basic_vs_applied_orientation,
           research + "/Research/Research/Agenda/Orientations/"
                      "BasicVsAppliedOrientation");
    finite(agenda.orientations.competence_preservation_policy,
           research + "/Research/Research/Agenda/Orientations/"
                      "CompetencePreservationPolicy");
    finite(agenda.orientations.portfolio_diversity_policy,
           research + "/Research/Research/Agenda/Orientations/"
                      "PortfolioDiversityPolicy");
    finite(agenda.orientations.foreign_science_engagement,
           research + "/Research/Research/Agenda/Orientations/"
                      "ForeignScienceEngagement");
    for (std::size_t index = 0; index < agenda.culture_axes.size(); ++index)
      finite(agenda.culture_axes[index].value,
             research + "/Research/Research/Agenda/CultureAxes/" +
                 std::to_string(index));
    if (agenda.last_major_review_year)
      finite(*agenda.last_major_review_year,
             research + "/Research/Research/Agenda/LastMajorReviewYear");
    for (std::size_t index = 0; index < v4.foreign_assessments.size();
         ++index) {
      const auto path =
          research + "/Research/ForeignAssessments/" + std::to_string(index);
      finite(v4.foreign_assessments[index].last_assessment_year,
             path + "/LastAssessmentYear");
      finite(v4.foreign_assessments[index].confidence, path + "/Confidence");
    }
    for (std::size_t index = 0; index < v4.foreign_packages.size(); ++index)
      finite(v4.foreign_packages[index].integrity,
             research + "/Research/ForeignPackages/" + std::to_string(index) +
                 "/Integrity");
    for (std::size_t index = 0; index < v5.outcomes.summaries.size(); ++index)
      finite(v5.outcomes.summaries[index].last_outcome_year,
             research + "/Outcomes/Summaries/" + std::to_string(index) +
                 "/LastOutcomeYear");
    for (std::size_t index = 0; index < v5.outcomes.recent_records.size();
         ++index)
      finite(v5.outcomes.recent_records[index].year,
             research + "/Outcomes/RecentRecords/" + std::to_string(index) +
                 "/Year");
  }
}

} // namespace

PlayerCampaignJsonError::PlayerCampaignJsonError(
    PlayerCampaignJsonStage stage, std::string source_type, std::string message,
    std::optional<std::string> inner_type,
    std::optional<std::string> inner_message, std::string path,
    std::optional<std::size_t> byte)
    : std::runtime_error(std::move(message)), stage_(stage),
      source_type_(std::move(source_type)), inner_type_(std::move(inner_type)),
      inner_message_(std::move(inner_message)), path_(std::move(path)),
      byte_(byte) {}

PlayerCampaignJsonStage PlayerCampaignJsonError::stage() const noexcept {
  return stage_;
}
const std::string &PlayerCampaignJsonError::source_type() const noexcept {
  return source_type_;
}
const std::optional<std::string> &
PlayerCampaignJsonError::inner_type() const noexcept {
  return inner_type_;
}
const std::optional<std::string> &
PlayerCampaignJsonError::inner_message() const noexcept {
  return inner_message_;
}
const std::string &PlayerCampaignJsonError::path() const noexcept {
  return path_;
}
const std::optional<std::size_t> &
PlayerCampaignJsonError::byte() const noexcept {
  return byte_;
}

RestoredPlayerCampaignV17 restore_player_campaign_v17_json_impl(
    AdaptiveResearchStrategicRuntime research_runtime,
    std::string_view utf8_json, const PlayerCampaignJsonRestoreHooks &hooks) {
  report_restore_stage(hooks, PlayerCampaignJsonStage::Parse);
  OrderedValue root_value;
  try {
    root_value = json_detail::parse_ordered_json(utf8_json);
  } catch (const json_detail::ParseFailure &error) {
    fail(PlayerCampaignJsonStage::Parse, "JsonReaderException", error.what(),
         std::nullopt, std::nullopt, {}, error.byte);
  }
  const auto *root = std::get_if<OrderedObject>(&root_value.data);
  if (!root)
    fail(PlayerCampaignJsonStage::Envelope, "InvalidOperationException",
         "The node must be of type 'JsonObject'.");
  reject_root_duplicates(*root);
  if (member(*root, "DeveloperFormatVersion"))
    fail(PlayerCampaignJsonStage::Envelope, "InvalidDataException",
         "Developer campaign envelopes cannot be opened as Player saves.");
  const auto *format_member = member(*root, "FormatVersion");
  if (!format_member ||
      std::holds_alternative<std::nullptr_t>(format_member->data))
    fail(PlayerCampaignJsonStage::Envelope, "InvalidDataException",
         "Save file did not declare FormatVersion.");
  const auto format = integer(*format_member, "FormatVersion");
  if (format < 1 || format > 17)
    fail(PlayerCampaignJsonStage::Envelope, "InvalidDataException",
         "Unsupported campaign save format " + std::to_string(format) +
             "; maximum supported is 17.");
  if (format != 17)
    fail(PlayerCampaignJsonStage::Representability,
         "NativeRepresentabilityException",
         "Current Player JSON persistence represents format 17 only.");

  const auto *diplomacy_member = member(*root, "Diplomacy");
  report_restore_stage(hooks, PlayerCampaignJsonStage::DiplomacyDecode);
  if (!diplomacy_member ||
      std::holds_alternative<std::nullptr_t>(diplomacy_member->data))
    fail(PlayerCampaignJsonStage::DiplomacyDecode, "InvalidDataException",
         "Format v17 save is missing the authoritative Diplomacy snapshot.");
  const auto diplomacy = decode_diplomacy(*diplomacy_member);
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(diplomacy);
  } catch (const DiplomacySnapshotValidationError &error) {
    fail(PlayerCampaignJsonStage::DiplomacyValidation, "InvalidDataException",
         "Format v17 Diplomacy snapshot failed strict invariant validation.",
         "DiplomacySnapshotValidationException", error.what());
  }

  const auto *galaxy_format_member = member(*root, "GalaxyFormatVersion");
  report_restore_stage(hooks, PlayerCampaignJsonStage::GalaxyDecode);
  if (!galaxy_format_member ||
      std::holds_alternative<std::nullptr_t>(galaxy_format_member->data))
    fail(PlayerCampaignJsonStage::GalaxyDecode, "InvalidDataException",
         "Format v17 save is missing GalaxyFormatVersion.");
  const auto galaxy_format =
      integer(*galaxy_format_member, "GalaxyFormatVersion");
  if (galaxy_format != GalaxyPayloadV16Dto::current_format_version)
    fail(PlayerCampaignJsonStage::GalaxyDecode, "InvalidDataException",
         "Format v17 save references unsupported galaxy format " +
             std::to_string(galaxy_format) + ".");

  GalaxyPayloadV16Dto galaxy_payload;
  try {
    constexpr std::array<std::string_view, 3> excluded{
        "Diplomacy", "AdaptiveResearch", "GalaxyFormatVersion"};
    galaxy_payload = detail::decode_galaxy_payload_v16_ordered(
        root_value, {galaxy_format, excluded});
  } catch (const GalaxyPayloadJsonError &error) {
    fail(error.phase() == GalaxyPayloadJsonErrorPhase::Representability
             ? PlayerCampaignJsonStage::Representability
             : PlayerCampaignJsonStage::GalaxyDecode,
         error.phase() == GalaxyPayloadJsonErrorPhase::Representability
             ? "NativeRepresentabilityException"
             : "JsonException",
         error.what(), std::nullopt, std::nullopt, error.path(), error.byte());
  }
  std::optional<RestoredGalaxyPayloadV16> restored;
  try {
    restored.emplace(restore_galaxy_payload_v16(galaxy_payload));
  } catch (const GalaxyPayloadPersistenceArgumentNullError &error) {
    fail(PlayerCampaignJsonStage::GalaxyRestore, "ArgumentNullException",
         error.what());
  } catch (const GalaxyPayloadPersistenceNullReferenceError &error) {
    fail(PlayerCampaignJsonStage::GalaxyRestore, "NullReferenceException",
         error.what());
  } catch (const GalaxyPayloadPersistenceDataError &error) {
    fail(PlayerCampaignJsonStage::GalaxyRestore, "InvalidDataException",
         error.what(), error.inner_type(), error.inner_message());
  } catch (const GalaxyPayloadPersistenceOperationError &error) {
    fail(PlayerCampaignJsonStage::GalaxyRestore, "InvalidOperationException",
         error.what());
  }

  const auto *research_member = member(*root, "AdaptiveResearch");
  bool research_decode_started = false;
  try {
    PlayerCampaignRestoreHooks restore_hooks;
    restore_hooks.before_diplomacy_references = [&hooks] {
      report_restore_stage(hooks, PlayerCampaignJsonStage::DiplomacyReferences);
    };
    restore_hooks.before_research_restore = [&hooks] {
      report_restore_stage(hooks, PlayerCampaignJsonStage::ResearchDecode);
    };
    restore_hooks.before_diplomacy_restore = [&hooks] {
      report_restore_stage(hooks, PlayerCampaignJsonStage::DiplomacyRestore);
    };
    return detail::finalize_restored_player_campaign_v17(
        std::move(research_runtime), std::move(*restored),
        [research_member, &research_decode_started]() {
          research_decode_started = true;
          if (!research_member ||
              std::holds_alternative<std::nullptr_t>(research_member->data))
            fail(PlayerCampaignJsonStage::ResearchDecode,
                 "InvalidDataException",
                 "Format v17 save is missing Adaptive Research state.");
          return decode_research(*research_member);
        },
        diplomacy, restore_hooks);
  } catch (const PlayerCampaignJsonError &) {
    throw;
  } catch (const PlayerCampaignPersistenceDataError &error) {
    fail(research_decode_started ? PlayerCampaignJsonStage::ResearchRestore
                                 : PlayerCampaignJsonStage::DiplomacyReferences,
         "InvalidDataException", error.what(), error.inner_type(),
         error.inner_message());
  } catch (const AdaptiveResearchSnapshotError &error) {
    fail(PlayerCampaignJsonStage::ResearchRestore, "InvalidDataException",
         error.what());
  } catch (const AdaptiveResearchStrategicSnapshotError &error) {
    fail(PlayerCampaignJsonStage::ResearchRestore, "InvalidDataException",
         error.what());
  } catch (const AdaptiveResearchForeignTechnologySnapshotError &error) {
    fail(PlayerCampaignJsonStage::ResearchRestore, "InvalidDataException",
         error.what());
  } catch (const AdaptiveResearchOutcomeSnapshotError &error) {
    fail(PlayerCampaignJsonStage::ResearchRestore, "InvalidDataException",
         error.what());
  } catch (const AdaptiveResearchCampaignDataError &error) {
    fail(PlayerCampaignJsonStage::ResearchRestore, "InvalidDataException",
         error.what());
  }
}

RestoredPlayerCampaignV17 restore_player_campaign_v17_json(
    AdaptiveResearchStrategicRuntime research_runtime, std::string_view utf8_json,
    const PlayerCampaignJsonRestoreHooks &hooks) {
  try {
    return restore_player_campaign_v17_json_impl(
        std::move(research_runtime), utf8_json, hooks);
  } catch (const RestoreProgressCallbackFailure &failure) {
    std::rethrow_exception(failure.error());
  }
}

std::string
encode_player_campaign_v17_json(const PlayerCampaignPayloadV17Dto &payload) {
  if (payload.format_version != 17)
    fail(PlayerCampaignJsonStage::Representability,
         "NativeRepresentabilityException",
         "Current Player JSON persistence represents format 17 only.");
  try {
    auto root = Json::parse(encode_galaxy_payload_v16_json(payload.galaxy));
    root["FormatVersion"] = payload.format_version;
    root["GalaxyFormatVersion"] = payload.galaxy_format_version
                                      ? Json(*payload.galaxy_format_version)
                                      : Json(nullptr);
    if (payload.diplomacy) {
      auto diplomacy = player_json_detail::jsnapshot(*payload.diplomacy);
      reject_nonfinite_numbers(diplomacy, "$/Diplomacy", "Diplomacy");
      root["Diplomacy"] = std::move(diplomacy);
    } else {
      root["Diplomacy"] = nullptr;
    }
    if (payload.adaptive_research) {
      validate_research_finite(*payload.adaptive_research);
      auto research =
          player_json_detail::encode_research(*payload.adaptive_research);
      reject_research_numeric_nulls(research, "$/AdaptiveResearch");
      root["AdaptiveResearch"] = std::move(research);
    } else {
      root["AdaptiveResearch"] = nullptr;
    }
    return root.dump(2);
  } catch (const GalaxyPayloadJsonError &error) {
    fail(error.phase() == GalaxyPayloadJsonErrorPhase::Representability
             ? PlayerCampaignJsonStage::Representability
             : PlayerCampaignJsonStage::Encode,
         error.phase() == GalaxyPayloadJsonErrorPhase::Representability
             ? "NativeRepresentabilityException"
             : "JsonException",
         error.what(), std::nullopt, std::nullopt, error.path(), error.byte());
  } catch (const nlohmann::json::exception &error) {
    fail(PlayerCampaignJsonStage::Encode, "JsonException", error.what());
  }
}

} // namespace stellar::core
