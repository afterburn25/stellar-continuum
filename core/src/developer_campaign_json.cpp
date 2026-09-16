#include <stellar/core/developer_campaign_json.hpp>

#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>

#include "json_ordered_value.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace stellar::core {
namespace {

using Json = nlohmann::ordered_json;
using OrderedValue = json_detail::Value;
using OrderedObject = OrderedValue::Object;

constexpr std::array<std::string_view, 4> envelope_fields{
    "DeveloperFormatVersion", "Mode", "ToolsUsed", "Campaign"};

[[noreturn]] void fail(std::string message) {
  throw PlayerCampaignJsonError(PlayerCampaignJsonStage::Envelope,
                                "InvalidDataException", std::move(message));
}

const OrderedValue *member(const OrderedObject &object,
                           const std::string_view name) {
  for (const auto &[key, value] : object)
    if (key == name)
      return &value;
  return nullptr;
}

long long integer(const OrderedValue &value, const std::string_view name) {
  const auto *number = std::get_if<OrderedValue::Number>(&value.data);
  if (!number)
    fail(std::string(name) + " must be a JSON number.");
  try {
    return std::stoll(number->text);
  } catch (const std::exception &) {
    fail(std::string(name) + " must be a JSON integer.");
  }
}

// Source: RejectNestedSessionMetadata — the canonical payload must not smuggle
// session state that would contradict the outer envelope.
void reject_nested_session_metadata(const OrderedValue &node) {
  if (const auto *object = std::get_if<OrderedObject>(&node.data)) {
    for (const auto &[key, value] : *object) {
      if (key == "DeveloperSession" || key == "DeveloperFormatVersion" ||
          key == "Mode" || key == "ToolsUsed")
        fail("Canonical campaign payload contains contradictory nested "
             "session metadata: " +
             key + ".");
      reject_nested_session_metadata(value);
    }
  } else if (const auto *array =
                 std::get_if<OrderedValue::Array>(&node.data)) {
    for (const auto &element : *array)
      reject_nested_session_metadata(element);
  }
}

void append_string(std::string &output, const std::string &text) {
  output.push_back('"');
  for (const char c : text) {
    switch (c) {
    case '"': output += "\\\""; break;
    case '\\': output += "\\\\"; break;
    case '\b': output += "\\b"; break;
    case '\f': output += "\\f"; break;
    case '\n': output += "\\n"; break;
    case '\r': output += "\\r"; break;
    case '\t': output += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        constexpr char digits[] = "0123456789abcdef";
        output += "\\u00";
        output.push_back(digits[(c >> 4) & 0xf]);
        output.push_back(digits[c & 0xf]);
      } else {
        output.push_back(c);
      }
    }
  }
  output.push_back('"');
}

void append_ordered(std::string &output, const OrderedValue &value) {
  if (std::holds_alternative<std::nullptr_t>(value.data)) {
    output += "null";
  } else if (const auto *boolean = std::get_if<bool>(&value.data)) {
    output += *boolean ? "true" : "false";
  } else if (const auto *number =
                 std::get_if<OrderedValue::Number>(&value.data)) {
    output += number->text;
  } else if (const auto *text = std::get_if<std::string>(&value.data)) {
    append_string(output, *text);
  } else if (const auto *array =
                 std::get_if<OrderedValue::Array>(&value.data)) {
    output.push_back('[');
    bool first = true;
    for (const auto &element : *array) {
      if (!first)
        output.push_back(',');
      first = false;
      append_ordered(output, element);
    }
    output.push_back(']');
  } else {
    output.push_back('{');
    bool first = true;
    for (const auto &[key, element] : std::get<OrderedObject>(value.data)) {
      if (!first)
        output.push_back(',');
      first = false;
      append_string(output, key);
      output.push_back(':');
      append_ordered(output, element);
    }
    output.push_back('}');
  }
}

} // namespace

