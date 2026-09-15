#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stellar/core/adaptive_research_outcomes.hpp>
#include <stellar/core/detail/adaptive_research_outcome_support_access.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;
std::string profile_id(ResearchUncertaintyProfile value);
ResearchUncertaintyProfile parse_profile(std::string_view id);
std::string outcome_display(ResearchOutcomeKind value) {
  switch (value) {
  case ResearchOutcomeKind::progress:
    return "Progress";
  case ResearchOutcomeKind::setback:
    return "Setback";
  case ResearchOutcomeKind::partial_success:
    return "PartialSuccess";
  case ResearchOutcomeKind::hypothesis_supported:
    return "HypothesisSupported";
  case ResearchOutcomeKind::hypothesis_refined:
    return "HypothesisRefined";
  case ResearchOutcomeKind::hypothesis_disproven:
    return "HypothesisDisproven";
  case ResearchOutcomeKind::anomalous_result:
    return "AnomalousResult";
  case ResearchOutcomeKind::hazard_incident:
    return "HazardIncident";
  case ResearchOutcomeKind::side_discovery:
    return "SideDiscovery";
  }
  throw std::out_of_range("Unknown research outcome.");
}
[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchOutcomeCatalogError(std::move(message));
}
const Json &require_object(const Json &value) {
  if (!value.is_object())
    throw Json::type_error::create(302, "value must be an object", &value);
  return value;
}
const Json &require_array(const Json &value) {
  if (!value.is_array())
    throw Json::type_error::create(302, "value must be an array", &value);
  return value;
}
[[noreturn]] void json_error(const Json::exception &e) {
  AdaptiveResearchOutcomeJsonErrorKind kind =
      AdaptiveResearchOutcomeJsonErrorKind::invalid_operation;
  if (dynamic_cast<const Json::parse_error *>(&e))
    kind = AdaptiveResearchOutcomeJsonErrorKind::reader;
  else if (const auto *out = dynamic_cast<const Json::out_of_range *>(&e))
    kind = out->id == 403
               ? AdaptiveResearchOutcomeJsonErrorKind::missing_property
               : AdaptiveResearchOutcomeJsonErrorKind::format;
  throw AdaptiveResearchOutcomeJsonError(kind, e.what());
}
std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw AdaptiveResearchOutcomeFileError("Could not find file '" +
                                           path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}
bool rejects_duplicate_key(std::string_view path) {
  return path == "/public_seed_weights" ||
         path.starts_with("/public_seed_weights/") ||
         path == "/effects/outcome_competence_gain";
}
std::string duplicate_key_display(std::string_view path, std::string_view key) {
  if (path == "/public_seed_weights")
    return profile_id(parse_profile(key));
  if (path.starts_with("/public_seed_weights/") ||
      path == "/effects/outcome_competence_gain")
    return outcome_display(AdaptiveResearchOutcomeCatalog::parse_outcome(key));
  return std::string(key);
}
Json load_json(const std::filesystem::path &root, const char *name) {
  struct Frame {
    bool object{};
    std::string path, pending;
    std::set<std::string, std::less<>> keys;
  };
  std::vector<Frame> stack;
  auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
    if (event == Json::parse_event_t::object_start ||
        event == Json::parse_event_t::array_start) {
      std::string path;
      if (!stack.empty()) {
        path = stack.back().path;
        if (stack.back().object && !stack.back().pending.empty()) {
          path += "/" + stack.back().pending;
          stack.back().pending.clear();
        }
      }
      stack.push_back({event == Json::parse_event_t::object_start,
                       std::move(path),
                       {},
                       {}});
    } else if (event == Json::parse_event_t::key) {
      auto &frame = stack.back();
      const auto key = parsed.get<std::string>();
      if (rejects_duplicate_key(frame.path) && !frame.keys.insert(key).second)
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: " +
            duplicate_key_display(frame.path, key));
      frame.pending = key;
    } else if (event == Json::parse_event_t::value) {
      if (!stack.empty() && stack.back().object)
        stack.back().pending.clear();
    } else if (event == Json::parse_event_t::object_end ||
               event == Json::parse_event_t::array_end)
      stack.pop_back();
    return true;
  };
  try {
    return Json::parse(bytes(root / name), callback);
  } catch (const AdaptiveResearchOutcomeCatalogError &) {
    throw;
  } catch (const Json::exception &e) {
    json_error(e);
  }
}
void catalog_id(const Json &root, std::string_view expected,
                std::string_view file) {
  const auto &v = root.at("catalog_id");
  const auto actual = v.is_null() ? std::string{} : v.get<std::string>();
  if (actual != expected)
    fail(std::string(file) + " catalog_id '" + actual + "' != '" +
         std::string(expected) + "'.");
}
int int32(const Json &v) {
  if (v.is_number_unsigned()) {
    const auto n = v.get<std::uint64_t>();
    if (n > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      throw Json::out_of_range::create(406, "number overflow", &v);
    return static_cast<int>(n);
  }
  if (v.is_number_float())
    throw Json::out_of_range::create(406, "number is not an Int32", &v);
  if (!v.is_number_integer())
    throw Json::type_error::create(302, "value must be an integer", &v);
  const auto n = v.get<std::int64_t>();
  if (n < std::numeric_limits<int>::min() ||
      n > std::numeric_limits<int>::max())
    throw Json::out_of_range::create(406, "number overflow", &v);
  return static_cast<int>(n);
}
double fraction(const Json &root, const char *name) {
  const auto value = root.at(name).get<double>();
  if (value < 0 || value > 1 || !std::isfinite(value))
    fail(std::string(name) + " must be within 0..1.");
  return value;
}
std::vector<std::string> strings(const Json &array) {
  if (!array.is_array())
    throw Json::type_error::create(302, "value must be an array", &array);
  std::vector<std::string> result;
  for (const auto &v : array) {
    if (!v.is_string())
      fail("Expected string array value.");
    result.push_back(v.get<std::string>());
  }
  return result;
}
void deduplicate(std::vector<std::string> &values) {
  std::set<std::string, std::less<>> seen;
  std::erase_if(values,
                [&](const auto &value) { return !seen.insert(value).second; });
}
std::vector<std::uint16_t> utf16_units(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point{};
    std::size_t width{};
    if (first <= 0x7f) {
      code_point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      code_point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      code_point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code_point = first & 0x07;
      width = 4;
    } else
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    if (value.size() < width)
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      code_point = (code_point << 6) | (byte & 0x3f);
    }
    if ((width == 3 && (code_point < 0x800 ||
                        (code_point >= 0xd800 && code_point <= 0xdfff))) ||
        (width == 4 && (code_point < 0x10000 || code_point > 0x10ffff)))
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    if (code_point <= 0xffff)
      result.push_back(static_cast<std::uint16_t>(code_point));
    else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(
          static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}
bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16_units(left) < utf16_units(right);
}
std::int64_t depth_distance(int left, int right) noexcept {
  const auto difference = static_cast<std::int64_t>(left) - right;
  return difference < 0 ? -difference : difference;
}
template <class E>
bool contains(std::span<const E> values, std::string_view id) {
  return std::ranges::find(values, id, &E::id) != values.end();
}
std::string profile_id(ResearchUncertaintyProfile value) {
  switch (value) {
  case ResearchUncertaintyProfile::established_extension:
    return "EstablishedExtension";
  case ResearchUncertaintyProfile::frontier_engineering:
    return "FrontierEngineering";
  case ResearchUncertaintyProfile::scientific_hypothesis:
    return "ScientificHypothesis";
  case ResearchUncertaintyProfile::hazardous_foreign_or_anomalous:
    return "HazardousForeignOrAnomalous";
  }
  throw std::out_of_range("Unknown uncertainty profile.");
}
ResearchUncertaintyProfile parse_profile(std::string_view id) {
  if (id == "established_extension")
    return ResearchUncertaintyProfile::established_extension;
  if (id == "frontier_engineering")
    return ResearchUncertaintyProfile::frontier_engineering;
  if (id == "scientific_hypothesis")
    return ResearchUncertaintyProfile::scientific_hypothesis;
  if (id == "hazardous_foreign_or_anomalous")
    return ResearchUncertaintyProfile::hazardous_foreign_or_anomalous;
  fail("Unknown uncertainty profile '" + std::string(id) + "'.");
}
} // namespace

