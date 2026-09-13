#pragma once
#include <stellar/core/campaign_economy.hpp>
#include <string_view>

namespace stellar::core {
struct QueuedConstructionProject { std::string project_id; double authorization_credits{}; };
struct ConstructionState {
    int civilization_id{};
    std::vector<std::string> completed_project_ids;
    std::optional<std::string> active_project_id;
    double active_project_progress{}, active_project_authorization_credits{};
    std::vector<QueuedConstructionProject> queued_projects;
};
inline constexpr int maximum_queued_construction_projects = 8;
struct CivilizationConstructionCapabilities { int civilization_id{}; std::vector<std::string> capability_ids; };
struct ConstructionReadView {
    std::span<const Civilization> civilizations;
    std::span<const PlanetaryBody> bodies;
    std::span<const ConstructionState> construction;
    std::span<const Colony> colonies;
    std::span<const CivilizationEconomy> economies;
    std::span<const CivilizationConstructionCapabilities> capabilities;
};
struct ConstructionWorld {
    std::span<const Civilization> civilizations;
    std::span<const PlanetaryBody> bodies;
    std::span<ConstructionState> construction;
    std::span<Colony> colonies;
    std::span<CivilizationEconomy> economies;
    std::span<const CivilizationConstructionCapabilities> capabilities;
    ConstructionReadView read() const { return {civilizations,bodies,construction,colonies,economies,capabilities}; }
};
bool construction_has_capability(ConstructionReadView world, int civilization_id, std::string_view id);
std::vector<ConstructionState> seed_construction(std::span<const Civilization> civilizations);
std::vector<EconomyConstructionState> economic_construction_projection(std::span<const ConstructionState> states);
struct ConstructionOrderResult { bool accepted{}; std::string message; };
struct ConstructionCancellationResult { bool accepted{}; std::string message; double refunded_credits{}; };
struct ConstructionEvent { int civilization_id{}; std::string project_id, message; };
} // namespace stellar::core
