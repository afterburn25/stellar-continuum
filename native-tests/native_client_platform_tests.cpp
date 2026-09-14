#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
int failures{};
void check(bool passed,const char *message){if(!passed){std::cerr<<message<<'\n';++failures;}}
void push(SDL_Event event){check(SDL_PushEvent(&event),"SDL event injection failed");}
}
int main(int argc,char **argv){
  using namespace stellar::native_map;
  try{
    if(argc!=2)throw std::invalid_argument("Usage: platform_input_tests <Rajdhani font>");
    bool rejected_font{};try{Window rejected("Rejected font",320,180,false,"missing-font.ttf");}catch(const std::exception &){rejected_font=true;}check(rejected_font,"missing private font silently fell back");
    Window window("Stellar native input replay",640,360,false,argv[1]);
    (void)window.poll(); // Discard ordinary create/show notifications.
    SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.button=SDL_BUTTON_LEFT;down.button.x=20;down.button.y=30;push(down);
    SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.x=180;move.motion.y=140;push(move);
    SDL_Event up{};up.type=SDL_EVENT_MOUSE_BUTTON_UP;up.button.button=SDL_BUTTON_LEFT;up.button.x=180;up.button.y=140;push(up);
    const auto ordered=window.poll();
    check(ordered.events.size()==3,"same-poll pointer sequence was collapsed");
    if(ordered.events.size()==3){check(ordered.events[0].type==InputEventType::LeftPressed,"press was not first");check(ordered.events[1].type==InputEventType::PointerMove,"move was not second");check(ordered.events[2].type==InputEventType::LeftReleased,"release was not third");check(ordered.events[0].position.x!=ordered.events[2].position.x,"press position was replaced by release position");}
    SDL_Event right_down{};right_down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;right_down.button.button=SDL_BUTTON_RIGHT;right_down.button.x=240;right_down.button.y=160;push(right_down);SDL_Event right_up{};right_up.type=SDL_EVENT_MOUSE_BUTTON_UP;right_up.button.button=SDL_BUTTON_RIGHT;right_up.button.x=240;right_up.button.y=160;push(right_up);const auto right_click=window.poll();check(right_click.events.size()==2,"right-click events were dropped");if(right_click.events.size()==2){check(right_click.events[0].type==InputEventType::RightPressed,"right press was not ordered");check(right_click.events[1].type==InputEventType::RightReleased,"right release was not ordered");}
    push(down);SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;push(focus);push(move);
    const auto cancelled=window.poll();
    check(cancelled.events.size()==3,"focus loss and hover motion were not preserved");
    if(cancelled.events.size()==3){check(cancelled.events[1].type==InputEventType::PointerCancelled,"focus loss did not emit cancellation");check(cancelled.events[2].type==InputEventType::PointerMove,"post-cancel hover motion was dropped");check(cancelled.pointer.x==cancelled.events[2].position.x,"snapshot did not retain final drawable pointer");}
    SDL_Event minimized{};minimized.type=SDL_EVENT_WINDOW_MINIMIZED;push(minimized);
    const auto inactive=window.poll();check(!inactive.renderable(),"minimized window remained renderable");
    SDL_Event restored{};restored.type=SDL_EVENT_WINDOW_RESTORED;push(restored);
    const auto active=window.poll();check(active.renderable(),"restored window did not become renderable");
    window.set_text_input(true);SDL_Event text_input{};text_input.type=SDL_EVENT_TEXT_INPUT;text_input.text.text="research";push(text_input);SDL_Event backspace{};backspace.type=SDL_EVENT_KEY_DOWN;backspace.key.key=SDLK_BACKSPACE;push(backspace);const auto editing=window.poll();check(editing.events.size()==2,"ordered text editing events were lost");if(editing.events.size()==2){check(editing.events[0].type==InputEventType::TextEntered&&editing.events[0].text=="research","UTF-8 text event was not preserved");check(editing.events[1].type==InputEventType::BackspacePressed,"Backspace event was not preserved");}window.set_text_input(false);
    DrawList repeated;repeated.overlay.emplace_back(FilledRectangle{{8,8,180,54},{8,18,34,240}});repeated.overlay.emplace_back(Text{{18,12},"CACHE STABLE TEXT WRAPS",{235,245,255,255},18,120.f,UiRect{12,10,150,44}});window.draw(repeated);const auto stable_entries=window.text_cache_entries(),stable_bytes=window.text_cache_bytes();for(int frame=0;frame<8;++frame)window.draw(repeated);check(window.text_cache_entries()==stable_entries&&window.text_cache_bytes()==stable_bytes,"identical frames rebuilt cached text resources");
    DrawList pressure;for(int index=0;index<700;++index)pressure.overlay.emplace_back(Text{{-200,-200},"bounded-cache-"+std::to_string(index),{255,255,255,255},14});window.draw(pressure);check(window.text_cache_entries()<=640,"text cache exceeded entry capacity");check(window.text_cache_bytes()<=32u*1024u*1024u,"text cache exceeded byte capacity");
    DrawList invalid;invalid.overlay.emplace_back(Text{{0,0},"invalid",{255,255,255,255},14,std::numeric_limits<float>::quiet_NaN()});bool rejected_bounds{};try{window.draw(invalid);}catch(const std::invalid_argument &){rejected_bounds=true;}check(rejected_bounds,"non-finite UI text bounds reached native conversion");
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
  if(failures==0)std::cout<<"Native ordered input, inactive state, font validation, stable reuse and bounded text cache passed\n";
  return failures==0?0:1;
}
