#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <algorithm>
#include <array>
#include <atomic>
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
#include <tlhelp32.h>
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
  // Symbolization for the fault report — deferred loads keep SymInitialize
  // cheap and resolve PDBs only when an address is queried.
  using SymInitializeFunction=BOOL(WINAPI*)(HANDLE,PCSTR,BOOL);
  using SymSetOptionsFunction=DWORD(WINAPI*)(DWORD);
  using SymFromAddrFunction=BOOL(WINAPI*)(HANDLE,DWORD64,PDWORD64,PSYMBOL_INFO);
  using SymLineFunction=BOOL(WINAPI*)(HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINE64);
  using SymCleanupFunction=BOOL(WINAPI*)(HANDLE);
  using SymRefreshFunction=BOOL(WINAPI*)(HANDLE);
  using StackWalkFunction=BOOL(WINAPI*)(DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,PREAD_PROCESS_MEMORY_ROUTINE64,PFUNCTION_TABLE_ACCESS_ROUTINE64,PGET_MODULE_BASE_ROUTINE64,PTRANSLATE_ADDRESS_ROUTINE64);
  SymInitializeFunction sym_init{};
  SymSetOptionsFunction sym_options{};
  SymFromAddrFunction sym_from_addr{};
  SymLineFunction sym_line{};
  SymCleanupFunction sym_cleanup{};
  SymRefreshFunction sym_refresh{};
  StackWalkFunction stack_walk{};
  bool sym_ready{};
  // In-progress guard (not one-shot): a fault inside an active walk skips the
  // nested trace but still emits fault_site/minidump; a completed trace leaves
  // the next episode free to trace again.
  std::atomic<bool> trace_active{};
  mutable bool sym_refreshed{};
  // Hang watchdog: armed on the first heartbeat() (sentinel -1), fires once
  // when the UI/main thread stalls past STELLAR_WATCHDOG_MS (default 30 s,
  // 0 disables). Suspending the loop thread to snapshot its CONTEXT is the
  // best-effort part — the process is already unresponsive by definition.
  HANDLE watch_thread{};
  HANDLE main_thread{};
  DWORD main_tid{};
  std::atomic<bool> watch_stop{};
  std::atomic<bool> hang_reported{};
  std::atomic<long long> last_beat{-1};
  unsigned watch_ms{};
  static long long steady_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void heartbeat() noexcept {last_beat.store(steady_ms(),std::memory_order_relaxed);}
  void watch() noexcept {
    while(!watch_stop.load(std::memory_order_relaxed)){
      Sleep(200);
      if(watch_stop.load(std::memory_order_relaxed))break;
      const auto last=last_beat.load(std::memory_order_relaxed);
      if(last<0||steady_ms()-last<=static_cast<long long>(watch_ms))continue;
      if(hang_reported.exchange(true))break;
      failed=true;
      const char* notice="\nHANG DETECTED: main thread heartbeat stalled\n";
      emergency(notice,std::char_traits<char>::length(notice));append(notice,std::char_traits<char>::length(notice));
      if(mutex.try_lock()){emergency(last_context.data(),strnlen_s(last_context.data(),last_context.size()));mutex.unlock();}
      if(main_thread&&SuspendThread(main_thread)!=static_cast<DWORD>(-1)){
        CONTEXT ctx{};ctx.ContextFlags=CONTEXT_FULL;
        if(GetThreadContext(main_thread,&ctx)){
          if(const auto site=describe_address(reinterpret_cast<const void*>(static_cast<uintptr_t>(ctx.Rip)));!site.empty()){
            char line[512]{};const int m=std::snprintf(line,sizeof(line)," hang_site=%s\n",site.c_str());
            if(m>0)emergency(line,static_cast<std::size_t>(std::min(m,static_cast<int>(sizeof(line))-1)));}
          trace(&ctx);
        }
        ResumeThread(main_thread);
      }
      // Deadlock attribution needs every thread, not just the loop thread.
      // Suspend → snapshot CONTEXT → resume → walk offline: holding a
      // suspended thread through dbghelp allocation could wedge the
      // reporter itself if that thread owns a heap/CRT lock.
      if(stack_walk&&sym_ready){
        const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snap!=INVALID_HANDLE_VALUE){
          THREADENTRY32 entry{sizeof(entry)};
          for(auto ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)){
            if(entry.th32OwnerProcessID!=GetCurrentProcessId()||entry.th32ThreadID==GetCurrentThreadId()||entry.th32ThreadID==main_tid)continue;
            const HANDLE th=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
            if(!th)continue;
            CONTEXT ctx{};
            if(SuspendThread(th)!=static_cast<DWORD>(-1)){ctx.ContextFlags=CONTEXT_FULL;if(!GetThreadContext(th,&ctx))ctx.ContextFlags=0;ResumeThread(th);}
            if(ctx.ContextFlags){
              char head[64]{};const int m=std::snprintf(head,sizeof(head)," thread %lu:\n",entry.th32ThreadID);
              if(m>0)emergency(head,static_cast<std::size_t>(std::min(m,static_cast<int>(sizeof(head))-1)));
              trace_thread(th,&ctx);
            }
            CloseHandle(th);
          }
          CloseHandle(snap);
        }
      }
      minidump(nullptr);return;
    }
  }
  std::string describe_address(const void* address) const noexcept {
    try{
      const auto a=reinterpret_cast<DWORD64>(address);
      std::string text;
      HMODULE module{};
      wchar_t name[MAX_PATH]{};
      // HMODULE is the module base on Windows — no Psapi lookup needed.
      if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&module)&&GetModuleFileNameW(module,name,MAX_PATH)){
        const auto file_name=std::filesystem::path(name).filename().u8string();
        text.append(file_name.begin(),file_name.end());
        char off[32]{};std::snprintf(off,sizeof(off),"+0x%llX",static_cast<unsigned long long>(a-reinterpret_cast<DWORD64>(module)));text+=off;
      }else{char raw[24]{};std::snprintf(raw,sizeof(raw),"%p",address);text=raw;}
      if(sym_ready){
        char buffer[sizeof(SYMBOL_INFO)+256]{};auto* sym=reinterpret_cast<PSYMBOL_INFO>(buffer);
        sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=255;
        DWORD64 displacement{};bool resolved=false;
        if(sym_from_addr)resolved=sym_from_addr(GetCurrentProcess(),a,&displacement,sym)!=FALSE;
        // Modules loaded after initialization need one list refresh before
        // the handler can attribute their addresses.
        if(!resolved&&!sym_refreshed&&sym_refresh&&sym_from_addr){sym_refreshed=true;
          if(sym_refresh(GetCurrentProcess()))resolved=sym_from_addr(GetCurrentProcess(),a,&displacement,sym)!=FALSE;}
        if(resolved){
          text+=" ";text+=sym->Name;
          char d[24]{};std::snprintf(d,sizeof(d),"+0x%llX",static_cast<unsigned long long>(displacement));text+=d;}
        if(sym_line){IMAGEHLP_LINE64 line{sizeof(line)};DWORD line_disp{};
          if(sym_line(GetCurrentProcess(),a,&line_disp,&line)){
            const auto source_file=std::filesystem::path(line.FileName).filename().u8string();
            text+=" ";text.append(source_file.begin(),source_file.end());text+="("+std::to_string(line.LineNumber)+")";}}
      }
      return text;
    }catch(...){return{};}
  }
  // Bounded mini-trace (<=32 frames) written to the crash report beside the
  // minidump. StackWalk64 mutates the context — callers pass a private copy.
  // trace_active guards re-entry: a fault inside the walk re-enters the
  // filter and must not recurse back in here.
  void trace_frames(HANDLE thread,CONTEXT* context) noexcept {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset=context->Rip;frame.AddrPC.Mode=AddrModeFlat;
    frame.AddrFrame.Offset=context->Rbp;frame.AddrFrame.Mode=AddrModeFlat;
    frame.AddrStack.Offset=context->Rsp;frame.AddrStack.Mode=AddrModeFlat;
    for(int i=0;i<32;++i){
      if(!stack_walk(IMAGE_FILE_MACHINE_AMD64,GetCurrentProcess(),thread?thread:GetCurrentThread(),&frame,context,nullptr,nullptr,nullptr,nullptr)||frame.AddrPC.Offset==0)break;
      const auto site=describe_address(reinterpret_cast<const void*>(static_cast<uintptr_t>(frame.AddrPC.Offset)));
      char line[560]{};const int m=std::snprintf(line,sizeof(line),"  #%02d %s\n",i,site.empty()?"?":site.c_str());
      if(m>0)emergency(line,static_cast<std::size_t>(std::min(m,static_cast<int>(sizeof(line))-1)));
    }
  }
  void trace(CONTEXT* context) noexcept {
    if(!stack_walk||!sym_ready||trace_active.exchange(true))return;
    struct TraceGuard{std::atomic<bool>& flag;~TraceGuard(){flag.store(false);}} guard{trace_active};
    emergency("\n stack:\n",9);
    trace_frames(nullptr,context);
  }
  void trace_thread(HANDLE thread,CONTEXT* context) noexcept {
    if(!stack_walk||!sym_ready||trace_active.exchange(true))return;
    struct TraceGuard{std::atomic<bool>& flag;~TraceGuard(){flag.store(false);}} guard{trace_active};
    trace_frames(thread,context);
  }
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
    if(const auto site=self->describe_address(pointers->ExceptionRecord->ExceptionAddress);!site.empty()){
      char site_line[512]{};const int m=std::snprintf(site_line,sizeof(site_line)," fault_site=%s\n",site.c_str());
      if(m>0)self->emergency(site_line,static_cast<std::size_t>(std::min(m,static_cast<int>(sizeof(site_line))-1)));}
    CONTEXT context=*pointers->ContextRecord;self->trace(&context);
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
      CONTEXT context{};RtlCaptureContext(&context);active->trace(&context);
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
    if(dbghelp){
      write_dump=reinterpret_cast<DumpFunction>(GetProcAddress(dbghelp,"MiniDumpWriteDump"));
      sym_init=reinterpret_cast<SymInitializeFunction>(GetProcAddress(dbghelp,"SymInitialize"));
      sym_options=reinterpret_cast<SymSetOptionsFunction>(GetProcAddress(dbghelp,"SymSetOptions"));
      sym_from_addr=reinterpret_cast<SymFromAddrFunction>(GetProcAddress(dbghelp,"SymFromAddr"));
      sym_line=reinterpret_cast<SymLineFunction>(GetProcAddress(dbghelp,"SymGetLineFromAddr64"));
      sym_cleanup=reinterpret_cast<SymCleanupFunction>(GetProcAddress(dbghelp,"SymCleanup"));
      sym_refresh=reinterpret_cast<SymRefreshFunction>(GetProcAddress(dbghelp,"SymRefreshModuleList"));
      stack_walk=reinterpret_cast<StackWalkFunction>(GetProcAddress(dbghelp,"StackWalk64"));
      if(sym_init&&sym_options){
        sym_options(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES|SYMOPT_FAIL_CRITICAL_ERRORS);
        // Search beside the executable so shipped PDBs resolve without
        // _NT_SYMBOL_PATH. Invading enumerates loaded modules once at init —
        // deferred loads keep PDB parsing lazy until an address is queried.
        const auto dir=executable_path().parent_path().u8string();
        const std::string search(dir.begin(),dir.end());
        sym_ready=sym_init(GetCurrentProcess(),search.empty()?nullptr:search.c_str(),TRUE)!=FALSE;
      }
    }
    unsigned threshold=30000;
    if(wchar_t env[16]{};GetEnvironmentVariableW(L"STELLAR_WATCHDOG_MS",env,16)!=0){try{threshold=std::stoul(env);}catch(...){}}
    watch_ms=threshold;
    if(watch_ms>0){
      main_tid=GetCurrentThreadId();
      if(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&main_thread,0,FALSE,DUPLICATE_SAME_ACCESS))
        watch_thread=CreateThread(nullptr,0,[](void* p)->DWORD{static_cast<Impl*>(p)->watch();return 0;},this,0,nullptr);
    }
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
    SetUnhandledExceptionFilter(old_filter);
    // Stop the watchdog before closing the crash file — a mid-flight hang
    // report must not write into a closed (possibly reused) handle.
    watch_stop.store(true);if(watch_thread){if(WaitForSingleObject(watch_thread,2000)==WAIT_TIMEOUT)TerminateThread(watch_thread,0);CloseHandle(watch_thread);}if(main_thread)CloseHandle(main_thread);
    if(crash_file!=INVALID_HANDLE_VALUE)CloseHandle(crash_file);
    if(sym_ready&&sym_cleanup)sym_cleanup(GetCurrentProcess());if(dbghelp)FreeLibrary(dbghelp);
