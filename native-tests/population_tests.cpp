#include <stellar/engine/population.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

DemographicProfile human() {
    DemographicProfile p;
    p.id = "species.human";
    p.base_fertility_per_year = 0.02;
    p.base_mortality_per_year = 0.01;
    p.lifespan_years = 80.0;
    p.workforce_participation = 0.6;
    p.education_progress_per_year = 0.05;
    return p;
}

CohortKey key(std::string occupation, EducationLevel edu = EducationLevel::Basic,
              WealthBracket w = WealthBracket::Standard) {
    CohortKey k;
    k.profile = "species.human";
    k.occupation = std::move(occupation);
    k.education = edu;
    k.wealth = w;
    return k;
}

SettlementConditions good_conditions(double jobs) {
    SettlementConditions c;
    c.jobs_available = jobs;
    return c;
}

} // namespace

int main() {
    // Growth under good conditions.
    {
        Population pop;
        pop.define_profile(human());
        pop.add(key("miner"), 10000.0);
        const double before = pop.total();
        const auto d = pop.advance(365.0, good_conditions(6000.0));
        check(d.births > 0.0 && d.deaths > 0.0, "births and deaths occur");
        check(d.births > d.deaths, "net growth in good conditions");
        check(std::abs(pop.total() - (before + d.births - d.deaths)) < 1e-6,
              "headcount accounting exact");
    }

    // Starvation and environmental mortality.
    {
        Population pop;
        pop.define_profile(human());
        pop.add(key("miner"), 10000.0);
        SettlementConditions bad;
        bad.food_ratio = 0.2;
        bad.jobs_available = 6000.0;
        const auto d = pop.advance(365.0, bad);
        check(d.starvation_deaths > 0.0, "starvation kills when food short");
        check(d.deaths > d.births, "starving colony shrinks");
        check(pop.cohort(key("miner"))->happiness < 0.6,
              "unhappiness drifts down under shortage");

        Population harsh;
        harsh.define_profile(human());
        auto kh = key("miner");
        harsh.add(kh, 10000.0);
        SettlementConditions bad_env = good_conditions(6000.0);
        bad_env.environment_suitability = 0.3;
        const auto dh = harsh.advance(365.0, bad_env);
        check(dh.environmental_deaths > 0.0, "hostile environment kills");
    }

    // Cohort merge and separate keys.
    {
        Population pop;
        pop.define_profile(human());
        pop.add(key("miner"), 500.0);
        pop.add(key("miner"), 700.0);
        pop.add(key("farmer"), 300.0);
        check(pop.cohort_count() == 2, "equal keys merge into one cohort");
        check(std::abs(pop.cohort(key("miner"))->size - 1200.0) < 1e-9,
              "merged size summed");
    }

    // Workforce and unemployment.
    {
        Population pop;
        pop.define_profile(human());
        pop.add(key("miner"), 10000.0);
        const double wf = pop.workforce();
        check(wf > 0.0 && wf < 10000.0, "workforce is a fraction of pop");
        auto d = pop.advance(365.0, good_conditions(0.0));
        check(d.unemployed > 0.0, "no jobs means unemployment");
        check(pop.unemployed() > 0.0, "unemployment reflected in state");
    }

    // Education progression moves members to a higher-education cohort.
    {
        Population pop;
        pop.define_profile(human());
        pop.add(key("miner", EducationLevel::Basic), 10000.0);
        const auto d = pop.advance(365.0, good_conditions(6000.0));
        check(d.educated > 0.0, "education advances members");
        const auto* skilled =
            pop.cohort(key("miner", EducationLevel::Skilled));
        check(skilled != nullptr && skilled->size > 0.0,
              "skilled cohort appeared");
    }

    // Aging shifts the distribution toward older buckets over decades.
    {
        Population pop;
        pop.define_profile(human());
        PopulationCohort c;
        c.key = key("miner");
        c.size = 10000.0;
        c.age_distribution = {0.5, 0.3, 0.2, 0.0, 0.0, 0.0, 0.0, 0.0};
        pop.add(c);
        const double young_before = pop.cohort(key("miner"))->age_distribution[0] +
                                    pop.cohort(key("miner"))->age_distribution[1];
        for (int i = 0; i < 20; ++i) pop.advance(365.0, good_conditions(6000.0));
        const auto* after = pop.cohort(key("miner"));
        const double young_after =
            after->age_distribution[0] + after->age_distribution[1];
        check(young_after < young_before, "population ages over decades");
        check(after->age_distribution[6] + after->age_distribution[7] > 0.0,
              "elderly buckets populated");
    }

    // Migration: pressure responds to misery; slices carry state.
    {
        Population origin;
        origin.define_profile(human());
        origin.add(key("miner"), 10000.0);
        SettlementConditions bad;
        bad.food_ratio = 0.4;
        bad.jobs_available = 0.0;
        origin.advance(365.0 * 3.0, bad); // let misery build
        const double pressure = origin.migration_pressure(key("miner"), bad);
        check(pressure > 0.0, "miserable cohorts want to leave");

        const double before_take = origin.total();
        const auto slice = origin.take_emigrants(key("miner"), 2000.0);
        check(slice.has_value() && slice->size == 2000.0,
              "emigrant slice removed");
        check(std::abs(origin.total() - (before_take - 2000.0)) < 1e-6,
              "origin decremented by slice size");
        Population dest;
        dest.define_profile(human());
        dest.take_immigrants(*slice);
        check(std::abs(dest.total() - 2000.0) < 1e-9, "immigrants merged");
    }

    // Determinism: identical populations evolve identically.
    {
        const auto run = [] {
            Population pop;
            pop.define_profile(human());
            pop.add(key("miner"), 8000.0);
            pop.add(key("farmer", EducationLevel::None, WealthBracket::Poor),
                    4000.0);
            pop.add(key("technician", EducationLevel::Advanced,
                        WealthBracket::Affluent),
                    1500.0);
            SettlementConditions c = good_conditions(4000.0);
            c.food_ratio = 0.85;
            for (int i = 0; i < 40; ++i) pop.advance(91.25, c);
            double acc = 0.0;
            for (const auto* co : pop.cohorts())
                acc = acc * 1.0000001 + co->size + co->happiness;
            return acc;
        };
        check(run() == run(), "population evolution is deterministic");
    }

    // Scale: 10M+ people across hundreds of cohorts stays cheap —
    // cost scales with cohorts, not headcount.
    {
        Population pop;
        pop.define_profile(human());
        const char* jobs[] = {"miner", "farmer", "technician", "clerk",
                              "engineer", "medic"};
        for (std::size_t i = 0; i < 240; ++i)
            pop.add(key(jobs[i % 6],
                        static_cast<EducationLevel>(i % 4),
                        static_cast<WealthBracket>(i % 5)),
                    50000.0 + i * 1000.0); // ~17.5M total
        SettlementConditions c = good_conditions(5.0e6);
        c.food_ratio = 0.95;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) pop.advance(30.0, c);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check(pop.total() > 0.0, "17M-cohort population survives");
        check(us < 500000, "50 monthly advances of 240 cohorts under 0.5s");
        std::cout << "population_scale: cohorts=" << pop.cohort_count()
                  << " headcount=" << static_cast<std::uint64_t>(pop.total())
                  << " 50x30d_us=" << us << '\n';
    }

    if (failures != 0) {
        std::cerr << failures << " population checks failed\n";
        return 1;
    }
    std::cout << "Population cohort model tests passed\n";
    return 0;
}
