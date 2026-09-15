#include <stellar/core/adaptive_research_starting_profiles.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {
namespace {
using json = nlohmann::ordered_json;
using Writer = detail::AdaptiveResearchStateWriter;

json parse_json(std::string_view text) {
  struct ObjectFrame {
    std::unordered_set<std::string> keys;
    std::optional<std::string> pending_key;
    bool rejects_duplicates{};
  };
  const auto is_dictionary_property = [](std::string_view name) {
    return name == "starting_node_states" ||
           name == "starting_field_competence" ||
           name == "starting_pressure_state" ||
           name == "additional_starting_pressure_state";
  };
  std::vector<ObjectFrame> objects;
  json::parser_callback_t callback =
      [&objects, is_dictionary_property](int, json::parse_event_t event,
                                         json &parsed) {
        if (event == json::parse_event_t::object_start) {
          bool reject = false;
          if (!objects.empty() && objects.back().pending_key) {
            reject = is_dictionary_property(*objects.back().pending_key);
            objects.back().pending_key.reset();
          }
          objects.push_back({{}, {}, reject});
        } else if (event == json::parse_event_t::object_end) {
          objects.pop_back();
          if (!objects.empty()) objects.back().pending_key.reset();
        } else if (event == json::parse_event_t::array_start) {
          if (!objects.empty()) objects.back().pending_key.reset();
        } else if (event == json::parse_event_t::key) {
          const auto key = parsed.get<std::string>();
          if (objects.back().rejects_duplicates &&
              !objects.back().keys.insert(key).second)
            throw std::invalid_argument(
                "An item with the same key has already been added. Key: " +
                key);
          objects.back().pending_key = key;
        } else if (event == json::parse_event_t::value && !objects.empty()) {
          objects.back().pending_key.reset();
        }
        return true;
      };
  return json::parse(text, callback);
}

int checked_int32(const json &value) {
  if (value.is_number_unsigned()) {
    const auto wide = value.get<std::uint64_t>();
    if (wide <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      return static_cast<int>(wide);
  } else if (value.is_number_integer()) {
    const auto wide = value.get<std::int64_t>();
    if (wide >= std::numeric_limits<int>::min() &&
        wide <= std::numeric_limits<int>::max())
      return static_cast<int>(wide);
  }
  throw std::out_of_range("JSON integer is outside the Int32 range.");
}

struct NodeSeed {
  std::string id;
  ResearchMaturity maturity{};
  std::optional<std::string> resolution;
};
struct CapabilitySeed {
  std::string id;
  std::optional<std::string> context;
};
struct EvidenceSeed {
  std::string instance_id;
  std::string type_id;
  std::string provenance;
  double quality{};
  double confidence{};
  std::optional<std::string> context;
};
struct Fragment {
  std::string id;
  std::string kind;
  std::vector<NodeSeed> nodes;
  std::vector<std::pair<std::string, StartingFieldCompetenceSeed>> competence;
  std::vector<std::string> traits;
  std::vector<CapabilitySeed> capabilities;
  std::vector<StartingResearchInstitutionSeed> institutions;
  std::vector<std::pair<std::string, double>> pressures;
  std::vector<EvidenceSeed> evidence;
  std::vector<StartingTacitAssetSeed> tacit;
  std::optional<std::string> stage;
  std::optional<std::string> notes;
};
struct Profile {
  std::string id;
  std::vector<std::string> fragments;
  std::vector<std::string> traits;
  std::vector<std::pair<std::string, double>> pressures;
  std::vector<CapabilitySeed> capabilities;
  std::vector<EvidenceSeed> evidence;
  std::optional<std::string> notes;
};

class JsonShapeError final : public std::logic_error {
public:
  using std::logic_error::logic_error;
};

std::string_view json_kind(const json &value) {
  if (value.is_object()) return "Object";
  if (value.is_array()) return "Array";
  if (value.is_string()) return "String";
  if (value.is_null()) return "Null";
  if (value.is_boolean()) return value.get<bool>() ? "True" : "False";
  if (value.is_number()) return "Number";
  return "Undefined";
}

[[noreturn]] void wrong_json_kind(std::string_view expected,
                                  const json &actual) {
  throw JsonShapeError(
      "The requested operation requires an element of type '" +
      std::string(expected) + "', but the target element has type '" +
      std::string(json_kind(actual)) + "'.");
}

const json &require_array(const json &value, std::string_view context) {
  (void)context;
  if (!value.is_array()) wrong_json_kind("Array", value);
  return value;
}

const json &require_object(const json &value, std::string_view context) {
  (void)context;
  if (!value.is_object()) wrong_json_kind("Object", value);
  return value;
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not find file '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string required(const json &value, std::string_view property,
                     std::string_view context) {
  require_object(value, context);
  const auto it = value.find(property);
  if (it == value.end() || !it->is_string())
    throw std::runtime_error(std::string(context) + " is missing string '" +
                             std::string(property) + "'.");
  return it->get<std::string>();
}
std::optional<std::string> optional_string(const json &value,
                                           std::string_view property) {
  require_object(value, property);
  const auto it = value.find(property);
  return it != value.end() && it->is_string()
             ? std::optional<std::string>(it->get<std::string>())
             : std::nullopt;
}
std::optional<double> optional_number(const json &value,
                                      std::string_view property) {
  require_object(value, property);
  const auto it = value.find(property);
  return it != value.end() && it->is_number()
             ? std::optional<double>(it->get<double>())
             : std::nullopt;
}
std::vector<std::string> strings(const json &value, std::string_view property) {
  const auto it = value.find(property);
  if (it == value.end())
    return {};
  std::vector<std::string> result;
  for (const auto &entry : require_array(*it, property)) {
    if (entry.is_null())
      throw std::runtime_error(std::string(property) + " contains null.");
    if (!entry.is_string()) wrong_json_kind("String", entry);
    result.push_back(entry.get<std::string>());
  }
  return result;
}
std::vector<std::pair<std::string, double>> numbers(
    const json &value, std::string_view property) {
  const auto it = value.find(property);
  if (it == value.end())
    return {};
  std::vector<std::pair<std::string, double>> result;
  for (const auto &[id, entry] : require_object(*it, property).items()) {
    if (!entry.is_number()) wrong_json_kind("Number", entry);
    result.emplace_back(id, entry.get<double>());
  }
  return result;
}
ResearchMaturity maturity(std::string_view value, std::string_view file,
                          std::string_view fragment) {
  if (value == "rumored") return ResearchMaturity::rumored;
  if (value == "hypothesized") return ResearchMaturity::hypothesized;
  if (value == "investigable") return ResearchMaturity::investigable;
  if (value == "experimental") return ResearchMaturity::experimental;
  if (value == "demonstrated") return ResearchMaturity::demonstrated;
  if (value == "engineering") return ResearchMaturity::engineering;
  if (value == "mature") return ResearchMaturity::mature;
  if (value == "archived") return ResearchMaturity::archived;
  throw std::runtime_error(std::string(file) + ":" + std::string(fragment) +
                           " uses unknown research maturity '" +
                           std::string(value) + "'.");
}
int rank(ResearchMaturity value) { return static_cast<int>(value); }

std::string_view maturity_name(ResearchMaturity value) {
  switch (value) {
  case ResearchMaturity::rumored: return "Rumored";
  case ResearchMaturity::hypothesized: return "Hypothesized";
  case ResearchMaturity::investigable: return "Investigable";
  case ResearchMaturity::experimental: return "Experimental";
  case ResearchMaturity::demonstrated: return "Demonstrated";
  case ResearchMaturity::engineering: return "Engineering";
  case ResearchMaturity::mature: return "Mature";
  case ResearchMaturity::archived: return "Archived";
  }
  return {};
}

double dotnet_min(double left, double right) {
  return std::isnan(left) || std::isnan(right) ?
             std::numeric_limits<double>::quiet_NaN() :
             std::min(left, right);
}

double dotnet_max(double left, double right) {
  return std::isnan(left) || std::isnan(right) ?
             std::numeric_limits<double>::quiet_NaN() :
             std::max(left, right);
}

bool ascii_iequals(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length) return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80) return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty()) return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value)) return false;
  return true;
}

