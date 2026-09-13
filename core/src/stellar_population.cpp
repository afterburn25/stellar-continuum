#include "stellar/core/galaxy_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {
constexpr int kMeasuredSystemCount = 96;
constexpr float kDefaultRadius = 128.0F;
constexpr float kMinimumSpacing = 3.5F;
constexpr double kPi = 3.1415926535897932384626433832795028841971694;
constexpr double kTau = 6.2831853071795864769252867665590057683943388;

void validate_count(int count) {
    if (count != 250 && count != 500 && count != 1000 && count != 2500)
        throw std::invalid_argument("Full-galaxy system count must be 250, 500, 1000, or 2500");
}

float length(Vec2 value) {
    const float squared = value.x * value.x + value.y * value.y;
    return std::sqrt(squared);
}

float distance(Vec2 left, Vec2 right) {
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    return std::sqrt(dx * dx + dy * dy);
}

float distance_squared(Vec2 left, Vec2 right) {
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    return dx * dx + dy * dy;
}

struct Cell {
    int x;
    int y;
    bool operator==(const Cell&) const = default;
};
struct CellHash {
    std::size_t operator()(const Cell& cell) const noexcept {
        const auto x = static_cast<std::uint32_t>(cell.x);
        const auto y = static_cast<std::uint32_t>(cell.y);
        return static_cast<std::size_t>((static_cast<std::uint64_t>(x) << 32U) | y);
    }
};
Cell cell_for(Vec2 value) {
    const double x = static_cast<double>(std::floor(value.x / kMinimumSpacing));
    const double y = static_cast<double>(std::floor(value.y / kMinimumSpacing));
    constexpr int minimum = std::numeric_limits<int>::min() + 1;
    constexpr int maximum = std::numeric_limits<int>::max() - 1;
    if (!std::isfinite(x) || !std::isfinite(y) || x < static_cast<double>(minimum) || x > static_cast<double>(maximum) ||
        y < static_cast<double>(minimum) || y > static_cast<double>(maximum))
        throw std::invalid_argument("Generated stellar position is outside the spacing grid");
    return {static_cast<int>(x), static_cast<int>(y)};
}

bool has_spacing(Vec2 candidate, const std::unordered_map<Cell, std::vector<Vec2>, CellHash>& cells) {
    const Cell center = cell_for(candidate);
    const float minimum_squared = kMinimumSpacing * kMinimumSpacing;
    for (int x = center.x - 1; x <= center.x + 1; ++x) {
        for (int y = center.y - 1; y <= center.y + 1; ++y) {
            const auto found = cells.find({x, y});
            if (found == cells.end()) continue;
            for (const Vec2 other : found->second)
                if (distance_squared(candidate, other) < minimum_squared) return false;
        }
    }
    return true;
}

bool allowed(Vec2 position, Vec2 core, float core_exclusion_radius, float galaxy_radius, float protected_radius) {
    const float galactocentric_distance = distance(position, core);
    return length(position) >= protected_radius && galactocentric_distance >= core_exclusion_radius &&
        galactocentric_distance <= galaxy_radius;
}

void shuffle(std::vector<StellarClass>& values, LegacyRandom& random) {
    for (std::size_t index = values.size(); index > 1; --index) {
        const auto swap = static_cast<std::size_t>(random.next(static_cast<int>(index)));
        std::swap(values[index - 1], values[swap]);
    }
}
} // namespace

float full_galaxy_radius(int count) {
    validate_count(count);
    return kDefaultRadius * std::sqrt(static_cast<float>(count) / 500.0F);
}

GalacticCore full_galaxy_core(int count) {
    const float radius = full_galaxy_radius(count);
    return {{-radius * 0.48F, -radius * 0.20F}, radius * 0.14F};
}

