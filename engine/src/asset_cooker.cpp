#include <stellar/engine/asset_cooker.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/sha256.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace stellar::engine {
namespace {
using Json=nlohmann::json;
using namespace stellar::native_map;
constexpr std::string_view cooker_version="stellar-cooker-v1.0.0-20260920";
struct Recipe {std::string alias,source,expected,category,group;std::vector<std::string>aliases;std::vector<std::string>dependencies;};
Json read_json(const std::filesystem::path&p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing cooker input: "+asset_path_utf8(p));return Json::parse(f);}
void write_json(const std::filesystem::path&p,const Json&j){std::filesystem::create_directories(p.parent_path());std::ofstream f(p);f<<j.dump(2);if(!f)throw std::runtime_error("Cannot write: "+asset_path_utf8(p));}
std::string digest(std::string_view text){return digest_hex(sha256(std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size())));}
void safe_relative(const std::string&s){auto p=std::filesystem::u8path(s);if(s.empty()||p.is_absolute()||s.find(':')!=s.npos||s.find('\\')!=s.npos)throw std::runtime_error("Unsafe cooker source: "+s);for(const auto&part:p)if(part==".."||part==".")throw std::runtime_error("Unsafe cooker source: "+s);}
std::string group_for(std::string_view s){if(s.find("/audio/")!=s.npos)return "Audio";if(s.find("stellar-eruptions")!=s.npos||s.find("phenomena")!=s.npos)return "VFX";if(s.find("background")!=s.npos||s.find("starfield")!=s.npos||s.find("/space/")!=s.npos||s.find("/galaxies/")!=s.npos)return "Backgrounds";if(s.find("/ships/")!=s.npos)return "Ships";if(s.find("/planets/")!=s.npos||s.find("/moons/")!=s.npos||s.find("/sol/")!=s.npos||s.find("/rings/")!=s.npos||s.find("/stellar/")!=s.npos||s.find("small-bodies")!=s.npos)return "Celestial";if(s.starts_with("assets/visual/"))return "UI";return "Core";}
std::string category_for(std::string_view s){if(s.ends_with("normal.png"))return "normal";if(s.ends_with("properties.png"))return "properties";const auto g=group_for(s);if(g=="Backgrounds")return "background";if(g=="UI")return "ui";if(g=="VFX")return "vfx";return "critical";}
std::map<std::string,Recipe> discover(const AssetCookOptions&o,Json&excluded){
 std::map<std::string,Recipe> recipes;
 auto add=[&](std::string alias,std::string source,std::string expected=""){
  safe_relative(alias);safe_relative(source);
  const bool unused=alias.find("/production/")!=alias.npos||alias.ends_with("provenance.json")||alias.find("/planets/")!=alias.npos&&(alias.ends_with("/clouds.png")||alias.ends_with("/thumbnail.png"));
  if(unused){excluded.push_back({{"source",source},{"state","SOURCE_ONLY"},{"reason","Not sampled by the native runtime"}});return;}
  Recipe r{alias,source,expected,category_for(alias),group_for(alias),{},{}};
  const auto it=recipes.find(alias);if(it!=recipes.end()){if(it->second.source!=source||!expected.empty()&&!it->second.expected.empty()&&it->second.expected!=expected)throw std::runtime_error("Conflicting runtime alias: "+alias);return;}
  recipes.emplace(alias,std::move(r));
 };
 // Import existing reviewed catalogs, preserving their allowlists and SHA pins.
 for(const auto&e:std::filesystem::directory_iterator(o.root/"export")){const auto name=asset_path_utf8(e.path().filename());if(!name.starts_with("native-")||!name.ends_with("-assets.json"))continue;
  const auto j=read_json(e.path());auto visit=[&](auto&&self,const Json&node)->void{
   if(node.is_object()){
    if(node.contains("runtimePath")&&node.contains("source")){
     // A reviewed import can record both its master SVG/image and its native
     // raster. Cook the pinned runtime representation, never the master bytes
     // under a misleading PNG alias.
     if(node.contains("runtimeSha256"))add(node.at("runtimePath"),node.at("runtimePath"),node.at("runtimeSha256"));
     else add(node.at("runtimePath"),node.at("source"),node.value("sha256",std::string{}));
     return;
    }
    if(node.contains("path")&&node.contains("sha256")){add(node.at("path"),node.at("path"),node.at("sha256"));return;}
    for(const auto&value:node)self(self,value);
   }else if(node.is_array())for(const auto&value:node)self(self,value);
  };visit(visit,j);
 }
 const std::string stars="assets/visual/stellar/";auto stellar=read_json(o.root/stars/"manifest.json");add(stars+"manifest.json",stars+"manifest.json");
 for(const auto&f:stellar.at("files"))add(stars+f.at("filename").get<std::string>(),stars+f.at("filename").get<std::string>(),f.at("sha256"));
 const std::string eruptions="assets/visual/stellar-eruptions/";auto vfx=read_json(o.root/eruptions/"manifest.json");add(eruptions+"manifest.json",eruptions+"manifest.json");
 for(const auto&set:vfx.at("visualSets"))for(const auto&texture:set.at("textures")){const auto name=texture.get<std::string>();const auto alias=eruptions+"1024/"+name;add(alias,alias);auto&r=recipes.at(alias);r.aliases={eruptions+"256/"+name,eruptions+"512/"+name};}
 const auto research=read_json(o.root/"export/research-runtime-files.json");for(const auto&f:research.at("files"))add(research.at("destination").get<std::string>()+"/"+f.get<std::string>(),research.at("root").get<std::string>()+"/"+f.get<std::string>());
 add("Data/astronomy/hyg-nearby-500-v1.json","data/astronomy/hyg-nearby-500-v1.json");
 if(std::filesystem::is_regular_file(o.root/"data/locale/en.json"))add("Data/locale/en.json","data/locale/en.json");
 // The cooker configuration is an additive reviewed override, never a recursive
 // 'include everything' switch. IDs remain stable while sources can be renamed.
 const auto config_path=o.root/"export/cooker-assets.json";
 if(std::filesystem::is_regular_file(config_path)){auto config=read_json(config_path);for(const auto&f:config.value("assets",Json::array())){const auto state=f.value("state",std::string("SOURCE_ONLY"));const auto alias=f.at("id").get<std::string>();
  const std::set<std::string> states{"ACCEPTED_RUNTIME","SOURCE_ONLY","REJECTED","QA_ONLY","EDITOR_ONLY","DEBUG_ONLY","DEPRECATED"};
  if(!states.contains(state))throw std::runtime_error("Unknown asset state: "+state);
  if(state!="ACCEPTED_RUNTIME"&&!(o.profile!="release"&&state=="QA_ONLY")&&!(o.profile=="development"&&(state=="EDITOR_ONLY"||state=="DEBUG_ONLY"))){recipes.erase(alias);excluded.push_back(f);continue;}
  recipes.erase(alias);add(alias,f.at("source"),f.value("sha256",std::string{}));auto&r=recipes.at(alias);r.category=f.value("category",r.category);r.group=f.value("package",r.group);r.aliases=f.value("aliases",r.aliases);r.dependencies=f.value("dependencies",r.dependencies);
  safe_relative(r.group);if(r.group.find('/')!=r.group.npos)throw std::runtime_error("Invalid package group");
 }}
 for(auto&[name,r]:recipes){if(name.ends_with("/manifest.json")||name.ends_with("/catalog.json")){const auto parent=name.substr(0,name.rfind('/')+1);for(const auto&[other,unused]:recipes)if(other!=name&&other.starts_with(parent))r.dependencies.push_back(other);}}
 if(!o.category.empty()){
  std::set<std::string> selected;std::function<void(const std::string&)> include=[&](const auto&id){if(!selected.insert(id).second)return;const auto it=recipes.find(id);if(it==recipes.end())throw std::runtime_error("Missing dependency: "+id);for(const auto&dep:it->second.dependencies)include(dep);};
  for(const auto&[id,r]:recipes)if(r.group==o.category||r.category==o.category)include(id);
  std::erase_if(recipes,[&](const auto&entry){return !selected.contains(entry.first);});
 }
 return recipes;
}
struct Cooked {AssetRecord record;std::string group,key;std::vector<std::filesystem::path> files;bool hit{},fallback{};std::string error;};
}
void cook_asset_repository(const AssetCookOptions&o){
 if(o.profile!="release"&&o.profile!="qa"&&o.profile!="development")throw std::runtime_error("Unknown cooker profile");
 const auto started=std::chrono::steady_clock::now();Json excluded=Json::array();auto recipes=discover(o,excluded);
 // Source and recursive dependency fingerprints precede workers, so changes in
 // imported metadata invalidate dependent products deterministically.
 std::map<std::string,std::string> hashes,keys;std::set<std::string> visiting;
 for(const auto&[id,r]:recipes){const auto hash=sha256_file(o.root/std::filesystem::u8path(r.source));if(!r.expected.empty()&&hash!=r.expected)throw std::runtime_error("Reviewed asset SHA mismatch: "+r.source);hashes[id]=hash;}
 std::function<std::string(const std::string&)> fingerprint=[&](const auto&id)->std::string{
  if(keys.contains(id))return keys.at(id);if(!recipes.contains(id))throw std::runtime_error("Missing dependency: "+id);if(!visiting.insert(id).second)throw std::runtime_error("Cyclic asset dependency: "+id);
  const auto&r=recipes.at(id);std::string input=std::string(cooker_version)+"/windows/"+STELLAR_COOKER_IMPLEMENTATION_HASH+"/"+o.profile+"/"+r.category+"/"+hashes.at(id);
  for(const auto&d:r.dependencies)input+="/"+d+"/"+fingerprint(d);visiting.erase(id);return keys[id]=digest(input);
 };
 for(const auto&[id,r]:recipes)(void)fingerprint(id);
 std::string generation_input;for(const auto&[id,key]:keys)generation_input+=id+key;
 const auto generation=digest(generation_input).substr(0,16);
 std::filesystem::create_directories(o.cache);if(o.package)std::filesystem::create_directories(o.output/"Content");
 std::vector<Recipe> ordered;for(auto&[name,r]:recipes)ordered.push_back(std::move(r));std::vector<Cooked> cooked(ordered.size());
 std::atomic_size_t next{},done{};std::mutex console;std::vector<std::jthread> workers;
 for(unsigned t=0;t<std::clamp(o.threads,1u,16u);++t)workers.emplace_back([&]{for(;;){const auto i=next++;if(i>=ordered.size())break;const auto&r=ordered[i];auto&c=cooked[i];try{
   const auto source=o.root/std::filesystem::u8path(r.source);const auto&source_hash=hashes.at(r.alias);
   c.group=r.group;c.key=keys.at(r.alias);
   const auto cached=o.cache/(c.key+".json");Json meta;
   if(!o.clean&&std::filesystem::is_regular_file(cached)){try{meta=read_json(cached);bool valid=meta.at("chunks").is_array()&&!meta.at("chunks").empty();for(const auto&ch:meta.at("chunks")){const auto name=ch.at("file").get<std::string>();safe_relative(name);if(name.find('/')!=name.npos)throw std::runtime_error("Invalid cache name");const auto file=o.cache/name;if(!std::filesystem::is_regular_file(file)||sha256_file(file)!=ch.at("storedHash").get<std::string>()){valid=false;break;}}if(valid)c.hit=true;}catch(const std::exception&){meta=Json{};}}
   if(!c.hit){std::vector<Bc1MipLevel> levels;std::string format="bytes",type="data";
    const auto ext=asset_path_utf8(source.extension());double rmse=0,max_error=0;
    if(ext==".png"||ext==".jpg"||ext==".jpeg"){auto image=decode_rgba_image(source);auto texture=cook_texture(*image,r.category);format=texture_format_name(texture.format);type="texture";levels=std::move(texture.mips);rmse=texture.rmse;max_error=texture.max_error;c.fallback=texture.lossless_fallback;}
    else {auto bytes=read_resource(source);
     if(r.alias=="assets/visual/stellar-eruptions/manifest.json"){
      const auto source_json=Json::parse(bytes);const auto runtime=Json{{"version",1},{"visualSets",source_json.at("visualSets")}}.dump();bytes.assign(runtime.begin(),runtime.end());
     }else if(r.alias=="assets/visual/stellar/manifest.json"){
      const auto source_json=Json::parse(bytes);const auto runtime=Json{{"version",1},{"objects",source_json.at("objects")}}.dump();bytes.assign(runtime.begin(),runtime.end());
     }
     levels.push_back({0,0,std::move(bytes)});if(c.group=="Audio")type="audio";if(ext==".ttf")type="font";
    }
    meta={{"version",1},{"format",format},{"type",type},{"rmse",rmse},{"maxError",max_error},{"losslessFallback",c.fallback},{"chunks",Json::array()}};
    for(std::size_t l=0;l<levels.size();++l){auto&level=levels[l];AssetCodec codec{};const auto stored=compress_asset_bytes(level.blocks,codec,format=="RGBA8");const auto stored_hash=digest_hex(sha256(stored));const auto file=stored_hash+".chunk";const auto target=o.cache/file;
     // Same content can be discovered concurrently. Private temporary names keep
     // every published cache blob complete; replacement data is identical.
     if(!std::filesystem::is_regular_file(target)||sha256_file(target)!=stored_hash)write_file_atomically(target,std::as_bytes(std::span(stored)));
     meta["chunks"].push_back({{"file",file},{"storedHash",stored_hash},{"hash",digest_hex(sha256(level.blocks))},{"raw",level.blocks.size()},{"stored",stored.size()},{"codec",static_cast<unsigned>(codec)},{"width",level.width},{"height",level.height}});
    }
    const auto text=meta.dump();write_file_atomically(cached,std::as_bytes(std::span(text.data(),text.size())));
   }
   c.fallback=meta.value("losslessFallback",false);auto&a=c.record;a.id=r.alias;a.aliases=r.aliases;a.dependencies=r.dependencies;a.type=meta.at("type");a.subtype=r.category;a.format=meta.at("format");a.source_hash=source_hash;a.source_bytes=std::filesystem::file_size(source);a.quality_rmse=meta.at("rmse");a.quality_max_error=meta.at("maxError");
   for(const auto&ch:meta.at("chunks")){AssetChunk chunk;chunk.hash=ch.at("hash");chunk.raw_bytes=ch.at("raw");chunk.stored_bytes=ch.at("stored");chunk.codec=static_cast<AssetCodec>(ch.at("codec").get<unsigned>());chunk.width=ch.at("width");chunk.height=ch.at("height");a.chunks.push_back(chunk);c.files.push_back(o.cache/ch.at("file").get<std::string>());}
   a.width=a.canvas_width=a.chunks.front().width;a.height=a.canvas_height=a.chunks.front().height;
  }catch(const std::exception&e){c.error=e.what();}
  const auto finished=++done;if(finished%100==0||finished==ordered.size()){std::lock_guard lock(console);std::cout<<"Cooked "<<finished<<" / "<<ordered.size()<<" assets\n"<<std::flush;}
 }});
 workers.clear();Json errors=Json::array();for(std::size_t i=0;i<cooked.size();++i)if(!cooked[i].error.empty())errors.push_back({{"asset",ordered[i].alias},{"error",cooked[i].error}});
 if(!errors.empty()){write_json(o.report,{{"errors",errors},{"excluded",excluded}});throw std::runtime_error("Asset cooking failed; see "+asset_path_utf8(o.report));}
 std::map<std::string,AssetChunk>unique;std::map<std::string,std::unique_ptr<std::ofstream>>packages;std::vector<AssetRecord>records;
 Json report={{"schemaVersion",1},{"cookerVersion",cooker_version},{"profile",o.profile},{"sourceBytes",0},{"cookedLogicalBytes",0},{"cookedUniqueBytes",0},{"cacheHits",0},{"recooked",0},{"duplicateChunks",0},{"duplicateBytes",0},{"losslessFallbacks",0},{"excluded",excluded},{"assets",Json::array()},{"formats",Json::object()}};
 auto inc=[&](std::string_view key,std::uint64_t n=1){report[key]=report[key].get<std::uint64_t>()+n;};
 for(std::size_t i=0;i<cooked.size();++i){auto&c=cooked[i];auto&r=c.record;inc("sourceBytes",r.source_bytes);inc(c.hit?"cacheHits":"recooked");if(c.fallback)inc("losslessFallbacks");std::uint64_t size=0,gpu=0;
  for(std::size_t l=0;l<r.chunks.size();++l){auto&ch=r.chunks[l];inc("cookedLogicalBytes",ch.stored_bytes);size+=ch.stored_bytes;gpu+=ch.raw_bytes;
   const auto key=ch.hash+"/"+std::to_string(static_cast<unsigned>(ch.codec));auto found=unique.find(key);
   if(found!=unique.end()){const int w=ch.width,h=ch.height;ch=found->second;ch.width=w;ch.height=h;inc("duplicateChunks");inc("duplicateBytes",ch.stored_bytes);}
   else{inc("cookedUniqueBytes",ch.stored_bytes);ch.package=c.group+"-"+generation+".stpak";
    if(o.package){auto&f=packages[ch.package];if(!f){initialize_asset_package(o.output/"Content"/(ch.package+".pending"));f=std::make_unique<std::ofstream>(o.output/"Content"/(ch.package+".pending"),std::ios::binary|std::ios::app);f->seekp(0,std::ios::end);}ch.offset=static_cast<std::uint64_t>(f->tellp());std::ifstream input(c.files[l],std::ios::binary);if(ch.stored_bytes)*f<<input.rdbuf();if(!*f)throw std::runtime_error("Package write failed");}
    unique.emplace(key,ch);
   }
  }
  auto&format=report["formats"][r.format];if(format.is_null())format=0;format=format.get<std::size_t>()+1;
  report["assets"].push_back({{"id",r.id},{"source",ordered[i].source},{"state","ACCEPTED_RUNTIME"},{"sourceBytes",r.source_bytes},{"cookedBytes",size},{"gpuMipBytes",r.type=="texture"?gpu:0},{"format",r.format},{"category",r.subtype},{"width",r.width},{"height",r.height},{"mips",r.chunks.size()},{"losslessFallback",c.fallback},{"testedRmse",r.quality_rmse},{"testedMaxError",r.quality_max_error},{"cacheKey",c.key},{"dependencies",r.dependencies}});records.push_back(r);
 }
 std::vector<std::string> published;for(const auto&[name,stream]:packages)published.push_back(name);packages.clear();
 for(const auto&name:published){const auto target=o.output/"Content"/name,temporary=o.output/"Content"/(name+".pending");
  if(std::filesystem::is_regular_file(target)){if(sha256_file(target)==sha256_file(temporary)){std::filesystem::remove(temporary);continue;}throw std::runtime_error("Existing package generation is corrupt; cook to a new output directory: "+name);}
  std::filesystem::rename(temporary,target);
 }
 if(o.package){write_asset_manifest(o.output/"Content/runtime.stmanifest",records,o.profile);AssetRegistry registry(o.output/"Content/runtime.stmanifest");if(o.validate)registry.validate_all();report["validatedReads"]=registry.diagnostics().reads;}
 report["elapsedSeconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();report["assetCount"]=records.size();
 auto largest=report["assets"];std::sort(largest.begin(),largest.end(),[](const Json&a,const Json&b){return a.at("cookedBytes").get<std::uint64_t>()>b.at("cookedBytes").get<std::uint64_t>();});while(largest.size()>100)largest.erase(largest.end()-1);report["top100RuntimeAssets"]=largest;
 write_json(o.report,report);std::cout<<"Cook complete: "<<records.size()<<" assets, "<<report["cookedUniqueBytes"]<<" unique stored bytes; "<<report["cacheHits"]<<" cache hits\n";
}
}


