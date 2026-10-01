#pragma once

#include <limits>

#include <stellar/engine/planetary.hpp>
#include <stellar/engine/population.hpp>

namespace stellar::engine {

// Population <-> planetary adapter: resolves a demographic profile's
// declared environment_needs against a PlanetEnvironment through the
// shared habitability evaluator instead of a second tag matcher.
//
// Needs are hard requirements: every declared tag must be present for
// the cohort to be suited to the habitat. Physical ranges stay open —
// species with temperature/gravity tolerances express them with a full
// HabitabilityProfile instead; the two profiles compose (tags from
// needs, ranges from the species profile).
[[nodiscard]] inline HabitabilityProfile
needs_habitability_profile(const DemographicProfile &profile) {
    HabitabilityProfile out;
    out.id = profile.id;
    out.temperature_max_k = std::numeric_limits<double>::max();
    out.atmosphere_max = std::numeric_limits<double>::max();
    out.gravity_max_g = std::numeric_limits<double>::max();
    out.water_min = 0.0;
    out.tolerance = 0.0;
    out.required_tags = profile.environment_needs;
    return out;
}

// Full report: suitability is 1.0 when every need tag is satisfied and
// 0.0 otherwise; unmet entries name the missing tags as "requires:<tag>".
[[nodiscard]] inline HabitabilityReport
evaluate_needs(const PlanetEnvironment &env,
               const DemographicProfile &profile) {
    return evaluate_habitability(env, needs_habitability_profile(profile));
}

// The scalar callers feed into Population StepConditions
// .environment_suitability (or a Cohort's per-step suitability).
[[nodiscard]] inline double
needs_suitability(const PlanetEnvironment &env,
                  const DemographicProfile &profile) {
    return evaluate_needs(env, profile).suitability;
}

} // namespace stellar::engine
