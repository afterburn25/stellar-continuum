#pragma once
#include <stellar/core/construction_state.hpp>
#include <limits>

namespace stellar::core {
enum class ConstructionCategory { Science, Industry, Orbital, Ftl };
struct ConstructionProjectDefinition {
    std::string id, name, description;
    double industry_cost{};
    std::vector<std::string> required_technologies;
    ConstructionCategory category{};
    double credit_cost{}, industry_per_day{}, upkeep_credits_per_day{};
    std::vector<std::string> required_projects;
};
inline constexpr double construction_project_industry_per_day = 30.0;
std::span<const ConstructionProjectDefinition> construction_project_catalog();
const ConstructionProjectDefinition* find_construction_project(std::string_view id);
const ConstructionProjectDefinition& get_construction_project(std::string_view id);
std::optional<std::string> construction_project_lock_reason(ConstructionReadView world, int civilization_id,
    const ConstructionProjectDefinition& project);
std::vector<ConstructionProjectDefinition> available_construction_projects(ConstructionReadView world, int civilization_id);
std::optional<std::string> construction_queue_blocker(ConstructionReadView world, int civilization_id);
ConstructionOrderResult start_construction_project(ConstructionWorld world, int civilization_id, std::string_view project_id);
ConstructionOrderResult queue_construction_project(ConstructionWorld world, int civilization_id, std::string_view project_id);
ConstructionCancellationResult cancel_construction_project(ConstructionWorld world, int civilization_id, std::string_view project_id);
double construction_cancellation_refund_preview(const ConstructionState& state, std::string_view project_id);
double construction_industry_demand(ConstructionReadView world, int civilization_id,
    double simulation_days = std::numeric_limits<double>::infinity());
void ensure_automatic_construction_orders(ConstructionWorld world);
struct ConstructionIndustryBudget { int civilization_id{}; double industry{}; };
std::vector<ConstructionEvent> advance_construction(ConstructionWorld world,
    std::optional<std::span<const ConstructionIndustryBudget>> budgets = std::nullopt, double simulation_days = 1.0);
std::vector<ConstructionEvent> advance_construction_for_civilization(ConstructionWorld world,
    int civilization_id, double industry_budget, double simulation_days);
} // namespace stellar::core
