#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/legacy_random.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace stellar::core {
namespace {
constexpr std::string_view terran = "terran_baseline";

struct Template {
    const char* name;
    CivilizationArchetype archetype;
    CivilizationTraits traits;
};

constexpr Template pre_warp_templates[] = {
    {"Aster Union", CivilizationArchetype::Adaptive, {.35,.25,.30,.55,.45,1.00,false}},
    {"Kesh Exchange", CivilizationArchetype::Mercantile, {.20,.15,.80,.45,.30,1.00,false}},
    {"Velari Institute", CivilizationArchetype::Scientific, {.18,.20,.25,.95,.25,1.00,false}},
    {"Dravak Compact", CivilizationArchetype::Militarist, {.82,.58,.30,.30,.62,1.00,false}},
    {"Orryn Enclave", CivilizationArchetype::Isolationist, {.12,.55,.18,.58,.18,1.00,false}},
    {"Tarkesh Reach", CivilizationArchetype::Territorial, {.56,.92,.38,.32,.48,1.00,false}},
    {"Seren Accord", CivilizationArchetype::Diplomatic, {.12,.08,.22,.52,.22,1.00,false}},
    {"Kor Vow", CivilizationArchetype::HonorBound, {.66,.38,.16,.28,.78,.88,true}},
    {"Namar Coalition", CivilizationArchetype::Adaptive, {.28,.24,.42,.48,.38,1.00,false}},
    {"Ilyr Concord", CivilizationArchetype::Diplomatic, {.10,.12,.30,.60,.20,1.00,false}},
    {"Vask Dominion", CivilizationArchetype::Militarist, {.76,.62,.24,.26,.58,1.00,false}},
    {"Pelagos Combine", CivilizationArchetype::Mercantile, {.18,.18,.76,.50,.26,1.00,false}},
    {"Thren Observatory", CivilizationArchetype::Scientific, {.14,.16,.22,.90,.24,1.00,false}},
};
constexpr Template ancient_templates[] = {
    {"Aurelian Custodians", CivilizationArchetype::AncientCustodian, {.08,.08,.05,.96,.10,1.00,false}},
    {"Veyr Archive", CivilizationArchetype::AncientArchivist, {.04,.04,.04,1.00,.08,1.00,false}},
    {"Orison Keepers", CivilizationArchetype::AncientCustodian, {.10,.06,.03,.92,.12,1.00,false}},
};

std::int32_t seeder_seed(std::int64_t seed) {
    // Only the low 32 bits survive the explicit C# int cast.  Unsigned arithmetic provides
    // the required unchecked long multiplication without C++ signed-overflow UB.
    const auto bits = std::bit_cast<std::uint64_t>(seed);
    const auto value = bits * 397ULL ^ (bits >> 32U) ^ 0x51A7C0DEULL;
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

template <std::size_t N>
std::vector<const Template*> randomized_deck(const Template (&templates)[N], int take, LegacyRandom& random) {
    // Enumerable.Take(0) is deferred and never enumerates OrderBy, so an empty ancient deck
    // consumes no random values at all.
    if (take == 0) return {};
    std::vector<std::pair<int, const Template*>> keyed;
    keyed.reserve(N);
    // LINQ OrderBy evaluates its selector once in source order and is stable.
    for (const auto& item : templates) keyed.emplace_back(random.next(), &item);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<const Template*> result;
    result.reserve(static_cast<std::size_t>(take));
    for (int index = 0; index < take; ++index) result.push_back(keyed[static_cast<std::size_t>(index)].second);
    return result;
}

std::vector<CivilizationOffice> founding_roster(int civilization_id, bool human) {
    struct Office { const char* office; const char* human_name; const char* other_name; };
    static constexpr Office offices[] = {
        {"FleetCommander", "Commander Elena Voss", "Fleet Commander"},
        {"ChiefScientist", "Dr. Amara Chen", "Chief Scientist"},
        {"Diplomat", "Ambassador Mara Okafor", "Diplomatic Envoy"},
        {"Governor", "Governor Elias Ward", "Governor"},
        {"EconomicAdvisor", "Economic Advisor", "Economic Advisor"},
        {"OperationsOfficer", "Operations Officer", "Operations Officer"},
        {"ExpeditionCommander", "Expedition Commander", "Expedition Commander"},
    };
    std::vector<CivilizationOffice> result;
    result.reserve(std::size(offices));
    for (const auto& entry : offices) {
        const std::string id = "civ-" + std::to_string(civilization_id) + ":" + entry.office + ":founder";
        result.push_back({entry.office, {id, human ? entry.human_name : entry.other_name, {}, {}}});
    }
    return result;
}
} // namespace

std::vector<Civilization> seed_civilizations(std::span<const StellarSystem> systems,
    std::span<const PlanetaryBody> bodies, int pre_warp_count, int ancient_count, std::int64_t seed,
    const std::string& player_species_id) {
    if (pre_warp_count < 1 || pre_warp_count > static_cast<int>(std::size(pre_warp_templates)))
        throw std::out_of_range{"pre_warp_count"};
    if (ancient_count < 0 || ancient_count > static_cast<int>(std::size(ancient_templates)))
        throw std::out_of_range{"ancient_count"};
    (void)species_environment_profile(player_species_id); // Preserve the C# unknown-player validation.
    const int civilization_count = pre_warp_count + ancient_count;
    if (systems.size() < static_cast<std::size_t>(civilization_count))
        throw std::invalid_argument{"There are fewer star systems than seeded civilizations."};

    const bool canonical_starts = std::any_of(systems.begin(), systems.end(), [](const StellarSystem& system) {
        return system.catalog_preset_id && *system.catalog_preset_id == sol_catalog_preset_id;
    });
    std::vector<std::string> species_ids;
    species_ids.reserve(static_cast<std::size_t>(civilization_count));
    for (int id = 0; id < civilization_count; ++id) species_ids.push_back(assign_species(seed, id, canonical_starts));
    const int player_id = player_species_id == terran || pre_warp_count == 1 ? 0 : 1;
    if (canonical_starts && player_species_id != terran) species_ids[static_cast<std::size_t>(player_id)] = player_species_id;
    const auto homes = plan_species_homeworlds(systems, bodies, species_ids);

    LegacyRandom random(seeder_seed(seed));
    const auto pre_warp_deck = randomized_deck(pre_warp_templates, pre_warp_count, random);
    const auto ancient_deck = randomized_deck(ancient_templates, ancient_count, random);
    std::vector<Civilization> result;
    result.reserve(static_cast<std::size_t>(civilization_count));
    for (int index = 0; index < pre_warp_count; ++index) {
        const auto& item = *pre_warp_deck[static_cast<std::size_t>(index)];
        const auto& home = homes[static_cast<std::size_t>(index)];
        const bool human = species_ids[static_cast<std::size_t>(index)] == terran;
        result.push_back({index, canonical_starts && human && index == 0 ? "Human Commonwealth" : item.name,
            home.system_id, item.archetype, item.traits, index == player_id, CivilizationDevelopmentStage::PreWarp,
            false, true, false, species_ids[static_cast<std::size_t>(index)], founding_roster(index, human)});
    }
    for (int index = 0; index < ancient_count; ++index) {
        const int id = pre_warp_count + index;
        const auto& item = *ancient_deck[static_cast<std::size_t>(index)];
        const bool human = species_ids[static_cast<std::size_t>(id)] == terran;
        result.push_back({id, item.name, homes[static_cast<std::size_t>(id)].system_id, item.archetype, item.traits,
            false, CivilizationDevelopmentStage::AncientSpacefaring, true, false, true,
            species_ids[static_cast<std::size_t>(id)], founding_roster(id, human)});
    }
    return result;
}
} // namespace stellar::core
