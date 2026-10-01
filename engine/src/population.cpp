#include <stellar/engine/population.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace stellar::engine {

namespace {

constexpr double kDaysPerYear = 365.0;

// Mortality contribution rates (fraction of cohort per year) at full
// severity of each stressor.
constexpr double kStarvationRate = 0.5;
constexpr double kEnvironmentalRate = 0.1;
constexpr double kOvercrowdingRate = 0.02;
constexpr double kInsecurityRate = 0.05;
constexpr double kUnhousedRate = 0.01;
// Healthcare reduces effective mortality by up to 30%.
constexpr double kHealthcareRelief = 0.3;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

bool cohort_less(const CohortKey& a, const CohortKey& b) {
    return std::tie(a.profile, a.culture, a.occupation, a.education, a.wealth) <
           std::tie(b.profile, b.culture, b.occupation, b.education, b.wealth);
}

// Working-age fraction: buckets 2..5 (ages 20-69 of 8 ten-year buckets).
double working_age_fraction(const PopulationCohort& c) {
    double sum = 0.0;
    for (std::size_t i = 2; i <= 5; ++i) sum += c.age_distribution[i];
    return sum;
}

void normalize_age(PopulationCohort& c) {
    double sum = 0.0;
    for (const double f : c.age_distribution) sum += f;
    if (sum > 0.0) {
        for (double& f : c.age_distribution) f /= sum;
    } else {
        // Default: uniform-ish working-age-heavy distribution.
        c.age_distribution = {0.08, 0.10, 0.18, 0.18, 0.18, 0.14, 0.08, 0.06};
    }
}

// Deterministic mix for CohortKey hashing.
std::uint64_t hash_mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t hash_string(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace

std::size_t CohortKeyHash::operator()(const CohortKey& k) const noexcept {
    std::uint64_t h = hash_string(k.profile);
    h = hash_mix(h, hash_string(k.culture));
    h = hash_mix(h, hash_string(k.occupation));
    h = hash_mix(h, static_cast<std::uint64_t>(k.education));
    h = hash_mix(h, static_cast<std::uint64_t>(k.wealth));
    return static_cast<std::size_t>(h);
}

void Population::define_profile(DemographicProfile profile) {
    if (profile.id.empty())
        throw std::invalid_argument("Population profile id empty");
    if (!profiles_.emplace(profile.id, std::move(profile)).second)
        throw std::invalid_argument("Population duplicate profile id");
}

const DemographicProfile* Population::profile(std::string_view id) const {
    const auto it = profiles_.find(std::string(id));
    return it == profiles_.end() ? nullptr : &it->second;
}

void Population::add(const CohortKey& key, double size) {
    if (!profiles_.count(key.profile))
        throw std::invalid_argument("Population cohort references unknown profile");
    if (size < 0.0)
        throw std::invalid_argument("Population negative cohort size");
    auto& c = cohorts_[key];
    c.key = key;
    c.size += size;
    if (c.size > 0.0) normalize_age(c);
}

void Population::add(const PopulationCohort& cohort) {
    if (!profiles_.count(cohort.key.profile))
        throw std::invalid_argument("Population cohort references unknown profile");
    if (cohort.size < 0.0)
        throw std::invalid_argument("Population negative cohort size");
    auto it = cohorts_.find(cohort.key);
    if (it == cohorts_.end()) {
        auto c = cohort;
        if (c.size > 0.0) normalize_age(c);
        cohorts_.emplace(c.key, c);
        return;
    }
    // Merge: weighted-average qualities, summed size.
    auto& c = it->second;
    const double total = c.size + cohort.size;
    if (total > 0.0) {
        const double w = cohort.size / total;
        c.health += (cohort.health - c.health) * w;
        c.happiness += (cohort.happiness - c.happiness) * w;
        c.morale += (cohort.morale - c.morale) * w;
        c.housing_coverage += (cohort.housing_coverage - c.housing_coverage) * w;
        c.employment_rate += (cohort.employment_rate - c.employment_rate) * w;
        c.environment_suitability +=
            (cohort.environment_suitability - c.environment_suitability) * w;
        for (std::size_t i = 0; i < kAgeBuckets; ++i)
            c.age_distribution[i] +=
                (cohort.age_distribution[i] - c.age_distribution[i]) * w;
    }
    c.size = total;
}

void Population::remove(const CohortKey& key, double size) {
    const auto it = cohorts_.find(key);
    if (it == cohorts_.end()) return;
    it->second.size = std::max(0.0, it->second.size - size);
}

const PopulationCohort* Population::cohort(const CohortKey& key) const {
    const auto it = cohorts_.find(key);
    return it == cohorts_.end() ? nullptr : &it->second;
}

std::vector<const PopulationCohort*> Population::cohorts() const {
    std::vector<const PopulationCohort*> out;
    out.reserve(cohorts_.size());
    for (const auto& [_, c] : cohorts_) out.push_back(&c);
    std::sort(out.begin(), out.end(),
              [](const PopulationCohort* a, const PopulationCohort* b) {
                  return cohort_less(a->key, b->key);
              });
    return out;
}

PopulationDelta Population::advance(double elapsed_days,
                                    const SettlementConditions& conditions) {
    PopulationDelta delta;
    if (elapsed_days <= 0.0) return delta;
    const double years = elapsed_days / kDaysPerYear;

    // Settlement-wide employment ratio applied equally to every cohort's
    // workforce (equal-share model; skill-weighted allocation is a
    // future layer).
    const double total_workforce = workforce();
    const double employment_target =
        total_workforce > 0.0
            ? clamp01(conditions.jobs_available / total_workforce)
            : 1.0;

    // Education moves are collected, then applied — mutating cohorts_
    // mid-pass would corrupt deterministic iteration.
    struct EducationMove {
        CohortKey from;
        PopulationCohort slice;
    };
    std::vector<EducationMove> moves;

    // Sorted iteration: unordered_map order is stable within a process
    // but not a portable contract — delta accumulation and education
    // moves must be bit-identical across runs and platforms.
    std::vector<CohortKey> order;
    order.reserve(cohorts_.size());
    for (const auto& [key, _] : cohorts_) order.push_back(key);
    std::sort(order.begin(), order.end(), cohort_less);

    for (const CohortKey& key : order) {
        auto& c = cohorts_.at(key);
        const DemographicProfile& p = profiles_.at(key.profile);
        if (c.size <= 0.0) continue;

        // --- mortality -------------------------------------------------
        const double starvation = (1.0 - clamp01(conditions.food_ratio));
        const double environmental =
            (1.0 - clamp01(conditions.environment_suitability *
                           c.environment_suitability));
        const double unhoused = 1.0 - clamp01(conditions.housing_ratio);
        const double insecurity = 1.0 - clamp01(conditions.security);
        double mortality = p.base_mortality_per_year +
                           starvation * kStarvationRate +
                           environmental * kEnvironmentalRate +
                           std::max(0.0, conditions.overcrowding) *
                               kOvercrowdingRate +
                           insecurity * kInsecurityRate +
                           unhoused * kUnhousedRate;
        mortality *= 1.0 - kHealthcareRelief * clamp01(conditions.healthcare);
        const double deaths = c.size * mortality * years;
        delta.deaths += deaths;
        delta.starvation_deaths += c.size * starvation * kStarvationRate * years;
        delta.environmental_deaths +=
            c.size * environmental * kEnvironmentalRate * years;

        // --- fertility -------------------------------------------------
        const double fertility = p.base_fertility_per_year *
                                 clamp01(conditions.food_ratio) *
                                 (0.5 + 0.5 * clamp01(conditions.housing_ratio)) *
                                 clamp01(conditions.environment_suitability *
                                         c.environment_suitability);
        const double births = c.size * fertility * years;
        delta.births += births;

        // --- aging -----------------------------------------------------
        // Buckets drain toward older ages; births land in bucket 0 and
        // deaths draw proportionally from all buckets (age-dependent
        // mortality curves are a profile refinement).
        const double bucket_width_years =
            std::max(1.0, p.lifespan_years / static_cast<double>(kAgeBuckets));
        const double shift = std::min(1.0, years / bucket_width_years);
        for (std::size_t i = kAgeBuckets - 1; i > 0; --i) {
            const double moved = c.age_distribution[i - 1] * shift;
            c.age_distribution[i - 1] -= moved;
            c.age_distribution[i] += moved;
        }
        const double post = c.size - deaths + births;
        if (post > 0.0) {
            // Newborns enter bucket 0; deaths drain uniformly.
            for (double& f : c.age_distribution) f *= (c.size - deaths);
            c.age_distribution[0] += births;
            for (double& f : c.age_distribution) f /= post;
        }
        c.size = std::max(0.0, post);

        // --- quality drift --------------------------------------------
        const double employment_now =
            employment_target; // equal-share across cohorts
        // Weighted comfort scaled by a hard food cap — a starving colony
        // cannot be happy regardless of housing and jobs.
        const double comfort =
            clamp01(0.25 * clamp01(conditions.food_ratio) +
                    0.25 * clamp01(conditions.housing_ratio) +
                    0.25 * employment_now +
                    0.15 * clamp01(conditions.security) +
                    0.10 * clamp01(conditions.environment_suitability *
                                   c.environment_suitability) -
                    0.20 * std::max(0.0, conditions.overcrowding));
        const double happiness_target =
            comfort * (0.3 + 0.7 * clamp01(conditions.food_ratio));
        const double drift = std::min(1.0, years * 0.5);
        c.happiness += (happiness_target - c.happiness) * drift;
        const double health_target =
            clamp01(0.5 * clamp01(conditions.healthcare) +
                    0.3 * clamp01(conditions.food_ratio) +
                    0.2 * clamp01(conditions.environment_suitability *
                                  c.environment_suitability));
        c.health += (health_target - c.health) * drift;
        c.morale += (c.happiness - c.morale) * drift;
        c.employment_rate += (employment_now - c.employment_rate) * drift;
        c.housing_coverage = clamp01(conditions.housing_ratio);
        delta.unemployed +=
            c.size * p.workforce_participation * working_age_fraction(c) *
            (1.0 - c.employment_rate);

        // --- education progression -------------------------------------
        if (c.size > 0.0 && c.health > 0.0 &&
            key.education != EducationLevel::Advanced) {
            const double advance_count =
                c.size * p.education_progress_per_year * years *
                clamp01(conditions.food_ratio);
            if (advance_count > 0.0) {
                PopulationCohort slice = c;
                slice.size = advance_count;
                slice.key.education = static_cast<EducationLevel>(
                    static_cast<int>(key.education) + 1);
                moves.push_back({key, slice});
                c.size -= advance_count;
                delta.educated += advance_count;
            }
        }

        delta.emigration_pressure +=
            c.size * migration_pressure(key, conditions);
    }

    for (auto& move : moves) {
        auto& dest = cohorts_[move.slice.key];
        if (dest.size <= 0.0) {
            dest = move.slice;
        } else {
            const double total = dest.size + move.slice.size;
            for (std::size_t i = 0; i < kAgeBuckets; ++i)
                dest.age_distribution[i] =
                    (dest.age_distribution[i] * dest.size +
                     move.slice.age_distribution[i] * move.slice.size) /
                    total;
            dest.size = total;
        }
        dest.key = move.slice.key;
    }
    return delta;
}

double Population::migration_pressure(
    const CohortKey& key, const SettlementConditions& conditions) const {
    const auto it = cohorts_.find(key);
    if (it == cohorts_.end() || it->second.size <= 0.0) return 0.0;
    const auto& c = it->second;
    const DemographicProfile& p = profiles_.at(key.profile);
    const double push =
        clamp01(0.4 * (1.0 - c.happiness) + 0.3 * (1.0 - c.employment_rate) +
                0.2 * std::max(0.0, conditions.overcrowding) +
                0.1 * (1.0 - clamp01(conditions.food_ratio)));
    return p.migration_tendency * push;
}

std::optional<PopulationCohort>
Population::take_emigrants(const CohortKey& key, double size) {
    const auto it = cohorts_.find(key);
    if (it == cohorts_.end() || size <= 0.0) return std::nullopt;
    auto& c = it->second;
    const double taken = std::min(size, c.size);
    if (taken <= 0.0) return std::nullopt;
    c.size -= taken;
    PopulationCohort slice = c;
    slice.size = taken;
    return slice;
}

void Population::take_immigrants(const PopulationCohort& arrivals) {
    if (arrivals.size <= 0.0) return;
    add(arrivals);
}

double Population::total() const {
    double sum = 0.0;
    for (const auto& [_, c] : cohorts_) sum += c.size;
    return sum;
}

double Population::workforce() const {
    double sum = 0.0;
    for (const auto& [key, c] : cohorts_)
        sum += c.size * profiles_.at(key.profile).workforce_participation *
               working_age_fraction(c);
    return sum;
}

double Population::employed() const {
    double sum = 0.0;
    for (const auto& [key, c] : cohorts_)
        sum += c.size * profiles_.at(key.profile).workforce_participation *
               working_age_fraction(c) * c.employment_rate;
    return sum;
}

double Population::unemployed() const { return workforce() - employed(); }

double Population::average_happiness() const {
    double sum = 0.0, n = 0.0;
    for (const auto& [_, c] : cohorts_) {
        sum += c.happiness * c.size;
        n += c.size;
    }
    return n > 0.0 ? sum / n : 0.0;
}

double Population::food_demand_per_day() const {
    double sum = 0.0;
    for (const auto& [key, c] : cohorts_)
        sum += c.size * profiles_.at(key.profile).food_per_capita_per_day;
    return sum;
}

Population::State Population::capture_state() const {
    State state;
    state.cohorts.reserve(cohorts_.size());
    for (const PopulationCohort* c : cohorts()) {
        CohortState s;
        s.key = c->key;
        s.size = c->size;
        s.age_distribution = c->age_distribution;
        s.health = c->health;
        s.happiness = c->happiness;
        s.morale = c->morale;
        s.housing_coverage = c->housing_coverage;
        s.employment_rate = c->employment_rate;
        s.environment_suitability = c->environment_suitability;
        s.political_tendency = c->political_tendency;
        state.cohorts.push_back(std::move(s));
    }
    return state;
}

void Population::restore_state(const State& state) {
    cohorts_.clear();
    for (const CohortState& s : state.cohorts) {
        if (!profiles_.count(s.key.profile))
            throw std::invalid_argument(
                "Population snapshot references undefined profile");
        PopulationCohort c;
        c.key = s.key;
        c.size = s.size;
        c.age_distribution = s.age_distribution;
        c.health = s.health;
        c.happiness = s.happiness;
        c.morale = s.morale;
        c.housing_coverage = s.housing_coverage;
        c.employment_rate = s.employment_rate;
        c.environment_suitability = s.environment_suitability;
        c.political_tendency = s.political_tendency;
        cohorts_.emplace(c.key, c);
    }
}

double Population::goods_demand_per_day() const {
    double sum = 0.0;
    for (const auto& [key, c] : cohorts_)
        sum += c.size * profiles_.at(key.profile).goods_per_capita_per_day;
    return sum;
}

} // namespace stellar::engine
