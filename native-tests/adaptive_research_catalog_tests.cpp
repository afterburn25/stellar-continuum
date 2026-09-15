#include <stellar/core/adaptive_research_catalog.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using Json = nlohmann::ordered_json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}

Json number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
double parse_number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("unsupported named number");
}

void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if ((actual.is_number_integer() || actual.is_number_unsigned()) &&
      (expected.is_number_integer() || expected.is_number_unsigned())) {
    require(actual == expected, field + ": integer mismatch");
    return;
  }
  if (actual.is_number() && expected.is_number()) {
    const auto left = actual.get<double>();
    const auto right = expected.get<double>();
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    require(std::isfinite(left) && std::isfinite(right) &&
                std::abs(left - right) <= 1e-12 * scale,
            field + ": number mismatch");
    return;
  }
  if (actual.is_array() && expected.is_array()) {
    require(actual.size() == expected.size(), field + ": array count");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (actual.is_object() && expected.is_object()) {
    require(actual.size() == expected.size(), field + ": object count");
    for (const auto &[key, value] : expected.items()) {
      require(actual.contains(key), field + ": missing " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  require(actual == expected, field + ": actual=" + actual.dump() +
                                  " expected=" + expected.dump());
}

Json strings(std::span<const std::string> values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back(value);
  return result;
}
Json number_requirements(std::span<const ResearchNumberRequirement> values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back({{"Id", value.id}, {"Value", number(value.value)}});
  return result;
}
Json encode_node(const AdaptiveResearchNodeDefinition &value) {
  return {
      {"Id", value.id},
      {"Name", value.name},
      {"DomainId", value.domain_id},
      {"Complexity", value.complexity},
      {"GraphDepth", value.graph_depth},
      {"SolutionFamily", value.solution_family},
      {"KnowledgeFields", strings(value.knowledge_fields)},
      {"AwarenessSources", strings(value.awareness_sources)},
      {"PressureAffinities", strings(value.pressure_affinities)},
      {"Prerequisites", {{"AllOf", strings(value.prerequisites.all_of)},
                          {"AnyOf", strings(value.prerequisites.any_of)}}},
      {"Applicability", {{"Traits", strings(value.applicability.traits)},
                          {"EvidenceTypes", strings(value.applicability.evidence_types)}}},
      {"CapabilityRequirements",
       {{"AllOf", strings(value.capability_requirements.all_of)},
        {"AnyOf", strings(value.capability_requirements.any_of)},
        {"Context", value.capability_requirements.context}}},
      {"DeclaredCapabilities", strings(value.declared_capabilities)},
      {"IsHypothesis", value.is_hypothesis},
      {"PublicNormalResearch", value.public_normal_research},
      {"ProjectRequirements",
       {{"BaseResearchPoints", number(value.project_requirements.base_research_points)},
        {"MinimumLabs", value.project_requirements.minimum_labs},
        {"RecommendedLabs", value.project_requirements.recommended_labs},
        {"RequiredPressure", number_requirements(value.project_requirements.required_pressure)},
        {"RequiredPressureAny", number_requirements(value.project_requirements.required_pressure_any)},
        {"RequiredEvidence", strings(value.project_requirements.required_evidence)}}}};
}
Json encode_stage(const DirectedResearchProgramStage &value) {
  return {{"Id", value.id},
          {"DirectedProgramLimit", value.directed_program_limit
                                       ? Json(*value.directed_program_limit)
                                       : Json(nullptr)},
          {"LabCapacityOnly", value.lab_capacity_only},
          {"RequiredTechnologyId", value.required_technology_id
                                         ? Json(*value.required_technology_id)
                                         : Json(nullptr)}};
}
Json encode_grant(const ResearchMaturityGrant &value) {
  return {{"CapabilityIds", strings(value.capability_ids)},
          {"CivilizationTraitIds", strings(value.civilization_trait_ids)},
          {"ResearchCapacityStageId", value.research_capacity_stage_id
                                          ? Json(*value.research_capacity_stage_id)
                                          : Json(nullptr)},
          {"EnabledDeploymentEventIds", strings(value.enabled_deployment_event_ids)}};
}
Json encode_index(std::span<const AdaptiveResearchWakeIndexEntry> values) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back({{"Key", value.key}, {"NodeIds", strings(value.node_ids)}});
  return result;
}
Json encode_catalog(const AdaptiveResearchCatalog &catalog) {
  const auto &metadata = catalog.metadata();
  Json nodes = Json::array();
  for (const auto &value : catalog.nodes())
    nodes.push_back(encode_node(value));
  Json capabilities = Json::array();
  for (const auto &value : catalog.capabilities())
    capabilities.push_back({{"Id", value.id}, {"Name", value.name},
                            {"Scope", static_cast<int>(value.scope)}});
  Json implications = Json::array();
  for (const auto &value : catalog.capability_implications())
    implications.push_back({{"FromCapabilityId", value.from_capability_id},
                            {"ToCapabilityId", value.to_capability_id},
                            {"PreserveTargetContext", value.preserve_target_context}});
  Json stages = Json::array();
  for (const auto &value : catalog.directed_program_stages())
    stages.push_back(encode_stage(value));
  Json demonstrated = Json::array();
  for (const auto &value : catalog.demonstrated_grants())
    demonstrated.push_back({{"NodeId", value.node_id}, {"Grant", encode_grant(value.grant)}});
  Json mature = Json::array();
  for (const auto &value : catalog.mature_grants())
    mature.push_back({{"NodeId", value.node_id}, {"Grant", encode_grant(value.grant)}});
  Json deployments = Json::array();
  for (const auto &value : catalog.deployment_events())
    deployments.push_back({{"Id", value.id},
                           {"RequiresAnyMatureTechnologyIds", strings(value.requires_any_mature_technology_ids)},
                           {"CivilizationTraitIds", strings(value.civilization_trait_ids)}});
  return {
      {"Metadata",
       {{"SchemaVersion", metadata.schema_version},
        {"CatalogId", metadata.catalog_id},
        {"DeclaredNodeCount", metadata.declared_node_count},
        {"DomainCount", metadata.domain_count},
        {"PressureCount", metadata.pressure_count},
        {"TraitCount", metadata.trait_count},
        {"EvidenceTypeCount", metadata.evidence_type_count},
        {"KnowledgeFieldCount", metadata.knowledge_field_count},
        {"CrossLineageCapabilityCount", metadata.cross_lineage_capability_count},
        {"BaseRpPerEffectiveLabPerYear", number(metadata.base_rp_per_effective_lab_per_year)},
        {"StartingDirectedProgramStageId", metadata.starting_directed_program_stage_id}}},
      {"Nodes", std::move(nodes)},
      {"Capabilities", std::move(capabilities)},
      {"CapabilityImplications", std::move(implications)},
      {"PressureIds", strings(catalog.pressure_ids())},
      {"TraitIds", strings(catalog.trait_ids())},
      {"EvidenceTypeIds", strings(catalog.evidence_type_ids())},
      {"KnowledgeFieldIds", strings(catalog.knowledge_field_ids())},
      {"LabScaling",
       {{"AtOrBelowRecommendedEfficiency", number(catalog.lab_scaling().at_or_below_recommended_efficiency)},
        {"AboveRecommendedToTwiceEfficiency", number(catalog.lab_scaling().above_recommended_to_twice_efficiency)},
        {"AboveTwiceRecommendedEfficiency", number(catalog.lab_scaling().above_twice_recommended_efficiency)}}},
      {"DirectedProgramStages", std::move(stages)},
      {"DemonstratedGrants", std::move(demonstrated)},
      {"MatureGrants", std::move(mature)},
      {"DeploymentEvents", std::move(deployments)},
      {"ChildrenByPrerequisite", encode_index(catalog.children_by_prerequisite())},
      {"NodesByPressure", encode_index(catalog.nodes_by_pressure())},
      {"NodesByEvidence", encode_index(catalog.nodes_by_evidence())},
      {"NodesByTrait", encode_index(catalog.nodes_by_trait())},
      {"NodesByCapabilityRequirement", encode_index(catalog.nodes_by_capability_requirement())}};
}

void copy_directory(const std::filesystem::path &source,
                    const std::filesystem::path &target) {
  require(!std::filesystem::exists(target), "scratch case already exists");
  require(std::filesystem::create_directory(target),
          "unable to create scratch case");
  for (const auto &entry : std::filesystem::directory_iterator(source))
    if (entry.is_regular_file())
      std::filesystem::copy_file(entry.path(), target / entry.path().filename());
}
Json &resolve(Json &root, const std::string &pointer) {
  return root[Json::json_pointer(pointer)];
}
std::filesystem::path safe_child(const std::filesystem::path &root,
                                 const std::string &relative_text) {
  const std::filesystem::path relative(relative_text);
  require(relative.is_relative(), "mutation file must be relative");
  const auto normalized = relative.lexically_normal();
  require(!normalized.empty(), "mutation file must not be empty");
  for (const auto &part : normalized)
    require(part != "..", "mutation file escapes scratch root");
  const auto result = (root / normalized).lexically_normal();
  require(result.parent_path() == root, "mutation file must be a root file");
  return result;
}
void apply_mutation(const std::filesystem::path &root, const Json &mutation) {
  const auto path = safe_child(root, mutation.at("File").get<std::string>());
  const auto kind = mutation.at("Kind").get<std::string>();
  if (kind == "DeleteFile") {
    std::filesystem::remove(path);
    return;
  }
  if (kind == "RawFile") {
    std::ofstream(path, std::ios::binary) << mutation.at("Text").get<std::string>();
    return;
  }
  std::ifstream input(path, std::ios::binary);
  auto document = Json::parse(input);
  const auto pointer = mutation.at("Pointer").get<std::string>();
  if (kind == "Set") {
    resolve(document, pointer) = Json::parse(mutation.at("Value").get<std::string>());
  } else if (kind == "Remove") {
    const auto slash = pointer.find_last_of('/');
    auto &parent = resolve(document, pointer.substr(0, slash));
    const auto key = pointer.substr(slash + 1);
    if (parent.is_array())
      parent.erase(parent.begin() + std::stoi(key));
    else
      parent.erase(key);
  } else if (kind == "CopyAppend") {
    const auto value = resolve(document, mutation.at("Value").get<std::string>());
    resolve(document, pointer).push_back(value);
  } else if (kind == "CopyProperty") {
    resolve(document, pointer) = resolve(document, mutation.at("Value").get<std::string>());
  } else {
    fail("unknown mutation kind " + kind);
  }
  std::ofstream output(path, std::ios::binary);
  output << document.dump(2);
}
std::vector<std::pair<std::string, std::string>> snapshot_files(
    const std::filesystem::path &root) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file())
      continue;
    std::ifstream input(entry.path(), std::ios::binary);
    result.emplace_back(entry.path().filename().string(),
                        std::string(std::istreambuf_iterator<char>(input), {}));
  }
  std::ranges::sort(result, {}, &std::pair<std::string, std::string>::first);
  return result;
}

