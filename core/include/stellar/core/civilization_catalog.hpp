#pragma once
#include <stellar/core/species_environment.hpp>
#include <optional>

namespace stellar::core {
enum class CivilizationArchetype { Adaptive, Mercantile, Scientific, Militarist, Isolationist,
    Territorial, Diplomatic, HonorBound, AncientCustodian, AncientArchivist };
enum class CivilizationDevelopmentStage { PreWarp, WarpCapable, AncientSpacefaring };
struct CivilizationTraits {
    double aggression{},territoriality{},greed{},scientific_curiosity{},risk_tolerance{},survival_priority{};
    bool honor_bound{};
};
struct CivilizationCharacter {
    std::string id,display_name;
    std::optional<std::string> voice_profile_id,portrait;
};
struct CivilizationOffice { std::string office; CivilizationCharacter character; };
struct Civilization {
    int id{}; std::string name; int home_system_id{};
    CivilizationArchetype archetype{}; CivilizationTraits traits;
    bool is_player{}; CivilizationDevelopmentStage development_stage{};
    bool is_seeded_ancient{},expansion_allowed{true},neutral_unless_provoked{};
    std::string species_id;
    // Founding roster in original office insertion order; succession commands remain a later gate.
    std::vector<CivilizationOffice> leadership;
};
std::vector<Civilization> seed_civilizations(std::span<const StellarSystem> systems,
    std::span<const PlanetaryBody> bodies,int pre_warp_count,int ancient_count,std::int64_t seed,
    const std::string& player_species_id="terran_baseline");
std::vector<SpeciesHomeworldAssignment> plan_species_homeworlds_with_nearby_expansion(
    std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies,
    std::span<const std::string> species_ids,int major_civilization_count,int search_limit=100000);
SpeciesHomeworldAssignment resolve_species_homeworld(int civilization_id,const std::string& species_id,
    int system_id,std::span<const PlanetaryBody> bodies);
// Preserves the original policy's transactional greedy fallback and globally unique expansion slots.
std::vector<PlanetaryBody> apply_nearby_habitable_guarantees(std::int64_t seed,
    std::span<StellarSystem> systems,std::span<const PlanetaryBody> bodies,
    std::span<const Civilization> civilizations,int guaranteed_worlds_per_civilization=2);
struct FoundingCatalog {
    std::vector<StellarSystem> systems;
    std::vector<PlanetaryBody> bodies;
    std::vector<Civilization> civilizations;
    bool used_constrained_home_fallback{};
};
// Stages after the physical catalog and before ColonySeeder; not a complete campaign or save.
FoundingCatalog create_founding_catalog(std::int64_t seed,std::span<const StellarSystem> physical_systems,
    int pre_warp_count=6,int ancient_count=1,const std::string& player_species_id="terran_baseline");
}