std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0) {
      code_point = first & 0x1f;
      length = 2;
    } else if ((first & 0xf0) == 0xe0) {
      code_point = first & 0x0f;
      length = 3;
    } else if ((first & 0xf8) == 0xf0) {
      code_point = first & 0x07;
      length = 4;
    }
    for (std::size_t index = 1; index < length && index < value.size();
         ++index)
      code_point = (code_point << 6) |
                   (static_cast<unsigned char>(value[index]) & 0x3f);
    value.remove_prefix(std::min(length, value.size()));
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
  }
  return result;
}

bool utf16_ordinal_less(std::string_view left, std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}

std::vector<CapabilitySeed> capabilities(const json &value,
                                         std::string_view property) {
  const auto it = value.find(property);
  if (it == value.end()) return {};
  std::vector<CapabilitySeed> result;
  for (const auto &entry : require_array(*it, property)) {
    if (entry.is_string()) result.push_back({entry.get<std::string>(), {}});
    else if (entry.is_object())
      result.push_back({required(entry, "capability_id", property),
                        optional_string(entry, "context_id")});
    else throw std::runtime_error(std::string(property) +
                                  " contains an invalid capability seed.");
  }
  return result;
}
std::vector<EvidenceSeed> evidence(const json &value, std::string_view property,
                                   std::string_view source) {
  const auto it = value.find(property);
  if (it == value.end()) return {};
  std::vector<EvidenceSeed> result;
  int index = 0;
  for (const auto &entry : require_array(*it, property)) {
    const auto type = required(entry, "evidence_type_id", property);
    auto instance = optional_string(entry, "evidence_instance_id");
    if (!instance)
      instance = "start:" + std::string(source) + ":" + type + ":" +
                 std::to_string(index++);
    result.push_back({*instance, type,
                      optional_string(entry, "provenance")
                          .value_or("starting_history:" + std::string(source)),
                      optional_number(entry, "quality").value_or(1.0),
                      optional_number(entry, "confidence").value_or(1.0),
                      optional_string(entry, "context_id")});
  }
  return result;
}

} // namespace

