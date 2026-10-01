#include <stellar/engine/population_habitability.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

PlanetEnvironment garden() {
    PlanetEnvironment env;
    env.temperature_k = 288.0;
    env.atmosphere_atm = 1.0;
    env.gravity_g = 1.0;
    env.water_fraction = 0.7;
    env.tags = {"breathable_atmosphere", "magnetosphere", "surface_water"};
    return env;
}

DemographicProfile humans() {
    DemographicProfile p;
    p.id = "species.human";
    p.environment_needs = {"breathable_atmosphere", "surface_water"};
    return p;
}

void test_satisfied_needs() {
    const auto report = evaluate_needs(garden(), humans());
    check(report.suitability == 1.0, "all needs met -> suitability 1.0");
    check(report.habitable, "all needs met -> habitable");
    check(report.unmet.empty(), "all needs met -> no unmet reasons");
    check(needs_suitability(garden(), humans()) == 1.0,
          "scalar convenience matches report");
}

void test_missing_need() {
    auto env = garden();
    env.tags = {"magnetosphere", "surface_water"};
    const auto report = evaluate_needs(env, humans());
    check(report.suitability == 0.0, "missing need -> suitability 0.0");
    check(!report.habitable, "missing need -> not habitable");
    check(std::find(report.unmet.begin(), report.unmet.end(),
                    "requires:breathable_atmosphere") != report.unmet.end(),
          "unmet reason names the missing tag");
}

void test_no_needs_is_unconstrained() {
    DemographicProfile p;
    p.id = "species.vacuum_native";
    PlanetEnvironment vacuum;
    vacuum.temperature_k = 40.0;
    vacuum.tags = {"vacuum", "high_radiation"};
    const auto report = evaluate_needs(vacuum, p);
    check(report.suitability == 1.0,
          "no declared needs -> any environment suits");
    check(report.habitable, "no declared needs -> habitable");
}

void test_profile_passthrough() {
    const auto hp = needs_habitability_profile(humans());
    check(hp.id == "species.human", "habitability profile keeps id");
    check(hp.required_tags == humans().environment_needs,
          "needs map onto required_tags");
    check(hp.forbidden_tags.empty(), "needs never forbid tags");
}

} // namespace

int main() {
    test_satisfied_needs();
    test_missing_need();
    test_no_needs_is_unconstrained();
    test_profile_passthrough();
    if (failures == 0) {
        std::cout << "population habitability tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
