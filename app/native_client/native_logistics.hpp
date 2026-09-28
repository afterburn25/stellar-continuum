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

// One canonical freight link between two observer-visible nodes. `used_per_day`
// is the sum of authoritative flow allocations routed over the link.
struct LinkRow {
  int id{};
  std::string from;
  std::string to;
  std::string status;
  double capacity_per_day{};
  double used_per_day{};
  double transit_days{};
  bool enabled{true};
  bool bidirectional{true};
  [[nodiscard]] bool operator==(const LinkRow &) const = default;
};

// One owned system outside the home system. Metrics are copied verbatim from
// the canonical `ExternalSystemLogisticsStatus`; `condition` is the worst
// per-colony `SupplyCondition` in that system.
struct ExternalRow {
  int system_id{};
  std::string name;
  std::string status;
  stellar::core::SupplyCondition condition{};
  int colony_count{};
  double capacity_per_day{};
  double demand_per_day{};
  double import_per_day{};
  bool corridor{};
  [[nodiscard]] bool operator==(const ExternalRow &) const = default;
};

// A detached, read-only presentation projection of one civilization's home
// system plus its owned external systems. Totals are copied verbatim from
// Core's canonical coverage result.
struct View {
  LoadState state{LoadState::Unavailable};
  std::string system_name{"SUPPLY NETWORK"};
  std::string message{"Supply network is unavailable."};
  std::string diagnostic;
  int corridor_count{};
  int owned_system_count{1};
  double supply_per_day{};
  double demand_per_day{};
  double delivered_per_day{};
  double shortfall_per_day{};
  // Interstellar demand with no represented freight corridor — Core's
  // `unrepresented_interstellar_support_per_day`, surfaced so players can see
  // colonies the home network cannot reach.
  double support_gap_per_day{};
  std::vector<NodeRow> nodes;
  std::vector<LinkRow> links;
  std::vector<ExternalRow> external;
};

[[nodiscard]] View build_home_logistics(
    const stellar::core::FreshCampaignState &campaign, int civilization_id,
    const stellar::engine::LocalizationTable *locale = nullptr);

class HomeLogisticsController final {
 public:
  using Projector = std::function<stellar::core::CivilizationLogisticsCoverage(
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