struct ErrorInfo { std::string type; std::string message; };
std::string replace_root(std::string message, const std::filesystem::path &root) {
  const auto root_text = root.string();
  if (const auto found = message.find(root_text); found != std::string::npos)
    message.replace(found, root_text.size(), "<ROOT>");
  return message;
}
ErrorInfo catalog_error(const std::exception &error,
                        const std::filesystem::path &root) {
  const std::string message = error.what();
  if (message.starts_with("Required Adaptive Research file not found:"))
    return {"FileNotFoundException", replace_root(message, root)};
  if (message.starts_with("Malformed Adaptive Research JSON"))
    return {"JsonException", replace_root(message, root)};
  if (message.starts_with("Adaptive Research data directory not found:"))
    return {"DirectoryNotFoundException", replace_root(message, root)};
  return {"InvalidDataException", replace_root(message, root)};
}
Json encode_error(const std::optional<ErrorInfo> &error) {
  return error ? Json{{"Type", error->type}, {"Message", error->message}}
               : Json(nullptr);
}

void run_case(const Json &test_case, const std::filesystem::path &canonical,
              const std::filesystem::path &generated_root,
              std::size_t case_index) {
  const auto name = test_case.at("Name").get<std::string>();
  const auto kind = test_case.at("Kind").get<std::string>();
  const auto &arguments = test_case.at("Arguments");
  require(kind == "Load" || kind == "NodeLookup" || kind == "StageLookup" ||
              kind == "ScaleLabs", name + ": unknown kind");
  std::filesystem::path directory = canonical;
  if (kind == "Load" && arguments.at("Directory") == "generated-copy") {
    directory = generated_root / ("case-" + std::to_string(case_index));
    copy_directory(canonical, directory);
    for (const auto &mutation : arguments.at("Mutations"))
      apply_mutation(directory, mutation);
  }
  const auto input_before = snapshot_files(directory);

  std::optional<AdaptiveResearchCatalog> preloaded;
  std::string query_id;
  double assigned{};
  double recommended{};
  if (kind != "Load")
    preloaded.emplace(load_adaptive_research_catalog(canonical));
  if (kind == "NodeLookup") query_id = arguments.at("NodeId").get<std::string>();
  if (kind == "StageLookup") query_id = arguments.at("StageId").get<std::string>();
  if (kind == "ScaleLabs") {
    assigned = parse_number(arguments.at("AssignedLabs"));
    recommended = parse_number(arguments.at("RecommendedLabs"));
  }
  const auto expected_result = test_case.at("Result");
  const auto expected_error = test_case.at("Error");
  const auto error_comparison =
      test_case.at("ErrorComparison").get<std::string>();
  const auto expected_input_unchanged =
      test_case.at("InputUnchanged").get<bool>();
  require(error_comparison == "Exact" ||
              error_comparison == "JsonParserCategory" ||
              error_comparison == "JsonAccessCategory" ||
              error_comparison == "NumericFormatCategory",
          name + ": invalid error comparison");
  if (!expected_error.is_null()) {
    require(expected_error.is_object() && expected_error.size() == 2 &&
                expected_error.at("Type").is_string() &&
                expected_error.at("Message").is_string(),
            name + ": malformed expected error");
  }

  std::optional<AdaptiveResearchCatalog> loaded;
  const AdaptiveResearchNodeDefinition *node{};
  const DirectedResearchProgramStage *stage{};
  std::optional<double> scaled;
  std::optional<ErrorInfo> error;
  try {
    if (kind == "Load") loaded.emplace(load_adaptive_research_catalog(directory));
    else if (kind == "NodeLookup") node = &preloaded->get_node(query_id);
    else if (kind == "StageLookup") stage = &preloaded->get_directed_program_stage(query_id);
    else scaled = preloaded->lab_scaling().scale_assigned_labs(assigned, recommended);
  } catch (const std::out_of_range &caught) {
    error = {"KeyNotFoundException", caught.what()};
  } catch (const std::invalid_argument &caught) {
    error = {"ArgumentException", caught.what()};
  } catch (const std::range_error &caught) {
    error = {"FormatException", caught.what()};
  } catch (const AdaptiveResearchCatalogError &caught) {
    error = catalog_error(caught, directory);
  } catch (const std::logic_error &caught) {
    error = {"InvalidOperationException", caught.what()};
  } catch (const nlohmann::json::type_error &caught) {
    error = {"InvalidOperationException", caught.what()};
  }

  Json result = nullptr;
  if (loaded) result = encode_catalog(*loaded);
  if (node) result = encode_node(*node);
  if (stage) result = encode_stage(*stage);
  if (scaled) result = number(*scaled);
  equal_json(result, expected_result, name + ".Result");
  const auto actual_error = encode_error(error);
  auto error_matches = actual_error == expected_error;
  if (!actual_error.is_null() && !expected_error.is_null()) {
    if (error_comparison == "JsonParserCategory")
      error_matches = actual_error.at("Type") == "JsonException" &&
                      expected_error.at("Type") == "JsonReaderException" &&
                      actual_error.at("Message").get<std::string>().contains("<ROOT>") &&
                      !expected_error.at("Message").get<std::string>().empty();
    else if (error_comparison == "JsonAccessCategory")
      error_matches = actual_error.at("Type") == "InvalidOperationException" &&
                      expected_error.at("Type") == "InvalidOperationException" &&
                      actual_error.at("Message").get<std::string>().contains("wrong JSON type") &&
                      !expected_error.at("Message").get<std::string>().empty();
    else if (error_comparison == "NumericFormatCategory")
      error_matches = actual_error.at("Type") == "FormatException" &&
                      expected_error.at("Type") == "FormatException" &&
                      !actual_error.at("Message").get<std::string>().empty() &&
                      !expected_error.at("Message").get<std::string>().empty();
  }
  if (!error_matches) {
    std::cerr << name << " error actual=" << actual_error.dump()
              << " expected=" << test_case.at("Error").dump() << '\n';
    fail(name + ": error mismatch");
  }
  require(expected_input_unchanged && snapshot_files(directory) == input_before,
          name + ": loader mutated source files");
}

