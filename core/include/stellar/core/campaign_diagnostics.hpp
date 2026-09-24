#pragma once
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/engine/diagnostic_log.hpp>

namespace stellar::core {
// Read-only checks of actual model constraints. This does not repair state,
// simulate outcomes or assume that a lack of resources is an AI deadlock.
[[nodiscard]] std::vector<stellar::engine::DiagnosticRecord> inspect_campaign_invariants(
    const FreshCampaignState &, std::uint64_t tick, double simulation_day,
    std::size_t maximum_findings=128);
// Reports canonical operational failures separately from corrupt-state
// invariants. Read-only; an idle fleet alone is not evidence of an AI stall.
[[nodiscard]] std::vector<stellar::engine::DiagnosticRecord> inspect_campaign_operations(
    const FreshCampaignState &,std::uint64_t tick,double simulation_day,
    std::size_t maximum_findings=128);
// Inspects the persisted diplomatic state — which lives on the campaign
// runtime, outside FreshCampaignState — against its own snapshot
// invariant validator and the campaign's entity universe. Read-only.
[[nodiscard]] std::vector<stellar::engine::DiagnosticRecord> inspect_diplomacy_invariants(
    const DiplomacyState &, const FreshCampaignState &,
    std::uint64_t tick, double simulation_day,
    std::size_t maximum_findings=128);
// Inspects the adaptive research campaign state — also runtime-held —
// by capturing its authoritative snapshot codec and replaying the
// save-path restore validation, plus the campaign-entity refs the
// codec cannot check alone. Read-only.
[[nodiscard]] std::vector<stellar::engine::DiagnosticRecord> inspect_research_invariants(
    const AdaptiveResearchCampaignState &, const AdaptiveResearchStrategicRuntime &,
    const FreshCampaignState &, std::uint64_t tick, double simulation_day,
    std::size_t maximum_findings=128);
// Observer adapter of returned canonical events; does not infer fake events
// from UI state or advance any subsystem.
[[nodiscard]] std::vector<stellar::engine::DiagnosticRecord> campaign_step_diagnostics(
    const IntegratedAdaptiveCampaignStepResult &, std::uint64_t tick, double day);
}
