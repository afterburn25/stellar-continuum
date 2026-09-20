#pragma once

#include "native_system_view.hpp"
#include "native_planet_lighting.hpp"

#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/colony_biology.hpp>
#include <stellar/core/colony_operations.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/surface_economy.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace stellar::native_colony {

struct NativeSurfaceSite {
  int building_id{};
  std::string type_id, name;
  float x{}, z{}, rotation_degrees{};
  double industry_progress{}, industry_cost{}, progress_fraction{};
  bool complete{}, powered{}, staffed{}, enabled{}, prioritized{};
  double condition{}, efficiency{}, stored_power_days{};
  std::string construction_stage;
  double construction_stage_progress{}, remaining_construction_materials{};
  std::optional<std::string> pending_upgrade_type_id;
  double upgrade_days_remaining{};
  // Eligibility and live cost information for the canonical upgrade/repair orders.
  bool can_upgrade{};
  std::string upgrade_name;
  double upgrade_credit_budget_units{}, upgrade_industry_cost{};
  bool can_afford_upgrade{};
  std::string upgrade_lock_reason;
  double repair_industry_cost{};
  bool can_afford_repair{};
  bool essential_service{};
  int slot_index{};
};

struct NativeSurfaceBuildOption {
  std::string type_id, name, description;
  double industry_cost{}, authorization_budget_units{};
  std::string formatted_authorization;
  float footprint_radius{};
  double power_supply{}, power_demand{}, workforce_required_millions{};
};

struct NativeColonyView {
  double simulation_days{};
  std::optional<stellar::core::StellarPhysicalProperties> illumination_star;
  float illumination_x{},illumination_y{};
  std::optional<stellar::native_planets::Lighting> stellar_lighting;
  double rotation_parent_bearing{};
  std::uint64_t campaign_generation{}, revision{};
  int player_civilization_id{}, system_id{}, body_id{}, colony_id{};
  std::string colony_name, body_display_name, population_species_id;
  bool resource_outpost{}, solid_surface{}, homeworld{};
  stellar::core::SovereignCurrencyDefinition currency;
  double treasury_budget_units{}, stored_industry{};
  std::string formatted_treasury;

  double population_millions{}, infrastructure{}, stability{};
  double working_age_population_millions{}, employed_population_millions{},
      unemployed_population_millions{}, employment_rate{};
  double workforce_available_millions{}, workforce_demand_millions{};
  double food_capacity_millions{}, water_capacity_millions{},
      housing_capacity_millions{}, supported_population_millions{},
      sustenance_support_ratio{};
  std::string limiting_sustenance_supply;
  double food_reserve_days{}, water_reserve_days{};

  int required_habitat_systems{}, building_capacity{}, surface_hub_level{};
  std::string hub_name;
  bool hub_upgrade_available{}, can_afford_hub_upgrade{};
  double hub_upgrade_credit_budget_units{}, hub_upgrade_industry_cost{};
  std::string hub_upgrade_lock_reason;
  double hub_upgrade_days_remaining{};
  double habitat_support_reduction{}, environmental_wear_multiplier{};
  double power_supply{}, power_demand{}, stored_power_days{},
      power_storage_capacity_days{}, storage_charge_per_day{},
      storage_discharge_per_day{};
  double credits_per_day{}, upkeep_credits_per_day{}, industry_per_day{},
      science_per_day{}, cargo_transfer_capacity_per_day{};
  int active_research_facilities{};
  double active_research_lab_units{};
  std::string specialization_name, specialization_description;
  int specialization_complexes{};
  bool specialization_active{};

  bool has_confirmed_deposit{};
  double extraction_per_day{}, stored_extracted_materials{},
      extracted_material_capacity{}, remaining_deposit_materials{},
      initial_deposit_materials{}, deposit_accessibility{},
      extraction_yield_multiplier{};
  std::string deposit_material_name, deposit_grade, outpost_status;

  std::string system_name, population_species_name;
  std::string owner_name;
  bool observer_only{};
  // Full developer inspection of another empire is independent of ownership.
  bool developer_inspection{}, foreign_settlement{};
  int owner_civilization_id{};
  std::optional<double> natural_habitability;
  stellar::native_system::NativeSystemBody planet;
  stellar::core::CreditFlowSnapshot local_credit_flow;
  double operating_funding{}, operating_arrears{}, empire_credit_flow{}, empire_industry_flow{}, construction_multiplier{};
  std::vector<NativeSurfaceSite> construction_sites;
  std::vector<NativeSurfaceBuildOption> available_buildings;
};

struct NativeColonyViewResult {
  std::optional<NativeColonyView> view;
  std::string denial;
};

class NativeColonyController final {
public:
  [[nodiscard]] NativeColonyViewResult
  build(stellar::core::CampaignFrame &, std::uint64_t campaign_generation,
        const stellar::native_system::NativeSystemSnapshot &,
        int selected_body_id);
  [[nodiscard]] bool is_current_generation(std::uint64_t) const noexcept;

private:
  void require_owner() const;
  void bind_generation(std::uint64_t);
  std::thread::id owner_{std::this_thread::get_id()};
  std::optional<std::uint64_t> generation_;
  std::string last_signature_;
  std::uint64_t revision_{};
};

} // namespace stellar::native_colony
