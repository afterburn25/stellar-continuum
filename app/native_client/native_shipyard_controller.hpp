#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/fleet_role.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/shipbuilding.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace stellar::engine { class LocalizationTable; }

namespace stellar::native_shipyard {

struct NativeShipDesign {
  std::string id;
  std::string name;
  std::string description;
  stellar::core::FleetRole role{};
  double industry_cost{};
  double minimum_build_days_at_full_shipyard_rate{};
  double credit_cost{};
  double population_cost_millions{};
  double strategic_speed{};
  double maximum_leg_range_light_years{};
  double fuel_endurance_light_years{};
  float sensor_range{};
  std::string propulsion_generation;
  std::string formatted_credit_cost;
  bool can_start{};
  bool will_queue{};
  std::optional<std::string> start_blocker;
  double minimum_source_population_millions{};
  std::optional<int> population_source_colony_id;
  std::optional<std::string> population_species_id;
  std::optional<double> population_source_current_millions;
  double hull{},armor{},shields{},weapon_damage{},weapon_interval_days{},cargo_capacity{};
  int crew{};
  std::vector<stellar::core::ShipbuildingBatchAssessment> batch_quotes;
};

struct NativeShipyardOrder {
  std::string order_id;
  std::string design_id;
  std::string design_name;
  bool active{};
  double progress_fraction{};
  double industry_progress{};
  double industry_remaining{};
  double authorization_credits{};
  double reserved_population_millions{};
  std::optional<std::string> reserved_population_species_id;
  std::optional<int> reserved_population_source_colony_id;
  bool can_cancel{};
  double refund_credits{};
  std::string formatted_refund;
  std::optional<std::string> cancellation_blocker;
};

struct NativeShipyardView {
  std::uint64_t campaign_generation{};
  std::uint64_t shipyard_revision{};
  int player_civilization_id{};
  int home_system_id{};
  bool orbital_shipyard_complete{};
  stellar::core::SovereignCurrencyDefinition currency;
  double treasury_credits{};
  std::string formatted_treasury;
  double available_industry{};
  double largest_owned_colony_population_millions{};
  int pending_build_count{};
  int maximum_pending_builds{};
  std::vector<NativeShipDesign> available_designs;
  std::vector<NativeShipyardOrder> orders;
  std::string yard_name;
};

struct NativeShipyardCommandOutcome {
  bool accepted{};
  std::string message;
  double refunded_credits{};
};

class NativeShipyardController final {
public:
  NativeShipyardController() = default;

  [[nodiscard]] NativeShipyardView
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation);
  [[nodiscard]] NativeShipyardCommandOutcome
  start(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
        std::uint64_t expected_shipyard_revision,
        std::string_view design_id,int quantity=1);
  [[nodiscard]] NativeShipyardCommandOutcome reorder(stellar::core::CampaignFrame &,
      std::uint64_t campaign_generation,std::uint64_t expected_shipyard_revision,
      std::string_view order_id,int direction);
  [[nodiscard]] NativeShipyardCommandOutcome
  cancel(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
         std::uint64_t expected_shipyard_revision,
         std::string_view order_id);

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
  std::optional<std::string> projected_player_species_id_;
  std::optional<NativeShipyardView> projected_view_;
};

} // namespace stellar::native_shipyard
