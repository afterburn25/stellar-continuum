#include <stellar/core/civilization_catalog.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
constexpr std::string_view terran = "terran_baseline";
constexpr std::string_view pelagic = "pelagic_high_pressure";
constexpr std::string_view compact = "compact_high_gravity";
constexpr std::string_view cryogenic = "cryogenic_hydrocarbon";

std::uint64_t mix(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

double within_system_score(const PlanetSpeciesAssessment& assessment) {
    return assessment.environment.natural_habitability * 10.0 +
        (assessment.suitability == SettlementSuitability::Comfortable ? .35 : 0.0);
}

struct Candidate { const StellarSystem* system; const PlanetaryBody* body; PlanetSpeciesAssessment assessment; };
struct CandidateSet { int civilization_id; std::string species_id; std::vector<Candidate> candidates; };

bool stable_expansion_star(const std::optional<StellarClass>& primary) {
    return primary != StellarClass::BlackHole && primary != StellarClass::NeutronStar &&
        primary != StellarClass::Pulsar && primary != StellarClass::HotBlueStar &&
        primary != StellarClass::Giant && primary != StellarClass::Protostar;
}

bool naturally_viable(const SpeciesEnvironmentProfile& species, const PlanetaryBody& body) {
    return !body.has_pre_warp_civilization && assess_species_planet(species, body).naturally_colonizable;
}

bool try_assign_expansion(int slot, const std::vector<std::vector<const StellarSystem*>>& candidates,
    std::unordered_map<int, int>& assignments, std::unordered_set<int>& visited) {
    for (const auto* candidate : candidates[static_cast<std::size_t>(slot / 2)]) {
        if (!visited.insert(candidate->id).second) continue;
        const auto assigned = assignments.find(candidate->id);
        if (assigned == assignments.end() || try_assign_expansion(assigned->second, candidates, assignments, visited)) {
            assignments[candidate->id] = slot;
            return true;
        }
    }
    return false;
}

bool has_expansion_assignment(std::span<const StellarSystem> systems, const std::unordered_set<int>& eligible,
    const std::vector<const Candidate*>& homes, int major_count, bool partial) {
    std::unordered_set<int> occupied_homes;
    for (const auto* home : homes) if (home) occupied_homes.insert(home->system->id);
    std::vector<const Candidate*> selected;
    const int limit = major_count;
    for (int i = 0; i < limit; ++i) if (homes[static_cast<std::size_t>(i)]) selected.push_back(homes[static_cast<std::size_t>(i)]);
    if (selected.empty()) return true;
    std::vector<std::vector<const StellarSystem*>> candidates;
    for (const auto* home : selected) {
        std::vector<const StellarSystem*> choices;
        for (const auto& system : systems) if (!occupied_homes.contains(system.id) && stable_expansion_star(system.primary) &&
            eligible.contains(system.id) && distance_light_years(home->system->position, system.position) <= 340.0) choices.push_back(&system);
        std::sort(choices.begin(), choices.end(), [&](const auto* left, const auto* right) {
            if (partial) return left->id < right->id;
            const auto dl = distance_light_years(home->system->position, left->position);
            const auto dr = distance_light_years(home->system->position, right->position);
            return dl != dr ? dl < dr : left->id < right->id;
        });
        if (choices.size() < 2) return false;
        candidates.push_back(std::move(choices));
    }
    std::vector<int> slots;
    for (int i = 0; i < static_cast<int>(selected.size()) * 2; ++i) slots.push_back(i);
    std::sort(slots.begin(), slots.end(), [&](int left, int right) { const auto l = candidates[left / 2].size(), r = candidates[right / 2].size(); return l != r ? l < r : left < right; });
    std::unordered_map<int, int> assigned;
    for (const auto slot : slots) { std::unordered_set<int> visited; if (!try_assign_expansion(slot, candidates, assigned, visited)) return false; }
    return true;
}

std::vector<Candidate> build_candidates(const SpeciesEnvironmentProfile& species, std::span<const PlanetaryBody> bodies,
    const std::unordered_map<int, const StellarSystem*>& systems_by_id, bool canonical_sol) {
    std::vector<Candidate> result;
    for (const auto& body : bodies) {
        if (body.has_pre_warp_civilization) continue;
        if (canonical_sol && (species.id == terran ? !(body.system_id == sol_system_id && body.id == earth_body_id) : body.system_id == sol_system_id)) continue;
        const auto assessment = assess_species_planet(species, body);
        if (!assessment.naturally_colonizable) continue;
        const auto found = systems_by_id.find(body.system_id);
        if (found == systems_by_id.end()) throw std::invalid_argument{"Planetary body references an unknown system."};
        result.push_back({found->second, &body, assessment});
    }
    return result;
}

double score_candidate(const Candidate& candidate, const std::vector<SpeciesHomeworldAssignment>& chosen,
    const std::unordered_map<int, const StellarSystem*>& systems_by_id) {
    double spread = 0.0;
    if (!chosen.empty()) {
        spread = std::numeric_limits<double>::infinity();
        for (const auto& existing : chosen) {
            const auto system = systems_by_id.find(existing.system_id);
            if (system == systems_by_id.end()) throw std::invalid_argument{"Chosen homeworld references an unknown system."};
            spread = std::min(spread, squared_distance_light_years(candidate.system->position, system->second->position));
        }
    }
    return within_system_score(candidate.assessment) + std::min(1.0, std::sqrt(spread) / 500.0);
}
} // namespace

