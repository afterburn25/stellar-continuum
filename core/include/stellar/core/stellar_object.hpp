#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace stellar::core {
enum class StellarObjectType {
  MRedDwarf, KOrangeStar, WhiteDwarf, GYellowStar, BrownDwarf,
  FYellowWhiteStar, QuietNeutronStar, RedGiant, AWhiteStar, BBlueWhiteStar,
  Pulsar, QuiescentBlackHole, YellowGiant, BlueGiant, AccretingBlackHole,
  RedSupergiant, BlueSupergiant, OHotBlueStar, Magnetar, WolfRayet,
  JetBlackHole, YellowSupergiant, Hypergiant, Count
};
inline constexpr std::size_t stellar_object_type_count=static_cast<std::size_t>(StellarObjectType::Count);
enum class PopulationState { Starburst, Active, Mature, Aging, Quiescent };
enum class GalaxyMorphology { Spiral, BarredSpiral, Lenticular, Elliptical, Irregular, Ring };
enum class StellarRegion { Disk, Arm, Bulge, StarForming, Ring, Interior,
  InnerDisk, InterArm, OuterDisk, Halo, IrregularClump, Bar };
enum class CentralBlackHoleState { Quiescent, Accreting, RelativisticJets };
struct StellarPopulationOptions {
  GalaxyMorphology morphology{GalaxyMorphology::BarredSpiral};
  PopulationState state{PopulationState::Mature};
  bool operator==(const StellarPopulationOptions&) const = default;
};
struct StellarRange { double minimum{},maximum{}; };
struct StellarDiscoveryHooks {
  std::string rarity_tier, discovery_event_id, discovery_text_key, sensor_signature, hazard_profile;
  bool is_rare_discovery{};
  std::vector<std::string> special_feature_ids, research_opportunity_ids,
      resource_opportunity_ids, unique_interaction_ids;
  bool operator==(const StellarDiscoveryHooks&) const = default;
};
struct StellarObjectDefinition {
  StellarObjectType type{};
  std::string id, name, evolution_class, fallback_distance_mode;
  int weight_millionths{};
  StellarRange mass_solar, radius_solar, luminosity_solar, temperature_kelvin,
      age_myr, lifetime_myr;
  double radiation_modifier{}, wind_modifier{}, habitability_modifier{}, visual_scale{}, distance_luminosity{};
  std::array<int,3> color{};
  bool young{}, evolved{}, remnant{}, black_hole{}, directional_jets{};
  StellarDiscoveryHooks hooks;
};
struct StellarPhysicalProperties {
  std::string generation_version{"stellar-population-v1"};
  StellarObjectType type{};
  double mass_solar{}, radius_solar{}, luminosity_solar{}, effective_temperature_kelvin{},
      age_myr{}, lifetime_myr{}, radiation_modifier{}, wind_modifier{}, habitability_modifier{},
      inner_hz_au{}, outer_hz_au{}, safe_approach_au{}, destruction_radius_au{},
      jet_axis_radians{}, jet_half_angle_radians{};
  bool active{}, measured_anchor{};
  StellarDiscoveryHooks hooks;
  bool operator==(const StellarPhysicalProperties&) const = default;
};
struct StellarPlanetProperties {
  double orbit_au{}, incident_flux{}, safe_approach_au{};
  bool baked{}, in_habitable_zone{};
  bool operator==(const StellarPlanetProperties&) const = default;
};
struct CentralBlackHoleProperties {
  std::string generation_version{"stellar-population-v1"};
  CentralBlackHoleState state{};
  double mass_solar{}, jet_axis_radians{}, jet_half_angle_radians{};
  bool operator==(const CentralBlackHoleProperties&) const = default;
};
inline constexpr double solar_radius_au=0.00465047;
inline constexpr double astronomical_unit_km=149597870.7;
const std::vector<StellarObjectDefinition>& stellar_object_definitions();
const StellarObjectDefinition& stellar_object_definition(StellarObjectType);
std::string_view population_state_name(PopulationState);
std::string_view morphology_name(GalaxyMorphology);
std::array<double,stellar_object_type_count> stellar_population_weights(
    StellarPopulationOptions={},std::optional<StellarRegion> region=std::nullopt);
StellarObjectType sample_stellar_object(std::uint64_t seed, StellarPopulationOptions={},
    std::optional<StellarRegion> region=std::nullopt);
StellarPhysicalProperties generate_stellar_physics(std::uint64_t seed,StellarObjectType);
void validate_stellar_physics(const StellarPhysicalProperties&);
void validate_stellar_planet(const StellarPlanetProperties&);
void validate_central_black_hole(const CentralBlackHoleProperties&);
std::pair<double,double> stellar_habitable_zone(double luminosity_solar);
StellarPlanetProperties stellar_planet_exposure(const StellarPhysicalProperties&,double orbit_au);
double stellar_hazard_extent_au(const StellarPhysicalProperties&);
double stellar_navigation_au_per_unit(const StellarPhysicalProperties&);
bool stellar_approach_unsafe(const StellarPhysicalProperties&,double x_au,double y_au);
CentralBlackHoleProperties generate_central_black_hole(std::uint64_t seed);
CentralBlackHoleProperties central_black_hole_with_state(CentralBlackHoleProperties,CentralBlackHoleState);
struct StellarSystem;
struct PlanetaryBody;
void apply_stellar_population(std::int64_t seed,std::vector<StellarSystem>&,StellarPopulationOptions,bool visual_footprint=false);
std::map<int,int> apply_stellar_planetary_physics(std::span<const StellarSystem>,std::vector<PlanetaryBody>&);
std::string stellar_population_diagnostics(std::span<const StellarSystem>,std::span<const PlanetaryBody>,
    StellarPopulationOptions,std::optional<CentralBlackHoleProperties>,std::uint64_t seed=0);
} // namespace stellar::core
