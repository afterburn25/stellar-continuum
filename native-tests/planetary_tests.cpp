#include <stellar/engine/planetary.hpp>

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

bool near(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

using namespace stellar::engine;

PlanetEnvironment earthlike() {
    PlanetEnvironment e;
    e.temperature_k = 288.0;
    e.atmosphere_atm = 1.0;
    e.gravity_g = 1.0;
    e.water_fraction = 0.7;
    e.tags = {};
    return e;
}

HabitabilityProfile human() {
    HabitabilityProfile p;
    p.id = "species.human";
    p.temperature_min_k = 253.0; // -20C
    p.temperature_max_k = 323.0; // +50C
    p.atmosphere_min = 0.5;
    p.atmosphere_max = 2.0;
    p.gravity_min_g = 0.5;
    p.gravity_max_g = 1.5;
    p.water_min = 0.2;
    p.tolerance = 0.25;
    p.forbidden_tags = {"toxic_atmosphere"};
    return p;
}

} // namespace

int main() {
    // --- Fully habitable ----------------------------------------------------
    {
        auto r = evaluate_habitability(earthlike(), human());
        check(r.habitable, "earthlike world habitable for humans");
        check(near(r.suitability, 1.0), "earthlike suitability 1.0");
        check(r.unmet.empty(), "no unmet reasons");
    }

    // --- Soft margin: outside hard range degrades gradually -------------------
    {
        auto env = earthlike();
        env.temperature_k = 340.0; // 17K past the 323 max; margin = .25*70 = 17.5
        auto r = evaluate_habitability(env, human());
        check(r.habitable, "marginal world still habitable");
        check(r.suitability > 0.0 && r.suitability < 1.0,
              "partial suitability in margin");
        check(!r.unmet.empty(), "temperature flagged");
        env.temperature_k = 400.0; // way past margin
        r = evaluate_habitability(env, human());
        check(!r.habitable, "far past margin is uninhabitable");
        check(near(r.suitability, 0.0), "suitability bottoms out");
    }

    // --- Forbidden tag fails outright ------------------------------------------
    {
        auto env = earthlike();
        env.tags = {"toxic_atmosphere"};
        auto r = evaluate_habitability(env, human());
        check(!r.habitable, "forbidden tag kills habitability");
        check(near(r.suitability, 0.0), "forbidden tag zeroes suitability");
        bool found = false;
        for (const auto& u : r.unmet)
            if (u == "forbidden:toxic_atmosphere") found = true;
        check(found, "forbidden tag reported");
    }

    // --- Required tags ------------------------------------------------------------
    {
        HabitabilityProfile p = human();
        p.required_tags = {"ocean_world"};
        auto r = evaluate_habitability(earthlike(), p);
        check(!r.habitable, "missing required tag fails");
        auto env = earthlike();
        env.tags = {"ocean_world"};
        r = evaluate_habitability(env, p);
        check(r.habitable, "required tag satisfied");
    }

    // --- Water floor ----------------------------------------------------------------
    {
        auto env = earthlike();
        env.water_fraction = 0.1; // half of the 0.2 minimum
        auto r = evaluate_habitability(env, human());
        check(near(r.suitability, 0.5, 1e-6), "water scales as fraction of minimum");
        env.water_fraction = 0.0;
        r = evaluate_habitability(env, human());
        check(!r.habitable, "no water = uninhabitable when water required");
    }

    // --- Determinism ------------------------------------------------------------------
    {
        const auto a = evaluate_habitability(earthlike(), human());
        const auto b = evaluate_habitability(earthlike(), human());
        check(a.suitability == b.suitability && a.unmet == b.unmet,
              "identical evaluation bit-equal");
    }

    if (failures == 0) {
        std::cout << "all planetary habitability tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