std::string assign_species(std::int64_t campaign_seed, int civilization_id, bool canonical_start) {
    if (civilization_id < 0) throw std::out_of_range{"Civilization IDs must be non-negative."};
    const auto seed = std::bit_cast<std::uint64_t>(campaign_seed) ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(civilization_id)) * 0xD1B54A32D192ED03ULL;
    const auto value = mix(seed);
    if (!canonical_start) {
        const auto profiles = species_environment_profiles();
        if (profiles.empty()) throw std::invalid_argument{"No species definitions are available."};
        return profiles[value % profiles.size()].id;
    }
    if (civilization_id == 0) return std::string{terran};
    switch (value % 3) {
    case 0: return std::string{pelagic};
    case 1: return std::string{compact};
    default: return std::string{cryogenic};
    }
}

std::vector<SpeciesHomeworldAssignment> plan_species_homeworlds(std::span<const StellarSystem> systems,
    std::span<const PlanetaryBody> bodies, std::span<const std::string> species_ids) {
    if (species_ids.empty()) return {};
    if (systems.size() < species_ids.size()) throw std::invalid_argument{"There are fewer star systems than founding civilizations."};
    std::unordered_map<int, const StellarSystem*> systems_by_id;
    systems_by_id.reserve(systems.size());
    bool canonical_sol = false;
    for (const auto& system : systems) {
        if (!systems_by_id.emplace(system.id, &system).second) throw std::invalid_argument{"Duplicate star system ID."};
        canonical_sol = canonical_sol || (system.catalog_preset_id && *system.catalog_preset_id == sol_catalog_preset_id);
    }

    std::vector<CandidateSet> sets;
    sets.reserve(species_ids.size());
    for (std::size_t index = 0; index < species_ids.size(); ++index) {
        const auto& species = species_environment_profile(species_ids[index]);
        sets.push_back({static_cast<int>(index), species.id, build_candidates(species, bodies, systems_by_id, canonical_sol)});
    }
    std::string missing;
    for (const auto& set : sets) if (set.candidates.empty()) {
        if (!missing.empty()) missing += ", ";
        missing += "civ " + std::to_string(set.civilization_id) + " / " + set.species_id;
    }
    if (!missing.empty()) throw std::invalid_argument{"Natural homeworld planning failed because no compatible uninhabited body exists for: " + missing + "."};

    std::stable_sort(sets.begin(), sets.end(), [](const CandidateSet& left, const CandidateSet& right) {
        std::unordered_set<int> left_systems, right_systems;
        for (const auto& item : left.candidates) left_systems.insert(item.system->id);
        for (const auto& item : right.candidates) right_systems.insert(item.system->id);
        return left_systems.size() != right_systems.size() ? left_systems.size() < right_systems.size() : left.civilization_id < right.civilization_id;
    });
    std::vector<SpeciesHomeworldAssignment> chosen;
    chosen.reserve(species_ids.size());
    std::unordered_set<int> occupied;
    for (const auto& set : sets) {
        const Candidate* best = nullptr;
        double best_score = 0.0;
        for (const auto& candidate : set.candidates) {
            if (occupied.contains(candidate.system->id)) continue;
            const auto score = score_candidate(candidate, chosen, systems_by_id);
            if (!best || score > best_score || (score == best_score && (candidate.assessment.environment.natural_habitability > best->assessment.environment.natural_habitability ||
                (candidate.assessment.environment.natural_habitability == best->assessment.environment.natural_habitability &&
                 (candidate.system->id < best->system->id || (candidate.system->id == best->system->id && candidate.body->id < best->body->id)))))) {
                best = &candidate; best_score = score;
            }
        }
        if (!best) {
            std::unordered_set<int> available_set;
            for (const auto& candidate : set.candidates) available_set.insert(candidate.system->id);
            std::vector<int> available{available_set.begin(), available_set.end()}; std::sort(available.begin(), available.end());
            std::string ids;
            for (const auto id : available) { if (!ids.empty()) ids += ","; ids += std::to_string(id); }
            throw std::invalid_argument{"Natural homeworld planning exhausted distinct systems for civ " + std::to_string(set.civilization_id) +
                " / " + set.species_id + ". Compatible systems: [" + ids + "]."};
        }
        occupied.insert(best->system->id);
        chosen.push_back({set.civilization_id, set.species_id, best->system->id, best->body->id,
            best->assessment.environment.natural_habitability, best->assessment.suitability});
    }
    std::sort(chosen.begin(), chosen.end(), [](const auto& left, const auto& right) { return left.civilization_id < right.civilization_id; });
    return chosen;
}

