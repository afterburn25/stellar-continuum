#pragma once

#include <stellar/core/player_campaign_json.hpp>

namespace stellar::core {

struct DeveloperResearchOptions {
  bool complete_normal_research{};
  bool complete_special_research{};
};

struct DeveloperResearchSetupResult {
  int completed_normal{};
  int completed_special{};
  std::vector<AdaptiveResearchRuntimeEvent> events;
};
void validate_developer_simulation_state(const DeveloperSimulationState &);
void set_developer_ai_control(IntegratedAdaptiveCampaignRuntime &, bool enabled);
void validate_developer_coverage(const FreshCampaignState &);
// Reveals the existing galaxy to the player through authoritative knowledge.
// Requires a developer campaign; leaves other observers and generated content alone.
void fully_explore_developer_galaxy(FreshCampaignState &);
// Full exploration changes visibility, never diplomatic ownership or treaties.
[[nodiscard]] std::vector<TerritorialClaimSnapshot> campaign_territorial_claims(
    const IntegratedAdaptiveCampaignRuntime &, int observer);
struct DeveloperEmpireSummary {
  int civilization_id{},home_system_id{};
  std::string name;
  CivilizationDevelopmentStage stage{};
  bool player{},expansion_allowed{};
  int colonies{},outposts{},fleets{},active_claims{},established_research{},active_projects{},buildings_in_progress{};
  double population_millions{},credits{},industry{},research_spending_per_day{},research_funding_fraction{};
};
// Read-only live projections, restricted to isolated developer campaigns.
[[nodiscard]] std::vector<DeveloperEmpireSummary> developer_empire_summaries(
    const IntegratedAdaptiveCampaignRuntime &);
[[nodiscard]] AdaptiveResearchView developer_empire_research(
    const IntegratedAdaptiveCampaignRuntime &, int civilization_id);
void set_developer_central_black_hole_state(IntegratedAdaptiveCampaignRuntime &,CentralBlackHoleState);

// Apply only to an explicitly marked developer campaign. With both switches
// off, its ordinary species starting research state remains unchanged.
[[nodiscard]] DeveloperResearchSetupResult initialize_developer_research(
    IntegratedAdaptiveCampaignRuntime &campaign, DeveloperResearchOptions options);

// Same authoritative world/research/diplomacy codecs, distinct tagged envelope.
// Player readers reject both the envelope and its tagged nested payload.
struct DeveloperCampaignPayload {
  PlayerCampaignPayloadV17Dto campaign;
  CampaignDeveloperProvenance provenance;
  std::optional<CampaignRuntimeContinuation> continuation;
};
[[nodiscard]] DeveloperCampaignPayload capture_developer_campaign(
    IntegratedAdaptiveCampaignRuntime &, const PlayerCampaignCaptureOptions &);
[[nodiscard]] std::string encode_developer_campaign_json(
    const DeveloperCampaignPayload &);
[[nodiscard]] std::string capture_developer_campaign_json(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const PlayerCampaignCaptureOptions &options);
[[nodiscard]] RestoredPlayerCampaignV17 restore_developer_campaign_json(
    AdaptiveResearchStrategicRuntime research_runtime,std::string_view json,
    const PlayerCampaignJsonRestoreHooks &hooks = {});

} // namespace stellar::core
