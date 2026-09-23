#include "planet_appearance_json.hpp"
#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/developer_campaign.hpp>

#include <stellar/core/detail/player_campaign_persistence_restore.hpp>
#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/galaxy_payload_json.hpp>

#include "galaxy_payload_json_internal.hpp"
#include "json_encode_validation.hpp"
#include "player_campaign_json_diplomacy.hpp"
#include "player_campaign_json_research.hpp"

#include <stellar/engine/history.hpp>

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

Json encode_event_history(const engine::EventHistory::State &);
engine::EventHistory::State decode_event_history(const OrderedValue &);

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
  if (const auto* array = std::get_if<OrderedValue::DeferredArray>(&value.data)) {
    output += array->source;
    return;
  }
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

static RestoredPlayerCampaignV17 restore_player_campaign_v17_ordered(
    AdaptiveResearchStrategicRuntime research_runtime,
    const OrderedValue& root_value, const PlayerCampaignJsonRestoreHooks &hooks,
    std::optional<CampaignDeveloperProvenance> developer = std::nullopt) {
  const auto *root = std::get_if<OrderedObject>(&root_value.data);
  if (!root)
    fail(PlayerCampaignJsonStage::Envelope, "InvalidOperationException",
         "The node must be of type 'JsonObject'.");
  reject_root_duplicates(*root);
  if (member(*root, "DeveloperFormatVersion"))
    fail(PlayerCampaignJsonStage::Envelope, "InvalidDataException",
         "Developer campaign envelopes cannot be opened as Player saves.");
  const auto *developer_member=member(*root,"DeveloperSession");
  if(developer){
    if(!developer_member||!typed_bool(*developer_member,"$.DeveloperSession"))
      fail(PlayerCampaignJsonStage::Envelope,"InvalidDataException","Developer campaign payload is missing its provenance marker.");
  }else if(developer_member)
    fail(PlayerCampaignJsonStage::Envelope,"InvalidDataException","Developer campaign payloads cannot be opened as Player saves.");
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

  std::optional<engine::EventHistory::State> event_history;
  if (const auto *history_member = member(*root, "EventHistory");
      history_member &&
      !std::holds_alternative<std::nullptr_t>(history_member->data))
    event_history = decode_event_history(*history_member);

  GalaxyPayloadV16Dto galaxy_payload;
  try {
    constexpr std::array<std::string_view, 4> excluded{
        "Diplomacy", "AdaptiveResearch", "GalaxyFormatVersion",
        "EventHistory"};
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
    restored->galaxy.developer_provenance=developer;
    validate_developer_coverage(restored->galaxy);
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
  // The restored galaxy owns its state; release the intermediate DTO before
  // research restoration builds its indexes and runtime objects.
  galaxy_payload = {};
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
        diplomacy, std::move(event_history), restore_hooks);
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
    report_restore_stage(hooks, PlayerCampaignJsonStage::Parse);
    OrderedValue root;
    try {
      constexpr std::array<std::string_view, 2> arrays{
          "/Galaxy/Systems", "/Galaxy/PlanetaryBodies"};
      root = json_detail::parse_ordered_json(utf8_json, {arrays});
    } catch (const json_detail::ParseFailure &error) {
      fail(PlayerCampaignJsonStage::Parse, "JsonReaderException", error.what(),
           std::nullopt, std::nullopt, {}, error.byte);
    }
    return restore_player_campaign_v17_ordered(
        std::move(research_runtime), root, hooks);
  } catch (const RestoreProgressCallbackFailure &failure) {
    std::rethrow_exception(failure.error());
  }
}