struct AdaptiveResearchOutcomeCatalog::Storage {
  const AdaptiveResearchCatalog *research;
  AdaptiveResearchOutcomeRuntimePolicy policy;
  std::vector<ResearchSideDiscoveryCandidates> side;
};
AdaptiveResearchOutcomeCatalogError::AdaptiveResearchOutcomeCatalogError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
AdaptiveResearchOutcomeJsonError::AdaptiveResearchOutcomeJsonError(
    AdaptiveResearchOutcomeJsonErrorKind kind, std::string message)
    : std::runtime_error(std::move(message)), kind_(kind) {}
AdaptiveResearchOutcomeJsonErrorKind
AdaptiveResearchOutcomeJsonError::kind() const noexcept {
  return kind_;
}
AdaptiveResearchOutcomeFileError::AdaptiveResearchOutcomeFileError(
    std::string message)
    : std::runtime_error(std::move(message)) {}
AdaptiveResearchOutcomeHistoryRangeError::
    AdaptiveResearchOutcomeHistoryRangeError(std::string message)
    : std::out_of_range(std::move(message)) {}
AdaptiveResearchOutcomeArgumentRangeError::
    AdaptiveResearchOutcomeArgumentRangeError(std::string message)
    : std::out_of_range(std::move(message)) {}
AdaptiveResearchOutcomeIndexError::AdaptiveResearchOutcomeIndexError(
    std::string message)
    : std::out_of_range(std::move(message)) {}
AdaptiveResearchOutcomeCatalog::AdaptiveResearchOutcomeCatalog(
    std::unique_ptr<Storage> s) noexcept
    : storage_(std::move(s)) {}
AdaptiveResearchOutcomeCatalog::~AdaptiveResearchOutcomeCatalog() = default;
AdaptiveResearchOutcomeCatalog::AdaptiveResearchOutcomeCatalog(
    AdaptiveResearchOutcomeCatalog &&) noexcept = default;
AdaptiveResearchOutcomeCatalog &AdaptiveResearchOutcomeCatalog::operator=(
    AdaptiveResearchOutcomeCatalog &&) noexcept = default;
const AdaptiveResearchCatalog &
AdaptiveResearchOutcomeCatalog::research_catalog() const noexcept {
  return *storage_->research;
}
const AdaptiveResearchOutcomeRuntimePolicy &
AdaptiveResearchOutcomeCatalog::policy() const noexcept {
  return storage_->policy;
}
std::span<const ResearchSideDiscoveryCandidates>
AdaptiveResearchOutcomeCatalog::side_discovery_candidates() const noexcept {
  return storage_->side;
}
std::span<const std::string>
AdaptiveResearchOutcomeCatalog::side_discovery_candidates_for(
    std::string_view id) const noexcept {
  const auto f = std::ranges::find(storage_->side, id,
                                   &ResearchSideDiscoveryCandidates::node_id);
  return f == storage_->side.end()
             ? std::span<const std::string>{}
             : std::span<const std::string>(f->candidate_node_ids);
}
ResearchUncertaintyProfile AdaptiveResearchOutcomeCatalog::get_profile(
    const AdaptiveResearchNodeDefinition &node) const noexcept {
  if (node.is_hypothesis)
    return ResearchUncertaintyProfile::scientific_hypothesis;
  if (std::ranges::find(storage_->policy.hazardous_node_ids, node.id) !=
      storage_->policy.hazardous_node_ids.end())
    return ResearchUncertaintyProfile::hazardous_foreign_or_anomalous;
  if (std::ranges::find(storage_->policy.frontier_engineering_node_ids,
                        node.id) !=
      storage_->policy.frontier_engineering_node_ids.end())
    return ResearchUncertaintyProfile::frontier_engineering;
  return ResearchUncertaintyProfile::established_extension;
}
ResearchOutcomeKind
AdaptiveResearchOutcomeCatalog::parse_outcome(std::string_view id) {
  if (id == "progress")
    return ResearchOutcomeKind::progress;
  if (id == "setback")
    return ResearchOutcomeKind::setback;
  if (id == "partial_success")
    return ResearchOutcomeKind::partial_success;
  if (id == "hypothesis_supported")
    return ResearchOutcomeKind::hypothesis_supported;
  if (id == "hypothesis_refined")
    return ResearchOutcomeKind::hypothesis_refined;
  if (id == "hypothesis_disproven")
    return ResearchOutcomeKind::hypothesis_disproven;
  if (id == "anomalous_result")
    return ResearchOutcomeKind::anomalous_result;
  if (id == "hazard_incident")
    return ResearchOutcomeKind::hazard_incident;
  if (id == "side_discovery")
    return ResearchOutcomeKind::side_discovery;
  fail("Unknown research outcome '" + std::string(id) + "'.");
}
std::string_view
AdaptiveResearchOutcomeCatalog::outcome_id(ResearchOutcomeKind v) {
  switch (v) {
  case ResearchOutcomeKind::progress:
    return "progress";
  case ResearchOutcomeKind::setback:
    return "setback";
  case ResearchOutcomeKind::partial_success:
    return "partial_success";
  case ResearchOutcomeKind::hypothesis_supported:
    return "hypothesis_supported";
  case ResearchOutcomeKind::hypothesis_refined:
    return "hypothesis_refined";
  case ResearchOutcomeKind::hypothesis_disproven:
    return "hypothesis_disproven";
  case ResearchOutcomeKind::anomalous_result:
    return "anomalous_result";
  case ResearchOutcomeKind::hazard_incident:
    return "hazard_incident";
  case ResearchOutcomeKind::side_discovery:
    return "side_discovery";
  }
  throw AdaptiveResearchOutcomeArgumentRangeError(
      "Specified argument was out of the range of valid values. (Parameter "
      "'value')");
}

