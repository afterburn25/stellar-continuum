#include "native_planetary_screen.hpp"
#include "native_planet_surface_assets.hpp"
#include <stellar/engine/native_triangle_mesh.hpp>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace stellar::native_colony_ui;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
  if(argc==2&&std::string_view(argv[1])=="--benchmark"){
    NativePlanetaryScreen screen;NativeColonyView view;
    view.campaign_generation=1;view.body_id=3;view.planet.details.emplace();
    view.planet.details->pressure_kpa=101;view.planet.sol_texture_key="earth";
    view.infrastructure=1;view.population_millions=1;
    const auto map=RgbaImage::create(1,1,{180,200,230,255});
    screen.set_globe_maps([&](std::string_view,int){return map;});screen.set_view(view);
    const auto area=PlanetaryLayout::make(1920,1080).globe;
    constexpr int frames=1000;std::size_t commands=0;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<frames;++i){DrawList draw;screen.globe().render(draw,area,view,0,14);commands+=draw.overlay.size();}
    std::cout<<"globe_prepare frames="<<frames<<" mean_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames<<" commands="<<commands<<'\n';return 0;
  }
  // Real packaged maps must reach the immutable 3D material without recolouring
  // or Earth-only dispatch. Changing discovery/key must invalidate a bound globe.
  check(argc==2,"Expected the Sol asset directory");
  const auto annulus=annulus_mesh(1.3f,2.3f);
  check(!intersect_mesh_segment(*annulus,{0,1,0},{0,-1,0}),"Ring hole is filled");
  check(intersect_mesh_segment(*annulus,{1.8,1,0},{1.8,-1,0}).has_value(),"Ring annulus cannot be intersected");
  bool invalid=false;try{(void)annulus_mesh(2,1);}catch(const std::invalid_argument&){invalid=true;}check(invalid,"Inverted ring radii accepted");
  for(std::size_t i=0;i<annulus->indices().size();i+=3){const auto& a=annulus->vertices()[annulus->indices()[i]].position;const auto& b=annulus->vertices()[annulus->indices()[i+1]].position;const auto& c=annulus->vertices()[annulus->indices()[i+2]].position;check((b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z)>0,"Ring winding disagrees with normal");}

  {
    using namespace stellar::native_system_ui;
    NativePlanetaryScreen screen;NativeColonyView v;v.campaign_generation=11;v.body_id=4;
    v.planet.details.emplace();v.planet.visual_class=stellar::native_system::NativeSystemBodyVisualClass::rocky;v.solid_surface=true;
    std::array<NativePlanetGlobe::Picture,planet_surface_assets.size()> maps;
    int calls=0;
    screen.set_globe_maps([&](std::string_view key,int layer)->NativePlanetGlobe::Picture{
      check(layer!=2,"Globe still requests an extra cloud layer");++calls;const auto index=planet_surface_asset_index(key,layer);if(!index)return {};
      auto& map=maps[*index];if(!map)map=NativePlanetGlobe::prepare_map(decode_rgba_image(std::filesystem::path(argv[1])/planet_surface_assets[*index].filename),layer);return map;
    });
    const auto material=[&]{DrawList draw;screen.render(draw,v,1920,1080);for(const auto& item:draw.overlay)
      if(const auto* s=std::get_if<Scene3DView>(&item);s&&s->scene){const auto rings=v.planet.details?planet_ring_bands(v.planet.sol_texture_key.value_or("")):std::span<const PlanetRingBand>{};check(s->scene->instances().size()==1+rings.size(),"Authored planet has missing rings or inherited extra layers");check(s->scene->instances()[0].mesh->bounding_radius()<1.00001f,"Authored colour map became invented terrain");return s->scene->instances()[0].material.texture;}
      throw std::runtime_error("Authored planet omitted its 3D scene");};
    for(const auto key:{"mercury","venus","moon","mars","jupiter","saturn","uranus","neptune","earth"}){
      v.planet.sol_texture_key=key;screen.set_view(v);const auto index=*planet_surface_asset_index(key,0);
      const auto original=decode_rgba_image(std::filesystem::path(argv[1])/planet_surface_assets[index].filename);
      check(maps[index]&&material()==maps[index],"Globe did not consume the requested canonical map");
      check(material()->width()==1774&&material()->height()==887&&material()->pixels()==original->pixels(),"Authored native map lost resolution or pixels");
      // Tilted ringed worlds must keep rendered positions, survey projection,
      // and picking aligned. Annuli are immutable and separate from the globe.
      DrawList posed;screen.render(posed,v,1920,1080);
      for(const auto& command:posed.overlay)if(const auto* scene=std::get_if<Scene3DView>(&command)){
        const auto& body=scene->scene->instances()[0];const auto area=PlanetaryLayout::make(1920,1080).globe;
        const auto prepared=prepare_instance3d(scene->scene->camera(),body,area.width/area.height);
        const float lon=screen.globe().rotation(),lat=.15f;
        const auto p=screen.globe().project(lon,lat,area);check(p.has_value(),"Front survey point hidden");
        const auto clip=transform(prepared.model_view_projection,{std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon),1});
        check(std::abs(p->x-(area.x+(clip[0]/clip[3]+1)*area.width*.5f))<.02f&&std::abs(p->y-(area.y+(1-clip[1]/clip[3])*area.height*.5f))<.02f,"Tilted artwork and survey projection diverged");
        check(screen.globe().hit(*p,area).has_value(),"Tilted planet cannot be picked");
        for(std::size_t i=1;i<scene->scene->instances().size();++i){const auto& ring=scene->scene->instances()[i];check(ring.material.double_sided&&ring.mesh==planet_ring_instances(key)[i-1].mesh,"Ring geometry was rebuilt or is one-sided");}
      }
      const auto count=calls;screen.set_view(v);check(calls==count,"Unchanged planet reloaded its maps");
      const auto area=PlanetaryLayout::make(1920,1080).globe;const auto center=screen.globe().center(area);
      const auto initial_rotation=screen.globe().rotation();
      (void)screen.globe().handle({InputEventType::LeftPressed,center},area);
      (void)screen.globe().handle({InputEventType::PointerMove,{center.x+520,center.y}},area);
      (void)screen.globe().handle({InputEventType::LeftReleased,{center.x+520,center.y}},area);
      check(std::abs(screen.globe().rotation()-initial_rotation)>3.f&&material()==maps[index],"Rotation replaced the authored surface");
    }
    const auto count=calls;v.planet.details.reset();screen.set_view(v);
    check(calls==count&&screen.globe().regions().empty()&&material()!=maps[*planet_surface_asset_index("earth",0)],"Hidden world retained authored details");
    v.planet.details.emplace();v.planet.sol_texture_key="earth";v.planet.details->pressure_kpa=101;v.population_millions=1;v.infrastructure=1;screen.set_view(v);
    DrawList earth;screen.render(earth,v,1920,1080);bool found=false;for(const auto& item:earth.overlay)if(const auto* s=std::get_if<Scene3DView>(&item);s&&s->scene){found=true;check(s->scene->instances().size()==2&&s->scene->instances()[0].material.texture==maps[0],"Earth must retain its colour and night layers without added clouds");}check(found,"Earth scene absent");
    check(!planet_surface_asset_index("../jupiter",0)&&!planet_surface_asset_index("mars",1)&&!planet_surface_asset_index("mars",-1),"Map resolver accepted an unsupported path/layer");
  }
  for(const auto [w,h]:std::array<std::pair<int,int>,5>{{{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}}}){
    NativePlanetaryScreen screen;NativeColonyView view;
    view.body_id=3;view.planet.details.emplace();view.solid_surface=true;view.campaign_generation=1;view.colony_id=7;view.building_capacity=32;view.surface_hub_level=2;view.body_display_name="Earth";
    screen.set_view(view);DrawList draw;screen.render(draw,view,w,h);
    const auto layout=PlanetaryLayout::make(w,h);const auto chrome=NativeUiLayout::for_viewport(w,h);
    check(layout.screen.x>chrome.inspect.x+chrome.inspect.width,"Planetary screen overlaps navigation");
    check(layout.screen.y>=chrome.navigation_bar.y+chrome.navigation_bar.height,"Planetary screen overlaps navigation header");
    for(const auto& r:{layout.facts,layout.slots,layout.details,layout.queue})
      check(r.width>100&&r.height>40&&r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Responsive panel has no usable bounds");
    const auto click=[&](Point p){(void)screen.handle({InputEventType::LeftPressed,p},w,h);return screen.handle({InputEventType::LeftReleased,p},w,h);};
    bool has_mesh=false;for(const auto& item:draw.overlay)if(const auto* scene=std::get_if<Scene3DView>(&item);scene&&scene->scene){has_mesh=!scene->scene->instances().empty()&&scene->scene->instances().front().material.texture!=nullptr;}
    check(has_mesh,"Planet is not a rendered textured globe");
    const Scene3DView* first_scene=nullptr;for(const auto& item:draw.overlay)if(const auto* scene=std::get_if<Scene3DView>(&item))first_scene=scene;
    check(first_scene!=nullptr,"Planet did not submit its 3D viewport");
    const auto geometry=first_scene->scene->instances().front().mesh;
    check(geometry->bounding_radius()>1.f,"Generated solid world did not acquire terrain geometry");
    for(const auto lon:std::array{-.1f,.25f,.7f})for(const auto lat:std::array{-.2f,.2f,.6f}){
      const auto projected=screen.globe().project(lon,lat,layout.globe);check(projected.has_value(),"Test province is not visible");
      const auto prepared=prepare_instance3d(first_scene->scene->camera(),first_scene->scene->instances().front(),layout.globe.width/layout.globe.height);
      const float r=1+screen.globe().surface_elevation(lon,lat);
      const auto clip=transform(prepared.model_view_projection,{r*std::cos(lat)*std::sin(lon),r*std::sin(lat),r*std::cos(lat)*std::cos(lon),1});
      const Point pixel{layout.globe.x+(clip[0]/clip[3]+1)*layout.globe.width*.5f,layout.globe.y+(1-clip[1]/clip[3])*layout.globe.height*.5f};
      check(std::abs(pixel.x-projected->x)<.01f&&std::abs(pixel.y-projected->y)<.01f,"3D globe does not align with region projection and picking");
      check(screen.globe().hit(*projected,layout.globe).has_value(),"Rendered terrain could not be picked");
    }
    DrawList repeated;screen.render(repeated,view,w,h);
    for(const auto& item:repeated.overlay)if(const auto* scene=std::get_if<Scene3DView>(&item))check(scene->scene->instances().front().mesh==geometry,"Globe rebuilt its immutable mesh");
    const auto center=screen.globe().center(layout.globe);click(center);check(screen.globe().selected()>=0,"Globe region selection failed");
    check(screen.handle({InputEventType::EscapePressed},w,h).action==PlanetaryAction::None&&screen.globe().selected()<0,"Region back did not return to planet");
    const auto yaw=screen.globe().rotation();(void)screen.handle({InputEventType::LeftPressed,center},w,h);(void)screen.handle({InputEventType::PointerMove,{center.x+40,center.y+10}},w,h);(void)screen.handle({InputEventType::LeftReleased,{center.x+40,center.y+10}},w,h);check(screen.globe().rotation()!=yaw&&screen.globe().selected()<0,"Drag selected region or failed rotation");
    (void)screen.handle({.type=InputEventType::Wheel,.position=center,.wheel_y=2},w,h);check(screen.globe().zoom()>1,"Globe wheel zoom failed");screen.globe().whole();
    click({layout.actions.x+20,layout.actions.y+10});draw={};screen.render(draw,view,w,h);
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
    screen.complete("Cancelled");++view.body_id;screen.set_view(view);
    check(screen.selected_slot()==-1&&!screen.modal(),"Changing planet retained previous selection or quote");
    check(screen.handle({InputEventType::LeftReleased,slot},w,h).action==PlanetaryAction::None,"Changing planet left stale hit targets");
  }
  {
    // Keyboard-focus contract: the ring walks the render-registered hit
    // registry in (y,x) order, Escape releases it before Back, pointer
    // presses reset it, Return/Space replays the same dispatch a matched
    // press+release takes, and the confirmation modal narrows the ring to
    // its own controls.
    NativePlanetaryScreen screen;NativeColonyView view;
    view.body_id=3;view.planet.details.emplace();view.solid_surface=true;
    view.campaign_generation=1;view.colony_id=7;view.building_capacity=32;
    view.surface_hub_level=2;view.body_display_name="Earth";
    screen.set_view(view);
    const int w=1920,h=1080;
    DrawList draw;screen.render(draw,view,w,h);
    const auto key=[&](std::uint32_t k,bool shift=false){
      InputEvent e{};e.type=InputEventType::KeyPressed;e.key=k;e.shift=shift;
      return screen.handle(e,w,h);};
    constexpr std::uint32_t kTab=9u,kReturn=13u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const auto escape=[&]{return screen.handle({InputEventType::EscapePressed},w,h);};
    check(screen.focus()<0,"Planet ring present before any key");
    check(screen.focused_label().empty()&&!screen.focused_bounds().has_value(),
          "Unfocused planet returned a label or bounds");
    check(key(kTab).action==PlanetaryAction::None&&screen.focus()==0,
          "Tab did not arm the planet ring");
    check(!screen.focused_label().empty()&&screen.focused_bounds().has_value(),
          "Focused planet control has no label or bounds");
    check(screen.focused_control()==stellar::engine::AnnouncementControl::Button,
          "Planet control did not classify as a button");
    check(key(kEnd).action==PlanetaryAction::None&&screen.focus()>0,
          "End did not reach the planet ring tail");
    check(key(kHome).action==PlanetaryAction::None&&screen.focus()==0,
          "Home did not return to the planet ring head");
    check(escape().action==PlanetaryAction::None&&screen.focus()<0,
          "Escape did not release the planet ring");
    check(escape().action==PlanetaryAction::Back,
          "Escape after release did not emit Back");
    // Keyboard activation replays the pointer dispatch: walk the ring to
    // the Save button and confirm it emits Save, not a tab/selection side
    // effect.
    (void)key(kTab);bool found_save=false;
    for(int i=0;i<200&&screen.focus()>=0;++i){
      if(screen.focused_label()=="Save"){found_save=true;break;}
      (void)key(kTab);
    }
    check(found_save,"Save control missing from the planet ring");
    check(key(kReturn).action==PlanetaryAction::Save,
          "Return did not replay the Save dispatch");
    // A pointer press clears the ring; the modal narrows it to Cancel and
    // Confirm only.
    (void)screen.handle({InputEventType::LeftPressed,{10,10}},w,h);
    check(screen.focus()<0,"Pointer press kept the planet ring");
    NativeSurfacePlacementQuote quote;quote.accepted=true;
    quote.building_name="Science lab";quote.message="Authorize construction";
    screen.set_confirmation(quote);draw={};screen.render(draw,view,w,h);
    (void)key(kTab);check(screen.focused_label()=="Cancel",
                          "Modal ring did not start at Cancel");
    (void)key(kEnd);check(screen.focused_label()=="Confirm",
                          "Modal ring tail is not Confirm");
    check(key(kReturn).action==PlanetaryAction::Confirm,
          "Return did not replay the Confirm dispatch");
    screen.complete("Done");
  }
  {
    // A deficit colony exposes the vitals strip plus issue chips that carry
    // the real gap; activating a chip routes into the economy tab.
    NativePlanetaryScreen screen;NativeColonyView view;
    view.body_id=3;view.planet.details.emplace();view.solid_surface=true;
    view.campaign_generation=1;view.colony_id=7;view.building_capacity=32;
    view.surface_hub_level=2;view.body_display_name="Earth";
    view.population_millions=9500;view.stability=.62;view.power_supply=10;
    view.power_demand=40;view.food_reserve_days=5;view.employment_rate=.77;
    view.workforce_demand_millions=120;view.workforce_available_millions=100;
    screen.set_view(view);
    const int w=1600,h=900;
    DrawList draw;screen.render(draw,view,w,h);
    const Text* chip=nullptr;bool vitals=false;
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item)){
      if(t->value=="POWER -30")chip=t;
      vitals|=t->value=="POPULATION";
    }
    check(vitals,"Owned colony omitted the vitals strip");
    check(chip,"Deficit colony did not expose a POWER issue chip");
    (void)screen.handle({InputEventType::LeftPressed,chip->at},w,h);
    (void)screen.handle({InputEventType::LeftReleased,chip->at},w,h);
    draw={};screen.render(draw,view,w,h);
    bool economy=false;
    for(const auto& item:draw.overlay)
      if(const auto* t=std::get_if<Text>(&item);t&&t->value=="PRODUCTION & REQUIREMENTS")economy=true;
    check(economy,"Issue chip did not activate the economy tab");
    // With an unpowered structure present the chip routes straight to it and
    // hover lists the affected facilities.
    NativeSurfaceSite dark;dark.name="Ore Refinery";dark.slot_index=4;
    dark.complete=true;dark.enabled=true;dark.staffed=true;dark.condition=1.;
    dark.efficiency=1.;
    view.construction_sites={dark};screen.set_view(view);
    draw={};screen.render(draw,view,w,h);
    chip=nullptr;for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value=="POWER -30")chip=t;
    check(chip,"POWER chip disappeared once a structure was attached");
    (void)screen.handle({InputEventType::PointerMove,chip->at},w,h);
    draw={};screen.render(draw,view,w,h);
    bool detail=false,heading=false;
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item)){detail|=t->value=="Ore Refinery";heading|=t->value=="Affected structures";}
    check(detail&&heading,"Chip hover did not list the affected structure");
    (void)screen.handle({InputEventType::LeftPressed,chip->at},w,h);
    (void)screen.handle({InputEventType::LeftReleased,chip->at},w,h);
    draw={};screen.render(draw,view,w,h);
    bool manage=false;
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item);t&&t->value=="Disable building")manage=true;
    check(manage,"Issue chip did not select the affected structure");
  }
  for (const auto [w,h] : std::array<std::pair<int,int>,2>{{{1280,720},{1920,1080}}}) {
    NativePlanetaryScreen developer; NativeColonyView v;
    v.campaign_generation=10;v.body_id=3;v.developer_inspection=true;v.foreign_settlement=true;
    v.owner_name="Alien Empire";v.population_millions=4321.;v.body_display_name="Alien Home";
    v.planet.visual_class=stellar::native_system::NativeSystemBodyVisualClass::rocky;
    v.planet.world_class=stellar::core::PlanetaryWorldClass::Continental;
    v.planet.details.emplace();v.hub_upgrade_available=true;v.can_afford_hub_upgrade=true;
    developer.set_view(v);DrawList draw;developer.render(draw,v,w,h);
    const Text* type=nullptr;const Text* world_class=nullptr;bool population=false,owner=false;
    for(const auto& item:draw.overlay) if(const auto* t=std::get_if<Text>(&item)) {
      if(t->value=="Rocky world"&&!type)type=t;
      if(t->value=="Continental"&&!world_class)world_class=t;
      population|=t->value=="4.32 B";owner|=t->value.find("Alien Empire")!=std::string::npos;
      check(t->value.find("not available to this observer")==std::string::npos,"Developer colony still shows withheld statistics");
    }
    check(population&&owner,"Alien population or ownership is absent from planetary screen");
    check(type&&world_class&&world_class->at.y>type->at.y&&world_class->at.x==type->at.x,"World class does not sit below physical type");
    const auto l=PlanetaryLayout::make(w,h);Point p{l.command.x+10,l.command.y+10};
    (void)developer.handle({InputEventType::LeftPressed,p},w,h);
    check(developer.handle({InputEventType::LeftReleased,p},w,h).action==PlanetaryAction::None,"Foreign inspection granted a construction command");
    v.observer_only=true;v.foreign_settlement=false;v.colony_id=0;v.body_id=9;developer.set_view(v);draw={};developer.render(draw,v,w,h);
    bool empty=false;for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item))empty|=t->value.find("Population 0")!=std::string::npos;
    check(empty,"Developer unsettled world does not state that it has zero colony population");
  }
  {
    // A disabled command-center button surfaces the authoritative lock reason
    // as a hover tooltip at the point of interaction.
    NativePlanetaryScreen screen;NativeColonyView v;
    v.campaign_generation=12;v.body_id=5;v.body_display_name="Held World";
    v.planet.visual_class=stellar::native_system::NativeSystemBodyVisualClass::rocky;
    v.planet.details.emplace();
    v.hub_upgrade_available=false;v.can_afford_hub_upgrade=false;
    v.hub_upgrade_lock_reason="Requires planetary shield coverage.";
    screen.set_view(v);
    const int w=1280,h=720;const auto l=PlanetaryLayout::make(w,h);
    (void)screen.handle({InputEventType::PointerMove,{l.command.x+10,l.command.y+10}},w,h);
    DrawList draw;screen.render(draw,v,w,h);
    bool reason=false,title=false;
    for(const auto& item:draw.overlay)if(const auto* t=std::get_if<Text>(&item)){reason|=t->value=="Requires planetary shield coverage.";title|=t->value=="Command Center unavailable";}
    check(reason&&title,"Disabled command center did not explain the blocker on hover");
  }
  NativePlanetaryScreen observer;NativeColonyView secret;secret.campaign_generation=9;secret.body_id=8;secret.observer_only=true;secret.body_display_name="Unknown";observer.set_view(secret);DrawList hidden;observer.render(hidden,secret,1920,1080);
  check(observer.globe().regions().empty(),"Unsurveyed planet leaked regions");
  secret.planet.details.emplace();secret.planet.visual_class=stellar::native_system::NativeSystemBodyVisualClass::gas_giant;observer.set_view(secret);
  check(observer.globe().regions().size()==12&&observer.globe().regions()[0].terrain=="Atmospheric band","Discovery refresh or gas type failed");
  hidden={};observer.render(hidden,secret,1920,1080);for(const auto& item:hidden.overlay)if(const auto* t=std::get_if<Text>(&item))check(t->value!="Available slot"&&t->value!="Begin construction"&&t->value!="Population","Observer leaked colony management");
  for(const auto& item:hidden.overlay)if(const auto* scene=std::get_if<Scene3DView>(&item))check(scene->scene->instances()[0].mesh->bounding_radius()<1.00001f,"Gas planet acquired solid terrain");
  std::cout<<"Planetary layout and mouse/modal routing passed at 720p, 1080p, 1440p and 4K.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
