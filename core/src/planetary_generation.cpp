#include <stellar/core/planetary_catalog.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
constexpr int body_id_stride = 1000;

class StableRandom {
public:
    static StableRandom for_system(std::int64_t campaign_seed, int system_id) {
        auto seed = std::bit_cast<std::uint64_t>(campaign_seed);
        const auto increment = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(system_id) + 1U);
        seed ^= static_cast<std::uint64_t>(increment) * 0x9E3779B97F4A7C15ULL;
        seed ^= seed >> 30; seed *= 0xBF58476D1CE4E5B9ULL;
        seed ^= seed >> 27; seed *= 0x94D049BB133111EBULL;
        seed ^= seed >> 31;
        return StableRandom{seed};
    }
    double next_double() { return static_cast<double>(next_u64() >> 11) * (1.0 / (1ULL << 53)); }
    int next_int(int max_exclusive) {
        if (max_exclusive <= 1) return 0;
        return static_cast<int>(next_u64() % static_cast<std::uint32_t>(max_exclusive));
    }
    double range(double minimum, double maximum) { return minimum + (maximum - minimum) * next_double(); }
private:
    explicit StableRandom(std::uint64_t state) : state_{state == 0 ? 0x9E3779B97F4A7C15ULL : state} {}
    std::uint64_t next_u64() {
        auto x = state_; x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        state_ = x; return x * 0x2545F4914F6CDD1DULL;
    }
    std::uint64_t state_;
};

double base_radiation(StarArchetype archetype) {
    switch (archetype) {
    case StarArchetype::NeutronPulsar: return 0.62;
    case StarArchetype::BlackHole: return 0.45;
    case StarArchetype::Dangerous: return 0.36;
    case StarArchetype::Nebula: return 0.18;
    default: return 0.06;
    }
}
double base_radiation(const StellarSystem& system) {
    double stellar = 0.06;
    if (system.primary) switch (*system.primary) {
    case StellarClass::NeutronStar: case StellarClass::Pulsar: stellar = 0.62; break;
    case StellarClass::BlackHole: stellar = 0.45; break;
    case StellarClass::HotBlueStar: stellar = 0.40; break;
    case StellarClass::Protostar: stellar = 0.31; break;
    case StellarClass::Giant: stellar = 0.22; break;
    case StellarClass::WhiteDwarf: stellar = 0.18; break;
    default: break;
    }
    return std::max(base_radiation(system.archetype), stellar);
}

std::string planet_name(std::int64_t seed, int system_id, int orbit) {
    static constexpr const char* names[] = {"Aestra", "Aion", "Arden", "Caelia", "Caligo", "Ceryn", "Damaris", "Eidra",
        "Elara", "Eryon", "Hesper", "Ilyra", "Kaelis", "Liora", "Maeron", "Neris", "Orison", "Phaedra", "Quillon", "Rhyssa",
        "Sereph", "Talora", "Thane", "Umbriel", "Vesper", "Viridia", "Xanthe", "Yarrow", "Zephra", "Aurelia", "Corven", "Pelagos"};
    auto mixed = std::bit_cast<std::uint64_t>(seed) ^ static_cast<std::uint64_t>(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(system_id) + 1U)) * 0x9E3779B97F4A7C15ULL;
    mixed ^= 0x504C414EU; mixed ^= mixed >> 30; mixed *= 0xBF58476D1CE4E5B9ULL; mixed ^= mixed >> 27;
    const auto offset = static_cast<int>(mixed % std::size(names));
    return names[(offset + orbit * 7) % std::size(names)];
}
std::string moon_name(std::int64_t seed, const PlanetaryBody& parent, int moon_index) {
    static constexpr const char* epithets[] = {"Ari", "Belen", "Cira", "Dysis", "Enna", "Faron", "Galen", "Hira", "Ione", "Jora",
        "Kora", "Lume", "Mira", "Noma", "Oryn", "Prax", "Quill", "Rhea", "Sola", "Tarin", "Una", "Vela", "Wren", "Xira",
        "Yana", "Zori", "Aven", "Brin", "Cyra", "Doran", "Eris", "Fira"};
    auto mixed = std::bit_cast<std::uint64_t>(seed) ^ static_cast<std::uint64_t>(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(parent.id) + 1U)) * 0x9E3779B97F4A7C15ULL;
    mixed ^= 0x4D4F4F4EU; mixed ^= mixed >> 30; mixed *= 0xBF58476D1CE4E5B9ULL; mixed ^= mixed >> 27;
    const auto offset = static_cast<int>(mixed % std::size(epithets));
    return parent.name + " " + epithets[(offset + moon_index * 5) % std::size(epithets)];
}

