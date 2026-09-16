#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace stellar::native_map;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action action){bool rejected{};try{action();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid display setting was accepted.");}
struct TempDirectory {
  std::filesystem::path path;
  TempDirectory(){const auto nonce=std::chrono::high_resolution_clock::now().time_since_epoch().count();path=std::filesystem::temp_directory_path()/(L"stellar-native-video-acceptance-"+std::to_wstring(nonce));std::filesystem::create_directories(path);}
  ~TempDirectory(){std::error_code ec;const auto root=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);const auto normalized=std::filesystem::weakly_canonical(path,ec);if(!ec&&normalized.parent_path()==root&&normalized.filename().wstring().starts_with(L"stellar-native-video-acceptance-"))std::filesystem::remove_all(normalized,ec);}
};
std::array<unsigned char,4> pixel(const RgbaImage& image,int x,int y){const auto offset=(static_cast<std::size_t>(y)*static_cast<std::size_t>(image.width())+static_cast<std::size_t>(x))*4u;const auto& p=image.pixels();return {p[offset],p[offset+1],p[offset+2],p[offset+3]};}
void near_color(const RgbaImage& image,int x,int y,Color wanted,const char* message){const auto got=pixel(image,x,y);require(std::abs(static_cast<int>(got[0])-wanted.r)<8&&std::abs(static_cast<int>(got[1])-wanted.g)<8&&std::abs(static_cast<int>(got[2])-wanted.b)<8,message);}
SDL_Window* find_test_window(){int count{};auto** windows=SDL_GetWindows(&count);if(!windows)return nullptr;SDL_Window* found{};for(int i=0;i<count;++i)if(std::string(SDL_GetWindowTitle(windows[i]))=="Stellar display validation")found=windows[i];SDL_free(windows);return found;}
void push_key(SDL_Keycode key){SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=key;event.key.repeat=false;require(SDL_PushEvent(&event)==1,"Could not inject screenshot key event.");}
void verify_screenshot(Window& window,const std::filesystem::path& path,int width,int height){window.draw(DrawList{});require(std::filesystem::is_regular_file(path),"Screenshot was not produced.");const auto decoded=decode_rgba_image(path);require(decoded->width()==width&&decoded->height()==height,"Screenshot dimensions did not match the rendered drawable.");const auto status=window.take_screenshot_status();require(status&&status->find("saved")!=std::string::npos,"Successful screenshot status was missing.");}
}
int main(int argc,char**argv){
  try{
    require(argc==2,"Expected packaged interface font.");
    TempDirectory temp;
    Window window("Stellar display validation",1280,720,true,std::filesystem::path(argv[1]));
    auto* native=find_test_window();require(native!=nullptr,"Could not identify the bounded SDL test window.");
    auto modes=window.display_modes();require(!modes.empty()&&modes.size()<=256,"Detected display choices were empty or unbounded.");
    int display_count{};auto* displays=SDL_GetDisplays(&display_count);require(displays&&display_count>0,"SDL did not report an active display.");
    const auto original_display=SDL_GetDisplayForWindow(native);require(original_display!=0,"Current display could not be queried.");
    const float scale=SDL_GetDisplayContentScale(original_display);require(std::isfinite(scale)&&scale>0.f,"Display DPI scale was invalid.");
    SDL_Rect bounds{},usable{};require(SDL_GetDisplayBounds(original_display,&bounds)&&SDL_GetDisplayUsableBounds(original_display,&usable),"Display coordinate bounds could not be queried.");
    const int desktop_w=window.drawable_width(),desktop_h=window.drawable_height();
    rejects([&]{window.set_display_mode(WindowDisplayMode::ExclusiveFullscreen,1,1,60.f);});
    require(window.drawable_width()==desktop_w&&window.drawable_height()==desktop_h,"Rejected resolution changed drawable dimensions.");
    const auto desktop_refresh=window.display_refresh_hz();
    rejects([&]{window.set_vsync(7);});
    rejects([&]{window.set_frame_cap(std::numeric_limits<double>::quiet_NaN());});
    window.set_fullscreen_mode(true);
    require(std::abs(window.display_refresh_hz()-desktop_refresh)<.1f,"Desktop-default exclusive mode changed the desktop refresh rate.");
    window.set_display_mode(WindowDisplayMode::Borderless);
    require((SDL_GetWindowFlags(native)&SDL_WINDOW_FULLSCREEN)!=0,"Borderless mode was not fullscreen.");
    require(window.drawable_width()==desktop_w&&window.drawable_height()==desktop_h,"Borderless mode lost the desktop drawable dimensions.");
    require(std::abs(window.display_refresh_hz()-desktop_refresh)<.1f,"Borderless mode changed the desktop refresh rate.");
    window.set_display_mode(WindowDisplayMode::Windowed,960,540);
    require((SDL_GetWindowFlags(native)&SDL_WINDOW_FULLSCREEN)==0,"Windowed mode retained fullscreen state.");
    require((SDL_GetWindowFlags(native)&SDL_WINDOW_RESIZABLE)!=0,"Windowed mode was not resizable.");
    require((SDL_GetWindowFlags(native)&SDL_WINDOW_BORDERLESS)==0,"Windowed mode was not decorated.");
    int client_w{},client_h{},x{},y{},top{},left{},bottom{},right{};
    require(SDL_GetWindowSize(native,&client_w,&client_h)&&client_w==960&&client_h==540,"Windowed client resolution was not preserved.");
    require(SDL_GetWindowPosition(native,&x,&y)&&SDL_GetWindowBordersSize(native,&top,&left,&bottom,&right),"Windowed coordinate or frame query failed.");
    require(x>=usable.x&&y>=usable.y&&x+client_w+left+right<=usable.x+usable.w&&y+client_h+top+bottom<=usable.y+usable.h,"Windowed frame did not fit within usable display bounds.");
    int pixel_w{},pixel_h{};require(SDL_GetWindowSizeInPixels(native,&pixel_w,&pixel_h),"Window pixel size query failed.");
    require(pixel_w==window.drawable_width()&&pixel_h==window.drawable_height(),"Renderer dimensions diverged from SDL drawable pixels.");
    const auto coordinate=Point{client_w*.5f,client_h*.5f};Point mapped{};require(SDL_RenderCoordinatesFromWindow(SDL_GetRenderer(native),coordinate.x,coordinate.y,&mapped.x,&mapped.y),"Window to drawable coordinate conversion failed.");
    require(std::abs(mapped.x-window.drawable_width()*.5f)<2.f&&std::abs(mapped.y-window.drawable_height()*.5f)<2.f,"Pointer coordinate mapping did not track the drawable scale.");
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=SDL_GetWindowID(native);motion.motion.x=coordinate.x;motion.motion.y=coordinate.y;require(SDL_PushEvent(&motion)==1,"Could not inject test pointer motion.");const auto pointer_snapshot=window.poll();const auto mapped_event=std::find_if(pointer_snapshot.events.begin(),pointer_snapshot.events.end(),[](const auto& item){return item.type==InputEventType::PointerMove;});require(mapped_event!=pointer_snapshot.events.end()&&std::abs(mapped_event->position.x-window.drawable_width()*.5f)<2.f&&std::abs(mapped_event->position.y-window.drawable_height()*.5f)<2.f,"Injected pointer coordinates did not map to the renderer drawable.");
    if(display_count>1){SDL_DisplayID second{};for(int i=0;i<display_count;++i)if(displays[i]!=original_display){second=displays[i];break;}require(second!=0,"Second active display was not distinct from the current display.");SDL_Rect second_bounds{};require(SDL_GetDisplayBounds(second,&second_bounds),"Second display bounds query failed.");const int target_x=second_bounds.x+std::max(0,(second_bounds.w-client_w)/2),target_y=second_bounds.y+std::max(0,(second_bounds.h-client_h)/2);require(SDL_SetWindowPosition(native,target_x,target_y)&&SDL_SyncWindow(native),"Could not move test window to the second display.");const auto second_current=SDL_GetDisplayForWindow(native);require(second_current==second,"Window did not move onto the second display.");window.set_display_mode(WindowDisplayMode::Borderless);require(SDL_GetDisplayForWindow(native)==second,"Borderless transition lost the selected display.");window.set_display_mode(WindowDisplayMode::Windowed,960,540);require(SDL_GetDisplayForWindow(native)==second,"Windowed restoration lost the selected display.");modes=window.display_modes();require(!modes.empty(),"Second display did not expose supported exclusive modes.");}
    SDL_free(displays);

    const Color red{232,24,32,255},green{18,218,42,255},blue{24,48,236,255},yellow{238,218,24,255};
    DrawList pattern;const float w=static_cast<float>(window.drawable_width()),h=static_cast<float>(window.drawable_height());
    pattern.overlay.emplace_back(FilledRectangle{{0,0,w*.5f,h*.5f},red});pattern.overlay.emplace_back(FilledRectangle{{w*.5f,0,w*.5f,h*.5f},green});pattern.overlay.emplace_back(FilledRectangle{{0,h*.5f,w*.5f,h*.5f},blue});pattern.overlay.emplace_back(FilledRectangle{{w*.5f,h*.5f,w*.5f,h*.5f},yellow});
    const auto capture=temp.path/L"capture-测试.png";require(window.request_screenshot(capture),"Screenshot request was not accepted.");require(!window.request_screenshot(temp.path/L"second.png"),"Pending screenshot queue was not bounded.");window.draw(pattern);
    auto decoded=decode_rgba_image(capture);require(decoded->width()==window.drawable_width()&&decoded->height()==window.drawable_height(),"PNG dimensions did not match drawable pixels.");near_color(*decoded,decoded->width()/4,decoded->height()/4,red,"Screenshot missed the rendered red quadrant.");near_color(*decoded,decoded->width()*3/4,decoded->height()/4,green,"Screenshot missed the rendered green quadrant.");near_color(*decoded,decoded->width()/4,decoded->height()*3/4,blue,"Screenshot missed the rendered blue quadrant.");near_color(*decoded,decoded->width()*3/4,decoded->height()*3/4,yellow,"Screenshot missed the rendered yellow quadrant.");
    {std::ofstream sentinel(capture,std::ios::binary|std::ios::trunc);sentinel<<"keep-existing-capture";}require(window.request_screenshot(capture),"A completed screenshot did not release the one-entry queue.");window.draw(pattern);{std::ifstream input(capture,std::ios::binary);std::string contents((std::istreambuf_iterator<char>(input)),{});require(contents=="keep-existing-capture","Screenshot overwrote an existing file.");}const auto collision=window.take_screenshot_status();require(collision&&collision->find("failed")!=std::string::npos,"Existing-file collision did not report a capture failure.");
    const auto missing_parent=temp.path/L"missing-parent"/L"failure.png";require(window.request_screenshot(missing_parent),"Unwritable capture request was not queued.");window.draw(pattern);const auto failed=window.take_screenshot_status();require(failed&&failed->find("failed")!=std::string::npos,"Write failure did not produce a status error.");window.draw(pattern);require(window.drawable_width()>0&&window.drawable_height()>0,"Screenshot write failure stopped rendering.");

    wchar_t* previous_env{};std::size_t previous_env_size{};require(_wdupenv_s(&previous_env,&previous_env_size,L"STELLAR_SCREENSHOT_DIR")==0,"Could not read screenshot directory environment.");const std::wstring saved_env=previous_env?previous_env:L"";std::free(previous_env);require(_wputenv_s(L"STELLAR_SCREENSHOT_DIR",(temp.path/L"hotkeys").c_str())==0,"Could not isolate F12 capture directory.");
    const auto hotkey_dir=temp.path/L"hotkeys";push_key(SDLK_A);(void)window.poll();window.draw(pattern);require(!std::filesystem::exists(hotkey_dir),"An unrelated key created a screenshot.");SDL_Event repeated{};repeated.type=SDL_EVENT_KEY_DOWN;repeated.key.key=SDLK_F12;repeated.key.repeat=true;require(SDL_PushEvent(&repeated)==1,"Could not inject repeated F12 event.");(void)window.poll();window.draw(pattern);require(!std::filesystem::exists(hotkey_dir),"A repeated F12 event created a screenshot.");
    push_key(SDLK_F12);(void)window.poll();window.draw(pattern);int screenshot_count{};for(const auto& entry:std::filesystem::directory_iterator(hotkey_dir))if(entry.path().extension()==L".png")++screenshot_count;require(screenshot_count==1,"F12 did not route to one automatic PNG screenshot.");
    push_key(SDLK_PRINTSCREEN);(void)window.poll();window.draw(pattern);screenshot_count=0;for(const auto& entry:std::filesystem::directory_iterator(hotkey_dir))if(entry.path().extension()==L".png")++screenshot_count;require(screenshot_count==2,"PrintScreen did not route to a second unique PNG screenshot.");require(_wputenv_s(L"STELLAR_SCREENSHOT_DIR",saved_env.c_str())==0,"Could not restore screenshot directory environment.");

    const auto chosen=modes.back();window.set_display_mode(WindowDisplayMode::ExclusiveFullscreen,chosen.width,chosen.height,chosen.refresh_hz);window.draw(pattern);require((SDL_GetWindowFlags(native)&SDL_WINDOW_FULLSCREEN)!=0,"Exclusive mode did not enter fullscreen.");require(window.drawable_width()==chosen.width&&window.drawable_height()==chosen.height,"Exclusive mode drawable did not match its selected resolution.");require(std::abs(window.display_refresh_hz()-chosen.refresh_hz)<.2f,"Exclusive mode did not retain its selected refresh tuple.");window.set_display_mode(WindowDisplayMode::Borderless);window.set_display_mode(WindowDisplayMode::Windowed,960,540);
    window.set_vsync(0);window.set_frame_cap(60.);window.draw(DrawList{});
    const auto start=std::chrono::steady_clock::now();double throttle{};for(int frame=0;frame<12;++frame){FrameTiming timing;window.draw(DrawList{}, {},&timing);throttle+=timing.throttle_ms;}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();require(elapsed>=150.&&throttle>0.,"Explicit frame cap did not pace presentation.");window.set_frame_cap(0.);require(window.presentation_mode()=="unbounded","Unlimited retained an invisible frame cap.");window.set_vsync(1);require(window.presentation_mode()=="vsync","VSync did not restore.");
    std::cout<<"native_video_platform: displays="<<display_count<<" modes="<<modes.size()<<" scale="<<scale<<" desktop="<<desktop_w<<'x'<<desktop_h<<" hz="<<desktop_refresh<<" capture_pixels=ok overwrite=blocked failure_nonfatal=ok f12_printscreen=ok exclusive=ok cap_elapsed_ms="<<elapsed<<" throttle_ms="<<throttle<<'\n';return 0;
  }catch(const std::exception& error){std::cerr<<"native video platform test failed: "<<error.what()<<'\n';return 1;}
}