namespace {
Json encode_player_campaign_tail(const PlayerCampaignPayloadV17Dto& payload){
    Json tail=Json::object();
    tail["GalaxyFormatVersion"] = payload.galaxy_format_version
                                      ? Json(*payload.galaxy_format_version)
                                      : Json(nullptr);
    if (payload.diplomacy) {
      auto diplomacy = player_json_detail::jsnapshot(*payload.diplomacy);
      reject_nonfinite_numbers(diplomacy, "$/Diplomacy", "Diplomacy");
      tail["Diplomacy"] = std::move(diplomacy);
    } else {
      tail["Diplomacy"] = nullptr;
    }
    if (payload.adaptive_research) {
      validate_research_finite(*payload.adaptive_research);
      auto research =
          player_json_detail::encode_research(*payload.adaptive_research);
      reject_research_numeric_nulls(research, "$/AdaptiveResearch");
      tail["AdaptiveResearch"] = std::move(research);
    } else {
      tail["AdaptiveResearch"] = nullptr;
    }
    tail["EventHistory"] = payload.event_history
                               ? encode_event_history(*payload.event_history)
                               : Json(nullptr);
    json_detail::validate_encoded_text(tail["Diplomacy"]);
    json_detail::validate_encoded_text(tail["AdaptiveResearch"]);
    json_detail::validate_encoded_text(tail["EventHistory"]);
    return tail;
}
Json encode_player_campaign_document(const PlayerCampaignPayloadV17Dto &payload) {
  if (payload.format_version != 17)
    fail(PlayerCampaignJsonStage::Representability,
         "NativeRepresentabilityException",
         "Current Player JSON persistence represents format 17 only.");
  try {
    auto root = detail::encode_galaxy_payload_v16_document(payload.galaxy);
    root["FormatVersion"] = payload.format_version;
    auto tail=encode_player_campaign_tail(payload);
    for(auto& [key,value]:tail.items())root[key]=std::move(value);
    return root;
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
} // namespace

std::string encode_player_campaign_v17_json(const PlayerCampaignPayloadV17Dto &payload) {
  try {
    return encode_player_campaign_document(payload).dump(2);
  } catch (const nlohmann::json::exception &error) {
    fail(PlayerCampaignJsonStage::Encode, "JsonException", error.what());
  }
}

namespace {
Json encode_runtime_continuation(const CampaignRuntimeContinuation &state){
  Json plans=Json::array();
  for(const auto &plan:state.strategic.plans){
    Json priorities=Json::array();
    for(const auto &priority:plan.priorities)priorities.push_back({{"Type",static_cast<int>(priority.type)},
        {"Score",priority.score},{"Reason",priority.reason}});
    plans.push_back({{"CivilizationId",plan.civilization_id},{"GeneratedAt",plan.generated_at_tick},
        {"ReviewAfter",plan.review_after_tick},{"Priorities",std::move(priorities)}});
  }
  Json strategic={{"Days",state.strategic.strategic_days},{"Plans",std::move(plans)}};
  if(state.strategic.campaign_seed)strategic["CampaignSeed"]=*state.strategic.campaign_seed;
  const auto &d=state.diplomacy;const auto &m=d.maintenance;
  return {{"Version",1},{"Strategic",std::move(strategic)},
    {"Diplomacy",{{"LastProcessed",d.last_processed_tick},{"Initialized",m.initialized},
      {"NextReview",m.next_review_tick},{"LastReview",m.last_review_tick},
      {"ReviewInterval",m.policy.review_interval_ticks},{"ContactStaleAfter",m.policy.contact_stale_after_ticks},
      {"ProposalLifetime",m.policy.proposal_lifetime_ticks}}}};
}
CampaignRuntimeContinuation decode_runtime_continuation(const OrderedValue &value){
  const auto object=[](const OrderedValue &v)->const OrderedObject&{
    const auto &o=typed_object(v,"$.RuntimeContinuation");reject_root_duplicates(o);return o;
  };
  const auto field=[](const OrderedObject &o,const char *name)->const OrderedValue&{
    if(const auto *v=member(o,name))return *v;
    throw PlayerCampaignPersistenceDataError(std::string("Incomplete runtime continuation: ")+name);
  };
  const auto integer_field=[&](const OrderedObject &o,const char *name){
    return typed_integer<std::int64_t>(field(o,name),std::string("$.RuntimeContinuation.")+name);
  };
  const auto &root=object(value);
  if(integer_field(root,"Version")!=1)throw PlayerCampaignPersistenceDataError("Unsupported runtime continuation.");
  CampaignRuntimeContinuation result;
  const auto &s=object(field(root,"Strategic"));
  result.strategic.strategic_days=typed_double(field(s,"Days"),"$.RuntimeContinuation.Strategic.Days");
  if(member(s,"CampaignSeed"))result.strategic.campaign_seed=integer_field(s,"CampaignSeed");
  const auto &plans=typed_array(field(s,"Plans"),"$.RuntimeContinuation.Strategic.Plans");
  if(plans.size()>4096)throw PlayerCampaignPersistenceDataError("Too many strategic plans.");
  for(const auto &entry:plans){
    const auto &p=object(entry);CivilizationStrategicPlan plan;
    plan.civilization_id=typed_integer<int>(field(p,"CivilizationId"),"$.RuntimeContinuation.CivilizationId");
    plan.generated_at_tick=integer_field(p,"GeneratedAt");plan.review_after_tick=integer_field(p,"ReviewAfter");
    const auto &priorities=typed_array(field(p,"Priorities"),"$.RuntimeContinuation.Priorities");
    if(priorities.size()>8)throw PlayerCampaignPersistenceDataError("Too many strategic priorities.");
    for(const auto &entry_priority:priorities){
      const auto &a=object(entry_priority);
      const auto type=integer_field(a,"Type");
      if(type<0||type>7)throw PlayerCampaignPersistenceDataError("Unknown strategic priority.");
      plan.priorities.push_back({static_cast<StrategicPriorityType>(type),
        typed_double(field(a,"Score"),"$.RuntimeContinuation.Score"),
        typed_string(field(a,"Reason"),"$.RuntimeContinuation.Reason")});
    }
    result.strategic.plans.push_back(std::move(plan));
  }
  const auto &d=object(field(root,"Diplomacy"));
  result.diplomacy.last_processed_tick=integer_field(d,"LastProcessed");
  auto &m=result.diplomacy.maintenance;
  m.initialized=typed_bool(field(d,"Initialized"),"$.RuntimeContinuation.Initialized");
  m.next_review_tick=integer_field(d,"NextReview");m.last_review_tick=integer_field(d,"LastReview");
  m.policy={integer_field(d,"ReviewInterval"),integer_field(d,"ContactStaleAfter"),integer_field(d,"ProposalLifetime")};
  return result;
}
Json encode_event_history(const engine::EventHistory::State &state){
  Json events=Json::array();
  for(const auto &e:state.events){
    Json actors=Json::array();
    for(const auto id:e.actors)actors.push_back(id);
    Json visible=Json::array();
    for(const auto id:e.visible_to)visible.push_back(id);
    Json tags=Json::array();
    for(const auto &tag:e.tags)tags.push_back(tag);
    events.push_back({{"Id",e.id},{"AtDay",e.at_day},{"Category",e.category},
      {"Summary",e.summary},{"Actors",std::move(actors)},{"Location",e.location},
      {"Significance",e.significance},{"VisibleTo",std::move(visible)},
      {"Tags",std::move(tags)}});
  }
  return {{"Version",state.version},{"NextId",state.next_id},{"Events",std::move(events)}};
}
engine::EventHistory::State decode_event_history(const OrderedValue &value){
  const auto &root=typed_object(value,"$.EventHistory");
  const auto field=[](const OrderedObject &o,const char *name)->const OrderedValue&{
    if(const auto *v=member(o,name))return *v;
    throw PlayerCampaignPersistenceDataError(std::string("Incomplete event history: ")+name);
  };
  engine::EventHistory::State result;
  result.version=static_cast<std::uint32_t>(typed_integer<std::int64_t>(field(root,"Version"),"$.EventHistory.Version"));
  if(result.version!=1)throw PlayerCampaignPersistenceDataError("Unsupported event history version.");
  result.next_id=static_cast<std::uint64_t>(typed_integer<std::int64_t>(field(root,"NextId"),"$.EventHistory.NextId"));
  const auto &events=typed_array(field(root,"Events"),"$.EventHistory.Events");
  if(events.size()>1000000)throw PlayerCampaignPersistenceDataError("Too many history events.");
  for(const auto &entry:events){
    const auto &o=typed_object(entry,"$.EventHistory.Events");
    engine::HistoryEvent e;
    e.id=static_cast<std::uint64_t>(typed_integer<std::int64_t>(field(o,"Id"),"$.EventHistory.Events.Id"));
    e.at_day=typed_double(field(o,"AtDay"),"$.EventHistory.Events.AtDay");
    e.category=typed_string(field(o,"Category"),"$.EventHistory.Events.Category");
    e.summary=typed_string(field(o,"Summary"),"$.EventHistory.Events.Summary");
    e.location=static_cast<std::uint64_t>(typed_integer<std::int64_t>(field(o,"Location"),"$.EventHistory.Events.Location"));
    e.significance=typed_double(field(o,"Significance"),"$.EventHistory.Events.Significance");
    for(const auto &a:typed_array(field(o,"Actors"),"$.EventHistory.Events.Actors"))
      e.actors.push_back(static_cast<std::uint64_t>(typed_integer<std::int64_t>(a,"$.EventHistory.Events.Actors")));
    if(const auto *v=member(o,"VisibleTo"))
      for(const auto &a:typed_array(*v,"$.EventHistory.Events.VisibleTo"))
        e.visible_to.push_back(static_cast<std::uint64_t>(typed_integer<std::int64_t>(a,"$.EventHistory.Events.VisibleTo")));
    if(const auto *t=member(o,"Tags"))
      for(const auto &tag:typed_array(*t,"$.EventHistory.Events.Tags"))
        e.tags.push_back(typed_string(tag,"$.EventHistory.Events.Tags"));
    result.events.push_back(std::move(e));
  }
  engine::EventHistory probe;
  try{probe.restore_state(result);}
  catch(const std::invalid_argument &error){
    throw PlayerCampaignPersistenceDataError(std::string("Invalid event history: ")+error.what());
  }
  return result;
}
}

DeveloperCampaignPayload capture_developer_campaign(
    IntegratedAdaptiveCampaignRuntime &campaign,const PlayerCampaignCaptureOptions &options){
  auto &world=campaign.world().campaign();
  if(!world.developer_provenance)
    throw PlayerCampaignPersistenceOperationError("Developer persistence requires an explicitly marked campaign.");
  if(!std::isfinite(options.simulation_days)||options.simulation_days<0)
    throw PlayerCampaignPersistenceRangeError("Invalid developer simulation time.");
  const auto diplomacy=campaign.diplomacy().snapshot();
  (void)DiplomacySnapshotInvariantValidator::validate(diplomacy);
  DiplomacyCampaignReferenceValidator::validate(world,diplomacy);
  PlayerCampaignPayloadV17Dto common;
  common.galaxy=capture_galaxy_payload_v16(world,{options.simulation_days,options.game_version,options.saved_at_utc,true});
  common.diplomacy=diplomacy;
  common.adaptive_research=AdaptiveResearchCampaignSnapshotCodec(campaign.research_runtime()).capture(campaign.research());
  common.event_history=campaign.history().capture_state();
  auto continuation=campaign.continuation();
  validate_developer_coverage(world);
  validate_campaign_runtime_continuation(continuation,world,options.simulation_days);
  return {std::move(common),*world.developer_provenance,std::move(continuation)};
}

namespace {
Json developer_envelope(const DeveloperCampaignPayload &snapshot,Json payload){
  const auto &provenance=snapshot.provenance;
  validate_developer_simulation_state(provenance.simulation);
  if(!provenance.tools_used&&(provenance.normal_research_completed||provenance.special_research_completed||provenance.player_ai_control||provenance.full_exploration))
    throw PlayerCampaignPersistenceDataError("Developer overrides require ToolsUsed provenance.");
  if(payload.is_object())payload["DeveloperSession"]=true;
  Json envelope={{"DeveloperFormatVersion",1},{"Mode","Developer"},{"DeveloperSession",true},
    {"ToolsUsed",provenance.tools_used},{"NormalResearchCompleted",provenance.normal_research_completed},
    {"SpecialResearchCompleted",provenance.special_research_completed},
    {"PlayerAiControl",provenance.player_ai_control},
    {"Simulation",{{"FixedTicks",provenance.simulation.fixed_ticks},{"Speed",provenance.simulation.speed},
       {"CompletedTicks",provenance.simulation.completed_ticks},{"BacklogNanoseconds",provenance.simulation.backlog_nanoseconds},
       {"TacticalCompletedTicks",provenance.simulation.tactical_completed_ticks},
       {"TacticalBacklogNanoseconds",provenance.simulation.tactical_backlog_nanoseconds}}},
    {"Campaign",std::move(payload)}};
  if(provenance.giant_test){const auto& lab=*provenance.giant_test;const auto& q=lab.controls;
    envelope["GiantLaboratory"]={{"Version",1},{"Class",static_cast<int>(q.type)},{"Subclass",q.subclass},{"PlanetVariant",q.planet_variant},{"RingVariant",q.ring_variant},{"Rings",q.rings},{"RingFamily",q.ring_family},{"Tilt",q.axial_tilt_degrees},{"Distance",q.distance_scale},{"Star",static_cast<int>(q.star_type)},{"Appearance",lab.appearance}};
  }
  if(provenance.full_exploration)envelope["FullExploration"]=true;
  if(snapshot.continuation)envelope["RuntimeContinuation"]=encode_runtime_continuation(*snapshot.continuation);
  if(provenance.full_celestial_coverage)envelope["CelestialCoverage"]={{"Enabled",true},{"Version",provenance.coverage_generation_version},{"ForcedSystemIds",provenance.coverage_forced_system_ids}};
  return envelope;
}
}

std::string encode_developer_campaign_json(const DeveloperCampaignPayload& snapshot){
  return developer_envelope(snapshot,encode_player_campaign_document(snapshot.campaign)).dump(2);
}
namespace {
void stream_player(detail::JsonStreamWriter& out,const PlayerCampaignPayloadV17Dto& payload,bool developer){
  if(payload.format_version!=17)fail(PlayerCampaignJsonStage::Representability,"NativeRepresentabilityException","Current Player JSON persistence represents format 17 only.");
  try{
    auto tail=encode_player_campaign_tail(payload);
    out.begin_object();detail::stream_galaxy_members(out,payload.galaxy,payload.format_version);
    for(const auto& [key,value]:tail.items())out.field(key,value);
    if(developer)out.field("DeveloperSession",true);
    out.end_object();
  }catch(const GalaxyPayloadJsonError& error){
    fail(error.phase()==GalaxyPayloadJsonErrorPhase::Representability?PlayerCampaignJsonStage::Representability:PlayerCampaignJsonStage::Encode,
      error.phase()==GalaxyPayloadJsonErrorPhase::Representability?"NativeRepresentabilityException":"JsonException",error.what(),std::nullopt,std::nullopt,error.path(),error.byte());
  }catch(const nlohmann::json::exception& error){fail(PlayerCampaignJsonStage::Encode,"JsonException",error.what());}
}
}
void stream_player_campaign_v17_json(const PlayerCampaignPayloadV17Dto& payload,const stellar::engine::AtomicTextSink& sink){
  detail::JsonStreamWriter out(sink);stream_player(out,payload,false);
}
void stream_developer_campaign_json(const DeveloperCampaignPayload& snapshot,const stellar::engine::AtomicTextSink& sink){
  const auto envelope=developer_envelope(snapshot,nullptr);detail::JsonStreamWriter out(sink);out.begin_object();
  for(const auto& [key,value]:envelope.items()){
    out.member(key);if(key=="Campaign")stream_player(out,snapshot.campaign,true);else out.value(value);
  }out.end_object();
}

std::string capture_developer_campaign_json(
    IntegratedAdaptiveCampaignRuntime &campaign,const PlayerCampaignCaptureOptions &options){
  return encode_developer_campaign_json(capture_developer_campaign(campaign,options));
}

RestoredPlayerCampaignV17 restore_developer_campaign_json(
    AdaptiveResearchStrategicRuntime runtime,std::string_view text,
    const PlayerCampaignJsonRestoreHooks &hooks){
  constexpr std::array<std::string_view, 2> arrays{
      "/Campaign/Galaxy/Systems", "/Campaign/Galaxy/PlanetaryBodies"};
  const auto ordered=json_detail::parse_ordered_json(text, {arrays});
  const auto &root=typed_object(ordered,"$");reject_root_duplicates(root);
  const auto *version=member(root,"DeveloperFormatVersion"),*mode=member(root,"Mode"),
    *session=member(root,"DeveloperSession"),*used=member(root,"ToolsUsed"),
    *normal=member(root,"NormalResearchCompleted"),*special=member(root,"SpecialResearchCompleted"),
    *payload=member(root,"Campaign");
  if(!version||integer(*version,"DeveloperFormatVersion")!=1||!mode||typed_string(*mode,"$.Mode")!="Developer"||
     !session||!typed_bool(*session,"$.DeveloperSession")||!used||!normal||!special||!payload)
    throw PlayerCampaignPersistenceDataError("Unsupported or incomplete developer campaign envelope.");
  CampaignDeveloperProvenance provenance{typed_bool(*used,"$.ToolsUsed"),
    typed_bool(*normal,"$.NormalResearchCompleted"),typed_bool(*special,"$.SpecialResearchCompleted")};
  if(const auto *simulation=member(root,"Simulation")){
    const auto &fields=typed_object(*simulation,"$.Simulation");reject_root_duplicates(fields);
    const auto *fixed=member(fields,"FixedTicks"),*speed=member(fields,"Speed"),
      *ticks=member(fields,"CompletedTicks"),*backlog=member(fields,"BacklogNanoseconds");
    if(!fixed||!speed||!ticks||!backlog)throw PlayerCampaignPersistenceDataError("Incomplete developer simulation state.");
    provenance.simulation={typed_bool(*fixed,"$.Simulation.FixedTicks"),typed_integer<std::uint32_t>(*speed,"$.Simulation.Speed"),
      typed_integer<std::uint64_t>(*ticks,"$.Simulation.CompletedTicks"),typed_integer<std::int64_t>(*backlog,"$.Simulation.BacklogNanoseconds")};
    const auto *battle_ticks=member(fields,"TacticalCompletedTicks"),*battle_backlog=member(fields,"TacticalBacklogNanoseconds");
    if(battle_ticks||battle_backlog){
      if(!battle_ticks||!battle_backlog)throw PlayerCampaignPersistenceDataError("Incomplete tactical simulation state.");
      provenance.simulation.tactical_completed_ticks=typed_integer<std::uint64_t>(*battle_ticks,"$.Simulation.TacticalCompletedTicks");
      provenance.simulation.tactical_backlog_nanoseconds=typed_integer<std::int64_t>(*battle_backlog,"$.Simulation.TacticalBacklogNanoseconds");
    }
  }
  validate_developer_simulation_state(provenance.simulation);
  if(const auto *ai=member(root,"PlayerAiControl"))provenance.player_ai_control=typed_bool(*ai,"$.PlayerAiControl");
  if(const auto *exploration=member(root,"FullExploration"))provenance.full_exploration=typed_bool(*exploration,"$.FullExploration");
  if(const auto *coverage=member(root,"CelestialCoverage")){
    const auto &fields=typed_object(*coverage,"$.CelestialCoverage");reject_root_duplicates(fields);
    const auto *enabled=member(fields,"Enabled"),*coverage_version=member(fields,"Version"),*ids=member(fields,"ForcedSystemIds");
    if(!enabled||!coverage_version||!ids)throw PlayerCampaignPersistenceDataError("Incomplete coverage metadata.");
    provenance.full_celestial_coverage=typed_bool(*enabled,"$.CelestialCoverage.Enabled");
    provenance.coverage_generation_version=typed_string(*coverage_version,"$.CelestialCoverage.Version");
    const auto &array=typed_array(*ids,"$.CelestialCoverage.ForcedSystemIds");
    if(array.size()>stellar_object_type_count)throw PlayerCampaignPersistenceDataError("Too many forced stellar objects.");
    for(const auto &id:array)provenance.coverage_forced_system_ids.push_back(typed_integer<int>(id,"$.CelestialCoverage.ForcedSystemIds[]"));
  }
  if(!provenance.tools_used&&(provenance.normal_research_completed||provenance.special_research_completed||provenance.player_ai_control||provenance.full_exploration))
    throw PlayerCampaignPersistenceDataError("Developer overrides require ToolsUsed provenance.");
  try{
    report_restore_stage(hooks, PlayerCampaignJsonStage::Parse);
    auto restored=restore_player_campaign_v17_ordered(std::move(runtime),*payload,hooks,provenance);
    if(const auto* lab=member(root,"GiantLaboratory")){
      const auto j=nlohmann::json::parse(raw_json(*lab));if(j.at("Version").get<int>()!=1)throw PlayerCampaignPersistenceDataError("Unsupported giant laboratory version");
      DeveloperGiantTestRequest q;q.type=static_cast<PlanetClass>(j.at("Class").get<int>());q.subclass=j.at("Subclass");q.planet_variant=j.at("PlanetVariant");q.ring_variant=j.at("RingVariant");q.rings=j.at("Rings");q.ring_family=j.at("RingFamily");q.axial_tilt_degrees=j.at("Tilt");q.distance_scale=j.at("Distance");q.star_type=static_cast<StellarObjectType>(j.at("Star").get<int>());
      auto& world=restored.galaxy();world.developer_provenance->giant_test=DeveloperGiantTestState{q,j.at("Appearance").get<PlanetAppearance>()};(void)build_developer_giant_test(world);
    }
    if(const auto *continuation=member(root,"RuntimeContinuation"))
      restored.set_runtime_continuation(decode_runtime_continuation(*continuation));
    return restored;
  }
  catch(const RestoreProgressCallbackFailure &failure){std::rethrow_exception(failure.error());}
}

} // namespace stellar::core
