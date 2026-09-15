#pragma once
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/surface_economy.hpp>

namespace stellar::core {
struct ResourceDepositProfile {
    std::string material_name, grade;
    double grade_multiplier{}, accessibility{}, environmental_hazard{}, extraction_yield_multiplier{};
};
ResourceDepositProfile resource_deposit_profile(const PlanetaryBody& body);
double initial_deposit_reserve(const PlanetaryBody& body);
struct ResourceOutpostOperationsSnapshot {
    bool is_resource_outpost{}, has_confirmed_deposit{};
    double extraction_per_day{}, stored_materials{}, storage_capacity{}, remaining_deposit_materials{}, initial_deposit_materials{};
    std::string deposit_material_name, deposit_grade;
    double deposit_accessibility{}, extraction_yield_multiplier{};
    std::string status;
};
ResourceOutpostOperationsSnapshot resource_outpost_snapshot(std::span<const PlanetaryBody> bodies,
    std::span<const CivilizationEconomy> economies, const Colony& settlement,
    std::optional<double> operating_funding_fraction = {});
void advance_resource_outpost(std::span<const PlanetaryBody> bodies, std::span<const CivilizationEconomy> economies,
    Colony& settlement, double simulation_days, double operating_funding_fraction = 1.0);
double surface_environmental_wear(std::span<const PlanetaryBody> bodies, const Colony& colony);
void advance_surface_condition(std::span<const PlanetaryBody> bodies, Colony& colony,
    double funding_fraction, double simulation_days);
} // namespace stellar::core