struct AdaptiveResearchStartingProfileComposer::Storage {
  const AdaptiveResearchRuntime *runtime;
  std::filesystem::path root;
  std::vector<Fragment> fragments;
  std::vector<Profile> profiles;
  std::vector<std::string> fragment_ids;
  std::vector<std::string> profile_ids;
  int competence_cap{100};

  const Fragment *fragment(std::string_view id) const {
    const auto it = std::find_if(fragments.begin(), fragments.end(),
                                 [id](const auto &v) { return v.id == id; });
    return it == fragments.end() ? nullptr : std::addressof(*it);
  }
  const Profile *profile(std::string_view id) const {
    const auto it = std::find_if(profiles.begin(), profiles.end(),
                                 [id](const auto &v) { return v.id == id; });
    return it == profiles.end() ? nullptr : std::addressof(*it);
  }
  void validate_catalog(const json &value, std::string_view file) const {
    const auto actual = required(value, "catalog_id", file);
    const auto &expected = runtime->catalog().metadata().catalog_id;
    if (actual != expected)
      throw std::runtime_error(std::string(file) + " catalog_id '" + actual +
                               "' does not match '" + expected + "'.");
  }
  Fragment parse_fragment(const json &value, std::string_view file) const {
    Fragment result;
    result.id = required(value, "id", file);
    result.kind = required(value, "kind", file);
    if (const auto it = value.find("starting_node_states"); it != value.end())
      for (const auto &[id, entry] :
           require_object(*it, "starting_node_states").items()) {
        if (entry.is_string())
          result.nodes.push_back({id, maturity(entry.get<std::string>(), file,
                                               result.id), {}});
        else if (entry.is_object())
          result.nodes.push_back(
              {id, maturity(required(entry, "state", file), file, result.id),
               optional_string(entry, "archive_resolution")});
        else
          throw std::runtime_error(std::string(file) + ":" + result.id +
                                   " has invalid starting state for '" + id +
                                   "'.");
      }
    if (const auto it = value.find("starting_field_competence");
        it != value.end())
      for (const auto &[id, entry] :
           require_object(*it, "starting_field_competence").items()) {
        require_object(entry, "starting_field_competence value");
        StartingFieldCompetenceSeed seed{
            entry.at("theoretical").get<double>(),
            entry.at("experimental").get<double>(),
            entry.at("engineering").get<double>()};
        if (seed.theoretical < 0 || seed.theoretical > 100 ||
            seed.experimental < 0 || seed.experimental > 100 ||
            seed.engineering < 0 || seed.engineering > 100)
          throw std::runtime_error(std::string(file) + ":" + result.id +
                                   " competence '" + id +
                                   "' is outside 0..100.");
        result.competence.emplace_back(id, seed);
      }
    result.traits = strings(value, "starting_applicability_traits");
    result.capabilities = capabilities(value, "starting_capabilities");
    if (const auto it = value.find("starting_research_institutions");
        it != value.end())
      for (const auto &entry :
           require_array(*it, "starting_research_institutions")) {
        auto id = required(entry, "institution_archetype_id", file);
        const int count = checked_int32(entry.at("count"));
        if (count <= 0)
          throw std::runtime_error(std::string(file) + ":" + result.id +
                                   " institution '" + id +
                                   "' has non-positive count.");
        result.institutions.push_back({std::move(id), count});
      }
    result.pressures = numbers(value, "starting_pressure_state");
    result.evidence = evidence(value, "starting_evidence", result.id);
    if (const auto it = value.find("starting_tacit_assets"); it != value.end())
      for (const auto &entry :
           require_array(*it, "starting_tacit_assets")) {
        if (entry.is_string())
          result.tacit.push_back(
              {entry.get<std::string>(), "starting_history", {}});
        else if (entry.is_object())
          result.tacit.push_back(
              {required(entry, "asset_type_id", "starting_tacit_assets"),
               optional_string(entry, "provenance")
                   .value_or("starting_history"),
               optional_string(entry, "scope_ref")});
        else
          throw std::runtime_error(
              "starting_tacit_assets contains an invalid tacit-asset seed.");
      }
    result.stage = optional_string(value, "starting_directed_program_stage");
    result.notes = optional_string(value, "historical_notes");
    return result;
  }
  Profile parse_profile(const json &value, std::string_view file) const {
    Profile result;
    result.id = required(value, "id", file);
    result.fragments = strings(value, "fragment_ids");
    result.traits = strings(value, "additional_starting_applicability_traits");
    result.pressures = numbers(value, "additional_starting_pressure_state");
    result.capabilities =
        capabilities(value, "additional_starting_capabilities");
    result.evidence =
        evidence(value, "additional_starting_evidence", result.id);
    result.notes = optional_string(value, "historical_notes");
    return result;
  }
  void load() {
    const auto index_path = root / "starting_profile_index.json";
    const auto index = parse_json(read_file(index_path));
    validate_catalog(index, "starting_profile_index.json");
    const auto &files = require_object(index.at("files"), "files");
    for (const auto &file_value : require_array(
             files.at("history_fragment_files"), "history_fragment_files")) {
      const auto file = file_value.get<std::string>();
      const auto document = parse_json(read_file(root / file));
      validate_catalog(document, file);
      if (const auto cap = document.find(
              "composition_cap_per_competence_component");
          cap != document.end())
        competence_cap = std::min(competence_cap, checked_int32(*cap));
      for (const auto &entry :
           require_array(document.at("fragments"), "fragments")) {
        auto parsed = parse_fragment(entry, file);
        if (fragment(parsed.id))
          throw std::runtime_error("Duplicate starting research fragment '" +
                                   parsed.id + "'.");
        fragment_ids.push_back(parsed.id);
        fragments.push_back(std::move(parsed));
      }
    }
    for (const auto &file_value : require_array(
             files.at("reference_profile_files"), "reference_profile_files")) {
      const auto file = file_value.get<std::string>();
      const auto document = parse_json(read_file(root / file));
      validate_catalog(document, file);
      for (const auto &entry :
           require_array(document.at("profiles"), "profiles")) {
        auto parsed = parse_profile(entry, file);
        if (profile(parsed.id))
          throw std::runtime_error(
              "Duplicate starting research reference profile '" + parsed.id +
              "'.");
        profile_ids.push_back(parsed.id);
        profiles.push_back(std::move(parsed));
      }
    }
    const auto &counts = require_object(index.at("counts"), "counts");
    if (static_cast<int>(fragments.size()) !=
        checked_int32(counts.at("history_fragments")))
      throw std::runtime_error(
          "Starting research fragment count does not match "
          "starting_profile_index.json.");
    if (static_cast<int>(profiles.size()) !=
        checked_int32(counts.at("reference_profiles")))
      throw std::runtime_error(
          "Starting research reference-profile count does not match "
          "starting_profile_index.json.");
  }
};

