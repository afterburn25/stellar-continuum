#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace stellar::native_economy {
enum class EconomyState { Unavailable, Ready, Failed };
struct NativeEconomyCard {
  std::string label, value;
  bool warning{};
  bool operator==(const NativeEconomyCard&) const = default;
};
struct NativeEconomyFlowRow {
  std::string label, value, suffix;
  bool income{};
  bool operator==(const NativeEconomyFlowRow&) const = default;
};
struct NativeEconomyView {
  EconomyState state{EconomyState::Unavailable};
  std::uint64_t campaign_generation{}, revision{};
  int player_civilization_id{};
  std::string message{"Economy information is unavailable."}, diagnostic;
  std::array<NativeEconomyCard, 6> cards;
  std::string treasury_status, priority_status, priority_guidance;
  bool treasury_healthy{};
  core::IndustryPriority industry_priority{core::IndustryPriority::Balanced};
  std::vector<NativeEconomyFlowRow> income_rows, cost_rows;
};

[[nodiscard]] NativeEconomyView build_economy_view(
    const core::FreshCampaignState& campaign,
    const core::AdaptiveResearchCampaignState* research,
    const std::optional<core::CivilizationIndustryAllocation>& last_allocation);

class NativeEconomyController final {
 public:
  using Projector = std::function<NativeEconomyView(
      const core::FreshCampaignState&,const core::AdaptiveResearchCampaignState*,
      const std::optional<core::CivilizationIndustryAllocation>&)>;
  NativeEconomyController();
  explicit NativeEconomyController(Projector projector);
  [[nodiscard]] bool refresh(core::CampaignFrame& frame,std::uint64_t generation,
      const std::optional<core::CivilizationIndustryAllocation>& last_allocation,
      bool explicit_retry=false);
  [[nodiscard]] core::IndustryPriorityChangeResult change_priority(
      core::CampaignFrame& frame,std::uint64_t generation,
      std::uint64_t view_revision,core::IndustryPriority priority);
  void clear();
  [[nodiscard]] const NativeEconomyView& view() const noexcept { return view_; }
  [[nodiscard]] std::uint64_t attempted_refresh_count() const noexcept { return attempted_refresh_count_; }
  [[nodiscard]] std::uint64_t successful_refresh_count() const noexcept { return successful_refresh_count_; }
 private:
  void require_owner() const;
  Projector projector_;
  NativeEconomyView view_;
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::optional<int> observer_;
  bool failure_latched_{};
  std::uint64_t attempted_refresh_count_{},successful_refresh_count_{},next_revision_{1};
};
} // namespace stellar::native_economy
