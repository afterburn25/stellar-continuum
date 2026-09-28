#include <stellar/engine/asset_audit.hpp>
#include <stellar/engine/sha256.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace stellar::engine {
namespace {
using Json=nlohmann::ordered_json;
std::string utf8(const std::filesystem::path& p){const auto s=p.generic_u8string();return std::string(s.begin(),s.end());}
std::string lower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string category(const std::filesystem::path& p) {
  const auto e=lower(utf8(p.extension())),s=lower(utf8(p));
  if(e==".png"||e==".jpg"||e==".jpeg"||e==".dds"||e==".ktx2"||e==".bmp"||e==".webp"||e==".exr"||e==".tga")return "textures";
  if(e==".wav"||e==".mp3"||e==".ogg"||e==".flac")return "audio";
  if(e==".fbx"||e==".obj"||e==".gltf"||e==".glb"||e==".blend")return "models";
  if(e==".pdb")return "debug-symbols";
  if(e==".exe"||e==".dll")return "binaries";
  if(e==".zip"||e==".7z"||e==".stpak")return "archives";
  if(e==".spv"||e==".vert"||e==".frag"||e==".hlsl")return "shaders";
  if(s.find("save")!=s.npos||e==".player17"||e==".dev17")return "saves";
  if(s.find("test")!=s.npos)return "tests";
  if(e==".cpp"||e==".hpp"||e==".h"||e==".c"||e==".cs"||e==".py"||e==".gd"||e==".cmake")return "source";
  if(e==".md"||e==".txt"||e==".pdf")return "documentation";
  if(s.starts_with("build")||s.starts_with("work/"))return "temporary-build";
  return "miscellaneous";
}
}
void audit_asset_repository(const std::filesystem::path& root,const std::filesystem::path& output,unsigned threads) {
  struct File {std::filesystem::path path;std::string name,kind,hash,error;std::uint64_t bytes{};};
  std::vector<File> files; Json errors=Json::array();
  std::error_code ec;
  auto end=std::filesystem::recursive_directory_iterator();
  for(auto it=std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied);it!=end;it.increment(ec)) {
    if(ec){errors.push_back(ec.message());ec.clear();continue;}
    if(it->is_symlink()){it.disable_recursion_pending();continue;}
    if(it->is_regular_file()) {
      const auto name=utf8(it->path().lexically_relative(root));
      if(name.starts_with(".git/"))continue;
      const auto size=it->file_size(ec);if(ec){errors.push_back(name+": "+ec.message());ec.clear();continue;}
      files.push_back({it->path(),name,category(it->path().lexically_relative(root)),{},{},size});
    }
  }
  std::sort(files.begin(),files.end(),[](const auto&a,const auto&b){return a.name<b.name;});
  std::atomic_size_t next{},done{};std::vector<std::jthread> workers;
  for(unsigned n=0;n<std::clamp(threads,1u,16u);++n)workers.emplace_back([&]{for(;;){const auto i=next++;if(i>=files.size())break;auto& f=files[i];
    if(f.kind=="textures"||f.kind=="audio"||f.kind=="models"||f.name.starts_with("assets/"))try{f.hash=sha256_file(f.path);}catch(const std::exception&e){f.error=e.what();}
    ++done;
  }});
  workers.clear();
  Json result={{"schemaVersion",1},{"root",utf8(root)},{"fileCount",files.size()},{"files",Json::array()},{"categories",Json::object()},{"directories",Json::object()},{"duplicates",Json::array()},{"errors",errors}};
  std::map<std::string,std::vector<std::size_t>> groups;
  std::uint64_t total{},duplicate_bytes{};
  for(std::size_t i=0;i<files.size();++i){const auto& f=files[i];total+=f.bytes;
    const bool runtime_candidate=f.name.starts_with("assets/");
    result["files"].push_back({{"path",f.name},{"category",f.kind},{"bytes",f.bytes},{"sha256",f.hash},{"state",runtime_candidate?"SOURCE_ONLY":"DEBUG_ONLY"},{"runtimeUse","unresolved until registry reference analysis"}});
    auto& c=result["categories"][f.kind];if(c.is_null())c={{"count",0},{"bytes",0}};c["count"]=c["count"].get<std::uint64_t>()+1;c["bytes"]=c["bytes"].get<std::uint64_t>()+f.bytes;
    auto dir=f.name.substr(0,f.name.find('/'));auto& d=result["directories"][dir];if(d.is_null())d=0;d=d.get<std::uint64_t>()+f.bytes;
    if(!f.hash.empty())groups[f.hash].push_back(i);
    if(!f.error.empty())result["errors"].push_back(f.error);
  }
  for(const auto&[hash,indices]:groups)if(indices.size()>1){Json paths=Json::array();for(auto i:indices)paths.push_back(files[i].name);const auto bytes=files[indices.front()].bytes*(indices.size()-1);duplicate_bytes+=bytes;result["duplicates"].push_back({{"sha256",hash},{"copies",indices.size()},{"redundantBytes",bytes},{"paths",paths}});}
  result["totalBytes"]=total;result["duplicateAssetBytes"]=duplicate_bytes;
  std::sort(files.begin(),files.end(),[](const auto&a,const auto&b){return a.bytes>b.bytes;});
  result["largestFiles"]=Json::array();for(std::size_t i=0;i<std::min<std::size_t>(100,files.size());++i)result["largestFiles"].push_back({{"path",files[i].name},{"bytes",files[i].bytes}});
  std::filesystem::create_directories(output.parent_path());std::ofstream report(output);report<<result.dump(2);if(!report)throw std::runtime_error("Could not write asset audit");
  std::cout<<"Audit: "<<files.size()<<" files, "<<total<<" bytes, "<<duplicate_bytes<<" exact duplicate asset bytes. Report: "<<output.string()<<'\n';
}
}