PlanetaryAtmosphereRegime rocky_atmosphere(double mass, double temperature, StableRandom& random) {
    if (mass < .10 || (mass < .32 && random.next_double() < .72)) return PlanetaryAtmosphereRegime::Vacuum;
    if (temperature > 430.0) return random.next_double() < .70 ? PlanetaryAtmosphereRegime::CarbonDioxideRich : PlanetaryAtmosphereRegime::Other;
    if (temperature < 150.0) return random.next_double() < .62 ? PlanetaryAtmosphereRegime::Inert : PlanetaryAtmosphereRegime::Reducing;
    const auto roll = random.next_double();
    if (roll < .34) return PlanetaryAtmosphereRegime::CarbonDioxideRich;
    if (roll < .57) return PlanetaryAtmosphereRegime::Reducing;
    if (roll < .78) return PlanetaryAtmosphereRegime::Inert;
    if (roll < .92) return PlanetaryAtmosphereRegime::Other;
    return PlanetaryAtmosphereRegime::OxygenNitrogen;
}
double rocky_pressure(PlanetaryAtmosphereRegime atmosphere, double mass, StableRandom& random) {
    if (atmosphere == PlanetaryAtmosphereRegime::Vacuum) return 0;
    const auto retention = std::clamp(.35 + mass * .55, .25, 2.8);
    switch (atmosphere) {
    case PlanetaryAtmosphereRegime::CarbonDioxideRich: return random.range(8, 1200) * retention;
    case PlanetaryAtmosphereRegime::Reducing: return random.range(2, 420) * retention;
    case PlanetaryAtmosphereRegime::Inert: return random.range(.4, 260) * retention;
    case PlanetaryAtmosphereRegime::OxygenNitrogen: return random.range(45, 165) * retention;
    case PlanetaryAtmosphereRegime::OxygenRich: return random.range(35, 145) * retention;
    default: return random.range(.2, 310) * retention;
    }
}
PlanetarySolventRegime solvent(double temperature, double pressure, StableRandom& random) {
    if (pressure <= .01 || random.next_double() < .42) return PlanetarySolventRegime::None;
    if (temperature >= 250 && temperature <= 390) return random.next_double() < .58 ? PlanetarySolventRegime::Water : PlanetarySolventRegime::None;
    if (temperature >= 165 && temperature < 250) return random.next_double() < .42 ? PlanetarySolventRegime::Ammonia : PlanetarySolventRegime::None;
    if (temperature >= 70 && temperature < 165) return random.next_double() < .46 ? PlanetarySolventRegime::Hydrocarbon : PlanetarySolventRegime::None;
    return random.next_double() < .04 ? PlanetarySolventRegime::Other : PlanetarySolventRegime::None;
}