std::array<int, 12> target_stellar_class_counts(int count) {
    validate_count(count);
    // This is dictionary insertion order in the C# source. Pulsar deliberately precedes
    // BlackHole and Protostar here even though its enum ordinal does not.
    constexpr std::array<StellarClass, 12> insertion_order = {StellarClass::MRedDwarf,
        StellarClass::KOrangeDwarf, StellarClass::GYellowDwarf, StellarClass::FYellowWhiteDwarf,
        StellarClass::AWhiteStar, StellarClass::HotBlueStar, StellarClass::Giant, StellarClass::WhiteDwarf,
        StellarClass::NeutronStar, StellarClass::Pulsar, StellarClass::BlackHole, StellarClass::Protostar};
    constexpr std::array<int, 12> reference = {375, 50, 25, 10, 5, 1, 5, 24, 2, 1, 1, 1};
    struct Allocation { StellarClass stellar_class; int count; double remainder; };
    std::array<Allocation, 12> allocations{};
    int allocated = 0;
    for (std::size_t index = 0; index < allocations.size(); ++index) {
        const double exact = static_cast<double>(reference[index]) * static_cast<double>(count) / 500.0;
        const int base = static_cast<int>(std::floor(exact));
        allocations[index] = {insertion_order[index], base, exact - static_cast<double>(base)};
        allocated += base;
    }
    std::array<std::size_t, 12> rank{};
    for (std::size_t index = 0; index < rank.size(); ++index) rank[index] = index;
    std::sort(rank.begin(), rank.end(), [&allocations](std::size_t left, std::size_t right) {
        if (allocations[left].remainder != allocations[right].remainder)
            return allocations[left].remainder > allocations[right].remainder;
        return static_cast<int>(allocations[left].stellar_class) < static_cast<int>(allocations[right].stellar_class);
    });
    for (int index = 0; index < count - allocated; ++index) ++allocations[rank[static_cast<std::size_t>(index)]].count;

    for (const StellarClass rare : {StellarClass::HotBlueStar, StellarClass::Giant, StellarClass::WhiteDwarf,
             StellarClass::NeutronStar, StellarClass::Pulsar, StellarClass::BlackHole, StellarClass::Protostar}) {
        auto allocation = std::find_if(allocations.begin(), allocations.end(), [rare](const Allocation& value) {
            return value.stellar_class == rare;
        });
        if (allocation->count == 0) {
            allocation->count = 1;
            auto dominant = std::find_if(allocations.begin(), allocations.end(), [](const Allocation& value) {
                return value.stellar_class == StellarClass::MRedDwarf;
            });
            --dominant->count;
        }
    }
    std::array<int, 12> result{};
    for (const Allocation& allocation : allocations) result[static_cast<std::size_t>(allocation.stellar_class)] = allocation.count;
    return result;
}

std::vector<StellarClass> full_galaxy_stellar_classes(std::int64_t seed, int count,
    std::span<const CatalogStar> measured) {
    validate_count(count);
    if (measured.size() != kMeasuredSystemCount) throw std::invalid_argument("Full galaxy requires 96 measured stars");
    const auto targets = target_stellar_class_counts(count);
    std::array<int, 12> remainder = targets;
    std::vector<StellarClass> result;
    result.reserve(static_cast<std::size_t>(count));
    for (const CatalogStar& star : measured) {
        const auto stellar_class = classify_spectral_type(star.spectral_type);
        if (!stellar_class || remainder[static_cast<std::size_t>(*stellar_class)] == 0)
            throw std::invalid_argument("Measured nearby stars exceed a full-galaxy stellar quota");
        --remainder[static_cast<std::size_t>(*stellar_class)];
        result.push_back(*stellar_class);
    }
    constexpr std::array<StellarClass, 12> deck_order = {StellarClass::MRedDwarf, StellarClass::KOrangeDwarf,
        StellarClass::GYellowDwarf, StellarClass::FYellowWhiteDwarf, StellarClass::AWhiteStar,
        StellarClass::HotBlueStar, StellarClass::Giant, StellarClass::WhiteDwarf, StellarClass::NeutronStar,
        StellarClass::Pulsar, StellarClass::BlackHole, StellarClass::Protostar};
    std::vector<StellarClass> generated;
    generated.reserve(static_cast<std::size_t>(count - kMeasuredSystemCount));
    for (const StellarClass stellar_class : deck_order)
        generated.insert(generated.end(), static_cast<std::size_t>(remainder[static_cast<std::size_t>(stellar_class)]), stellar_class);
    if (generated.size() != static_cast<std::size_t>(count - kMeasuredSystemCount))
        throw std::logic_error("Full-galaxy stellar quotas do not total the requested system count");
    LegacyRandom random(population_seed(seed, 0x53544152));
    shuffle(generated, random);
    result.insert(result.end(), generated.begin(), generated.end());
    return result;
}

