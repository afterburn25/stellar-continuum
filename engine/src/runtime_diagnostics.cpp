#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#endif
namespace stellar::engine {
namespace {
constexpr std::size_t log_limit=4u*1024u*1024u;
std::string utc(){
  const auto now=std::chrono::system_clock::now();const auto time=std::chrono::system_clock::to_time_t(now);std::tm t{};
#ifdef _WIN32
  gmtime_s(&t,&time);
#else
  gmtime_r(&time,&t);
#endif
  char text[40]{};std::strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&t);return text;
}
std::filesystem::path default_directory(){
#ifdef _WIN32
  PWSTR value{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&value)))throw std::runtime_error("Local diagnostics folder unavailable");
  std::filesystem::path path(value);CoTaskMemFree(value);return path/L"Stellar Continuum"/L"Logs";
#else
  return std::filesystem::temp_directory_path()/"Stellar Continuum"/"Logs";
#endif
}
void prune(const std::filesystem::path& directory){
  // Every session has the same basename for log, fatal report and optional dump.
  for(auto ext:{".log",".txt",".dmp"}){
    std::vector<std::filesystem::directory_entry> files;
    for(const auto& e:std::filesystem::directory_iterator(directory))if(e.is_regular_file()&&e.path().filename().string().starts_with("session-")&&e.path().extension()==ext)files.push_back(e);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
    for(std::size_t i=7;i<files.size();++i){std::error_code ec;std::filesystem::remove(files[i],ec);}
  }
}
}
struct RuntimeDiagnostics::Impl {
  static inline Impl* active{};
  std::filesystem::path path,report,dump;
  std::ofstream file;std::mutex mutex;std::size_t bytes{};bool failed{},limited{};
  std::array<char,1024> last_context{};
  std::chrono::steady_clock::time_point last_context_log{};
  std::terminate_handler old_terminate{};
  using SignalHandler=void(*)(int);SignalHandler old_abort{};
#ifdef _WIN32
  HANDLE crash_file{INVALID_HANDLE_VALUE};LPTOP_LEVEL_EXCEPTION_FILTER old_filter{};
  HMODULE dbghelp{};
  using DumpFunction=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
  DumpFunction write_dump{};
  void emergency(const char* text,std::size_t length) noexcept {if(crash_file!=INVALID_HANDLE_VALUE){DWORD written{};WriteFile(crash_file,text,static_cast<DWORD>(length),&written,nullptr);FlushFileBuffers(crash_file);}}
  void minidump(EXCEPTION_POINTERS* pointers) noexcept {
    if(!write_dump)return;
    const auto handle=CreateFileW(dump.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),pointers,FALSE};
    const bool ok=write_dump(GetCurrentProcess(),GetCurrentProcessId(),handle,static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules),pointers?&info:nullptr,nullptr,nullptr)!=FALSE;
    CloseHandle(handle);if(!ok)DeleteFileW(dump.c_str());
  }
  static LONG WINAPI fault(EXCEPTION_POINTERS* pointers) noexcept {
    auto* self=active;if(!self)return EXCEPTION_EXECUTE_HANDLER;self->failed=true;
    char text[160]{};const int n=std::snprintf(text,sizeof(text),"\nUNHANDLED WINDOWS FAULT code=0x%08lX address=%p thread=%lu\n",pointers->ExceptionRecord->ExceptionCode,pointers->ExceptionRecord->ExceptionAddress,GetCurrentThreadId());
    if(n>0)self->emergency(text,static_cast<std::size_t>(n));
    // try_lock prevents a fault inside stream logging from deadlocking reporting.
    if(self->mutex.try_lock()){self->emergency(self->last_context.data(),strnlen_s(self->last_context.data(),self->last_context.size()));self->mutex.unlock();}
    self->minidump(pointers);return EXCEPTION_EXECUTE_HANDLER;
  }
