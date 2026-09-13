#include <stellar/core/civilization_catalog.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
std::string lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}

void rename_homes_and_ensure_unique(std::vector<StellarSystem>& systems, std::span<const Civilization> civilizations) {
    std::unordered_map<int, std::size_t> by_id;
    for (std::size_t index = 0; index < systems.size(); ++index) by_id.emplace(systems[index].id, index);
    for (const Civilization& civilization : civilizations) {
        if (civilization.is_player) continue;
        const auto found = by_id.find(civilization.home_system_id);
        if (found == by_id.end()) throw std::invalid_argument{"Civilization home references an unknown system."};
        StellarSystem& home = systems[found->second];
        if (home.stellar_catalog_id) continue;
        const auto space = civilization.name.find(' ');
        home.name = civilization.name.substr(0, space);
    }
    std::unordered_set<std::string> used;
    for (StellarSystem& system : systems) {
        const std::string original = system.name;
        std::string candidate = original;
        int sequence = 2;
        while (!used.insert(lower(candidate)).second) candidate = original + " " + std::to_string(sequence++);
        system.name = std::move(candidate);
    }
}

bool nearby_placement_failure(const std::exception& error) {
    constexpr std::string_view prefix = "Could not place nearby viable worlds";
    return std::string_view(error.what()).starts_with(prefix);
}
} // namespace

FoundingCatalog create_founding_catalog(std::int64_t seed, std::span<const StellarSystem> physical_systems,
    int pre_warp_count, int ancient_count, const std::string& player_species_id) {
    FoundingCatalog result;
    result.systems.assign(physical_systems.begin(), physical_systems.end());
    const auto original_systems = result.systems;
    result.bodies = generate_planetary_catalog(seed, result.systems);
    result.civilizations = seed_civilizations(result.systems, result.bodies, pre_warp_count, ancient_count, seed, player_species_id);
    rename_homes_and_ensure_unique(result.systems, result.civilizations);
    result.bodies = generate_planetary_catalog(seed, result.systems);
    try {
        result.bodies = apply_nearby_habitable_guarantees(seed, result.systems, result.bodies, result.civilizations, 2);
    } catch (const std::exception& error) {
        if (!nearby_placement_failure(error)) throw;
        result.used_constrained_home_fallback = true;
        result.systems = original_systems;
        result.bodies = generate_planetary_catalog(seed, result.systems);
        std::vector<std::string> species_ids;
        species_ids.reserve(result.civilizations.size());
        std::vector<const Civilization*> ordered_civilizations;
        ordered_civilizations.reserve(result.civilizations.size());
        for (const Civilization& civilization : result.civilizations) ordered_civilizations.push_back(&civilization);
        std::sort(ordered_civilizations.begin(), ordered_civilizations.end(), [](const auto* left, const auto* right) {
            return left->id < right->id;
        });
        for (const Civilization* civilization : ordered_civilizations) species_ids.push_back(civilization->species_id);
        std::vector<SpeciesHomeworldAssignment> homes;
        try {
            homes = plan_species_homeworlds_with_nearby_expansion(result.systems, result.bodies, species_ids, pre_warp_count);
        } catch (const std::exception& constrained_error) {
            throw std::runtime_error{"Nearby-world home fallback could not satisfy fresh seed " + std::to_string(seed) + ": " + constrained_error.what()};
        }
        std::unordered_map<int, int> home_systems;
        for (const auto& home : homes) home_systems.emplace(home.civilization_id, home.system_id);
        for (Civilization& civilization : result.civilizations) {
            const auto found = home_systems.find(civilization.id);
            if (found == home_systems.end()) throw std::invalid_argument{"Constrained home planner omitted a civilization."};
            civilization.home_system_id = found->second;
        }
        rename_homes_and_ensure_unique(result.systems, result.civilizations);
        result.bodies = generate_planetary_catalog(seed, result.systems);
        result.bodies = apply_nearby_habitable_guarantees(seed, result.systems, result.bodies, result.civilizations, 2);
    }
    return result;
}
} // namespace stellar::core
