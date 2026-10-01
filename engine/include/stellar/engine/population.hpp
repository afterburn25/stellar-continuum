#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Population framework — cohort/aggregate demographics for
// civilization-scale simulation. Citizens are NEVER individual entities
// at this layer: a PopulationCohort is a tagged aggregate (species ×
// culture × occupation × education × wealth) carrying a headcount and
// rates. A Population is a deterministic cohort set — births, deaths,
// aging, workforce, migration pressure — advanced in elapsed time, not
// frames. Designed to sit behind SimulationExecutor tiers: a dormant
// colony integrates months of growth in one call.
//
// All arithmetic is expected-value double math in sorted cohort-key
// order — deterministic by construction, no RNG required.

inline constexpr std::size_t kAgeBuckets = 8; // 0-9,10-19,...,60-69,70+

enum class EducationLevel : std::uint8_t {
    None, Basic, Skilled, Advanced, Count
};

enum class WealthBracket : std::uint8_t {
    Destitute, Poor, Standard, Affluent, Elite, Count
};

// Static demographic template — data-driven, one per species/culture
// archetype the game defines.
struct DemographicProfile {
    std::string id;
    double base_fertility_per_year{0.02};   // births per capita per year
    double base_mortality_per_year{0.01};   // deaths per capita per year
    double lifespan_years{80.0};            // shapes age-bucket drain
    double food_per_capita_per_day{1.0};
    double goods_per_capita_per_day{0.1};
    double workforce_participation{0.5};    // fraction able to work
    double migration_tendency{0.5};         // 0 = rooted, 1 = volatile
    double education_progress_per_year{0.05}; // fraction advancing a level
    std::vector<std::string> environment_needs; // tags the habitat must satisfy
};

// The aggregate identity: cohorts with equal keys merge.
struct CohortKey {
    std::string profile;    // DemographicProfile id (species/culture traits)
    std::string culture;    // cultural group tag, "" = none
    std::string occupation; // job role tag, "" = unemployed/none
    EducationLevel education{EducationLevel::Basic};
    WealthBracket wealth{WealthBracket::Standard};

    bool operator==(const CohortKey&) const = default;
};

struct CohortKeyHash {
    std::size_t operator()(const CohortKey& k) const noexcept;
};

// One aggregate slice of a population. Rates/qualities are 0..1 unless
// noted; `size` is the represented headcount.
struct PopulationCohort {
    CohortKey key;
    double size{0.0};
    std::array<double, kAgeBuckets> age_distribution{}; // fractions summing ~1
    double health{0.8};
    double happiness{0.6};
    double morale{0.6};
    double housing_coverage{1.0};   // fraction with housing
    double employment_rate{1.0};    // fraction of workforce employed
    double environment_suitability{1.0}; // habitat fit for this cohort
    double political_tendency{0.0}; // -1..+1 axis, owner-defined meaning
};

// What the settlement/planet currently offers — the inputs demographic
// simulation responds to. Supplied by the colony/environment owner.
struct SettlementConditions {
    double food_ratio{1.0};              // supply ÷ need (1 = fed)
    double goods_ratio{1.0};
    double housing_ratio{1.0};
    double jobs_available{0.0};          // absolute openings this period
    double healthcare{0.5};              // 0..1 coverage
    double overcrowding{0.0};            // >0 means crowded
    double environment_suitability{1.0}; // habitat fit multiplier
    double security{1.0};                // 1 = safe; lower raises mortality
};

struct PopulationDelta {
    double births{0.0};
    double deaths{0.0};
    double educated{0.0};      // cohort-members who advanced a level
    double starvation_deaths{0.0};
    double environmental_deaths{0.0};
    double unemployed{0.0};    // snapshot: workforce without jobs
    // Aggregate headcount that wants to leave under current conditions
    // (informational — actual emigration is explicit: take_emigrants).
    double emigration_pressure{0.0};
};

// Deterministic cohort population. Members are addressed by CohortKey;
// iteration order is always sorted key order.
class Population {
public:
    // Register the static template before adding cohorts that use it.
    void define_profile(DemographicProfile profile); // duplicate id throws
    [[nodiscard]] const DemographicProfile* profile(std::string_view id) const;

    // Adds headcount; merges into an existing cohort with the same key.
    void add(const CohortKey& key, double size);
    void add(const PopulationCohort& cohort); // full-state insert/merge
    void remove(const CohortKey& key, double size); // clamps at zero
    [[nodiscard]] const PopulationCohort* cohort(const CohortKey& key) const;
    [[nodiscard]] std::vector<const PopulationCohort*> cohorts() const; // sorted
    [[nodiscard]] std::size_t cohort_count() const { return cohorts_.size(); }

    // Advance all cohorts by elapsed_days under the given conditions.
    // Returns aggregate flows. Deterministic: sorted iteration, expected
    // values, no randomness.
    PopulationDelta advance(double elapsed_days,
                          const SettlementConditions& conditions);

    // Migration: pressure is computed per cohort from unhappiness,
    // unemployment, overcrowding and the profile's migration tendency
    // (0..1 headcount fraction wanting to leave per year). Destination
    // selection is game-level; take_emigrants removes a deterministic
    // slice carrying cohort state, take_immigrants merges arrivals.
    [[nodiscard]] double migration_pressure(const CohortKey& key,
                                            const SettlementConditions& conditions) const;
    // Removes up to `size` members of the cohort, returns the slice
    // actually removed (carries cohort state for the destination).
    [[nodiscard]] std::optional<PopulationCohort>
    take_emigrants(const CohortKey& key, double size);
    void take_immigrants(const PopulationCohort& arrivals);

    [[nodiscard]] double total() const;
    [[nodiscard]] double workforce() const;      // able-bodied workers
    [[nodiscard]] double employed() const;
    [[nodiscard]] double unemployed() const;
    [[nodiscard]] double average_happiness() const;
    // Resource demand this population generates per day.
    [[nodiscard]] double food_demand_per_day() const;
    [[nodiscard]] double goods_demand_per_day() const;

    // --- persistence -------------------------------------------------
    // Serializable population state: every cohort's identity key and
    // mutable fields. Profiles are definitions — content, re-registered
    // on load like recipes — so the state carries cohorts only.
    struct CohortState {
        CohortKey key;
        double size{0.0};
        std::array<double, kAgeBuckets> age_distribution{};
        double health{0.0};
        double happiness{0.0};
        double morale{0.0};
        double housing_coverage{0.0};
        double employment_rate{0.0};
        double environment_suitability{0.0};
        double political_tendency{0.0};
    };
    struct State {
        std::uint32_t version{1};
        std::vector<CohortState> cohorts; // sorted by CohortKey
    };
    [[nodiscard]] State capture_state() const;
    // Replaces all cohorts with the snapshot. Throws invalid_argument if
    // a cohort references an undefined profile — that is a content
    // mismatch, not a partial-load case.
    void restore_state(const State& state);

private:
    std::unordered_map<std::string, DemographicProfile> profiles_;
    std::unordered_map<CohortKey, PopulationCohort, CohortKeyHash> cohorts_;
};

} // namespace stellar::engine