AdaptiveResearchStartingProfileComposer::AdaptiveResearchStartingProfileComposer(
    const AdaptiveResearchRuntime &runtime,
    const std::filesystem::path &root_path)
    : storage_(std::make_unique<Storage>()) {
  storage_->runtime = &runtime;
  storage_->root = std::filesystem::absolute(root_path);
  storage_->load();
}
AdaptiveResearchStartingProfileComposer::~AdaptiveResearchStartingProfileComposer() =
    default;
AdaptiveResearchStartingProfileComposer::AdaptiveResearchStartingProfileComposer(
    AdaptiveResearchStartingProfileComposer &&) noexcept = default;
AdaptiveResearchStartingProfileComposer &
AdaptiveResearchStartingProfileComposer::operator=(
    AdaptiveResearchStartingProfileComposer &&) noexcept = default;
std::span<const std::string>
AdaptiveResearchStartingProfileComposer::reference_profile_ids() const noexcept {
  return storage_->profile_ids;
}
std::span<const std::string>
AdaptiveResearchStartingProfileComposer::fragment_ids() const noexcept {
  return storage_->fragment_ids;
}

AdaptiveResearchStartingCompositionResult
AdaptiveResearchStartingProfileComposer::compose_reference_profile(
    std::string civilization_id, std::string reference_profile_id,
    std::string primary_context_id) const {
  if (blank(civilization_id))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'civilizationId')");
  if (blank(reference_profile_id))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'referenceProfileId')");
  if (blank(primary_context_id))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'primaryApplicabilityContextId')");
  const auto *profile = storage_->profile(reference_profile_id);
  if (!profile)
    throw std::out_of_range("Unknown starting research reference profile '" +
                            reference_profile_id + "'.");
  std::vector<const Fragment *> selected;
  for (const auto &id : profile->fragments) {
    const auto *value = storage_->fragment(id);
    if (!value)
      throw std::runtime_error("Starting profile '" + profile->id +
                               "' references unknown fragment '" + id +
                               "'.");
    selected.push_back(value);
  }
  if (std::count_if(selected.begin(), selected.end(), [](const auto *value) {
        return value->kind == "base_era";
      }) != 1)
    throw std::runtime_error("Starting profile '" + profile->id +
                             "' must compose exactly one base-era fragment.");

  std::vector<NodeSeed> nodes;
  for (const auto *fragment : selected)
    for (const auto &seed : fragment->nodes) {
      (void)storage_->runtime->catalog().get_node(seed.id);
      const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const auto &v) {
        return v.id == seed.id;
      });
      if (it == nodes.end()) nodes.push_back(seed);
      else {
        if ((it->maturity == ResearchMaturity::archived) !=
            (seed.maturity == ResearchMaturity::archived))
          throw std::runtime_error(
              "Starting fragments contain conflicting archived/non-archived "
              "history for '" + seed.id + "'.");
        if (rank(seed.maturity) > rank(it->maturity)) *it = seed;
      }
    }
  std::vector<std::pair<std::string, double>> pressures;
  const auto merge_pressure = [&](const auto &pair) {
    const auto it = std::find_if(pressures.begin(), pressures.end(),
                                 [&](const auto &v) { return v.first == pair.first; });
    if (it == pressures.end()) pressures.push_back(pair);
    else it->second = dotnet_max(it->second, pair.second);
  };
  for (const auto *fragment : selected)
    for (const auto &pair : fragment->pressures) merge_pressure(pair);
  for (const auto &pair : profile->pressures) merge_pressure(pair);

  std::vector<std::string> traits;
  const auto add_trait = [&](const std::string &id) {
    if (std::find(traits.begin(), traits.end(), id) == traits.end())
      traits.push_back(id);
  };
  for (const auto *fragment : selected)
    for (const auto &id : fragment->traits) add_trait(id);
  for (const auto &id : profile->traits) add_trait(id);
  std::vector<CapabilitySeed> capability_seeds;
  for (const auto *fragment : selected)
    for (const auto &seed : fragment->capabilities) {
      const auto found = std::find_if(capability_seeds.begin(), capability_seeds.end(),
          [&](const auto &v) { return v.id == seed.id && v.context == seed.context; });
      if (found == capability_seeds.end()) capability_seeds.push_back(seed);
    }
  for (const auto &seed : profile->capabilities) {
    const auto found = std::find_if(capability_seeds.begin(), capability_seeds.end(),
        [&](const auto &v) { return v.id == seed.id && v.context == seed.context; });
    if (found == capability_seeds.end()) capability_seeds.push_back(seed);
  }
  std::vector<EvidenceSeed> evidence_seeds;
  for (const auto *fragment : selected)
    for (const auto &seed : fragment->evidence)
      if (std::none_of(evidence_seeds.begin(), evidence_seeds.end(),
                       [&](const auto &v) { return v.instance_id == seed.instance_id; }))
        evidence_seeds.push_back(seed);
  for (const auto &seed : profile->evidence)
    if (std::none_of(evidence_seeds.begin(), evidence_seeds.end(),
                     [&](const auto &v) { return v.instance_id == seed.instance_id; }))
      evidence_seeds.push_back(seed);

  std::vector<StartingResearchInstitutionSeed> institutions;
  for (const auto *fragment : selected)
    for (const auto &seed : fragment->institutions) {
      const auto it = std::find_if(institutions.begin(), institutions.end(),
          [&](const auto &v) { return v.institution_archetype_id == seed.institution_archetype_id; });
      if (it == institutions.end()) institutions.push_back(seed);
      else {
        const auto sum = static_cast<long long>(it->count) + seed.count;
        if (sum > std::numeric_limits<int>::max() ||
            sum < std::numeric_limits<int>::min())
          throw std::overflow_error("Arithmetic operation resulted in an overflow.");
        it->count = static_cast<int>(sum);
      }
    }
  std::sort(institutions.begin(), institutions.end(), [](const auto &a, const auto &b) {
    return utf16_ordinal_less(a.institution_archetype_id,
                              b.institution_archetype_id);
  });
  std::vector<std::pair<std::string, StartingFieldCompetenceSeed>> competence;
  for (const auto *fragment : selected)
    for (const auto &[id, raw] : fragment->competence) {
      if (!storage_->runtime->catalog().has_knowledge_field(id))
        throw std::runtime_error("Starting fragment references unknown knowledge field '" + id + "'.");
      StartingFieldCompetenceSeed value{
          dotnet_min(storage_->competence_cap, raw.theoretical),
          dotnet_min(storage_->competence_cap, raw.experimental),
          dotnet_min(storage_->competence_cap, raw.engineering)};
      const auto it = std::find_if(competence.begin(), competence.end(),
                                   [&](const auto &v) { return v.first == id; });
      if (it == competence.end()) competence.emplace_back(id, value);
      else {
        it->second.theoretical = dotnet_max(it->second.theoretical, value.theoretical);
        it->second.experimental = dotnet_max(it->second.experimental, value.experimental);
        it->second.engineering = dotnet_max(it->second.engineering, value.engineering);
      }
    }
  std::vector<StartingTacitAssetSeed> tacit;
  for (const auto *fragment : selected)
    tacit.insert(tacit.end(), fragment->tacit.begin(), fragment->tacit.end());

  auto state = storage_->runtime->create_civilization_state(civilization_id);
  std::vector<std::string> stages;
  for (const auto *fragment : selected)
    if (fragment->stage &&
        std::find(stages.begin(), stages.end(), *fragment->stage) == stages.end())
      stages.push_back(*fragment->stage);
  std::string stage_id = storage_->runtime->catalog()
                             .metadata()
                             .starting_directed_program_stage_id;
  int best = std::numeric_limits<int>::min();
  for (const auto &id : stages) {
    const auto &stage = storage_->runtime->catalog().get_directed_program_stage(id);
    const int score = stage.lab_capacity_only
                          ? std::numeric_limits<int>::max()
                          : stage.directed_program_limit.value_or(0);
    if (score >= best) { best = score; stage_id = id; }
  }
  Writer::set_directed_program_stage(state, stage_id);
  std::vector<std::string> population_traits;
  for (const auto &id : traits) {
    const auto &trait = storage_->runtime->applicability().get_trait(id);
    if (trait.scope == ResearchApplicabilityTraitScope::civilization)
      Writer::add_civilization_trait(state, id);
    else population_traits.push_back(id);
  }
  Writer::set_applicability_context_traits(state, primary_context_id,
                                           population_traits);
  for (const auto &[id, value] : pressures) {
    if (!storage_->runtime->catalog().has_pressure(id))
      throw std::runtime_error("Starting profile '" + profile->id +
                               "' references unknown Research Pressure '" + id + "'.");
    Writer::set_pressure(state, id, value);
  }
  for (const auto &seed : evidence_seeds) {
    if (!storage_->runtime->catalog().has_evidence_type(seed.type_id))
      throw std::runtime_error("Starting profile '" + profile->id +
                               "' references unknown evidence type '" + seed.type_id + "'.");
    Writer::add_evidence(state, {seed.instance_id, seed.type_id, seed.provenance,
        seed.quality, seed.confidence,
        seed.context ? seed.context : std::optional<std::string>(primary_context_id),
        detail::checked_next_research_state_revision(state.revision())});
  }
  std::vector<const NodeSeed *> ordered_nodes;
  ordered_nodes.reserve(nodes.size());
  for (const auto &seed : nodes) ordered_nodes.push_back(&seed);
  std::stable_sort(ordered_nodes.begin(), ordered_nodes.end(),
                   [&](const auto *a, const auto *b) {
    return storage_->runtime->catalog().get_node(a->id).graph_depth <
           storage_->runtime->catalog().get_node(b->id).graph_depth;
  });
  for (const auto *seed_pointer : ordered_nodes) {
    const auto &seed = *seed_pointer;
    const auto &node = storage_->runtime->catalog().get_node(seed.id);
    double total = 0;
    if (seed.maturity == ResearchMaturity::demonstrated)
      total = storage_->runtime->progress_policy()
                  .get_stage_band(ResearchMaturity::experimental)
                  .end_fraction * node.project_requirements.base_research_points;
    else if (seed.maturity == ResearchMaturity::engineering)
      total = storage_->runtime->progress_policy()
                  .get_stage_band(ResearchMaturity::demonstrated)
                  .end_fraction * node.project_requirements.base_research_points;
    else if (seed.maturity == ResearchMaturity::mature ||
             (seed.maturity == ResearchMaturity::archived && seed.resolution &&
              (ascii_iequals(*seed.resolution, "mature_history") ||
               ascii_iequals(*seed.resolution, "superseded"))))
      total = node.project_requirements.base_research_points;
    Writer::set_node_state(state, {seed.id, seed.maturity, seed.resolution, 0,
                                   total, 0});
  }
  for (const auto &node_state : state.node_states()) {
    if (node_state.maturity == ResearchMaturity::rumored ||
        node_state.maturity == ResearchMaturity::hypothesized ||
        node_state.maturity == ResearchMaturity::archived) continue;
    const auto &node =
        storage_->runtime->catalog().get_node(node_state.node_id);
    for (const auto &id : node.prerequisites.all_of)
      if (!state.has_established_knowledge(id))
        throw std::runtime_error("Starting profile '" + profile->id + "' seeds '" + node.id + "' at " +
          std::string(maturity_name(node_state.maturity)) +
          " without mature prerequisite '" + id + "'.");
    if (!node.prerequisites.any_of.empty() &&
        std::none_of(node.prerequisites.any_of.begin(), node.prerequisites.any_of.end(),
                     [&](const auto &id) { return state.has_established_knowledge(id); }))
      throw std::runtime_error("Starting profile '" + profile->id + "' seeds '" + node.id + "' without any mature alternative prerequisite.");
  }
  for (const auto &seed : nodes) {
    if (seed.maturity < ResearchMaturity::investigable ||
        seed.maturity == ResearchMaturity::archived) continue;
    const auto &node = storage_->runtime->catalog().get_node(seed.id);
    for (const auto &id : node.applicability.traits) {
      const auto &trait = storage_->runtime->applicability().get_trait(id);
      const bool present = trait.scope == ResearchApplicabilityTraitScope::civilization
                               ? state.has_civilization_trait(id)
                               : state.has_applicability_trait(primary_context_id, id);
      if (!present)
        throw std::runtime_error("Starting profile '" + profile->id + "' seeds '" + node.id + "' without required applicability trait '" + id + "'.");
    }
  }
  for (const auto &seed : capability_seeds) {
    const auto *definition = storage_->runtime->catalog().find_capability(seed.id);
    if (!definition)
      throw std::runtime_error("Starting profile '" + profile->id + "' references unknown capability '" + seed.id + "'.");
    (void)storage_->runtime->add_capability(
        state, seed.id,
        definition->scope == ResearchCapabilityScope::civilization
            ? std::nullopt
            : std::optional<std::string_view>(seed.context ? *seed.context : primary_context_id));
  }
  for (const auto &seed : nodes) {
    if (seed.maturity != ResearchMaturity::mature) continue;
    const auto &node = storage_->runtime->catalog().get_node(seed.id);
    for (const auto &id : node.declared_capabilities)
      if (const auto *definition = storage_->runtime->catalog().find_capability(id))
        (void)storage_->runtime->add_capability(
            state, id,
            definition->scope == ResearchCapabilityScope::civilization
                ? std::nullopt
                : std::optional<std::string_view>(primary_context_id));
    const auto grants = storage_->runtime->catalog().mature_grants();
    const auto grant = std::find_if(grants.begin(), grants.end(), [&](const auto &v) { return v.node_id == seed.id; });
    if (grant == grants.end()) continue;
    for (const auto &id : grant->grant.capability_ids) {
      const auto *definition = storage_->runtime->catalog().find_capability(id);
      (void)storage_->runtime->add_capability(state, id,
          definition->scope == ResearchCapabilityScope::civilization
              ? std::nullopt : std::optional<std::string_view>(primary_context_id));
    }
    for (const auto &id : grant->grant.civilization_trait_ids) {
      const auto &trait = storage_->runtime->applicability().get_trait(id);
      if (trait.scope == ResearchApplicabilityTraitScope::civilization)
        Writer::add_civilization_trait(state, id);
      else Writer::add_applicability_trait(state, primary_context_id, id);
    }
    if (grant->grant.research_capacity_stage_id)
      Writer::set_directed_program_stage(state,
                                         *grant->grant.research_capacity_stage_id);
    for (const auto &id : grant->grant.enabled_deployment_event_ids)
      Writer::add_enabled_deployment_event(state, id);
  }
  double total_labs = 0;
  for (const auto &seed : institutions) {
    const auto *institution = storage_->runtime->facilities().find_institution(
        seed.institution_archetype_id);
    if (!institution)
      throw std::runtime_error("Starting profile '" + profile->id +
                               "' references unknown research institution '" +
                               seed.institution_archetype_id + "'.");
    total_labs += institution->effective_lab_units * seed.count;
    for (const auto &id : institution->facility_capabilities)
      Writer::add_facility_capability(state, id);
  }
  Writer::set_total_effective_research_labs(state, total_labs);
  std::vector<std::string> basic;
  for (const auto &node : storage_->runtime->catalog().nodes())
    if (node.public_normal_research &&
        std::find(node.awareness_sources.begin(), node.awareness_sources.end(),
                  "basic_science") != node.awareness_sources.end())
      basic.push_back(node.id);
  auto events = storage_->runtime->review_basic_science_candidates(
      state, basic, primary_context_id);
  AdaptiveResearchDeferredStartingState deferred{
      competence, institutions, tacit, {}, profile->id, profile->notes};
  for (const auto *fragment : selected)
    deferred.selected_fragment_ids.push_back(fragment->id);
  return {std::move(state), std::move(deferred), std::move(events)};
}

} // namespace stellar::core
