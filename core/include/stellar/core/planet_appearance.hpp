#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {
enum class PlanetClass { Barren, Desert, Frozen, Ocean, Temperate, Tundra, Volcanic,
  Greenhouse, Carbon, SuperEarth, MiniNeptune, GasGiant, IceGiant, HotJupiter, Chthonian, Cracked, Count };
inline constexpr std::size_t planet_class_count=static_cast<std::size_t>(PlanetClass::Count);
// Irradiation zones use equilibrium temperature, so their orbital distances
// follow the actual star and albedo rather than a universal AU table.
enum class PlanetOrbitalZone { DeepCold, Cold, Temperate, Warm, Hot };
struct PlanetOrbitalZoneDefinition {PlanetOrbitalZone zone{};std::string id,name;std::array<double,2> equilibrium_kelvin;};
struct PlanetAtmosphereRules {
  std::array<double,2> pressure_kpa;double molecular_mass{},minimum_retention{},retention_check_above_kpa{10};
  std::string composition_rule;
};
struct PlanetClassDefinition {
  PlanetClass type{};std::string id,name;double weight{};
  std::array<double,2> mass,radius,temperature,pressure;
  double albedo{},greenhouse_coefficient{};
  bool solid{};
  PlanetAtmosphereRules atmosphere_rules;
};
struct PlanetSubclassDefinition {
  std::string id,name,composition,heat_source,history,temperate_group;
  PlanetClass primary{};std::array<double,2> temperature;
  double water{},ice{},vegetation{},clouds{},emission{};
  bool rings{};
  std::vector<PlanetOrbitalZone> valid_orbital_zones;
  bool water_allowed{},ice_allowed{},volcanism_allowed{};
  double generation_weight{1};std::string atmosphere_rule;
  bool generation_enabled{true};std::string cloud_profile;double storm_activity{.5};
};
struct PlanetArtDefinition {
  std::string id,material_id,source_filename,sha256,subclass;
  PlanetClass primary{};std::vector<PlanetClass> compatible;
  bool liquid{},ice{},vegetation{},clouds{},emission{},rings{};
};
struct RejectedPlanetArtDefinition {
  std::string id,source_filename,sha256,subclass,reason,duplicate_of,audited_subclass;
  PlanetClass primary{};bool earth_geography{};
};
// Resolved view of the normalized definitions. No duplicated editable image
// lists: pools are indexed from the accepted/rejected registries once.
struct PlanetTypeRecord {
  PlanetClass base_class{};std::string subclass,name;
  std::vector<PlanetOrbitalZone> valid_orbital_zones;
  std::array<double,2> surface_temperature_kelvin;
  std::string heat_rule;PlanetAtmosphereRules atmosphere_rules;
  bool water_allowed{},ice_allowed{},volcanism_allowed{};
  double class_percentage{},subclass_weight{},within_class_percentage{},baseline_percentage{};
  std::vector<std::string> accepted_image_pool,compatible_image_pool,rejected_image_pool;
};
struct PlanetClimate {
  double bond_albedo{},equilibrium_kelvin{},greenhouse_kelvin{},internal_flux_wm2{},
    snow_line_au{},retention_parameter{},surface_water{},surface_ice{},vegetation{};
  std::string heat_source{"stellar"},history{"undisturbed"};
  bool migrated{};
  bool operator==(const PlanetClimate&)const=default;
};
struct PlanetAtmosphereAppearance {
  std::array<double,3> color{.28,.55,.85};double density{},haze{},cloud_opacity{},cloud_period_days{1};
  bool operator==(const PlanetAtmosphereAppearance&)const=default;
};
struct PlanetRingAppearance {
  bool enabled{};double inner_radius{1.25},outer_radius{2.1},density{.55};
  std::array<double,3> color{.73,.69,.59};std::string composition{"water-ice-and-dust"};
  int version{};std::uint64_t seed{};std::string family,asset_id,significance{"none"},origin;
  double thickness{.0001},optical_depth{.55},particle_density{1000},reflectivity{.5},
    equilibrium_kelvin{},roche_radius{},plane_tilt_radians{},plane_node_radians{};
  bool operator==(const PlanetRingAppearance&)const=default;
};
struct PlanetGiantProfile {
  int version{};std::string atmospheric_composition,cloud_profile;
  double storm_activity{},formation_snow_line_au{};
  bool operator==(const PlanetGiantProfile&)const=default;
};
struct PlanetAppearance {
  std::string version{"planet-appearance-v1"};std::uint64_t visual_seed{};
  std::string source_asset_id,material_id,subclass;PlanetClass primary_class{};
  std::vector<PlanetClass> compatible_classes;
  PlanetClimate climate;PlanetAtmosphereAppearance atmosphere;PlanetRingAppearance rings;
  double axial_tilt_radians{},axis_node_radians{},rotation_period_days{1},initial_phase_radians{},
    oblateness{},terrain_height{.0004},emission_strength{};
  bool developer_example{};
  // Unset in legacy saves; resolved from parent/period metadata by Core.
  std::optional<bool> tidally_locked;
  PlanetGiantProfile giant;
  bool operator==(const PlanetAppearance&)const=default;
};
struct PlanetaryBody;struct StellarSystem;struct StellarPhysicalProperties;
struct RingFamilyDefinition {
  std::string id,name,composition;double bond_albedo{},particle_density{},maximum_kelvin{},weight{};bool fragmented{};
};
struct RingArtDefinition {std::string id,family,source_filename,sha256,rejection_reason;int variant{};bool accepted{};double source_inner_fraction{},source_outer_fraction{};};
const std::vector<RingFamilyDefinition>& ring_family_definitions();
const std::vector<RingArtDefinition>& ring_art_definitions();
double planet_rotational_oblateness(double mass_earth,double radius_earth,double period_days);
double planetary_roche_radius(double mass_earth,double radius_earth,double particle_density);
double planet_ring_probability(const PlanetaryBody&,const StellarPhysicalProperties*,std::size_t moon_count=0);
bool ring_family_thermally_valid(const RingFamilyDefinition&,double incident_flux,double eccentricity=0);
// Seeded Core policy; optional forced family/variant serves the same validated QA command path.
PlanetRingAppearance make_planet_ring(const PlanetaryBody&,const PlanetAppearance&,const StellarPhysicalProperties*,
  std::size_t moon_count=0,std::string_view forced_family={},int variant=-1);
