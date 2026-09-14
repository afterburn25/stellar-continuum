#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <iostream>
namespace {
int failures{};
void check(bool passed,const char *message){if(!passed){std::cerr<<message<<'\n';++failures;}}
void push(SDL_Event event){check(SDL_PushEvent(&event),"SDL event injection failed");}
}
int main(){
  using namespace stellar::native_map;
  try{
    Window window("Stellar native input replay",640,360,false);
    (void)window.poll(); // Discard ordinary create/show notifications.
    SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.button=SDL_BUTTON_LEFT;down.button.x=20;down.button.y=30;push(down);
    SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.x=180;move.motion.y=140;push(move);
    SDL_Event up{};up.type=SDL_EVENT_MOUSE_BUTTON_UP;up.button.button=SDL_BUTTON_LEFT;up.button.x=180;up.button.y=140;push(up);
    const auto ordered=window.poll();
    check(ordered.events.size()==3,"same-poll pointer sequence was collapsed");
    if(ordered.events.size()==3){check(ordered.events[0].type==InputEventType::LeftPressed,"press was not first");check(ordered.events[1].type==InputEventType::PointerMove,"move was not second");check(ordered.events[2].type==InputEventType::LeftReleased,"release was not third");check(ordered.events[0].position.x!=ordered.events[2].position.x,"press position was replaced by release position");}
    push(down);SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;push(focus);push(move);
    const auto cancelled=window.poll();
    check(cancelled.events.size()==2,"focus loss did not cancel held input cleanly");
    if(cancelled.events.size()==2)check(cancelled.events[1].type==InputEventType::PointerCancelled,"focus loss did not emit cancellation");
    SDL_Event minimized{};minimized.type=SDL_EVENT_WINDOW_MINIMIZED;push(minimized);
    const auto inactive=window.poll();check(!inactive.renderable(),"minimized window remained renderable");
    SDL_Event restored{};restored.type=SDL_EVENT_WINDOW_RESTORED;push(restored);
    const auto active=window.poll();check(active.renderable(),"restored window did not become renderable");
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
  return failures==0?0:1;
}
