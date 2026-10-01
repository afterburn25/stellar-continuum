#include <stellar/engine/planetary.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::engine {

namespace {

// 1.0 inside [min,max]; outside, ramps linearly to 0 over
// tolerance*span beyond the nearer edge. Degenerate (zero-width) ranges
// are treated as exact bounds with an absolute margin.
double range_score(double value, double min, double max,
                   double tolerance) {
    if (value >= min && value <= max) return 1.0;
    const double span = max - min;
    const double margin =
        span > 0.0 ? tolerance * span : std::max(std::abs(min), 1.0) * tolerance;
    if (margin <= 0.0) return 0.0;
    const double overshoot = value < min ? min - value : value - max;
    return std::clamp(1.0 - overshoot / margin, 0.0, 1.0);
}

bool has_tag(const std::vector<std::string>& tags, const std::string& t) {
    return std::binary_search(tags.begin(), tags.end(), t);
}

} // namespace

HabitabilityReport evaluate_habitability(const PlanetEnvironment& env,
                                         const HabitabilityProfile& p) {
    HabitabilityReport report;
    bool tags_ok = true;
    for (const auto& t : p.required_tags) {
        if (!has_tag(env.tags, t)) {
            report.unmet.push_back("requires:" + t);
            tags_ok = false;
        }
    }
    for (const auto& t : p.forbidden_tags) {
        if (has_tag(env.tags, t)) {
            report.unmet.push_back("forbidden:" + t);
            tags_ok = false;
        }
    }

    const double temp = range_score(env.temperature_k, p.temperature_min_k,
                                    p.temperature_max_k, p.tolerance);
    const double atm = range_score(env.atmosphere_atm, p.atmosphere_min,
                                   p.atmosphere_max, p.tolerance);
    const double grav = range_score(env.gravity_g, p.gravity_min_g,
                                    p.gravity_max_g, p.tolerance);
    if (temp < 1.0) report.unmet.push_back("temperature");
    if (atm < 1.0) report.unmet.push_back("atmosphere");
    if (grav < 1.0) report.unmet.push_back("gravity");

    double water = 1.0;
    if (env.water_fraction < p.water_min) {
        // Water is a floor, not a range: score is the achieved fraction
        // of the minimum.
        water = p.water_min > 0.0
                    ? std::clamp(env.water_fraction / p.water_min, 0.0, 1.0)
                    : 0.0;
        report.unmet.push_back("water");
    }

    report.suitability = tags_ok ? temp * atm * grav * water : 0.0;
    report.habitable = tags_ok && report.suitability > 0.0;
    std::sort(report.unmet.begin(), report.unmet.end());
    return report;
}

} // namespace stellar::engine
