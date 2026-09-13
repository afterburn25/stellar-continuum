#include <stellar/core/galaxy_catalog.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {
void validate_count(std::size_t count) {
    if (count != 250 && count != 500 && count != 1000 && count != 2500)
        throw std::invalid_argument("Full-galaxy traits require 250, 500, 1000, or 2500 systems");
}

StarArchetype align_compact_archetype(StellarClass stellar_class, StarArchetype archetype) {
    if (stellar_class == StellarClass::BlackHole) return StarArchetype::BlackHole;
    if (stellar_class == StellarClass::NeutronStar || stellar_class == StellarClass::Pulsar)
        return StarArchetype::NeutronPulsar;
    if (archetype == StarArchetype::BlackHole || archetype == StarArchetype::NeutronPulsar)
        return StarArchetype::Standard;
    return archetype;
}

std::vector<StarArchetype> build_quota_deck(std::size_t count, LegacyRandom& random) {
    // The insertion order is the C# settings dictionary order.
    constexpr std::array<StarArchetype, 10> archetypes = {StarArchetype::Standard, StarArchetype::ResourceRich,
        StarArchetype::HabitableRich, StarArchetype::BarrenFrontier, StarArchetype::Nebula,
        StarArchetype::NeutronPulsar, StarArchetype::BlackHole, StarArchetype::AncientRuin,
        StarArchetype::Dangerous, StarArchetype::Legendary};
    constexpr std::array<int, 10> weights = {46, 12, 10, 8, 6, 5, 3, 4, 4, 2};
    std::array<int, 10> allocation{};
    std::array<double, 10> remainder{};
    int assigned = 0;
    for (std::size_t index = 0; index < archetypes.size(); ++index) {
        const double exact = static_cast<double>(count) * (static_cast<double>(weights[index]) / 100.0);
        allocation[index] = static_cast<int>(exact);
        remainder[index] = exact - static_cast<double>(allocation[index]);
        assigned += allocation[index];
    }
    std::array<std::size_t, 10> rank{};
    for (std::size_t index = 0; index < rank.size(); ++index) rank[index] = index;
    std::stable_sort(rank.begin(), rank.end(), [&remainder](std::size_t left, std::size_t right) {
        return remainder[left] > remainder[right];
    });
    for (std::size_t index = 0; index < count - static_cast<std::size_t>(assigned); ++index) ++allocation[rank[index]];

    std::vector<StarArchetype> deck;
    deck.reserve(count);
    for (std::size_t index = 0; index < archetypes.size(); ++index)
        deck.insert(deck.end(), static_cast<std::size_t>(allocation[index]), archetypes[index]);
    for (std::size_t index = deck.size(); index > 1; --index)
        std::swap(deck[index - 1], deck[static_cast<std::size_t>(random.next(static_cast<int>(index)))]);
    return deck;
}
} // namespace

void apply_full_galaxy_traits(std::int64_t seed, std::vector<StellarSystem>& systems) {
    validate_count(systems.size());
    for (const StellarSystem& system : systems)
        if (!system.primary) throw std::invalid_argument("Full-galaxy traits require a primary stellar class for every system");

    LegacyRandom random(population_seed(seed, 0));
    std::vector<StarArchetype> deck = build_quota_deck(systems.size(), random);
    const auto standard = std::find(deck.begin(), deck.end(), StarArchetype::Standard);
    if (standard == deck.end()) throw std::logic_error("Fresh campaigns need a Standard star for Sol");
    std::swap(deck.front(), *standard);

    for (std::size_t index = 0; index < systems.size(); ++index) {
        StellarSystem& system = systems[index];
        const StarArchetype archetype = align_compact_archetype(*system.primary, deck[index]);
        system.archetype = archetype;
        system.has_habitable_world = archetype == StarArchetype::HabitableRich || random.next_double() < 0.16;
        system.has_anomaly = archetype == StarArchetype::AncientRuin || archetype == StarArchetype::Legendary ||
            random.next_double() < 0.20;
        system.has_rare_resource = archetype == StarArchetype::ResourceRich || random.next_double() < 0.12;
        system.has_pre_warp_civilization = system.has_habitable_world && random.next_double() < 0.04;
    }

    StellarSystem& sol = systems.front();
    sol.archetype = StarArchetype::Standard;
    sol.has_habitable_world = true;
    sol.has_anomaly = false;
    sol.has_rare_resource = false;
    sol.has_pre_warp_civilization = false;
}
} // namespace stellar::core