AdaptiveResearchOutcomeCatalog
load_adaptive_research_outcome_catalog(const std::filesystem::path &root_path,
                                       const AdaptiveResearchCatalog &catalog) {
  const auto root = std::filesystem::absolute(root_path);
  auto storage = std::make_unique<AdaptiveResearchOutcomeCatalog::Storage>();
  storage->research = &catalog;
  try {
    const auto policy_doc =
                   load_json(root, "research_outcome_runtime_policy.json"),
               maturation = load_json(root, "maturation_model.json"),
               index = load_json(root, "index.json");
    catalog_id(policy_doc, catalog.metadata().catalog_id,
               "research_outcome_runtime_policy.json");
    catalog_id(maturation, catalog.metadata().catalog_id,
               "maturation_model.json");
    std::map<ResearchUncertaintyProfile, std::set<ResearchOutcomeKind>> allowed;
    for (const auto &[name, value] :
         require_object(maturation.at("uncertainty_profiles")).items()) {
      const auto profile = parse_profile(name);
      for (const auto &item : require_array(value.at("normal_outcomes")))
        allowed[profile].insert(AdaptiveResearchOutcomeCatalog::parse_outcome(
            item.is_null() ? std::string{} : item.get<std::string>()));
    }
    for (const auto &[name, value] :
         require_object(policy_doc.at("public_seed_weights")).items()) {
      ResearchOutcomeProfileWeights profile{parse_profile(name), {}};
      for (const auto &[outcome_name, weight_json] :
           require_object(value).items()) {
        const auto outcome =
            AdaptiveResearchOutcomeCatalog::parse_outcome(outcome_name);
        const auto weight = weight_json.get<double>();
        if (weight <= 0 || !std::isfinite(weight))
          fail("Invalid outcome weight " + detail::legacy_general(weight) +
               " for " + name + "/" + outcome_name + ".");
        if (!allowed[profile.profile].contains(outcome))
          fail("Outcome '" + outcome_name +
               "' is not allowed by maturation profile '" + name + "'.");
        profile.weights.push_back({outcome, weight});
      }
      storage->policy.base_weights.push_back(std::move(profile));
    }
    const auto &effects = policy_doc.at("effects");
    for (const auto &[name, value] :
         require_object(effects.at("outcome_competence_gain")).items()) {
      require_array(value);
      if (value.size() != 3)
        fail("Outcome competence gain '" + name +
             "' must contain three values.");
      storage->policy.competence_gains.push_back(
          {AdaptiveResearchOutcomeCatalog::parse_outcome(name),
           {value[0].get<double>(), value[1].get<double>(),
            value[2].get<double>()}});
    }
    const auto &assignment = policy_doc.at("profile_assignment");
    storage->policy.frontier_engineering_node_ids =
        strings(assignment.at("frontier_engineering_node_ids"));
    storage->policy.hazardous_node_ids =
        strings(assignment.at("hazardous_node_ids"));
    deduplicate(storage->policy.frontier_engineering_node_ids);
    deduplicate(storage->policy.hazardous_node_ids);
    for (const auto &id : storage->policy.frontier_engineering_node_ids)
      if (!catalog.find_node(id))
        fail("Outcome policy references unknown node '" + id + "'.");
    for (const auto &id : storage->policy.hazardous_node_ids)
      if (!catalog.find_node(id))
        fail("Outcome policy references unknown node '" + id + "'.");
    const auto &readiness = policy_doc.at("readiness_effects"),
               &side = policy_doc.at("side_discovery"),
               &history = policy_doc.at("history");
    auto &p = storage->policy;
    p.setback_stage_progress_loss_fraction =
        fraction(effects, "setback_stage_progress_loss_fraction");
    p.partial_success_stage_progress_credit_fraction =
        fraction(effects, "partial_success_stage_progress_credit_fraction");
    p.refined_hypothesis_stage_progress_preserved_fraction = fraction(
        effects, "refined_hypothesis_stage_progress_preserved_fraction");
    p.high_readiness_risk_reduction_max_fraction = fraction(
        readiness,
        "high_readiness_reduces_setback_and_hazard_weight_max_fraction");
    p.max_side_discovery_candidates =
        int32(side.at("max_candidates_per_resolution"));
    p.max_materialized_side_discoveries =
        int32(side.at("max_materialized_nodes_per_resolution"));
    p.minimum_shared_knowledge_fields =
        int32(side.at("minimum_shared_knowledge_fields"));
    p.same_solution_family_depth_window =
        int32(side.at("same_solution_family_graph_depth_window"));
    p.max_recent_outcome_records =
        int32(history.at("max_recent_outcome_records"));
    p.max_per_node_recent_records =
        int32(history.at("max_per_node_recent_records"));
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>>
        alternatives;
    for (const auto &node : catalog.nodes())
      alternatives[node.id];
    for (const auto &set :
         require_array(index.at("alternative_solution_sets"))) {
      require_object(set);
      std::vector<std::string> nodes;
      for (const auto &id : strings(set.at("candidate_nodes")))
        if (catalog.find_node(id))
          nodes.push_back(id);
      for (const auto &a : nodes)
        for (const auto &b : nodes)
          if (a != b)
            alternatives[a].insert(b);
    }
    for (const auto &source : catalog.nodes()) {
      ResearchSideDiscoveryCandidates result{source.id, {}};
      std::set<std::string, std::less<>> seen{source.id};
      auto add = [&](auto ids) {
        for (const auto &id : ids) {
          if (p.max_side_discovery_candidates <= 0 ||
              result.candidate_node_ids.size() >=
                  static_cast<std::size_t>(p.max_side_discovery_candidates))
            return;
          if (const auto *candidate = catalog.find_node(id);
              candidate && candidate->public_normal_research &&
              seen.insert(id).second)
            result.candidate_node_ids.push_back(id);
        }
      };
      const auto child_span = catalog.children_for(source.id);
      std::vector<std::string> children(child_span.begin(), child_span.end());
      auto order = [&](auto &ids) {
        std::stable_sort(
            ids.begin(), ids.end(), [&](const auto &a, const auto &b) {
              const auto &x = catalog.get_node(a), &y = catalog.get_node(b);
              return x.graph_depth != y.graph_depth
                         ? x.graph_depth < y.graph_depth
                         : ordinal_less(a, b);
            });
      };
      order(children);
      add(children);
      std::vector<std::string> alts(alternatives[source.id].begin(),
                                    alternatives[source.id].end());
      order(alts);
      add(alts);
      std::vector<std::string> family, fields;
      for (const auto &candidate : catalog.nodes()) {
        if (candidate.solution_family == source.solution_family &&
            depth_distance(candidate.graph_depth, source.graph_depth) <=
                p.same_solution_family_depth_window)
          family.push_back(candidate.id);
        std::set<std::string_view, std::less<>> shared;
        for (const auto &id : candidate.knowledge_fields)
          if (std::ranges::find(source.knowledge_fields, id) !=
              source.knowledge_fields.end())
            shared.insert(id);
        if (static_cast<std::int64_t>(shared.size()) >=
            p.minimum_shared_knowledge_fields)
          fields.push_back(candidate.id);
      }
      order(family);
      add(family);
      std::stable_sort(
          fields.begin(), fields.end(), [&](const auto &a, const auto &b) {
            const auto da = depth_distance(catalog.get_node(a).graph_depth,
                                           source.graph_depth),
                       db = depth_distance(catalog.get_node(b).graph_depth,
                                           source.graph_depth);
            return da != db ? da < db : ordinal_less(a, b);
          });
      add(fields);
      storage->side.push_back(std::move(result));
    }
  } catch (const AdaptiveResearchOutcomeCatalogError &) {
    throw;
  } catch (const Json::exception &e) {
    json_error(e);
  }
  return AdaptiveResearchOutcomeCatalog(std::move(storage));
}

