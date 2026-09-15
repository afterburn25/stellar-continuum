#include <stellar/core/civilization_catalog.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
constexpr double maximum_opening_distance = 340.0;

bool stable_star(const std::optional<StellarClass>& primary) {
    return primary != StellarClass::BlackHole && primary != StellarClass::NeutronStar && primary != StellarClass::Pulsar &&
        primary != StellarClass::HotBlueStar && primary != StellarClass::Giant && primary != StellarClass::Protostar;
}
uint32_t mix(std::int64_t seed, int civilization_id, int system_id) {
    auto value = std::bit_cast<uint64_t>(seed) ^ static_cast<uint64_t>(static_cast<uint32_t>(civilization_id)) * 0x9E3779B9ULL ^
        static_cast<uint64_t>(static_cast<uint32_t>(system_id)) * 0x85EBCA6BULL;
    value ^= value >> 30; value *= 0xBF58476D1CE4E5B9ULL; value ^= value >> 27;
    return static_cast<uint32_t>(value ^ (value >> 32));
}
bool natural(const PlanetaryBody& body, const std::string& species) {
    return !body.has_pre_warp_civilization && assess_species_planet(species_environment_profile(species), body).naturally_colonizable;
}
const PlanetaryBody& home_body(std::span<const PlanetaryBody> bodies, const Civilization& civilization) {
    const auto& species = species_environment_profile(civilization.species_id);
    const PlanetaryBody* best = nullptr;
    double best_habitability = 0.0;
    for (const auto& body : bodies) {
        if (body.system_id != civilization.home_system_id || body.has_pre_warp_civilization) continue;
        const auto assessment = assess_species_planet(species, body);
        if (!assessment.naturally_colonizable) continue;
        if (!best || assessment.environment.natural_habitability > best_habitability ||
            (assessment.environment.natural_habitability == best_habitability && body.id < best->id)) {
            best = &body;
            best_habitability = assessment.environment.natural_habitability;
        }
    }
    if (!best) throw std::invalid_argument{"Civilization " + std::to_string(civilization.id) + " has no natural homeworld for nearby-world guarantees."};
    return *best;
}
struct Candidate { const StellarSystem* system; double distance; const PlanetaryBody* natural_body; const PlanetaryBody* fallback_body; };
struct Requirement { const Civilization* civilization; const PlanetaryBody* home; int slot; std::vector<Candidate> candidates; };

std::unordered_map<int, PlanetaryBody> bodies_by_id(std::span<const PlanetaryBody> bodies) {
    std::unordered_map<int, PlanetaryBody> result;
    result.reserve(bodies.size());
    for (const auto& body : bodies) {
        if (!result.emplace(body.id, body).second)
            throw std::invalid_argument{"Duplicate planetary body ID."};
    }
    return result;
}

std::vector<Candidate> candidates_for(std::int64_t seed, std::span<const StellarSystem> systems, std::span<const PlanetaryBody> bodies,
    const std::unordered_map<int, PlanetaryBody>& result, const std::unordered_set<int>& excluded, const Civilization& civilization,
    bool sort_natural) {
    const auto home_it = std::find_if(systems.begin(), systems.end(), [&](const auto& s) { return s.id == civilization.home_system_id; });
    if (home_it == systems.end()) throw std::invalid_argument{"Civilization home system was not found."};
    const auto& home_system = *home_it;
    std::vector<Candidate> output;
    for (const auto& system : systems) {
        if (excluded.contains(system.id) || !stable_star(system.primary)) continue;
        const auto distance = distance_light_years(home_system.position, system.position);
        if (distance > maximum_opening_distance) continue;
        std::vector<const PlanetaryBody*> planets;
        for (const auto& body : bodies) {
            if (body.system_id == system.id && body.kind == PlanetaryBodyKind::Planet &&
                body.environment.has_solid_surface && !body.has_pre_warp_civilization)
                planets.push_back(&body);
        }
        std::sort(planets.begin(), planets.end(), [](const auto* l, const auto* r) { return l->id < r->id; });
        if (planets.empty()) continue;
        const PlanetaryBody* natural_body = nullptr;
        for (const auto* body : planets) {
            if (natural(result.at(body->id), civilization.species_id)) {
                natural_body = body;
                break;
            }
        }
        const auto fallback_index = mix(seed, civilization.id, system.id) % planets.size();
        output.push_back({&system, distance, natural_body, planets[fallback_index]});
    }
    std::sort(output.begin(), output.end(), [=](const auto& l, const auto& r) {
        if (sort_natural && (l.natural_body != nullptr) != (r.natural_body != nullptr)) return l.natural_body != nullptr;
        return l.distance != r.distance ? l.distance < r.distance : l.system->id < r.system->id;
    });
    return output;
}
bool try_assign(const Requirement* requirement, std::unordered_map<int, const Requirement*>& assignments, std::unordered_set<int>& visited) {
    for (const auto& candidate : requirement->candidates) {
        if (!visited.insert(candidate.system->id).second) continue;
        const auto found = assignments.find(candidate.system->id);
        if (found == assignments.end() || try_assign(found->second, assignments, visited)) {
            assignments[candidate.system->id] = requirement;
            return true;
        }
    }
    return false;
}
}