struct OwnedScratchDirectory {
  std::filesystem::path path;
  OwnedScratchDirectory() = default;
  explicit OwnedScratchDirectory(std::filesystem::path owned_path)
      : path(std::move(owned_path)) {}
  OwnedScratchDirectory(const OwnedScratchDirectory &) = delete;
  OwnedScratchDirectory &operator=(const OwnedScratchDirectory &) = delete;
  OwnedScratchDirectory(OwnedScratchDirectory &&other) noexcept
      : path(std::exchange(other.path, {})) {}
  OwnedScratchDirectory &operator=(OwnedScratchDirectory &&) = delete;
  ~OwnedScratchDirectory() {
    if (!path.empty()) {
      const auto normalized = path.lexically_normal();
      const auto parent = std::filesystem::temp_directory_path().lexically_normal();
      const auto leaf = normalized.filename().string();
      if (normalized.parent_path() == parent &&
          leaf.starts_with("stellar-adaptive-catalog-042-")) {
        std::error_code ignored;
        std::filesystem::remove_all(normalized, ignored);
      }
    }
  }
};

OwnedScratchDirectory create_owned_scratch() {
  const auto parent = std::filesystem::temp_directory_path();
  std::random_device random;
  for (int attempt = 0; attempt < 128; ++attempt) {
    const auto nonce = std::to_string(random()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto candidate = parent / ("stellar-adaptive-catalog-042-" + nonce);
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error))
      return OwnedScratchDirectory(std::move(candidate));
  }
  fail("unable to create unique owned scratch directory");
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "usage: adaptive_research_catalog_tests <fixture> <canonical-root>");
    std::ifstream input(argv[1], std::ios::binary);
    const auto fixture = Json::parse(input);
    require(fixture.at("Schema") == "stellar-adaptive-research-catalog-oracle-v1",
            "schema");
    const auto canonical = std::filesystem::absolute(argv[2]);
    auto scratch = create_owned_scratch();
    std::size_t case_index = 0;
    for (const auto &test_case : fixture.at("Cases"))
      run_case(test_case, canonical, scratch.path, case_index++);

    static_assert(!std::is_copy_constructible_v<AdaptiveResearchCatalog>);
    static_assert(!std::is_copy_assignable_v<AdaptiveResearchCatalog>);
    static_assert(noexcept(std::declval<const AdaptiveResearchCatalog &>()
                               .find_node(std::string_view{})));
    auto original = load_adaptive_research_catalog(canonical);
    require(original.find_node("prototype_warp_drive") != nullptr,
            "transparent node lookup");
    require(original.find_capability("interstellar_transit") != nullptr,
            "transparent capability lookup");
    require(!original.children_for("computational_science").empty() &&
                !original.nodes_for_pressure("scientific_curiosity").empty() &&
                !original.nodes_for_evidence("alien_signal").empty() &&
                !original.nodes_for_trait("metabolic_biology").empty() &&
                !original.nodes_for_capability_requirement(
                             "spacecraft_construction")
                     .empty(),
            "transparent wake-index lookup");
    auto moved = std::move(original);
    require(moved.get_node("prototype_warp_drive").id ==
                "prototype_warp_drive",
            "moved catalog query");
    original = load_adaptive_research_catalog(canonical);
    require(original.metadata().declared_node_count == 370,
            "moved-from catalog reassignment");
    for (const auto invalid_path : {std::string{}, std::string{"   "}}) {
      bool rejected = false;
      try {
        static_cast<void>(load_adaptive_research_catalog(invalid_path));
      } catch (const std::invalid_argument &caught) {
        rejected = std::string_view(caught.what()) ==
                   "Value cannot be null or whitespace. (Parameter 'rootPath')";
      }
      require(rejected, "empty/whitespace path boundary");
    }
    std::cout << "adaptive research catalog parity: "
              << fixture.at("Cases").size() << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive research catalog parity failure: " << error.what()
              << '\n';
    return 1;
  }
}
