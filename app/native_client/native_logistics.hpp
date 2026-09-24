#pragma once

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/logistics.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_logistics {

enum class LoadState { Unavailable, Ready, Failed };

struct NodeRow {
  int id{};
  std::string name;
  std::string kind_label;
  std::string status;
  double supply_per_day{};
  double demand_per_day{};
  double delivered_per_day{};
  [[nodiscard]] bool operator==(const NodeRow &) const = default;
};

// A detached, read-only presentation projection of one civilization's home
// system. Totals are copied verbatim from Core's canonical network result.
struct View {
  LoadState state{LoadState::Unavailable};
  std::string system_name{"SUPPLY NETWORK"};
  std::string message{"Supply network is unavailable."};
  std::string diagnostic;
  int corridor_count{};
  double supply_per_day{};
  double demand_per_day{};
  double delivered_per_day{};
  double shortfall_per_day{};
  std::vector<NodeRow> nodes;
};

[[nodiscard]] View build_home_logistics(
    const stellar::core::FreshCampaignState &campaign, int civilization_id,
    const stellar::engine::LocalizationTable *locale = nullptr);

class HomeLogisticsController final {
 public:
  using Projector = std::function<stellar::core::HomeSystemLogisticsNetwork(
      const stellar::core::FreshCampaignState &, int)>;

  HomeLogisticsController();
  explicit HomeLogisticsController(Projector projector);

  // True only when a canonical home-system logistics projection was invoked.
  // Repeating a failed identity/generation is deliberately a no-op unless the
  // caller explicitly requests a retry.
  [[nodiscard]] bool refresh(const stellar::core::FreshCampaignState &campaign,
                             int civilization_id, std::uint64_t generation,
                             bool explicit_retry = false);
  void clear() noexcept;
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

  [[nodiscard]] const View &view() const noexcept { return view_; }
  [[nodiscard]] std::uint64_t attempted_refresh_count() const noexcept {
    return attempted_refresh_count_;
  }
  [[nodiscard]] std::uint64_t successful_refresh_count() const noexcept {
    return successful_refresh_count_;
  }

 private:
  Projector projector_;
  const stellar::engine::LocalizationTable *locale_{};
  View view_;
  std::optional<int> civilization_id_;
  std::optional<std::uint64_t> generation_;
  bool failure_latched_{};
  std::uint64_t attempted_refresh_count_{};
  std::uint64_t successful_refresh_count_{};
};

}  // namespace stellar::native_logistics
