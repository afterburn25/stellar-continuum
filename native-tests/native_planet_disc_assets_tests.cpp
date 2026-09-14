#include "native_planet_disc_assets.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <string>

namespace fs=std::filesystem;
using namespace stellar::native_system;
using namespace stellar::native_system_ui;

namespace {
void require(bool value,std::string message,const std::source_location where=std::source_location::current()){if(!value)throw std::runtime_error(std::move(message)+" at "+where.file_name()+":"+std::to_string(where.line()));}
class FixtureDirectory final {
public:
  explicit FixtureDirectory(const fs::path &source){const auto seed=std::chrono::high_resolution_clock::now().time_since_epoch().count();for(int attempt=0;attempt<100;++attempt){path_=fs::temp_directory_path()/(L"stellar-planet-discs-"+std::to_wstring(seed)+L"-"+std::to_wstring(attempt));if(fs::create_directory(path_))break;}if(path_.empty()||!fs::exists(path_))throw std::runtime_error("Could not create isolated planet-disc fixtures.");fs::copy_file(source/L"mercury.jpg",path_/L"mercury.jpg");fs::copy_file(source/L"neptune.jpg",path_/L"neptune.jpg");}
  ~FixtureDirectory(){std::error_code ignored;fs::remove(path_/L"mercury.jpg",ignored);fs::remove(path_/L"neptune.jpg",ignored);fs::remove(path_,ignored);}
  [[nodiscard]]const fs::path&path()const noexcept{return path_;}
private:fs::path path_;
};
std::array<unsigned,4> pixel(const stellar::native_map::RgbaImage &image,int x,int y){const auto index=(static_cast<std::size_t>(y)*image.width()+static_cast<std::size_t>(x))*4u;const auto &p=image.pixels();return {p[index],p[index+1],p[index+2],p[index+3]};}
SystemBodyAppearance appearance(std::uint64_t generation,int body,NativeSystemBodyVisualClass visual,std::optional<std::string> key={},float light=0){return {generation,body,true,visual,std::move(key),static_cast<std::uint32_t>(body*7919),light};}
}

