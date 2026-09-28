#include "native_stellar_eruptions.hpp"
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
using namespace stellar::core;
using namespace stellar::native_map;
int main(int argc,char** argv)try{
  if(argc!=2)throw std::runtime_error("Supply a cooked game directory");
  const std::filesystem::path root=argv[1];stellar::engine::mount_asset_registry(root,false);
  // Exercise each real cooked format and both decode usages. The estimator must
  // reserve retained memory exactly without performing a package read itself.
  std::set<std::string> formats;
  for(const auto& record:stellar::engine::mounted_asset_registry()->records()){
    if(record.type!="texture"||record.aliases.empty()||!formats.insert(record.format).second)continue;
    const auto path=root/record.aliases.front();
    for(const int width:{0,256,512})for(const auto usage:{ImageDecodeUsage::PreserveMipChain,ImageDecodeUsage::PixelsOnly}){
      const auto before=stellar::engine::mounted_asset_registry()->diagnostics().reads;
      const auto bytes=image_decode_output_bytes(path,0,width,usage);
      if(stellar::engine::mounted_asset_registry()->diagnostics().reads!=before)throw std::runtime_error("Reservation read package data");
      if(bytes!=decode_rgba_image(path,width,usage)->byte_size())throw std::runtime_error("Cooked decoder reservation does not match retained output");
    }
  }
  FreshCampaignState world;world.seed=7;world.developer_provenance.emplace();
  const std::array types{StellarObjectType::OHotBlueStar,StellarObjectType::BBlueWhiteStar,StellarObjectType::AWhiteStar,StellarObjectType::FYellowWhiteStar,StellarObjectType::GYellowStar,StellarObjectType::KOrangeStar,StellarObjectType::MRedDwarf};
  for(int c=0;c<7;++c){StellarSystem s;s.id=c+1;s.stellar_object=generate_stellar_physics(c+1,types[c]);world.systems.push_back(s);}
  initialize_stellar_activity(world.seed,world.systems);StellarActivityScheduler scheduler;
  int covered=0;
  for(int quality=0;quality<4;++quality){
    auto queue=std::make_shared<ImagePreparationQueue>();
    stellar::native_stellar::EruptionArtwork art(root);art.use_queue(queue);
    for(auto& s:world.systems)for(int kind=0;kind<5;++kind){
      StellarActivityCommand cmd;cmd.system_id=s.id;cmd.action=StellarActivityAction::ClearForced;(void)apply_developer_stellar_activity(world,scheduler,1,cmd);
      cmd.action=StellarActivityAction::Force;cmd.type=static_cast<StellarEruptionType>(kind);cmd.variant=0;cmd.longitude=1.35;
      cmd.event_id=apply_developer_stellar_activity(world,scheduler,1,cmd);cmd.action=StellarActivityAction::Scrub;cmd.fraction=.35;(void)apply_developer_stellar_activity(world,scheduler,1,cmd);
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
      for(int frame=0;;++frame){
        art.begin_frame(1,1,covered+frame*.016,false,quality);DrawList draw;
        // Enter detailed LOD exactly as the star map does during zoom.
        art.append(draw,{400,300},frame==0?21.f:110.f,s,0,{0,0,800,600});
        if(!art.records().empty()){
          const auto& material=std::get<Scene3DView>(draw.world.at(0)).scene->instances().front().material;
          if(material.texture->cooked_mips().empty())throw std::runtime_error("Cooked VFX lost their supplied mip chain");
          break;
        }
        if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Cooked eruption never became ready");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      ++covered;
    }
  }
  const auto stats=stellar::engine::mounted_asset_registry()->diagnostics();
  if(stats.failures)throw std::runtime_error("Cooked resource read failed");
  std::cout<<"PASS: "<<covered<<" cooked asynchronous class/type/quality zoom transitions; original mip chains retained.\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
