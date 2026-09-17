#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::native_research {

enum class NativeResearchIntent { None, Start, Pause, Resume, Cancel };

struct NativeResearchQuery {
  std::optional<std::string> domain_id;
  std::string search;
};

struct NativeResearchAction {
  NativeResearchIntent intent{NativeResearchIntent::None};
  bool enabled{};
  std::string reason;
};

struct NativeResearchCost {
  double assigned_effective_labs{};
  double authorization_credits{};
  double milestone_commitment_credits{};
  double operating_credits_per_day{};
  double estimated_total_credits{};
  double estimated_years_at_full_funding{};
  double credits_needed_to_start{};
  std::string formatted_authorization;
  std::string formatted_milestone_commitment;
  std::string formatted_operating_cost_rate;
  std::string formatted_estimated_total;
  std::string formatted_credits_needed_to_start;
};

struct NativeResearchCapability {
  std::string id;
  std::string display_name;
};

struct NativeResearchNode {
  std::string id;
  std::string display_name;
  std::string domain_id;
  std::string domain_label;
  std::string solution_family;
  stellar::core::ResearchMaturity maturity{};
  int graph_depth{};
  bool active{};
  bool paused{};
  double stage_progress{};
  double total_progress{};
  double assigned_effective_labs{};
  std::string readiness_band;
  std::vector<std::string> blockers;
  std::vector<NativeResearchCapability> known_capabilities;
  std::optional<NativeResearchCost> cost;
  NativeResearchAction primary_action;
  NativeResearchAction cancel_action;
  std::string purpose;
  std::string benefits;
  double research_points{};
};

struct NativeResearchDomainTab {
  std::string id;
  std::string label;
  std::size_t known_node_count{};
};

struct NativeResearchEdge {
  std::string from_id;
  std::string to_id;
  std::string relationship;
};

struct NativeResearchWindow {
  std::uint64_t campaign_generation{};
  std::int64_t research_revision{};
  std::uint64_t funding_revision{};
  stellar::core::SovereignCurrencyDefinition currency;
  std::optional<double> treasury_credits;
  std::optional<std::string> formatted_treasury;
  double free_effective_labs{};
  double total_effective_labs{};
  std::vector<NativeResearchDomainTab> domain_tabs;
  std::vector<NativeResearchNode> nodes;
  std::vector<NativeResearchEdge> edges;
  std::optional<std::string> selected_node_id;
};

struct NativeResearchCommandOutcome {
  bool accepted{};
  std::string message;
  std::int64_t research_revision{};
};

// Client-owned projection/controller. It stores only IDs and revisions; every
// view is rebuilt from the current CampaignFrame owner. A generation change
// invalidates selection, and commands reject stale generation/revision input.
class NativeResearchController final {
public:
  NativeResearchController() = default;

  [[nodiscard]] NativeResearchWindow
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
        const NativeResearchQuery &query = {});
  [[nodiscard]] NativeResearchCommandOutcome
  execute(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
          std::int64_t expected_research_revision,
          std::uint64_t expected_funding_revision,
          NativeResearchIntent intent, std::string_view node_id);

  void select(std::optional<std::string> node_id);
  [[nodiscard]] const std::optional<std::string> &selection() const;

private:
  void require_owner() const;
  void bind_generation(std::uint64_t);

  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t funding_revision_{};
  std::optional<std::string> funding_signature_;
  std::optional<std::string> selected_node_id_;
};

} // namespace stellar::native_research
