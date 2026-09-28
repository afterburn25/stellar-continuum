#include <stellar/core/planet_appearance.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Export the same resolved registry used by generation and the native index.
int main(int argc,char** argv)try{
 if(argc!=2)throw std::invalid_argument("Usage: stellar_planet_type_registry output.json");
 using namespace stellar::core;using nlohmann::json;
 json root={{"schemaVersion",1},{"generationPercentages","Baseline before physical eligibility and stellar modifiers; eligible weights are renormalized."},{"orbitalZoneMeaning","Equilibrium temperature from actual stellar flux and class albedo; lower inclusive, upper exclusive. Surface, heat and atmosphere rules must also pass."}};
 root["orbitalZones"]=json::array();for(const auto& zone:planet_orbital_zone_definitions())root["orbitalZones"].push_back({{"id",zone.id},{"name",zone.name},{"equilibriumKelvin",zone.equilibrium_kelvin}});
 root["types"]=json::array();
 for(const auto& r:planet_type_registry()){
  const auto& a=r.atmosphere_rules;json zones=json::array();for(auto z:r.valid_orbital_zones)zones.push_back(planet_orbital_zone_definition(z).id);
  root["types"].push_back({{"baseClass",planet_class_definition(r.base_class).id},{"subclass",r.subclass},{"name",r.name},{"validOrbitalZones",zones},
   {"temperatureRules",{{"surfaceKelvin",r.surface_temperature_kelvin},{"heatRule",r.heat_rule},{"liquidWaterRule","273.16–350 K, sufficient saturation pressure and available water solvent"},{"frozenPeriapsisRule","Ordinary frozen classes require periapsis equilibrium <= 273 K at albedo 0.55"}}},
   {"atmosphereRules",{{"pressureKpa",a.pressure_kpa},{"composition",a.composition_rule},{"molecularMassU",a.molecular_mass},{"minimumRetention",a.minimum_retention},{"retentionCheckAboveKpa",a.retention_check_above_kpa}}},
   {"waterAllowed",r.water_allowed},{"iceAllowed",r.ice_allowed},{"volcanismAllowed",r.volcanism_allowed},
   {"generation",{{"classPercentage",r.class_percentage},{"subclassWeight",r.subclass_weight},{"withinClassPercentage",r.within_class_percentage},{"overallBaselinePercentage",r.baseline_percentage}}},
   {"acceptedImagePool",r.accepted_image_pool},{"compatibleImagePool",r.compatible_image_pool},{"rejectedImagePool",r.rejected_image_pool}});
 }
 root["acceptedImages"]=json::object();for(const auto& a:planet_art_definitions())root["acceptedImages"][a.id]={{"filename",a.source_filename},{"sha256",a.sha256},{"materialId",a.material_id}};
 root["rejectedImages"]=json::object();for(const auto& a:rejected_planet_art_definitions())root["rejectedImages"][a.id]={{"filename",a.source_filename},{"sha256",a.sha256},{"reason",a.reason},{"duplicateOf",a.duplicate_of},{"earthGeography",a.earth_geography},{"auditedSubclass",a.audited_subclass}};
 std::ofstream out(argv[1],std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open registry report");out<<root.dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("Cannot write registry report");
 std::cout<<root["types"].size()<<" types, "<<root["acceptedImages"].size()<<" accepted and "<<root["rejectedImages"].size()<<" rejected images exported.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
