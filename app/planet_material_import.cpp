#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
using nlohmann::json;
int main(int argc,char** argv){
 try{
  if(argc<3)throw std::invalid_argument("Usage: stellar_planet_material_import audit.json output-directory [width] [id]");
  json audit;std::ifstream(argv[1])>>audit;const auto output=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(output);
  std::atomic<std::size_t> cursor{};std::mutex mutex;json results=json::array();std::vector<std::thread> workers;
  for(int worker=0;worker<4;++worker)workers.emplace_back([&]{for(;;){const auto i=cursor.fetch_add(1);if(i>=audit["images"].size())break;const auto& item=audit["images"][i];
   const auto id=item["id"].get<std::string>();if(item["status"]!="candidate"||(argc>4&&id!=argv[4]))continue;
   json record{{"id",id}};
   try{stellar::native_map::SphericalSourceOptions options;options.width=argc>3?std::stoi(argv[3]):1024;options.seed=std::stoull(item["sha256"].get<std::string>().substr(0,16),nullptr,16);
    const auto& flags=item["preparation"];options.opaque_clouds=flags["opaqueClouds"];options.separate_clouds=flags["separateClouds"];options.liquid=flags["liquid"];options.ice=flags["ice"];options.emissive=flags["emissive"];options.rings=flags["rings"];options.flatten_canopy=flags["flattenCanopy"];
    const auto primary=item["primaryClass"].get<std::string>();options.zonal_clouds=primary=="gas-giant"||primary=="ice-giant"||primary=="mini-neptune"||primary=="hot-jupiter";
    options.preserve_zonal_detail=flags.value("preserveZonalDetail",false);options.source_roll_degrees=flags.value("sourceRollDegrees",0.);
    options.maximum_light_gradient=flags.value("maximumLightGradient",.7);
    options.maximum_dark_fraction=flags.value("maximumDarkFraction",.08);
    const auto source=stellar::native_map::decode_rgba_image(std::filesystem::u8path(item["source"].get<std::string>()));const auto maps=stellar::native_map::prepare_spherical_material(*source,options);
    record.update({{"usable",maps.usable},{"reason",maps.rejection_reason},{"disc",maps.disc},{"removedLightGradient",maps.removed_light_gradient},{"observedSurfaceFraction",maps.observed_surface_fraction},{"blackFraction",maps.black_fraction},{"clippedFraction",maps.clipped_fraction},{"seamError",maps.seam_error}});
    if(maps.usable){const auto dir=output/id;std::filesystem::create_directories(dir);
     for(const auto& [name,image]:std::initializer_list<std::pair<const char*,std::shared_ptr<const stellar::native_map::RgbaImage>>>{{"albedo",maps.albedo},{"properties",maps.properties},{"clouds",maps.clouds},{"emission",maps.emission},{"normal",maps.normal},{"thumbnail",maps.thumbnail}})stellar::native_map::encode_rgba_png(*image,dir/(std::string(name)+".png"));
     auto definition=item;definition["conversion"]=record;definition["preparationVersion"]=options.preserve_zonal_detail?"spherical-oriented-atmosphere-v4":options.zonal_clouds||options.rings?"spherical-zonal-v3":"spherical-source-patch-v2";definition["unseenHemisphere"]=options.preserve_zonal_detail?"Latitude-aligned continuation of supplied hemisphere; no recovered hidden geography.":options.zonal_clouds||options.rings?"Reconstructed equatorial cloud bands from the source color strip; no recovered hidden geography.":"Deterministic spherical source-patch synthesis; not recovered photographic geography.";std::ofstream(dir/"material.json")<<definition.dump(2)<<'\n';
    }
   }catch(const std::exception& e){record["usable"]=false;record["reason"]=e.what();}
   std::lock_guard lock(mutex);results.push_back(record);std::cout<<id<<" "<<(record["usable"].get<bool>()?"prepared":record["reason"].get<std::string>())<<'\n';std::ofstream(output/(argc>4?std::string(argv[4])+"-conversion.json":"conversion.json"))<<results.dump(2)<<'\n';
  }});
  for(auto& worker:workers)worker.join();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