PlayerCampaignPayloadV17Dto capture_developer_campaign_v17(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options) {
  auto &galaxy = campaign.world().campaign();
  if (!galaxy.developer_provenance)
    throw PlayerCampaignPersistenceOperationError(
        "Developer saves require explicit Developer session provenance.");
  if (!std::isfinite(options.simulation_days) ||
      options.simulation_days < 0.0)
    throw PlayerCampaignPersistenceRangeError(
        "Simulation time must be finite and non-negative. (Parameter "
        "'simulationDays')");

  const auto diplomacy = campaign.diplomacy().snapshot();
  (void)DiplomacySnapshotInvariantValidator::validate(diplomacy);
  DiplomacyCampaignReferenceValidator::validate(galaxy, diplomacy);
  auto galaxy_payload = capture_galaxy_payload_v16(
      galaxy, {options.simulation_days, options.game_version,
               options.saved_at_utc, true});
  if (galaxy_payload.format_version !=
      GalaxyPayloadV16Dto::current_format_version)
    throw PlayerCampaignPersistenceDataError(
        "Expected galaxy payload format 16, got " +
        std::to_string(galaxy_payload.format_version) + ".");
  auto research =
      AdaptiveResearchCampaignSnapshotCodec(campaign.research_runtime())
          .capture(campaign.research());
  return {PlayerCampaignPayloadV17Dto::current_format_version,
          GalaxyPayloadV16Dto::current_format_version,
          std::move(galaxy_payload), diplomacy, std::move(research)};
}

std::string encode_developer_campaign_json(
    const PlayerCampaignPayloadV17Dto &payload, const bool tools_used) {
  Json envelope;
  envelope["DeveloperFormatVersion"] = 1;
  envelope["Mode"] = "Developer";
  envelope["ToolsUsed"] = tools_used;
  envelope["Campaign"] =
      Json::parse(encode_player_campaign_v17_json(payload));
  return envelope.dump(2);
}

LoadedDeveloperCampaignV17 restore_developer_campaign_json(
    AdaptiveResearchStrategicRuntime research_runtime,
    const std::string_view utf8_json,
    const PlayerCampaignJsonRestoreHooks &hooks) {
  OrderedValue root_value;
  try {
    root_value = json_detail::parse_ordered_json(utf8_json);
  } catch (const json_detail::ParseFailure &error) {
    throw PlayerCampaignJsonError(
        PlayerCampaignJsonStage::Parse, "JsonReaderException", error.what(),
        std::nullopt, std::nullopt, {}, error.byte);
  }
  const auto *root = std::get_if<OrderedObject>(&root_value.data);
  if (!root)
    fail("Developer save must contain an envelope object.");
  std::set<std::string_view> missing(envelope_fields.begin(),
                                     envelope_fields.end());
  for (const auto &[key, value] : *root)
    if (missing.erase(key) == 0)
      fail("Developer envelope has an unexpected or duplicate field: " + key +
           ".");
  if (!missing.empty()) {
    std::string names;
    for (const auto field : missing) {
      if (!names.empty())
        names += ", ";
      names += field;
    }
    fail("Developer envelope is missing required fields: " + names + ".");
  }
  if (integer(*member(*root, "DeveloperFormatVersion"),
              "DeveloperFormatVersion") != 1)
    fail("Unsupported Developer save format; expected "
         "DeveloperFormatVersion 1.");
  const auto *mode = member(*root, "Mode");
  const auto *mode_text =
      mode ? std::get_if<std::string>(&mode->data) : nullptr;
  if (!mode_text || *mode_text != "Developer")
    fail("Developer envelope must declare Mode exactly as Developer.");
  const auto *tools = member(*root, "ToolsUsed");
  const auto *tools_used = tools ? std::get_if<bool>(&tools->data) : nullptr;
  if (!tools_used)
    fail("Developer envelope ToolsUsed must be an explicit boolean.");
  const auto *campaign = member(*root, "Campaign");
  const auto *campaign_object =
      campaign ? std::get_if<OrderedObject>(&campaign->data) : nullptr;
  if (!campaign_object)
    fail("Developer Campaign must contain a canonical v9, v11, v13, v15 or "
         "v17 campaign payload.");
  const auto *canonical_version = member(*campaign_object, "FormatVersion");
  if (!canonical_version)
    fail("Developer Campaign must contain a canonical v9, v11, v13, v15 or "
         "v17 campaign payload.");
  const auto format = integer(*canonical_version, "Campaign.FormatVersion");
  if (format != 9 && format != 11 && format != 13 && format != 15 &&
      format != 17)
    fail("Developer Campaign must contain a canonical v9, v11, v13, v15 or "
         "v17 campaign payload.");
  reject_nested_session_metadata(*campaign);

  std::string campaign_json;
  campaign_json.reserve(utf8_json.size());
  append_ordered(campaign_json, *campaign);
  auto restored = restore_player_campaign_v17_json(
      std::move(research_runtime), campaign_json, hooks);
  restored.galaxy().developer_provenance =
      CampaignDeveloperProvenance{*tools_used};
  return {std::move(restored), *tools_used};
}

} // namespace stellar::core
