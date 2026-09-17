#include "native_planetary_screen.hpp"
#include <iostream>
#include <stdexcept>
using namespace stellar::native_colony_ui;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
  for(const auto [w,h]:std::array<std::pair<int,int>,4>{{{1280,720},{1920,1080},{2560,1440},{3840,2160}}}){
    NativePlanetaryScreen screen;NativeColonyView view;
    view.campaign_generation=1;view.colony_id=7;view.building_capacity=32;view.surface_hub_level=2;view.body_display_name="Earth";
    screen.set_view(view);DrawList draw;screen.render(draw,view,w,h);
    const auto layout=PlanetaryLayout::make(w,h);const auto chrome=NativeUiLayout::for_viewport(w,h);
    check(layout.screen.x>chrome.map.x+chrome.map.width,"Planetary screen overlaps navigation");
    check(layout.screen.y>chrome.day_text.y+chrome.day_text.height,"Planetary screen overlaps campaign date");
    for(const auto& r:{layout.facts,layout.slots,layout.details,layout.queue})
      check(r.width>100&&r.height>60&&r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Responsive panel has no usable bounds");
    Point slot{};bool found=false;
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value=="Available slot"){
      slot={t->at.x+10,t->at.y+3};found=true;break;
    }
    check(found,"No visible building slots");
    (void)screen.handle({InputEventType::LeftReleased,slot},w,h);check(screen.selected_slot()==-1,"Release-only input selected a slot");
    (void)screen.handle({InputEventType::LeftPressed,slot},w,h);(void)screen.handle({InputEventType::LeftReleased,slot},w,h);
    check(screen.selected_slot()==0,"Mouse selection missed visible slot");
    NativeSurfacePlacementQuote quote;quote.accepted=true;quote.building_name="Science lab";quote.message="Authorize construction";
    screen.set_confirmation(quote);draw={};screen.render(draw,view,w,h);
    (void)screen.handle({InputEventType::LeftPressed,slot},w,h);check(screen.handle({InputEventType::LeftReleased,slot},w,h).action==PlanetaryAction::None,"Modal let a background slot through");
    check(screen.handle({InputEventType::EscapePressed},w,h).action==PlanetaryAction::Cancel,"Escape did not cancel the review");
    screen.complete("Cancelled");++view.colony_id;screen.set_view(view);
    check(screen.selected_slot()==-1&&!screen.modal(),"Changing planet retained previous selection or quote");
    check(screen.handle({InputEventType::LeftReleased,slot},w,h).action==PlanetaryAction::None,"Changing planet left stale hit targets");
  }
  std::cout<<"Planetary layout and mouse/modal routing passed at 720p, 1080p, 1440p and 4K.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
