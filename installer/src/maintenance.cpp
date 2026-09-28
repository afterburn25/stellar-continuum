#include "stellar/installer/maintenance.hpp"
#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/runtime_directory_lease.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace stellar::installer {
namespace {
constexpr auto transaction_name=".stellar-transaction";
constexpr auto record_name="install_manifest.json";
constexpr std::uint64_t margin=64ULL*1024*1024;
std::string lower(std::string s) {for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
fs::path relative(std::string_view value) {return path_from_utf8(value);}
std::string utf8(const fs::path& p) {auto s=p.u8string();return {s.begin(),s.end()};}
bool same_path(const fs::path& a,const fs::path& b) {return lower(utf8(fs::weakly_canonical(a)))==lower(utf8(fs::weakly_canonical(b)));}
void require(bool value,std::string message) {if(!value)throw std::runtime_error(std::move(message));}
void durable_move(const fs::path& from,const fs::path& to) {
  fs::create_directories(to.parent_path());
  if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Cannot move "+utf8(from)+" to "+utf8(to));
}
bool matches(const fs::path& root,const File& file) {
  validate_tree_path(root,relative(file.path)); const auto p=root/relative(file.path);
  return fs::is_regular_file(p)&&fs::file_size(p)==file.bytes&&sha256_file(p)==file.sha256;
}
File parse_file(const Json& j) {
  File f{j.at("path").get<std::string>(),j.at("sha256").get<std::string>(),j.value("package",std::string{}),j.at("bytes").get<std::uint64_t>()};
  validate_relative_path(f.path);
  require(f.sha256.size()==64&&f.sha256.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid file hash: "+f.path);
  require(j.at("bytes").is_number_unsigned()|| (j.at("bytes").is_number_integer()&&j.at("bytes").get<std::int64_t>()>=0),"Negative file size.");
  require(f.bytes<=128ULL*1024*1024*1024,"Package is too large."); return f;
}
Json file_json(const File& f) {return {{"path",f.path},{"sha256",f.sha256},{"bytes",f.bytes},{"package",f.package}};}
Json registration_json(const Registration& r) {return {{"path",utf8(r.root)},{"installId",r.install_id},{"version",r.version},{"installedAt",r.installed_at},{"desktop",r.desktop},{"developer",r.developer}};}
void check_record(const Json& record,const Registration& registered,const fs::path& root) {
  require(record.at("productId")==product_id&&record.at("manifestVersion")==1,"Unknown installed product record.");
  auto r=record.at("installation");
  require(r.at("installId")==registered.install_id&&r.at("version")==registered.version&&same_path(relative(r.at("path").get<std::string>()),root),"Installation registration does not match install_manifest.json. Restore the matching installation record before continuing.");
  require(record.at("release").at("gameVersion")==registered.version,"Installed release version disagrees with registration.");
  (void)Release::parse(record.at("release"));
}
Json installed_record(const fs::path& root,const Registration& registered,Platform& platform) {
  validate_tree_path(root,record_name);
  try {auto j=read_json(root/record_name);check_record(j,registered,root);return j;}
  catch(const std::exception&) {auto backup=platform.record_backup();if(backup.is_null())throw;check_record(backup,registered,root);platform.log("Using registered installation metadata to repair a missing/damaged local manifest.");return backup;}
}
void notify(const Request& r,std::string phase,std::string file,std::uint64_t n,std::uint64_t total,bool cancel) {
  if(r.progress)r.progress({std::move(phase),std::move(file),n,total,cancel});
  if(cancel&&r.cancel&&r.cancel->load())throw std::runtime_error("Cancelled. The existing game was not changed.");
}
void clear_transaction(const fs::path& root) {
  const auto txn=root/transaction_name;validate_tree_path(root,transaction_name);
  if(!fs::exists(txn))return;
  // Never follow a junction/symlink planted inside maintenance staging.
  for(const auto& e:fs::recursive_directory_iterator(txn))validate_tree_path(root,fs::relative(e.path(),root));
  fs::remove_all(txn);
}
void checkpoint(const Request& r,std::string_view point) {if(r.checkpoint)r.checkpoint(point);}
}

void validate_relative_path(std::string_view value) {
  require(!value.empty()&&value.size()<240,"Invalid package path length.");
  require(std::all_of(value.begin(),value.end(),[](char c){return static_cast<unsigned char>(c)>=32&&static_cast<unsigned char>(c)<127;}),"Shipping file names must be ASCII; the installation folder may contain Unicode.");
  require(value.find_first_of("\\:\0",0,3)==value.npos&&value.front()!='/'&&value.back()!='/',"Absolute paths, alternate streams and backslashes are forbidden.");
  auto p=relative(value);
  for(const auto& part:p) {
    auto s=lower(utf8(part)); auto stem=s.substr(0,s.find('.'));
    require(s!="."&&s!=".."&&!s.empty()&&s.back()!='.'&&s.back()!=' '&&s.find_first_of("<>\"|?*") ==s.npos,"Unsafe package path.");
    require(std::none_of(s.begin(),s.end(),[](char c){return static_cast<unsigned char>(c)<32;}),"Control character in path.");
    require(stem!="con"&&stem!="prn"&&stem!="aux"&&stem!="nul"&&!(stem.size()==4&&(stem.starts_with("com")||stem.starts_with("lpt"))&&stem[3]>='0'&&stem[3]<='9'),"Reserved Windows path.");
  }
  require(value.find("//")==value.npos,"Empty path component.");
  // A release is a cooked runtime allowlist, not an arbitrary file deployment.
  static const std::set<std::string> top={"stellar-continuum-native.exe","SDL3.dll","StellarContinuumUninstall.exe","cooked-only.marker","Play Game.cmd","Developer Game.cmd","README.txt"};
  require(top.contains(std::string(value))||value=="Content/runtime.stmanifest"||(value.starts_with("Content/")&&value.ends_with(".stpak")&&std::count(value.begin(),value.end(),'/')==1)||(value.starts_with("Licenses/")&&value.ends_with(".txt")&&std::count(value.begin(),value.end(),'/')==1),"File is outside the shipping allowlist: "+std::string(value));
}
void validate_tree_path(const fs::path& root,const fs::path& rel) {
  auto full=fs::absolute(root/rel).lexically_normal(); auto base=fs::absolute(root).lexically_normal();
  auto remaining=full.lexically_relative(base);
  require(!remaining.empty()&&*remaining.begin()!=L"..","Path escapes installation directory.");
  require(!full.native().starts_with(L"\\\\"),"Install on a local Windows drive.");
  fs::path current=full.root_path();
  for(const auto& part:full.relative_path()) {
    current/=part;DWORD attrs=GetFileAttributesW(current.c_str());
    if(attrs!=INVALID_FILE_ATTRIBUTES)require(!(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"Junctions and symbolic links are not allowed in managed paths: "+utf8(current));
    else require(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND,"Cannot inspect path: "+utf8(current));
  }
}
Json read_json(const fs::path& path) {
  require(fs::file_size(path)<=16*1024*1024,"Manifest exceeds the metadata size limit.");
  std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read manifest: "+utf8(path));return Json::parse(in);
}
void write_json(const fs::path& path,const Json& j) {
  auto text=j.dump(2);engine::write_file_atomically(path,std::as_bytes(std::span(text)));
}
std::string new_id() {GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create installation ID.");wchar_t text[40]{};StringFromGUID2(id,text,40);std::wstring s(text);std::string result;for(wchar_t c:s)result+=static_cast<char>(c);return result;}
std::string timestamp() {SYSTEMTIME t{};GetSystemTime(&t);char b[40]{};sprintf_s(b,"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return b;}
std::string sha256_file(const fs::path& file,const std::function<void(std::uint64_t)>& progress) {
  struct Hash {BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};~Hash(){if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);}} h;
  require(BCryptOpenAlgorithmProvider(&h.alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0&&BCryptCreateHash(h.alg,&h.hash,nullptr,0,nullptr,0,0)>=0,"SHA-256 initialization failed.");
  std::ifstream in(file,std::ios::binary);require(bool(in),"Cannot read "+utf8(file));std::vector<unsigned char> buffer(1024*1024);
  while(in) {in.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size()));auto size=static_cast<ULONG>(in.gcount());if(!size)break;require(BCryptHashData(h.hash,buffer.data(),size,0)>=0,"SHA-256 update failed.");if(progress)progress(size);}
  require(!in.bad(),"Read failed: "+utf8(file));std::array<unsigned char,32> digest{};require(BCryptFinishHash(h.hash,digest.data(),32,0)>=0,"SHA-256 finish failed.");std::string result;constexpr char hex[]="0123456789abcdef";for(auto b:digest){result+=hex[b>>4];result+=hex[b&15];}return result;
}
Release Release::parse(const Json& j) {
  require(j.at("manifestVersion")==2&&j.at("productId")==product_id&&j.at("platform")=="windows-x64"&&j.at("assets")=="cooked-only","Unsupported release manifest.");
  Release r;r.document=j;r.version=Version::parse(j.at("gameVersion").get<std::string>());r.engine_version=j.at("engineVersion").get<std::string>();r.build_id=j.at("buildId").get<std::string>();
  require(j.at("channel")==r.version.channel_name()&&!r.build_id.empty()&&r.build_id.size()<160,"Release channel/build identity mismatch.");
  require(j.at("files").is_array()&&!j.at("files").empty()&&j.at("files").size()<=10000,"Invalid release file table.");
  std::set<std::string> paths;std::uint64_t total=0;bool exe=false,marker=false,index=false,uninstaller=false,package=false;
  for(const auto& item:j.at("files")) {auto f=parse_file(item);require(paths.insert(lower(f.path)).second,"Duplicate release path.");total+=f.bytes;exe|=f.path=="stellar-continuum-native.exe";marker|=f.path=="cooked-only.marker";index|=f.path=="Content/runtime.stmanifest";uninstaller|=f.path=="StellarContinuumUninstall.exe";package|=f.path.ends_with(".stpak");r.files.push_back(f);}
  require(exe&&marker&&index&&uninstaller&&package,"Release is missing a required runtime component.");
  require(j.at("runtimeBytes")==total,"Release size does not match its file table.");
  if(j.contains("updateFrom")){
    const auto& from=j.at("updateFrom");r.base_version=from.at("gameVersion").get<std::string>();r.base_build=from.at("buildId").get<std::string>();
    require(Version::parse(r.base_version)<r.version&&!r.base_build.empty()&&r.base_build.size()<160,"Invalid update baseline.");
    require(j.at("payloadPaths").is_array()&&!j.at("payloadPaths").empty(),"Update file list is empty.");
    std::set<std::string> included;
    for(const auto& value:j.at("payloadPaths")){auto path=value.get<std::string>();validate_relative_path(path);require(std::any_of(r.files.begin(),r.files.end(),[&](const File& f){return f.path==path;})&&included.insert(lower(path)).second,"Invalid or duplicate update file.");r.payload_paths.push_back(path);}
  }else require(!j.contains("payloadPaths"),"Partial payload requires an update baseline.");
  return r;
}
bool Release::includes_payload(const File& f)const{return base_build.empty()||std::find(payload_paths.begin(),payload_paths.end(),f.path)!=payload_paths.end();}
Mode determine_mode(const std::optional<Registration>& installed,const Release& r) {
  require(r.base_build.empty()||installed.has_value(),"This update requires an existing Stellar Continuum "+r.base_version+" installation.");
  if(!installed)return Mode::Install;auto before=Version::parse(installed->version);if(before<r.version)return Mode::Update;if(before>r.version)return Mode::DowngradeBlocked;return Mode::Repair;
}
std::string mode_name(Mode m) {switch(m){case Mode::Install:return "Install";case Mode::Update:return "Update";case Mode::Repair:return "Repair";default:return "Downgrade blocked";}}
Plan make_plan(const Request& r,Platform& platform) {
  Plan plan;auto installed=platform.installed();plan.mode=determine_mode(installed,r.release);require(plan.mode!=Mode::DowngradeBlocked,"A newer version is installed. Downgrades are blocked.");
  validate_tree_path(r.root,".");std::map<std::string,File> old;
  if(installed) {require(same_path(installed->root,r.root),"An installation is already registered at another location.");auto record=installed_record(r.root,*installed,platform);auto prior=Release::parse(record.at("release"));
    if(!r.release.base_build.empty())require((prior.version.string()==r.release.base_version&&prior.build_id==r.release.base_build)||(prior.version==r.release.version&&prior.build_id==r.release.build_id),"This update requires base version "+r.release.base_version+" (build "+r.release.base_build+"). Download the update for your installed version.");
    for(auto& f:prior.files)old.emplace(lower(f.path),f);}
  else if(fs::exists(r.root)) {for(auto& e:fs::directory_iterator(r.root))require(e.path().filename()==transaction_name,"Choose an empty folder. Setup will not adopt a source checkout or an unregistered installation.");}
  const auto all_old=old;
  std::uint64_t scanned=0;for(const auto& f:r.release.files) {
    notify(r,"Checking installed files",f.path,scanned,r.release.files.size(),true);
    validate_tree_path(r.root,relative(f.path));
    require(!fs::exists(r.root/relative(f.path))||fs::is_regular_file(r.root/relative(f.path)),"A directory occupies a managed file path; its contents were preserved: "+f.path);
    require(old.contains(lower(f.path))||!fs::exists(r.root/relative(f.path)),"Unmanaged file conflicts with this release: "+f.path);
    if(old.contains(lower(f.path))&&matches(r.root,f)) {plan.unchanged.push_back(f);plan.unchanged_bytes+=f.bytes;}
    else {plan.changed.push_back(f);plan.stage_bytes+=f.bytes;
      if(f.path.ends_with(".stpak"))for(const auto& [key,previous]:all_old){(void)key;if(previous.sha256==f.sha256&&previous.bytes==f.bytes&&matches(r.root,previous)){plan.local_reuse[f.path]=previous.path;break;}}
      if(!r.release.includes_payload(f))require(plan.local_reuse.contains(f.path),"An unchanged installed file is missing or damaged: "+f.path+". Repair the base installation before using this changed-files-only update.");
    }plan.installed_bytes+=f.bytes;old.erase(lower(f.path));++scanned;
  }
  for(auto& [key,f]:old){(void)key;validate_tree_path(r.root,relative(f.path));require(!fs::exists(r.root/relative(f.path))||fs::is_regular_file(r.root/relative(f.path)),"A directory occupies an obsolete file path: "+f.path);plan.obsolete.push_back(f);}plan.required_bytes=plan.stage_bytes+margin;
  auto disk=r.root;while(!fs::exists(disk)&&disk.has_parent_path())disk=disk.parent_path();
  auto free=fs::space(disk).available;
  platform.log(mode_name(plan.mode)+" installed="+(installed?installed->version:"none")+" incoming="+r.release.version.string()+" path="+utf8(r.root)+" changed="+std::to_string(plan.changed.size())+" unchanged="+std::to_string(plan.unchanged.size())+" required="+std::to_string(plan.required_bytes)+" free="+std::to_string(free));
  require(free>=plan.required_bytes,"Not enough free disk space. Required for staging and safety reserve: "+std::to_string(plan.required_bytes)+" bytes; available: "+std::to_string(free)+" bytes.");return plan;
}
void recover(const fs::path& root,Platform& platform) {
  const auto txn=root/transaction_name;validate_tree_path(root,transaction_name);if(!fs::exists(txn))return;
  if(!fs::exists(txn/"journal.json")) {
    // A crash during creation of the first atomic journal cannot have touched
    // live files. Remove only its empty folder / known journal temporary files.
    for(const auto& entry:fs::directory_iterator(txn)) {auto name=entry.path().filename().string();require(entry.is_regular_file()&&name.starts_with("journal.json.")&&name.ends_with(".tmp"),"An unrecognized staging folder exists. Preserve it and contact support: "+utf8(txn));}
    clear_transaction(root);platform.set_pending({});return;
  }
  validate_tree_path(root,fs::path(transaction_name)/"journal.json");
  auto j=read_json(txn/"journal.json");require(j.at("productId")==product_id&&j.at("schemaVersion")==1&&same_path(relative(j.at("root").get<std::string>()),root),"Invalid recovery journal.");
  auto phase=j.at("phase").get<std::string>();require(phase=="staging"||phase=="committing"||phase=="committed","Unknown recovery state.");
  if(phase=="committing") {
    platform.log("Recovering interrupted transaction; restoring prior files and registration.");
    // Parse the whole journal before mutating anything. Only shipping paths can
    // be restored, and backups stay confined to this installation directory.
    for(const auto& op:j.at("operations")){(void)parse_file(op.at("file"));require(op.at("hadOriginal").is_boolean(),"Invalid recovery operation.");}
    for(auto it=j.at("operations").rbegin();it!=j.at("operations").rend();++it) {
      auto f=parse_file(it->at("file"));auto dest=root/relative(f.path),backup=txn/"backup"/relative(f.path);validate_tree_path(root,relative(f.path));validate_tree_path(root,fs::relative(backup,root));
      if(fs::exists(backup)) {require(fs::is_regular_file(backup),"Recovery backup is not a regular file.");if(fs::exists(dest)) {require(matches(root,f),"A file changed during recovery; retained backup: "+utf8(backup));fs::remove(dest);}durable_move(backup,dest);}
      else if(!it->at("hadOriginal").get<bool>()&&fs::exists(dest)) {require(matches(root,f),"Recovery found an unexpected file; it was preserved: "+utf8(dest));fs::remove(dest);}
    }
    validate_tree_path(root,record_name);
    if(j.at("oldInstall").is_null())fs::remove(root/record_name);else write_json(root/record_name,j.at("oldInstall"));
    platform.restore(j.at("platform"));platform.log("Rollback verified and previous registration restored.");
  }
  const bool initial=j.at("oldInstall").is_null();
  clear_transaction(root);
  if(initial&&phase!="committed")for(auto name:{"Content","Licenses"}){std::error_code ec;fs::remove(root/name,ec);}
  platform.set_pending({});
}
void execute(const Request& r,Platform& platform) {
  std::unique_ptr<engine::RuntimeDirectoryLease> single_install;
  if(auto key=platform.serialization_key())single_install=std::make_unique<engine::RuntimeDirectoryLease>(*key);
  engine::RuntimeDirectoryLease lease(r.root);platform.check_running(r.root);recover(r.root,platform);
  auto plan=make_plan(r,platform);auto previous=platform.installed();fs::create_directories(r.root);
  const auto txn=r.root/transaction_name;
  Json journal={{"schemaVersion",1},{"productId",product_id},{"root",utf8(fs::absolute(r.root))},{"phase","staging"},{"platform",platform.snapshot()},{"oldInstall",previous?installed_record(r.root,*previous,platform):Json(nullptr)},{"operations",Json::array()}};
  for(const auto& f:plan.changed)journal["operations"].push_back({{"file",file_json(f)},{"hadOriginal",fs::exists(r.root/relative(f.path))}});
  for(const auto& f:plan.obsolete)journal["operations"].push_back({{"file",file_json(f)},{"hadOriginal",fs::exists(r.root/relative(f.path))}});
  platform.set_pending(r.root);fs::create_directory(txn);write_json(txn/"journal.json",journal);
  try {
    std::uint64_t staged=0;
    for(const auto& f:plan.changed) {
      validate_tree_path(r.payload,relative(f.path));auto source=r.payload/relative(f.path);auto stage=txn/"stage"/relative(f.path);fs::create_directories(stage.parent_path());
      require(fs::is_regular_file(source)&&fs::file_size(source)==f.bytes,"Missing or incomplete installer payload: "+f.path);
      if(auto found=plan.local_reuse.find(f.path);found!=plan.local_reuse.end()) {
        validate_tree_path(r.root,relative(found->second));
        if(CreateHardLinkW(stage.c_str(),(r.root/relative(found->second)).c_str(),nullptr)) {
          require(matches(txn/"stage",f),"Reused package verification failed.");staged+=f.bytes;
          notify(r,"Reusing verified package",f.path,staged,plan.stage_bytes,true);
          platform.log("Reused installed package bytes: "+found->second+" -> "+f.path);continue;
        }
      }
      // Copy in bounded chunks; cancellation only happens before any live file
      // is replaced. Destination handles are flushed before hash verification.
      std::ifstream in(source,std::ios::binary);require(bool(in),"Cannot open payload: "+f.path);
      HANDLE out=CreateFileW(stage.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE)throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Cannot stage "+f.path);
      try {std::vector<char> buf(1024*1024);while(in) {in.read(buf.data(),static_cast<std::streamsize>(buf.size()));DWORD count=static_cast<DWORD>(in.gcount()),written{};if(!count)break;if(!WriteFile(out,buf.data(),count,&written,nullptr)||written!=count)throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Staging write failed");staged+=written;notify(r,"Preparing verified files",f.path,staged,plan.stage_bytes,true);}require(!in.bad(),"Payload read failed.");if(!FlushFileBuffers(out))throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Cannot flush staging file");CloseHandle(out);out=INVALID_HANDLE_VALUE;}
      catch(...){if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);throw;}
      require(matches(txn/"stage",f),"Installer payload hash mismatch: "+f.path);platform.log("Staged and verified "+f.path+" SHA256="+f.sha256);
    }
    checkpoint(r,"staged");notify(r,"Ready to commit","",plan.stage_bytes,plan.stage_bytes,true);
    platform.check_running(r.root);journal["phase"]="committing";write_json(txn/"journal.json",journal);checkpoint(r,"commit-start");
    std::uint64_t n=0;
    for(const auto& op:journal.at("operations")) {
      auto f=parse_file(op.at("file"));validate_tree_path(r.root,relative(f.path));auto dest=r.root/relative(f.path);auto backup=txn/"backup"/relative(f.path);auto stage=txn/"stage"/relative(f.path);
      notify(r,"Applying update safely",f.path,n,journal.at("operations").size(),false);
      if(op.at("hadOriginal").get<bool>())durable_move(dest,backup);
      checkpoint(r,"backed-up");if(fs::exists(stage))durable_move(stage,dest);checkpoint(r,"replaced");++n;
    }
    n=0;for(const auto& f:r.release.files) {notify(r,"Verifying installed game",f.path,n,plan.installed_bytes,false);std::uint64_t bytes=0;validate_tree_path(r.root,relative(f.path));require(fs::is_regular_file(r.root/relative(f.path))&&fs::file_size(r.root/relative(f.path))==f.bytes,"Installed file size mismatch: "+f.path);auto hash=sha256_file(r.root/relative(f.path),[&](auto size){bytes+=size;notify(r,"Verifying installed game",f.path,n+bytes,plan.installed_bytes,false);});require(hash==f.sha256,"Installed file verification failed: "+f.path);n+=f.bytes;}
    Registration reg{fs::absolute(r.root),previous?previous->install_id:new_id(),r.release.version.string(),previous?previous->installed_at:timestamp(),r.desktop,r.developer};
    write_json(r.root/record_name,{{"manifestVersion",1},{"productId",product_id},{"installation",registration_json(reg)},{"release",r.release.document}});checkpoint(r,"record-published");
    platform.publish(reg,r.release,plan.installed_bytes);checkpoint(r,"registered");journal["phase"]="committed";write_json(txn/"journal.json",journal);
    platform.log("Verified transaction committed: "+reg.version+" installId="+reg.install_id);
  } catch(const std::exception& e) {platform.log(std::string("Transaction failed: ")+e.what());recover(r.root,platform);throw;}
  clear_transaction(r.root);platform.set_pending({});notify(r,"Complete","",1,1,false);
}
void uninstall(const Registration& reg,Platform& platform,const ProgressSink& progress) {
  std::unique_ptr<engine::RuntimeDirectoryLease> single_install;
  if(auto key=platform.serialization_key())single_install=std::make_unique<engine::RuntimeDirectoryLease>(*key);
  engine::RuntimeDirectoryLease lease(reg.root);platform.check_running(reg.root);recover(reg.root,platform);auto record=installed_record(reg.root,reg,platform);auto release=Release::parse(record.at("release"));
  // Uninstall uses the same durable move journal: locked files or registration
  // failures restore all managed files. Unknown files and user data are kept.
  const auto txn=reg.root/transaction_name;fs::create_directory(txn);
  Json j={{"schemaVersion",1},{"productId",product_id},{"root",utf8(reg.root)},{"phase","committing"},{"platform",platform.snapshot()},{"oldInstall",record},{"operations",Json::array()}};
  for(const auto& f:release.files) {validate_tree_path(reg.root,relative(f.path));require(!fs::exists(reg.root/relative(f.path))||fs::is_regular_file(reg.root/relative(f.path)),"A directory replaced a managed file; it was preserved: "+f.path);j["operations"].push_back({{"file",file_json(f)},{"hadOriginal",fs::exists(reg.root/relative(f.path))}});}
  platform.set_pending(reg.root);write_json(txn/"journal.json",j);
  try {std::uint64_t n=0;for(const auto& op:j.at("operations")){auto f=parse_file(op.at("file"));if(progress)progress({"Removing game files",f.path,n++,release.files.size(),false});if(op.at("hadOriginal").get<bool>())durable_move(reg.root/relative(f.path),txn/"backup"/relative(f.path));}fs::remove(reg.root/record_name);platform.remove_registration(reg);j["phase"]="committed";write_json(txn/"journal.json",j);}
  catch(const std::exception& e){platform.log(std::string("Uninstall failed: ")+e.what());recover(reg.root,platform);throw;}
  clear_transaction(reg.root);platform.set_pending({});for(auto name:{"Content","Licenses"}){std::error_code ec;fs::remove(reg.root/name,ec);}std::error_code ec;fs::remove(reg.root,ec);platform.log("Uninstalled managed game files. Saves, settings, mods and unlisted files preserved.");
}
}
