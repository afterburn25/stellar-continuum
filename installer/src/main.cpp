#include "stellar/installer/windows_platform.hpp"
#include "stellar/engine/runtime_paths.hpp"
#include "stellar/engine/runtime_directory_lease.hpp"
#include "stellar/build_version.hpp"
#ifndef STELLAR_UNINSTALLER
#include "setup_pin.hpp"
#else
#define STELLAR_SETUP_MANIFEST_SHA256 ""
#endif
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <mutex>
#include <thread>
#include <memory>
#include <sstream>
#include <iomanip>

namespace si=stellar::installer;
namespace fs=std::filesystem;
namespace {
#ifdef STELLAR_UNINSTALLER
constexpr bool removing=true;
#else
constexpr bool removing=false;
#endif
constexpr COLORREF background=RGB(14,20,30),panel=RGB(21,31,44),text_color=RGB(226,234,246),muted=RGB(157,176,199),accent=RGB(111,215,221);
constexpr int action_id=101,browse_id=102,cancel_id=103;
struct App {
  explicit App(fs::path test_root={}):platform(std::move(test_root)){}
  si::WindowsPlatform platform;std::optional<si::Registration> installed;si::Release release;fs::path payload,root,capture;
  si::Mode mode{};HWND window{},location{},versions{},space{},status{},bar{},action{},cancel_button{},desktop{},developer{},launch{},browse{};
  HFONT font{},heading{},small_font{};HBRUSH brush{};std::thread worker;std::mutex mutex;si::Progress progress;std::string error;std::atomic_bool cancel{},busy{},finished{},commit_started{};bool success{};
  ~App(){if(worker.joinable())worker.join();for(auto f:{font,heading,small_font})if(f)DeleteObject(f);if(brush)DeleteObject(brush);}
};
std::wstring size_text(std::uint64_t size){std::wostringstream s;s<<std::fixed<<std::setprecision(2)<<double(size)/(1024.0*1024*1024)<<L" GB";return s.str();}
void label(HWND h,const std::wstring& s){SetWindowTextW(h,s.c_str());}
HWND control(App& a,const wchar_t* kind,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id=0){auto result=CreateWindowExW(0,kind,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,a.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(a.font),TRUE);return result;}
void update_space(App& a){if(removing){label(a.space,L"Saves and settings will be kept.\r\nOnly installed game files are removed.");return;}try{wchar_t path[32768]{};GetWindowTextW(a.location,path,32768);a.root=path;auto probe=a.root;while(!fs::exists(probe)&&probe.has_parent_path())probe=probe.parent_path();auto free=fs::space(probe).available;std::uint64_t required=a.release.document.value("maximumAdditionalBytes",a.release.document.value("runtimeBytes",std::uint64_t{})+64ULL*1024*1024);label(a.space,(removing?L"Saves and settings will be kept.":L"Free: "+size_text(free)+L"    |    Maximum additional space: "+size_text(required))+L"\r\n"+(removing?L"Only files installed by Stellar Continuum are removed.":L"Exact update space is checked after scanning installed files."));}catch(const std::exception&){label(a.space,L"Choose an accessible local drive and folder.");}}
void browse(App& a){IFileOpenDialog* dialog=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Choose a parent folder for Stellar Continuum");if(SUCCEEDED(dialog->Show(a.window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR p=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){label(a.location,(fs::path(p)/"Stellar Continuum").wstring());CoTaskMemFree(p);}item->Release();}}dialog->Release();}update_space(a);}
void start(App& a){
  if(a.worker.joinable())a.worker.join();wchar_t root[32768]{};GetWindowTextW(a.location,root,32768);a.root=root;
  if(!a.root.is_absolute()){label(a.status,L"Choose an absolute folder on a local drive.");return;}
  a.cancel=false;a.finished=false;a.commit_started=false;a.busy=true;a.success=false;{std::lock_guard lock(a.mutex);a.error.clear();a.progress={"Checking installation","",0,1,true};}
  for(auto h:{a.action,a.browse,a.location,a.desktop,a.developer,a.launch})EnableWindow(h,FALSE);label(a.cancel_button,L"Cancel");
  bool desktop=SendMessageW(a.desktop,BM_GETCHECK,0,0)==BST_CHECKED,developer=SendMessageW(a.developer,BM_GETCHECK,0,0)==BST_CHECKED;
  a.worker=std::thread([&a,desktop,developer]{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{auto sink=[&a](const si::Progress& p){std::lock_guard lock(a.mutex);a.progress=p;if(!p.can_cancel)a.commit_started=true;};if(removing)si::uninstall(*a.installed,a.platform,sink);else{si::Request r{a.payload,a.root,a.release,desktop,developer,&a.cancel,sink,{}};si::execute(r,a.platform);}a.success=true;}catch(const std::exception& e){a.platform.log(e.what());std::lock_guard lock(a.mutex);a.error=e.what();}CoUninitialize();a.finished=true;});
}
void capture_window(App& a){
  RECT rect{};GetClientRect(a.window,&rect);int w=rect.right,h=rect.bottom;HDC screen=GetDC(a.window),memory=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(memory,bitmap);SendMessageW(a.window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);for(HWND child=GetWindow(a.window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!(GetWindowLongW(child,GWL_STYLE)&WS_VISIBLE))continue;RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,a.window,reinterpret_cast<POINT*>(&bounds),2);int state=SaveDC(memory);SetViewportOrgEx(memory,bounds.left,bounds.top,nullptr);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND|PRF_CHILDREN);RestoreDC(memory,state);}BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*4);SelectObject(memory,old);GetDIBits(memory,bitmap,0,static_cast<UINT>(h),pixels.data(),&info,DIB_RGB_COLORS);BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());fs::create_directories(a.capture.parent_path());std::ofstream out(a.capture,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(a.window,screen);
}
void finish(App& a){
  if(a.worker.joinable())a.worker.join();a.busy=false;
  if(a.success){label(a.status,removing?L"Stellar Continuum has been removed. Your saves and settings are safe.":L"Stellar Continuum is ready. All installed files passed verification.");SendMessageW(a.bar,PBM_SETPOS,1000,0);label(a.action,L"Done");EnableWindow(a.action,TRUE);EnableWindow(a.cancel_button,FALSE);
    if(!removing&&SendMessageW(a.launch,BM_GETCHECK,0,0)==BST_CHECKED)ShellExecuteW(a.window,L"open",(a.root/"stellar-continuum-native.exe").c_str(),nullptr,a.root.c_str(),SW_SHOWNORMAL);
  }else{std::lock_guard lock(a.mutex);label(a.status,si::widen(a.error)+L"\r\nLog: "+a.platform.log_path().wstring());label(a.action,L"Retry");EnableWindow(a.action,TRUE);EnableWindow(a.cancel_button,TRUE);label(a.cancel_button,L"Close");if(!a.installed){EnableWindow(a.browse,TRUE);EnableWindow(a.location,TRUE);}for(auto h:{a.desktop,a.developer,a.launch})EnableWindow(h,TRUE);}
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l){
  auto* a=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);a->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}if(!a)return DefWindowProcW(window,message,w,l);
  switch(message){
  case WM_ERASEBKGND:return 1;
  case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType!=ODT_BUTTON)break;bool enabled=IsWindowEnabled(item->hwndItem)!=FALSE;auto fill=CreateSolidBrush(item->CtlID==action_id&&enabled?accent:panel);FillRect(item->hDC,&item->rcItem,fill);DeleteObject(fill);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,item->CtlID==action_id&&enabled?background:enabled?text_color:muted);SelectObject(item->hDC,a->font);wchar_t title[128]{};GetWindowTextW(item->hwndItem,title,128);auto rect=item->rcItem;if(item->itemState&ODS_SELECTED)OffsetRect(&rect,1,1);DrawTextW(item->hDC,title,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS){InflateRect(&rect,-4,-4);DrawFocusRect(item->hDC,&rect);}return TRUE;}
  case WM_PAINT:case WM_PRINTCLIENT:{PAINTSTRUCT ps{};HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(w):BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);FillRect(dc,&r,a->brush);RECT side{0,0,195,r.bottom};auto b=CreateSolidBrush(panel);FillRect(dc,&side,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,accent);SelectObject(dc,a->heading);RECT brand{24,35,180,125};DrawTextW(dc,L"STELLAR\nCONTINUUM",-1,&brand,DT_LEFT|DT_WORDBREAK);SelectObject(dc,a->small_font);SetTextColor(dc,muted);RECT sideinfo{24,150,175,430};DrawTextW(dc,L"WINDOWS EDITION\n\nOffline setup\n\nCurrent user only\n\nSaves stay safe",-1,&sideinfo,DT_LEFT|DT_WORDBREAK);RECT foot{24,470,179,554};auto version=si::widen(STELLAR_GAME_VERSION);DrawTextW(dc,version.c_str(),-1,&foot,DT_LEFT|DT_WORDBREAK);
