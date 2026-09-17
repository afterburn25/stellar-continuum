#pragma once
#include <stellar/core/stellar_object.hpp>
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
STELLAR_STRICT_ENUM(CentralBlackHoleState,3)
STELLAR_STRICT_ENUM(StellarRegion,12)
#undef STELLAR_STRICT_ENUM
// All fields are required when this versioned extension is present. Old saves omit the extension.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarDiscoveryHooks,rarity_tier,discovery_event_id,discovery_text_key,sensor_signature,hazard_profile,is_rare_discovery,special_feature_ids,research_opportunity_ids,resource_opportunity_ids,unique_interaction_ids)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPhysicalProperties,generation_version,type,mass_solar,radius_solar,luminosity_solar,effective_temperature_kelvin,age_myr,lifetime_myr,radiation_modifier,wind_modifier,habitability_modifier,inner_hz_au,outer_hz_au,safe_approach_au,destruction_radius_au,jet_axis_radians,jet_half_angle_radians,active,measured_anchor,hooks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPlanetProperties,orbit_au,incident_flux,safe_approach_au,baked,in_habitable_zone)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CentralBlackHoleProperties,generation_version,state,mass_solar,jet_axis_radians,jet_half_angle_radians)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarPopulationOptions,morphology,state)
}