std::vector<SpeciesHomeworldAssignment> plan_species_homeworlds_with_nearby_expansion(
    std::span<const StellarSystem> systems, std::span<const PlanetaryBody> bodies,
    std::span<const std::string> species_ids, int major_civilization_count, int search_limit) {
    if (major_civilization_count < 1 || major_civilization_count > static_cast<int>(species_ids.size()))
        throw std::out_of_range{"major_civilization_count"};
    if (search_limit < 1) throw std::out_of_range{"search_limit"};
    std::unordered_map<int, const StellarSystem*> systems_by_id;
    bool canonical_sol = false;
    for (const auto& system : systems) {
        if (!systems_by_id.emplace(system.id, &system).second) throw std::invalid_argument{"Duplicate star system ID."};
        canonical_sol = canonical_sol || (system.catalog_preset_id && *system.catalog_preset_id == sol_catalog_preset_id);
    }
    std::unordered_set<int> eligible;
    for (const auto& system : systems) if (stable_expansion_star(system.primary) && std::any_of(bodies.begin(), bodies.end(), [&](const auto& body) {
        return body.system_id == system.id && body.kind == PlanetaryBodyKind::Planet && body.environment.has_solid_surface && !body.has_pre_warp_civilization;
    })) eligible.insert(system.id);
    struct HomeSet { int id; std::vector<Candidate> candidates; };
    std::vector<HomeSet> options;
    std::vector<int> missing;
    for (std::size_t i = 0; i < species_ids.size(); ++i) {
        auto choices = build_candidates(species_environment_profile(species_ids[i]), bodies, systems_by_id, canonical_sol);
        std::unordered_map<int, Candidate> best;
        for (const auto& candidate : choices) {
            const auto prior = best.find(candidate.system->id);
            if (prior == best.end() || within_system_score(candidate.assessment) > within_system_score(prior->second.assessment) ||
                (within_system_score(candidate.assessment) == within_system_score(prior->second.assessment) &&
                 (candidate.assessment.environment.natural_habitability > prior->second.assessment.environment.natural_habitability ||
                  (candidate.assessment.environment.natural_habitability == prior->second.assessment.environment.natural_habitability && candidate.body->id < prior->second.body->id)))) best[candidate.system->id] = candidate;
        }
        choices.clear(); for (const auto& [id, candidate] : best) { (void)id; choices.push_back(candidate); }
        std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { const auto as = within_system_score(a.assessment), bs = within_system_score(b.assessment); return as != bs ? as > bs : a.assessment.environment.natural_habitability != b.assessment.environment.natural_habitability ? a.assessment.environment.natural_habitability > b.assessment.environment.natural_habitability : a.system->id != b.system->id ? a.system->id < b.system->id : a.body->id < b.body->id; });
        if (choices.empty()) missing.push_back(static_cast<int>(i));
        options.push_back({static_cast<int>(i), std::move(choices)});
    }
    if (!missing.empty()) {
        std::string names;
        for (const auto civilization_id : missing) {
            if (!names.empty()) names += ", ";
            names += "civ " + std::to_string(civilization_id);
        }
        throw std::invalid_argument{"Constrained homeworld planning found no natural home for " + names + "."};
    }
    std::sort(options.begin(), options.end(), [](const auto& a, const auto& b) { return a.candidates.size() != b.candidates.size() ? a.candidates.size() < b.candidates.size() : a.id < b.id; });
    std::vector<const Candidate*> chosen(species_ids.size());
    std::unordered_set<int> occupied;
    int explored = 0;
    bool exhausted = false;
    const auto search = [&](auto&& self, int next) -> bool {
        if (++explored > search_limit) { exhausted = true; return false; }
        if (next == static_cast<int>(options.size())) return has_expansion_assignment(systems, eligible, chosen, major_civilization_count, false);
        const auto& set = options[static_cast<std::size_t>(next)];
        for (const auto& candidate : set.candidates) {
            if (!occupied.insert(candidate.system->id).second) continue;
            chosen[static_cast<std::size_t>(set.id)] = &candidate;
            if (has_expansion_assignment(systems, eligible, chosen, major_civilization_count, true) && self(self, next + 1)) return true;
            if (exhausted) return false;
            chosen[static_cast<std::size_t>(set.id)] = nullptr;
            occupied.erase(candidate.system->id);
        }
        return false;
    };
    if (!search(search, 0)) {
        const auto outcome = exhausted ? "Constrained natural-home search budget exhausted" :
            "No complete natural-home and nearby-expansion assignment exists";
        throw std::invalid_argument{std::string(outcome) + " after " + std::to_string(explored) +
            " states; major civilizations=" + std::to_string(major_civilization_count) +
            ", systems=" + std::to_string(systems.size()) + "."};
    }
    std::vector<SpeciesHomeworldAssignment> result;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const auto* candidate = chosen[i];
        if (!candidate) throw std::invalid_argument{"Constrained homeworld planner returned an incomplete assignment."};
        result.push_back({static_cast<int>(i), species_ids[i], candidate->system->id,
            candidate->body->id, candidate->assessment.environment.natural_habitability,
            candidate->assessment.suitability});
    }
    return result;
}

SpeciesHomeworldAssignment resolve_species_homeworld(int civilization_id, const std::string& species_id, int system_id,
    std::span<const PlanetaryBody> bodies) {
    const auto& species = species_environment_profile(species_id);
    const PlanetaryBody* best = nullptr; PlanetSpeciesAssessment best_assessment;
    for (const auto& body : bodies) if (body.system_id == system_id && naturally_viable(species, body)) {
        const auto assessment = assess_species_planet(species, body);
        if (!best || within_system_score(assessment) > within_system_score(best_assessment) ||
            (within_system_score(assessment) == within_system_score(best_assessment) && (assessment.environment.natural_habitability > best_assessment.environment.natural_habitability || (assessment.environment.natural_habitability == best_assessment.environment.natural_habitability && body.id < best->id)))) { best = &body; best_assessment = assessment; }
    }
    if (!best) throw std::invalid_argument{"Planned home system " + std::to_string(system_id) + " has no naturally viable body for civ " + std::to_string(civilization_id) + " / " + species_id + "."};
    return {civilization_id, species_id, system_id, best->id, best_assessment.environment.natural_habitability, best_assessment.suitability};
}
} // namespace stellar::core