PlanetaryBody create_planet(std::int64_t seed, int id, const StellarSystem& system, int orbit, bool legacy, bool rare, bool anomaly, bool pre_warp, StableRandom& random) {
    const auto name = system.primary ? planet_name(seed, system.id, orbit) : system.name + " " + static_cast<char>('b' + orbit);
    if (legacy) {
        const auto radius = random.range(.78, 1.28), gravity = random.range(.72, 1.30), mass = gravity * radius * radius;
        return {id, system.id, {}, orbit, name, PlanetaryBodyKind::Planet, radius, mass,
            {gravity, random.range(255, 310), random.range(62, 155), random.next_double() < .82 ? PlanetaryAtmosphereRegime::OxygenNitrogen : PlanetaryAtmosphereRegime::OxygenRich,
             PlanetarySolventRegime::Water, std::clamp(base_radiation(system) + random.range(0, .10), 0., .35), random.next_double() < .18, true},
            true, rare, anomaly, pre_warp};
    }
    const auto fraction = orbit / 7.0;
    if (random.next_double() < std::clamp(.10 + fraction * .36, .10, .46)) {
        const auto radius = random.range(3.4, 10.8), mass = random.range(18, 320), gravity = std::clamp(mass / (radius * radius), .55, 3.8);
        const auto temperature = std::clamp(420.0 - orbit * 48.0 + random.range(-55, 55), 35., 650.);
        const auto atmosphere = random.next_double() < .72 ? PlanetaryAtmosphereRegime::Reducing : PlanetaryAtmosphereRegime::Inert;
        const auto selected_solvent = temperature < 135 && random.next_double() < .45 ? PlanetarySolventRegime::Hydrocarbon : PlanetarySolventRegime::None;
        return {id, system.id, {}, orbit, name, PlanetaryBodyKind::Planet, radius, mass,
            {gravity, temperature, random.range(7000, 180000), atmosphere, selected_solvent,
             std::clamp(base_radiation(system) + random.range(.08, .32), 0., 1.), false, false}, false, rare, anomaly, false};
    }
    const auto radius = random.range(.30, 1.95), density = random.range(.55, 1.55), mass = std::max(.01, radius * radius * radius * density);
    const auto gravity = std::clamp(mass / (radius * radius), .03, 3.2), temperature = std::clamp(445.0 - orbit * 54.0 + random.range(-70, 70), 28., 760.);
    const auto atmosphere = rocky_atmosphere(mass, temperature, random);
    const auto pressure = rocky_pressure(atmosphere, mass, random);
    const auto selected_solvent = solvent(temperature, pressure, random);
    const auto immersed = selected_solvent != PlanetarySolventRegime::None && random.next_double() < .13;
    return {id, system.id, {}, orbit, name, PlanetaryBodyKind::Planet, radius, mass,
        {gravity, temperature, pressure, atmosphere, selected_solvent, std::clamp(base_radiation(system) + (pressure < 5 ? .20 : .04) + random.range(0, .22), 0., 1.),
         immersed, true}, false, rare, anomaly, false};
}

PlanetaryBody create_moon(std::int64_t seed, int id, const PlanetaryBody& parent, int index, bool proper_name, StableRandom& random) {
    const auto radius = random.range(.07, std::min(.78, std::max(.13, parent.radius_earth * .22)));
    const auto mass = std::max(.0005, radius * radius * radius * random.range(.55, 1.35));
    const auto gravity = std::clamp(mass / (radius * radius), .005, .75), temperature = std::clamp(parent.environment.temperature_kelvin + random.range(-22, 22), 18., 780.);
    const auto atmosphere = random.next_double() < .78 ? PlanetaryAtmosphereRegime::Vacuum : (random.next_double() < .55 ? PlanetaryAtmosphereRegime::Inert : PlanetaryAtmosphereRegime::Reducing);
    const auto pressure = atmosphere == PlanetaryAtmosphereRegime::Vacuum ? 0. : random.range(.05, 18.);
    const auto selected_solvent = solvent(temperature, pressure, random);
    return {id, parent.system_id, parent.id, index, proper_name ? moon_name(seed, parent, index) : parent.name + "-" + std::to_string(index + 1), PlanetaryBodyKind::Moon, radius, mass,
        {gravity, temperature, pressure, atmosphere, selected_solvent, std::clamp(parent.environment.radiation_hazard + random.range(.02, .24), 0., 1.), false, true}, false, false, false, false};
}

