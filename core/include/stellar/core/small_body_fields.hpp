#pragma once
#include <stellar/engine/analytic_orbit.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
enum class SmallBodyFieldType { Rocky,Metallic,Carbonaceous,Mixed,Ice,DebrisDisk,Shattered,CrackedCluster,PlanetaryHalo,Count };
enum class SmallBodyMaterial { Rock,Metal,Carbon,WaterIce,MethaneIce,AmmoniaIce,RockIce,FrozenVolatiles,Count };
enum class SmallBodyResource { Minerals,Metals,Organics,Water,Hydrogen,Deuterium,Volatiles,Salvage,Exotics,Count };
enum class SmallBodyAssetPool { RockyBody,IcyBody,RockDust,IceDust,RockFar,IceFar,IceCluster,DebrisDisk,Shattered,Count };
inline constexpr std::size_t small_body_material_count=8,small_body_resource_count=9;
struct SmallBodyExtraction {
  std::uint32_t body_index{};
  std::array<double,small_body_resource_count> extracted{};
  bool operator==(const SmallBodyExtraction&) const = default;
};
struct SmallBodyField {
  int version{1};
  int id{}; // Stable within the owning system. IDs are never vector indices.
  SmallBodyFieldType type{};
  std::uint64_t seed{};
  double inner_radius_au{},outer_radius_au{},thickness_au{},density{};
  std::uint32_t body_count{},visible_count{};
  std::array<double,small_body_material_count> composition{};
  double tilt{},eccentricity{},clustering{},arc_fraction{1},phase_origin{},epoch_days{},central_mass_solar{1},irregular_fraction{.18};
  double spin_min{.1},spin_max{1},retrograde_fraction{.03};
  std::array<int,4> asset_variants{1,2,3,4};
  SmallBodyAssetPool body_pool{SmallBodyAssetPool::RockyBody},dust_pool{SmallBodyAssetPool::RockDust},far_pool{SmallBodyAssetPool::RockFar};
  int associated_planet_id{-1},associated_belt_parent_id{-1};
  bool planet_centered{};
  std::string rarity_profile{"mature"};
  std::vector<SmallBodyExtraction> extraction;
  bool operator==(const SmallBodyField&) const = default;
};
struct SmallBodyInstance {
  std::uint32_t id{};
  SmallBodyMaterial material{};
  SmallBodyAssetPool asset_pool{};
  int asset_variant_id{};
  stellar::engine::AnalyticOrbit orbit;
  stellar::engine::AnalyticSpin spin;
  double scale{},material_brightness{};
  std::uint32_t local_cluster_id{};
  bool operator==(const SmallBodyInstance&) const = default;
};
struct SmallBodyTypeRule {
  double density_min{},density_max{},clustering{},eccentricity{},thickness{},arc_fraction{1};
  std::uint32_t visible_count{},body_count{};
  std::array<double,small_body_material_count> composition{};
  SmallBodyAssetPool body_pool{},dust_pool{},far_pool{};
};
struct SmallBodyConfiguration {
  std::array<double,5> belt_weights{25,34,14,20,7};
  std::array<double,4> rocky_weights{40,20,20,20};
  double disk_chance{.07},shattered_chance{.04},young_disk_multiplier{3},old_shattered_multiplier{1.5},
      giant_belt_multiplier{1.12},disturbed_multiplier{2},cracked_chance{.025},
      local_cluster_chance{.4},cracked_multiplier{2},cracked_shattered_chance{.32},halo_chance{.45},
      irregular_fraction{.18},spin_min{.1},spin_max{1},orbit_unit{2600},orbit_exponent{.32},planet_scale{1.22},moon_scale{1.18};
  std::array<SmallBodyTypeRule,9> types;
};
struct StellarSystem;
struct PlanetaryBody;
const SmallBodyConfiguration& small_body_configuration();
SmallBodyConfiguration parse_small_body_configuration(std::string_view);
std::string_view small_body_field_name(SmallBodyFieldType);
std::string_view small_body_material_name(SmallBodyMaterial);
std::string_view small_body_resource_name(SmallBodyResource);
std::string_view small_body_asset_name(SmallBodyAssetPool);
double planetary_orbit_au(const StellarSystem&,const PlanetaryBody&);
// Fit a field into a clear stellar orbital gap without changing its identity,
// composition, representative bodies or extraction ledger. Only shattered
// parents can share their own debris band.
void place_small_body_field(const StellarSystem&,std::span<const PlanetaryBody>,SmallBodyField&);
void reconcile_small_body_orbits(std::vector<StellarSystem>&,std::span<const PlanetaryBody>);
void validate_small_body_field(const SmallBodyField&);
void validate_small_body_catalog(std::span<const StellarSystem>,std::span<const PlanetaryBody>);
SmallBodyField make_small_body_field(const StellarSystem&,std::span<const PlanetaryBody>,SmallBodyFieldType,int id,std::uint64_t seed,int parent=-1,const SmallBodyConfiguration& =small_body_configuration());
std::vector<SmallBodyField> generate_small_body_fields(std::int64_t seed,const StellarSystem&,std::span<const PlanetaryBody>,const SmallBodyConfiguration& =small_body_configuration());
// Initialize only missing versioned records. Explicit empty catalogs stay empty.
void initialize_small_body_fields(std::int64_t,std::vector<StellarSystem>&,std::vector<PlanetaryBody>&,bool generate_cracked_worlds=false);
SmallBodyInstance small_body_instance(const SmallBodyField&,std::uint32_t index);
std::vector<SmallBodyInstance> small_body_instances(const SmallBodyField&,std::size_t limit);
struct SmallBodyEnvironment {
  double density{},navigation_multiplier{1},concealment{},hazard{},scan_difficulty{1};
};
SmallBodyEnvironment small_body_environment(const SmallBodyField&,std::array<double,3> position_au,double days);
std::array<double,small_body_resource_count> small_body_resources(const SmallBodyField&,std::uint32_t);
// Authoritative depletion hook. Command callers own access/effort/cargo policy.
double extract_small_body_resource(SmallBodyField&,std::uint32_t,SmallBodyResource,double requested);
}
