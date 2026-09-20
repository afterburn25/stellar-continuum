#include "stellar/installer/windows_platform.hpp"
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <restartmanager.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace stellar::installer {
namespace {
constexpr wchar_t registry_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StellarContinuum";
void checked(LSTATUS result,const char* operation) {if(result!=ERROR_SUCCESS)throw std::system_error(static_cast<int>(result),std::system_category(),operation);}
struct Key {HKEY h{};~Key(){if(h)RegCloseKey(h);}};
Json registry(const std::wstring& registry_key) {
  Key key;auto code=RegOpenKeyExW(HKEY_CURRENT_USER,registry_key.c_str(),0,KEY_READ,&key.h);if(code==ERROR_FILE_NOT_FOUND)return nullptr;checked(code,"Read installation registration");
  Json values=Json::object();for(DWORD i=0;;++i){wchar_t name[256]{};DWORD namesize=256,type{},size=0;code=RegEnumValueW(key.h,i,name,&namesize,nullptr,&type,nullptr,&size);if(code==ERROR_NO_MORE_ITEMS)break;checked(code,"Enumerate registration");if(size>65536)throw std::runtime_error("Registration value is too large.");std::vector<unsigned char> data(size);DWORD n=256;checked(RegEnumValueW(key.h,i,name,&n,nullptr,&type,data.data(),&size),"Read registration value");values[narrow(name)]={{"type",type},{"bytes",data}};}return values;
}
std::string string_value(const Json& values,const char* name) {
  if(!values.contains(name))throw std::runtime_error(std::string("Installation registration is incomplete: ")+name);
  auto value=values.at(name);auto bytes=value.at("bytes").get<std::vector<unsigned char>>();if(value.at("type")!=REG_SZ||bytes.size()<2||bytes.size()%2)throw std::runtime_error("Invalid registration string.");std::wstring result(bytes.size()/2,L'\0');memcpy(result.data(),bytes.data(),bytes.size());if(result.back()!=0)throw std::runtime_error("Unterminated registration string.");result.pop_back();return narrow(result);
}
void set_string(HKEY key,const wchar_t* name,std::wstring_view text) {checked(RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(text.data()),static_cast<DWORD>((text.size()+1)*sizeof(wchar_t))),"Write registration");}
fs::path known(REFKNOWNFOLDERID id) {PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(id,KF_FLAG_CREATE,nullptr,&p)))throw std::runtime_error("Windows user folder is unavailable.");fs::path result=p;CoTaskMemFree(p);return result;}
std::vector<fs::path> shortcut_paths(const fs::path& test_root) {if(!test_root.empty())return {test_root/"Desktop/Game.lnk",test_root/"Start/Game.lnk",test_root/"Start/Developer.lnk",test_root/"Start/Uninstall.lnk"};auto menu=known(FOLDERID_Programs)/"Stellar Continuum";return {known(FOLDERID_Desktop)/"Stellar Continuum.lnk",menu/"Stellar Continuum.lnk",menu/"Stellar Continuum - Developer.lnk",menu/"Uninstall Stellar Continuum.lnk"};}
void shortcut(const fs::path& path,const fs::path& executable,std::wstring_view args={}) {
  fs::create_directories(path.parent_path());IShellLinkW* link=nullptr;HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));if(FAILED(hr))throw std::runtime_error("Cannot create Windows shortcut.");
  link->SetPath(executable.c_str());link->SetWorkingDirectory(executable.parent_path().c_str());link->SetArguments(std::wstring(args).c_str());link->SetIconLocation(executable.c_str(),0);IPersistFile* file=nullptr;hr=link->QueryInterface(IID_PPV_ARGS(&file));if(SUCCEEDED(hr)){hr=file->Save(path.c_str(),TRUE);file->Release();}link->Release();if(FAILED(hr))throw std::runtime_error("Cannot save shortcut.");
}
bool path_equal(const fs::path& a,const fs::path& b) {auto x=fs::weakly_canonical(a).wstring(),y=fs::weakly_canonical(b).wstring();return CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;}
}
std::wstring widen(std::string_view s) {if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF-8.");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string narrow(std::wstring_view s) {if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(n<=0)throw std::runtime_error("Invalid Unicode path.");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
fs::path local_data() {return known(FOLDERID_LocalAppData)/"Stellar Continuum";}
fs::path default_install_directory() {return known(FOLDERID_LocalAppData)/"Programs"/"Stellar Continuum";}
WindowsPlatform::WindowsPlatform(fs::path test_root) : registry_key_(registry_key),shortcut_root_(test_root.empty()?fs::path{}:test_root/"Shortcuts") {
  if(!test_root.empty()) registry_key_=L"Software\\StellarContinuumInstallerTests\\"+widen(test_root.filename().string());
  pending_file_=(test_root.empty()?local_data()/"Installer":test_root)/"pending.json";
  auto dir=(test_root.empty()?local_data()/"Installer":test_root)/"Logs";fs::create_directories(dir);auto name=timestamp();std::replace(name.begin(),name.end(),':','-');log_path_=dir/(name+"-"+std::to_string(GetCurrentProcessId())+".log");log_.open(log_path_,std::ios::app);log("Stellar Continuum per-user Windows x64 maintenance; session "+new_id());
}
void WindowsPlatform::log(std::string_view text) {log_<<timestamp()<<" "<<text<<'\n';log_.flush();}
Json WindowsPlatform::record_backup() {auto values=registry(registry_key_);if(values.is_null()||!values.contains("InstallManifest"))return nullptr;return Json::parse(string_value(values,"InstallManifest"));}
std::optional<fs::path> WindowsPlatform::pending() {if(!fs::exists(pending_file_))return {};auto j=read_json(pending_file_);if(j.at("productId")!=product_id)throw std::runtime_error("Invalid pending installation record.");fs::path path(widen(j.at("path").get<std::string>()));if(!path.is_absolute()||path==path.root_path())throw std::runtime_error("Invalid pending installation folder.");return path;}
void WindowsPlatform::set_pending(const std::optional<fs::path>& path) {if(path)write_json(pending_file_,{{"productId",product_id},{"path",narrow(fs::absolute(*path).wstring())}});else fs::remove(pending_file_);}
std::optional<Registration> WindowsPlatform::installed() {
  auto values=registry(registry_key_);if(values.is_null())return {};if(string_value(values,"ProductId")!=product_id)throw std::runtime_error("Registration product ID mismatch.");
  Registration r{fs::path(widen(string_value(values,"InstallLocation"))),string_value(values,"InstallId"),string_value(values,"DisplayVersion"),string_value(values,"InstallDateUtc"),string_value(values,"DesktopShortcut")=="1",string_value(values,"DeveloperShortcut")=="1"};
  (void)Version::parse(r.version);if(!r.root.is_absolute()||r.root==r.root.root_path())throw std::runtime_error("Invalid registered installation folder.");return r;
}
Json WindowsPlatform::snapshot() {
  Json links=Json::array();for(auto& path:shortcut_paths(shortcut_root_)){if(fs::exists(path)){if(fs::file_size(path)>1024*1024)throw std::runtime_error("Shortcut is unexpectedly large.");std::ifstream in(path,std::ios::binary);links.push_back(std::vector<unsigned char>(std::istreambuf_iterator<char>(in),{}));}else links.push_back(nullptr);}return {{"registry",registry(registry_key_)},{"shortcuts",links}};
}
void WindowsPlatform::publish(const Registration& r,const Release& release,std::uint64_t bytes) {
  Key key;checked(RegCreateKeyExW(HKEY_CURRENT_USER,registry_key_.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&key.h,nullptr),"Create installation registration");
  auto str=[&](const wchar_t* name,const std::string& value){set_string(key.h,name,widen(value));};
  str(L"ProductId",product_id);str(L"DisplayName","Stellar Continuum");str(L"Publisher","Stellar Continuum");str(L"DisplayVersion",r.version);str(L"InstallId",r.install_id);str(L"InstallDateUtc",r.installed_at);str(L"Channel",release.version.channel_name());str(L"BuildId",release.build_id);str(L"DesktopShortcut",r.desktop?"1":"0");str(L"DeveloperShortcut",r.developer?"1":"0");
  str(L"InstallManifest",read_json(r.root/"install_manifest.json").dump());
  set_string(key.h,L"InstallLocation",r.root.wstring());set_string(key.h,L"DisplayIcon",(r.root/"stellar-continuum-native.exe").wstring());set_string(key.h,L"UninstallString",L"\""+(r.root/"StellarContinuumUninstall.exe").wstring()+L"\"");
  DWORD size=static_cast<DWORD>(std::min<std::uint64_t>(bytes/1024,MAXDWORD)),one=1;checked(RegSetValueExW(key.h,L"EstimatedSize",0,REG_DWORD,reinterpret_cast<BYTE*>(&size),sizeof(size)),"Write installed size");for(auto name:{L"NoModify",L"NoRepair"})checked(RegSetValueExW(key.h,name,0,REG_DWORD,reinterpret_cast<BYTE*>(&one),sizeof(one)),"Write maintenance policy");checked(RegFlushKey(key.h),"Flush installation registration");
  auto paths=shortcut_paths(shortcut_root_);auto game=r.root/"stellar-continuum-native.exe";
  if(r.desktop)shortcut(paths[0],game);else fs::remove(paths[0]);shortcut(paths[1],game);if(r.developer)shortcut(paths[2],game,L"--dev-game");else fs::remove(paths[2]);shortcut(paths[3],r.root/"StellarContinuumUninstall.exe");log("Published Apps & Features registration and user shortcuts.");
}
void WindowsPlatform::remove_registration(const Registration& r) {auto current=installed();if(!current||current->install_id!=r.install_id)throw std::runtime_error("Installation registration changed during uninstall.");checked(RegDeleteTreeW(HKEY_CURRENT_USER,registry_key_.c_str()),"Remove installation registration");for(auto& p:shortcut_paths(shortcut_root_))fs::remove(p);std::error_code ec;fs::remove(shortcut_paths(shortcut_root_)[1].parent_path(),ec);}
void WindowsPlatform::restore(const Json& saved) {
  auto code=RegDeleteTreeW(HKEY_CURRENT_USER,registry_key_.c_str());if(code!=ERROR_FILE_NOT_FOUND)checked(code,"Restore registration");auto values=saved.at("registry");if(!values.is_null()){Key key;checked(RegCreateKeyExW(HKEY_CURRENT_USER,registry_key_.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key.h,nullptr),"Restore installation registration");for(auto it=values.begin();it!=values.end();++it){auto name=widen(it.key());auto data=it->at("bytes").get<std::vector<unsigned char>>();checked(RegSetValueExW(key.h,name.c_str(),0,it->at("type").get<DWORD>(),data.data(),static_cast<DWORD>(data.size())),"Restore registration value");}checked(RegFlushKey(key.h),"Flush restored registration");}
  auto paths=shortcut_paths(shortcut_root_);if(saved.at("shortcuts").size()!=paths.size())throw std::runtime_error("Invalid shortcut recovery record.");for(std::size_t i=0;i<paths.size();++i){auto bytes=saved.at("shortcuts")[i];if(bytes.is_null())fs::remove(paths[i]);else {auto data=bytes.get<std::vector<unsigned char>>();fs::create_directories(paths[i].parent_path());std::ofstream out(paths[i],std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("Cannot restore shortcut.");}}
}
void WindowsPlatform::check_running(const fs::path& root) {
  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check running processes.");PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);std::string message;
  if(Process32FirstW(snap,&entry))do {if(_wcsicmp(entry.szExeFile,L"stellar-continuum-native.exe")!=0)continue;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);if(process){wchar_t path[32768]{};DWORD n=32768;if(QueryFullProcessImageNameW(process,0,path,&n)&&path_equal(fs::path(path).parent_path(),root))message="Close Stellar Continuum (process "+std::to_string(entry.th32ProcessID)+"), then retry. No game files have been replaced.";CloseHandle(process);}}while(Process32NextW(snap,&entry));CloseHandle(snap);if(!message.empty())throw std::runtime_error(message);
  DWORD session{};WCHAR key[CCH_RM_SESSION_KEY+1]{};auto error=RmStartSession(&session,0,key);if(error!=ERROR_SUCCESS)throw std::system_error(static_cast<int>(error),std::system_category(),"Cannot check file locks");
  std::vector<std::wstring> resources;for(auto name:{"stellar-continuum-native.exe","SDL3.dll","StellarContinuumUninstall.exe"})if(fs::exists(root/name))resources.push_back((root/name).wstring());
  if(fs::exists(root/"Content")){validate_tree_path(root,"Content");for(auto& f:fs::directory_iterator(root/"Content"))if(f.is_regular_file())resources.push_back(f.path().wstring());}
  std::vector<LPCWSTR> pointers;for(auto& s:resources)pointers.push_back(s.c_str());error=RmRegisterResources(session,static_cast<UINT>(pointers.size()),pointers.data(),0,nullptr,0,nullptr);
  UINT needed{},count{};DWORD reason{};if(error==ERROR_SUCCESS)error=RmGetList(session,&needed,&count,nullptr,&reason);if(error==ERROR_MORE_DATA){std::vector<RM_PROCESS_INFO> processes(needed);count=needed;error=RmGetList(session,&needed,&count,processes.data(),&reason);if(error==ERROR_SUCCESS)for(UINT i=0;i<count;++i)if(processes[i].Process.dwProcessId!=GetCurrentProcessId())message+="Close "+narrow(processes[i].strAppName)+" (process "+std::to_string(processes[i].Process.dwProcessId)+") and retry. ";}RmEndSession(session);
  if(error!=ERROR_SUCCESS)throw std::system_error(static_cast<int>(error),std::system_category(),"Cannot enumerate file locks");if(!message.empty())throw std::runtime_error(message);
}
}