int moon_count(const PlanetaryBody& planet, StableRandom& random) {
    if (planet.kind != PlanetaryBodyKind::Planet) return 0;
    if (!planet.environment.has_solid_surface) return random.next_int(4);
    if (planet.mass_earth > 1.4 && random.next_double() < .55) return 1 + random.next_int(2);
    return random.next_double() < .32 ? 1 : 0;
}
int planet_count(StarArchetype archetype, StableRandom& random) {
    const int minimum = archetype == StarArchetype::BlackHole || archetype == StarArchetype::NeutronPulsar ? 1 : 2;
    const int maximum_exclusive = archetype == StarArchetype::Nebula ? 6 : 8;
    return minimum + random.next_int(std::max(1, maximum_exclusive - minimum));
}

std::optional<std::unordered_map<int, int>> balanced_counts(std::int64_t seed, std::span<const StellarSystem> systems) {
    const int count = static_cast<int>(systems.size());
    const int scale = count % 100 == 0 && count >= 500 && count <= 2500 &&
        (std::all_of(systems.begin(), systems.end(), [](const auto& s) { return s.stellar_catalog_id.has_value(); }) ||
         std::all_of(systems.begin(), systems.end(), [](const auto& s) { return s.primary.has_value(); })) ? count / 100 : 1;
    if (count != 100 * scale || (scale == 1 && std::any_of(systems.begin(), systems.end(), [](const auto& s) { return !s.primary; }))) return std::nullopt;
    LegacyRandom random{population_seed(seed, 0x504C4E54)};
    std::vector<const StellarSystem*> non_sol;
    for (const auto& system : systems) if (!system.catalog_preset_id || *system.catalog_preset_id != sol_catalog_preset_id) non_sol.push_back(&system);
    for (int index = static_cast<int>(non_sol.size()) - 1; index > 0; --index) std::swap(non_sol[index], non_sol[random.next(index + 1)]);
    std::vector<const StellarSystem*> zero_candidates;
    for (const auto* system : non_sol) if (!system->has_habitable_world) zero_candidates.push_back(system);
    std::stable_sort(zero_candidates.begin(), zero_candidates.end(), [](const auto* left, const auto* right) {
        const auto priority = [](const StellarSystem* system) { return !system->has_habitable_world && system->primary &&
            (*system->primary == StellarClass::BlackHole || *system->primary == StellarClass::NeutronStar || *system->primary == StellarClass::Pulsar || *system->primary == StellarClass::Protostar) ? 0 : 1; };
        return priority(left) < priority(right);
    });
    std::unordered_set<int> zero;
    for (const auto* system : zero_candidates) if (static_cast<int>(zero.size()) < 18 * scale) zero.insert(system->id);
    if (static_cast<int>(zero.size()) != 18 * scale) throw std::invalid_argument{"Balanced planetary architecture needs the required non-habitable planetless systems."};
    std::unordered_map<int, int> result;
    for (const auto id : zero) result.emplace(id, 0);
    std::vector<const StellarSystem*> populated;
    for (const auto* system : non_sol) if (!zero.contains(system->id)) populated.push_back(system);
    std::vector<int> deck;
    for (int index = 0; index < 22 * scale; ++index) deck.push_back(1 + index % 2);
    for (int index = 0; index < 42 * scale; ++index) deck.push_back(3 + index % 4);
    for (int index = 0; index < 14 * scale - 1; ++index) deck.push_back(7 + index % 4);
    for (int index = 0; index < 4 * scale; ++index) deck.push_back(11 + index % 4);
    for (int index = static_cast<int>(deck.size()) - 1; index > 0; --index) std::swap(deck[index], deck[random.next(index + 1)]);
    for (std::size_t index = 0; index < populated.size(); ++index) result[populated[index]->id] = deck[index];
    result[sol_system_id] = 8;
    return result;
}

