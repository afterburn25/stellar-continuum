#include "stellar/installer/maintenance.hpp"
#include "stellar/engine/runtime_directory_lease.hpp"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include <thread>
using namespace stellar::installer;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected rejection");}
void put(const fs::path& p,std::string_view data){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<data;}
std::uint64_t identity(const fs::path& p){HANDLE file=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);check(file!=INVALID_HANDLE_VALUE,"File identity open failed");BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(file,&info)!=FALSE;CloseHandle(file);check(ok,"File identity read failed");return (std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;}
struct TestPlatform:Platform {
  fs::path state;bool running{},fail_publish{};std::vector<std::string> events;
  explicit TestPlatform(fs::path p):state(std::move(p)){}
  std::optional<Registration> installed() override {if(!fs::exists(state))return {};auto j=read_json(state);return Registration{path_from_utf8(j.at("path").get<std::string>()),j.at("id"),j.at("version"),j.at("date"),j.at("desktop"),j.at("developer")};}
  Json snapshot() override{return fs::exists(state)?read_json(state):Json(nullptr);}
  void publish(const Registration& r,const Release& release,std::uint64_t) override {auto path=r.root.u8string();write_json(state,{{"path",std::string(path.begin(),path.end())},{"id",r.install_id},{"version",release.version.string()},{"date",r.installed_at},{"desktop",r.desktop},{"developer",r.developer}});if(fail_publish)throw std::runtime_error("Injected Windows registration failure");}
  void remove_registration(const Registration&)override {fs::remove(state);}
  void restore(const Json& j)override {if(j.is_null())fs::remove(state);else write_json(state,j);}
  void check_running(const fs::path&)override {if(running)throw std::runtime_error("Game is running");}
  void log(std::string_view s)override {events.emplace_back(s);}
};
Release fixture(const fs::path& path,const std::string& version,std::string game="GAME-ONE",bool obsolete=true){
  put(path/"stellar-continuum-native.exe",game);put(path/"StellarContinuumUninstall.exe","UNINSTALL");put(path/"cooked-only.marker","cooked");put(path/"Content/runtime.stmanifest","MANIFEST");put(path/"Content/Core-one.stpak","SHARED-CONTENT-PACKAGE");if(obsolete)put(path/"Licenses/old.txt","OLD LICENSE");
  Json j={{"manifestVersion",2},{"productId",product_id},{"platform","windows-x64"},{"assets","cooked-only"},{"gameVersion",version},{"engineVersion","0.1.63"},{"channel",Version::parse(version).channel_name()},{"buildId",version},{"files",Json::array()}};std::uint64_t total{};
  for(auto& f:fs::recursive_directory_iterator(path))if(f.is_regular_file()){auto file=fs::relative(f.path(),path).generic_string();if(file=="release-manifest.json")continue;auto bytes=f.file_size();total+=bytes;j["files"].push_back({{"path",file},{"bytes",bytes},{"sha256",sha256_file(f.path())},{"package",file}});}j["runtimeBytes"]=total;write_json(path/"release-manifest.json",j);return Release::parse(j);
}
void verify(const fs::path& root,const Release& r){for(const auto& f:r.files)check(sha256_file(root/path_from_utf8(f.path))==f.sha256,"Installed hash differs");}
}
int main(int argc,char** argv){try{
  if(argc<2)throw std::runtime_error("Supply an isolated test directory");fs::path base=fs::absolute(argv[1]);
  if(argc==4&&std::string_view(argv[2])=="--crash"){TestPlatform p(base/"registration.json");Request r{base/"v2",base/"Installed",Release::parse(read_json(base/"v2/release-manifest.json")),true,false,nullptr,{},[&](std::string_view point){if(point==argv[3])ExitProcess(77);}};execute(r,p);return 2;}
  if(argc==5&&std::string_view(argv[2])=="--update-package"){
    check(!fs::exists(base),"Use a fresh shipping-update test directory");
    TestPlatform p(base/"registration.json");fs::path payload=fs::absolute(argv[3]),delta=fs::absolute(argv[4]);
    Request baseline{payload,base/"Game",Release::parse(read_json(payload/"release-manifest.json")),true,true};
    Request update{delta,baseline.root,Release::parse(read_json(delta/"release-manifest.json")),true,true};
    check(!update.release.base_build.empty(),"Expected partial update metadata");
    execute(baseline,p);
    std::map<std::string,fs::file_time_type> unchanged;
    for(const auto& f:update.release.files)if(!update.release.includes_payload(f))unchanged[f.path]=fs::last_write_time(baseline.root/path_from_utf8(f.path));
    put(base/"UserData/campaign.player17.json","PRESERVE SAVE");put(baseline.root/"Mods/user.txt","PRESERVE MOD");
    auto plan=make_plan(update,p);check(plan.changed.size()==update.release.payload_paths.size(),"Shipping delta contains redundant or missing changed files");
    execute(update,p);verify(baseline.root,update.release);execute(update,p);
    put(baseline.root/"stellar-continuum-native.exe","damaged");execute(update,p);verify(baseline.root,update.release);
    for(const auto& [path,time]:unchanged)check(fs::last_write_time(baseline.root/path_from_utf8(path))==time,"Shipping delta rewrote an unchanged file");
    uninstall(*p.installed(),p);check(fs::exists(base/"UserData/campaign.player17.json")&&fs::exists(baseline.root/"Mods/user.txt"),"Shipping update/uninstall lost user data");
    std::cout<<"PASS: real shipping baseline, changed-files update, full hash verification, no-op repair, executable repair, unchanged content timestamps and save/mod preservation.\n";return 0;
  }
  if(argc==4&&std::string_view(argv[2])=="--full-package"){
    TestPlatform p(base/"registration.json");fs::path payload=fs::absolute(argv[3]);Request r{payload,base/"Full Game",Release::parse(read_json(payload/"release-manifest.json")),true,true};
    execute(r,p);verify(r.root,r.release);auto original=fs::last_write_time(r.root/"Content/runtime.stmanifest");execute(r,p);check(fs::last_write_time(r.root/"Content/runtime.stmanifest")==original,"Repair rewrote an unchanged file");put(r.root/"README.txt","damaged");fs::remove(r.root/"cooked-only.marker");execute(r,p);verify(r.root,r.release);put(base/"UserData/campaign.player17.json","PRESERVE SAVE");put(r.root/"Mods/user.txt","PRESERVE MOD");uninstall(*p.installed(),p);check(fs::exists(base/"UserData/campaign.player17.json")&&fs::exists(r.root/"Mods/user.txt"),"User data lost");std::cout<<"Full shipping payload: install, no-op repair, corruption repair, uninstall and user preservation passed.\n";return 0;
  }
  base/=new_id();fs::create_directories(base);check(!fs::exists(base/"registration.json"),"Use a fresh test directory");
  auto a=Version::parse("1.9.0.2-stable"),b=Version::parse("1.10.0.0-dev");check(a<b,"Version comparison is lexicographic");check(Version::parse("1.0.0.0-beta")>Version::parse("1.0.0.0-dev"),"Channel ordering");check(Version::parse("1.0.0.1-dev")>Version::parse("1.0.0.0-stable"),"Newer dev protection");for(auto v:{"1.2.3","1.2.3.4-alpha","1.2.3.65536-dev","01.2.3.4-dev","1.2.3.4.5-dev"})rejects([&]{(void)Version::parse(v);});
  for(auto path:{"../save.json","C:/Windows/x.exe","Content/../x.stpak","Content/a.stpak:evil","Content/CON.stpak","Content/a.stpak.","Content//a.stpak","Content\\x.stpak","UserData/save.json","Content/runtime.stmanifest/child"})rejects([&]{validate_relative_path(path);});
  auto v1=fixture(base/"v1","0.1.0.1-dev");auto v2=fixture(base/"v2","0.1.0.2-dev","GAME-TWO",false);auto invalid=v2.document;invalid["files"].push_back(invalid["files"][0]);rejects([&]{Release::parse(invalid);});invalid=v2.document;invalid["channel"]="stable";rejects([&]{Release::parse(invalid);});
  TestPlatform p(base/"registration.json");Request first{base/"v1",base/"Installed",v1,true,false};Request second{base/"v2",first.root,v2,true,true};
  put(base/"UserData/save.json","SAVE");put(base/"UserData/settings.json","SETTINGS");
  execute(first,p);check(determine_mode(p.installed(),v1)==Mode::Repair,"Same version mode");check(determine_mode(p.installed(),v2)==Mode::Update,"Update mode");verify(first.root,v1);
  auto package_time=fs::last_write_time(first.root/"Content/Core-one.stpak");auto clean=make_plan(first,p);check(clean.changed.empty()&&clean.unchanged.size()==v1.files.size(),"No-op repair plan");execute(first,p);check(fs::last_write_time(first.root/"Content/Core-one.stpak")==package_time,"No-op repair copied package");
  put(first.root/"stellar-continuum-native.exe","corrupted");fs::remove(first.root/"cooked-only.marker");auto repair=make_plan(first,p);check(repair.changed.size()==2,"Repair must select exactly damaged/missing files");execute(first,p);verify(first.root,v1);
  auto before=p.snapshot();std::atomic_bool cancel=true;second.cancel=&cancel;rejects([&]{execute(second,p);});second.cancel=nullptr;verify(first.root,v1);check(p.snapshot()==before,"Cancelled update changed registration");
  p.running=true;rejects([&]{execute(second,p);});p.running=false;
  // Fail after every persistent boundary; rollback must restore both file bytes
  // and registration, even when the registry writer partially succeeds.
  for(auto point:{"staged","commit-start","backed-up","replaced","record-published","registered"}){second.checkpoint=[&](std::string_view current){if(current==point)throw std::runtime_error("Injected failure");};rejects([&]{execute(second,p);});verify(first.root,v1);check(p.snapshot()==before,"Rollback changed registration");check(!fs::exists(first.root/".stellar-transaction"),"Rollback left staging");}
  second.checkpoint={};p.fail_publish=true;rejects([&]{execute(second,p);});p.fail_publish=false;verify(first.root,v1);check(p.snapshot()==before,"Failed publish did not restore registration");
  put(base/"v2/stellar-continuum-native.exe","BAD-BITS");rejects([&]{execute(second,p);});verify(first.root,v1);put(base/"v2/stellar-continuum-native.exe","GAME-TWO");fs::remove(base/"v2/stellar-continuum-native.exe");rejects([&]{execute(second,p);});put(base/"v2/stellar-continuum-native.exe","GAME-TWO");
  // Abrupt process death is different from an exception: no destructors run.
  for(auto point:{"staged","backed-up","replaced","registered"}){wchar_t own[32768]{};GetModuleFileNameW(nullptr,own,32768);std::wstring command=L"\""+std::wstring(own)+L"\" \""+base.wstring()+L"\" --crash "+std::wstring(point,point+strlen(point));STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot launch crash fixture");WaitForSingleObject(process.hProcess,30000);DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);check(code==77,"Crash fixture did not reach checkpoint");recover(first.root,p);verify(first.root,v1);check(p.snapshot()==before,"Crash recovery registration mismatch");}
  std::atomic_bool locked=false;{stellar::engine::RuntimeDirectoryLease lease(first.root);std::thread other([&]{try{stellar::engine::RuntimeDirectoryLease duplicate(first.root);}catch(...){locked=true;}});other.join();}check(locked,"Runtime launch/installer lease is not exclusive");
  put(first.root/"Mods/custom.txt","MOD");execute(second,p);verify(first.root,v2);check(fs::last_write_time(first.root/"Content/Core-one.stpak")==package_time,"Update copied unchanged package");check(!fs::exists(first.root/"Licenses/old.txt"),"Obsolete managed file not removed");check(determine_mode(p.installed(),v1)==Mode::DowngradeBlocked,"Downgrade not blocked");rejects([&]{execute(first,p);});
  auto rename_doc=v2.document;rename_doc["gameVersion"]="0.1.0.3-dev";
  for(auto& f:rename_doc["files"]){auto old=f.at("path").get<std::string>();if(old=="Content/Core-one.stpak")f["path"]="Content/Core-renamed.stpak";auto dest=base/"v3"/f.at("path").get<std::string>();fs::create_directories(dest.parent_path());fs::copy_file(base/"v2"/old,dest);}
  auto v3=Release::parse(rename_doc);Request renamed{base/"v3",first.root,v3,true,true};auto old_identity=identity(first.root/"Content/Core-one.stpak");auto rename_plan=make_plan(renamed,p);check(rename_plan.local_reuse.size()==1,"Identical renamed package was not recognized");execute(renamed,p);verify(first.root,v3);check(identity(first.root/"Content/Core-renamed.stpak")==old_identity,"Renamed package was copied instead of reused");
  // Locked managed files produce a rollback, not a partially uninstalled game.
  auto lockedfile=CreateFileW((first.root/"stellar-continuum-native.exe").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(lockedfile!=INVALID_HANDLE_VALUE,"Cannot create file lock");rejects([&]{uninstall(*p.installed(),p);});CloseHandle(lockedfile);verify(first.root,v3);uninstall(*p.installed(),p);check(!p.installed(),"Uninstall registration remains");check(fs::exists(first.root/"Mods/custom.txt")&&fs::exists(base/"UserData/save.json")&&fs::exists(base/"UserData/settings.json"),"Uninstall lost user data");rejects([&]{execute(first,p);});
  Request initial_failure=first;initial_failure.root=base/"Failed fresh installation";initial_failure.checkpoint=[](std::string_view point){if(point=="registered")throw std::runtime_error("Failed first installation");};rejects([&]{execute(initial_failure,p);});check(!p.installed()&&fs::is_empty(initial_failure.root),"Failed initial install left files or registration");initial_failure.checkpoint={};execute(initial_failure,p);uninstall(*p.installed(),p);
  auto huge=v1.document;auto total=huge.at("runtimeBytes").get<std::uint64_t>();for(int i=0;i<1000;++i){auto f=huge["files"][0];f["path"]="Content/space-"+std::to_string(i)+".stpak";f["bytes"]=128ULL*1024*1024*1024;total+=f["bytes"].get<std::uint64_t>();huge["files"].push_back(f);}huge["runtimeBytes"]=total;Request space_check{base/"v1",base/"Not enough space",Release::parse(huge)};rejects([&]{make_plan(space_check,p);});check(!fs::exists(space_check.root),"Disk preflight wrote installation files");
  // Update downloads contain only changed bytes; omitted files must be verified
  // from the exact registered base, and must never be silently skipped.
  TestPlatform delta_platform(base/"delta-registration.json");Request baseline{base/"v1",base/"Delta Installed",v1};execute(baseline,delta_platform);
  auto delta_doc=v2.document;delta_doc["updateFrom"]={{"gameVersion",v1.version.string()},{"buildId",v1.build_id}};delta_doc["payloadPaths"]={"stellar-continuum-native.exe"};
  auto delta=Release::parse(delta_doc);put(base/"delta/stellar-continuum-native.exe","GAME-TWO");Request update{base/"delta",baseline.root,delta};
  auto malformed_delta=delta_doc;malformed_delta["payloadPaths"]={"STELLAR-CONTINUUM-NATIVE.EXE"};rejects([&]{Release::parse(malformed_delta);});
  malformed_delta=delta_doc;malformed_delta["payloadPaths"].push_back("stellar-continuum-native.exe");rejects([&]{Release::parse(malformed_delta);});
  malformed_delta=delta_doc;malformed_delta.erase("updateFrom");rejects([&]{Release::parse(malformed_delta);});
  auto base_time=fs::last_write_time(baseline.root/"Content/Core-one.stpak");
  auto wrong=delta_doc;wrong["updateFrom"]["buildId"]="different-build";Request invalid_delta=update;invalid_delta.release=Release::parse(wrong);rejects([&]{make_plan(invalid_delta,delta_platform);});
  rejects([&]{determine_mode({},delta);});
  put(baseline.root/"Content/Core-one.stpak","DAMAGED");rejects([&]{execute(update,delta_platform);});check(!fs::exists(baseline.root/".stellar-transaction"),"Partial update mutated a damaged base");execute(baseline,delta_platform);base_time=fs::last_write_time(baseline.root/"Content/Core-one.stpak");
  auto failed_update=update;failed_update.checkpoint=[](std::string_view point){if(point=="registered")throw std::runtime_error("Delta rollback fixture");};rejects([&]{execute(failed_update,delta_platform);});verify(baseline.root,v1);
  execute(update,delta_platform);verify(baseline.root,delta);execute(update,delta_platform);
  check(fs::last_write_time(baseline.root/"Content/Core-one.stpak")==base_time,"Partial update rewrote unchanged content");uninstall(*delta_platform.installed(),delta_platform);
  std::cout<<"PASS: numeric/channel versions, paths, malformed manifests, install, changed-file update, repair, downgrade block, cancellation, running game, hash/missing payload, six rollback boundaries, four hard-exit recoveries, file locks, uninstall, user data preservation and base-validated partial update/rollback/repair.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
