#include "native_developer_celestial_index.hpp"
#include "native_developer_empire_monitor.hpp"
#include "native_developer_planet_index.hpp"
#include "native_developer_simulation_panel.hpp"
#include "native_stellar_activity_panel.hpp"
#include "native_stellar_observation.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace stellar::native_map;
using namespace stellar::core;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
Point control(const DrawList &draw,std::string_view label){
  for(const auto &command:draw.overlay)if(const auto *text=std::get_if<Text>(&command);text&&text->value.starts_with(label)&&text->clip){
    const auto &r=*text->clip;return {r.x+r.width*.5f,r.y+std::min(r.height*.5f,10.f)};
  }
  throw std::runtime_error("Missing visible control: "+std::string(label));
}
int main(int argc,char **argv)try{
  check(argc==3,"Expected catalog/research paths.");
  for(const auto size:std::array<std::pair<int,int>,3>{{{1280,720},{1920,1080},{3840,2160}}}){
    const auto [w,h]=size;
    const auto central_radius=[&](double zoom){return stellar::native_stellar::central_black_hole_map_radius(60.,zoom,w,h);};
    check(std::abs(central_radius(.01)-55.*std::clamp(h/1080.,.67,2.))<.01,"Central black hole lost its prominent overview size.");
    check(central_radius(3.)>central_radius(.01)*1.4,"Central black hole failed to enlarge with map zoom.");
    check(std::abs(central_radius(3.)-162.)<.01,"Central black hole no longer uses the enlarged world scale.");
    check(std::abs(central_radius(1000.)-.34*std::min(w,h))<.01,"Central black hole exceeds its close-zoom viewport bound.");
    auto world=seed_persistable_fresh_campaign(-9142050,load_nearby_catalog(argv[1]),
        {"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},true});
    auto runtime=IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(world));
    CampaignFrame frame(std::move(runtime),StrategicClock{},CampaignFramePolicy::Developer);
    NativeDeveloperCelestialIndex index;
    NativeDeveloperSimulationPanel panel;
    const auto saved=[&]{return capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"});};
    const auto original=saved();
    panel.toggle();DrawList draw;panel.render(draw,w,h,frame);
    auto export_button=control(draw,"EXPORT DIAGNOSTIC BUNDLE");
    (void)panel.handle({InputEventType::LeftPressed,export_button},w,h,frame);
    (void)panel.handle({InputEventType::PointerCancelled},w,h,frame);
    (void)panel.handle({InputEventType::LeftReleased,export_button},w,h,frame);
    check(!panel.take_export_request(),"Cancelled export click was accepted.");
    (void)panel.handle({InputEventType::LeftPressed,export_button},w,h,frame);
    (void)panel.handle({InputEventType::LeftReleased,export_button},w,h,frame);
    check(panel.take_export_request()&&!panel.take_export_request(),"Export request missing or sticky.");
    panel.set_export_busy(true);
    (void)panel.handle({InputEventType::LeftPressed,export_button},w,h,frame);
    (void)panel.handle({InputEventType::LeftReleased,export_button},w,h,frame);
    check(!panel.take_export_request()&&saved()==original,"Busy export duplicated request or mutated state.");
    auto button=control(draw,"CELESTIAL INDEX");
    (void)panel.handle({InputEventType::LeftPressed,button},w,h,frame);
    check(!panel.take_index_request(),"Index opened before matching release.");
    (void)panel.handle({InputEventType::LeftReleased,button},w,h,frame);
    check(panel.take_index_request()&&!panel.take_index_request(),"Index request was missing or sticky.");
    index.open(frame.runtime().world().campaign());
    const auto render=[&]{DrawList result;index.render(result,w,h,frame);return result;};
    draw=render();
    for(const auto &command:draw.overlay)if(const auto *text=std::get_if<Text>(&command);text&&text->clip){
      const auto &r=*text->clip;
      check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Index text escaped viewport.");
    }
    // The engine VirtualizedList owns the scroll offset — wheel deltas
    // scroll whole rows and the offset clamps at both ends.
    {
      Point list_point{};bool found=false;
      for(const auto &command:draw.overlay)if(const auto *text=std::get_if<Text>(&command);text&&text->clip&&text->value.find(" objects")!=std::string::npos){
        list_point={text->clip->x+5,text->clip->y+text->clip->height+5};found=true;}
      check(found,"Index census label missing.");
      const auto texts=[](const DrawList &d){std::vector<std::string> v;for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)v.push_back(t->value);std::ranges::sort(v);return v;};
      const auto at_top=texts(render());
      InputEvent wheel{InputEventType::Wheel,list_point,{},-10.f};
      (void)index.handle(wheel,w,h,frame);
      check(texts(render())!=at_top,"Wheel did not scroll the celestial index.");
      InputEvent up{InputEventType::Wheel,list_point,{},10.f};
      (void)index.handle(up,w,h,frame);
      check(texts(render())==at_top,"Wheel scroll did not return to the head.");
      InputEvent far{InputEventType::Wheel,list_point,{},-10000.f};
      (void)index.handle(far,w,h,frame);
      const auto tail=texts(render());
      (void)index.handle(far,w,h,frame);
      check(texts(render())==tail,"Scrolling past the tail did not clamp.");
    }
    auto click=[&](Point p){check(index.handle({InputEventType::LeftPressed,p},w,h,frame),"Index press leaked through modal.");
      check(index.handle({InputEventType::LeftReleased,p},w,h,frame),"Index release leaked through modal.");};
    click(control(draw,"Search name"));
    (void)index.handle({InputEventType::TextEntered,{}, {},0,"Galactic center"},w,h,frame);
    draw=render();(void)control(draw,"Class  Central Supermassive Black Hole");
    check(saved()==original,"Index search/render mutated campaign or discovery.");
    click(control(draw,"Central state:"));
    draw=render();click(control(draw,"Relativistic Jets"));
    check(frame.runtime().world().campaign().galactic_core->black_hole->state==CentralBlackHoleState::RelativisticJets,"Inspector failed to use canonical central-state command.");
    const auto changed=saved();
    button=control(render(),"CENTER GALAXY MAP");
    (void)index.handle({InputEventType::LeftPressed,button},w,h,frame);
    (void)index.handle({InputEventType::PointerCancelled},w,h,frame);
    (void)index.handle({InputEventType::LeftReleased,button},w,h,frame);
    check(!index.take_focus_request()&&index.visible(),"Cancelled click centered map.");
    click(button);auto target=index.take_focus_request();
    check(target&&target->central&&!target->system_id&&!index.visible(),"Central navigation target is incorrect.");
    check(saved()==changed,"Index navigation changed exploration or simulation.");
    index.open(frame.runtime().world().campaign());
    check(!index.take_focus_request(),"Reopened index retained old navigation.");
    click(control(render(),"Search name"));
    (void)index.handle({InputEventType::TextEntered,{}, {},0,"no matching object"},w,h,frame);
    click(control(render(),"CENTER GALAXY MAP"));
    check(!index.take_focus_request()&&index.visible(),"Empty search activated navigation.");
    (void)index.handle({InputEventType::EscapePressed},w,h,frame);
    check(!index.visible()&&!index.handle({InputEventType::LeftPressed},w,h,frame),"Closed index captured gameplay input.");
    // Keyboard ring: the panel's rendered controls walk in (y,x) order —
    // close, category filter, search field, visible rows, then the
    // conditional central-state and center-map actions. Activation replays
    // the same dispatch pointer input takes; the search field enters edit
    // mode on Return and owns its keys until Tab commits out.
    {
      index.open(frame.runtime().world().campaign());
      const auto press=[&](std::uint32_t key,bool shift=false){
        InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;ev.shift=shift;
        return index.handle(ev,w,h,frame);};
      constexpr std::uint32_t kTab=9u,kReturn=13u,kDown=0x40000051u;
      check(index.focus()<0,"Reopened index retained keyboard focus.");
      check(press(kTab),"Index Tab press leaked.");
      check(index.focus()>=0&&!index.focused_label(w,h).empty(),"Tab did not focus a labelled index control.");
      check(index.focused_bounds(w,h).has_value(),"Focused index control lacks bounds.");
      check(press(kReturn),"Index Return leaked.");
      check(!index.visible(),"Close activation did not close the index.");
      index.open(frame.runtime().world().campaign());
      check(press(kTab)&&press(kTab)&&press(kTab),"Index navigation leaked.");
      check(index.focused_control(w,h)==stellar::engine::AnnouncementControl::Edit,"Search field was not classified as an Edit control.");
      check(press(kReturn)&&index.wants_text_input(),"Search activation did not enter edit mode.");
      check(index.handle({InputEventType::TextEntered,{}, {},0,"Galactic center"},w,h,frame),"Search text leaked.");
      check(press(kTab)&&!index.wants_text_input(),"Tab did not commit out of search editing.");
      check(press(kDown)&&press(kReturn),"Row activation leaked.");
      int guard=0;
      while(index.visible()&&index.focused_label(w,h).find("Center galaxy map")!=0&&guard++<64)
        check(press(kDown),"Index navigation past the row leaked.");
      check(index.focused_label(w,h).find("Center galaxy map")==0,"Ring did not reach the center-map action.");
      check(press(kReturn),"Center-map activation leaked.");
      const auto keyed=index.take_focus_request();
      check(keyed&&keyed->central&&!index.visible(),"Keyboard center-map activation produced the wrong target.");
      index.open(frame.runtime().world().campaign());
      check(press(kTab)&&index.focus()>=0,"Tab did not re-enter the index ring.");
      (void)index.handle({InputEventType::LeftPressed,{w*.5f,h*.5f}},w,h,frame);
      check(index.focus()<0,"Pointer press did not clear the index ring.");
      check(press(kTab),"Index Tab press leaked.");
      check(index.handle({InputEventType::EscapePressed},w,h,frame)&&index.focus()<0&&index.visible(),
        "Escape closed the index instead of releasing its ring.");
      check(index.handle({InputEventType::EscapePressed},w,h,frame)&&!index.visible(),"Second Escape did not close the index.");
    }
    NativeDeveloperPlanetIndex planet_index;planet_index.open(frame.runtime().world().campaign());
    const auto planet_render=[&]{DrawList d;planet_index.render(d,w,h);return d;};
    auto planet_click=[&](Point point){check(planet_index.handle({InputEventType::LeftPressed,point},w,h,frame),"Planet index leaked input");check(planet_index.handle({InputEventType::LeftReleased,point},w,h,frame),"Planet index release leaked input");};
    // The planet list scrolls through the same engine VirtualizedList —
    // wheel deltas move whole rows and clamp at both ends.
    {
      const auto planet_texts=[](const DrawList &d){std::vector<std::string> v;for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)v.push_back(t->value);std::ranges::sort(v);return v;};
      const auto planet_top=planet_texts(planet_render());
      const auto anchor=control(planet_render(),"Ancient Grey Crater");
      InputEvent wheel{InputEventType::Wheel,anchor,{},-10.f};
      (void)planet_index.handle(wheel,w,h,frame);
      check(planet_texts(planet_render())!=planet_top,"Wheel did not scroll the planet index.");
      InputEvent up{InputEventType::Wheel,anchor,{},10.f};
      (void)planet_index.handle(up,w,h,frame);
      check(planet_texts(planet_render())==planet_top,"Planet index scroll did not return to the head.");
    }
    const auto before_planets=saved();planet_click(control(planet_render(),"Ancient Grey Crater"));
    const auto& indexed_world=frame.runtime().world().campaign();
    std::size_t imported_count=0,habitable_count=0;
    for(const auto& entry:build_developer_planet_index(indexed_world,DeveloperPlanetFilter::ImportedArtwork))if(entry.example){
      check(!entry.example->appearance->source_asset_id.empty()&&!entry.example->appearance->source_asset_id.starts_with("sol:"),"Imported filter selected a fallback or Sol");imported_count+=entry.count;
    }
    const auto player=std::ranges::find(indexed_world.civilizations,indexed_world.player_civilization_id,&Civilization::id);
    for(const auto& entry:build_developer_planet_index(indexed_world,DeveloperPlanetFilter::Habitable))if(entry.example){
      check(assess_species_planet(species_environment_profile(player->species_id),*entry.example).naturally_colonizable,"Habitable index bypassed species rules");habitable_count+=entry.count;
    }
    check(imported_count>0&&habitable_count>0,"Campaign artwork or habitable worlds cannot be found");
    (void)control(planet_render(),"Source:");check(saved()==before_planets,"Planet inspection changed state");
    planet_click(control(planet_render(),"VIEW RULES"));
    const auto rules_draw=planet_render();for(const auto label:{"Base class:","Subclass:","Orbital zones:","Surface temperature:","Atmosphere:","Pressure:","Water allowed:","Volcanism allowed:","Within class:","Image pools:"})(void)control(rules_draw,label);
    for(const auto& command:rules_draw.overlay)if(const auto* text=std::get_if<Text>(&command);text&&text->clip){const auto& r=*text->clip;check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Planet rules escaped viewport");}
    check(saved()==before_planets,"Reading planet registry mutated campaign");planet_click(control(rules_draw,"VIEW EXAMPLE"));(void)control(planet_render(),"Source:");
    const auto count=frame.runtime().world().campaign().bodies.size();planet_click(control(planet_render(),"GENERATE SUBCLASS"));
    check(frame.runtime().world().campaign().bodies.size()==count+1,"Planet subclass control failed to create its example");
    const auto created=frame.runtime().world().campaign().bodies.back();check(created.appearance&&created.appearance->subclass=="ancient-grey-crater","Planet control generated wrong subtype");
    check(planet_appearance_contradictions(created).empty(),"Developer control bypassed physical eligibility");
    planet_click(control(planet_render(),"GO TO EXAMPLE"));const auto planet_target=planet_index.take_focus_request();
    check(planet_target&&planet_target->second==created.id&&!planet_index.visible(),"Planet navigation did not target the newly created body");
    // Keyboard ring: the panel's rendered controls walk in (y,x) order —
    // header actions, class filter, rendered rows, then the
    // example/generate footer — and activation replays the same dispatch
    // pointer press+release takes, including the canonical
    // force_developer_planet_type command and the map focus request.
    {
      planet_index.open(frame.runtime().world().campaign());
      const auto planet_press=[&](std::uint32_t key,bool shift=false){
        InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;ev.shift=shift;
        return planet_index.handle(ev,w,h,frame);};
      constexpr std::uint32_t kTab=9u,kReturn=13u,kDown=0x40000051u;
      check(planet_index.focus()<0,"Reopened planet index retained keyboard focus.");
      check(planet_press(kTab)&&planet_index.focus()>=0,"Tab did not enter the planet index ring.");
      check(planet_index.focused_label(w,h)=="Giant and ring test panel","First planet-index target is not the giant panel.");
      check(planet_index.focused_bounds(w,h).has_value(),"Focused planet-index control lacks bounds.");
      int planet_guard=0;
      while(planet_index.focused_label(w,h).find("Ancient Grey Crater")!=0&&planet_guard++<64)
        check(planet_press(kDown),"Planet-index row navigation leaked.");
      check(planet_press(kReturn),"Planet row activation leaked.");
      while(planet_index.focused_label(w,h).find("Generate subclass")!=0&&planet_guard++<128)
        check(planet_press(kDown),"Planet-index footer navigation leaked.");
      const auto bodies_before=frame.runtime().world().campaign().bodies.size();
      check(planet_press(kReturn),"Subclass activation leaked.");
      check(frame.runtime().world().campaign().bodies.size()==bodies_before+1,"Keyboard subclass generation did not run the canonical command.");
      while(planet_index.focused_label(w,h).find("Go to example")!=0&&planet_guard++<192)
        check(planet_press(kDown),"Planet-index example navigation leaked.");
      check(planet_press(kReturn),"Example activation leaked.");
      const auto keyed_target=planet_index.take_focus_request();
      check(keyed_target&&!planet_index.visible(),"Keyboard GO TO EXAMPLE produced the wrong target.");
      planet_index.open(frame.runtime().world().campaign());
      check(planet_press(kTab)&&planet_index.focus()>=0,"Tab did not re-enter the planet index ring.");
      (void)planet_index.handle({InputEventType::LeftPressed,{w*.5f,h*.5f}},w,h,frame);
      check(planet_index.focus()<0,"Pointer press did not clear the planet index ring.");
      check(planet_press(kTab),"Planet-index Tab press leaked.");
      check(planet_index.handle({InputEventType::EscapePressed},w,h,frame)&&planet_index.focus()<0&&planet_index.visible(),
        "Escape closed the planet index instead of releasing its ring.");
      check(planet_index.handle({InputEventType::EscapePressed},w,h,frame)&&!planet_index.visible(),"Second Escape did not close the planet index.");
    }
    // Arrow scroll-follow: the ring covers the rendered slots only, so Down
    // on the last rendered row scrolls the list to reveal the next logical
    // row and Up on the first rendered row scrolls back — every row stays
    // keyboard-reachable without the pointer.
    {
      planet_index.open(frame.runtime().world().campaign());
      constexpr std::uint32_t kDown=0x40000051u,kUp=0x40000052u;
      const auto planet_key=[&](std::uint32_t key,bool shift=false){
        InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;ev.shift=shift;
        return planet_index.handle(ev,w,h,frame);};
      const auto planet_texts2=[&]{DrawList d;planet_index.render(d,w,h);std::vector<std::string> v;
        for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)v.push_back(t->value);
        std::ranges::sort(v);return v;};
      const auto is_row=[&](const std::string &label){
        return !label.empty()&&label!="Giant and ring test panel"&&label!="Close planet type index"
          &&label.find("Planet class filter")!=0&&label.find("Generate")!=0&&label.find("Go to example")!=0
          &&label!="View rules"&&label!="View example";};
      check(planet_key(9u)&&planet_index.focus()>=0,"Tab did not enter the planet index ring for scrolling.");
      int scroll_guard=0;
      while(!is_row(planet_index.focused_label(w,h))&&scroll_guard++<16)
        check(planet_key(kDown),"Planet-index header navigation leaked.");
      check(is_row(planet_index.focused_label(w,h)),"Planet-index ring never reached a rendered row.");
      const auto initial_texts=planet_texts2();
      scroll_guard=0;std::string scrolled_label;
      while(scroll_guard++<64){
        check(planet_key(kDown),"Planet-index scroll-follow leaked.");
        const auto label=planet_index.focused_label(w,h);
        if(!is_row(label))break;
        if(!std::ranges::contains(initial_texts,label)){scrolled_label=label;break;}
      }
      check(!scrolled_label.empty(),"Down on the last rendered planet row left the list instead of scrolling.");
      const auto scrolled_texts=planet_texts2();
      scroll_guard=0;bool scrolled_back=false;
      while(scroll_guard++<128){
        check(planet_key(kUp),"Planet-index upward navigation leaked.");
        if(planet_texts2()!=scrolled_texts){scrolled_back=true;break;}
        if(!is_row(planet_index.focused_label(w,h)))break;
      }
      check(scrolled_back,"Up on the first rendered planet row did not scroll the list back.");
      check(planet_index.handle({InputEventType::EscapePressed},w,h,frame)&&planet_index.focus()<0,"Planet index ring did not release.");
      planet_index.close();
    }
    // Stellar activity panel: the twenty-four command buttons ring in
    // (y,x) order and activation replays the same dispatch a pointer
    // press takes — CLOSE ends the panel through the shared path.
    {
      StellarActivityPanel activity;activity.open(frame,std::nullopt);
      check(activity.visible()&&activity.focus()<0,"Activity panel opened with stale focus.");
      const auto activity_press=[&](std::uint32_t key){InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;return activity.handle(ev,w,h,frame);};
      check(activity_press(9)&&activity.focus()>=0,"Tab did not enter the activity ring.");
      check(activity.focused_label(w,h)=="PROMINENCE","First activity target is not PROMINENCE.");
      check(activity.focused_bounds(w,h).has_value(),"Focused activity control lacks bounds.");
      check(activity.focused_control(w,h)==stellar::engine::AnnouncementControl::Button,"Activity button misclassified.");
      int activity_guard=0;
      while(activity.focused_label(w,h)!="CLOSE"&&activity_guard++<48)check(activity_press(9),"Activity navigation leaked.");
      check(activity_press(13)&&!activity.visible(),"Keyboard CLOSE did not close the activity panel.");
      activity.open(frame,std::nullopt);
      check(activity_press(9)&&activity.focus()>=0,"Tab did not re-enter the activity ring.");
      check(activity.handle({InputEventType::EscapePressed},w,h,frame)&&activity.focus()<0&&activity.visible(),"Escape closed the activity panel instead of releasing its ring.");
      check(activity.handle({InputEventType::EscapePressed},w,h,frame)&&!activity.visible(),"Second Escape did not close the activity panel.");
    }
    // Empire monitor: close, rendered rows, refresh and show-home ring in
    // (y,x) order; activation replays the press/release hit dispatch, so
    // keyboard SHOW HOME issues the same focus request the pointer takes.
    {
      NativeDeveloperEmpireMonitor empires;empires.open(frame);
      check(empires.visible()&&empires.focus()<0,"Empire monitor opened with stale focus.");
      const auto empires_press=[&](std::uint32_t key){InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;return empires.handle(ev,w,h,frame);};
      check(empires_press(9)&&empires.focus()>=0,"Tab did not enter the empire ring.");
      check(empires.focused_label(w,h)=="Close empire monitor","First empire target is not the close control.");
      check(empires.focused_bounds(w,h).has_value(),"Focused empire control lacks bounds.");
      check(empires.focused_control(w,h)==stellar::engine::AnnouncementControl::Button,"Empire monitor control misclassified.");
      int empires_guard=0;
      while(empires.focused_label(w,h)!="Show home system"&&empires_guard++<32)check(empires_press(9),"Empire navigation leaked.");
      check(empires.focused_label(w,h)=="Show home system","SHOW HOME SYSTEM never joined the empire ring.");
      check(empires_press(13)&&!empires.visible()&&empires.take_focus_request().has_value(),"Keyboard SHOW HOME did not issue a focus request.");
      empires.open(frame);
      check(empires_press(9)&&empires.focus()>=0,"Tab did not re-enter the empire ring.");
      check(empires.handle({InputEventType::EscapePressed},w,h,frame)&&empires.focus()<0&&empires.visible(),"Escape closed the empire monitor instead of releasing its ring.");
      check(empires.handle({InputEventType::EscapePressed},w,h,frame)&&!empires.visible(),"Second Escape did not close the empire monitor.");
    }
    // Empire monitor edge scroll: with more empires than the twelve
    // rendered slots, Down on the bottom row scrolls the content window so
    // every empire is reachable — wrap only happens at the true last row.
    {
      auto many=seed_persistable_fresh_campaign(-9142051,load_nearby_catalog(argv[1]),
          {"2050-03-21T00:00:00Z",250,13,3,"terran_baseline",StellarPopulationOptions{},true});
      auto many_runtime=IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(many));
      CampaignFrame many_frame(std::move(many_runtime),StrategicClock{},CampaignFramePolicy::Developer);
      NativeDeveloperEmpireMonitor empires;empires.open(many_frame);
      check(stellar::core::developer_empire_summaries(many_frame.runtime()).size()>12,"The many-empire fixture did not overflow the monitor's rendered window.");
      const auto press=[&](std::uint32_t key){InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;return empires.handle(ev,w,h,many_frame);};
      check(press(9),"Tab did not enter the many-empire ring.");
      std::unordered_set<std::string> seen;
      std::string top_row_label;
      bool wrapped=false;
      for(int i=0;i<64;++i){
        check(press(0x40000051u),"Empire edge-scroll navigation leaked.");
        const auto label=empires.focused_label(w,h);
        if(label=="Close empire monitor"&&!seen.empty()){wrapped=true;break;}
        if(top_row_label.empty()&&label!="Close empire monitor")
          top_row_label=label;
        seen.insert(label);
      }
      check(wrapped,"Down never wrapped back to the ring's top.");
      // Twelve slots render at once — distinct row labels past that count
      // prove Down edge-scrolled the content window.
      seen.erase("Show home system");
      seen.erase("Refresh empire monitor");
      check(seen.size()>12,"Down did not edge-scroll the empire list past its rendered window.");
      // Up walks back: the top row's label returns once the window is home.
      bool returned=false;
      for(int i=0;i<64;++i){
        check(press(0x40000052u),"Empire edge-scroll Up navigation leaked.");
        if(empires.focused_label(w,h)==top_row_label){returned=true;break;}
      }
      check(returned,"Up did not scroll the empire list back to its first row.");
    }
    // Simulation panel: the sixteen rendered buttons ring in (y,x) order
    // and activation replays the same dispatch a matched press/release
    // takes — the 25× speed lands through set_developer_speed unchanged.
    {
      NativeDeveloperSimulationPanel simulation;simulation.toggle();
      check(simulation.visible()&&simulation.focus()<0,"Simulation simulation opened with stale focus.");
      const auto sim_press=[&](std::uint32_t key){InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;return simulation.handle(ev,w,h,frame);};
      check(sim_press(9)&&simulation.focus()>=0,"Tab did not enter the simulation panel ring.");
      check(simulation.focused_label(w,h,frame)=="1×","First simulation target is not the 1× speed.");
      check(simulation.focused_bounds(w,h,frame).has_value(),"Focused simulation control lacks bounds.");
      check(simulation.focused_control(w,h,frame)==stellar::engine::AnnouncementControl::Button,"Simulation control misclassified.");
      while(simulation.focused_label(w,h,frame)!="25×")check(sim_press(9),"Simulation navigation leaked.");
      check(sim_press(13)&&frame.runtime().world().campaign().developer_provenance->simulation.speed==25,"Keyboard speed activation did not reach set_developer_speed.");
      int sim_guard=0;
      while(simulation.focused_label(w,h,frame)!="CLOSE"&&sim_guard++<20)check(sim_press(9),"Simulation navigation leaked.");
      check(sim_press(13)&&!simulation.visible(),"Keyboard CLOSE did not close the simulation panel.");
      simulation.toggle();
      check(sim_press(9)&&simulation.focus()>=0,"Tab did not re-enter the simulation ring.");
      check(simulation.handle({InputEventType::EscapePressed},w,h,frame)&&simulation.focus()<0&&simulation.visible(),"Escape closed the simulation panel instead of releasing its ring.");
      check(simulation.handle({InputEventType::EscapePressed},w,h,frame)&&!simulation.visible(),"Second Escape did not close the simulation panel.");
    }
    // Running the authoritative campaign after appending a planet exercises
    // borrowed simulation views and ensures no stale vector pointers survive.
    (void)frame.runtime().advance(.25,.25);
    auto observed=frame.runtime().world().campaign();
    check(!stellar::native_stellar::observed_central_artwork(observed,observed.player_civilization_id),"Developer state command revealed central artwork.");
    observed.knowledge.unlock_galactic_core_access(observed.player_civilization_id);
    check(!stellar::native_stellar::observed_central_artwork(observed,observed.player_civilization_id),"Unlock revealed unexplored central artwork.");
    observed.knowledge.record_galactic_core_exploration(observed.player_civilization_id);
    for(const auto &[state,asset]:std::array<std::pair<CentralBlackHoleState,std::string_view>,3>{{
        {CentralBlackHoleState::Quiescent,"central-supermassive-black-hole"},
        {CentralBlackHoleState::Accreting,"accreting-black-hole"},
        {CentralBlackHoleState::RelativisticJets,"jet-black-hole"}}}){
      observed.galactic_core->black_hole=central_black_hole_with_state(*observed.galactic_core->black_hole,state);
      check(stellar::native_stellar::observed_central_artwork(observed,observed.player_civilization_id)==asset,"Discovered central state did not select matching artwork.");
      check(!stellar::native_stellar::observed_central_artwork(observed,9999),"Artwork leaked to a different observer.");
    }
  }
  std::cout<<"native developer celestial index tests passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
