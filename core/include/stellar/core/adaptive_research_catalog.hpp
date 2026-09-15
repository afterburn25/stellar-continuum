#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stellar::core {

enum class ResearchMaturity {
  rumored = 1,
  hypothesized = 2,
  investigable = 3,
  experimental = 4,
  demonstrated = 5,
  engineering = 6,
  mature = 7,
  archived = 8,
};

enum class ResearchCapabilityScope {
  civilization,
  population_or_species,
  colony_or_installation,
};

struct ResearchNodePrerequisites {
  std::vector<std::string> all_of;
  std::vector<std::string> any_of;
};

struct ResearchCapabilityRequirements {
  std::vector<std::string> all_of;
  std::vector<std::string> any_of;
  std::string context;
};

struct ResearchApplicabilityRequirements {
  std::vector<std::string> traits;
  std::vector<std::string> evidence_types;
};

struct ResearchNumberRequirement {
  std::string id;
  double value{};
};

struct ResearchProjectRequirements {
  double base_research_points{};
  int minimum_labs{};
  int recommended_labs{};
  std::vector<ResearchNumberRequirement> required_pressure;
  std::vector<ResearchNumberRequirement> required_pressure_any;
  std::vector<std::string> required_evidence;
};

struct AdaptiveResearchNodeDefinition {
  std::string id;
  std::string name;
  std::string domain_id;
  std::string complexity;
  int graph_depth{};
  std::string solution_family;
  std::vector<std::string> knowledge_fields;
  std::vector<std::string> awareness_sources;
  std::vector<std::string> pressure_affinities;
  ResearchNodePrerequisites prerequisites;
  ResearchApplicabilityRequirements applicability;
  ResearchCapabilityRequirements capability_requirements;
  std::vector<std::string> declared_capabilities;
  bool is_hypothesis{};
  bool public_normal_research{};
  ResearchProjectRequirements project_requirements;
};

struct ResearchCapabilityDefinition {
  std::string id;
  std::string name;
  ResearchCapabilityScope scope{};
};

struct ResearchCapabilityImplication {
  std::string from_capability_id;
  std::string to_capability_id;
  bool preserve_target_context{};
};

struct ResearchLabScaling {
  double at_or_below_recommended_efficiency{};
  double above_recommended_to_twice_efficiency{};
  double above_twice_recommended_efficiency{};

  [[nodiscard]] double scale_assigned_labs(double assigned_labs,
                                           double recommended_labs) const;
};

struct DirectedResearchProgramStage {
  std::string id;
  std::optional<int> directed_program_limit;
  bool lab_capacity_only{};
  std::optional<std::string> required_technology_id;
};

struct ResearchMaturityGrant {
  std::vector<std::string> capability_ids;
  std::vector<std::string> civilization_trait_ids;
  std::optional<std::string> research_capacity_stage_id;
  std::vector<std::string> enabled_deployment_event_ids;
};

struct ResearchNodeGrant {
  std::string node_id;
  ResearchMaturityGrant grant;
};

struct ResearchDeploymentEventDefinition {
  std::string id;
  std::vector<std::string> requires_any_mature_technology_ids;
  std::vector<std::string> civilization_trait_ids;
};

struct AdaptiveResearchCatalogMetadata {
  int schema_version{};
  std::string catalog_id;
  int declared_node_count{};
  int domain_count{};
  int pressure_count{};
  int trait_count{};
  int evidence_type_count{};
  int knowledge_field_count{};
  int cross_lineage_capability_count{};
  double base_rp_per_effective_lab_per_year{};
  std::string starting_directed_program_stage_id;
};

struct AdaptiveResearchWakeIndexEntry {
  std::string key;
  std::vector<std::string> node_ids;
};

class AdaptiveResearchCatalogError : public std::runtime_error {
public:
  explicit AdaptiveResearchCatalogError(std::string message);
};

class AdaptiveResearchCatalog final {
public:
  AdaptiveResearchCatalog(const AdaptiveResearchCatalog &) = delete;
  AdaptiveResearchCatalog &operator=(const AdaptiveResearchCatalog &) = delete;
  AdaptiveResearchCatalog(AdaptiveResearchCatalog &&) noexcept;
  AdaptiveResearchCatalog &operator=(AdaptiveResearchCatalog &&) noexcept;
  ~AdaptiveResearchCatalog();

