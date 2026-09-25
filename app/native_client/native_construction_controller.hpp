#pragma once
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/construction_projects.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_construction {
struct NativeConstructionAction {
  bool enabled{};
  bool will_start_now{};
  bool will_queue{};
  std::string message;
};
struct NativeConstructionProject {
  std::string id, name, description;
  stellar::core::ConstructionCategory category{};
  double industry_cost{}, credit_cost{}, upkeep_credits_per_day{}, industry_per_day{};
  std::string formatted_credit_cost, formatted_upkeep_rate;
  std::vector<std::string> requirements;
  bool complete{}, active{}, queued{};
  int queue_position{};
  double progress_fraction{}, industry_progress{}, industry_remaining{}, minimum_days_remaining{};
  double authorization_credits{}, cancellation_refund{};
  std::string formatted_authorization, formatted_cancellation_refund;
  std::optional<std::string> queue_blocker;
  NativeConstructionAction start, queue;
};
struct NativeConstructionView {
  std::uint64_t campaign_generation{}, construction_revision{};
  int player_civilization_id{}, home_system_id{};
  stellar::core::SovereignCurrencyDefinition currency;
  double treasury_credits{}, available_industry{};
  std::string formatted_treasury;
  std::vector<NativeConstructionProject> projects;
};
struct NativeConstructionCommandOutcome {
  bool accepted{};
  std::string message;
  double refunded_credits{};
};
class NativeConstructionController final {
public:
  [[nodiscard]] NativeConstructionView build(stellar::core::CampaignFrame &, std::uint64_t generation);
  [[nodiscard]] NativeConstructionCommandOutcome start(stellar::core::CampaignFrame &, std::uint64_t generation, std::uint64_t revision, std::string_view project_id);
  [[nodiscard]] NativeConstructionCommandOutcome queue(stellar::core::CampaignFrame &, std::uint64_t generation, std::uint64_t revision, std::string_view project_id);
  [[nodiscard]] NativeConstructionCommandOutcome cancel(stellar::core::CampaignFrame &, std::uint64_t generation, std::uint64_t revision, std::string_view project_id);
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept { locale_ = table; }
private:
  void require_owner() const;
  const stellar::engine::LocalizationTable *locale_{};
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t revision_{};
  std::optional<std::string> signature_;
  std::optional<std::string> player_species_id_;
  std::optional<NativeConstructionView> projected_view_;
};
}
