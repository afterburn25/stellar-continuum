#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/sha256.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <compressapi.h>
#endif
namespace stellar::engine {
namespace {
using Json=nlohmann::json;
constexpr std::size_t maximum_chunk=256u*1024u*1024u;
std::mutex mount_mutex;
std::shared_ptr<const AssetRegistry> mounted;
std::filesystem::path mount_root;
bool fallback=true;
std::string normalized(std::string s){std::replace(s.begin(),s.end(),'\\','/');for(auto&c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return s;}
void safe_name(std::string_view name){if(name.empty()||name.front()=='/'||name.find(':')!=name.npos||name.find('\\')!=name.npos)throw std::runtime_error("Unsafe asset name: "+std::string(name));std::istringstream s{std::string(name)};std::string part;while(std::getline(s,part,'/'))if(part==".."||part=="."||part.empty())throw std::runtime_error("Unsafe asset name: "+std::string(name));}
bool valid_hash(std::string_view s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::vector<std::uint8_t> read_file(const std::filesystem::path&p,std::size_t limit){const auto n=std::filesystem::file_size(p);if(n>limit)throw std::runtime_error("Resource exceeds budget: "+asset_path_utf8(p));std::vector<std::uint8_t>b(static_cast<std::size_t>(n));std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(n));if(!f)throw std::runtime_error("Failed reading resource: "+asset_path_utf8(p));return b;}
Json chunk_json(const AssetChunk&c){return Json{{"package",c.package},{"hash",c.hash},{"offset",c.offset},{"stored",c.stored_bytes},{"raw",c.raw_bytes},{"codec",static_cast<unsigned>(c.codec)},{"width",c.width},{"height",c.height}};}
}
std::string asset_path_utf8(const std::filesystem::path&p){const auto s=p.generic_u8string();return {s.begin(),s.end()};}
namespace {
#ifdef _WIN32
std::vector<std::uint8_t> windows_compress(std::span<const std::uint8_t> bytes,DWORD algorithm){
  COMPRESSOR_HANDLE compressor{};
  if(!CreateCompressor(algorithm,nullptr,&compressor))return {};
  SIZE_T required{};Compress(compressor,bytes.data(),bytes.size(),nullptr,0,&required);
  std::vector<std::uint8_t> result(required);
  const auto ok=Compress(compressor,bytes.data(),bytes.size(),result.data(),result.size(),&required);CloseCompressor(compressor);
  if(!ok||required+64>=bytes.size()*95/100)return {};
  result.resize(required);return result;
}
#endif
}
std::vector<std::uint8_t> compress_asset_bytes(std::span<const std::uint8_t> bytes,AssetCodec& codec,bool rgba_predictor){
  codec=AssetCodec::None;
  if(bytes.empty())return {};
  std::vector<std::uint8_t> best(bytes.begin(),bytes.end());
#ifdef _WIN32
  // Huffman-only coding is cheap but weak on image data; LZMS costs more cook
  // time and is tried only where the bytes justify it (the RGBA8 path).
  if(rgba_predictor)if(auto packed=windows_compress(bytes,COMPRESS_ALGORITHM_LZMS);!packed.empty()&&packed.size()<best.size()){best=std::move(packed);codec=AssetCodec::Lzms;}
  if(auto packed=windows_compress(bytes,COMPRESS_ALGORITHM_XPRESS_HUFF);!packed.empty()&&packed.size()<best.size()){best=std::move(packed);codec=AssetCodec::XpressHuff;}
  if(rgba_predictor&&bytes.size()>4&&bytes.size()%4==0){
    std::vector<std::uint8_t> delta(bytes.begin(),bytes.end());
    for(std::size_t i=4;i<bytes.size();++i)delta[i]=static_cast<std::uint8_t>(bytes[i]-bytes[i-4]);
    if(auto packed=windows_compress(delta,COMPRESS_ALGORITHM_LZMS);!packed.empty()&&packed.size()<best.size()){best=std::move(packed);codec=AssetCodec::LzmsRgbaDelta;}
    if(auto packed=windows_compress(delta,COMPRESS_ALGORITHM_XPRESS_HUFF);!packed.empty()&&packed.size()<best.size()){best=std::move(packed);codec=AssetCodec::XpressRgbaDelta;}
  }
#endif
  return best;
}
std::vector<std::uint8_t> decompress_asset_bytes(std::span<const std::uint8_t> bytes,AssetCodec codec,std::size_t raw){
  if(raw>maximum_chunk)throw std::runtime_error("Asset chunk exceeds decompression budget");
  if(codec==AssetCodec::XpressRgbaDelta||codec==AssetCodec::LzmsRgbaDelta){
    if(raw%4)throw std::runtime_error("Invalid RGBA predictor length");
    auto out=decompress_asset_bytes(bytes,codec==AssetCodec::LzmsRgbaDelta?AssetCodec::Lzms:AssetCodec::XpressHuff,raw);
    for(std::size_t i=4;i<out.size();++i)out[i]=static_cast<std::uint8_t>(out[i]+out[i-4]);
    return out;
  }
  if(codec==AssetCodec::None){if(bytes.size()!=raw)throw std::runtime_error("Uncompressed asset length mismatch");return {bytes.begin(),bytes.end()};}
#ifdef _WIN32
  if(codec==AssetCodec::XpressHuff||codec==AssetCodec::Lzms){const auto algorithm=codec==AssetCodec::Lzms?COMPRESS_ALGORITHM_LZMS:COMPRESS_ALGORITHM_XPRESS_HUFF;DECOMPRESSOR_HANDLE handle{};if(!CreateDecompressor(algorithm,nullptr,&handle))throw std::runtime_error("Windows decompressor unavailable");std::vector<std::uint8_t> out(raw);SIZE_T actual{};const auto ok=Decompress(handle,bytes.data(),bytes.size(),out.data(),out.size(),&actual);CloseDecompressor(handle);if(!ok||actual!=raw)throw std::runtime_error("Invalid compressed asset chunk");return out;}
#endif
  throw std::runtime_error("Unsupported package compression codec");
}
void initialize_asset_package(const std::filesystem::path&p){std::ofstream f(p,std::ios::binary|std::ios::trunc);const std::array<char,16>header{'S','T','P','A','K','0','0','1',1,0,0,0,0,0,0,0};f.write(header.data(),header.size());if(!f)throw std::runtime_error("Cannot create package");}
void write_asset_manifest(const std::filesystem::path&p,std::span<const AssetRecord>records,std::string_view profile){
  Json j={{"version",1},{"platform","windows"},{"profile",profile},{"assets",Json::array()},{"packages",Json::object()}};
  for(const auto&r:records){Json chunks=Json::array();for(const auto&c:r.chunks){chunks.push_back(chunk_json(c));j["packages"][c.package]=std::filesystem::file_size(p.parent_path()/c.package);}
    j["assets"].push_back({{"id",r.id},{"type",r.type},{"subtype",r.subtype},{"format",r.format},{"sourceHash",r.source_hash},{"sourceBytes",r.source_bytes},{"aliases",r.aliases},{"dependencies",r.dependencies},{"width",r.width},{"height",r.height},{"canvasWidth",r.canvas_width},{"canvasHeight",r.canvas_height},{"cropX",r.crop_x},{"cropY",r.crop_y},{"rmse",r.quality_rmse},{"maxError",r.quality_max_error},{"chunks",chunks}});
  }
  const auto bytes=Json::to_cbor(j);const auto hash=sha256(bytes);
  std::vector<std::uint8_t> output{'S','T','M','N','F','0','0','1'};output.insert(output.end(),hash.begin(),hash.end());output.insert(output.end(),bytes.begin(),bytes.end());
  write_file_atomically(p,std::as_bytes(std::span(output)));
}
struct AssetRegistry::Impl {std::filesystem::path directory;std::vector<AssetRecord>records;std::map<std::string,std::size_t>lookup;mutable std::mutex mutex;mutable AssetDiagnostics stats;};
AssetRegistry::~AssetRegistry()=default;
AssetRegistry::AssetRegistry(const std::filesystem::path& manifest):impl_(std::make_unique<Impl>()){
  impl_->directory=manifest.parent_path();const auto bytes=read_file(manifest,32u*1024u*1024u);
  if(bytes.size()<40||std::string_view(reinterpret_cast<const char*>(bytes.data()),8)!="STMNF001")throw std::runtime_error("Invalid cooked manifest header/version");
  const auto body=std::span(bytes).subspan(40);const auto hash=sha256(body);if(!std::equal(hash.begin(),hash.end(),bytes.begin()+8))throw std::runtime_error("Cooked manifest checksum mismatch");
  const auto j=Json::from_cbor(body);if(j.at("version")!=1||j.at("platform")!="windows")throw std::runtime_error("Incompatible asset manifest");
  std::map<std::string,std::uint64_t> packages;
  for(const auto&[name,size]:j.at("packages").items()){safe_name(name);if(std::filesystem::path(name).filename()!=name||!name.ends_with(".stpak"))throw std::runtime_error("Invalid package name");const auto p=impl_->directory/name;const auto expected=size.get<std::uint64_t>();if(std::filesystem::file_size(p)!=expected)throw std::runtime_error("Truncated or replaced package: "+name);std::ifstream f(p,std::ios::binary);std::array<char,16>header{};f.read(header.data(),header.size());if(!f||std::string_view(header.data(),8)!="STPAK001"||header[8]!=1||std::any_of(header.begin()+9,header.end(),[](char c){return c!=0;}))throw std::runtime_error("Invalid package header: "+name);packages[name]=expected;}
  for(const auto&a:j.at("assets")){if(impl_->records.size()>=100000)throw std::runtime_error("Excessive asset count");AssetRecord r;r.id=a.at("id");r.type=a.at("type");r.subtype=a.at("subtype");r.format=a.at("format");r.source_hash=a.at("sourceHash");r.source_bytes=a.at("sourceBytes");r.aliases=a.at("aliases").get<std::vector<std::string>>();r.dependencies=a.at("dependencies").get<std::vector<std::string>>();r.width=a.at("width");r.height=a.at("height");r.canvas_width=a.at("canvasWidth");r.canvas_height=a.at("canvasHeight");r.crop_x=a.at("cropX");r.crop_y=a.at("cropY");r.quality_rmse=a.at("rmse");r.quality_max_error=a.at("maxError");
    auto add=[&](const std::string&name){safe_name(name);if(!impl_->lookup.emplace(normalized(name),impl_->records.size()).second)throw std::runtime_error("Duplicate asset ID/alias: "+name);};add(r.id);for(const auto&alias:r.aliases)if(normalized(alias)!=normalized(r.id))add(alias);
    for(const auto&c:a.at("chunks")){AssetChunk ch;ch.package=c.at("package");ch.hash=c.at("hash");ch.offset=c.at("offset");ch.stored_bytes=c.at("stored");ch.raw_bytes=c.at("raw");ch.codec=static_cast<AssetCodec>(c.at("codec").get<unsigned>());ch.width=c.at("width");ch.height=c.at("height");auto pi=packages.find(ch.package);
      if(pi==packages.end()||ch.offset<16||ch.offset>pi->second||ch.stored_bytes>pi->second-ch.offset||ch.raw_bytes>maximum_chunk||ch.stored_bytes>maximum_chunk||static_cast<unsigned>(ch.codec)>4||!valid_hash(ch.hash)||(ch.codec==AssetCodec::None&&ch.raw_bytes!=ch.stored_bytes))throw std::runtime_error("Invalid chunk bounds/codec: "+r.id+" package="+ch.package+" offset="+std::to_string(ch.offset));r.chunks.push_back(std::move(ch));}
    if(r.chunks.empty()||r.chunks.size()>14||!valid_hash(r.source_hash)||!std::isfinite(r.quality_rmse)||r.quality_rmse<0||!std::isfinite(r.quality_max_error)||r.quality_max_error<0)throw std::runtime_error("Invalid asset metadata: "+r.id);
    if(r.type=="texture"){
      if(r.width<1||r.height<1||r.width>8192||r.height>8192||static_cast<std::uint64_t>(r.width)*r.height*4>64u*1024u*1024u||r.canvas_width<r.width||r.canvas_height<r.height||r.canvas_width>8192||r.canvas_height>8192||r.crop_x<0||r.crop_y<0||r.crop_x>r.canvas_width-r.width||r.crop_y>r.canvas_height-r.height)throw std::runtime_error("Invalid texture bounds: "+r.id);
      if(r.format!="RGBA8"&&r.format!="BC7"&&r.format!="BC5"&&r.format!="BC4")throw std::runtime_error("Unknown texture format: "+r.id);
      int w=r.width,h=r.height;std::uint64_t total=0;
      for(std::size_t i=0;i<r.chunks.size();++i){const auto&c=r.chunks[i];const auto expected=r.format=="RGBA8"?static_cast<std::uint64_t>(w)*h*4:static_cast<std::uint64_t>((w+3)/4)*((h+3)/4)*(r.format=="BC4"?8:16);
        if(c.width!=w||c.height!=h||c.raw_bytes!=expected||(w==1&&h==1&&i+1!=r.chunks.size()))throw std::runtime_error("Invalid texture mip: "+r.id);
        total+=expected;w=std::max(1,w/2);h=std::max(1,h/2);
      }
      if(r.chunks.back().width!=1||r.chunks.back().height!=1||total>128u*1024u*1024u)throw std::runtime_error("Incomplete or excessive texture chain: "+r.id);
    }else if(r.chunks.size()!=1||r.format!="bytes"||r.width||r.height)throw std::runtime_error("Invalid binary asset: "+r.id);
    impl_->records.push_back(std::move(r));
  }
  for(const auto&r:impl_->records)for(const auto&d:r.dependencies)if(!find(d))throw std::runtime_error("Missing asset dependency: "+r.id+" -> "+d);
  impl_->stats.assets=impl_->records.size();impl_->stats.packages=packages.size();
}
const AssetRecord* AssetRegistry::find(std::string_view id)const{auto it=impl_->lookup.find(normalized(std::string(id)));return it==impl_->lookup.end()?nullptr:&impl_->records[it->second];}
const std::vector<AssetRecord>& AssetRegistry::records()const{return impl_->records;}
std::vector<std::uint8_t> AssetRegistry::read(const AssetRecord&r,std::size_t index)const{
  try{if(find(r.id)!=&r)throw std::runtime_error("Asset record does not belong to this registry");const auto&c=r.chunks.at(index);std::ifstream f(impl_->directory/c.package,std::ios::binary);f.seekg(static_cast<std::streamoff>(c.offset));std::vector<std::uint8_t>b(static_cast<std::size_t>(c.stored_bytes));f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("Read failed");auto raw=decompress_asset_bytes(b,c.codec,static_cast<std::size_t>(c.raw_bytes));if(digest_hex(sha256(raw))!=c.hash)throw std::runtime_error("Chunk checksum mismatch");std::lock_guard lock(impl_->mutex);++impl_->stats.reads;impl_->stats.bytes_read+=b.size();impl_->stats.decoded_bytes+=raw.size();return raw;
  }catch(const std::exception&e){const auto context="asset="+r.id+" type="+r.type+" chunk="+std::to_string(index)+(index<r.chunks.size()?" package="+r.chunks[index].package+" offset="+std::to_string(r.chunks[index].offset):"")+": "+e.what();{std::lock_guard lock(impl_->mutex);++impl_->stats.failures;auto&v=impl_->stats.recent_failures;if(v.size()==16)v.erase(v.begin());v.push_back(context);}throw std::runtime_error(context);}
}
AssetDiagnostics AssetRegistry::diagnostics()const{std::lock_guard lock(impl_->mutex);return impl_->stats;}
void AssetRegistry::validate_all()const{for(const auto&r:impl_->records)for(std::size_t i=0;i<r.chunks.size();++i)(void)read(r,i);}
void mount_asset_registry(std::filesystem::path root,bool allow){auto r=std::make_shared<AssetRegistry>(root/"Content/runtime.stmanifest");std::lock_guard lock(mount_mutex);mount_root=std::filesystem::absolute(root).lexically_normal();fallback=allow;mounted=std::move(r);}
void unmount_asset_registry(){std::lock_guard lock(mount_mutex);mounted.reset();mount_root.clear();fallback=true;}
std::shared_ptr<const AssetRegistry> mounted_asset_registry(){std::lock_guard lock(mount_mutex);return mounted;}
std::string asset_alias(const std::filesystem::path&p){std::lock_guard lock(mount_mutex);if(!p.is_absolute())return asset_path_utf8(p.lexically_normal());return asset_path_utf8(p.lexically_normal().lexically_relative(mount_root));}
bool resource_exists(const std::filesystem::path&p){auto r=mounted_asset_registry();if(r&&r->find(asset_alias(p)))return true;{std::lock_guard lock(mount_mutex);if(r&&!fallback)return false;}return std::filesystem::is_regular_file(p);}
std::vector<std::uint8_t> read_resource(const std::filesystem::path&p,std::size_t maximum){auto r=mounted_asset_registry();if(r){if(const auto*a=r->find(asset_alias(p))){if(a->chunks.front().raw_bytes>maximum)throw std::runtime_error("Resource exceeds requested budget: "+a->id);return r->read(*a);}std::lock_guard lock(mount_mutex);if(!fallback)throw std::runtime_error("Missing cooked asset (source fallback disabled): "+asset_path_utf8(p));}return read_file(p,maximum);}
std::istringstream resource_stream(const std::filesystem::path&p){if(!resource_exists(p)){std::istringstream s;s.setstate(std::ios_base::failbit);return s;}auto b=read_resource(p);return std::istringstream(std::string(b.begin(),b.end()));}
}

