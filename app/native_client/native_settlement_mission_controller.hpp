#pragma once

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/settlement_planning.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace stellar::native_colony {

enum class NativeSettlementMissionKind { Colony, ResourceOutpost };

struct NativeSettlementCandidate {
  int system_id{}, body_id{};
  std::string system_name, body_name;
  bool can_order{};
  std::string reason;
  double distance_from_fleet{};
  stellar::core::MissionReachAssessment reach;

  stellar::core::SpeciesColonizationViability viability{};
  double natural_habitability{}, unprotected_operational_capacity{};
  stellar::core::EnvironmentalLimitingFactor limiting_factor{};
  bool requires_gravity_mitigation{}, requires_thermal_control{},
      requires_pressure_control{}, requires_sealed_habitat{},
      requires_artificial_biosphere{}, requires_radiation_shielding{};
  bool system_reserved_by_friendly_colony_mission{};
  std::optional<int> reserved_by_fleet_id;

  bool has_confirmed_deposit{}, too_harsh_for_colony{};
  std::string deposit_material_name, deposit_grade;
  double deposit_accessibility{}, extraction_yield_multiplier{},
      initial_deposit_materials{};
};

struct NativeSettlementMissionView {
  std::uint64_t campaign_generation{}, revision{};
  int player_civilization_id{}, fleet_id{}, mission_order_revision{};
  NativeSettlementMissionKind kind{};
  std::string fleet_name, personnel_species_id, personnel_species_name;
  std::optional<std::string> design_id;
  std::optional<int> destination_system_id, destination_body_id,
      settlement_body_id;
  double personnel_millions{};
  double settlement_days_completed{}, establishment_days{};
  bool can_receive_orders{}, funded{};
  bool requires_new_authorization{};
  std::string status;
  double authorization_budget_units{}, treasury_budget_units{};
  std::string formatted_authorization, formatted_treasury;
  stellar::core::SovereignCurrencyDefinition currency;
  std::vector<NativeSettlementCandidate> candidates;
};

struct NativeSettlementCommandOutcome {
  bool accepted{};
  std::string message;
  int mission_order_revision{};
};
struct NativeSettlementLiveStatus {
  int fleet_id{};
  std::string status;
  std::optional<int> destination_system_id, destination_body_id,
      settlement_body_id;
  double settlement_days_completed{}, establishment_days{};
};

// Detached authorization for one manually selected, observer-admitted body.
// The target need not be present in the bounded suggestion list.
struct NativeSettlementTargetPreview {
  std::uint64_t campaign_generation{}, revision{};
  int player_civilization_id{}, fleet_id{}, mission_order_revision{};
  int destination_system_id{}, body_id{};
  NativeSettlementMissionKind kind{};
  std::string fleet_name, personnel_species_id, personnel_species_name;
  std::optional<std::string> design_id;
  double personnel_millions{};
  double authorization_budget_units{}, treasury_budget_units{};
  bool requires_new_authorization{}, funded{}, accepted{};
  std::string formatted_authorization, formatted_treasury, message;
  stellar::core::SovereignCurrencyDefinition currency;
  std::optional<NativeSettlementCandidate> candidate;
};

class NativeSettlementMissionController final {
public:
  [[nodiscard]] std::vector<NativeSettlementMissionView>
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation);
  [[nodiscard]] NativeSettlementCommandOutcome
  issue(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
        std::uint64_t revision, int fleet_id, int destination_system_id,
        int body_id);
  [[nodiscard]] NativeSettlementTargetPreview
  preview_exact(stellar::core::CampaignFrame &,
                std::uint64_t campaign_generation, int fleet_id,
                int destination_system_id, int body_id);
  [[nodiscard]] NativeSettlementCommandOutcome
  issue_exact(stellar::core::CampaignFrame &,
              std::uint64_t campaign_generation,
              std::uint64_t preview_revision);
  [[nodiscard]] std::optional<NativeSettlementLiveStatus>
  live_status(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
              int fleet_id);
  [[nodiscard]] bool is_current_generation(std::uint64_t) const noexcept;

private:
  void require_owner() const;
  void bind_generation(std::uint64_t);
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::uint64_t next_revision_{1};
  std::vector<NativeSettlementMissionView> projected_;
  std::optional<NativeSettlementTargetPreview> exact_;
};

} // namespace stellar::native_colony
