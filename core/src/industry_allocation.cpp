#include <stellar/core/industry_allocation.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
void validate_industry_value(double value, const char* name) {
    if (!std::isfinite(value) || value < 0.0)
        throw std::out_of_range(std::string(name) + " must be finite and non-negative.");
}

IndustryPriorityWeights validate_weights(IndustryPriorityWeights weights) {
    if (!std::isfinite(weights.construction_weight) || weights.construction_weight <= 0.0)
        throw std::out_of_range("Construction weight must be finite and greater than zero.");
    if (!std::isfinite(weights.shipbuilding_weight) || weights.shipbuilding_weight <= 0.0)
        throw std::out_of_range("Shipbuilding weight must be finite and greater than zero.");
    return weights;
}

IndustryPriorityWeights weights_for(IndustryPriority priority) {
    switch (priority) {
    case IndustryPriority::Balanced: return {1.0, 1.0};
    case IndustryPriority::InfrastructureFirst: return {3.0, 1.0};
    case IndustryPriority::ShipbuildingFirst: return {1.0, 3.0};
    }
    throw std::invalid_argument("Unknown persisted industry priority.");
}

bool known_priority(IndustryPriority priority) {
    return priority == IndustryPriority::Balanced || priority == IndustryPriority::InfrastructureFirst ||
        priority == IndustryPriority::ShipbuildingFirst;
}

std::string_view display_priority(IndustryPriority priority) {
    switch (priority) {
    case IndustryPriority::InfrastructureFirst: return "Infrastructure first";
    case IndustryPriority::ShipbuildingFirst: return "Shipbuilding first";
    case IndustryPriority::Balanced: return "Balanced";
    }
    return "";
}
}

IndustryPriorityWeights campaign_industry_weights(std::span<const CivilizationEconomy> economies,
    int civilization_id, IndustryPriorityWeights fallback) {
    const auto found=std::find_if(economies.begin(),economies.end(),[=](const auto& economy) {
        return economy.civilization_id == civilization_id;
    });
    if (found == economies.end() || !found->industry_priority) return fallback;
    return weights_for(*found->industry_priority);
}

CivilizationIndustryAllocation allocate_industry(const IndustryAllocationContext& context,
    IndustryPriorityWeights weights) {
    validate_industry_value(context.available_industry,"AvailableIndustry");
    validate_industry_value(context.construction_demand,"ConstructionDemand");
    validate_industry_value(context.shipbuilding_demand,"ShipbuildingDemand");
    weights=validate_weights(weights);

    double construction_allocated=0.0;
    double shipbuilding_allocated=0.0;
    const auto available=context.available_industry;
    const auto construction=context.construction_demand;
    const auto shipbuilding=context.shipbuilding_demand;
    if (available > 0.0 && (construction > 0.0 || shipbuilding > 0.0)) {
        if (available >= construction + shipbuilding) {
            construction_allocated=construction;
            shipbuilding_allocated=shipbuilding;
        } else if (construction <= 0.0) {
            shipbuilding_allocated=std::min(available,shipbuilding);
        } else if (shipbuilding <= 0.0) {
            construction_allocated=std::min(available,construction);
        } else {
            const auto total_weight=weights.construction_weight+weights.shipbuilding_weight;
            construction_allocated=std::min(construction,available*weights.construction_weight/total_weight);
            shipbuilding_allocated=std::min(shipbuilding,available*weights.shipbuilding_weight/total_weight);
            auto remaining=std::max(0.0,available-construction_allocated-shipbuilding_allocated);
            if (remaining > 0.0) {
                const auto extra=std::min(remaining,std::max(0.0,construction-construction_allocated));
                construction_allocated+=extra;
                remaining-=extra;
            }
            if (remaining > 0.0)
                shipbuilding_allocated+=std::min(remaining,std::max(0.0,shipbuilding-shipbuilding_allocated));
        }
    }
    return {context.civilization_id,available,construction,shipbuilding,weights.construction_weight,weights.shipbuilding_weight,
        construction_allocated,shipbuilding_allocated,construction_allocated+shipbuilding_allocated};
}

IndustryPriorityChangeResult set_industry_priority(std::span<CivilizationEconomy> economies,
    int actor_civilization_id, int target_civilization_id, IndustryPriority priority) {
    if (!known_priority(priority)) return {false,"Unknown industry priority."};
    if (actor_civilization_id != target_civilization_id) return {false,"Only the owning civilization can set its industry priority."};
    const auto found=std::find_if(economies.begin(),economies.end(),[=](const auto& economy) {
        return economy.civilization_id == target_civilization_id;
    });
    if (found == economies.end()) return {false,"Unknown civilization economy."};
    found->industry_priority=priority;
    return {true,"Industry priority set to "+std::string(display_priority(priority))+"."};
}

double civilization_operating_funding(std::span<const CivilizationEconomy> economies, int civilization_id) {
    const auto found=std::find_if(economies.begin(),economies.end(),[=](const auto& economy) {
        return economy.civilization_id == civilization_id;
    });
    const auto value=found == economies.end() ? 1.0 : found->last_base_operations_funding_fraction;
    return std::isfinite(value) ? std::clamp(value,0.0,1.0) : 0.0;
}
} // namespace stellar::core
