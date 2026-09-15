#include <stellar/core/planetary_catalog.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace stellar::core {
namespace {
enum class Profile { TemperateSurfaceWater, HighPressureOcean, HighGravitySurface, CryogenicHydrocarbon };

std::optional<Profile> resolve_profile(int system_id) {
    const int cycle = ((system_id % 8) + 8) % 8;
    switch (cycle) {
    case 0: return Profile::TemperateSurfaceWater;
    case 1: return Profile::HighPressureOcean;
    case 2: return Profile::HighGravitySurface;
    case 3: return Profile::CryogenicHydrocarbon;
    default: return std::nullopt;
    }
}

double jitter(std::int64_t campaign_seed, int body_id, int salt, double center, double half_range) {
    std::uint64_t mixed = static_cast<std::uint64_t>(campaign_seed);
    const auto incremented_body_id = static_cast<std::int32_t>(static_cast<std::uint32_t>(body_id) + 1U);
    const auto incremented_salt = static_cast<std::int32_t>(static_cast<std::uint32_t>(salt) + 1U);
    mixed ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(incremented_body_id)) * 0x9E3779B97F4A7C15ULL;
    mixed ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(incremented_salt)) * 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 30U;
    mixed *= 0xBF58476D1CE4E5B9ULL;
    mixed ^= mixed >> 27U;
    mixed *= 0x94D049BB133111EBULL;
    mixed ^= mixed >> 31U;
    const double unit = static_cast<double>(mixed >> 11U) * (1.0 / static_cast<double>(1ULL << 53U));
    return center + (unit * 2.0 - 1.0) * half_range;
}

PlanetaryBody apply_profile(std::int64_t seed, PlanetaryBody body, Profile profile) {
    double gravity = 0.0;
    double radius = 0.0;
    PlanetaryEnvironment environment{};
    switch (profile) {
    case Profile::TemperateSurfaceWater:
        gravity = jitter(seed, body.id, 11, 1.00, 0.05);
        radius = jitter(seed, body.id, 21, 1.00, 0.07);
        environment = {gravity, jitter(seed, body.id, 31, 288.0, 4.0), jitter(seed, body.id, 32, 101.3, 8.0),
            PlanetaryAtmosphereRegime::OxygenNitrogen, PlanetarySolventRegime::Water,
            jitter(seed, body.id, 33, 0.06, 0.02), false, true};
        break;
    case Profile::HighPressureOcean:
        gravity = jitter(seed, body.id, 12, 0.85, 0.05);
        radius = jitter(seed, body.id, 22, 0.95, 0.07);
        environment = {gravity, jitter(seed, body.id, 41, 282.0, 4.0), jitter(seed, body.id, 42, 350.0, 24.0),
            PlanetaryAtmosphereRegime::OxygenNitrogen, PlanetarySolventRegime::Water,
            jitter(seed, body.id, 43, 0.08, 0.025), true, true};
        break;
    case Profile::HighGravitySurface:
        gravity = jitter(seed, body.id, 13, 1.75, 0.08);
        radius = jitter(seed, body.id, 23, 1.00, 0.06);
        environment = {gravity, jitter(seed, body.id, 51, 300.0, 4.0), jitter(seed, body.id, 52, 160.0, 12.0),
            PlanetaryAtmosphereRegime::OxygenRich, PlanetarySolventRegime::Water,
            jitter(seed, body.id, 53, 0.10, 0.025), false, true};
        break;
    case Profile::CryogenicHydrocarbon:
        gravity = jitter(seed, body.id, 14, 0.14, 0.02);
        radius = jitter(seed, body.id, 24, 0.85, 0.06);
        environment = {gravity, jitter(seed, body.id, 61, 94.0, 4.0), jitter(seed, body.id, 62, 150.0, 12.0),
            PlanetaryAtmosphereRegime::Reducing, PlanetarySolventRegime::Hydrocarbon,
            jitter(seed, body.id, 63, 0.12, 0.025), false, true};
        break;
    }
    body.radius_earth = radius;
    body.mass_earth = std::max(0.0005, gravity * radius * radius);
    body.environment = environment;
    validate_planetary_body(body);
    return body;
}

bool preserves_identity(const PlanetaryBody& before, const PlanetaryBody& after) {
    return before.id == after.id && before.system_id == after.system_id && before.parent_body_id == after.parent_body_id &&
        before.orbit_index == after.orbit_index && before.kind == after.kind && before.name == after.name &&
        before.legacy_colonization_candidate == after.legacy_colonization_candidate &&
        before.has_rare_resource == after.has_rare_resource && before.has_anomaly == after.has_anomaly &&
        before.has_pre_warp_civilization == after.has_pre_warp_civilization;
}
} // namespace

std::vector<PlanetaryBody> apply_environmental_diversity(std::int64_t seed, std::span<const StellarSystem> systems,
    std::span<const PlanetaryBody> bodies) {
    std::unordered_map<int, PlanetaryBody> result;
    result.reserve(bodies.size());
    for (const PlanetaryBody& body : bodies) {
        if (!result.emplace(body.id, body).second)
            throw std::invalid_argument("Environmental diversity requires unique planetary body IDs");
    }

    std::vector<const StellarSystem*> ordered_systems;
    ordered_systems.reserve(systems.size());
    for (const StellarSystem& system : systems) ordered_systems.push_back(&system);
    std::stable_sort(ordered_systems.begin(), ordered_systems.end(), [](const StellarSystem* left, const StellarSystem* right) {
        return left->id < right->id;
    });
    for (const StellarSystem* system : ordered_systems) {
        if (system->catalog_preset_id) continue;
        const auto profile = resolve_profile(system->id);
        if (!profile) continue;

        const PlanetaryBody* target = nullptr;
        for (const PlanetaryBody& body : bodies) {
            if (body.system_id != system->id || body.kind != PlanetaryBodyKind::Planet) continue;
            if (!target || std::tuple{body.has_pre_warp_civilization, !body.environment.has_solid_surface,
                               !body.legacy_colonization_candidate, body.id} <
                    std::tuple{target->has_pre_warp_civilization, !target->environment.has_solid_surface,
                        !target->legacy_colonization_candidate, target->id})
                target = &body;
        }
        if (!target) {
            if (system->primary || system->stellar_catalog_id) continue;
            throw std::runtime_error("System has no planet available for its diversity anchor");
        }
        result.at(target->id) = apply_profile(seed, *target, *profile);
    }

    std::vector<PlanetaryBody> ordered;
    ordered.reserve(bodies.size());
    for (const PlanetaryBody& body : bodies) ordered.push_back(result.at(body.id));
    if (ordered.size() != bodies.size()) throw std::runtime_error("Environmental diversity conditioning changed planetary body count");
    for (std::size_t index = 0; index < bodies.size(); ++index)
        if (!preserves_identity(bodies[index], ordered[index]))
            throw std::runtime_error("Environmental diversity conditioning changed planetary body identity or non-environment facts");
    return ordered;
}
} // namespace stellar::core
