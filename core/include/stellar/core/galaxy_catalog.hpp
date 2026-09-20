#pragma once
#include <stellar/core/interstellar_distance.hpp>
#include <stellar/core/legacy_random.hpp>
#include <stellar/core/stellar_object.hpp>
#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/stellar_activity.hpp>
#include <stellar/core/small_body_fields.hpp>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace stellar::core {
// One shared size contract for generation, setup, and bounded presentation input.
inline constexpr std::array<int, 8> full_galaxy_system_counts{250, 500, 1000, 2500, 5000, 10000, 25000, 50000};
inline constexpr int maximum_full_galaxy_system_count = full_galaxy_system_counts.back();
constexpr bool supported_full_galaxy_system_count(std::size_t count) noexcept {
    for (int supported : full_galaxy_system_counts)
        if (count == static_cast<std::size_t>(supported)) return true;
    return false;
}
enum class StellarClass { MRedDwarf, KOrangeDwarf, GYellowDwarf, FYellowWhiteDwarf, AWhiteStar,
    HotBlueStar, Giant, WhiteDwarf, NeutronStar, BlackHole, Protostar, Pulsar };
enum class GalaxyShape { LegacyDisk, BarredSpiral, SolarNeighborhood, FullGalaxy };
enum class StarArchetype { Standard, ResourceRich, HabitableRich, BarrenFrontier, Nebula, NeutronPulsar,
    BlackHole, AncientRuin, Dangerous, Legendary };
struct Vec2 { float x{},y{}; };
struct GalacticCore { Vec2 position; float exclusion_radius{}; };
struct CatalogComponent { int hyg_id{}; std::string name,spectral_type; };
struct CatalogStar {
    int hyg_id{}; std::string name,name_kind; double distance_parsecs{},x{},y{},z{};
    std::string spectral_type; std::vector<CatalogComponent> components;
};
struct StellarSystem {
    int id{}; std::string name; StarPosition position{};
    std::optional<StellarClass> primary,secondary,tertiary;
    std::optional<std::string> catalog_preset_id,stellar_catalog_id;
    StarArchetype archetype{StarArchetype::Standard};
    bool has_habitable_world{},has_anomaly{},has_rare_resource{},has_pre_warp_civilization{};
    std::optional<StellarPhysicalProperties> stellar_object;
    int engulfed_planets{};
    std::optional<StellarRegion> stellar_region;
    std::optional<std::vector<SmallBodyField>> small_body_fields;
    std::optional<StellarOrbitArchitecture> stellar_orbits;
  std::optional<std::vector<StellarActivityState>> stellar_activity;
};
std::optional<StellarClass> classify_spectral_type(const std::string& spectral_type);
std::vector<CatalogStar> load_nearby_catalog(const std::filesystem::path& path);
std::vector<CatalogStar> nearest_classified(std::span<const CatalogStar> catalog,int count);
void apply_catalog_star(StellarSystem& system,const CatalogStar& star);
float full_galaxy_radius(int count);
GalacticCore full_galaxy_core(int count);
std::array<int,12> target_stellar_class_counts(int count);
std::vector<StellarClass> full_galaxy_stellar_classes(std::int64_t seed,int count,std::span<const CatalogStar> measured);
Vec2 next_spatial_position(GalaxyShape shape,float radius,LegacyRandom& random);
std::vector<Vec2> full_galaxy_generated_positions(std::int64_t seed,int count,GalacticCore core,std::span<const CatalogStar> measured);
std::vector<std::string> procedural_system_names(std::int64_t seed,int count);
void apply_stellar_companions(std::int64_t seed,std::vector<StellarSystem>& systems);
// Existing FullGalaxy500 UI defaults; includes the legacy quota/random stream and Sol override.
void apply_full_galaxy_traits(std::int64_t seed,std::vector<StellarSystem>& systems);
// Physical star metadata before civilization-dependent home-system renaming/planet guarantees.
std::vector<StellarSystem> generate_stellar_catalog(std::int64_t seed,int count,std::span<const CatalogStar> catalog);
}
