#pragma once

#include <string>
#include <vector>

namespace stellar::engine {

// Planetary environment model — the engine-level physical description
// that habitability evaluation reads and terraforming mutates. This is
// an ADAPTER surface: authoritative planet state stays in Core
// (PlanetaryBody, planetary catalog); adapters project it into this
// plain struct so engine systems (Population environment_needs, Colony
// required_tags, terraforming) can evaluate it without depending on
// game types.

struct PlanetEnvironment {
    double temperature_k{0.0};   // mean surface temperature
    double atmosphere_atm{0.0};  // surface pressure
    double gravity_g{1.0};       // surface gravity
    double water_fraction{0.0};  // 0..1 surface water coverage
    // Sorted unique free-form tags: "volatile", "tidal_lock",
    // "high_radiation", "dense_atmosphere", ... Games define the
    // vocabulary; species profiles and colony specs match against it.
    std::vector<std::string> tags;
};

// Species/culture-relative habitability requirements. One profile per
// demographic archetype; ranges are HARD habitability bounds, and
// `tolerance` (fraction of each range's span) extends a soft margin
// beyond each edge where suitability ramps linearly to zero.
struct HabitabilityProfile {
    std::string id;
    double temperature_min_k{0.0};
    double temperature_max_k{0.0};
    double atmosphere_min{0.0};
    double atmosphere_max{0.0};
    double gravity_min_g{0.0};
    double gravity_max_g{0.0};
    double water_min{0.0};
    double tolerance{0.25}; // soft margin = tolerance × range span
    std::vector<std::string> required_tags;  // must all be present
    std::vector<std::string> forbidden_tags; // any present fails outright
};

struct HabitabilityReport {
    double suitability{0.0};        // 0..1 product of per-parameter scores
    bool habitable{false};          // suitability > 0 AND all tags satisfied
    std::vector<std::string> unmet; // sorted reason strings
};

// Deterministic evaluation — pure function, no state.
HabitabilityReport evaluate_habitability(const PlanetEnvironment& env,
                                         const HabitabilityProfile& profile);

// Maps environment tags -> aggregate suitability for callers that only
// need Population's environment_suitability scalar.
[[nodiscard]] inline double
suitability_scalar(const PlanetEnvironment& env,
                   const HabitabilityProfile& profile) {
    return evaluate_habitability(env, profile).suitability;
}

} // namespace stellar::engine
