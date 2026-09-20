#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs=std::filesystem;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const fs::path& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv)try{
  if(argc<2)throw std::runtime_error("Supply an isolated diagnostics directory");const fs::path root=fs::absolute(argv[1]);
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  if(argc==3){stellar::engine::RuntimeDiagnostics log("0.1.14.2-dev","0.1.64",root);
    stellar::engine::RuntimeDiagnostics::context("view=star-map selected=42 map_zoom=256");std::cerr<<"Worker image preparation evidence\n";
    if(std::string_view(argv[2])=="fault"){RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);return 9;}
    std::thread worker([]{throw std::runtime_error("Unhandled worker failure fixture");});worker.join();return 8;
  }
  fs::create_directories(root);fs::path logged;
  {stellar::engine::RuntimeDiagnostics log("0.1.14.2-dev","0.1.64",root/"caught");logged=log.log_path();check(!logged.empty(),"No persistent log");
    std::cout<<"ordinary message\n";std::cerr<<"error evidence\n";
    stellar::engine::RuntimeDiagnostics::context("view=star-map map_zoom=128");log.fatal("Caught image budget fixture");}
  auto content=read(logged);check(content.find("ordinary message")!=std::string::npos&&content.find("error evidence")!=std::string::npos&&content.find("map_zoom=128")!=std::string::npos&&content.find("Caught image budget fixture")!=std::string::npos,"Caught failure evidence missing");
  logged.replace_extension(".txt");check(fs::file_size(logged)>0,"No caught error report");
  for(int i=0;i<12;++i){stellar::engine::RuntimeDiagnostics log("test","test",root/"rotation");std::cout<<"rotation fixture\n";}
  int logs=0;for(const auto& e:fs::directory_iterator(root/"rotation")){check(e.path().extension()==".log","Clean exit left a crash report");++logs;}check(logs<=8,"Session retention is unbounded");
  for(const auto mode:{"fault","terminate"}){
    auto dir=root/mode;auto command=L"\""+stellar::engine::executable_path().wstring()+L"\" \""+dir.wstring()+L"\" "+std::wstring(mode,mode+strlen(mode));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start diagnostic fault child");
    const auto wait=WaitForSingleObject(process.hProcess,30000);if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(process.hThread);check(wait==WAIT_OBJECT_0&&code!=0,"Fault fixture did not exit");
    bool report=false,dump=false;for(const auto& e:fs::directory_iterator(dir)){
      if(e.path().extension()==".txt"){const auto text=read(e.path());report=text.find("map_zoom=256")!=std::string::npos&&text.find(mode==std::string("fault")?"WINDOWS FAULT":"Unhandled worker failure")!=std::string::npos;}
      if(e.path().extension()==".dmp"){const auto text=read(e.path());dump=text.starts_with("MDMP");}
    }check(report&&dump,"Hardware/worker failure did not retain report and minidump");
  }
  std::cout<<"PASS: console mirroring, caught exception, view context, clean exit, bounded retention, Windows fault and worker terminate reports/dumps.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
