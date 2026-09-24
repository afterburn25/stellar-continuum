#pragma once
#include "native_dropdown.hpp"
#include <stellar/core/developer_planet_index.hpp>
#include <stellar/core/campaign_frame.hpp>
#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <utility>
namespace stellar::native_map {
class NativeDeveloperPlanetIndex {
 using Entry=stellar::core::DeveloperPlanetTypeEntry;
 std::vector<Entry> entries_;std::vector<int> rows_;stellar::native_ui::Dropdown dropdown_;
 bool visible_{},rules_{},giant_request_{};int selected_{-1},filter_{-1},pressed_{-1},ring_{-1};Point pointer_{};std::string notice_;
 mutable stellar::engine::VirtualizedList list_view_{};
 std::optional<std::pair<int,int>> focus_;
 struct Layout{UiRect panel,close,filter,list,details,go,force_class,force_sub,rules,giants;float scale;};
 static Layout layout(int w,int h){const float s=std::clamp(std::min(w/1280.f,h/720.f),.7f,2.f);Layout l{};l.scale=s;l.panel={w*.5f-545*s,h*.5f-300*s,1090*s,600*s};const auto p=l.panel;
  l.giants={p.x+478*s,p.y+14*s,300*s,32*s};l.close={p.x+974*s,p.y+14*s,96*s,32*s};l.rules={p.x+794*s,p.y+14*s,164*s,32*s};l.filter={p.x+18*s,p.y+54*s,440*s,36*s};l.list={p.x+18*s,p.y+106*s,440*s,392*s};l.details={p.x+478*s,p.y+60*s,590*s,442*s};l.go={p.x+18*s,p.y+516*s,230*s,36*s};l.force_class={p.x+266*s,p.y+516*s,344*s,36*s};l.force_sub={p.x+628*s,p.y+516*s,440*s,36*s};return l;}
 static UiRect row(const Layout& l,int i){return {l.list.x,l.list.y+i*49*l.scale,l.list.width,45*l.scale};}
 stellar::core::DeveloperPlanetFilter mode()const{return filter_==16?stellar::core::DeveloperPlanetFilter::ImportedArtwork:filter_==17?stellar::core::DeveloperPlanetFilter::Habitable:stellar::core::DeveloperPlanetFilter::All;}
 void rebuild(){rows_.clear();for(std::size_t i=0;i<entries_.size();++i)if(filter_<0||(filter_>=16&&entries_[i].count>0)||static_cast<int>(entries_[i].type)==filter_)rows_.push_back(static_cast<int>(i));}
 // The engine VirtualizedList owns the scroll offset — synced per
 // call so a rebuild that shrinks rows can never leave a stale offset
 // past the tail; the panel scrolls whole rows.
 int first_row(const Layout& l)const{return static_cast<int>(list_view_.sync_rows(rows_.size(),49*l.scale,l.list.height));}
 static std::string number(double v,int precision=2){std::ostringstream o;o<<std::fixed<<std::setprecision(precision)<<v;return o.str();}
 struct FocusTarget{
  enum class Kind{Close,Rules,Giants,Filter,Row,Go,ForceClass,ForceSub};
  UiRect rect;Kind kind{};int row{-1};std::string label;
  stellar::engine::AnnouncementControl control{stellar::engine::AnnouncementControl::Button};
 };
 std::vector<FocusTarget> focusables(const Layout& l)const{
  std::vector<FocusTarget> out;
  const auto push=[&](UiRect r,FocusTarget::Kind kind,std::string label,int row=-1){
   if(r.width>0&&r.height>0)out.push_back({r,kind,row,std::move(label)});};
  push(l.giants,FocusTarget::Kind::Giants,"Giant and ring test panel");
  if(selected_>=0)push(l.rules,FocusTarget::Kind::Rules,rules_?"View example":"View rules");
  push(l.close,FocusTarget::Kind::Close,"Close planet type index");
  push(l.filter,FocusTarget::Kind::Filter,std::string("Planet class filter · ")+(filter_<0?"All planet classes":filter_==16?"Imported artwork in this campaign":filter_==17?"Habitable for your species (unsettled)":stellar::core::planet_class_definition(static_cast<stellar::core::PlanetClass>(filter_)).name));
  const int first=first_row(l);
  for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i)
   push(row(l,i),FocusTarget::Kind::Row,entries_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(first+i)])].name,first+i);
  if(selected_>=0){
   const auto& selected=entries_[static_cast<std::size_t>(selected_)];
   if(selected.example)push(l.go,FocusTarget::Kind::Go,"Go to example · "+selected.name);
   push(l.force_class,FocusTarget::Kind::ForceClass,"Generate class · "+selected.name);
   push(l.force_sub,FocusTarget::Kind::ForceSub,"Generate subclass · "+selected.name);
  }
  std::ranges::sort(out,[](const FocusTarget&a,const FocusTarget&b){return a.rect.y==b.rect.y?a.rect.x<b.rect.x:a.rect.y<b.rect.y;});
  return out;
 }
 void open_filter_dropdown(){
  std::vector<std::string> options{"All planet classes"};for(const auto& d:stellar::core::planet_class_definitions())options.push_back(d.name+" · "+number(d.weight,1)+"%");options.push_back("Imported artwork in this campaign");options.push_back("Habitable for your species (unsettled)");dropdown_.open(0,std::move(options),filter_+1);
 }
 void force_type(bool subclass,stellar::core::CampaignFrame& frame){
  const auto selected=entries_[static_cast<std::size_t>(selected_)];
  try{auto& world=frame.runtime().world().campaign();
   const auto target=stellar::core::force_developer_planet_type(world,selected.type,subclass?selected.subclass:std::string{},selected.example?selected.example->system_id:-1,frame.clock().simulation_days());
   entries_=stellar::core::build_developer_planet_index(world,mode());notice_="Created a physically valid example. Select GO TO EXAMPLE to inspect it.";const auto b=std::ranges::find(world.bodies,target.second,&stellar::core::PlanetaryBody::id);
   if(b!=world.bodies.end())for(std::size_t i=0;i<entries_.size();++i)if(entries_[i].type==b->appearance->primary_class&&entries_[i].subclass==b->appearance->subclass){
    selected_=static_cast<int>(i);entries_[i].example=*b;const auto system=std::ranges::find(world.systems,target.first,&stellar::core::StellarSystem::id);if(system!=world.systems.end())entries_[i].system_name=system->name;break;
   }rebuild();}catch(const std::exception& error){notice_=error.what();}
 }
 void activate(const FocusTarget& t,stellar::core::CampaignFrame& frame){
  switch(t.kind){
   case FocusTarget::Kind::Close:close();break;
   case FocusTarget::Kind::Rules:if(selected_>=0)rules_=!rules_;break;
   case FocusTarget::Kind::Giants:giant_request_=true;close();break;
   case FocusTarget::Kind::Filter:open_filter_dropdown();break;
   case FocusTarget::Kind::Row:if(t.row>=0&&t.row<static_cast<int>(rows_.size())){selected_=rows_[static_cast<std::size_t>(t.row)];notice_.clear();}break;
   case FocusTarget::Kind::Go:if(selected_>=0){const auto& selected=entries_[static_cast<std::size_t>(selected_)];if(selected.example){focus_={{selected.example->system_id,selected.example->id}};close();}}break;
   case FocusTarget::Kind::ForceClass:if(selected_>=0)force_type(false,frame);break;
   case FocusTarget::Kind::ForceSub:if(selected_>=0)force_type(true,frame);break;
  }
 }
 public:
 void open(const stellar::core::FreshCampaignState& w){entries_=stellar::core::build_developer_planet_index(w);visible_=true;rules_=false;selected_=-1;filter_=-1;list_view_.scroll_offset=0;notice_.clear();focus_.reset();ring_=-1;rebuild();}
 void close(){visible_=false;dropdown_.close();pressed_=-1;ring_=-1;}
 bool visible()const{return visible_;}
 // Keyboard-focus contract: the ring walks the rendered controls in (y,x)
 // order — header actions, class filter, each rendered row, then the
 // example/generate footer — with activation replaying the same dispatch
 // pointer press+release takes (dropdown open, row select, rules toggle,
 // canonical force_developer_planet_type and the map focus request).
 [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
 [[nodiscard]] int focus()const noexcept{return ring_;}
 [[nodiscard]] std::string focused_label(int w,int h)const{
  const auto targets=focusables(layout(w,h));
  return ring_>=0&&ring_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(ring_)].label:std::string{};
 }
 [[nodiscard]] std::optional<UiRect> focused_bounds(int w,int h)const{
  const auto targets=focusables(layout(w,h));
  return ring_>=0&&ring_<static_cast<int>(targets.size())?std::optional<UiRect>{targets[static_cast<std::size_t>(ring_)].rect}:std::nullopt;
 }
 [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int w,int h)const{
  const auto targets=focusables(layout(w,h));
  return ring_>=0&&ring_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(ring_)].control:stellar::engine::AnnouncementControl::Custom;
 }
 bool take_giant_request(){return std::exchange(giant_request_,false);}
 std::optional<std::pair<int,int>> take_focus_request(){return std::exchange(focus_,{});}
 bool handle(const InputEvent& e,int w,int h,stellar::core::CampaignFrame& frame){
  if(!visible_)return false;const auto l=layout(w,h);pointer_=e.position;
  if(dropdown_.visible()){if(const auto choice=dropdown_.handle(e,l.filter,w,h)){filter_=*choice-1;selected_=-1;list_view_.scroll_offset=0;entries_=stellar::core::build_developer_planet_index(frame.runtime().world().campaign(),mode());rebuild();}return true;}
  if(e.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}close();return true;}
  if(e.type==InputEventType::PointerCancelled){pressed_=-1;ring_=-1;return true;}
  if(e.type==InputEventType::Wheel&&l.list.contains(e.position)){(void)first_row(l);list_view_.scroll_to(list_view_.scroll_offset-std::round(e.wheel_y)*list_view_.row_height);}
  if(e.type==InputEventType::KeyPressed&&e.key){
   constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
   constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
   constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
   const auto targets=focusables(l);const int count=static_cast<int>(targets.size());
   if(count){
    const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
    const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
    const bool home=e.key==kHome,end=e.key==kEnd;
    if(fwd||bwd||home||end){
     if(ring_<0)ring_=bwd||end?count-1:0;
     else if(fwd)ring_=(ring_+1)%count;
     else if(bwd)ring_=(ring_+count-1)%count;
     else if(home)ring_=0;
     else ring_=count-1;
     const auto&t=targets[static_cast<std::size_t>(ring_)];
     if(t.kind==FocusTarget::Kind::Row&&t.row>=0)list_view_.ensure_visible(static_cast<std::size_t>(t.row));
     return true;
    }
    if((e.key==kReturn||e.key==kSpace)&&ring_>=0){activate(targets[static_cast<std::size_t>(ring_)],frame);return true;}
   }
  }
  if(e.type==InputEventType::LeftPressed){
   ring_=-1;
   if(l.filter.contains(e.position)){open_filter_dropdown();return true;}
   const int first=first_row(l);
   for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i)if(row(l,i).contains(e.position)){selected_=rows_[first+i];notice_.clear();return true;}
   pressed_=l.close.contains(e.position)?0:l.go.contains(e.position)?1:l.force_class.contains(e.position)?2:l.force_sub.contains(e.position)?3:l.rules.contains(e.position)?4:l.giants.contains(e.position)?5:-1;
  }
  if(e.type==InputEventType::LeftReleased){const int p=std::exchange(pressed_,-1);if(p==0&&l.close.contains(e.position)){close();return true;}
   if(p==5&&l.giants.contains(e.position)){giant_request_=true;close();return true;}
   if(p==4&&l.rules.contains(e.position)){rules_=!rules_;return true;}
   if(selected_>=0){const auto selected=entries_[selected_];
    if(p==1&&l.go.contains(e.position)&&selected.example){focus_={{selected.example->system_id,selected.example->id}};close();}
    if((p==2&&l.force_class.contains(e.position))||(p==3&&l.force_sub.contains(e.position)))force_type(p==3,frame);
   }
  }return true;
 }
 void render(DrawList& out,int w,int h)const{
  if(!visible_)return;const auto l=layout(w,h);const float s=l.scale;const int font=std::max(12,static_cast<int>(15*s));native_menu_style::panel(out,l.panel,s);
  const auto text=[&](UiRect r,std::string value,Color color=native_menu_style::ink){native_menu_style::text(out,r,std::move(value),font,color);};
  text({l.panel.x+18*s,l.panel.y+16*s,450*s,28*s},"PLANET TYPE TEST INDEX",native_menu_style::cyan);
  native_menu_style::button(out,l.giants,"GIANT & RING TEST PANEL",font,l.giants.contains(pointer_),true,s);
  native_menu_style::button(out,l.close,"CLOSE",font,l.close.contains(pointer_),true,s);
  native_menu_style::button(out,l.rules,rules_?"VIEW EXAMPLE":"VIEW RULES",font,l.rules.contains(pointer_),selected_>=0,s);
  native_menu_style::button(out,l.filter,filter_<0?"All planet classes":filter_==16?"Imported artwork in this campaign":filter_==17?"Habitable for your species (unsettled)":stellar::core::planet_class_definition(static_cast<stellar::core::PlanetClass>(filter_)).name,font,l.filter.contains(pointer_),true,s);
  const int first=first_row(l);
  for(int i=0;i<8&&first+i<static_cast<int>(rows_.size());++i){const auto& entry=entries_[rows_[first+i]];const auto r=row(l,i);if(rows_[first+i]==selected_)out.overlay.emplace_back(FilledRectangle{r,{17,50,68,245}});text({r.x+7*s,r.y+3*s,r.width-14*s,22*s},entry.name);text({r.x+7*s,r.y+24*s,r.width-14*s,20*s},stellar::core::planet_class_definition(entry.type).name+" · "+std::to_string(entry.count)+" examples",native_menu_style::muted);}
  if(selected_>=0){const auto& entry=entries_[selected_];const auto& d=stellar::core::planet_class_definition(entry.type);const auto& sub=stellar::core::planet_subclass_definition(entry.type,entry.subclass);float y=l.details.y;
   const auto line=[&](std::string value){text({l.details.x,y,l.details.width,25*s},std::move(value));y+=27*s;};
   if(rules_){const auto& r=stellar::core::planet_type_record(entry.type,entry.subclass);const auto& atmosphere=r.atmosphere_rules;
    line("Base class: "+d.name);line("Subclass: "+r.name);
    std::string zones;for(auto zone:r.valid_orbital_zones){if(!zones.empty())zones+=", ";zones+=stellar::core::planet_orbital_zone_definition(zone).id;}line("Orbital zones: "+zones);
    line("Surface temperature: "+number(r.surface_temperature_kelvin[0],0)+"–"+number(r.surface_temperature_kelvin[1],0)+" K");
    line("Atmosphere: "+atmosphere.composition_rule);line("Pressure: "+number(atmosphere.pressure_kpa[0],2)+"–"+number(atmosphere.pressure_kpa[1],2)+" kPa");
    line("Retention >= "+number(atmosphere.minimum_retention,0)+" above "+number(atmosphere.retention_check_above_kpa,0)+" kPa · gas mass "+number(atmosphere.molecular_mass,1)+" u");
    const auto yes=[](bool v){return v?"yes":"no";};line(std::string("Water allowed: ")+yes(r.water_allowed)+" · ice allowed: "+yes(r.ice_allowed));line(std::string("Volcanism allowed: ")+yes(r.volcanism_allowed)+" · heat: "+r.heat_rule);
    line("Class baseline: "+number(r.class_percentage,2)+"% · subclass weight: "+number(r.subclass_weight,2));line("Within class: "+number(r.within_class_percentage,2)+"% · overall baseline: "+number(r.baseline_percentage,3)+"%");
    line("Image pools: "+std::to_string(r.accepted_image_pool.size())+" accepted · "+std::to_string(r.rejected_image_pool.size())+" rejected");line("Compatible images from other classes: "+std::to_string(r.compatible_image_pool.size()));
    line("Physics filters and renormalizes generation weights.");line("Full image IDs and reasons: planet type registry report.");
   }else{
   line(entry.name);line("Class baseline: "+number(d.weight,1)+"% · "+std::to_string(entry.artwork_count)+" approved images");line("Allowed temperature: "+number(sub.temperature[0],0)+"–"+number(sub.temperature[1],0)+" K");
   if(entry.example){const auto& b=*entry.example;const auto& a=*b.appearance;
    line(entry.system_name+" / "+b.name+(a.developer_example?" · QA":""));line("Mass "+number(b.mass_earth)+" Earth · radius "+number(b.radius_earth)+" Earth");
    line("Orbit "+number(b.stellar_exposure?b.stellar_exposure->orbit_au:0,4)+" AU · flux "+number(b.stellar_exposure?b.stellar_exposure->incident_flux:0,3)+" Sol");
    line("Surface "+number(b.environment.temperature_kelvin,1)+" K · pressure "+number(b.environment.pressure_kpa,1)+" kPa");line("Equilibrium "+number(a.climate.equilibrium_kelvin,1)+" K · greenhouse +"+number(a.climate.greenhouse_kelvin,1)+" K");
    line("Water "+number(a.climate.surface_water*100,0)+"% · ice "+number(a.climate.surface_ice*100,0)+"% · retention "+number(a.climate.retention_parameter,1));
    line("Heat: "+a.climate.heat_source+" · "+a.climate.history);line("Source: "+(a.source_asset_id.empty()?"seeded procedural surface":a.source_asset_id));line("3D material: "+a.material_id);line("Display: static 3D globe · drag to inspect");
    const auto errors=stellar::core::planet_appearance_contradictions(b);line(errors.empty()?"Orbit / water / atmosphere checks: valid":errors.front());
   }else line("No example in this campaign. Generate a class or subclass below.");}
  }else text(l.details,"Select a subclass to inspect its materials, physical rules, and campaign examples.");
  native_menu_style::button(out,l.go,"GO TO EXAMPLE",font,l.go.contains(pointer_),selected_>=0&&entries_[selected_].example.has_value(),s);
  native_menu_style::button(out,l.force_class,"GENERATE CLASS",font,l.force_class.contains(pointer_),selected_>=0,s);
  native_menu_style::button(out,l.force_sub,"GENERATE SUBCLASS",font,l.force_sub.contains(pointer_),selected_>=0,s);
  text({l.panel.x+18*s,l.panel.y+560*s,l.panel.width-36*s,32*s},notice_.empty()?"Examples are added to eligible orbits. Existing worlds and colonies are preserved.":notice_,native_menu_style::muted);
  if(ring_>=0){
   const auto targets=focusables(l);
   if(ring_<static_cast<int>(targets.size())){
    const auto& r=targets[static_cast<std::size_t>(ring_)].rect;
    const UiRect outer{r.x-3*s,r.y-3*s,r.width+6*s,r.height+6*s};
    out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
    out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
   }
  }
  if(dropdown_.visible())dropdown_.render(out,l.filter,w,h,font);
 }
};
}