Vec2 next_spatial_position(GalaxyShape shape, float radius, LegacyRandom& random) {
    if (!std::isfinite(radius) || radius <= 0.0F) throw std::invalid_argument("Galaxy radius must be finite and positive");
    const double first = random.next_double();
    const double second = random.next_double();
    const double third = random.next_double();
    if (shape == GalaxyShape::LegacyDisk) {
        const double angle = first * kPi * 2.0;
        const double radial = std::sqrt(second) * static_cast<double>(radius);
        const double jitter = 0.65 + third * 0.35;
        return {static_cast<float>(std::cos(angle) * radial * jitter), static_cast<float>(std::sin(angle) * radial * jitter)};
    }
    constexpr double tilt = -0.26;
    double x = 0.0;
    double y = 0.0;
    if (first < 0.18) {
        const double along = (second * 2.0 - 1.0) * static_cast<double>(radius) * 0.36;
        const double across = (third + first / 0.18 - 1.0) * static_cast<double>(radius) * 0.075;
        x = along * std::cos(tilt) - across * std::sin(tilt);
        y = (along * std::sin(tilt) + across * std::cos(tilt)) * 0.72;
    } else if (first < 0.93) {
        const double arm_sample = second * 4.0;
        const double arm = std::floor(arm_sample);
        const double radial_fraction = 0.20 + 0.72 * std::sqrt((first - 0.18) / 0.75);
        const double angle = arm * kPi * 0.5 + radial_fraction * kPi * 2.35 +
            (third - 0.5) * 0.62 + (arm_sample - arm - 0.5) * 0.18;
        const double radial = static_cast<double>(radius) * radial_fraction;
        x = std::cos(angle) * radial;
        y = std::sin(angle) * radial * 0.72;
    } else {
        const double angle = second * kPi * 2.0;
        const double radial = static_cast<double>(radius) * (0.86 + third * 0.12);
        x = std::cos(angle) * radial;
        y = std::sin(angle) * radial * 0.72;
    }
    return {static_cast<float>(x), static_cast<float>(y)};
}

std::vector<Vec2> full_galaxy_generated_positions(std::int64_t seed, int count, GalacticCore core,
    std::span<const CatalogStar> measured) {
    validate_count(count);
    if (measured.size() != kMeasuredSystemCount) throw std::invalid_argument("Full galaxy requires 96 measured stars");
    if (!std::isfinite(core.position.x) || !std::isfinite(core.position.y) || !std::isfinite(core.exclusion_radius) ||
        core.exclusion_radius <= 0.0F) throw std::invalid_argument("Galactic core must be finite with a positive exclusion radius");
    double furthest = 0.0;
    for (const CatalogStar& star : measured) {
        if (!std::isfinite(star.x) || !std::isfinite(star.y)) throw std::invalid_argument("Measured star position must be finite");
        furthest = std::max(furthest, std::sqrt(star.x * star.x + star.y * star.y));
    }
    const float protected_radius = static_cast<float>(furthest) + kMinimumSpacing;
    const float galaxy_radius = full_galaxy_radius(count);
    const int generated_count = count - kMeasuredSystemCount;
    LegacyRandom random(population_seed(seed, 0x504F534E));
    std::vector<Vec2> positions;
    positions.reserve(static_cast<std::size_t>(generated_count));
    std::unordered_map<Cell, std::vector<Vec2>, CellHash> spacing;
    const int cluster_count = generated_count / 4;
    const int larger_clusters = generated_count % 4;
    for (int cluster_index = 0; cluster_index < cluster_count; ++cluster_index) {
        bool accepted = false;
        for (int attempt = 0; attempt < 2048 && !accepted; ++attempt) {
            Vec2 anchor = next_spatial_position(GalaxyShape::FullGalaxy, galaxy_radius, random);
            anchor.x += core.position.x;
            anchor.y += core.position.y;
            const double rotation = random.next_double() * kTau;
            const int cluster_size = cluster_index < larger_clusters ? 5 : 4;
            std::array<Vec2, 5> cluster{};
            cluster[0] = anchor;
            for (int member = 1; member < cluster_size; ++member) {
                const double angle = rotation + static_cast<double>(member - 1) * kTau /
                    static_cast<double>(cluster_size - 1) + (random.next_double() - 0.5) * 0.18;
                const double member_radius = 4.5 + random.next_double() * 4.0;
                cluster[static_cast<std::size_t>(member)] = {anchor.x + static_cast<float>(std::cos(angle) * member_radius),
                    anchor.y + static_cast<float>(std::sin(angle) * member_radius)};
            }
            bool valid = true;
            for (int member = 0; member < cluster_size && valid; ++member)
                valid = allowed(cluster[static_cast<std::size_t>(member)], core.position, core.exclusion_radius,
                    galaxy_radius, protected_radius) && has_spacing(cluster[static_cast<std::size_t>(member)], spacing);
            for (int member = 0; member < cluster_size && valid; ++member)
                for (int other = 0; other < member; ++other)
                    if (distance_squared(cluster[static_cast<std::size_t>(member)], cluster[static_cast<std::size_t>(other)]) <
                        kMinimumSpacing * kMinimumSpacing) valid = false;
            if (!valid) continue;
            for (int member = 0; member < cluster_size; ++member) {
                const Vec2 position = cluster[static_cast<std::size_t>(member)];
                positions.push_back(position);
                spacing[cell_for(position)].push_back(position);
            }
            accepted = true;
        }
        if (!accepted) throw std::runtime_error("Could not place a bounded full-galaxy stellar region");
    }
    return positions;
}
} // namespace stellar::core