void validate_planet_ring(const PlanetRingAppearance&);
void migrate_giant_appearance(PlanetaryBody&);
bool planet_is_tidally_locked(const PlanetaryBody&,const StellarPhysicalProperties*);
const std::vector<PlanetClassDefinition>& planet_class_definitions();
const std::vector<PlanetSubclassDefinition>& planet_subclass_definitions();
const std::vector<PlanetArtDefinition>& planet_art_definitions();
const std::vector<RejectedPlanetArtDefinition>& rejected_planet_art_definitions();
const std::vector<PlanetOrbitalZoneDefinition>& planet_orbital_zone_definitions();
PlanetOrbitalZone planet_orbital_zone(double equilibrium_kelvin);
const PlanetOrbitalZoneDefinition& planet_orbital_zone_definition(PlanetOrbitalZone);
const std::vector<PlanetTypeRecord>& planet_type_registry();
const PlanetTypeRecord& planet_type_record(PlanetClass,std::string_view subclass);
const PlanetClassDefinition& planet_class_definition(PlanetClass);
const PlanetSubclassDefinition& planet_subclass_definition(PlanetClass,std::string_view);
PlanetClass planet_class_from_id(std::string_view);
std::array<double,planet_class_count> planet_baseline_weights();
// Thermal estimates are a bounded game climate model, not a general circulation simulation.
double planet_equilibrium_temperature(double flux,double bond_albedo);
double planet_atmosphere_retention(double mass,double radius,double kelvin,double molecular_mass,
  const StellarPhysicalProperties&,double incident_flux);
bool planet_liquid_water_possible(double kelvin,double pressure_kpa);
struct PlanetEligibility { PlanetClass type{};double weight{};std::string reason; };
std::vector<PlanetEligibility> planet_class_eligibility(const PlanetaryBody&,const StellarPhysicalProperties&);
// New generation only: preserves IDs/orbits and never re-rolls an existing appearance.
void generate_planet_appearances(std::int64_t seed,std::span<const StellarSystem>,std::vector<PlanetaryBody>&);
// Repair legacy cold primary worlds placed at warm stellar distances. Preserve
// primary environments, artwork and stable IDs; update the whole moon family.
void reconcile_frozen_planet_orbits(std::span<const StellarSystem>,std::vector<PlanetaryBody>&);
// Assign a visual to existing physical state without changing the saved environment.
PlanetAppearance planet_appearance_for_existing(std::uint64_t seed,const PlanetaryBody&,const StellarPhysicalProperties* = nullptr);
// Produces a fully checked QA planet around the supplied real star. No world mutation.
PlanetaryBody make_planet_type_example(std::uint64_t seed,int body_id,int system_id,int orbit_index,
  const StellarPhysicalProperties&,PlanetClass,std::string_view subclass);
void validate_planet_appearance(const PlanetAppearance&);
std::string planet_appearance_display_name(const PlanetAppearance&);
std::vector<std::string> planet_appearance_contradictions(const PlanetaryBody&);
double planet_rotation_phase(const PlanetAppearance&,double simulation_days,bool clouds=false);
}