namespace {
int checked_increment(int value, std::string_view field) {
  if (value == std::numeric_limits<int>::max())
    throw std::overflow_error("Adaptive Research outcome " +
                              std::string(field) + " overflow.");
  return value + 1;
}
std::int64_t checked_increment(std::int64_t value, std::string_view field) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw std::overflow_error("Adaptive Research outcome " +
                              std::string(field) + " overflow.");
  return value + 1;
}
double dotnet_min(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}
double dotnet_max(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}
} // namespace

struct AdaptiveResearchOutcomeState::Storage {
  std::int64_t revision{}, next_sequence{1};
  std::vector<ResearchOutcomeNodeSummary> summaries;
  std::vector<ResearchOutcomeHistoryRecord> recent;
};
AdaptiveResearchOutcomeState::AdaptiveResearchOutcomeState()
    : storage_(std::make_unique<Storage>()) {}
AdaptiveResearchOutcomeState::~AdaptiveResearchOutcomeState() = default;
AdaptiveResearchOutcomeState::AdaptiveResearchOutcomeState(
    AdaptiveResearchOutcomeState &&) noexcept = default;
AdaptiveResearchOutcomeState &AdaptiveResearchOutcomeState::operator=(
    AdaptiveResearchOutcomeState &&) noexcept = default;
AdaptiveResearchOutcomeState::AdaptiveResearchOutcomeState(
    const AdaptiveResearchOutcomeState &other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}
AdaptiveResearchOutcomeState &AdaptiveResearchOutcomeState::operator=(
    const AdaptiveResearchOutcomeState &other) {
  if (this != &other)
    storage_ = std::make_unique<Storage>(*other.storage_);
  return *this;
}
std::int64_t AdaptiveResearchOutcomeState::revision() const noexcept {
  return storage_->revision;
}
std::int64_t AdaptiveResearchOutcomeState::next_sequence() const noexcept {
  return storage_->next_sequence;
}
std::span<const ResearchOutcomeNodeSummary>
AdaptiveResearchOutcomeState::summaries() const noexcept {
  return storage_->summaries;
}
std::span<const ResearchOutcomeHistoryRecord>
AdaptiveResearchOutcomeState::recent_records() const noexcept {
  return storage_->recent;
}
ResearchOutcomeNodeSummary
AdaptiveResearchOutcomeState::get_summary(std::string_view id) const {
  const auto f = std::ranges::find(storage_->summaries, id,
                                   &ResearchOutcomeNodeSummary::node_id);
  if (f != storage_->summaries.end())
    return *f;
  return {std::string(id),
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          std::nullopt,
          -std::numeric_limits<double>::infinity()};
}