std::vector<PlanetaryBody> apply_nearby_habitable_guarantees(std::int64_t seed, std::span<StellarSystem> systems,
    std::span<const PlanetaryBody> bodies, std::span<const Civilization> civilizations, int guaranteed) {
    auto original = std::vector<StellarSystem>(systems.begin(), systems.end());
    const auto greedy = [&]() {
        if (guaranteed <= 0) return std::vector<PlanetaryBody>(bodies.begin(), bodies.end());
        auto result = bodies_by_id(bodies);
        std::unordered_set<int> reserved;
        for (const auto& civilization : civilizations) reserved.insert(civilization.home_system_id);
        std::vector<const Civilization*> ordinary;
        for (const auto& civilization : civilizations) {
            if (!civilization.is_seeded_ancient) ordinary.push_back(&civilization);
        }
        std::sort(ordinary.begin(), ordinary.end(), [](const auto* l, const auto* r) { return l->id < r->id; });
        for (const auto* civ : ordinary) {
            const auto& home = home_body(bodies, *civ);
            const auto candidates = candidates_for(seed, systems, bodies, result, reserved, *civ, false);
            int accepted = 0;
            for (const auto& candidate : candidates) {
                if (!candidate.natural_body) continue;
                reserved.insert(candidate.system->id);
                if (++accepted == guaranteed) break;
            }
            if (accepted < guaranteed) for (const auto& candidate : candidates) {
                if (reserved.contains(candidate.system->id)) continue;
                auto changed = *candidate.fallback_body;
                changed.mass_earth = std::max(.0005,
                    home.environment.gravity_g * changed.radius_earth * changed.radius_earth);
                changed.environment = home.environment;
                result[changed.id] = changed;
                const auto system = std::find_if(systems.begin(), systems.end(),
                    [&](const auto& value) { return value.id == candidate.system->id; });
                system->has_habitable_world = true;
                reserved.insert(candidate.system->id);
                if (++accepted == guaranteed) break;
            }
            if (accepted != guaranteed) {
                throw std::invalid_argument{"Could not place " + std::to_string(guaranteed) +
                    " nearby viable worlds for civilization " + std::to_string(civ->id) +
                    " within 340 map units."};
            }
        }
        std::vector<PlanetaryBody> answer;
        answer.reserve(bodies.size());
        for (const auto& body : bodies) {
            validate_planetary_body(result.at(body.id));
            answer.push_back(result.at(body.id));
        }
        return answer;
    };
    try { return greedy(); } catch (const std::invalid_argument& error) {
        if (!std::string_view(error.what()).starts_with("Could not place ")) throw;
        std::copy(original.begin(), original.end(), systems.begin());
    }
    auto result = bodies_by_id(bodies);
    std::unordered_set<int> homes;
    for (const auto& civilization : civilizations) homes.insert(civilization.home_system_id);
    std::vector<Requirement> requirements;
    std::vector<const Civilization*> ordinary;
    for (const auto& civilization : civilizations) {
        if (!civilization.is_seeded_ancient) ordinary.push_back(&civilization);
    }
    std::sort(ordinary.begin(), ordinary.end(), [](const auto* l, const auto* r) { return l->id < r->id; });
    for (const auto* civilization : ordinary) {
        const auto& home = home_body(bodies, *civilization);
        const auto choices = candidates_for(seed, systems, bodies, result, homes, *civilization, true);
        for (int slot = 0; slot < guaranteed; ++slot)
            requirements.push_back({civilization, &home, slot, choices});
    }
    std::vector<const Requirement*> order;
    for (const auto& requirement : requirements) order.push_back(&requirement);
    std::sort(order.begin(), order.end(), [](const auto* l, const auto* r) { return l->candidates.size() != r->candidates.size() ? l->candidates.size() < r->candidates.size() : l->civilization->id != r->civilization->id ? l->civilization->id < r->civilization->id : l->slot < r->slot; });
    std::unordered_map<int, const Requirement*> assigned;
    for (const auto* requirement : order) {
        std::unordered_set<int> visited;
        if (try_assign(requirement, assigned, visited)) continue;
        std::unordered_set<int> pool;
        for (const auto& all_requirement : requirements) {
            for (const auto& candidate : all_requirement.candidates)
                pool.insert(candidate.system->id);
        }
        throw std::invalid_argument{"Could not place nearby viable worlds for civilization " +
            std::to_string(requirement->civilization->id) + " within 340 map units for seed " +
            std::to_string(seed) + "; slot " + std::to_string(requirement->slot + 1) + " has " +
            std::to_string(requirement->candidates.size()) + " eligible systems, " +
            std::to_string(assigned.size()) + "/" + std::to_string(requirements.size()) +
            " slots were assigned, and the global candidate pool contains " +
            std::to_string(pool.size()) + " systems."};
    }
    std::vector<int> assigned_systems;
    assigned_systems.reserve(assigned.size());
    for (const auto& [system_id, requirement] : assigned) {
        (void)requirement;
        assigned_systems.push_back(system_id);
    }
    std::sort(assigned_systems.begin(), assigned_systems.end());
    for (const auto system_id : assigned_systems) {
        const auto* requirement = assigned.at(system_id);
        const auto candidate = std::find_if(requirement->candidates.begin(), requirement->candidates.end(),
            [&](const auto& value) { return value.system->id == system_id; });
        if (candidate->natural_body) continue;
        auto changed = *candidate->fallback_body;
        changed.mass_earth = std::max(.0005,
            requirement->home->environment.gravity_g * changed.radius_earth * changed.radius_earth);
        changed.environment = requirement->home->environment;
        result[changed.id] = changed;
        const auto system = std::find_if(systems.begin(), systems.end(),
            [&](const auto& value) { return value.id == system_id; });
        system->has_habitable_world = true;
    }
    std::vector<PlanetaryBody> answer;
    answer.reserve(bodies.size());
    for (const auto& body : bodies) {
        validate_planetary_body(result.at(body.id));
        answer.push_back(result.at(body.id));
    }
    return answer;
}
}
