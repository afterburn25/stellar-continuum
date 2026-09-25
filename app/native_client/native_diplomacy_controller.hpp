#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/diplomacy_state.hpp>

#include <cstdint>
#include <optional>
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
          std::optional<std::int64_t> proposal_id);
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