if(message==WM_PAINT)EndPaint(window,&ps);return 0;}
  case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetTextColor(reinterpret_cast<HDC>(w),text_color);SetBkColor(reinterpret_cast<HDC>(w),background);return reinterpret_cast<LRESULT>(a->brush);
  case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(w),text_color);SetBkColor(reinterpret_cast<HDC>(w),panel);return reinterpret_cast<LRESULT>(a->brush);
  case WM_COMMAND:if(LOWORD(w)==action_id){if(a->success)DestroyWindow(window);else start(*a);}else if(LOWORD(w)==browse_id)browse(*a);else if(LOWORD(w)==cancel_id){if(a->busy){a->cancel=true;label(a->status,L"Cancelling preparation safely...");}else DestroyWindow(window);}else if(reinterpret_cast<HWND>(l)==a->location&&HIWORD(w)==EN_CHANGE&&a->space)update_space(*a);return 0;
  case WM_TIMER:if(w==2){capture_window(*a);DestroyWindow(window);return 0;}if(a->finished.exchange(false))finish(*a);else if(a->busy){std::lock_guard lock(a->mutex);auto& p=a->progress;label(a->status,si::widen(p.phase)+L"\r\n"+si::widen(p.file));auto pos=p.total?std::min<std::uint64_t>(1000,p.completed*1000/p.total):0;SendMessageW(a->bar,PBM_SETPOS,static_cast<WPARAM>(pos),0);EnableWindow(a->cancel_button,p.can_cancel&&!a->cancel.load());}return 0;
  case WM_CLOSE:if(a->busy){if(!a->commit_started)a->cancel=true;label(a->status,a->commit_started?L"Finishing the protected update. Please keep this window open.":L"Cancelling preparation safely...");return 0;}DestroyWindow(window);return 0;
  case WM_DESTROY:PostQuitMessage(0);return 0;
  }return DefWindowProcW(window,message,w,l);
}
void show(App& a){
  a.brush=CreateSolidBrush(background);a.font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");a.heading=CreateFontW(-25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");a.small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StellarContinuumSetup";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(101));RegisterClassW(&wc);
  auto window=CreateWindowExW(0,wc.lpszClassName,removing?L"Stellar Continuum — Uninstall":L"Stellar Continuum — Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,870,640,nullptr,nullptr,wc.hInstance,&a);BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
  auto title=removing?L"Remove Stellar Continuum":a.mode==si::Mode::Install?L"Install Stellar Continuum":a.mode==si::Mode::Update?L"Update Stellar Continuum":a.mode==si::Mode::Repair?L"Your game is already installed":L"A newer version is installed";
  auto h=control(a,L"STATIC",title,0,224,28,600,42);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(a.heading),TRUE);
  auto versions=L"Installed: "+si::widen(a.installed?a.installed->version:"Not installed")+(removing?L"":L"     Available: "+si::widen(a.release.version.string()));a.versions=control(a,L"STATIC",versions.c_str(),0,226,79,598,32);
  control(a,L"STATIC",L"Installation folder",0,226,123,550,24);a.location=control(a,L"EDIT",a.root.c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,226,151,478,30);a.browse=control(a,L"BUTTON",L"Browse…",WS_TABSTOP|BS_OWNERDRAW,714,151,110,30,browse_id);SendMessageW(a.location,EM_SETREADONLY,a.installed.has_value(),0);EnableWindow(a.browse,!a.installed);
  a.space=control(a,L"STATIC",L"",0,226,194,595,52);SendMessageW(a.space,WM_SETFONT,reinterpret_cast<WPARAM>(a.small_font),TRUE);update_space(a);
  a.desktop=control(a,L"BUTTON",L"Create a desktop shortcut",BS_AUTOCHECKBOX|WS_TABSTOP,226,262,595,27);a.developer=control(a,L"BUTTON",L"Add Developer Game to the Start menu",BS_AUTOCHECKBOX|WS_TABSTOP,226,293,595,27);a.launch=control(a,L"BUTTON",L"Launch the game when finished",BS_AUTOCHECKBOX|WS_TABSTOP,226,324,595,27);
  SendMessageW(a.desktop,BM_SETCHECK,a.installed?a.installed->desktop:TRUE,0);SendMessageW(a.developer,BM_SETCHECK,a.installed?a.installed->developer:FALSE,0);SendMessageW(a.launch,BM_SETCHECK,BST_UNCHECKED,0);
  for(auto checkbox:{a.desktop,a.developer,a.launch})SetWindowTheme(checkbox,L"",L"");
  if(removing)for(auto c:{a.desktop,a.developer,a.launch})ShowWindow(c,SW_HIDE);
  a.status=control(a,L"STATIC",a.mode==si::Mode::Repair&&!removing?L"Repair checks every file and replaces only missing or damaged files.":a.mode==si::Mode::DowngradeBlocked?L"This older installer cannot replace your newer game. Use a newer setup.":removing?L"Your saves, settings, mods and personal files will remain on this computer.":L"Ready. Setup will check the package and available space before making changes.",0,226,370,600,114);SendMessageW(a.status,WM_SETFONT,reinterpret_cast<WPARAM>(a.small_font),TRUE);
  a.bar=control(a,PROGRESS_CLASSW,L"",PBS_SMOOTH,226,496,598,12);SetWindowTheme(a.bar,L"",L"");SendMessageW(a.bar,PBM_SETBKCOLOR,0,panel);SendMessageW(a.bar,PBM_SETBARCOLOR,0,accent);SendMessageW(a.bar,PBM_SETRANGE32,0,1000);
  a.cancel_button=control(a,L"BUTTON",L"Close",WS_TABSTOP|BS_OWNERDRAW,587,539,100,36,cancel_id);auto action=removing?L"Uninstall":a.mode==si::Mode::DowngradeBlocked?L"Unavailable":si::widen(si::mode_name(a.mode));a.action=control(a,L"BUTTON",action.c_str(),BS_OWNERDRAW|WS_TABSTOP,700,539,124,36,action_id);EnableWindow(a.action,a.mode!=si::Mode::DowngradeBlocked);SetTimer(window,1,100,nullptr);ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);if(!a.capture.empty())SetTimer(window,2,1000,nullptr);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
  bool check=false,apply_update=false;
  try{
    auto own=stellar::engine::executable_path();int count{};auto argv=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring worker_id;fs::path test_root,capture;
    for(int i=1;i<count;++i){std::wstring arg=argv[i];
#ifdef STELLAR_INSTALLER_UI_TESTS
      if(arg==L"--test-root"&&i+1<count){test_root=argv[++i];continue;}
#endif
      if(arg==L"--capture-ui"&&i+1<count)capture=argv[++i];else if(arg==L"--check-package")check=true;else if(arg==L"--apply-update")apply_update=true;else if(arg==L"--uninstall-worker"&&i+1<count)worker_id=argv[++i];else throw std::runtime_error("Unknown installer argument.");}LocalFree(argv);
    if(apply_update&&(removing||check||!capture.empty()))throw std::runtime_error("Choose one installer operation.");
#ifdef STELLAR_INSTALLER_UI_TESTS
    if(test_root.empty()||capture.empty())throw std::runtime_error("Review executable requires an isolated test root and capture path.");
#endif
    App app(test_root);app.capture=capture;
    if(!removing){
      app.payload=own.parent_path()/"Payload";auto manifest=app.payload/"release-manifest.json";
      if(std::string_view(STELLAR_SETUP_MANIFEST_SHA256).size()!=64)throw std::runtime_error("This setup has no pinned release. Build it with tools/build-release-installer.ps1.");
      if(fs::file_size(manifest)>16*1024*1024||si::sha256_file(manifest)!=STELLAR_SETUP_MANIFEST_SHA256)throw std::runtime_error("Installer metadata is changed or from a different release. Extract the complete original setup archive again.");
      app.release=si::Release::parse(si::read_json(manifest));
      if(app.release.version.string()!=STELLAR_GAME_VERSION||app.release.engine_version!=STELLAR_ENGINE_VERSION)throw std::runtime_error("Installer and payload version mismatch.");
      if(check){for(const auto& f:app.release.files){if(!app.release.includes_payload(f))continue;si::validate_tree_path(app.payload,si::path_from_utf8(f.path));if(fs::file_size(app.payload/si::path_from_utf8(f.path))!=f.bytes||si::sha256_file(app.payload/si::path_from_utf8(f.path))!=f.sha256)throw std::runtime_error("Payload verification failed: "+f.path);}app.platform.log("Offline package check passed; no installation changes.");return 0;}
    }
    if(app.capture.empty()){
      stellar::engine::RuntimeDirectoryLease maintenance(*app.platform.serialization_key());
      if(auto pending=app.platform.pending()){stellar::engine::RuntimeDirectoryLease lease(*pending);app.platform.check_running(*pending);si::recover(*pending,app.platform);app.platform.set_pending({});}
    }
    app.installed=app.platform.installed();app.root=app.installed?app.installed->root:si::default_install_directory();
    if(removing){
      if(!app.installed)throw std::runtime_error("Stellar Continuum is not registered for this user.");
      if(worker_id.empty()){
        auto target=si::local_data()/"Installer"/"Cache"/si::new_id()/"StellarContinuumUninstall.exe";fs::create_directories(target.parent_path());fs::copy_file(own,target);
        auto args=L"--uninstall-worker \""+si::widen(app.installed->install_id)+L"\"";
        auto result=ShellExecuteW(nullptr,L"open",target.c_str(),args.c_str(),target.parent_path().c_str(),SW_SHOWNORMAL);
        if(reinterpret_cast<INT_PTR>(result)<=32)throw std::runtime_error("Cannot start uninstall helper.");return 0;
      }
      if(si::narrow(worker_id)!=app.installed->install_id)throw std::runtime_error("Uninstall identity changed.");app.mode=si::Mode::Repair;
    }else app.mode=si::determine_mode(app.installed,app.release);
    if(apply_update){
      if(!app.installed||app.release.base_build.empty())throw std::runtime_error("--apply-update requires a changed-files update and an existing registered installation.");
      si::Request request{app.payload,app.root,app.release,app.installed->desktop,app.installed->developer};si::execute(request,app.platform);CoUninitialize();return 0;
    }
    show(app);CoUninitialize();return app.success?0:app.error.empty()?0:1;
  }catch(const std::exception& e){try{si::WindowsPlatform{}.log(e.what());}catch(...){}if(!check&&!apply_update)MessageBoxW(nullptr,si::widen(e.what()).c_str(),L"Stellar Continuum Setup",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
}
