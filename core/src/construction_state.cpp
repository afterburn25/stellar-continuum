#include <stellar/core/construction_state.hpp>
#include <stellar/core/construction_projects.hpp>

#include <algorithm>

namespace stellar::core {
bool construction_has_capability(ConstructionReadView world, int civilization_id, std::string_view id) {
    if (world.capability_query) return world.capability_query(civilization_id, id);
    // The supplied view is already the resolved C# IConstructionCapabilityView
    // answer.  Select the first matching civilization entry, preserving its
    // explicit set and ordinal, case-sensitive project/capability identifiers.
    const auto state = std::find_if(world.capabilities.begin(), world.capabilities.end(), [=](const auto& capability) {
        return capability.civilization_id == civilization_id;
    });
    if (state == world.capabilities.end()) return false;
    return std::find(state->capability_ids.begin(), state->capability_ids.end(), id) != state->capability_ids.end();
}

std::vector<ConstructionState> seed_construction(std::span<const Civilization> civilizations) {
    std::vector<ConstructionState> result;
    result.reserve(civilizations.size());
    for (const auto& civilization : civilizations) {
        ConstructionState state;
        state.civilization_id = civilization.id;
        if (civilization.is_seeded_ancient) {
            for (const auto& project : construction_project_catalog()) state.completed_project_ids.push_back(project.id);
        }
        result.push_back(std::move(state));
    }
    return result;
}

std::vector<EconomyConstructionState> economic_construction_projection(std::span<const ConstructionState> states) {
    std::vector<EconomyConstructionState> result;
    result.reserve(states.size());
    for (const auto& state : states) {
        EconomyConstructionState projection;
        projection.civilization_id = state.civilization_id;
        for (const auto& id : state.completed_project_ids) {
            if (std::find(projection.completed_project_ids.begin(), projection.completed_project_ids.end(), id) == projection.completed_project_ids.end())
                projection.completed_project_ids.push_back(id);
        }
        result.push_back(std::move(projection));
    }
    return result;
}
} // namespace stellar::core