void detail::AdaptiveResearchOutcomeStateWriter::record(
    AdaptiveResearchOutcomeState &state, std::string node_id,
    std::string checkpoint_id, int attempt_index, ResearchOutcomeKind outcome,
    std::optional<std::string> side, double year, std::string explanation,
    int max_recent, int max_per_node) {
  const std::string trim_node = node_id;
  auto old = state.get_summary(node_id);
  ResearchOutcomeNodeSummary updated = old;
  if (attempt_index == std::numeric_limits<int>::max())
    throw std::overflow_error(
        "Adaptive Research outcome attempt index overflow.");
  updated.attempts = std::max(old.attempts, attempt_index + 1);
  if (outcome == ResearchOutcomeKind::setback)
    updated.setbacks = checked_increment(old.setbacks, "setback count");
  if (outcome == ResearchOutcomeKind::partial_success)
    updated.partial_successes =
        checked_increment(old.partial_successes, "partial-success count");
  if (outcome == ResearchOutcomeKind::hypothesis_refined)
    updated.refinements =
        checked_increment(old.refinements, "refinement count");
  if (outcome == ResearchOutcomeKind::hypothesis_disproven)
    updated.disproofs = checked_increment(old.disproofs, "disproof count");
  if (outcome == ResearchOutcomeKind::anomalous_result)
    updated.anomalies = checked_increment(old.anomalies, "anomaly count");
  if (outcome == ResearchOutcomeKind::hazard_incident)
    updated.hazards = checked_increment(old.hazards, "hazard count");
  if (side)
    updated.side_discoveries =
        checked_increment(old.side_discoveries, "side-discovery count");
  updated.last_outcome_id =
      std::string(AdaptiveResearchOutcomeCatalog::outcome_id(outcome));
  updated.last_outcome_year = year;
  const auto sequence = state.storage_->next_sequence;
  const auto next = checked_increment(sequence, "sequence");
  const auto revision =
      checked_increment(state.storage_->revision, "state revision");
  const auto found = std::ranges::find(state.storage_->summaries, node_id,
                                       &ResearchOutcomeNodeSummary::node_id);
  if (found == state.storage_->summaries.end())
    state.storage_->summaries.push_back(std::move(updated));
  else
    *found = std::move(updated);
  state.storage_->recent.push_back(
      {sequence, std::move(node_id), std::move(checkpoint_id), attempt_index,
       outcome, std::move(side), year, std::move(explanation)});
  state.storage_->next_sequence = next;
  if (max_recent < 0) {
    state.storage_->recent.clear();
    throw AdaptiveResearchOutcomeHistoryRangeError(
        "Index was out of range. Must be non-negative and less than the size "
        "of the collection. (Parameter 'index')");
  }
  while (static_cast<int>(state.storage_->recent.size()) > max_recent)
    state.storage_->recent.erase(state.storage_->recent.begin());
  while (static_cast<int>(std::ranges::count(
             state.storage_->recent, trim_node,
             &ResearchOutcomeHistoryRecord::node_id)) > max_per_node) {
    const auto first =
        std::ranges::find(state.storage_->recent, trim_node,
                          &ResearchOutcomeHistoryRecord::node_id);
    if (first == state.storage_->recent.end())
      break;
    state.storage_->recent.erase(first);
  }
  state.storage_->revision = revision;
}
void detail::AdaptiveResearchOutcomeStateWriter::restore_summary(
    AdaptiveResearchOutcomeState &state, ResearchOutcomeNodeSummary summary) {
  const auto revision =
      checked_increment(state.storage_->revision, "state revision");
  const auto found =
      std::ranges::find(state.storage_->summaries, summary.node_id,
                        &ResearchOutcomeNodeSummary::node_id);
  if (found == state.storage_->summaries.end())
    state.storage_->summaries.push_back(std::move(summary));
  else
    *found = std::move(summary);
  state.storage_->revision = revision;
}
void detail::AdaptiveResearchOutcomeStateWriter::restore_record(
    AdaptiveResearchOutcomeState &state, ResearchOutcomeHistoryRecord record) {
  const auto revision =
      checked_increment(state.storage_->revision, "state revision");
  const auto next = record.sequence == std::numeric_limits<std::int64_t>::max()
                        ? throw std::overflow_error(
                              "Adaptive Research outcome sequence overflow.")
                        : record.sequence + 1;
  state.storage_->recent.push_back(std::move(record));
  state.storage_->next_sequence = std::max(state.storage_->next_sequence, next);
  state.storage_->revision = revision;
}

ResearchOutcomeApplicationResult
ResearchOutcomeApplicationResult::rejected(std::string message) {
  return {false, std::nullopt, {}, {}, std::move(message)};
}

namespace {
using OutcomeWriter = detail::AdaptiveResearchOutcomeStateWriter;
using StateWriter = detail::AdaptiveResearchStateWriter;
const ResearchProjectRuntimeState *
project(const AdaptiveResearchCivilizationState &s, std::string_view id) {
  const auto f = std::ranges::find(s.active_projects(), id,
                                   &ResearchProjectRuntimeState::node_id);
  return f == s.active_projects().end() ? nullptr : &*f;
}
const ResearchNodeRuntimeState *
node_state(const AdaptiveResearchCivilizationState &s, std::string_view id) {
  const auto f = std::ranges::find(s.node_states(), id,
                                   &ResearchNodeRuntimeState::node_id);
  return f == s.node_states().end() ? nullptr : &*f;
}
bool consume_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    point = first & 7;
    length = 4;
  } else if (first >= 0x80)
    return false;
  if (value.size() < length)
    return false;
  for (std::size_t i = 1; i < length; ++i) {
    const auto c = static_cast<unsigned char>(value[i]);
    if ((c & 0xc0) != 0x80)
      return false;
    point = (point << 6) | (c & 0x3f);
  }
  value.remove_prefix(length);
  return (point >= 9 && point <= 13) || point == 0x20 || point == 0x85 ||
         point == 0xa0 || point == 0x1680 ||
         (point >= 0x2000 && point <= 0x200a) || point == 0x2028 ||
         point == 0x2029 || point == 0x202f || point == 0x205f ||
         point == 0x3000;
}
bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_whitespace(value))
      return false;
  return true;
}
} // namespace
struct AdaptiveResearchOutcomeRuntime::Storage {
  const AdaptiveResearchAuthority *authority;
  const AdaptiveResearchOutcomeCatalog *catalog;
  const AdaptiveResearchPressureRuntime *pressure{};
  mutable detail::AdaptiveResearchWeakStateTable<AdaptiveResearchOutcomeState>
      states;
  Storage(const AdaptiveResearchAuthority &a,
          const AdaptiveResearchOutcomeCatalog &c,
          const AdaptiveResearchPressureRuntime *p)
      : authority(&a), catalog(&c), pressure(p) {}
};
AdaptiveResearchOutcomeRuntime::AdaptiveResearchOutcomeRuntime(
    const AdaptiveResearchAuthority &a, const AdaptiveResearchOutcomeCatalog &c)
    : storage_(std::make_unique<Storage>(a, c, nullptr)) {}
