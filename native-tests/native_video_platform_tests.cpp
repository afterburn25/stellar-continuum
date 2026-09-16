#include <stellar/engine/native_map_platform.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action action){bool rejected{};try{action();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid display setting was accepted.");}
}
int main(int argc,char**argv){
  try{
    using namespace stellar::native_map;
    require(argc==2,"Expected packaged interface font.");
    Window window("Stellar display validation",1280,720,true,std::filesystem::path(argv[1]));
    const auto modes=window.display_modes();
    require(!modes.empty()&&modes.size()<=256,"Detected display choices were empty or unbounded.");
    require(std::isfinite(window.display_refresh_hz())&&window.display_refresh_hz()>1.f,"Invalid detected refresh rate.");
    const int desktop_w=window.drawable_width(),desktop_h=window.drawable_height();
    rejects([&]{window.set_fullscreen_mode(true,1,1,60.f);});
    require(window.drawable_width()==desktop_w&&window.drawable_height()==desktop_h,"Rejected resolution changed window dimensions.");
    rejects([&]{window.set_vsync(7);});
    rejects([&]{window.set_frame_cap(std::numeric_limits<double>::quiet_NaN());});
    const auto desktop_refresh=window.display_refresh_hz();
    window.set_fullscreen_mode(true); // Engine resolves an enumerated desktop mode, never a synthetic SDL mode.
    require(std::abs(window.display_refresh_hz()-desktop_refresh)<.1f,"Desktop default changed the desktop refresh rate.");
    DrawList draw;window.draw(draw);
    window.set_fullscreen_mode(false);
    require(window.drawable_width()==desktop_w&&window.drawable_height()==desktop_h,"Borderless restoration lost desktop dimensions.");
    // Exercise an explicit enumerated resolution/refresh tuple, then restore.
    const auto chosen=modes.back();
    window.set_fullscreen_mode(true,chosen.width,chosen.height,chosen.refresh_hz);
    window.draw(draw);window.set_fullscreen_mode(false);
    window.set_vsync(0);window.set_frame_cap(60.);window.draw(draw);
    const auto start=std::chrono::steady_clock::now();
    double throttle{};
    for(int frame=0;frame<12;++frame){FrameTiming timing;window.draw(draw,{},&timing);throttle+=timing.throttle_ms;}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    require(elapsed>=150.&&throttle>0.,"Explicit frame cap did not pace presentation.");
    window.set_frame_cap(0.);require(window.presentation_mode()=="unbounded","Unlimited retained an invisible frame cap.");
    window.set_vsync(1);require(window.presentation_mode()=="vsync","VSync did not restore.");
    std::cout<<"native_video_platform: detected_modes="<<modes.size()<<" desktop="<<desktop_w<<'x'<<desktop_h
      <<" hz="<<window.display_refresh_hz()<<" exclusive=ok borderless_restore=ok invalid_rejected=ok cap_elapsed_ms="<<elapsed<<" throttle_ms="<<throttle<<'\n';
    return 0;
  }catch(const std::exception& error){std::cerr<<"native video platform test failed: "<<error.what()<<'\n';return 1;}
}
