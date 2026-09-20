#pragma once
#include <stellar/core/stellar_object.hpp>
#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/galaxy_configuration.hpp>
#include <nlohmann/json.hpp>
namespace stellar::core {
// Reject fractional or out-of-range enums instead of silently truncating a save.
#define STELLAR_STRICT_ENUM(Type, Limit) \
inline void to_json(nlohmann::json& j,const Type& value){j=static_cast<int>(value);} \
inline void from_json(const nlohmann::json& j,Type& value){ \
  if(!j.is_number_integer())throw std::invalid_argument("Invalid saved " #Type); \
  const auto n=j.get<std::int64_t>();if(n<0||n>=Limit)throw std::invalid_argument("Invalid saved " #Type);value=static_cast<Type>(n);}
STELLAR_STRICT_ENUM(StellarObjectType,23)
STELLAR_STRICT_ENUM(GalaxyMorphology,6)
STELLAR_STRICT_ENUM(PopulationState,5)
STELLAR_STRICT_ENUM(PopulationSelection,6)
STELLAR_STRICT_ENUM(CentralBlackHoleState,3)
STELLAR_STRICT_ENUM(StellarRegion,12)
#undef STELLAR_STRICT_ENUM
// All fields are required when this versioned extension is present. Old saves omit the extension.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarDiscoveryHooks,rarity_tier,discovery_event_id,discovery_text_key,sensor_signature,hazard_profile,is_rare_discovery,special_feature_ids,research_opportunity_ids,resource_opportunity_ids,unique_interaction_ids)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPhysicalProperties,generation_version,type,mass_solar,radius_solar,luminosity_solar,effective_temperature_kelvin,age_myr,lifetime_myr,radiation_modifier,wind_modifier,habitability_modifier,inner_hz_au,outer_hz_au,safe_approach_au,destruction_radius_au,jet_axis_radians,jet_half_angle_radians,active,measured_anchor,hooks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPlanetProperties,orbit_au,incident_flux,safe_approach_au,baked,in_habitable_zone)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CentralBlackHoleProperties,generation_version,state,mass_solar,jet_axis_radians,jet_half_angle_radians)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPopulationOptions,morphology,state)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GalaxyGenerationConfig,base_seed,morphology,requested_population,resolved_population,system_count,pre_warp_count,ancient_count,player_species_id,developer_full_coverage,generator_version,asset_set_version,fingerprint,preview_asset_id,map_asset_id)
}

namespace stellar::engine {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AnalyticOrbit,radius,eccentricity,inclination,ascending_node,periapsis,phase,angular_speed)
}
namespace stellar::core {
template<class Json> int orbital_integer(const Json& j,const char* name){const auto& v=j.at(name);
 if(!v.is_number_integer()||v<0||v>std::numeric_limits<int>::max())throw std::invalid_argument("Invalid stellar orbit integer");return v.template get<int>();}
template<class Json> void to_json(Json& j,const StellarOrbitBinding& v){j={{"body_id",v.body_id},{"host",v.host}};}
template<class Json> void from_json(const Json& j,StellarOrbitBinding& v){v={orbital_integer(j,"body_id"),orbital_integer(j,"host")};}
template<class Json> void to_json(Json& j,const StellarOrbitArchitecture& v){j={{"version",v.version},{"companions",v.companions},{"relative_orbits",v.relative_orbits},{"planets",v.planets},{"belt_host",v.belt_host}};}
template<class Json> void from_json(const Json& j,StellarOrbitArchitecture& v){v={orbital_integer(j,"version"),j.at("companions").template get<std::vector<StellarPhysicalProperties>>(),j.at("relative_orbits").template get<std::vector<stellar::engine::AnalyticOrbit>>(),j.at("planets").template get<std::vector<StellarOrbitBinding>>(),orbital_integer(j,"belt_host")};}
}