int main(int argc,char **argv)try{
  require(argc==2,"Usage: native_planet_disc_assets_tests <Sol asset root>");FixtureDirectory fixtures(fs::absolute(argv[1]));NativePlanetDiscAssets assets(fixtures.path());
  auto mercury=appearance(1,101,NativeSystemBodyVisualClass::rocky,"mercury",0);const auto mercury_disc=assets.image(mercury);require(mercury_disc&&mercury_disc->width()==256&&mercury_disc->height()==256,"Mercury did not use the approved 256-pixel source disc");require(pixel(*mercury_disc,0,0)[3]==0,"planet disc corner was not transparent");bool antialiased{},matte{};for(std::size_t i=0;i<mercury_disc->pixels().size();i+=4){const auto alpha=mercury_disc->pixels()[i+3];if(alpha>0&&alpha<255){antialiased=true;if(static_cast<unsigned>(mercury_disc->pixels()[i])+mercury_disc->pixels()[i+1]+mercury_disc->pixels()[i+2]>20)matte=true;}}require(antialiased,"planet limb has no antialiased coverage");require(matte,"antialiased photo limb sampled only a dark matte");
  const auto decode_after_mercury=assets.source_decode_count(),generated_after_mercury=assets.generated_disc_count();for(int frame=0;frame<20;++frame)require(assets.image(mercury)==mercury_disc,"camera-only frame rebuilt the cached Mercury disc");require(assets.source_decode_count()==decode_after_mercury&&assets.generated_disc_count()==generated_after_mercury,"camera-only frames decoded or generated a disc");
  auto mercury_other_light=mercury;mercury_other_light.lighting_longitude=2.2f;const auto mercury_lit=assets.image(mercury_other_light);require(mercury_lit->pixels()==mercury_disc->pixels(),"observed Mercury photograph was synthetically darkened by map lighting");
  const auto raw_mercury=stellar::native_map::decode_rgba_image(fixtures.path()/L"mercury.jpg");const auto raw_center=pixel(*raw_mercury,raw_mercury->width()/2,raw_mercury->height()/2),disc_center=pixel(*mercury_disc,128,128);for(int channel=0;channel<3;++channel)require(std::abs(static_cast<int>(raw_center[channel])-static_cast<int>(disc_center[channel]))<40,"photographic center was recolored or double-darkened");
  auto neptune=appearance(1,102,NativeSystemBodyVisualClass::ice_giant,"neptune",0);const auto neptune_day=assets.image(neptune);neptune.lighting_longitude=std::numbers::pi_v<float>;const auto neptune_night=assets.image(neptune);require(neptune_day->pixels()!=neptune_night->pixels(),"equirectangular Neptune map ignored star-directed lighting");require(neptune_day->pixels()!=mercury_disc->pixels(),"Neptune and Mercury asset mappings collapsed to one disc");
  auto unknown=appearance(1,103,NativeSystemBodyVisualClass::unknown_planet);require(!assets.image(unknown),"unknown body received a generated surface");unknown.texture_key="mercury";bool unknown_spoof_rejected{};try{(void)assets.image(unknown);}catch(const std::invalid_argument&){unknown_spoof_rejected=true;}require(unknown_spoof_rejected,"unknown body accepted a spoofed Sol texture key");auto partial=mercury;partial.body_id=104;partial.fully_surveyed=false;bool partial_spoof_rejected{};try{(void)assets.image(partial);}catch(const std::invalid_argument&){partial_spoof_rejected=true;}require(partial_spoof_rejected,"non-full survey accepted a Sol texture key");
  partial.texture_key.reset();bool known_recon_rejected{};try{(void)assets.image(partial);}catch(const std::invalid_argument&){known_recon_rejected=true;}require(known_recon_rejected,"non-full survey disclosed a known class fallback without a texture key");auto unapproved=mercury;unapproved.body_id=105;unapproved.texture_key="pluto";bool unapproved_rejected{};try{(void)assets.image(unapproved);}catch(const std::invalid_argument&){unapproved_rejected=true;}require(unapproved_rejected,"unapproved Sol key reached path resolution");
  require(fs::remove(fixtures.path()/L"mercury.jpg"),"decoded Mercury source remained locked or was missing");require(assets.image(mercury)==mercury_disc,"cached disc attempted to decode its released source again");
  NativePlanetDiscAssets missing_assets(fixtures.path());bool missing_path_reported{};try{(void)missing_assets.image(mercury);}catch(const std::exception &error){missing_path_reported=std::string(error.what()).find("mercury.jpg")!=std::string::npos;}require(missing_path_reported,"missing planet asset diagnostic omitted the requested path");require(missing_assets.cache_entries()==0,"failed source decode populated the disc cache");
  auto old_fallback=assets.image(appearance(1,200,NativeSystemBodyVisualClass::oceanic));std::weak_ptr<const stellar::native_map::RgbaImage> old_weak=old_fallback;old_fallback.reset();const auto next=assets.image(appearance(2,200,NativeSystemBodyVisualClass::oceanic));require(next&&old_weak.expired(),"campaign generation replacement retained old disc resources");bool stale_rejected{};try{(void)assets.image(appearance(1,201,NativeSystemBodyVisualClass::rocky));}catch(const std::invalid_argument&){stale_rejected=true;}require(stale_rejected,"stale campaign generation repopulated the disc cache");
  for(std::size_t index=0;index<maximum_planet_disc_entries+8;++index)(void)assets.image(appearance(2,1000+static_cast<int>(index),NativeSystemBodyVisualClass::rocky));require(assets.cache_entries()<=maximum_planet_disc_entries,"planet disc cache exceeded its entry cap");require(assets.cache_bytes()<=maximum_planet_disc_bytes,"planet disc cache exceeded its byte cap");assets.discard_campaign();require(assets.cache_entries()==0&&assets.cache_bytes()==0,"campaign discard retained generated discs");
  std::cout<<"native planet source crops, observer gating, lighting and bounded cache passed\n";return 0;
}catch(const std::exception &error){std::cerr<<"native planet disc tests failed: "<<error.what()<<'\n';return 1;}