void validate_catalog(std::span<const PlanetaryBody> bodies, std::span<const StellarSystem> systems) {
    std::unordered_set<int> ids, system_ids;
    for (const auto& system : systems) system_ids.insert(system.id);
    for (const auto& body : bodies) {
        if (!ids.insert(body.id).second) throw std::invalid_argument{"Duplicate planetary body ID."};
        if (!system_ids.contains(body.system_id)) throw std::invalid_argument{"Planetary body references unknown system."};
    }
    std::unordered_map<int, const PlanetaryBody*> by_id;
    for (const auto& body : bodies) by_id.emplace(body.id, &body);
    for (const auto& moon : bodies) if (moon.kind == PlanetaryBodyKind::Moon) {
        if (!moon.parent_body_id || !by_id.contains(*moon.parent_body_id)) throw std::invalid_argument{"Moon has no valid parent body."};
        const auto& parent = *by_id.at(*moon.parent_body_id);
        if (parent.kind != PlanetaryBodyKind::Planet || parent.system_id != moon.system_id) throw std::invalid_argument{"Moon parent is not a planet in the same system."};
    }
    for (const auto& system : systems) if (system.has_habitable_world && !std::any_of(bodies.begin(), bodies.end(), [&](const auto& body) {
        return body.system_id == system.id && body.legacy_colonization_candidate;
    })) throw std::invalid_argument{"Legacy habitable system has no compatibility colony candidate."};
}
int checked_body_id(int system_id, int local_id) {
    const auto value = static_cast<std::int64_t>(system_id) * body_id_stride + local_id;
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) throw std::overflow_error{"Planetary body ID overflow."};
    return static_cast<int>(value);
}
} // namespace

std::vector<PlanetaryBody> generate_planetary_catalog(std::int64_t seed, std::span<const StellarSystem> systems) {
    std::vector<PlanetaryBody> result;
    result.reserve(systems.size() * 8);
    const auto counts = balanced_counts(seed, systems);
    std::vector<const StellarSystem*> ordered;
    for (const auto& system : systems) ordered.push_back(&system);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) { return left->id < right->id; });
    for (const auto* system : ordered) {
        if (system->catalog_preset_id) {
            if (*system->catalog_preset_id != sol_catalog_preset_id) throw std::invalid_argument{"Unknown planetary catalog preset."};
            const auto sol = create_sol_catalog(*system); result.insert(result.end(), sol.begin(), sol.end()); continue;
        }
        auto random = StableRandom::for_system(seed, system->id);
        const int planets = counts ? counts->at(system->id) : planet_count(system->archetype, random);
        const int legacy_orbit = system->has_habitable_world ? random.next_int(planets) : -1;
        const int rare_orbit = system->has_rare_resource ? random.next_int(planets) : -1;
        const int anomaly_orbit = system->has_anomaly ? random.next_int(planets) : -1;
        int local_id = 1;
        for (int orbit = 0; orbit < planets; ++orbit) {
            const auto planet = create_planet(seed, checked_body_id(system->id, local_id++), *system, orbit, orbit == legacy_orbit, orbit == rare_orbit, orbit == anomaly_orbit,
                system->has_pre_warp_civilization && orbit == legacy_orbit, random);
            validate_planetary_body(planet); result.push_back(planet);
            const auto moons = moon_count(planet, random);
            for (int moon = 0; moon < moons; ++moon) {
                const auto body = create_moon(seed, checked_body_id(system->id, local_id++), planet, moon, system->primary.has_value(), random);
                validate_planetary_body(body); result.push_back(body);
            }
        }
    }
    const auto conditioned = apply_environmental_diversity(seed, systems, result);
    validate_catalog(conditioned, systems);
    return conditioned;
}
} // namespace stellar::core
