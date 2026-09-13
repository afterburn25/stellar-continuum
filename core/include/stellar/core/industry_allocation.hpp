#pragma once
#include <stellar/core/colony_economy.hpp>

namespace stellar::core {
struct IndustryPriorityWeights { double construction_weight{1.0}, shipbuilding_weight{1.0}; };
struct IndustryAllocationContext {
    int civilization_id{};
    double available_industry{}, construction_demand{}, shipbuilding_demand{};
};
struct CivilizationIndustryAllocation {
    int civilization_id{};
    double available_industry{}, construction_demand{}, shipbuilding_demand{};
    double construction_weight{}, shipbuilding_weight{}, construction_allocated{}, shipbuilding_allocated{}, total_allocated{};
};
IndustryPriorityWeights campaign_industry_weights(std::span<const CivilizationEconomy> economies,
    int civilization_id, IndustryPriorityWeights fallback = {});
CivilizationIndustryAllocation allocate_industry(const IndustryAllocationContext& context,
    IndustryPriorityWeights weights = {});
struct IndustryPriorityChangeResult { bool accepted{}; std::string message; };
IndustryPriorityChangeResult set_industry_priority(std::span<CivilizationEconomy> economies,
    int actor_civilization_id, int target_civilization_id, IndustryPriority priority);
double civilization_operating_funding(std::span<const CivilizationEconomy> economies, int civilization_id);
} // namespace stellar::core
