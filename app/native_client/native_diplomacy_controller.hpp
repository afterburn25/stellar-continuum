#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/diplomacy_state.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_diplomacy {

// Observer-safe presentation rows shaped from DiplomaticStateView. The
// controller never exposes authoritative DiplomacyState; unidentified contacts
// keep no civilization id, name, species or relationship readings.
struct NativeDiplomacyContact {
  std::string contact_id;
  std::string display_name;
  std::string status;
  std::string communication;
  // Raw relationship state for filtering; `status` is localized display text.
  std::optional<stellar::core::DiplomaticPoliticalState> political_state;
  double confidence{};
  bool identified{};
  bool communication_available{};
  std::optional<int> civilization_id;
  std::size_t source_index{};
  std::optional<int> last_observed_system_id;
  std::string last_observed_system_name;
  std::optional<double> cooperation;
  int pending_proposal_count{};
  std::optional<std::string> species_name;
};

struct NativeDiplomacyProposalRow {
  std::int64_t proposal_id{};
  std::string direction;
  std::string kind;
  std::string agreement_type;
  std::string summary;
  bool can_accept{}, can_reject{}, can_withdraw{};
};

struct NativeDiplomacyAgreementRow {
  std::int64_t agreement_id{};
  std::string type;
  std::string status;
  // Raw agreement state for filtering; `status` is localized display text.
  stellar::core::DiplomaticAgreementStatus agreement_status{};
  std::string started;
  std::string ended;
};

struct NativeDiplomacyHistoryRow {
  std::int64_t event_id{};
  std::string date;
  std::string kind;
  std::string summary;
};

// Observer-scoped war ledger rows: only wars the observer legitimately sees —
// as a belligerent or after identifying both sides — are projected.
struct NativeDiplomacyWarGoalRow {
  stellar::core::WarGoalKind kind{};
  std::string label;
  std::optional<int> system_id;
  bool achieved{};
};
struct NativeDiplomacyWarRow {
  std::int64_t war_id{};
  std::optional<int> aggressor_civilization_id;
  std::optional<int> defender_civilization_id;
  std::string aggressor_name;
  std::string defender_name;
  // The counterpart relative to the observer; empty for foreign-vs-foreign wars.
  std::optional<int> counterpart_id;
  std::string counterpart_name;
  bool observer_is_belligerent{};
  bool observer_is_aggressor{};
  // [-1, 1]; positive favors the observer when they are a belligerent,
  // otherwise favors the aggressor.
  double score{};
  double observer_exhaustion{};
  double counterpart_exhaustion{};
  std::string declared;
  std::optional<std::string> resolved;
  std::string outcome;
  std::vector<NativeDiplomacyWarGoalRow> goals;
};

// A war-goal choice the observer may attach to a declaration; the authoritative
// command revalidates it — this list only shapes the picker.
struct NativeDiplomacyWarGoalOption {
  stellar::core::WarGoalKind kind{};
  std::optional<int> system_id;
  std::string label;
};

// Empire-policy rows for the player's own empire: the catalog is public
// knowledge, the active assignment is authoritative state.
struct NativeDiplomacyPolicyRow {
  int domain_index{};
  std::string domain_label;
  std::string policy_id;
  std::string display_name;
  std::string summary;
  std::string effects;
  bool active{};
};

struct NativeDiplomacySelected {
  bool present{};
  std::optional<int> target_civilization_id;
  std::size_t contact_index{};
  std::string contact_name;
  std::string contact_status;
  std::string communication_status;
  std::string political_status;
  bool has_visible_communication{};
  std::optional<double> trust, hostility, fear, respect, cooperation;
  std::string access_summary;
  std::string agreements_summary;
  std::string proposal_summary;
  std::vector<std::string> recent_events;
  std::optional<std::string> species_id;
  std::string our_access;
  std::string their_access;
  bool can_attempt_communication{};
  bool can_offer_non_aggression{};
  bool can_request_access{};
  bool can_offer_peace{};
  bool can_offer_ceasefire{};
  bool can_set_access{};
  bool can_declare_war{};
  // Per-action blocker reasons from the availability projection (plus the
  // workspace's finer per-term gating); `none` falls back to the status
  // summaries for contacts without an availability row.
  stellar::core::DiplomacyActionBlocker communication_blocker{};
  stellar::core::DiplomacyActionBlocker negotiate_blocker{};
  stellar::core::DiplomacyActionBlocker offer_non_aggression_blocker{};
  stellar::core::DiplomacyActionBlocker request_access_blocker{};
  stellar::core::DiplomacyActionBlocker offer_peace_blocker{};
  stellar::core::DiplomacyActionBlocker offer_ceasefire_blocker{};
  stellar::core::DiplomacyActionBlocker set_access_blocker{};
  stellar::core::DiplomacyActionBlocker declare_war_blocker{};
  // Goals the declaration picker may offer; humiliate/resist handling stays
  // authoritative — conquer rows appear only for systems with an active
  // observer claim.
  std::vector<NativeDiplomacyWarGoalOption> war_goal_options;
  // One-line status when a visible war exists with the selected counterpart.
  std::string war_summary;
};

struct NativeDiplomacyView {
  std::uint64_t campaign_generation{};
  std::uint64_t diplomacy_revision{};
  int observer_civilization_id{};
  std::string date;
  std::vector<NativeDiplomacyContact> contacts;
  NativeDiplomacySelected selected;
  std::vector<NativeDiplomacyProposalRow> proposals;
  std::vector<NativeDiplomacyAgreementRow> agreements;
  std::vector<NativeDiplomacyHistoryRow> history;
  std::vector<NativeDiplomacyWarRow> wars;
  std::vector<NativeDiplomacyPolicyRow> policies;
};

enum class NativeDiplomacyContactFilter {
  all,
  identified,
  unidentified,
  cooperative,
  neutral,
  hostile,
  at_war,
  pending_proposal,
  communication_available,
};

enum class DiplomacyWorkspaceAction {
  establish_communication,
  propose_non_aggression,
  request_access,
  offer_peace,
  offer_ceasefire,
  grant_access,
  deny_access,
  declare_war,
  accept_proposal,
  reject_proposal,
  withdraw_proposal,
  set_empire_policy,
};

struct NativeDiplomacyCommandOutcome {
  bool accepted{};
  std::string message;
};

[[nodiscard]] std::vector<const NativeDiplomacyContact *>
filter_native_diplomacy_contacts(const NativeDiplomacyView &view,
                                 NativeDiplomacyContactFilter filter);

// Mirrors DiplomacyRelationsPresenter/ObserverDiplomacyCommandService over the
// observer-safe view. Command execution revalidates the exact quoted revision
// and campaign generation before any authoritative mutation.
class NativeDiplomacyController final {
public:
  [[nodiscard]] NativeDiplomacyView
  build(stellar::core::CampaignFrame &frame, std::uint64_t generation,
        std::size_t contact_index);
  [[nodiscard]] NativeDiplomacyCommandOutcome
  execute(stellar::core::CampaignFrame &frame, std::uint64_t generation,
          std::uint64_t revision, DiplomacyWorkspaceAction action,
          std::optional<int> target_civilization_id,
          std::optional<std::int64_t> proposal_id,
          std::span<const stellar::core::WarGoalSpec> war_goals = {},
          std::string_view policy_id = {});
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

private:
  void require_owner() const;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t revision_{};
  std::optional<std::string> signature_;
};

} // namespace stellar::native_diplomacy
