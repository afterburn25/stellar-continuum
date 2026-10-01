#include <stellar/engine/population.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// Population scale benchmark — Milestone 3 coverage.
//
// Usage: stellar_population_scale_tests <settlements> <cohorts_each>
//        <headcount_each> [ticks]
//
// Models `settlements` independent Population instances, each carrying
// `cohorts_each` cohorts totalling `headcount_each` people, and advances
// every settlement `ticks` times (30 days per advance — the strategic
// cadence the executor would use for Background colonies). Headcount is
// a scalar on aggregate cohorts, so cost scales with cohort/settlement
// count, not raw citizens — exactly what the milestone requires.

namespace {

using namespace stellar::engine;

struct ScenarioResult {
    std::vector<std::uint64_t> advance_ns;
    double total_population{};
    double checksum{};
    std::uint64_t births{};
    std::uint64_t deaths{};
};

// Deterministic scenario builder: N settlements × C cohorts.
std::vector<Population> build(std::size_t settlements, std::size_t cohorts,
                              double headcount) {
    std::vector<Population> out(settlements);
    static const char* kOccupations[] = {"miner", "farmer", "technician",
                                         "clerk", "laborer", "specialist"};
    static const char* kCultures[] = {"core", "frontier", "exile", "union"};
    for (std::size_t s = 0; s < settlements; ++s) {
        DemographicProfile profile;
        profile.id = "species.human";
        out[s].define_profile(profile);
        DemographicProfile adapted;
        adapted.id = "species.adapted";
        adapted.base_fertility_per_year = 0.03;
        adapted.environment_needs = {"low_gravity"};
        out[s].define_profile(adapted);

        for (std::size_t i = 0; i < cohorts; ++i) {
            CohortKey key;
            key.profile = (i % 7 == 0) ? "species.adapted" : "species.human";
            key.culture = kCultures[i % 4];
            key.occupation = kOccupations[i % 6];
            key.education =
                static_cast<EducationLevel>(i % 4); // None..Advanced
            key.wealth = static_cast<WealthBracket>(i % 5);
            // Stagger headcounts deterministically across cohorts.
            const double share =
                headcount * (1.0 + static_cast<double>(i % 5) * 0.25) /
                (static_cast<double>(cohorts) * 2.0);
            out[s].add(key, share);
        }
    }
    return out;
}

ScenarioResult run_scenario(std::size_t settlements, std::size_t cohorts,
                            double headcount, int ticks) {
    auto pops = build(settlements, cohorts, headcount);
    SettlementConditions conditions;
    conditions.food_ratio = 0.95;
    conditions.goods_ratio = 0.9;
    conditions.housing_ratio = 0.85;
    conditions.jobs_available = headcount * 0.4;
    conditions.healthcare = 0.6;
    conditions.overcrowding = 0.15;
    conditions.environment_suitability = 0.9;
    conditions.security = 0.95;

    ScenarioResult result;
    result.advance_ns.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        for (auto& pop : pops) {
            const PopulationDelta delta = pop.advance(30.0, conditions);
            result.births += static_cast<std::uint64_t>(delta.births);
            result.deaths += static_cast<std::uint64_t>(delta.deaths);
        }
        result.advance_ns.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count()));
    }
    for (const auto& pop : pops) {
        result.total_population += pop.total();
        result.checksum += pop.total() + pop.average_happiness() * 1000.0 +
                           pop.workforce() * 0.5;
    }
    return result;
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto idx = static_cast<std::size_t>(
        std::clamp(p * static_cast<double>(values.size() - 1), 0.0,
                   static_cast<double>(values.size() - 1)));
    return values[idx];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " <settlements> <cohorts_each> <headcount_each> [ticks]\n";
        return 2;
    }
    const std::size_t settlements = std::stoull(argv[1]);
    const std::size_t cohorts = std::stoull(argv[2]);
    const double headcount = std::stod(argv[3]);
    const int ticks = argc > 4 ? std::atoi(argv[4]) : 60;

    const ScenarioResult a =
        run_scenario(settlements, cohorts, headcount, ticks);
    // Determinism: an identical fresh run must match bit-for-bit.
    const ScenarioResult b =
        run_scenario(settlements, cohorts, headcount, 5);

    const double total_headcount =
        static_cast<double>(settlements) * headcount;
    double mean = 0.0;
    for (const std::uint64_t ns : a.advance_ns) mean += static_cast<double>(ns);
    mean /= static_cast<double>(a.advance_ns.size());
    const std::uint64_t p95 = percentile(a.advance_ns, 0.95);
    const std::uint64_t p99 = percentile(a.advance_ns, 0.99);
    const std::uint64_t peak =
        *std::max_element(a.advance_ns.begin(), a.advance_ns.end());

    std::cout << "population scale: " << settlements << " settlements x "
              << cohorts << " cohorts, total headcount " << total_headcount
              << ", " << ticks << " ticks\n"
              << "  advance mean " << mean / 1e3 << " us, p95 "
              << p95 / 1e3 << " us, p99 " << p99 / 1e3 << " us, peak "
              << peak / 1e3 << " us\n"
              << "  final population " << a.total_population
              << ", births " << a.births << ", deaths " << a.deaths << '\n';

    // Re-run determinism check on overlapping tick count.
    const ScenarioResult expected_partial =
        run_scenario(settlements, cohorts, headcount, 5);
    if (b.checksum != expected_partial.checksum ||
        b.total_population != expected_partial.total_population) {
        std::cerr << "FAIL: repeat run diverged\n";
        return 1;
    }
    if (a.total_population <= 0.0) {
        std::cerr << "FAIL: population collapsed\n";
        return 1;
    }
    std::cout << "Population scale benchmark passed (deterministic repeat "
                 "verified)\n";
    return 0;
}
