#include "native_campaign_calendar.hpp"
#pragma once
#include "native_colony_controller.hpp"
#include "native_surface_construction_controller.hpp"
#include "native_menu_style.hpp"
#include "native_ui_layout.hpp"
#include "native_planet_globe.hpp"
#include <stellar/engine/localization.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <variant>

namespace stellar::native_colony_ui {
using namespace stellar::native_map;
using namespace stellar::native_colony;
enum class PlanetaryAction {None, Back, Build, Upgrade, Repair, Enable, Priority, Remove, CommandCenter, Confirm, Cancel, Freight, Save};
struct PlanetaryCommand { PlanetaryAction action{}; int slot{-1}, building_id{}; std::string type; bool value{}; };
struct PlanetaryLayout {
  float s{};int font{},small{},heading{};UiRect screen,back,save,hero,portrait,metrics,left,command,facts,slots,right,queue,tabs,details,notice,modal,confirm,cancel;
  UiRect globe,layers,actions,view_modes,alerts,region_art;
  static PlanetaryLayout make(int width,int height){
    PlanetaryLayout l;l.s=std::clamp(height/1080.f,.8f,2.f);const float s=l.s,g=10*s;
    l.font=std::max(13,static_cast<int>(16*s));l.small=std::max(12,static_cast<int>(14*s));l.heading=static_cast<int>(24*s);
    const auto chrome=NativeUiLayout::for_viewport(width,height);
    const float x=native_navigation_content_left*chrome.scale,y=native_workspace_top(width,height);
    l.screen={x,y,width-x-10*s,height-y-8*s};
    const float bottom=height-118*s,pw=std::clamp(l.screen.width*.21f,260*s,336*s);
    l.left={x,y,pw,bottom-y};l.right={width-pw-10*s,y,pw,bottom-y};
    l.globe={x+pw+g,y,l.right.x-x-pw-2*g,bottom-y-42*s};
    l.hero={x+12*s,y+38*s,pw-24*s,98*s};l.portrait={x+12*s,y+40*s,82*s,82*s};
    l.back={x+10*s,y+5*s,pw-20*s,28*s};
    l.layers={x+12*s,bottom-194*s,pw-24*s,178*s};
    l.alerts={x+12*s,l.layers.y-70*s,pw-24*s,60*s};
    l.facts={x+12*s,y+166*s,pw-24*s,l.alerts.y-y-178*s};
    l.region_art={l.right.x+12*s,y+76*s,pw-24*s,132*s};
    l.command={l.right.x+12*s,bottom-102*s,pw-24*s,32*s};
    l.queue={l.right.x+12*s,bottom-64*s,pw-24*s,54*s};
    l.tabs={l.right.x+12*s,y+218*s,pw-24*s,32*s};
    l.details={l.tabs.x,l.tabs.y+42*s,l.tabs.width,l.command.y-l.tabs.y-54*s};
    l.slots=l.details;l.metrics={};
    l.actions={x,bottom+8*s,l.screen.width,72*s};l.save={x+l.screen.width-90*s,l.actions.y,90*s,72*s};
    l.view_modes={l.globe.x,l.globe.y+l.globe.height+5*s,l.globe.width,32*s};
    l.notice={x,bottom+86*s,l.screen.width,22*s};
    const float mw=std::min(650*s,l.screen.width),mh=std::min(350*s,l.screen.height-30*s);
    l.modal={(width-mw)*.5f,(height-mh)*.5f,mw,mh};
    l.cancel={l.modal.x+20*s,l.modal.y+mh-54*s,(mw-52*s)*.5f,36*s};l.confirm={l.cancel.x+l.cancel.width+12*s,l.cancel.y,l.cancel.width,l.cancel.height};
    return l;
  }
};
class NativePlanetaryScreen {
 public:
  using Picture=std::shared_ptr<const RgbaImage>;
  using Art=std::function<Picture(bool)>;
  void set_globe_maps(std::function<Picture(std::string_view,int)> maps){globe_.set_maps(std::move(maps));}
  NativePlanetGlobe& globe(){return globe_;}
  void set_art(Art art){art_=std::move(art);}
  void set_action_art(std::function<Picture(int)> art){action_art_=std::move(art);}
  void set_portrait(Picture p){portrait_=std::move(p);}
  void set_measurer(std::function<TextExtent(const Text&)> value){measure_=std::move(value);}
  void set_localization(const stellar::engine::LocalizationTable *table)noexcept{locale_=table;globe_.set_localization(table);}
  void reset(){selected_=-1;tab_=2;globe_.reset();fact_scroll_=detail_scroll_=slot_scroll_=queue_scroll_={};pending_={};notice_.clear();pressed_.reset();hits_.clear();portrait_.reset();}
  void set_view(const NativeColonyView& v){if(identity_!=std::pair{v.campaign_generation,v.body_id}){reset();identity_={v.campaign_generation,v.body_id};}globe_.bind(v);}
  void set_confirmation(std::variant<std::monostate,NativeSurfacePlacementQuote,NativeSurfaceManagementQuote,NativeSurfaceRemovalQuote> value){pending_=std::move(value);pressed_.reset();}
  const auto& pending() const{return pending_;}
  bool modal() const{return pending_.index()!=0;}
  void complete(std::string notice){pending_={};notice_=std::move(notice);pressed_.reset();}
  int selected_slot()const{return selected_;}
  PlanetaryCommand handle(const InputEvent& e,int width,int height){
    pointer_=e.position;const auto l=PlanetaryLayout::make(width,height);
    if(e.type==InputEventType::PointerCancelled){pressed_.reset();globe_.handle(e,l.globe);return {};}
    if(e.type==InputEventType::EscapePressed||e.type==InputEventType::RightPressed){if(modal())return {PlanetaryAction::Cancel};if(selected_>=0){selected_=-1;return {};}if(globe_.selected()>=0||globe_.zoom()!=1.f){globe_.whole();tab_=2;return {};}return {PlanetaryAction::Back};}
    if(!modal()&&globe_.handle(e,l.globe)){if(e.type==InputEventType::LeftReleased){tab_=2;selected_=-1;detail_scroll_={};}return {};}
    if(e.type==InputEventType::Wheel&&!modal()){
      pressed_.reset();
      const float change=e.wheel_y*48*l.s;
      if(l.facts.contains(e.position)){fact_scroll_.sync(fact_height_,l.facts.height);fact_scroll_.scroll_by(-change);}
      else if(tab_==1&&selected_<0&&l.slots.contains(e.position)){slot_scroll_.sync(slot_height_+55*l.s,l.slots.height);slot_scroll_.scroll_by(-change);}
      else if(l.details.contains(e.position)){detail_scroll_.sync(detail_height_,l.details.height);detail_scroll_.scroll_by(-change);}
      else if(l.queue.contains(e.position)){queue_scroll_.sync(queue_height_,l.queue.height);queue_scroll_.scroll_by(-change);}
      else if(l.slots.contains(e.position)){slot_scroll_.sync(slot_height_+55*l.s,l.slots.height);slot_scroll_.scroll_by(-change);}
      return {};
    }
    const auto hit=[&]()->std::optional<Hit>{for(auto it=hits_.rbegin();it!=hits_.rend();++it)if(it->rect.contains(e.position)&&it->enabled)return *it;return {};};
    if(e.type==InputEventType::LeftPressed){pressed_=hit();return {};}
    if(e.type!=InputEventType::LeftReleased)return {};
    const auto prior=std::exchange(pressed_,std::nullopt),current=hit();
    if(!prior||!current||prior->id!=current->id||prior->select!=current->select||prior->tab!=current->tab||prior->command.action!=current->command.action||prior->command.slot!=current->command.slot||prior->command.building_id!=current->command.building_id||prior->command.type!=current->command.type||prior->command.value!=current->command.value)return {};
    if(current->control>=0){
      if(current->control==0){globe_.whole();tab_=2;}
      else if(current->control==1)globe_.focus();
      else if(current->control==2)globe_.set_night(!globe_.night());
      else if(current->control==3)globe_.zoom_by(1.2f);
      else if(current->control==4)globe_.zoom_by(1/1.2f);
      else if(current->control>=10)layer_=layer_==current->control-10?-1:current->control-10;
      return {};
    }
    if(current->tab>=0){tab_=current->tab;selected_=-1;detail_scroll_=slot_scroll_={};return {};}
    if(current->select>=0){selected_=current->select;tab_=1;detail_scroll_={};return {};}
    return current->command;
  }
  void render(DrawList& out,const NativeColonyView& v,int width,int height)const{
    using namespace stellar::native_menu_style;
    const auto l=PlanetaryLayout::make(width,height);const float s=l.s;hits_.clear();
    out.overlay.emplace_back(FilledRectangle{l.screen,{2,9,16,255}});
    // Seeded starfield belongs to this planet view, with no background galaxies.
    for(int i=0;i<170;++i){const float x=l.globe.x+std::fmod(i*73.79f+v.body_id*.07f,std::max(1.f,l.globe.width)),y=l.globe.y+std::fmod(i*131.31f,std::max(1.f,l.globe.height));out.overlay.emplace_back(FilledRectangle{{x,y,i%17==0?2.f:1.f,1.f},{145,185,221,static_cast<std::uint8_t>(60+i%130)}});}
    globe_.render(out,l.globe,v,layer_,l.small);
    panel(out,l.left,s);panel(out,l.right,s);
    button(out,l.back,trf("PLANET_BACK",{v.system_name},"‹  {0} system"),{PlanetaryAction::Back},l);
    if(portrait_)out.overlay.emplace_back(Image{portrait_,l.portrait,{},{255,255,255,255},l.portrait});
    label(out,{l.hero.x+94*s,l.hero.y+6*s,l.hero.width-94*s,32*s},v.body_display_name,l.heading,ink);
    label(out,{l.hero.x+94*s,l.hero.y+42*s,l.hero.width-94*s,22*s},classification(v),l.small,cyan);
    label(out,{l.hero.x+94*s,l.hero.y+66*s,l.hero.width-94*s,22*s},world_class(v),l.small,ink);
    label(out,{l.hero.x,l.hero.y+92*s,l.hero.width,28*s},v.natural_habitability?trf("PLANET_HABITABILITY",{number(*v.natural_habitability*100,0)},"HABITABILITY  {0}%"):tr("PLANET_HABITABILITY_UNKNOWN","HABITABILITY  UNKNOWN"),l.font,v.natural_habitability&&*v.natural_habitability>.6?Color{113,233,158,255}:gold);
    float fy=l.facts.y-fact_scroll_.scroll_offset;
    const auto fact=[&](std::string title,std::string value,Color color=ink){
      const float rh=std::max(28*s,static_cast<float>(l.small+8));
      const UiRect row{l.facts.x,fy,l.facts.width,rh};
      const auto clip=intersection(row,l.facts);
      if(clip.height>0){out.overlay.emplace_back(Line{{row.x,clip.y+clip.height-1},{row.x+row.width,clip.y+clip.height-1},{39,74,93,140}});
        label(out,{row.x,fy+3*s,row.width*.43f,rh},std::move(title),l.small,muted,l.facts);
      }
      const float actual=wrapped(out,{row.x+row.width*.44f,fy+3*s,row.width*.54f,0},l.facts,std::move(value),l.small,color);fy+=std::max(rh,actual+6*s);
    };
    fact(tr("PLANET_FACT_OWNER","Owner"),v.owner_name.empty()?tr("PLANET_UNKNOWN","Unknown"):v.owner_name);
    fact(tr("PLANET_FACT_DIAMETER","Diameter"),v.planet.details?trf("PLANET_UNIT_KM",{number(v.planet.radius_earth*12742,0)},"{0} km"):tr("PLANET_UNKNOWN","Unknown"));
    if(v.planet.details){const auto& d=*v.planet.details;fact(tr("PLANET_FACT_MASS","Mass"),trf("PLANET_UNIT_MASS",{number(d.mass_earth*5.9722,3)},"{0} ×10²⁴ kg"));fact(tr("PLANET_FACT_GRAVITY","Gravity"),trf("PLANET_UNIT_GRAVITY",{number(d.gravity_g*9.80665,2)},"{0} m/s²"));fact(tr("PLANET_FACT_ATMOSPHERE","Atmosphere"),atmosphere(d.atmosphere));fact(tr("PLANET_FACT_TEMPERATURE","Temperature"),trf("PLANET_UNIT_CELSIUS",{number(d.temperature_kelvin-273.15,1)},"{0} °C"));fact(tr("PLANET_FACT_PRESSURE","Pressure"),trf("PLANET_UNIT_KPA",{number(d.pressure_kpa,1)},"{0} kPa"));fact(tr("PLANET_FACT_SOLVENT","Solvent"),solvent(d.available_solvent));fact(tr("PLANET_FACT_RADIATION","Radiation"),trf("PLANET_UNIT_PERCENT",{number(d.radiation_hazard*100,0)},"{0}%"));}
    else fact(tr("PLANET_FACT_ENVIRONMENT","Environment"),tr("PLANET_UNSURVEYED","Unsurveyed"),gold);
    if(!v.observer_only){fact(tr("PLANET_FACT_POPULATION","Population"),population(v.population_millions),cyan);fact(tr("PLANET_FACT_SPECIES","Species"),v.population_species_name);fact(tr("PLANET_FACT_STABILITY","Stability"),trf("PLANET_UNIT_PERCENT",{number(v.stability*100,0)},"{0}%"));fact(tr("PLANET_FACT_INFRASTRUCTURE","Infrastructure"),trf("PLANET_UNIT_PERCENT",{number(v.infrastructure*100,0)},"{0}%"));fact(tr("PLANET_FACT_COMMAND","Command Center"),trf("PLANET_LEVEL",{std::to_string(v.surface_hub_level)},"Level {0}"));fact(tr("PLANET_FACT_DEVELOPMENT","Development"),v.specialization_name);}
    fact_height_=fy-l.facts.y-fact_scroll_.scroll_offset;fact_scroll_.sync(fact_height_,l.facts.height);scrollbar(out,l.facts,fact_scroll_);
    if(!v.observer_only){const bool critical=v.sustenance_support_ratio<1||v.operating_funding<.999;label(out,{l.alerts.x,l.alerts.y,l.alerts.width,22*s},critical?tr("PLANET_ALERTS_TITLE","!  COLONY ALERTS"):tr("PLANET_STATUS_TITLE","COLONY STATUS"),l.small,critical?bad:cyan);button(out,{l.alerts.x,l.alerts.y+24*s,l.alerts.width,30*s},critical?tr("PLANET_REVIEW_CRITICAL","Review critical needs"):tr("PLANET_REVIEW_STATUS","Review colony conditions"),{},l);hits_.back().tab=0;}
    label(out,{l.layers.x,l.layers.y,l.layers.width,23*s},tr("PLANET_LAYERS_TITLE","PLANETARY MAP LAYERS"),l.small,cyan);
    const std::array<const char*,5> layer_keys={"PLANET_LAYER_PROVINCES","PLANET_LAYER_INFRA","PLANET_LAYER_POWER","PLANET_LAYER_RESOURCES","PLANET_LAYER_MILITARY"};
    const std::array<const char*,5> layer_names={"Geographic provinces","Infrastructure","Operational power","Resources · unavailable","Military · unavailable"};
    for(int i=0;i<5;++i){UiRect r{l.layers.x,l.layers.y+28*s+i*28*s,l.layers.width,25*s};const bool enabled=i==0?v.planet.details.has_value():i<3&&!v.observer_only;button(out,r,(layer_==i?"●  ":"○  ")+tr(layer_keys[i],layer_names[i]),{},l,enabled);hits_.back().control=10+i;}
    render_right(out,v,l);
    for(int i=0;i<3;++i){UiRect r{l.view_modes.x+i*(l.view_modes.width-76*s)/3,l.view_modes.y,(l.view_modes.width-82*s)/3,l.view_modes.height};button(out,r,i==0?tr("PLANET_VIEW_PLANET","Planet view"):i==1?tr("PLANET_VIEW_REGION","Region view"):globe_.night()?tr("PLANET_VIEW_DAY","Day view"):tr("PLANET_VIEW_NIGHT","Night view"),{},l,i!=1||globe_.selected()>=0);hits_.back().control=i;}
    for(int i=0;i<2;++i){UiRect r{l.view_modes.x+l.view_modes.width-70*s+i*36*s,l.view_modes.y,32*s,32*s};button(out,r,i==0?"+":"−",{},l);hits_.back().control=3+i;}
    const std::array<std::string,5> actions={v.foreign_settlement?tr("PLANET_ACTION_STRUCTURES","Structures"):tr("PLANET_ACTION_BUILD","Build"),tr("PLANET_ACTION_ECONOMY","Economy"),tr("PLANET_ACTION_ENVIRONMENT","Environment"),tr("PLANET_ACTION_TERRAFORM","Terraform"),tr("PLANET_ACTION_POLICIES","Policies")};
    const std::array<std::string,5> descriptions={v.foreign_settlement?tr("PLANET_DESC_STRUCTURES","Inspect structures"):tr("PLANET_DESC_BUILD","Construct structures"),v.foreign_settlement?tr("PLANET_DESC_COLONY_FOREIGN","Inspect colony"):tr("PLANET_DESC_COLONY","Manage colony"),tr("PLANET_DESC_ENVIRONMENT","Planet conditions"),tr("PLANET_DESC_TERRAFORM","Research required"),tr("PLANET_DESC_POLICIES","Not yet available")};
    const float aw=(l.actions.width-102*s)/5;
    for(int i=0;i<5;++i){UiRect r{l.actions.x+i*aw,l.actions.y,aw-8*s,l.actions.height};button(out,r,actions[i],{},l,i<3&&(i==2||!v.observer_only),{},action_art_?action_art_(i):Picture{},descriptions[i]);hits_.back().tab=i==0?1:i==1?0:3;}
    button(out,l.save,tr("PLANET_SAVE","Save"),{PlanetaryAction::Save},l);
    label(out,l.notice,notice_.empty()?(v.foreign_settlement?trf("PLANET_NOTICE_FOREIGN",{v.owner_name},"Developer inspection · {0} · Live statistics"):v.observer_only?(v.developer_inspection?tr("PLANET_NOTICE_UNSETTLED","Unsettled world · No colony population or structures."):tr("PLANET_NOTICE_SURVEY","Survey information only. Colony actions require an owned settlement.")):alerts(v)):notice_,l.small,notice_.empty()?muted:cyan);
    if(!modal())if(const auto hovered=globe_.hit(pointer_,l.globe)){const auto& region=globe_.regions()[*hovered];const float tw=std::min(235*s,l.globe.width),th=68*s;UiRect tip{std::clamp(pointer_.x+14*s,l.globe.x,l.globe.x+l.globe.width-tw),std::clamp(pointer_.y+18*s,l.globe.y,l.globe.y+l.globe.height-th),tw,th};panel(out,tip,s);label(out,{tip.x+10*s,tip.y+8*s,tw-20*s,24*s},region.name,l.small,cyan);label(out,{tip.x+10*s,tip.y+34*s,tw-20*s,25*s},region.terrain,l.small,ink);}
    if(modal())render_confirmation(out,l);
  }