AdaptiveResearchOutcomeRuntime::AdaptiveResearchOutcomeRuntime(
    const AdaptiveResearchAuthority &a, const AdaptiveResearchOutcomeCatalog &c,
    const AdaptiveResearchPressureRuntime &p)
    : storage_(std::make_unique<Storage>(a, c, &p)) {}
AdaptiveResearchOutcomeRuntime::~AdaptiveResearchOutcomeRuntime() = default;
AdaptiveResearchOutcomeRuntime::AdaptiveResearchOutcomeRuntime(
    AdaptiveResearchOutcomeRuntime &&) noexcept = default;
AdaptiveResearchOutcomeRuntime &AdaptiveResearchOutcomeRuntime::operator=(
    AdaptiveResearchOutcomeRuntime &&) noexcept = default;
const AdaptiveResearchOutcomeCatalog &
AdaptiveResearchOutcomeRuntime::catalog() const noexcept {
  return *storage_->catalog;
}
const AdaptiveResearchOutcomeState &AdaptiveResearchOutcomeRuntime::state(
    const AdaptiveResearchCivilizationState &s) const {
  return storage_->states.get_or_create(s);
}
AdaptiveResearchOutcomeState &detail::AdaptiveResearchOutcomeSupportAccess::state(
    const AdaptiveResearchOutcomeRuntime &runtime,
    const AdaptiveResearchCivilizationState &civilization) {
  return runtime.storage_->states.get_or_create(civilization);
}
namespace {
double deterministic(std::string key) {
  const auto hash = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(key.data()), key.size()});
  std::uint64_t raw{};
  for (int i = 0; i < 8; ++i)
    raw = (raw << 8) | hash[i];
  return static_cast<double>(raw) / 18446744073709551616.0;
}
const ResearchOutcomeProfileWeights &
weights_for(const AdaptiveResearchOutcomeRuntimePolicy &p,
            ResearchUncertaintyProfile profile) {
  const auto found = std::ranges::find(p.base_weights, profile,
                                       &ResearchOutcomeProfileWeights::profile);
  if (found == p.base_weights.end())
    throw std::out_of_range("The given key '" + profile_id(profile) +
                            "' was not present in the dictionary.");
  return *found;
}
} // namespace
PlannedResearchOutcome AdaptiveResearchOutcomeRuntime::plan_outcome(
    const AdaptiveResearchCivilizationState &s, std::string_view node_id,
    std::string_view seed, std::string_view checkpoint,
    std::optional<std::string_view> context) const {
  if (blank(seed))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'campaignSeed')");
  if (blank(checkpoint))
    throw std::invalid_argument(
        "The value cannot be an empty string or composed entirely of "
        "whitespace. (Parameter 'checkpointId')");
  const std::string owned_node(node_id), owned_checkpoint(checkpoint);
  const auto &node = storage_->authority->catalog().get_node(owned_node);
  const auto profile = storage_->catalog->get_profile(node);
  const auto attempt =
      storage_->states.get_or_create(s).get_summary(owned_node).attempts;
  std::vector<ResearchOutcomeWeightEntry> weights =
      weights_for(storage_->catalog->policy(), profile).weights;
  if (const auto *p = project(s, owned_node)) {
    const auto readiness = storage_->authority->get_project_readiness(
        s, owned_node, p->stage, p->assigned_effective_labs,
        context ? context
                : (p->target_applicability_context_id
                       ? std::optional<std::string_view>(
                             *p->target_applicability_context_id)
                       : std::nullopt));
    const auto reduction =
        readiness.overall_readiness_score / 100 *
        storage_->catalog->policy().high_readiness_risk_reduction_max_fraction;
    double removed{};
    for (const auto risky :
         {ResearchOutcomeKind::setback, ResearchOutcomeKind::hazard_incident,
          ResearchOutcomeKind::anomalous_result}) {
      const auto found = std::ranges::find(
          weights, risky, &ResearchOutcomeWeightEntry::outcome);
      if (found == weights.end())
        continue;
      const auto delta = found->weight * reduction;
      found->weight -= delta;
      removed += delta;
    }
    if (removed > 0)
      for (const auto preferred :
           {ResearchOutcomeKind::progress, ResearchOutcomeKind::partial_success,
            ResearchOutcomeKind::hypothesis_refined}) {
        const auto f = std::ranges::find(weights, preferred,
                                         &ResearchOutcomeWeightEntry::outcome);
        if (f != weights.end()) {
          f->weight += removed;
          break;
        }
      }
  }
  std::ranges::sort(weights, {}, &ResearchOutcomeWeightEntry::outcome);
  if (weights.empty())
    throw AdaptiveResearchOutcomeIndexError(
        "Index was outside the bounds of the array.");
  const auto key = storage_->authority->catalog().metadata().catalog_id + "|" +
                   std::string(seed) + "|" + s.civilization_id() + "|" +
                   owned_node + "|" + owned_checkpoint + "|" +
                   std::to_string(attempt) + "|outcome";
  const auto roll = deterministic(key);
  double total{}, cursor{};
  for (const auto &w : weights)
    total += w.weight;
  auto outcome = weights.back().outcome;
  for (const auto &w : weights) {
    cursor += w.weight;
    if (roll * total <= cursor) {
      outcome = w.outcome;
      break;
    }
  }
  std::optional<std::string> side;
  if (outcome == ResearchOutcomeKind::side_discovery) {
    const auto candidates =
        storage_->catalog->side_discovery_candidates_for(owned_node);
    if (!candidates.empty()) {
      const auto side_key =
          storage_->authority->catalog().metadata().catalog_id + "|" +
          std::string(seed) + "|" + s.civilization_id() + "|" + owned_node +
          "|" + owned_checkpoint + "|" + std::to_string(attempt) + "|side";
      auto start = static_cast<std::size_t>(
          std::floor(deterministic(side_key) * candidates.size()));
      if (start >= candidates.size())
        start = candidates.size() - 1;
      for (std::size_t offset = 0; offset < candidates.size(); ++offset) {
        const auto &id = candidates[(start + offset) % candidates.size()];
        if (!node_state(s, id)) {
          side = id;
          break;
        }
      }
    }
  }
  return {owned_node,
          owned_checkpoint,
          attempt,
          profile,
          outcome,
          roll,
          std::move(side),
          node.name + " resolved " + owned_checkpoint + " as " +
              std::string(AdaptiveResearchOutcomeCatalog::outcome_id(outcome)) +
              " under profile " + profile_id(profile) +
              "; exact deterministic roll remains an internal replay value."};
}
void detail::AdaptiveResearchOutcomeRuntimeTestAccess::maintain(
    AdaptiveResearchOutcomeRuntime &r, std::size_t limit) {
  r.storage_->states.maintain(limit);
}
std::size_t detail::AdaptiveResearchOutcomeRuntimeTestAccess::state_count(
    const AdaptiveResearchOutcomeRuntime &r) noexcept {
  return r.storage_->states.entry_count_for_testing();
}