#endif
    if(!failed){file<<"\nClean exit "<<utc()<<'\n';std::error_code ec;std::filesystem::remove(report,ec);}file.flush();
  }
};
RuntimeDiagnostics::RuntimeDiagnostics(std::string_view game,std::string_view engine,std::filesystem::path directory) noexcept {try{impl_=std::make_unique<Impl>(game,engine,std::move(directory));}catch(const std::exception& e){std::cerr<<"Persistent logging unavailable: "<<e.what()<<'\n';}}
RuntimeDiagnostics::~RuntimeDiagnostics()=default;
void RuntimeDiagnostics::fatal(std::string_view message)noexcept{if(impl_)impl_->fatal(message);}
std::filesystem::path RuntimeDiagnostics::log_path()const{return impl_?impl_->path:std::filesystem::path{};}
std::string RuntimeDiagnostics::describe_address(const void* address) const noexcept {
#ifdef _WIN32
  return impl_?impl_->describe_address(address):std::string{};
#else
  (void)address;return{};
#endif
}
void RuntimeDiagnostics::heartbeat() noexcept {
#ifdef _WIN32
  if(auto* p=Impl::active)p->heartbeat();
#endif
}
void RuntimeDiagnostics::context(std::string_view text)noexcept{
  auto* p=Impl::active;if(!p)return;
  try{std::lock_guard lock(p->mutex);const auto n=std::min(text.size(),p->last_context.size()-1);std::copy_n(text.data(),n,p->last_context.data());p->last_context[n]=0;
    const auto now=std::chrono::steady_clock::now();
    if(now-p->last_context_log>=std::chrono::seconds(1)&&p->bytes<log_limit){p->file<<"[view] "<<p->last_context.data()<<'\n';p->bytes+=n+8;p->file.flush();p->last_context_log=now;}
  }catch(...){}
}
}