 private:
  struct Hit{UiRect rect;int id{};PlanetaryCommand command;bool enabled{true};int select{-1},tab{-1},control{-1};};
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const{
    if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
    return std::string(fallback);
  }
  [[nodiscard]] std::string trf(std::string_view key,std::initializer_list<std::string> args,std::string_view fallback)const{
    if(locale_&&locale_->contains(key)){
      const std::vector<std::string> values(args.begin(),args.end());
      return locale_->format(key,std::span<const std::string>(values));
    }
    std::string out{fallback};std::size_t index=0;
    for(const auto& arg:args){const std::string marker="{"+std::to_string(index++)+"}";if(const auto at=out.find(marker);at!=std::string::npos)out.replace(at,marker.size(),arg);}
    return out;
  }
  const stellar::engine::LocalizationTable *locale_{};
  static constexpr Color bad{255,156,137,255};
  mutable NativePlanetGlobe globe_;int layer_{0};
  Art art_;std::function<Picture(int)> action_art_;Picture portrait_;std::function<TextExtent(const Text&)> measure_;
  std::pair<std::uint64_t,int> identity_{};int selected_{-1},tab_{2};mutable stellar::engine::ScrollView fact_scroll_{},detail_scroll_{},slot_scroll_{},queue_scroll_{};
  mutable float fact_height_{},detail_height_{},slot_height_{},queue_height_{};mutable std::vector<Hit> hits_;std::optional<Hit> pressed_;Point pointer_{};std::string notice_;
  std::variant<std::monostate,NativeSurfacePlacementQuote,NativeSurfaceManagementQuote,NativeSurfaceRemovalQuote> pending_;
  static std::string number(double n,int precision=1){std::ostringstream o;o<<std::fixed<<std::setprecision(precision)<<n;return o.str();}
  static std::string signed_number(double n){return (n>0?"+":"")+number(n,2);}
  std::string population(double n)const{return std::abs(n)>=1000?number(n/1000,2)+tr("PLANET_POP_BILLIONS"," B"):number(n,2)+tr("PLANET_POP_MILLIONS"," M");}
  std::string atmosphere(stellar::core::PlanetaryAtmosphereRegime v)const{const std::array<const char*,7> keys={"PLANET_ATMO_VACUUM","PLANET_ATMO_OXYGEN","PLANET_ATMO_OXYGEN_RICH","PLANET_ATMO_CO2","PLANET_ATMO_REDUCING","PLANET_ATMO_INERT","PLANET_ATMO_OTHER"};const std::array<const char*,7> names={"Vacuum","Oxygen / nitrogen","Oxygen rich","Carbon dioxide rich","Reducing","Inert","Other"};const auto i=static_cast<std::size_t>(v);return i<names.size()?tr(keys[i],names[i]):tr("PLANET_ATMO_SURVEYED","Surveyed atmosphere");}
  std::string solvent(stellar::core::PlanetarySolventRegime v)const{const std::array<const char*,5> keys={"PLANET_SOLVENT_NONE","PLANET_SOLVENT_WATER","PLANET_SOLVENT_AMMONIA","PLANET_SOLVENT_HYDROCARBON","PLANET_SOLVENT_OTHER"};const std::array<const char*,5> names={"None","Water","Ammonia","Hydrocarbons","Other"};const auto i=static_cast<std::size_t>(v);return i<names.size()?tr(keys[i],names[i]):tr("PLANET_SOLVENT_SURVEYED","Surveyed solvent");}
  std::string world_class(const NativeColonyView& v)const{if(v.planet.appearance&&!v.planet.appearance->source_asset_id.starts_with("sol:"))return stellar::core::planet_appearance_display_name(*v.planet.appearance);return v.planet.world_class?std::string(stellar::core::planetary_world_class_name(*v.planet.world_class)):tr("PLANET_CLASS_UNKNOWN","Classification unknown");}
  std::string classification(const NativeColonyView& v)const{
    using K=stellar::native_system::NativeSystemBodyVisualClass;
    switch(v.planet.visual_class){case K::oceanic:case K::frozen:case K::hot_rocky:return tr("PLANET_CLASS_ROCKY","Rocky world");case K::gas_giant:return tr("PLANET_CLASS_GAS","Gas giant");case K::ice_giant:return tr("PLANET_CLASS_ICE","Ice giant");case K::moon:return tr("PLANET_CLASS_MOON","Rocky moon");case K::unknown_planet:case K::unknown_moon:return tr("PLANET_CLASS_UNSURVEYED","Unsurveyed world");default:return tr("PLANET_CLASS_ROCKY","Rocky world");}
  }
  static void label(DrawList& out,UiRect r,std::string value,int size,Color color,std::optional<UiRect> clip={}){out.overlay.emplace_back(Text{{r.x,r.y},std::move(value),color,size,r.width,clip.value_or(r),TextAlign::Left,FontFace::Interface});}
  static UiRect intersection(UiRect a,UiRect b){const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};}
  float wrapped(DrawList& out,UiRect r,UiRect clip,std::string value,int font,Color color)const{
    Text t{{r.x,r.y},std::move(value),color,font,r.width,clip,TextAlign::Left,FontFace::Interface};
    float h=static_cast<float>(font+5);if(measure_)h=static_cast<float>(measure_(t).height)+5;
    else {int lines=1;float count=0,cols=std::max(1.f,r.width/(font*.55f));for(char c:t.value){if(c=='\n'){++lines;count=0;}else if(++count>cols){++lines;count=0;}}h=lines*(font+5.f);}
    out.overlay.emplace_back(std::move(t));return std::max(h,font+5.f);
  }
  void button(DrawList& out,UiRect r,std::string title,PlanetaryCommand command,const PlanetaryLayout& l,bool enabled=true,std::optional<UiRect> clip={},Picture icon={},std::string subtitle={})const{
    const UiRect visible=clip?intersection(r,*clip):r;if(visible.height<=0||visible.width<=0)return;
    stellar::engine::ui_skin::control(out,r,visible.contains(pointer_),false,enabled,l.s,visible);
    const int font=r.width<110*l.s||r.height<30*l.s?l.small:l.font;const float icon_size=subtitle.empty()?28*l.s:42*l.s,pad=icon?icon_size+18*l.s:8*l.s;
    if(icon)out.overlay.emplace_back(Image{icon,{r.x+8*l.s,r.y+(r.height-icon_size)*.5f,icon_size,icon_size},{},{255,255,255,static_cast<std::uint8_t>(enabled?255:110)},visible});
    label(out,{r.x+pad,r.y+(subtitle.empty()?(r.height-font)*.5f:16*l.s),r.width-pad-8*l.s,r.height},std::move(title),font,enabled?stellar::native_menu_style::ink:stellar::native_menu_style::muted,visible);
    if(!subtitle.empty())label(out,{r.x+pad,r.y+40*l.s,r.width-pad-8*l.s,24*l.s},std::move(subtitle),l.small,stellar::native_menu_style::muted,visible);
    hits_.push_back({visible,static_cast<int>(hits_.size()),std::move(command),enabled});
  }
  static void scrollbar(DrawList& out,UiRect r,const stellar::engine::ScrollView& scroll){const auto thumb=scroll.thumb(r.height,14.f);if(thumb.size<=0)return;out.overlay.emplace_back(FilledRectangle{{r.x+r.width-3,r.y+thumb.offset,3,thumb.size},{105,157,178,210}});}
  static int art_index(std::string_view type){const auto family=stellar::core::surface_functional_family(type);if(family=="power_generator")return 0;if(family=="science_lab")return 1;if(family=="fabricator")return 2;if(family=="trade_hub")return 3;if(family=="habitat_complex")return 4;if(family=="controlled_agriculture")return 5;if(family=="water_reclamation")return 6;if(family=="grid_battery")return 7;return 8;}
  void building_art(DrawList& out,std::string_view type,UiRect r,UiRect clip)const{if(!art_)return;auto image=art_(true);if(!image)return;const int i=art_index(type);const float w=image->width()/3.f,h=image->height()/3.f;out.overlay.emplace_back(Image{image,r,UiRect{(i%3)*w,(i/3)*h,w,h},{255,255,255,255},clip});}
  std::string state(const NativeSurfaceSite& b)const{return !b.complete?trf("PLANET_STATE_BUILDING",{number(b.progress_fraction*100,0)},"Building {0}%"):b.upgrade_days_remaining>0?tr("PLANET_STATE_UPGRADING","Upgrading"):!b.enabled?tr("PLANET_STATE_DISABLED","Disabled"):b.condition<=.15?tr("PLANET_STATE_REPAIRS","Repairs required"):!b.powered?tr("PLANET_STATE_POWER","Power needed"):!b.staffed?tr("PLANET_STATE_WORKERS","Workers needed"):tr("PLANET_STATE_OPERATIONAL","Operational");}
  void render_slots(DrawList& out,const NativeColonyView& v,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;const float s=l.s;panel(out,l.slots,s);
    label(out,{l.slots.x+10*s,l.slots.y+10*s,l.slots.width-20*s,28*s},trf("PLANET_SLOTS_TITLE",{std::to_string(v.construction_sites.size()),std::to_string(v.building_capacity)},"BUILDING SLOTS  {0} / {1}"),l.font,cyan);
    const UiRect area{l.slots.x+10*s,l.slots.y+45*s,l.slots.width-20*s,l.slots.height-55*s};
    const int columns=std::clamp(static_cast<int>(area.width/(138*s)),2,3),count=v.building_capacity>0?v.building_capacity:v.resource_outpost?8:16;
    const float pitch=176*s,cw=(area.width-(columns-1)*8*s)/columns;
    slot_height_=std::ceil(count/static_cast<float>(columns))*pitch;
    for(int i=0;i<count;++i){UiRect r{area.x+(i%columns)*(cw+8*s),area.y-slot_scroll_.scroll_offset+(i/columns)*pitch,cw,pitch-8*s};auto clip=intersection(r,area);if(clip.height<=0)continue;
      const auto found=std::ranges::find(v.construction_sites,i,&NativeSurfaceSite::slot_index);const auto* b=found==v.construction_sites.end()?nullptr:&*found;const bool unlocked=i<v.building_capacity;
      out.overlay.emplace_back(FilledRectangle{clip,i==selected_?Color{25,61,75,255}:Color{12,30,43,255}});out.overlay.emplace_back(StrokedRectangle{clip,i==selected_?cyan:Color{55,88,109,255}});
      label(out,{r.x+8*s,r.y+5*s,r.width-16*s,20*s},trf("PLANET_SITE",{std::to_string(i+1)},"SITE {0}"),l.small,muted,clip);
      if(b)building_art(out,b->type_id,{r.x+8*s,r.y+27*s,r.width-16*s,65*s},clip);
      else label(out,{r.x+cw*.43f,r.y+34*s,cw*.5f,46*s},unlocked?"+":"◇",static_cast<int>(34*s),unlocked?cyan:muted,clip);
      label(out,{r.x+8*s,r.y+97*s,r.width-16*s,29*s},b?b->name:unlocked?tr("PLANET_SLOT_AVAILABLE","Available slot"):tr("PLANET_SLOT_LOCKED","Locked slot"),l.small,ink,clip);
      label(out,{r.x+8*s,r.y+133*s,r.width-16*s,30*s},b?state(*b):unlocked?tr("PLANET_CONSTRUCT","Construct"):tr("PLANET_LOCKED","Locked"),l.small,b&&(!b->powered||!b->staffed)?bad:cyan,clip);
      if(b&&!b->complete){UiRect bar{r.x+5*s,r.y+r.height-4*s,static_cast<float>((r.width-10*s)*b->progress_fraction),3*s};out.overlay.emplace_back(FilledRectangle{intersection(bar,area),gold});}
      hits_.push_back({clip,static_cast<int>(hits_.size()),{},unlocked,i,-1});
    }
    slot_scroll_.sync(slot_height_+55*s,area.height);scrollbar(out,area,slot_scroll_);
  }
  void render_right(DrawList& out,const NativeColonyView& v,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;const float s=l.s;panel(out,l.right,s);
    const auto region=globe_.selected()>=0?&globe_.regions()[globe_.selected()]:nullptr;
    label(out,{l.right.x+12*s,l.right.y+10*s,l.right.width-24*s,28*s},region?region->name:v.foreign_settlement?tr("PLANET_TITLE_INSPECT","PLANET INSPECTION"):tr("PLANET_TITLE_COMMAND","PLANETARY COMMAND"),l.heading,ink);
    label(out,{l.right.x+12*s,l.right.y+42*s,l.right.width-24*s,38*s},region?region->terrain:v.observer_only?tr("PLANET_SUB_INTEL","Surveyed intelligence"):tr("PLANET_SUB_OPS","Colony operations and development"),l.small,cyan);
    if(!v.observer_only&&v.surface_hub_level>0&&v.planet.sol_texture_key==std::optional<std::string>{"earth"}&&art_){
      if(const auto image=art_(false)){
        out.overlay.emplace_back(Image{image,l.region_art,{},{255,255,255,255},l.region_art});
        const UiRect caption{l.region_art.x,l.region_art.y+l.region_art.height-20*s,l.region_art.width,20*s};
        out.overlay.emplace_back(FilledRectangle{caption,{2,12,22,215}});
        label(out,{caption.x+6*s,caption.y+2*s,caption.width-12*s,caption.height},tr("PLANET_ART_CAPTION","Colony development · illustration"),l.small,muted,caption);
      }
    }else if(portrait_){
      stellar::engine::ui_skin::surface(out,l.region_art,s);
      const float side=l.region_art.height-8*s;
      out.overlay.emplace_back(Image{portrait_,{l.region_art.x+(l.region_art.width-side)*.5f,l.region_art.y+4*s,side,side},{},{255,255,255,255},l.region_art});
    }
    button(out,l.command,v.foreign_settlement?tr("PLANET_CMD_INSPECT","Developer inspection"):v.observer_only?tr("PLANET_CMD_NONE","No owned settlement"):v.hub_upgrade_days_remaining>0?tr("PLANET_CMD_PROGRESS","Command Center in progress"):v.surface_hub_level==0?tr("PLANET_CMD_BUILD","Build Command Center"):tr("PLANET_CMD_UPGRADE","Upgrade Command Center"),{PlanetaryAction::CommandCenter},l,!v.observer_only&&!v.foreign_settlement&&v.hub_upgrade_available&&v.can_afford_hub_upgrade);
    float qy=l.queue.y-queue_scroll_.scroll_offset;int queued=0;
    if(v.hub_upgrade_days_remaining>0){qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,trf("PLANET_QUEUE_HUB",{stellar::native_campaign::format_campaign_duration(v.hub_upgrade_days_remaining)},"Command Center · {0}*"),l.small,gold);++queued;}
    for(const auto& b:v.construction_sites)if(!b.complete||b.upgrade_days_remaining>0){qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,!b.complete?trf("PLANET_QUEUE_PROGRESS",{b.name,number(b.progress_fraction*100,0)},"{0} · {1}%"):trf("PLANET_QUEUE_UPGRADE",{b.name,number(b.upgrade_days_remaining,1)},"{0} · {1} days*"),l.small,ink);++queued;}
    if(queued==0)qy+=wrapped(out,{l.queue.x,qy,l.queue.width,0},l.queue,v.observer_only&&!v.developer_inspection?tr("PLANET_QUEUE_INTEL","Construction intelligence unavailable."):tr("PLANET_QUEUE_EMPTY","No construction scheduled."),l.small,muted);
    queue_height_=qy-l.queue.y-queue_scroll_.scroll_offset;queue_scroll_.sync(queue_height_,l.queue.height);scrollbar(out,l.queue,queue_scroll_);
    const std::array<const char*,4> caption_keys={"PLANET_TAB_ECONOMY","PLANET_TAB_STRUCTURES","PLANET_TAB_OVERVIEW","PLANET_TAB_CLIMATE"};
    const std::array<const char*,4> caption_names={"Economy","Structures","Overview","Climate"};
    const std::array<std::string,4> captions{tr(caption_keys[0],caption_names[0]),tr(caption_keys[1],caption_names[1]),tr(caption_keys[2],caption_names[2]),tr(caption_keys[3],caption_names[3])};
    for(int i=0;i<4;++i){UiRect r{l.tabs.x+i*l.tabs.width*.25f,l.tabs.y,l.tabs.width*.25f,l.tabs.height};button(out,r,captions[i],{},l,!v.observer_only||i>=2);hits_.back().tab=i;if(tab_==i)out.overlay.emplace_back(FilledRectangle{{r.x,r.y+r.height-2*s,r.width,2*s},cyan});}
    if(tab_==1&&selected_<0&&!v.observer_only){render_slots(out,v,l);return;}
    float y=l.details.y-detail_scroll_.scroll_offset;const auto write=[&](std::string value,Color color=ink,int size=0){y+=wrapped(out,{l.details.x,y,l.details.width-7*s,0},l.details,std::move(value),size?size:l.small,color)+7*s;};
    const auto action=[&](std::string title,PlanetaryCommand cmd,bool enabled=true){button(out,{l.details.x,y,l.details.width-7*s,36*s},std::move(title),cmd,l,enabled&&(!v.foreign_settlement||cmd.action==PlanetaryAction::None),l.details);y+=44*s;};
    if(tab_==2){
      write(region?tr("PLANET_OVERVIEW_REGION","REGION OVERVIEW"):tr("PLANET_OVERVIEW_WORLD","WORLD OVERVIEW"),cyan,l.font);
      if(region){write(region->terrain);write(tr("PLANET_REGION_DESC","Geographic survey province. Population and deposits are currently recorded at planetary level."),muted);}
      write(classification(v));
      write(world_class(v),cyan);
      if(v.observer_only){write(v.developer_inspection?tr("PLANET_UNSETTLED","Unsettled world · Population 0. No colony structures or construction."):tr("PLANET_OBSERVER_LIMITED","Colony population, structures and military installations are not available to this observer."),gold);}
      else {
        write(trf("PLANET_POPULATION_LINE",{population(v.population_millions)},"PLANET POPULATION  {0}"),ink,l.font);write(trf("PLANET_INFRA_STABILITY",{number(v.infrastructure*100,0),number(v.stability*100,0)},"Infrastructure  {0}%\nStability  {1}%"));
        write(tr("PLANET_ALERTS_HEADING","ALERTS"),cyan,l.font);write(alerts(v),v.sustenance_support_ratio<1||v.operating_funding<.999?bad:muted);
        action(tr("PLANET_REVIEW_ECONOMY","Review colony economy"),{},true);hits_.back().tab=0;
        write(trf("PLANET_HUB_COST",{v.currency.format(v.hub_upgrade_credit_budget_units),number(v.hub_upgrade_industry_cost,0),v.hub_upgrade_lock_reason},"Command Center: {0} + {1} materials\n{2}"));
        write(trf("PLANET_STRUCTURES_LINE",{std::to_string(v.construction_sites.size()),std::to_string(v.building_capacity)},"STRUCTURES  {0} / {1}"),cyan,l.font);
        for(const auto& site:v.construction_sites){action(trf("PLANET_SITE_STATE",{site.name,state(site)},"{0} · {1}"),{});hits_.back().select=site.slot_index;}
        action(tr("PLANET_MANAGE","Manage structures"),{});hits_.back().tab=1;
      }
    }else if(tab_==3){
      write(tr("PLANET_ENV_TITLE","ENVIRONMENT"),cyan,l.font);
      if(!v.planet.details)write(tr("PLANET_ENV_UNSURVEYED","UNSURVEYED · Send a science vessel for environmental readings."),gold);
      else {const auto& d=*v.planet.details;write(trf("PLANET_SURFACE",{d.has_solid_surface?tr("PLANET_SURFACE_SOLID","Solid"):tr("PLANET_SURFACE_NONE","No solid surface")},"Surface  {0}"));write(trf("PLANET_ATMOSPHERE",{atmosphere(d.atmosphere)},"Atmosphere  {0}"));write(trf("PLANET_PRESSURE",{number(d.pressure_kpa,1)},"Pressure  {0} kPa"));write(trf("PLANET_TEMPERATURE",{number(d.temperature_kelvin,1)},"Temperature  {0} K"));write(trf("PLANET_SOLVENT",{solvent(d.available_solvent)},"Solvent  {0}"));write(trf("PLANET_RADIATION",{number(d.radiation_hazard*100,0)},"Radiation hazard  {0}%"));
        if(d.stellar_exposure)write(tr("PLANET_STELLAR_LIMIT","Stellar conditions constrain surface development."));
        if(!d.has_solid_surface)write(tr("PLANET_GAS_GIANT","Gas giants support atmospheric and orbital investigation. Ground construction is unavailable."),gold);
      }
      write(tr("PLANET_CLIMATE_UNAVAILABLE","Regional climate, pollution and terraforming simulation are not available."),muted);
    }else if(tab_==0&&!v.observer_only){
      write(tr("PLANET_PROD_TITLE","PRODUCTION & REQUIREMENTS"),cyan,l.font);write(tr("PLANET_PROD_NOTE","Rates use game days. Food, water and housing are population-support capacities."),muted);
      const auto money=[&](double n){return v.currency.format_rate(n);};
      write(trf("PLANET_PROD_TAX",{money(v.local_credit_flow.colony_revenue_per_day)},"Tax revenue  {0}"));write(trf("PLANET_PROD_TRADE",{money(v.local_credit_flow.trade_revenue_per_day)},"Trade revenue  {0}"));write(trf("PLANET_PROD_OPERATING",{money(v.local_credit_flow.operating_costs_per_day)},"Local operating need  {0}"));
      write(trf("PLANET_PROD_BREAKDOWN",{money(v.local_credit_flow.colony_administration_per_day),money(v.local_credit_flow.population_services_per_day),money(v.local_credit_flow.habitat_support_per_day),money(v.local_credit_flow.surface_maintenance_per_day)},"Administration  {0}\nServices  {1}\nHabitats  {2}\nBuildings  {3}"),muted);
      write(trf("PLANET_PROD_LABS",{number(v.active_research_lab_units,2)},"Operational labs  {0}"));double materials=0;for(const auto& b:v.construction_sites)if(!b.complete)materials+=b.remaining_construction_materials;write(trf("PLANET_PROD_QUEUED",{number(materials,1)},"Queued materials  {0}"));
      write(trf("PLANET_PROD_POWER",{number(v.power_supply,1),number(v.power_demand,1)},"Power  {0} supply / {1} demand"));write(trf("PLANET_PROD_STORAGE",{number(v.stored_power_days,1),number(v.power_storage_capacity_days,1),number(v.storage_charge_per_day,1),number(v.storage_discharge_per_day,1)},"Stored power  {0} / {1} power-days\nCharge / discharge  {2} / {3}"));
      const std::array<const char*,3> capacity_keys={"PLANET_CAP_FOOD","PLANET_CAP_WATER","PLANET_CAP_HOUSING"};
      const std::array<const char*,3> capacity_names={"Food","Water","Housing"};
      const std::array<std::pair<const char*,double>,3> capacity_pairs{{{"Food",v.food_capacity_millions},{"Water",v.water_capacity_millions},{"Housing",v.housing_capacity_millions}}};
      for(std::size_t i=0;i<3;++i){
        const auto& pair=capacity_pairs[i];
        write(trf("PLANET_CAP_LINE",{tr(capacity_keys[i],capacity_names[i]),population(pair.second),population(v.population_millions),pair.second<v.population_millions?tr("PLANET_DEFICIT","Deficit "):tr("PLANET_SURPLUS","Surplus "),population(std::abs(pair.second-v.population_millions))},"{0}  {1} / {2}\n{3}{4}"),pair.second<v.population_millions?bad:ink);
      }
      write(trf("PLANET_RESERVES",{number(v.food_reserve_days,1),number(v.water_reserve_days,1)},"Food / water reserves  {0} / {1} days"));write(trf("PLANET_WORKERS",{population(v.workforce_available_millions),population(v.workforce_demand_millions)},"Workers  {0} available / {1} required"));write(trf("PLANET_EMPLOYMENT",{number(v.employment_rate*100,1),population(v.working_age_population_millions),population(v.employed_population_millions)},"Employment  {0}%\nWorking age  {1}\nEmployed  {2}"));
      write(trf("PLANET_CARGO",{number(v.cargo_transfer_capacity_per_day,1)},"Cargo transfer  {0} materials/day"));if(v.resource_outpost){write(trf("PLANET_DEPOSIT",{v.deposit_material_name,v.deposit_grade,number(v.remaining_deposit_materials,0),number(v.extraction_per_day,2),number(v.stored_extracted_materials,1),number(v.extracted_material_capacity,1)},"Deposit  {0} · {1}\nRemaining  {2} materials\nExtraction  {3} / day\nExtracted stores  {4} / {5}"));action(tr("PLANET_COLLECT","Collect materials"),{PlanetaryAction::Freight});}
      write(trf("PLANET_FUNDING",{number(v.operating_funding*100,1)},"Operating funding  {0}%"),v.operating_funding<.999?bad:ink);write(trf("PLANET_TREASURY",{v.formatted_treasury,number(v.stored_industry,1),money(v.empire_credit_flow),signed_number(v.empire_industry_flow),v.currency.format(v.operating_arrears)},"Empire treasury  {0}\nConstruction stores  {1} materials\nEmpire credit flow  {2}\nEmpire material output  {3} / day\nOperating arrears  {4}"));
    }else if(selected_<0){write(tr("PLANET_MGMT_TITLE","BUILDING MANAGEMENT"),cyan,l.font);write(tr("PLANET_MGMT_HINT","Select an empty slot to construct a building, or an occupied slot to manage it.\n\nConstruction consumes materials over time. Pausing the campaign stops progress."));}
    else {
      action(tr("PLANET_ALL_STRUCTURES","‹ All structures"),{});hits_.back().tab=1;
      write(trf("PLANET_SITE",{std::to_string(selected_+1)},"SITE {0}"),cyan,l.font);const auto it=std::ranges::find(v.construction_sites,selected_,&NativeSurfaceSite::slot_index);
      if(it==v.construction_sites.end()){
        write(tr("PLANET_CONSTRUCT_TITLE","CONSTRUCT A BUILDING"),ink,l.font);write(tr("PLANET_CONSTRUCT_HINT","Credits authorize construction. Materials are drawn from shared empire stores over time. The selected slot is reserved immediately."),muted);
        for(const auto& option:v.available_buildings){
          const float top=y,tx=l.details.x+90*s,tw=l.details.width-97*s;
          building_art(out,option.type_id,{l.details.x,top,78*s,70*s},l.details);
          y+=wrapped(out,{tx,y,tw,0},l.details,option.name,l.font,gold)+4*s;
          y+=wrapped(out,{tx,y,tw,0},l.details,trf("PLANET_OPTION_COST",{option.formatted_authorization,number(option.industry_cost,0)},"{0}\n{1} materials"),l.small,ink);
          y=std::max(y,top+70*s)+10*s;
          write(option.description);write(trf("PLANET_OPTION_POWER",{number(option.power_supply,1),number(option.power_demand,1),population(option.workforce_required_millions)},"Power +{0} / -{1} · Workers {2}"),muted);
          action(tr("PLANET_BEGIN","Begin construction"),{PlanetaryAction::Build,selected_,0,option.type_id},!v.observer_only&&v.solid_surface&&selected_<v.building_capacity&&v.treasury_budget_units+.0001>=option.authorization_budget_units);
          y+=10*s;
        }
      }else {const auto& b=*it;building_art(out,b.type_id,{l.details.x,y,l.details.width-7*s,110*s},l.details);y+=120*s;write(b.name,gold,l.heading);write(state(b),cyan);write(trf("PLANET_CONDITION",{number(b.condition*100,0),number(b.efficiency*100,0),b.powered?tr("PLANET_POWERED","Powered"):tr("PLANET_POWER_OFF","Power unavailable"),b.staffed?tr("PLANET_STAFFED","Staffed"):tr("PLANET_STAFF_NEEDED","Workers needed")},"Condition {0}% · Efficiency {1}%\n{2} · {3}"));if(!b.complete)write(trf("PLANET_MATERIALS_LEFT",{b.construction_stage,number(b.remaining_construction_materials,1)},"{0}\n{1} materials remaining"));if(b.upgrade_days_remaining>0)write(trf("PLANET_DAYS_LEFT",{number(b.upgrade_days_remaining,1)},"{0} days remaining at full funding"));
        if(b.can_upgrade){write(trf("PLANET_UPGRADE_COST",{b.upgrade_name,v.currency.format(b.upgrade_credit_budget_units),number(b.upgrade_industry_cost,0),b.upgrade_lock_reason},"Upgrade: {0}\n{1} + {2} materials\n{3}"));action(tr("PLANET_UPGRADE_ACTION","Upgrade building"),{PlanetaryAction::Upgrade,selected_,b.building_id},b.can_afford_upgrade&&b.upgrade_lock_reason.empty());}
        if(b.complete){action(b.enabled?tr("PLANET_DISABLE","Disable building"):tr("PLANET_ENABLE","Enable building"),{PlanetaryAction::Enable,selected_,b.building_id,{},!b.enabled});action(b.prioritized?tr("PLANET_PRIORITY_NORMAL","Use normal priority"):tr("PLANET_PRIORITIZE","Prioritize operations"),{PlanetaryAction::Priority,selected_,b.building_id,{},!b.prioritized});action(trf("PLANET_REPAIR",{number(b.repair_industry_cost,1)},"Repair · {0} materials"),{PlanetaryAction::Repair,selected_,b.building_id},b.can_afford_repair&&b.condition<.999999);}
        action(b.complete?tr("PLANET_DEMOLISH","Demolish building…"):tr("PLANET_CANCEL_CONSTRUCTION","Cancel construction…"),{PlanetaryAction::Remove,selected_,b.building_id});
      }
    }
    detail_height_=y-l.details.y-detail_scroll_.scroll_offset;detail_scroll_.sync(detail_height_,l.details.height);scrollbar(out,l.details,detail_scroll_);
  }
  std::string alerts(const NativeColonyView& v)const{std::string r;if(v.surface_hub_level==0)r+=tr("PLANET_ALERT_HUB","COMMAND CENTER REQUIRED.  ");if(v.power_supply+v.storage_discharge_per_day<v.power_demand)r+=tr("PLANET_ALERT_POWER","POWER DEFICIT.  ");if(v.workforce_demand_millions>v.workforce_available_millions)r+=tr("PLANET_ALERT_WORKERS","WORKERS NEEDED.  ");if(v.sustenance_support_ratio<1)r+=tr("PLANET_ALERT_LIFE","LIFE SUPPORT DEFICIT.  ");if(v.operating_funding<.999)r+=tr("PLANET_ALERT_FUNDING","OPERATIONS UNDERFUNDED.  ");return r.empty()?(v.foreign_settlement?tr("PLANET_ALERT_NONE_FOREIGN","No critical needs. Inspect structures or the colony economy for details."):tr("PLANET_ALERT_NONE","Select a slot to build or manage.  * Construction estimates assume full funding.")):r;}
  void render_confirmation(DrawList& out,const PlanetaryLayout& l)const{
    using namespace stellar::native_menu_style;out.overlay.emplace_back(FilledRectangle{l.screen,{0,3,8,185}});panel(out,l.modal,l.s);hits_.clear();
    std::string title,body;bool accepted=false;
    std::visit([&](const auto& q){using T=std::decay_t<decltype(q)>;if constexpr(!std::is_same_v<T,std::monostate>){accepted=q.accepted;title=q.building_name;body=q.message;if constexpr(std::is_same_v<T,NativeSurfacePlacementQuote>)body=trf("PLANET_CONFIRM_PLACE",{q.formatted_authorization,number(q.industry_cost,0),q.message},"{0} authorization\n{1} construction materials over time\n\n{2}");else if constexpr(std::is_same_v<T,NativeSurfaceManagementQuote>)body=trf("PLANET_CONFIRM_MANAGE",{q.description,q.formatted_authorization,number(q.industry_cost,1),q.message},"{0}\n\n{1} + {2} materials\n\n{3}");}},pending_);
    label(out,{l.modal.x+20*l.s,l.modal.y+18*l.s,l.modal.width-40*l.s,40*l.s},title,l.heading,cyan);
    const UiRect body_rect{l.modal.x+20*l.s,l.modal.y+66*l.s,l.modal.width-40*l.s,l.cancel.y-l.modal.y-78*l.s};wrapped(out,body_rect,body_rect,body,l.font,ink);
    button(out,l.cancel,tr("PLANET_CANCEL","Cancel"),{PlanetaryAction::Cancel},l);button(out,l.confirm,tr("PLANET_CONFIRM","Confirm"),{PlanetaryAction::Confirm},l,accepted);
  }
};
}
