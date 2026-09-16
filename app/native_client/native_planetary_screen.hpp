#pragma once
#include "native_colony_controller.hpp"
#include "native_surface_construction_controller.hpp"
#include "native_menu_style.hpp"
#include "native_ui_layout.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <variant>

namespace stellar::native_colony_ui {
using namespace stellar::native_map;
using namespace stellar::native_colony;
enum class PlanetaryAction {None, Back, Build, Upgrade, Repair, Enable, Priority, Remove, CommandCenter, Confirm, Cancel, Freight, Save};
struct PlanetaryCommand { PlanetaryAction action{}; int slot{-1}, building_id{}; std::string type; bool value{}; };
struct PlanetaryLayout {
  float s{};int font{},small{},heading{};UiRect screen,back,save,hero,portrait,metrics,left,command,facts,slots,right,queue,tabs,details,notice,modal,confirm,cancel;
  static PlanetaryLayout make(int width,int height){
    PlanetaryLayout l; l.s=std::clamp(height/1080.f,.8f,2.5f);const float s=l.s,g=12*s;
    l.font=std::max(14,static_cast<int>(16*s));l.small=std::max(12,static_cast<int>(13*s));l.heading=static_cast<int>(23*s);
    const auto chrome=NativeUiLayout::for_viewport(width,height);
    const float left=chrome.map.x+chrome.map.width+12*s,top=chrome.day_text.y+chrome.day_text.height+8*s;
    l.screen={left,top,static_cast<float>(width)-left-12*s,static_cast<float>(height)-top-12*s};
    const auto r=l.screen;const float x=r.x+g,w=r.width-2*g,y=r.y+g;
    l.back={x+w-166*s,y,166*s,34*s};l.save={l.back.x-86*s,y,76*s,34*s};
    l.hero={x,y+46*s,w-326*s,142*s};l.portrait={l.hero.x+l.hero.width+g,l.hero.y,314*s,142*s};
    l.metrics={x,l.hero.y+l.hero.height+g,w,68*s};
    const float contentY=l.metrics.y+l.metrics.height+g,bottom=r.y+r.height-44*s;
    l.left={x,contentY,280*s,bottom-contentY};l.right={x+w-348*s,contentY,348*s,bottom-contentY};
    l.slots={l.left.x+l.left.width+g,contentY,l.right.x-l.left.x-l.left.width-2*g,bottom-contentY};
    l.command={l.left.x+10*s,l.left.y+88*s,l.left.width-20*s,34*s};
    l.facts={l.left.x+10*s,l.command.y+l.command.height+14*s,l.left.width-20*s,l.left.y+l.left.height-l.command.y-l.command.height-24*s};
    l.queue={l.right.x+10*s,l.right.y+34*s,l.right.width-20*s,90*s};
    l.tabs={l.right.x+10*s,l.queue.y+l.queue.height+8*s,l.right.width-20*s,32*s};
    l.details={l.tabs.x,l.tabs.y+l.tabs.height+10*s,l.tabs.width,l.right.y+l.right.height-l.tabs.y-l.tabs.height-20*s};
    l.notice={x,bottom+8*s,w,30*s};
    const float mw=std::min(650*s,w),mh=std::min(350*s,r.height-30*s);
    l.modal={(width-mw)*.5f,(height-mh)*.5f,mw,mh};
    l.cancel={l.modal.x+20*s,l.modal.y+mh-54*s,(mw-52*s)*.5f,36*s};l.confirm={l.cancel.x+l.cancel.width+12*s,l.cancel.y,l.cancel.width,l.cancel.height};
    return l;
  }
};
class NativePlanetaryScreen {
 public:
  using Picture=std::shared_ptr<const RgbaImage>;
  using Art=std::function<Picture(bool)>;
  void set_art(Art art){art_=std::move(art);}
  void set_portrait(Picture p){portrait_=std::move(p);}
  void set_measurer(std::function<TextExtent(const Text&)> value){measure_=std::move(value);}
  void reset(){selected_=-1;tab_=0;fact_scroll_=detail_scroll_=slot_scroll_=queue_scroll_=0;pending_={};notice_.clear();pressed_.reset();hits_.clear();portrait_.reset();}
  void set_view(const NativeColonyView& v){if(identity_!=std::pair{v.campaign_generation,v.colony_id}){reset();identity_={v.campaign_generation,v.colony_id};}}
  void set_confirmation(std::variant<std::monostate,NativeSurfacePlacementQuote,NativeSurfaceManagementQuote,NativeSurfaceRemovalQuote> value){pending_=std::move(value);pressed_.reset();}
  const auto& pending() const{return pending_;}
  bool modal() const{return pending_.index()!=0;}
  void complete(std::string notice){pending_={};notice_=std::move(notice);pressed_.reset();}
  int selected_slot()const{return selected_;}
  PlanetaryCommand handle(const InputEvent& e,int width,int height){
    pointer_=e.position;const auto l=PlanetaryLayout::make(width,height);
    if(e.type==InputEventType::PointerCancelled){pressed_.reset();return {};}
    if(e.type==InputEventType::EscapePressed){if(modal())return {PlanetaryAction::Cancel};return {PlanetaryAction::Back};}
    if(e.type==InputEventType::Wheel&&!modal()){
      pressed_.reset();
      const float change=e.wheel_y*48*l.s;
      if(l.facts.contains(e.position))fact_scroll_=std::clamp(fact_scroll_+change,std::min(0.f,l.facts.height-fact_height_),0.f);
      else if(l.details.contains(e.position))detail_scroll_=std::clamp(detail_scroll_+change,std::min(0.f,l.details.height-detail_height_),0.f);
      else if(l.queue.contains(e.position))queue_scroll_=std::clamp(queue_scroll_+change,std::min(0.f,l.queue.height-queue_height_),0.f);
      else if(l.slots.contains(e.position))slot_scroll_=std::clamp(slot_scroll_+change,std::min(0.f,l.slots.height-55*l.s-slot_height_),0.f);
      return {};
    }
    const auto hit=[&]()->std::optional<Hit>{for(auto it=hits_.rbegin();it!=hits_.rend();++it)if(it->rect.contains(e.position)&&it->enabled)return *it;return {};};
    if(e.type==InputEventType::LeftPressed){pressed_=hit();return {};}
    if(e.type!=InputEventType::LeftReleased)return {};
    const auto prior=std::exchange(pressed_,std::nullopt),current=hit();
    if(!prior||!current||prior->id!=current->id||prior->select!=current->select||prior->tab!=current->tab||prior->command.action!=current->command.action||prior->command.slot!=current->command.slot||prior->command.building_id!=current->command.building_id||prior->command.type!=current->command.type||prior->command.value!=current->command.value)return {};
    if(current->tab>=0){tab_=current->tab;detail_scroll_=0;return {};}
    if(current->select>=0){selected_=current->select;tab_=1;detail_scroll_=0;return {};}
    return current->command;
  }
  void render(DrawList& out,const NativeColonyView& v,int width,int height)const{
    using namespace stellar::native_menu_style;
    const auto l=PlanetaryLayout::make(width,height);const float s=l.s;
    hits_.clear();out.overlay.emplace_back(FilledRectangle{{l.screen.x,l.screen.y,static_cast<float>(width)-l.screen.x,static_cast<float>(height)-l.screen.y},{6,17,27,255}});
    label(out,{l.hero.x,l.back.y,l.save.x-l.hero.x-12*s,34*s},"STELLAR CONTINUUM  /  PLANETARY OPERATIONS",l.small,cyan);
    button(out,l.back,"Return to orbit",{PlanetaryAction::Back},l);button(out,l.save,"Save",{PlanetaryAction::Save},l);
    // The approved city panorama is illustration, and only belongs on established temperate worlds.
    const bool city=v.surface_hub_level>0&&v.planet.details&&v.planet.details->temperature_kelvin>=273&&v.planet.details->temperature_kelvin<=310&&v.planet.details->available_solvent==stellar::core::PlanetarySolventRegime::Water;
    panel(out,l.hero,s);if(city&&art_)if(auto image=art_(false)){
      const float ih=static_cast<float>(image->height()),iw=static_cast<float>(image->width());
      const float sh=std::min(ih,iw*l.hero.height/l.hero.width),sw=std::min(iw,ih*l.hero.width/l.hero.height);
      out.overlay.emplace_back(Image{image,l.hero,UiRect{(iw-sw)*.5f,(ih-sh)*.45f,sw,sh},{255,255,255,255},l.hero});
    }
    out.overlay.emplace_back(FilledRectangle{{l.hero.x,l.hero.y,l.hero.width*.62f,l.hero.height},{4,15,27,204}});
    label(out,{l.hero.x+16*s,l.hero.y+12*s,l.hero.width-32*s,25*s},v.homeworld?"EMPIRE CAPITAL":v.resource_outpost?"RESOURCE OUTPOST":v.surface_hub_level==0?"COLONY FOUNDATION":"PLANETARY COLONY",l.small,gold);
    label(out,{l.hero.x+16*s,l.hero.y+40*s,l.hero.width-32*s,52*s},v.body_display_name,static_cast<int>(35*s),ink);
    label(out,{l.hero.x+16*s,l.hero.y+91*s,l.hero.width-32*s,23*s},v.colony_name+"  ·  "+v.system_name,l.small,ink);
    label(out,{l.hero.x+16*s,l.hero.y+119*s,l.hero.width-32*s,20*s},city?"COLONY VISTA · ILLUSTRATION":"ORBITAL SURVEY",std::max(11,l.small-1),muted);
    panel(out,l.portrait,s);if(portrait_)out.overlay.emplace_back(Image{portrait_,{l.portrait.x+7*s,l.portrait.y+7*s,128*s,128*s},{},{255,255,255,255},l.portrait});
    const float px=l.portrait.x+145*s;
    label(out,{px,l.portrait.y+16*s,160*s,24*s},"WORLD SUMMARY",l.small,cyan);
    label(out,{px,l.portrait.y+47*s,160*s,76*s},planet_summary(v),l.small,ink);
    const std::array<std::string,5> titles={"POPULATION","POWER BALANCE","LOCAL CREDITS / DAY","MATERIALS / DAY","RESEARCH LAB CAPACITY"};
    const double power=v.power_supply+v.storage_discharge_per_day-v.power_demand;
    const std::array<std::string,5> values={population(v.population_millions),signed_number(power),v.currency.format_rate(v.local_credit_flow.net_credits_per_day),"+"+number(v.industry_per_day,2),number(v.active_research_lab_units,2)};
    const std::array<Color,5> colors={Color{125,189,255,255},gold,gold,Color{246,172,112,255},Color{197,160,255,255}};
    for(int i=0;i<5;++i){UiRect r{l.metrics.x+i*(l.metrics.width+9*s)/5,l.metrics.y,(l.metrics.width-36*s)/5,l.metrics.height};panel(out,r,s);out.overlay.emplace_back(FilledRectangle{{r.x,r.y,r.width,3*s},colors[i]});label(out,{r.x+10*s,r.y+9*s,r.width-20*s,22*s},titles[i],l.small,colors[i]);label(out,{r.x+10*s,r.y+31*s,r.width-20*s,32*s},values[i],i==2?l.font:static_cast<int>(24*s),(i==1&&power<0)||(i==2&&v.local_credit_flow.net_credits_per_day<0)?bad:ink);}
    panel(out,l.left,s);label(out,{l.left.x+10*s,l.left.y+10*s,l.left.width-20*s,24*s},"COMMAND CENTER",l.font,gold);
    label(out,{l.left.x+10*s,l.left.y+37*s,l.left.width-20*s,49*s},v.hub_upgrade_days_remaining>0?"Construction · "+number(v.hub_upgrade_days_remaining,1)+" days*":v.surface_hub_level==0?"Not built · Slots locked":"Level "+std::to_string(v.surface_hub_level)+" · "+std::to_string(v.building_capacity)+" slots",l.small,ink);
    button(out,l.command,v.hub_upgrade_days_remaining>0?"Construction in progress":v.surface_hub_level==0?"Build Command Center":"Upgrade Command Center",{PlanetaryAction::CommandCenter},l,v.hub_upgrade_available&&v.can_afford_hub_upgrade);
    float fy=l.facts.y+fact_scroll_;
    const auto fact=[&](std::string title,std::string value,Color color=ink){label(out,{l.facts.x,fy,l.facts.width,22*s},std::move(title),l.small,muted,l.facts);fy+=22*s;fy+=wrapped(out,{l.facts.x,fy,l.facts.width,0},l.facts,std::move(value),l.font,color)+12*s;};
    if(v.hub_upgrade_available)fact("COMMAND CENTER COST",v.currency.format(v.hub_upgrade_credit_budget_units)+" + "+number(v.hub_upgrade_industry_cost,0)+" materials\n"+number(v.hub_upgrade_industry_cost/30,1)+" days at full funding");
    if(!v.hub_upgrade_lock_reason.empty())fact("REQUIREMENT",v.hub_upgrade_lock_reason,bad);
    fact("SYSTEM",v.system_name);fact("POPULATION SPECIES",v.population_species_name.empty()?v.population_species_id:v.population_species_name);fact("STABILITY / INFRASTRUCTURE",number(v.stability*100,0)+"% / "+number(v.infrastructure*100,0)+"%");
    fact("RADIUS",number(v.planet.radius_earth*6371,0)+" km");
    if(v.planet.details){const auto& d=*v.planet.details;fact("MASS",number(d.mass_earth,3)+" Earth masses");fact("GRAVITY",number(d.gravity_g*9.80665,2)+" m/s²");fact("TEMPERATURE",number(d.temperature_kelvin,0)+" K / "+number(d.temperature_kelvin-273.15,0)+" °C");fact("PRESSURE",number(d.pressure_kpa,1)+" kPa");fact("ATMOSPHERE",atmosphere(d.atmosphere));fact("SOLVENT",solvent(d.available_solvent));fact("RADIATION HAZARD",number(d.radiation_hazard*100,0)+"%");}
    fact("SURVEY FEATURES",v.planet.positive_signatures.empty()?"No confirmed signatures":std::to_string(v.planet.positive_signatures.size())+" confirmed signatures");
    fact("SPECIALIZATION",v.specialization_name+"\n"+v.specialization_description);fact("CONSTRUCTION / WEAR",number(v.construction_multiplier,2)+"× cost / "+number(v.environmental_wear_multiplier,2)+"× wear");
    fact_height_=fy-l.facts.y-fact_scroll_;scrollbar(out,l.facts,fact_height_,fact_scroll_);
    render_slots(out,v,l);render_right(out,v,l);
    label(out,l.notice,notice_.empty()?alerts(v):notice_,l.small,notice_.empty()?muted:cyan);
    if(modal())render_confirmation(out,l);
  }
 private:
  struct Hit{UiRect rect;int id{};PlanetaryCommand command;bool enabled{true};int select{-1},tab{-1};};
  static constexpr Color bad{255,156,137,255};
  Art art_;Picture portrait_;std::function<TextExtent(const Text&)> measure_;
  std::pair<std::uint64_t,int> identity_{};int selected_{-1},tab_{};float fact_scroll_{},detail_scroll_{},slot_scroll_{},queue_scroll_{};
  mutable float fact_height_{},detail_height_{},slot_height_{},queue_height_{};mutable std::vector<Hit> hits_;std::optional<Hit> pressed_;Point pointer_{};std::string notice_;
  std::variant<std::monostate,NativeSurfacePlacementQuote,NativeSurfaceManagementQuote,NativeSurfaceRemovalQuote> pending_;
  static std::string number(double n,int precision=1){std::ostringstream o;o<<std::fixed<<std::setprecision(precision)<<n;return o.str();}
  static std::string signed_number(double n){return (n>0?"+":"")+number(n,2);}
  static std::string population(double n){return std::abs(n)>=1000?number(n/1000,2)+" B":number(n,2)+" M";}
  static std::string atmosphere(stellar::core::PlanetaryAtmosphereRegime v){const std::array<const char*,7> names={"Vacuum","Oxygen / nitrogen","Oxygen rich","Carbon dioxide rich","Reducing","Inert","Other"};const auto i=static_cast<std::size_t>(v);return i<names.size()?names[i]:"Surveyed atmosphere";}
  static std::string solvent(stellar::core::PlanetarySolventRegime v){const std::array<const char*,5> names={"None","Water","Ammonia","Hydrocarbons","Other"};const auto i=static_cast<std::size_t>(v);return i<names.size()?names[i]:"Surveyed solvent";}
  static std::string planet_summary(const NativeColonyView& v){return v.system_name+" system\n"+(v.planet.details?number(v.planet.details->gravity_g*9.80665,2)+" m/s²\n"+number(v.planet.details->temperature_kelvin-273.15,0)+" °C\n":"")+"Stability "+number(v.stability*100,0)+"%";}
  static void label(DrawList& out,UiRect r,std::string value,int size,Color color,std::optional<UiRect> clip={}){out.overlay.emplace_back(Text{{r.x,r.y},std::move(value),color,size,r.width,clip.value_or(r),TextAlign::Left,FontFace::Interface});}
  static UiRect intersection(UiRect a,UiRect b){const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};}
  float wrapped(DrawList& out,UiRect r,UiRect clip,std::string value,int font,Color color)const{
    Text t{{r.x,r.y},std::move(value),color,font,r.width,clip,TextAlign::Left,FontFace::Interface};
    float h=static_cast<float>(font+5);if(measure_)h=static_cast<float>(measure_(t).height)+5;
    else {int lines=1;float count=0,cols=std::max(1.f,r.width/(font*.55f));for(char c:t.value){if(c=='\n'){++lines;count=0;}else if(++count>cols){++lines;count=0;}}h=lines*(font+5.f);}
    out.overlay.emplace_back(std::move(t));return std::max(h,font+5.f);
  }
  void button(DrawList& out,UiRect r,std::string title,PlanetaryCommand command,const PlanetaryLayout& l,bool enabled=true,std::optional<UiRect> clip={})const{
    const UiRect visible=clip?intersection(r,*clip):r;if(visible.height<=0||visible.width<=0)return;
    out.overlay.emplace_back(FilledRectangle{visible,enabled?(visible.contains(pointer_)?Color{35,89,113,255}:Color{21,52,71,255}):Color{15,29,40,255}});
    out.overlay.emplace_back(StrokedRectangle{visible,enabled?Color{84,139,161,255}:Color{50,73,89,255}});
    label(out,{r.x+8*l.s,r.y+(r.height-l.font)*.5f,r.width-16*l.s,r.height},std::move(title),l.font,enabled?stellar::native_menu_style::ink:stellar::native_menu_style::muted,visible);
    hits_.push_back({visible,static_cast<int>(hits_.size()),std::move(command),enabled});
  }
  static void scrollbar(DrawList& out,UiRect r,float content,float offset){if(content<=r.height||r.height<=0)return;const float h=std::max(14.f,r.height*r.height/content),y=r.y+(-offset/(content-r.height))*(r.height-h);out.overlay.emplace_back(FilledRectangle{{r.x+r.width-3,y,3,h},{105,157,178,210}});}
  static int art_index(std::string_view type){const auto family=stellar::core::surface_functional_family(type);if(family=="power_generator")return 0;if(family=="science_lab")return 1;if(family=="fabricator")return 2;if(family=="trade_hub")return 3;if(family=="habitat_complex")return 4;if(family=="controlled_agriculture")return 5;if(family=="water_reclamation")return 6;if(family=="grid_battery")return 7;return 8;}
  void building_art(DrawList& out,std::string_view type,UiRect r,UiRect clip)const{if(!art_)return;auto image=art_(true);if(!image)return;const int i=art_index(type);const float w=image->width()/3.f,h=image->height()/3.f;out.overlay.emplace_back(Image{image,r,UiRect{(i%3)*w,(i/3)*h,w,h},{255,255,255,255},clip});}
  static std::string state(const NativeSurfaceSite& b){return !b.complete?"Building "+number(b.progress_fraction*100,0)+"%":b.upgrade_days_remaining>0?"Upgrading":!b.enabled?"Disabled":b.condition<=.15?"Repairs required":!b.powered?"Power needed":!b.staffed?"Workers needed":"Operational";}
  void render_slots(DrawList& out,const NativeColonyView& v,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;const float s=l.s;panel(out,l.slots,s);
    label(out,{l.slots.x+10*s,l.slots.y+10*s,l.slots.width-20*s,28*s},"BUILDING SLOTS  "+std::to_string(v.construction_sites.size())+" / "+std::to_string(v.building_capacity),l.font,cyan);
    const UiRect area{l.slots.x+10*s,l.slots.y+45*s,l.slots.width-20*s,l.slots.height-55*s};
    const int columns=std::clamp(static_cast<int>(area.width/(150*s)),2,6),count=v.building_capacity>0?v.building_capacity:v.resource_outpost?8:16;
    const float pitch=160*s,cw=(area.width-(columns-1)*8*s)/columns;
    slot_height_=std::ceil(count/static_cast<float>(columns))*pitch;
    for(int i=0;i<count;++i){UiRect r{area.x+(i%columns)*(cw+8*s),area.y+slot_scroll_+(i/columns)*pitch,cw,pitch-8*s};auto clip=intersection(r,area);if(clip.height<=0)continue;
      const auto found=std::ranges::find(v.construction_sites,i,&NativeSurfaceSite::slot_index);const auto* b=found==v.construction_sites.end()?nullptr:&*found;const bool unlocked=i<v.building_capacity;
      out.overlay.emplace_back(FilledRectangle{clip,i==selected_?Color{25,61,75,255}:Color{12,30,43,255}});out.overlay.emplace_back(StrokedRectangle{clip,i==selected_?cyan:Color{55,88,109,255}});
      label(out,{r.x+8*s,r.y+5*s,r.width-16*s,20*s},"SLOT "+std::to_string(i+1),l.small,muted,clip);
      if(b)building_art(out,b->type_id,{r.x+8*s,r.y+27*s,r.width-16*s,65*s},clip);
      else label(out,{r.x+cw*.43f,r.y+34*s,cw*.5f,46*s},unlocked?"+":"◇",static_cast<int>(34*s),unlocked?cyan:muted,clip);
      label(out,{r.x+8*s,r.y+97*s,r.width-16*s,29*s},b?b->name:unlocked?"Available slot":"Locked slot",l.small,ink,clip);
      label(out,{r.x+8*s,r.y+126*s,r.width-16*s,24*s},b?state(*b):unlocked?"Construct building":"Command Center required",l.small,b&&(!b->powered||!b->staffed)?bad:cyan,clip);
      if(b&&!b->complete){UiRect bar{r.x+5*s,r.y+r.height-4*s,static_cast<float>((r.width-10*s)*b->progress_fraction),3*s};out.overlay.emplace_back(FilledRectangle{intersection(bar,area),gold});}
      hits_.push_back({clip,static_cast<int>(hits_.size()),{},unlocked,i,-1});
    }
    scrollbar(out,area,slot_height_,slot_scroll_);
  }
  void render_right(DrawList& out,const NativeColonyView& v,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;const float s=l.s;panel(out,l.right,s);
    label(out,{l.right.x+10*s,l.right.y+9*s,l.right.width-20*s,25*s},"CONSTRUCTION QUEUE",l.font,gold);
    float qy=l.queue.y+queue_scroll_;int queued=0;
    if(v.hub_upgrade_days_remaining>0){qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,"Command Center · "+number(v.hub_upgrade_days_remaining,1)+" days*",l.small,gold);++queued;}
    for(const auto& b:v.construction_sites)if(!b.complete||b.upgrade_days_remaining>0){qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,b.name+" · "+(!b.complete?number(b.progress_fraction*100,0)+"%":number(b.upgrade_days_remaining,1)+" days*"),l.small,ink);++queued;}
    if(queued==0)qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,"No construction scheduled.\nChoose an empty slot to begin.",l.small,muted);
    queue_height_=qy-l.queue.y-queue_scroll_;scrollbar(out,l.queue,queue_height_,queue_scroll_);
    for(int i=0;i<2;++i){UiRect r{l.tabs.x+i*l.tabs.width*.5f,l.tabs.y,l.tabs.width*.5f,l.tabs.height};button(out,r,i==0?"Economy":"Buildings",{},l);hits_.back().tab=i;if(tab_==i)out.overlay.emplace_back(FilledRectangle{{r.x,r.y+r.height-2*s,r.width,2*s},cyan});}
    float y=l.details.y+detail_scroll_;const auto write=[&](std::string value,Color color=ink,int size=0){y+=wrapped(out,{l.details.x,y,l.details.width-7*s,0},l.details,std::move(value),size?size:l.small,color)+7*s;};
    const auto action=[&](std::string title,PlanetaryCommand cmd,bool enabled=true){button(out,{l.details.x,y,l.details.width-7*s,36*s},std::move(title),std::move(cmd),l,enabled,l.details);y+=44*s;};
    if(tab_==0){
      write("PRODUCTION & REQUIREMENTS",cyan,l.font);write("Rates use game days. Food, water and housing are population-support capacities.",muted);
      const auto money=[&](double n){return v.currency.format_rate(n);};
      write("Tax revenue  "+money(v.local_credit_flow.colony_revenue_per_day));write("Trade revenue  "+money(v.local_credit_flow.trade_revenue_per_day));write("Local operating need  "+money(v.local_credit_flow.operating_costs_per_day));
      write("Administration  "+money(v.local_credit_flow.colony_administration_per_day)+"\nServices  "+money(v.local_credit_flow.population_services_per_day)+"\nHabitats  "+money(v.local_credit_flow.habitat_support_per_day)+"\nBuildings  "+money(v.local_credit_flow.surface_maintenance_per_day),muted);
      write("Operational labs  "+number(v.active_research_lab_units,2));double materials=0;for(const auto& b:v.construction_sites)if(!b.complete)materials+=b.remaining_construction_materials;write("Queued materials  "+number(materials,1));
      write("Power  "+number(v.power_supply,1)+" supply / "+number(v.power_demand,1)+" demand");write("Stored power  "+number(v.stored_power_days,1)+" / "+number(v.power_storage_capacity_days,1)+" power-days\nCharge / discharge  "+number(v.storage_charge_per_day,1)+" / "+number(v.storage_discharge_per_day,1));
      for(const auto& pair:std::array<std::pair<const char*,double>,3>{{{"Food",v.food_capacity_millions},{"Water",v.water_capacity_millions},{"Housing",v.housing_capacity_millions}}})write(std::string(pair.first)+"  "+population(pair.second)+" / "+population(v.population_millions)+"\n"+(pair.second<v.population_millions?"Deficit ":"Surplus ")+population(std::abs(pair.second-v.population_millions)),pair.second<v.population_millions?bad:ink);
      write("Food / water reserves  "+number(v.food_reserve_days,1)+" / "+number(v.water_reserve_days,1)+" days");write("Workers  "+population(v.workforce_available_millions)+" available / "+population(v.workforce_demand_millions)+" required");write("Employment  "+number(v.employment_rate*100,1)+"%\nWorking age  "+population(v.working_age_population_millions)+"\nEmployed  "+population(v.employed_population_millions));
      write("Cargo transfer  "+number(v.cargo_transfer_capacity_per_day,1)+" materials/day");if(v.resource_outpost){write("Deposit  "+v.deposit_material_name+" · "+v.deposit_grade+"\nRemaining  "+number(v.remaining_deposit_materials,0)+" materials\nExtraction  "+number(v.extraction_per_day,2)+" / day\nExtracted stores  "+number(v.stored_extracted_materials,1)+" / "+number(v.extracted_material_capacity,1));action("Collect materials",{PlanetaryAction::Freight});}
      write("Operating funding  "+number(v.operating_funding*100,1)+"%",v.operating_funding<.999?bad:ink);write("Empire treasury  "+v.formatted_treasury+"\nConstruction stores  "+number(v.stored_industry,1)+" materials\nEmpire credit flow  "+money(v.empire_credit_flow)+"\nEmpire material output  "+signed_number(v.empire_industry_flow)+" / day\nOperating arrears  "+v.currency.format(v.operating_arrears));
    }else if(selected_<0){write("BUILDING MANAGEMENT",cyan,l.font);write("Select an empty slot to construct a building, or an occupied slot to manage it.\n\nConstruction consumes materials over time. Pausing the campaign stops progress.");}
    else {
      write("SLOT "+std::to_string(selected_+1),cyan,l.font);const auto it=std::ranges::find(v.construction_sites,selected_,&NativeSurfaceSite::slot_index);
      if(it==v.construction_sites.end()){
        write("CONSTRUCT A BUILDING",ink,l.font);write("Credits authorize construction. Materials are drawn from shared empire stores over time. The selected slot is reserved immediately.",muted);
        for(const auto& option:v.available_buildings){
          const float top=y,tx=l.details.x+90*s,tw=l.details.width-97*s;
          building_art(out,option.type_id,{l.details.x,top,78*s,70*s},l.details);
          y+=wrapped(out,{tx,y,tw,0},l.details,option.name,l.font,gold)+4*s;
          y+=wrapped(out,{tx,y,tw,0},l.details,option.formatted_authorization+"\n"+number(option.industry_cost,0)+" materials",l.small,ink);
          y=std::max(y,top+70*s)+10*s;
          write(option.description);write("Power +"+number(option.power_supply,1)+" / -"+number(option.power_demand,1)+" · Workers "+population(option.workforce_required_millions),muted);
          action("Begin construction",{PlanetaryAction::Build,selected_,0,option.type_id},selected_<v.building_capacity&&v.treasury_budget_units+.0001>=option.authorization_budget_units);
          y+=10*s;
        }
      }else {const auto& b=*it;building_art(out,b.type_id,{l.details.x,y,l.details.width-7*s,110*s},l.details);y+=120*s;write(b.name,gold,l.heading);write(state(b),cyan);write("Condition "+number(b.condition*100,0)+"% · Efficiency "+number(b.efficiency*100,0)+"%\n"+(b.powered?"Powered":"Power unavailable")+" · "+(b.staffed?"Staffed":"Workers needed"));if(!b.complete)write(b.construction_stage+"\n"+number(b.remaining_construction_materials,1)+" materials remaining");if(b.upgrade_days_remaining>0)write(number(b.upgrade_days_remaining,1)+" days remaining at full funding");
        if(b.can_upgrade){write("Upgrade: "+b.upgrade_name+"\n"+v.currency.format(b.upgrade_credit_budget_units)+" + "+number(b.upgrade_industry_cost,0)+" materials\n"+b.upgrade_lock_reason);action("Upgrade building",{PlanetaryAction::Upgrade,selected_,b.building_id},b.can_afford_upgrade&&b.upgrade_lock_reason.empty());}
        if(b.complete){action(b.enabled?"Disable building":"Enable building",{PlanetaryAction::Enable,selected_,b.building_id,{},!b.enabled});action(b.prioritized?"Use normal priority":"Prioritize operations",{PlanetaryAction::Priority,selected_,b.building_id,{},!b.prioritized});action("Repair · "+number(b.repair_industry_cost,1)+" materials",{PlanetaryAction::Repair,selected_,b.building_id},b.can_afford_repair&&b.condition<.999999);}
        action(b.complete?"Demolish building…":"Cancel construction…",{PlanetaryAction::Remove,selected_,b.building_id});
      }
    }
    detail_height_=y-l.details.y-detail_scroll_;scrollbar(out,l.details,detail_height_,detail_scroll_);
  }
  static std::string alerts(const NativeColonyView& v){std::string r;if(v.surface_hub_level==0)r+="COMMAND CENTER REQUIRED.  ";if(v.power_supply+v.storage_discharge_per_day<v.power_demand)r+="POWER DEFICIT.  ";if(v.workforce_demand_millions>v.workforce_available_millions)r+="WORKERS NEEDED.  ";if(v.sustenance_support_ratio<1)r+="LIFE SUPPORT DEFICIT.  ";if(v.operating_funding<.999)r+="OPERATIONS UNDERFUNDED.  ";return r.empty()?"Select a slot to build or manage.  * Construction estimates assume full funding.":r;}
  void render_confirmation(DrawList& out,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;out.overlay.emplace_back(FilledRectangle{l.screen,{0,3,8,185}});panel(out,l.modal,l.s);hits_.clear();
    std::string title,body;bool accepted=false;
    std::visit([&](const auto& q){using T=std::decay_t<decltype(q)>;if constexpr(!std::is_same_v<T,std::monostate>){accepted=q.accepted;title=q.building_name;body=q.message;if constexpr(std::is_same_v<T,NativeSurfacePlacementQuote>)body=q.formatted_authorization+" authorization\n"+number(q.industry_cost,0)+" construction materials over time\n\n"+q.message;else if constexpr(std::is_same_v<T,NativeSurfaceManagementQuote>)body=q.description+"\n\n"+q.formatted_authorization+" + "+number(q.industry_cost,1)+" materials\n\n"+q.message;}},pending_);
    label(out,{l.modal.x+20*l.s,l.modal.y+18*l.s,l.modal.width-40*l.s,40*l.s},title,l.heading,cyan);
    const UiRect body_rect{l.modal.x+20*l.s,l.modal.y+66*l.s,l.modal.width-40*l.s,l.cancel.y-l.modal.y-78*l.s};wrapped(out,body_rect,body_rect,body,l.font,ink);
    button(out,l.cancel,"Cancel",{PlanetaryAction::Cancel},l);button(out,l.confirm,"Confirm",{PlanetaryAction::Confirm},l,accepted);
  }
};
}
