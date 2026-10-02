#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/integrated_adaptive_campaign.hpp>
#include <stellar/engine/history.hpp>
#include <stellar/engine/scripted_content.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

// One scripted firing input produced from a completed campaign step. `topic`
// uses the canonical chronicle category vocabulary (see
// campaign_event_history.hpp) so an anomaly event document's "on" name
// matches the category the same domain event records under.
struct ScriptedStepEvent {
  std::string topic;
  engine::ScriptFiringContext context;
};

// Maps a completed step's discrete domain events onto scripted-content
// topics and firing contexts. Order is deterministic: core event vectors in
// declaration order, then the step's research events, then the new
// diplomatic journal slice.
[[nodiscard]] std::vector<ScriptedStepEvent>
script_events_for_step(const IntegratedAdaptiveCampaignStepResult &step,
    std::span<const DiplomaticHistoryEventSnapshot> diplomacy_events = {});

// Leaf check vocabulary (each leaf: {"check": name, "scope", "args"}):
//   treasury_at_least        civ      {amount}
//   colony_count_at_least    civ      {count}
//   population_at_least      civ|colony {millions}
//   has_capability           civ      {capability}  (e.g. "tech:<id>")
//   relationship_at_least    civ      {civilization, axis?, value}
//   at_war                   civ      [{civilization}]
//   system_known             system   (observer: args.civilization or context)
//   system_fully_surveyed    system   (observer: args.civilization or context)
//
// Civilization reference arguments ("civilization", "visible_to") accept a
// concrete id or a reserved name: "player" (the campaign's player
// civilization) or "origin" (the firing context's origin — the other party
// in a contact/war event). Unresolvable references no-op/false at runtime.
//   body_has_anomaly         body
//   civilization_is_player   civ
//   elapsed_days_at_least    global   {days}
// Engine-reserved checks like "random_chance" never reach this adapter.
//
// Effect vocabulary (each effect: {"do": name, "scope", "args"}):
//   grant_credits|grant_industry|grant_science  civ    {amount}
//   modify_relationship        civ(observer) {civilization, trust?, hostility?,
//                               fear?, respect?, cooperation?, reason?,
//                               grievance?}
//   set_hostile                civ      {civilization, reason}
//   declare_war                civ      {civilization}
//   resolve_anomaly            body     (consumes the anomaly site)
//   reveal_system              system   (observer: args.civilization or context)
//   grant_capability           civ      {capability}
//   chronicle_record           any      {category, summary, significance?,
//                                        visible_to?}
//
// Effects route through authoritative services: diplomacy through
// DiplomacySimulation (which validates and journals), economy through
// CivilizationEconomy fields, knowledge through CivilizationKnowledgeState,
// research through the adaptive-research capability writer. An effect that
// is impossible at fire time (unknown id, unidentified contact) no-ops —
// load-time validation is responsible for statically knowable failures.
class CampaignScriptedContentAdapter final
    : public engine::ScriptedContentAdapter {
public:
  CampaignScriptedContentAdapter(CampaignSimulationState &world,
                                 DiplomacyState &diplomacy,
                                 AdaptiveResearchCampaignState &research,
                                 engine::EventHistory &history);
  ~CampaignScriptedContentAdapter() override = default;

  // The campaign day stamped before each scripted evaluation batch; backs
  // elapsed-day checks and diplomacy ticks.
  void set_simulation_day(double day) noexcept { day_ = day; }

  std::string validate_condition(const engine::ScriptCondition &leaf,
                                 std::string_view file,
                                 std::string_view object_id) override;
  std::string validate_effect(const engine::ScriptEffect &effect,
                              std::string_view file,
                              std::string_view object_id) override;
  std::vector<engine::ScriptFiringContext>
  enumerate_contexts(const engine::ScriptedEventDefinition &definition) override;
  bool evaluate_condition(const engine::ScriptCondition &leaf,
                          const engine::ScriptFiringContext &context) override;
  void apply_effect(const engine::ScriptEffect &effect,
                    const engine::ScriptFiringContext &context) override;
  // Records the fired event on the campaign chronicle ("scripted.<id>"),
  // visibility-scoped exactly like domain events: the involved civilization
  // plus every civilization knowing the event's system.
  void event_fired(const engine::ScriptedEventDefinition &definition,
                   const engine::ScriptFiringContext &context) override;

private:
  [[nodiscard]] std::int64_t scope_id(const engine::ScriptScope &scope,
      const engine::ScriptFiringContext &context) const;

  CampaignSimulationState *world_{};
  DiplomacyState *diplomacy_state_{};
  DiplomacySimulation diplomacy_;
  AdaptiveResearchCampaignState *research_{};
  engine::EventHistory *history_{};
  double day_{};
};

// Loads every `*.json` document directly under `root` into the campaign's
// scripted runtime. Enumeration is lexicographic by filename so document
// load order (and therefore definition registration order) is
// deterministic across platforms. Errors are aggregated across all
// documents — an empty return means the directory loaded cleanly. A
// missing or non-directory root reports one error; hosts decide whether
// that is fatal for their session.
[[nodiscard]] std::vector<engine::ScriptLoadError>
load_scripted_content_directory(
    IntegratedAdaptiveCampaignRuntime &campaign,
    const std::filesystem::path &root);

} // namespace stellar::core
