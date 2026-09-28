#pragma once
#include <stellar/core/galaxy_phenomena.hpp>
#include <nlohmann/json.hpp>
namespace stellar::engine {
template<class Json> inline void to_json(Json& j,const OrganicShape& s){j=static_cast<int>(s);}
template<class Json> inline void from_json(const Json& j,OrganicShape& s){if(!j.is_number_integer()||j<0||j>5)throw std::invalid_argument("Invalid phenomenon shape");s=static_cast<OrganicShape>(j.template get<int>());}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OrganicRegion,x,y,extent_x,extent_y,rotation,roughness,seed,shape)
}
namespace stellar::core {
template<class Json> inline void to_json(Json& j,const PhenomenonType& s){j=static_cast<int>(s);}
template<class Json> inline void from_json(const Json& j,PhenomenonType& s){if(!j.is_number_integer()||j<0||j>=phenomenon_type_count)throw std::invalid_argument("Invalid phenomenon type");s=static_cast<PhenomenonType>(j.template get<int>());}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PhenomenonEffects,sensor,scanning,movement,hazard,colonization,research,anomaly_bias,resource_bias,concealment,combat_visibility,attrition,interference)
template<class Json> inline void to_json(Json& j,const GalaxyPhenomenon& r){
 j=Json{{"id",r.id},{"type",r.type},{"designation",r.designation},{"shape",r.shape},{"intensity",r.intensity},{"opacity",r.opacity},{"color",r.color},{"affinity",r.affinity},{"effects",r.effects},{"systems_contained",r.systems_contained},{"hooks",r.hooks},
 {"asset_id",r.asset_id},{"mirrored",r.mirrored},{"natural_weight",r.natural_weight},{"population_modifier",r.population_modifier},{"morphology_modifier",r.morphology_modifier}};
}
template<class Json> inline void from_json(const Json& j,GalaxyPhenomenon& r){
 j.at("id").get_to(r.id);j.at("type").get_to(r.type);j.at("designation").get_to(r.designation);j.at("shape").get_to(r.shape);j.at("intensity").get_to(r.intensity);j.at("opacity").get_to(r.opacity);j.at("color").get_to(r.color);j.at("affinity").get_to(r.affinity);j.at("effects").get_to(r.effects);j.at("systems_contained").get_to(r.systems_contained);j.at("hooks").get_to(r.hooks);
 r.asset_id=j.value("asset_id",std::string{});r.mirrored=j.value("mirrored",false);r.natural_weight=j.value("natural_weight",0.);r.population_modifier=j.value("population_modifier",1.);r.morphology_modifier=j.value("morphology_modifier",1.);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GalaxyPhenomena,version,configuration_fingerprint,regions)
}