#endif
  struct Tee:std::streambuf {
    Impl* owner{};std::streambuf* original{};
    std::streamsize xsputn(const char* text,std::streamsize count) override {
      const auto result=original->sputn(text,count);owner->append(text,static_cast<std::size_t>(count));return result;
    }
    int overflow(int value) override {if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);char c=traits_type::to_char_type(value);return xsputn(&c,1)==1?value:traits_type::eof();}
    int sync() override {std::lock_guard lock(owner->mutex);owner->file.flush();return original->pubsync();}
  } out,err,clog;
  void append(const char* text,std::size_t count) noexcept {
    try{std::lock_guard lock(mutex);if(bytes<log_limit){auto n=std::min(count,log_limit-bytes);file.write(text,static_cast<std::streamsize>(n));bytes+=n;file.flush();}
      else if(!limited){file<<"\nSession log reached 4 MiB; fatal reporting remains active.\n";file.flush();limited=true;}}
    catch(...){}
  }
  void fatal(std::string_view message) noexcept {
    failed=true;
    try{std::lock_guard lock(mutex);const auto text="\nFATAL "+utc()+": "+std::string(message)+"\nLast view: "+last_context.data()+"\n";file<<text;file.flush();
#ifdef _WIN32
      emergency(text.data(),text.size());
#else
      std::ofstream(report,std::ios::app)<<text;
#endif
    }catch(...){}
  }
  static void terminate() noexcept {
    if(active){
      const char* message="Unhandled C++ termination";
      const auto report=[](const char* reason)noexcept{
#ifdef _WIN32
        active->failed=true;char text[2048]{};const int n=std::snprintf(text,sizeof(text),"\nFATAL TERMINATION: %.1800s\n",reason);
        if(n>0)active->emergency(text,static_cast<std::size_t>(n));
        if(active->mutex.try_lock()){active->emergency(active->last_context.data(),strnlen_s(active->last_context.data(),active->last_context.size()));active->mutex.unlock();}
#else
        active->fatal(reason);
#endif
      };
      try{if(auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::exception& e){report(e.what());message=nullptr;}catch(...){}
      if(message)report(message);
#ifdef _WIN32
      active->minidump(nullptr);TerminateProcess(GetCurrentProcess(),3);
#endif
    }
    std::_Exit(3);
  }
  static void abort_signal(int) noexcept {terminate();}
  Impl(std::string_view game,std::string_view engine,std::filesystem::path directory){
    if(active)throw std::logic_error("Only one runtime diagnostics session may be active");
    if(directory.empty())directory=default_directory();std::filesystem::create_directories(directory);prune(directory);
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    auto name="session-"+std::to_string(stamp);
#ifdef _WIN32
    name+="-"+std::to_string(GetCurrentProcessId());
#endif
    path=directory/(name+".log");report=directory/(name+".txt");dump=directory/(name+".dmp");
    file.open(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open runtime log");
    const auto exe=executable_path().u8string();
    const auto header="Session "+utc()+"\nGame "+std::string(game)+" | Engine "+std::string(engine)+"\nExecutable: "+std::string(exe.begin(),exe.end())+"\n";
    file<<header;file.flush();
#ifdef _WIN32
    crash_file=CreateFileW(report.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    emergency(header.data(),header.size());
    dbghelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(dbghelp)write_dump=reinterpret_cast<DumpFunction>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));
#endif
    out.owner=err.owner=clog.owner=this;out.original=std::cout.rdbuf();err.original=std::cerr.rdbuf();clog.original=std::clog.rdbuf();
    std::cout.rdbuf(&out);std::cerr.rdbuf(&err);std::clog.rdbuf(&clog);active=this;
    old_terminate=std::set_terminate(terminate);
    old_abort=std::signal(SIGABRT,abort_signal);
#ifdef _WIN32
    old_filter=SetUnhandledExceptionFilter(fault);
#endif
  }
  ~Impl(){
    std::cout.rdbuf(out.original);std::cerr.rdbuf(err.original);std::clog.rdbuf(clog.original);
    active=nullptr;std::set_terminate(old_terminate);std::signal(SIGABRT,old_abort);
#ifdef _WIN32
    SetUnhandledExceptionFilter(old_filter);if(crash_file!=INVALID_HANDLE_VALUE)CloseHandle(crash_file);if(dbghelp)FreeLibrary(dbghelp);
#endif
    if(!failed){file<<"\nClean exit "<<utc()<<'\n';std::error_code ec;std::filesystem::remove(report,ec);}file.flush();
  }
};
RuntimeDiagnostics::RuntimeDiagnostics(std::string_view game,std::string_view engine,std::filesystem::path directory) noexcept {try{impl_=std::make_unique<Impl>(game,engine,std::move(directory));}catch(const std::exception& e){std::cerr<<"Persistent logging unavailable: "<<e.what()<<'\n';}}
RuntimeDiagnostics::~RuntimeDiagnostics()=default;
void RuntimeDiagnostics::fatal(std::string_view message)noexcept{if(impl_)impl_->fatal(message);}
std::filesystem::path RuntimeDiagnostics::log_path()const{return impl_?impl_->path:std::filesystem::path{};}
void RuntimeDiagnostics::context(std::string_view text)noexcept{
  auto* p=Impl::active;if(!p)return;
  try{std::lock_guard lock(p->mutex);const auto n=std::min(text.size(),p->last_context.size()-1);std::copy_n(text.data(),n,p->last_context.data());p->last_context[n]=0;
    const auto now=std::chrono::steady_clock::now();
    if(now-p->last_context_log>=std::chrono::seconds(1)&&p->bytes<log_limit){p->file<<"[view] "<<p->last_context.data()<<'\n';p->bytes+=n+8;p->file.flush();p->last_context_log=now;}
  }catch(...){}
}
}
