#include "stellar/installer/windows_platform.hpp"
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <iostream>
#include <fstream>
using namespace stellar::installer;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void put(const fs::path& p,std::string_view s){fs::create_directories(p.parent_path());std::ofstream out(p);out<<s;}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected failure");}
void check_link(const fs::path& path,const fs::path& target,std::wstring_view arguments) {
  IShellLinkW* link=nullptr;check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"Cannot inspect shortcut");IPersistFile* file=nullptr;check(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))),"Shortcut persistence unavailable");check(SUCCEEDED(file->Load(path.c_str(),STGM_READ)),"Shortcut missing");wchar_t actual[32768]{},args[2048]{};link->GetPath(actual,32768,nullptr,SLGP_RAWPATH);link->GetArguments(args,2048);file->Release();link->Release();check(fs::path(actual)==target&&args==arguments,"Shortcut has wrong executable/arguments");
}
}
int wmain(int argc,wchar_t** argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try {
  if(argc==3&&std::wstring_view(argv[2])==L"--cleanup-ui"){WindowsPlatform platform(fs::absolute(argv[1]));if(auto registered=platform.installed())uninstall(*registered,platform);CoUninitialize();return 0;}
  check(argc>=2,"Supply test output root");bool ui_fixture=argc==4&&std::wstring_view(argv[2])==L"--prepare-ui";auto root=ui_fixture?fs::absolute(argv[1]):fs::absolute(argv[1])/new_id();WindowsPlatform platform(root);auto real_before=WindowsPlatform{}.installed();check(!platform.installed(),"Test registration is not isolated");
  auto payload=root/"Payload";for(auto& path:{"stellar-continuum-native.exe","StellarContinuumUninstall.exe","cooked-only.marker","Content/runtime.stmanifest","Content/Core-one.stpak"})put(payload/path,path);
  Json j={{"manifestVersion",2},{"productId",product_id},{"platform","windows-x64"},{"assets","cooked-only"},{"gameVersion","0.1.0.1-dev"},{"channel","dev"},{"engineVersion","0.1.63"},{"buildId","os-test"},{"files",Json::array()}};std::uint64_t total{};for(auto& file:fs::recursive_directory_iterator(payload))if(file.is_regular_file()){auto size=file.file_size();j["files"].push_back({{"path",fs::relative(file.path(),payload).generic_string()},{"bytes",size},{"sha256",sha256_file(file.path())}});total+=size;}j["runtimeBytes"]=total;auto release=Release::parse(j);
  Request request{payload,root/L"Game with spaces Ω",release,true,true};execute(request,platform);auto reg=platform.installed();check(reg&&reg->version=="0.1.0.1-dev","Windows registration failed");check(!platform.pending(),"Pending transaction did not clear");auto shortcuts=root/"Shortcuts";check_link(shortcuts/"Desktop/Game.lnk",request.root/"stellar-continuum-native.exe",L"");check_link(shortcuts/"Start/Developer.lnk",request.root/"stellar-continuum-native.exe",L"--dev-game");check_link(shortcuts/"Start/Uninstall.lnk",request.root/"StellarContinuumUninstall.exe",L"");
  if(ui_fixture){auto next=j;auto version=Version::parse(narrow(argv[3]));next["gameVersion"]=version.string();next["channel"]=version.channel_name();request.release=Release::parse(next);execute(request,platform);std::cout<<"Prepared isolated installed-state fixture for UI review.\n";CoUninitialize();return 0;}
  fs::remove(request.root/"install_manifest.json");execute(request,platform);check(read_json(request.root/"install_manifest.json")==platform.record_backup(),"Missing local record not repaired");put(request.root/"install_manifest.json","BROKEN JSON");execute(request,platform);check(read_json(request.root/"install_manifest.json")==platform.record_backup(),"Corrupt local record not repaired");
  auto snap=platform.snapshot();request.checkpoint=[](std::string_view p){if(p=="registered")throw std::runtime_error("Fail after Windows registration and shortcuts");};request.desktop=false;request.developer=false;rejects([&]{execute(request,platform);});check(platform.snapshot()==snap,"OS integration rollback did not restore registry/shortcuts");request.checkpoint={};
  auto pending=request.root/".stellar-transaction";fs::create_directories(pending);put(pending/"journal.json.interrupted.tmp","INCOMPLETE");platform.set_pending(request.root);check(platform.pending()==request.root,"Custom folder recovery pointer missing");recover(*platform.pending(),platform);check(!fs::exists(pending)&&!platform.pending(),"First-journal interruption not recovered");
  uninstall(*platform.installed(),platform);check(!platform.installed()&&!fs::exists(shortcuts/"Start/Game.lnk"),"Windows uninstall left product registration/shortcut");auto real_after=WindowsPlatform{}.installed();check(real_before.has_value()==real_after.has_value()&&(!real_before||real_before->install_id==real_after->install_id),"Test touched real registration");
  std::cout<<"PASS: isolated actual HKCU registration, Unicode/space paths, ShellLink targets, manifest recovery, registry/shortcut rollback, interrupted custom install pointer and uninstall.\n";CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';CoUninitialize();return 1;}}
