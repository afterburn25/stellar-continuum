#include <stellar/engine/terraforming.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

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

bool has_tag(const stellar::engine::PlanetEnvironment& e,
             const std::string& t) {
    return std::find(e.tags.begin(), e.tags.end(), t) != e.tags.end();
}

using namespace stellar::engine;

PlanetEnvironment mars() {
    PlanetEnvironment e;
    e.temperature_k = 210.0;
    e.atmosphere_atm = 0.006;
    e.gravity_g = 0.38;
    e.water_fraction = 0.0;
    e.tags = {"dust_world"};
    return e;
}

TerraformProject warm_up() {
    TerraformProject p;
    p.id = "terraform.warm";
    TerraformStage s1;
    s1.id = "greenhouse_release";
    s1.duration_days = 100.0;
    s1.temperature_delta_k = +30.0;
    s1.atmosphere_delta = +0.2;
    s1.add_tags = {"greenhouse_active"};
    p.stages.push_back(s1);
    TerraformStage s2;
    s2.id = "melt_ice";
    s2.duration_days = 50.0;
    s2.temperature_delta_k = +10.0;
    s2.water_delta = +0.3;
    s2.remove_tags = {"dust_world"};
    p.stages.push_back(s2);
    return p;
}

HabitabilityProfile human() {
    HabitabilityProfile p;
    p.id = "species.human";
    p.temperature_min_k = 253.0;
    p.temperature_max_k = 323.0;
    p.atmosphere_min = 0.5;
    p.atmosphere_max = 2.0;
    p.gravity_min_g = 0.5;
    p.gravity_max_g = 1.5;
    p.water_min = 0.2;
    return p;
}

} // namespace

int main() {
    // --- Validation -----------------------------------------------------------------
    {
        Terraforming t;
        bool threw = false;
        try {
            t.define_project(TerraformProject{});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "empty project rejected");
        t.define_project(warm_up());
        threw = false;
        try {
            t.define_project(warm_up());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "duplicate project rejected");
    }

    // --- Staged progression -------------------------------------------------------------
    {
        Terraforming t;
        t.define_project(warm_up());
        t.set_environment(mars());
        check(!t.active(), "inactive before start");
        check(!t.start("nope"), "unknown project rejected");
        check(t.start("terraform.warm"), "project starts");
        check(!t.start("terraform.warm"), "second start while active rejected");

        auto r = t.advance(50.0); // halfway through stage 1 (100d)
        check(near(t.environment().temperature_k, 225.0),
              "temperature interpolates across stage");
        check(near(t.stage_progress(), 0.5), "stage progress 0.5");
        check(r.stages_completed.empty(), "stage not done at half");

        r = t.advance(50.0); // stage 1 done at 100d
        check(r.stages_completed.size() == 1 &&
                  r.stages_completed[0] == "greenhouse_release",
              "stage 1 completes at duration");
        check(has_tag(t.environment(), "greenhouse_active"),
              "stage completion adds tag");
        check(near(t.environment().atmosphere_atm, 0.206, 1e-9),
              "atmosphere delta fully applied");

        r = t.advance(50.0); // stage 2 (50d) completes
        check(r.project_completed, "project completes");
        check(has_tag(t.environment(), "greenhouse_active") &&
                  !has_tag(t.environment(), "dust_world"),
              "tag add/remove applied at completion");
        check(near(t.environment().temperature_k, 250.0),
              "all temperature deltas applied");
        check(near(t.environment().water_fraction, 0.3), "water applied");
        check(!t.active(), "inactive after completion");
    }

    // --- Multi-stage completion in one advance ----------------------------------------------
    {
        Terraforming t;
        t.define_project(warm_up());
        t.set_environment(mars());
        t.start("terraform.warm");
        auto r = t.advance(200.0); // covers both stages (100+50)
        check(r.stages_completed.size() == 2, "both stages complete in one step");
        check(r.project_completed, "project done");
        check(near(t.environment().temperature_k, 250.0),
              "deltas identical regardless of step size");
    }

    // --- Terraforming actually moves habitability ----------------------------------------------
    {
        Terraforming t;
        t.define_project(warm_up());
        t.set_environment(mars());
        const double before = evaluate_habitability(t.environment(), human()).suitability;
        t.start("terraform.warm");
        t.advance(200.0);
        const double after = evaluate_habitability(t.environment(), human()).suitability;
        check(after > before, "terraforming raises suitability");
    }

    // --- Cancel keeps applied deltas ---------------------------------------------------------
    {
        Terraforming t;
        t.define_project(warm_up());
        t.set_environment(mars());
        t.start("terraform.warm");
        t.advance(50.0);
        const double temp_at_cancel = t.environment().temperature_k;
        t.cancel();
        check(!t.active(), "cancel clears active project");
        check(near(t.environment().temperature_k, temp_at_cancel),
              "applied deltas persist through cancel");
        auto r = t.advance(100.0);
        check(r.stages_completed.empty() && !r.project_completed,
              "inactive project advances nothing");
        check(near(t.environment().temperature_k, temp_at_cancel),
              "no drift while inactive");
    }

    // --- Determinism ----------------------------------------------------------------------------
    {
        auto run = [] {
            Terraforming t;
            t.define_project(warm_up());
            t.set_environment(mars());
            t.start("terraform.warm");
            for (int i = 0; i < 10; ++i) t.advance(15.0);
            return t.environment().temperature_k;
        };
        check(run() == run(), "identical runs bit-equal");
    }

    if (failures == 0) {
        std::cout << "all terraforming tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
