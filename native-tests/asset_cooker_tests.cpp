#include <stellar/engine/asset_cooker.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/content_resolver.hpp>
#include <stellar/engine/sha256.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace stellar::engine;
using namespace stellar::native_map;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,message);}
std::string hash(std::string_view s){return digest_hex(sha256(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size())));}
void write(const fs::path&p,std::string_view s){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<s;require(bool(f),"Fixture write failed");}
Json read_json(const fs::path&p){std::ifstream f(p);return Json::parse(f);}
void corrupt(const fs::path&p,std::uint64_t offset){std::fstream f(p,std::ios::in|std::ios::out|std::ios::binary);f.seekg(static_cast<std::streamoff>(offset));char c{};f.read(&c,1);c^=0x4a;f.seekp(static_cast<std::streamoff>(offset));f.write(&c,1);}
void texture_tests(){
 std::vector<std::uint8_t> p(32*16*4);for(std::size_t i=0;i<p.size();i+=4){p[i]=120;p[i+1]=100;p[i+2]=80;p[i+3]=255;}
 auto image=RgbaImage::create(32,16,p);auto cooked=cook_texture(*image,"critical");require(cooked.format==TextureFormat::Bc7,"Constant color should compress");
 auto resident=RgbaImage::create_cooked(cooked.format,cooked.mips);require(select_texture_mip(*resident,8)->width()==8,"LOD did not select cooked mip");
 require(cooked.mips.back().width==1&&cooked.mips.back().height==1,"Missing tail mip");
 std::vector<Bc1MipLevel> lossless;
 std::size_t lossless_bytes=0;
 for(int w=32,h=16;;w=std::max(1,w/2),h=std::max(1,h/2)){
   std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4,137);
   lossless_bytes+=pixels.size();lossless.push_back({w,h,std::move(pixels)});
   if(w==1&&h==1)break;
 }
 auto shared=RgbaImage::create_cooked(TextureFormat::Rgba8,std::move(lossless));
 require(shared->pixels().data()==shared->cooked_mips().front().blocks.data()&&shared->pixels().front()==137,"Lossless base mip not shared with CPU pixels");
 require(shared->byte_size()==lossless_bytes,"Lossless base mip counted twice");
 for(std::size_t i=0;i<p.size();i+=4){p[i]=static_cast<std::uint8_t>(i*17);p[i+1]=static_cast<std::uint8_t>(i*43);p[i+2]=static_cast<std::uint8_t>(i*61);p[i+3]=0;}
 auto invisible=cook_texture(*RgbaImage::create(32,16,p),"vfx");std::cout<<"Invisible RGBA metric "<<invisible.rmse<<" / "<<invisible.max_error<<'\n';require(!invisible.lossless_fallback&&invisible.max_error<=2,"Invisible RGB counted as visible error");
 for(std::size_t i=0;i<p.size();i+=4){p[i]=128;p[i+1]=128;p[i+2]=255;p[i+3]=255;}
 auto normal=cook_texture(*RgbaImage::create(32,16,p),"normal");require(normal.format==TextureFormat::Bc5,"Normal map not BC5");
 for(std::size_t i=0;i<p.size();i+=4)p[i]=p[i+1]=p[i+2]=77;
 auto mask=cook_texture(*RgbaImage::create(32,16,p),"mask");require(mask.format==TextureFormat::Bc4,"Mask not BC4");
 rejects([&]{auto broken=cooked.mips;broken.pop_back();(void)RgbaImage::create_cooked(cooked.format,broken);},"Incomplete mip chain accepted");
 rejects([&]{auto broken=cooked.mips;broken.front().width=-1;(void)RgbaImage::create_cooked(cooked.format,broken);},"Negative dimensions accepted");
 rejects([&]{(void)RgbaImage::create_cooked(static_cast<TextureFormat>(99),cooked.mips);},"Unknown format accepted");
 AssetCodec codec{};const std::vector<std::uint8_t> bytes(8192,13);auto packed=compress_asset_bytes(bytes,codec);require(codec==AssetCodec::XpressHuff&&decompress_asset_bytes(packed,codec,bytes.size())==bytes,"Compression round trip failed");
 require(compress_asset_bytes({},codec).empty(),"Empty compression failed");
 std::vector<std::uint8_t> gradient(256*256*4);for(std::size_t i=0;i<gradient.size();++i)gradient[i]=static_cast<std::uint8_t>(i/4+i%4*37);
 const auto delta=compress_asset_bytes(gradient,codec,true);require(codec==AssetCodec::XpressHuff||codec==AssetCodec::XpressRgbaDelta||codec==AssetCodec::Lzms||codec==AssetCodec::LzmsRgbaDelta,"Compressed codec tag out of range");require(decompress_asset_bytes(delta,codec,gradient.size())==gradient,"Lossless RGBA predictor changed pixels");
 std::vector<std::uint8_t> smooth(512*512*4);for(std::size_t i=0;i<smooth.size();++i)smooth[i]=static_cast<std::uint8_t>((i/4)%256/2+(i/2048)%64);
 const auto smooth_packed=compress_asset_bytes(smooth,codec,true);require(codec==AssetCodec::Lzms||codec==AssetCodec::LzmsRgbaDelta,"LZMS path never selected for smooth RGBA");require(decompress_asset_bytes(smooth_packed,codec,smooth.size())==smooth,"LZMS round trip changed pixels");
 rejects([&]{(void)decompress_asset_bytes(packed,AssetCodec::XpressHuff,1);},"Wrong decompressed size accepted");
}
int main(int argc,char**argv){try{
 require(argc==2,"Expected disposable test output path");
 require(hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA empty vector failed");
 require(hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA abc vector failed");
 require(hash(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA long vector failed");
 texture_tests();
 const auto root=fs::absolute(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 write(root/"assets/visual/stellar/manifest.json",R"({"files":[],"objects":[]})");
 write(root/"assets/visual/stellar-eruptions/manifest.json",R"({"visualSets":[]})");
 write(root/"export/research-runtime-files.json",R"({"root":"data/research","destination":"Data/research","files":[]})");
 write(root/"data/astronomy/hyg-nearby-500-v1.json","{}");
 std::vector<std::uint8_t> pixels(32*16*4,255);for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=77;pixels[i+1]=140;}
 fs::create_directories(root/"source");encode_rgba_png(*RgbaImage::create(32,16,pixels),root/"source/moon.png");
 fs::create_directories(root/"assets/visual/native-navigation");
 fs::copy_file(root/"source/moon.png",root/"assets/visual/native-navigation/icon.png");
 write(root/"source/icon.svg","<svg/>");
 write(root/"export/native-navigation-assets.json",Json{{"assets",Json::array({
  {{"source","source/icon.svg"},{"runtimePath","assets/visual/native-navigation/icon.png"},
   {"runtimeSha256",sha256_file(root/"source/moon.png")}}
 })}}.dump());
 write(root/"source/rules.json","{\"v\":1}");write(root/"source/rejected.txt","not shipped");
 Json config={{"assets",Json::array({
  {{"id","assets/visual/moons/test/albedo.png"},{"source","source/moon.png"},{"state","ACCEPTED_RUNTIME"},{"dependencies",{"Data/rules.json"}}},
  {{"id","assets/visual/moons/duplicate/albedo.png"},{"source","source/moon.png"},{"state","ACCEPTED_RUNTIME"}},
  {{"id","Data/rules.json"},{"source","source/rules.json"},{"state","ACCEPTED_RUNTIME"}},
  {{"id","Data/rejected.txt"},{"source","source/rejected.txt"},{"state","REJECTED"}},
  {{"id","Data/qa.txt"},{"source","source/rejected.txt"},{"state","QA_ONLY"}}
 })}};
 write(root/"export/cooker-assets.json",config.dump());
 AssetCookOptions o{root,root/"out",root/"cache",root/"report.json"};o.threads=3;cook_asset_repository(o);
 const auto report=read_json(o.report);require(report["duplicateChunks"].get<int>()>0,"Identical assets not deduplicated");
 const auto manifest=o.output/"Content/runtime.stmanifest";const auto initial=sha256_file(manifest);
 AssetRegistry registry(manifest);registry.validate_all();require(!registry.find("Data/rejected.txt")&&!registry.find("Data/qa.txt"),"Nonruntime content shipped");
 require(registry.find("assets/visual/native-navigation/icon.png")->type=="texture","Imported master shipped instead of native raster");
 const auto*moon=registry.find("ASSETS/visual/moons/test/albedo.png");require(moon&&moon->chunks[0].package.starts_with("Celestial-"),"Moon package incorrect");
 auto forged=*moon;forged.chunks[0].package="../../source/moon.png";rejects([&]{(void)registry.read(forged);},"Foreign record accepted");
 mount_asset_registry(o.output,false);const auto small=decode_rgba_image(o.output/"assets/visual/moons/test/albedo.png",8);require(small->width()==8&&small->cooked_format()==TextureFormat::Bc7,"Cooked mip loading failed");
 require(mounted_asset_registry()->diagnostics().reads==4,"Loader read unwanted high resolution mips");
 const auto pixels_only=decode_rgba_image(o.output/"assets/visual/moons/test/albedo.png",8,ImageDecodeUsage::PixelsOnly);
 require(mounted_asset_registry()->diagnostics().reads==5,"CPU image read an unused mip chain");
 require(pixels_only->pixels()==small->pixels()&&pixels_only->cooked_mips().empty()&&pixels_only->byte_size()==8*4*4,"CPU-only decoding changed pixels or retained redundant data");
 write(o.output/"loose.txt","exists");rejects([&]{(void)read_resource(o.output/"loose.txt");},"Strict package fell back to loose file");
 unmount_asset_registry();
 o.threads=1;cook_asset_repository(o);require(sha256_file(manifest)==initial,"Parallel cook not deterministic");
 require(read_json(o.report)["recooked"]==0,"Unchanged assets recooked");
 // Invalid cached JSON must trigger rebuilding rather than poison all later cooks.
 const auto key=report["assets"][0]["cacheKey"].get<std::string>();write(o.cache/(key+".json"),"damaged");cook_asset_repository(o);require(read_json(o.report)["recooked"]==1,"Corrupt cache not repaired");
 write(root/"source/rules.json","{\"v\":2}");cook_asset_repository(o);require(read_json(o.report)["recooked"]==2,"Dependency edit did not invalidate dependent product");
 const auto updated=sha256_file(manifest);require(updated!=initial,"Changed metadata did not publish new manifest");
 {AssetRegistry r(manifest);const auto*a=r.find("Data/rules.json");corrupt(o.output/"Content"/a->chunks[0].package,a->chunks[0].offset);rejects([&]{(void)r.read(*a);},"Corrupt package payload accepted");require(r.diagnostics().failures==1,"Missing contextual failure diagnostic");}
 // Separate output because the preceding generation was intentionally corrupted.
 o.output=root/"qa";o.profile="qa";cook_asset_repository(o);require(AssetRegistry(o.output/"Content/runtime.stmanifest").find("Data/qa.txt"),"QA-only content unavailable in QA profile");
 // Verify failures at mount time, before a worker can request corrupt content.
 const auto intact_manifest=o.output/"Content/runtime.stmanifest";
 const auto package_name=AssetRegistry(intact_manifest).records().front().chunks.front().package;
 for(const auto scenario:{"header","truncated","missing"}){
  const auto copy=root/scenario;fs::create_directories(copy/"Content");
  fs::copy(o.output/"Content",copy/"Content",fs::copy_options::recursive|fs::copy_options::overwrite_existing);
  const auto target=copy/"Content"/package_name;
  if(std::string_view(scenario)=="header")corrupt(target,0);
  else if(std::string_view(scenario)=="truncated")fs::resize_file(target,8);
  else fs::remove(target);
  rejects([&]{AssetRegistry r(copy/"Content/runtime.stmanifest");},"Invalid package accepted at mount");
 }
 auto badmanifest=o.output/"Content/runtime.stmanifest";corrupt(badmanifest,45);rejects([&]{AssetRegistry r(badmanifest);},"Manifest checksum ignored");
 config["assets"][0]["dependencies"]={"missing"};write(root/"export/cooker-assets.json",config.dump());rejects([&]{cook_asset_repository(o);},"Missing dependency accepted");
 config["assets"][0]["dependencies"]={"assets/visual/moons/test/albedo.png"};write(root/"export/cooker-assets.json",config.dump());rejects([&]{cook_asset_repository(o);},"Dependency cycle accepted");
 // Generic project scan mode: a content tree with no reviewed export
 // manifests cooks into a single project-namespaced package.
 {
  const auto project=root/"project";
  write(project/"game.demo/content/readme.txt","hello engine");
  write(project/"game.demo/package.json","{}");
  write(project/"game.demo/content/nested/data.json","{\"v\":1}");
  fs::create_directories(project/"game.demo/content/img");
  fs::copy_file(root/"source/moon.png",project/"game.demo/content/img/moon.png");
  AssetCookOptions g;g.scan_content=true;g.package_group="game.demo";g.root=project;
  g.output=project/"build/cooked";g.cache=project/"build/cache";g.report=project/"build/report.json";g.threads=2;
  std::size_t last_done=0,last_total=0;
  g.progress=[&](std::size_t d,std::size_t t){last_done=d;last_total=t;};
  cook_asset_repository(g);
  require(last_done==4&&last_total==4,"Cook progress callback not fired");
  const auto gm=g.output/"Content/runtime.stmanifest";
  require(fs::is_regular_file(gm),"Scan-mode manifest missing");
  AssetRegistry gr(gm);gr.validate_all();
  require(gr.records().size()==4,"Scan-mode asset count wrong");
  const auto*img=gr.find("game.demo/content/img/moon.png");
  require(img&&img->type=="texture","Scan-mode image not texture-cooked");
  const auto*doc=gr.find("game.demo/content/readme.txt");
  require(doc&&!doc->chunks.empty(),"Scan-mode file missing chunks");
  for(const auto&record:gr.records())
    require(record.chunks.front().package.starts_with("game.demo-"),"Scan-mode package group not applied");
  const auto bytes=gr.read(*doc);const std::string text(bytes.begin(),bytes.end());
  require(text=="hello engine","Scan-mode payload corrupted");
  // ContentResolver serves the same paths from the dev-layout cooked
  // manifest (project/build/cooked) and the loose source tree.
  ContentResolver resolver{"game.demo",project,project/"build/host"};
  require(resolver.cooked_count()==4,"ContentResolver cooked count wrong");
  const auto*resolved_rec=resolver.find_cooked("readme.txt");
  require(resolved_rec&&resolved_rec->id==doc->id,"ContentResolver cooked lookup missed");
  const auto resolved=resolver.read_bytes("readme.txt");
  require(resolved&&std::string(resolved->begin(),resolved->end())=="hello engine","ContentResolver cooked bytes wrong");
  require(resolver.loose_path("readme.txt")==project/"packages/game.demo/content/readme.txt","ContentResolver loose path wrong");
  require(!resolver.read_bytes("missing.bin").has_value(),"ContentResolver should miss absent assets");
 }
 std::cout<<"Asset cooker, mip loading, deterministic cache and integrity checks passed\n";return 0;
}catch(const std::exception&e){unmount_asset_registry();std::cerr<<e.what()<<'\n';return 1;}}
