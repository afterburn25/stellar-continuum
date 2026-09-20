#pragma once
#include <stellar/core/planet_appearance.hpp>
#include <nlohmann/json.hpp>
namespace stellar::core {
template<class Json> inline void to_json(Json& j,const PlanetClass& value){j=planet_class_definition(value).id;}
template<class Json> inline void from_json(const Json& j,PlanetClass& value){
 if(j.is_string()){value=planet_class_from_id(j.template get<std::string>());return;}
 // Earlier ordered autosaves emitted enum ordinals. Freeze that v1 mapping so
 // those campaigns keep their artwork even if the live enum is later reordered.
 static constexpr std::array legacy_v1{PlanetClass::Barren,PlanetClass::Desert,
  PlanetClass::Frozen,PlanetClass::Ocean,PlanetClass::Temperate,PlanetClass::Tundra,
  PlanetClass::Volcanic,PlanetClass::Greenhouse,PlanetClass::Carbon,PlanetClass::SuperEarth,
  PlanetClass::MiniNeptune,PlanetClass::GasGiant,PlanetClass::IceGiant,PlanetClass::HotJupiter,
  PlanetClass::Chthonian,PlanetClass::Cracked};
 if(!j.is_number_integer()||j<0||j>=legacy_v1.size())
  throw std::invalid_argument("Invalid saved planet class: expected a class ID or legacy v1 ordinal");
 value=legacy_v1[j.template get<std::size_t>()];
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlanetClimate,bond_albedo,equilibrium_kelvin,greenhouse_kelvin,internal_flux_wm2,snow_line_au,retention_parameter,surface_water,surface_ice,vegetation,heat_source,history,migrated)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlanetAtmosphereAppearance,color,density,haze,cloud_opacity,cloud_period_days)
template<class Json> inline void to_json(Json& j,const PlanetRingAppearance& r){
 j=Json{{"enabled",r.enabled},{"inner_radius",r.inner_radius},{"outer_radius",r.outer_radius},{"density",r.density},{"color",r.color},{"composition",r.composition},{"version",r.version},{"seed",r.seed},{"family",r.family},{"asset_id",r.asset_id},{"significance",r.significance},{"origin",r.origin},{"thickness",r.thickness},{"optical_depth",r.optical_depth},{"particle_density",r.particle_density},{"reflectivity",r.reflectivity},{"equilibrium_kelvin",r.equilibrium_kelvin},{"roche_radius",r.roche_radius},{"plane_tilt_radians",r.plane_tilt_radians},{"plane_node_radians",r.plane_node_radians}};
}
inline void from_json(const nlohmann::json& j,PlanetRingAppearance& r){
 j.at("enabled").get_to(r.enabled);j.at("inner_radius").get_to(r.inner_radius);j.at("outer_radius").get_to(r.outer_radius);j.at("density").get_to(r.density);j.at("color").get_to(r.color);j.at("composition").get_to(r.composition);
 r.version=j.value("version",0);r.seed=j.value("seed",std::uint64_t{});r.family=j.value("family",std::string{});r.asset_id=j.value("asset_id",std::string{});r.significance=j.value("significance",std::string{"none"});r.origin=j.value("origin",std::string{});
 r.thickness=j.value("thickness",.0001);r.optical_depth=j.value("optical_depth",.55);r.particle_density=j.value("particle_density",1000.);r.reflectivity=j.value("reflectivity",.5);r.equilibrium_kelvin=j.value("equilibrium_kelvin",0.);r.roche_radius=j.value("roche_radius",0.);r.plane_tilt_radians=j.value("plane_tilt_radians",0.);r.plane_node_radians=j.value("plane_node_radians",0.);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlanetGiantProfile,version,atmospheric_composition,cloud_profile,storm_activity,formation_snow_line_au)
template<class Json> inline void to_json(Json& j,const PlanetAppearance& a){j=Json::object();j["version"]=a.version;j["visual_seed"]=a.visual_seed;j["source_asset_id"]=a.source_asset_id;j["material_id"]=a.material_id;j["subclass"]=a.subclass;j["primary_class"]=a.primary_class;j["compatible_classes"]=a.compatible_classes;j["climate"]=a.climate;j["atmosphere"]=a.atmosphere;j["rings"]=a.rings;j["axial_tilt_radians"]=a.axial_tilt_radians;j["axis_node_radians"]=a.axis_node_radians;j["rotation_period_days"]=a.rotation_period_days;j["initial_phase_radians"]=a.initial_phase_radians;j["oblateness"]=a.oblateness;j["terrain_height"]=a.terrain_height;j["emission_strength"]=a.emission_strength;j["developer_example"]=a.developer_example;j["giant"]=a.giant;if(a.tidally_locked)j["tidally_locked"]=*a.tidally_locked;}
inline void from_json(const nlohmann::json& j,PlanetAppearance& a){j.at("version").get_to(a.version);j.at("visual_seed").get_to(a.visual_seed);j.at("source_asset_id").get_to(a.source_asset_id);j.at("material_id").get_to(a.material_id);j.at("subclass").get_to(a.subclass);j.at("primary_class").get_to(a.primary_class);j.at("compatible_classes").get_to(a.compatible_classes);j.at("climate").get_to(a.climate);j.at("atmosphere").get_to(a.atmosphere);j.at("rings").get_to(a.rings);j.at("axial_tilt_radians").get_to(a.axial_tilt_radians);j.at("axis_node_radians").get_to(a.axis_node_radians);j.at("rotation_period_days").get_to(a.rotation_period_days);j.at("initial_phase_radians").get_to(a.initial_phase_radians);j.at("oblateness").get_to(a.oblateness);j.at("terrain_height").get_to(a.terrain_height);j.at("emission_strength").get_to(a.emission_strength);j.at("developer_example").get_to(a.developer_example);a.giant=j.value("giant",PlanetGiantProfile{});a.tidally_locked.reset();if(j.contains("tidally_locked"))a.tidally_locked=j.at("tidally_locked").get<bool>();}
}