ResearchOutcomeApplicationResult
AdaptiveResearchOutcomeRuntime::resolve_pending_hypothesis(
    AdaptiveResearchCivilizationState &s, std::string_view node_id,
    std::string_view seed, double year,
    std::optional<std::string_view> context) const {
  const std::string owned_node(node_id), owned_seed(seed);
  const auto *p = project(s, owned_node);
  if (!p || !p->paused ||
      p->pause_reason !=
          std::optional<std::string>("hypothesis_resolution_required"))
    return ResearchOutcomeApplicationResult::rejected(
        "No hypothesis is awaiting experimental resolution for that node.");
  if (!storage_->authority->catalog().get_node(owned_node).is_hypothesis)
    return ResearchOutcomeApplicationResult::rejected(
        "The pending project is not a scientific hypothesis.");
  auto copy = *p;
  auto resolution =
      plan_outcome(s, owned_node, owned_seed, "experimental_boundary",
                   context ? context
                           : (copy.target_applicability_context_id
                                  ? std::optional<std::string_view>(
                                        *copy.target_applicability_context_id)
                                  : std::nullopt));
  return apply_outcome(s, std::move(copy), std::move(resolution), year);
}
ResearchOutcomeApplicationResult
AdaptiveResearchOutcomeRuntime::resolve_active_checkpoint(
    AdaptiveResearchCivilizationState &s, std::string_view node_id,
    std::string_view seed, std::string_view checkpoint, double year) const {
  const std::string owned_node(node_id), owned_seed(seed),
      owned_checkpoint(checkpoint);
  const auto *p = project(s, owned_node);
  if (!p)
    return ResearchOutcomeApplicationResult::rejected(
        "No active project exists for that outcome checkpoint.");
  auto copy = *p;
  const auto &node = storage_->authority->catalog().get_node(owned_node);
  if (node.is_hypothesis && copy.paused &&
      copy.pause_reason ==
          std::optional<std::string>("hypothesis_resolution_required"))
    return resolve_pending_hypothesis(
        s, owned_node, owned_seed, year,
        copy.target_applicability_context_id
            ? std::optional<std::string_view>(
                  *copy.target_applicability_context_id)
            : std::nullopt);
  auto resolution =
      plan_outcome(s, owned_node, owned_seed, owned_checkpoint,
                   copy.target_applicability_context_id
                       ? std::optional<std::string_view>(
                             *copy.target_applicability_context_id)
                       : std::nullopt);
  if (resolution.outcome == ResearchOutcomeKind::hypothesis_supported ||
      resolution.outcome == ResearchOutcomeKind::hypothesis_disproven)
    return ResearchOutcomeApplicationResult::rejected(
        "A non-hypothesis checkpoint cannot resolve fundamental hypothesis "
        "truth.");
  return apply_outcome(s, std::move(copy), std::move(resolution), year);
}
ResearchOutcomeApplicationResult AdaptiveResearchOutcomeRuntime::apply_outcome(
    AdaptiveResearchCivilizationState &s, ResearchProjectRuntimeState original,
    PlannedResearchOutcome resolution, double year) const {
  std::vector<AdaptiveResearchRuntimeEvent> research_events;
  std::vector<AdaptiveResearchOutcomeEvent> events;
  const auto &node = storage_->authority->catalog().get_node(original.node_id);
  std::optional<std::string> side;
  auto set_progress = [&](double value) {
    if (const auto *current = project(s, node.id)) {
      auto updated = *current;
      updated.stage_research_points = value;
      StateWriter::set_project(s, std::move(updated));
    }
    if (const auto *current = node_state(s, node.id)) {
      auto updated = *current;
      updated.stage_research_points = value;
      StateWriter::set_node_state(s, std::move(updated));
    }
  };
  auto setback = [&] {
    if (const auto *current = project(s, node.id)) {
      const auto work = storage_->authority->progress_policy().get_stage_work(
          node, current->stage);
      set_progress(dotnet_max(
          0.0, current->stage_research_points -
                   work * storage_->catalog->policy()
                              .setback_stage_progress_loss_fraction));
    }
  };
  auto refined = [&] {
    if (const auto *current = project(s, node.id)) {
      const auto work = storage_->authority->progress_policy().get_stage_work(
          node, ResearchMaturity::experimental);
      const auto preserved =
          work * storage_->catalog->policy()
                     .refined_hypothesis_stage_progress_preserved_fraction;
      auto updated = *current;
      updated.stage = ResearchMaturity::experimental;
      updated.stage_research_points = preserved;
      updated.paused = false;
      updated.pause_reason.reset();
      const auto total = current->total_research_points;
      StateWriter::set_project(s, std::move(updated));
      StateWriter::set_node_state(s, {node.id, ResearchMaturity::experimental,
                                      std::string("refined_hypothesis"),
                                      preserved, total, 0});
    }
  };
  switch (resolution.outcome) {
  case ResearchOutcomeKind::progress:
    break;
  case ResearchOutcomeKind::setback:
    setback();
    events.push_back({AdaptiveResearchOutcomeEventType::project_setback,
                      s.civilization_id(), node.id, std::nullopt,
                      "A failed test created additional engineering work; "
                      "total scientific work remains recorded."});
    break;
  case ResearchOutcomeKind::partial_success:
    if (const auto *current = project(s, node.id)) {
      const auto work = storage_->authority->progress_policy().get_stage_work(
          node, current->stage);
      set_progress(dotnet_min(
          dotnet_max(0.0, work - 0.000001),
          current->stage_research_points +
              work * storage_->catalog->policy()
                         .partial_success_stage_progress_credit_fraction));
    }
    events.push_back({AdaptiveResearchOutcomeEventType::partial_success,
                      s.civilization_id(), node.id, std::nullopt,
                      "A limited/reduced-performance result worked and reduced "
                      "remaining work without granting Mature technology."});
    break;
  case ResearchOutcomeKind::hypothesis_supported: {
    auto result = storage_->authority->resolve_hypothesis(s, node.id, true);
    if (!result.accepted)
      return ResearchOutcomeApplicationResult::rejected(result.message);
    research_events = std::move(result.events);
    break;
  }
  case ResearchOutcomeKind::hypothesis_disproven: {
    auto result = storage_->authority->resolve_hypothesis(s, node.id, false);
    if (!result.accepted)
      return ResearchOutcomeApplicationResult::rejected(result.message);
    research_events = std::move(result.events);
    events.push_back(
        {AdaptiveResearchOutcomeEventType::hypothesis_disproven,
         s.civilization_id(), node.id, std::nullopt,
         "The tested hypothesis was disproven and archived; negative knowledge "
         "and scientific practice are retained."});
    break;
  }
  case ResearchOutcomeKind::hypothesis_refined:
    refined();
    events.push_back(
        {AdaptiveResearchOutcomeEventType::hypothesis_refined,
         s.civilization_id(), node.id, std::nullopt,
         "The hypothesis survived only in revised form; most effective "
         "experimental progress was preserved for another test cycle."});
    break;
  case ResearchOutcomeKind::anomalous_result:
    refined();
    if (storage_->pressure)
      (void)storage_->pressure->report_event_signal(
          s, "experiment_violates_current_model", 1.0,
          original.target_applicability_context_id
              ? std::optional<std::string_view>(
                    *original.target_applicability_context_id)
              : std::nullopt);
    events.push_back(
        {AdaptiveResearchOutcomeEventType::anomalous_result,
         s.civilization_id(), node.id, std::nullopt,
         "A reproducible anomaly did not validate the expected model; it "
         "created a legitimate new fundamental-science signal."});
    break;
  case ResearchOutcomeKind::hazard_incident:
    setback();
    events.push_back(
        {AdaptiveResearchOutcomeEventType::hazard_reported, s.civilization_id(),
         node.id, std::nullopt,
         "A research hazard occurred. Adaptive Research reports the fact; "
         "physical damage/casualties are resolved by the owning subsystem."});
    break;
  case ResearchOutcomeKind::side_discovery: {
    if (resolution.planned_side_discovery_node_id &&
        !node_state(s, *resolution.planned_side_discovery_node_id)) {
      const auto candidate_id = *resolution.planned_side_discovery_node_id;
      const auto &candidate =
          storage_->authority->catalog().get_node(candidate_id);
      const auto eligibility =
          storage_->authority->kernel()
              .eligibility()
              .evaluate_scientific_eligibility(
                  s, candidate_id,
                  original.target_applicability_context_id
                      ? std::optional<std::string_view>(
                            *original.target_applicability_context_id)
                      : std::nullopt);
      const auto maturity = eligibility.allowed
                                ? ResearchMaturity::investigable
                                : ResearchMaturity::hypothesized;
      StateWriter::set_node_state(
          s, {candidate_id, maturity, std::string("side_discovery"), 0, 0, 0});
      events.push_back(
          {AdaptiveResearchOutcomeEventType::side_discovery_materialized,
           s.civilization_id(), resolution.node_id, candidate_id,
           eligibility.allowed
               ? "Unexpected work made " + candidate.name +
                     " immediately Investigable because its normal "
                     "requirements were already met."
               : "Unexpected work exposed " + candidate.name +
                     " as a related hypothesis; normal requirements still "
                     "govern researchability."});
      side = candidate_id;
    }
    if (node.is_hypothesis && original.paused)
      refined();
    break;
  }
  default:
    throw AdaptiveResearchOutcomeArgumentRangeError(
        "Specified argument was out of the range of valid values.");
  }
  const auto gain = std::ranges::find(
      storage_->catalog->policy().competence_gains, resolution.outcome,
      &ResearchOutcomeCompetenceGainEntry::outcome);
  if (gain != storage_->catalog->policy().competence_gains.end() &&
      !node.knowledge_fields.empty()) {
    const auto divisor = static_cast<double>(node.knowledge_fields.size());
    for (const auto &field : node.knowledge_fields) {
      const auto current = s.expertise().get_field(field).current;
      storage_->authority->expertise_service().seed_field_competence(
          s, field,
          {dotnet_min(100.0,
                      current.theoretical + gain->gain.theoretical / divisor),
           dotnet_min(100.0,
                      current.experimental + gain->gain.experimental / divisor),
           dotnet_min(100.0,
                      current.engineering + gain->gain.engineering / divisor)},
          year);
    }
  }
  if (side)
    resolution.planned_side_discovery_node_id = side;
  OutcomeWriter::record(
      storage_->states.get_or_create(s), node.id, resolution.checkpoint_id,
      resolution.attempt_index, resolution.outcome, side, year,
      resolution.explanation,
      storage_->catalog->policy().max_recent_outcome_records,
      storage_->catalog->policy().max_per_node_recent_records);
  events.push_back({AdaptiveResearchOutcomeEventType::outcome_resolved,
                    s.civilization_id(), node.id, side,
                    "Research outcome resolved: " +
                        std::string(AdaptiveResearchOutcomeCatalog::outcome_id(
                            resolution.outcome)) +
                        "."});
  const auto message = events.back().message;
  return {true, std::move(resolution), std::move(research_events),
          std::move(events), message};
}

ResearchOutcomeApplicationResult
detail::AdaptiveResearchOutcomeRuntimeTestAccess::apply(
    const AdaptiveResearchOutcomeRuntime &runtime,
    AdaptiveResearchCivilizationState &civilization,
    ResearchProjectRuntimeState project, PlannedResearchOutcome resolution,
    double current_year) {
  return runtime.apply_outcome(civilization, std::move(project),
                               std::move(resolution), current_year);
}

} // namespace stellar::core