  // Borrowed references and spans remain valid until this catalog is moved,
  // move-assigned, or destroyed. A moved-from catalog may only be destroyed or
  // assigned a new loaded catalog before it is queried again.

  [[nodiscard]] const AdaptiveResearchCatalogMetadata &metadata() const noexcept;
  [[nodiscard]] std::span<const AdaptiveResearchNodeDefinition> nodes() const noexcept;
  [[nodiscard]] const AdaptiveResearchNodeDefinition *find_node(
      std::string_view node_id) const noexcept;
  [[nodiscard]] const AdaptiveResearchNodeDefinition &get_node(
      std::string_view node_id) const;

  [[nodiscard]] std::span<const ResearchCapabilityDefinition> capabilities() const noexcept;
  [[nodiscard]] const ResearchCapabilityDefinition *find_capability(
      std::string_view capability_id) const noexcept;
  [[nodiscard]] std::span<const ResearchCapabilityImplication>
  capability_implications() const noexcept;
  [[nodiscard]] std::span<const std::string> pressure_ids() const noexcept;
  [[nodiscard]] std::span<const std::string> trait_ids() const noexcept;
  [[nodiscard]] std::span<const std::string> evidence_type_ids() const noexcept;
  [[nodiscard]] std::span<const std::string> knowledge_field_ids() const noexcept;
  [[nodiscard]] bool has_pressure(std::string_view id) const noexcept;
  [[nodiscard]] bool has_trait(std::string_view id) const noexcept;
  [[nodiscard]] bool has_evidence_type(std::string_view id) const noexcept;
  [[nodiscard]] bool has_knowledge_field(std::string_view id) const noexcept;

  [[nodiscard]] const ResearchLabScaling &lab_scaling() const noexcept;
  [[nodiscard]] std::span<const DirectedResearchProgramStage>
  directed_program_stages() const noexcept;
  [[nodiscard]] const DirectedResearchProgramStage *find_directed_program_stage(
      std::string_view stage_id) const noexcept;
  [[nodiscard]] const DirectedResearchProgramStage &get_directed_program_stage(
      std::string_view stage_id) const;
  [[nodiscard]] std::span<const ResearchNodeGrant> demonstrated_grants() const noexcept;
  [[nodiscard]] std::span<const ResearchNodeGrant> mature_grants() const noexcept;
  [[nodiscard]] std::span<const ResearchDeploymentEventDefinition>
  deployment_events() const noexcept;

  [[nodiscard]] std::span<const AdaptiveResearchWakeIndexEntry>
  children_by_prerequisite() const noexcept;
  [[nodiscard]] std::span<const AdaptiveResearchWakeIndexEntry>
  nodes_by_pressure() const noexcept;
  [[nodiscard]] std::span<const AdaptiveResearchWakeIndexEntry>
  nodes_by_evidence() const noexcept;
  [[nodiscard]] std::span<const AdaptiveResearchWakeIndexEntry>
  nodes_by_trait() const noexcept;
  [[nodiscard]] std::span<const AdaptiveResearchWakeIndexEntry>
  nodes_by_capability_requirement() const noexcept;
  [[nodiscard]] std::span<const std::string> children_for(
      std::string_view prerequisite_id) const noexcept;
  [[nodiscard]] std::span<const std::string> nodes_for_pressure(
      std::string_view pressure_id) const noexcept;
  [[nodiscard]] std::span<const std::string> nodes_for_evidence(
      std::string_view evidence_id) const noexcept;
  [[nodiscard]] std::span<const std::string> nodes_for_trait(
      std::string_view trait_id) const noexcept;
  [[nodiscard]] std::span<const std::string> nodes_for_capability_requirement(
      std::string_view capability_id) const noexcept;

private:
  struct Storage;
  explicit AdaptiveResearchCatalog(Storage storage);
  std::unique_ptr<Storage> storage_;

  friend AdaptiveResearchCatalog load_adaptive_research_catalog(
      const std::filesystem::path &root_path);
};

[[nodiscard]] AdaptiveResearchCatalog load_adaptive_research_catalog(
    const std::filesystem::path &root_path);

} // namespace stellar::core
