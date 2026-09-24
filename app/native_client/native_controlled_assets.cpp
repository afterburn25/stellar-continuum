#include "native_controlled_assets.hpp"
#include "native_menu_style.hpp"
#include "native_ui_layout.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_assets {
using namespace stellar::native_map;
namespace {
constexpr Color ink{229,241,249,255}, muted{146,180,201,255}, cyan{94,212,244,255}, green{106,218,159,255}, amber{255,195,100,255};
constexpr std::array names{"PLANETS","OUTPOSTS","FLEETS","SPACE STATIONS","SHIPYARDS"};
constexpr std::array name_keys{"ASSETS_CAT_PLANETS","ASSETS_CAT_OUTPOSTS","ASSETS_CAT_FLEETS","ASSETS_CAT_STATIONS","ASSETS_CAT_SHIPYARDS"};
std::string folded(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
std::string resolve(const stellar::engine::LocalizationTable* locale,std::string_view key,std::string_view fallback){
  if(locale&&locale->contains(key))return std::string(locale->translate(key));
  return std::string(fallback);}
std::string resolved(const stellar::engine::LocalizationTable* locale,std::string_view key,std::initializer_list<std::string> args,std::string_view fallback){
  if(locale&&locale->contains(key)){const std::vector<std::string> values(args.begin(),args.end());return locale->format(key,std::span<const std::string>(values));}
  std::string out{fallback};std::size_t index=0;for(const auto& arg:args){const std::string marker="{"+std::to_string(index++)+"}";if(const auto at=out.find(marker);at!=std::string::npos)out.replace(at,marker.size(),arg);}return out;}
std::string role(stellar::core::FleetRole r,const stellar::engine::LocalizationTable* locale){using stellar::core::FleetRole;switch(r){case FleetRole::Scout:return resolve(locale,"ASSETS_ROLE_SCOUT","Scout");case FleetRole::Science:return resolve(locale,"ASSETS_ROLE_SCIENCE","Science");case FleetRole::Colony:return resolve(locale,"ASSETS_ROLE_COLONY","Colony");case FleetRole::Military:return resolve(locale,"ASSETS_ROLE_MILITARY","Military");case FleetRole::Logistics:return resolve(locale,"ASSETS_ROLE_LOGISTICS","Logistics");}return resolve(locale,"ASSETS_ROLE_FLEET","Fleet");}
UiRect intersect(UiRect a,UiRect b){const auto x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0.f,std::min(a.x+a.width,b.x+b.width)-x),std::max(0.f,std::min(a.y+a.height,b.y+b.height)-y)};}
void label(DrawList& out,UiRect r,std::string value,Color color,int size,UiRect clip){clip=intersect(r,clip);if(clip.width>0&&clip.height>0)out.overlay.emplace_back(Text{{r.x,r.y},std::move(value),color,size,r.width,clip});}
}
View build(const stellar::core::FreshCampaignState& world,const stellar::native_colony_roster::View& colonies,
    const stellar::native_fleet::NativeFleetMapView& fleets,const stellar::native_shipyard::NativeShipyardView* yard,
    const std::function<std::string(int)>& system_name,
    const stellar::engine::LocalizationTable* locale){
  using namespace stellar::core;
  View result{colonies.generation,world.player_civilization_id,{}};
  if(!colonies.available||colonies.player_id!=result.observer||fleets.player_civilization_id!=result.observer||fleets.campaign_generation!=result.generation)return result;
  std::unordered_map<int,const Colony*> colony_index;for(const auto& c:world.colonies)if(c.civilization_id==result.observer)colony_index.emplace(c.id,&c);
  std::unordered_set<int> seen;
  for(const auto& c:colonies.rows){const auto it=colony_index.find(c.colony_id);if(it==colony_index.end()||!seen.insert(c.colony_id).second)continue;
    const auto& source=*it->second;Row r;r.key={source.kind==SettlementKind::ResourceOutpost?Category::Outposts:Category::Planets,c.colony_id};
    r.system_id=c.system_id;r.body_id=c.body_id;r.name=c.name;r.detail=c.kind_label+" · "+c.population;r.activity=c.system_name;r.actionable=c.can_open;
    r.tooltip=c.can_open?resolved(locale,"ASSETS_TIP_COLONY",{c.name,c.system_name,c.population},"{0}\n{1} · {2} population\nOwned · Click to focus; double-click or › to manage."):c.reason;
    if(source.stability<.35){r.severity=2;r.tooltip+=resolve(locale,"ASSETS_WARN_CRITICAL","\nCritical: colony stability below 35%.");}
    else if(source.stability<.6){r.severity=1;r.tooltip+=resolve(locale,"ASSETS_WARN_STABILITY","\nWarning: colony stability below 60%.");}
    if(source.population_millions>0&&source.stored_food_population_days_millions/source.population_millions<1.){r.severity=std::max(r.severity,1);r.tooltip+=resolve(locale,"ASSETS_WARN_FOOD","\nFood reserves below one day.");}
    result.rows.push_back(std::move(r));
  }
  seen.clear();std::unordered_map<int,const FleetState*> fleet_index;for(const auto& f:world.fleets)if(f.is_active&&f.civilization_id==result.observer)fleet_index.emplace(f.id,&f);
  for(const auto& f:fleets.own_fleets){const auto found=fleet_index.find(f.id);if(found==fleet_index.end()||!seen.insert(f.id).second)continue;const auto& source=*found->second;
    Row r;r.key={Category::Fleets,f.id};r.name=f.name;r.system_id=f.current_system_id.value_or(-1);
    const auto location=f.current_system_id?system_name(*f.current_system_id):resolve(locale,"ASSETS_EN_ROUTE","En route");
    // Each canonical FleetState currently represents one commissioned vessel.
    r.detail=resolved(locale,"ASSETS_FLEET_DETAIL",{location},"1 ship · {0}");r.activity=role(f.role,locale)+" · ";
    std::string order=resolve(locale,"ASSETS_ORDER_IDLE","Idle");
    if(source.combat&&source.combat->retreat_started)order=resolve(locale,"ASSETS_ORDER_RETREATING","Retreating");
    else if(source.return_to_base_requested)order=resolve(locale,"ASSETS_ORDER_RETURNING","Returning to base");
    else if(source.hold_requested)order=resolve(locale,"ASSETS_ORDER_HOLDING","Holding");
    else if(source.transit_phase!=FleetTransitPhase::None||source.destination_system_id)order=resolve(locale,"ASSETS_ORDER_MOVING","Moving");
    else if(source.freight_target_outpost_id)order=resolve(locale,"ASSETS_ORDER_SUPPLYING","Supplying outpost");
    else if(source.settlement_body_id)order=resolve(locale,"ASSETS_ORDER_SETTLING","Establishing settlement");
    else if(f.science_survey&&!f.science_survey->completed&&!f.science_survey->held)order=resolve(locale,"ASSETS_ORDER_SURVEYING","Surveying");
    else if(f.reconnaissance&&!f.reconnaissance->completed&&!f.reconnaissance->held)order=resolve(locale,"ASSETS_ORDER_EXPLORING","Exploring");
    else if(source.combat){switch(source.combat->order){case MilitaryOrderType::Defend:order=resolve(locale,"ASSETS_ORDER_DEFENDING","Defending");break;case MilitaryOrderType::Attack:order=resolve(locale,"ASSETS_ORDER_ATTACKING","Attacking");break;default:break;}}
    r.activity+=order;r.tooltip=resolved(locale,"ASSETS_TIP_FLEET",{f.name,r.detail,r.activity,std::to_string(static_cast<int>(std::lround(f.combat_power)))},"{0}\n{1}\n{2}\nFleet power: {3}");
    if(source.return_to_base_failure_reason){r.severity=1;r.tooltip+=resolve(locale,"ASSETS_WARN_ROUTE","\nReturn route needs attention.");}
    result.rows.push_back(std::move(r));
  }
  if(yard&&yard->campaign_generation==result.generation&&yard->player_civilization_id==result.observer&&yard->orbital_shipyard_complete){
    Row r;r.key={Category::Shipyards,result.observer};r.system_id=yard->home_system_id;r.name=resolved(locale,"ASSETS_YARD_NAME",{system_name(r.system_id)},"{0} Orbital Shipyard");
    r.detail=resolved(locale,"ASSETS_YARD_ORDERS",{std::to_string(yard->pending_build_count),std::to_string(yard->maximum_pending_builds)},"{0} / {1} orders");r.activity=resolve(locale,"ASSETS_ORDER_IDLE","Idle");
    const auto active=std::ranges::find_if(yard->orders,[](const auto& o){return o.active;});
    if(active!=yard->orders.end()){r.progress=std::clamp(active->progress_fraction,0.,1.);r.activity=resolved(locale,"ASSETS_YARD_BUILDING",{active->design_name},"Building {0}");}
    r.tooltip=resolved(locale,"ASSETS_TIP_YARD",{r.name,r.activity,r.detail},"{0}\n{1}\n{2}\nClick to focus; double-click or › for Ship Construction.");result.rows.push_back(std::move(r));
  }
  for(auto& r:result.rows)r.search=folded(r.name+" "+r.detail+" "+r.activity+" "+system_name(r.system_id));
  std::ranges::stable_sort(result.rows,[](const Row& a,const Row& b){if(a.key.category!=b.key.category)return a.key.category<b.key.category;return a.key.id<b.key.id;});
  return result;
}
Layout Layout::make(int width,int height){
  const float s=std::clamp(height/1080.f,.75f,2.f),w=std::min(340*s,width*.29f),x=width-w-12*s,y=stellar::native_map::native_workspace_top(width,height),h=height-y-80*s;
  const UiRect p{x,y,w,h};return {p,{x+12*s,y+12*s,w-54*s,25*s},{x+w-36*s,y+8*s,27*s,27*s},
    {x+12*s,y+48*s,w-24*s,32*s},{x+w-39*s,y+51*s,24*s,26*s},
    {x+8*s,y+92*s,w-16*s,h-102*s},{width-116*s,y,104*s,32*s},s,72*s,34*s};
}
std::string Navigator::tr(std::string_view key,std::string_view fallback)const{return resolve(locale_,key,fallback);}
std::string Navigator::trf(std::string_view key,std::initializer_list<std::string> args,std::string_view fallback)const{return resolved(locale_,key,args,fallback);}

void Navigator::set_view(View value){
  if(value.generation!=view_.generation||value.observer!=view_.observer){selected_.reset();temporary_reveal_.reset();search_.clear();scroll_=0;cancel_input();}
  const bool changed=value.generation!=view_.generation||value.observer!=view_.observer||value.rows!=view_.rows;
  if(!changed)return;
  const auto targets=[&]{std::vector<std::pair<Category,std::optional<Key>>> keys;for(const auto& e:entries_)keys.emplace_back(e.category,e.row?std::optional(view_.rows[*e.row].key):std::nullopt);return keys;};
  const auto before=pressed_?targets():decltype(targets()){};
  view_=std::move(value);
  if(selected_&&std::ranges::none_of(view_.rows,[&](const auto& r){return r.key==selected_;})){selected_.reset();temporary_reveal_.reset();}
  rebuild();
  // Live progress/status refreshes may arrive between press and release.
  // Cancel only if the visible targets moved, disappeared or changed identity.
  if(pressed_&&before!=targets())pressed_.reset();
}
void Navigator::set_selection(std::optional<Key> key,bool external){if(key==selected_)return;selected_=key;temporary_reveal_=external?key:std::nullopt;reveal_selection_=external&&key.has_value();rebuild();}
void Navigator::rebuild(){
  counts_.fill(0);matches_.fill(0);const auto query=folded(search_);
  for(const auto& r:view_.rows){const auto c=static_cast<std::size_t>(r.key.category);++counts_[c];if(query.empty()||r.search.find(query)!=std::string::npos)++matches_[c];}
  // TreeModel owns the expand/flatten bookkeeping — "c:<category>"
  // header nodes parent "r:<row index>" children, and entries_ is the
  // flattened projection (row index rides the node id, the category
  // ordinal rides the header label_key).
  tree_=stellar::engine::TreeModel{};
  for(int c=0;c<5;++c){if(!matches_[c])continue;const auto category=static_cast<Category>(c);
    const auto header_id="c:"+std::to_string(c);
    auto&header=tree_.add(header_id,std::to_string(c));
    const bool reveal=!query.empty()||(temporary_reveal_&&temporary_reveal_->category==category);
    header.expanded=!preferences_.collapsed[c]||reveal;
    for(std::size_t i=0;i<view_.rows.size();++i)if(view_.rows[i].key.category==category&&(query.empty()||view_.rows[i].search.find(query)!=std::string::npos))tree_.add("r:"+std::to_string(i),{},header_id);
  }
  entries_.clear();
  for(const auto &[node,depth]:tree_.flattened()){
    if(node->id.front()=='c')entries_.push_back({std::nullopt,static_cast<Category>(std::stoi(node->label_key))});
    else{const auto i=static_cast<std::size_t>(std::stoul(node->id.substr(2)));entries_.push_back({i,view_.rows[i].key.category});}
  }
}
float Navigator::extent(const Layout& l)const{float h=0;for(const auto& e:entries_)h+=e.row?l.row_height:l.category_height;return h;}
UiRect Navigator::entry_bounds(std::size_t index,const Layout& l)const{float y=l.list.y-scroll_;for(std::size_t i=0;i<index;++i)y+=entries_[i].row?l.row_height:l.category_height;return {l.list.x,y,l.list.width-5*l.scale,entries_[index].row?l.row_height:l.category_height};}
std::optional<UiRect> Navigator::row_bounds(Key key,int w,int h)const{const auto l=Layout::make(w,h);for(std::size_t i=0;i<entries_.size();++i)if(entries_[i].row&&view_.rows[*entries_[i].row].key==key){const auto r=entry_bounds(i,l);if(intersect(r,l.list).height>0)return r;}return {};}
UiRect Navigator::category_bounds(Category c,int w,int h)const{const auto l=Layout::make(w,h);for(std::size_t i=0;i<entries_.size();++i)if(!entries_[i].row&&entries_[i].category==c)return entry_bounds(i,l);return {};}
// Focusables walk actionable rects in (y,x) order: the hide control, the
// search field (and its clear button while text is present), then every
// entry — category headers and rows — clipped to the list viewport.
// Entry rects carry their entries_ index so scroll-follow can consult the
// unclipped bounds.
std::vector<Navigator::FocusTarget> Navigator::focusables(const Layout& l) const{
  std::vector<FocusTarget> out;
  out.push_back({l.hide,std::nullopt,tr("ASSETS_HIDE","Hide assets panel")});
  out.push_back({l.search,std::nullopt,tr("ASSETS_SEARCH","Search assets")});
  if(!search_.empty())out.push_back({l.clear,std::nullopt,tr("ASSETS_CLEAR","Clear search")});
  for(std::size_t i=0;i<entries_.size();++i)
    if(const auto clip=intersect(entry_bounds(i,l),l.list);clip.height>0.f){
      const auto&e=entries_[i];
      out.push_back({clip,i,e.row?view_.rows[*e.row].name
                                :tr(name_keys[static_cast<int>(e.category)],names[static_cast<int>(e.category)])});
    }
  std::ranges::sort(out,[](const FocusTarget& a,const FocusTarget& b){return a.bounds.y==b.bounds.y?a.bounds.x<b.bounds.x:a.bounds.y<b.bounds.y;});
  return out;
}
std::string Navigator::focused_label(int w,int h)const{
  if(focus_<0)return {};
  if(preferences_.hidden)return focus_==0?tr("ASSETS_RESTORE","Restore assets panel"):std::string{};
  const auto targets=focusables(Layout::make(w,h));
  return focus_<static_cast<int>(targets.size())?targets[static_cast<std::size_t>(focus_)].label:std::string{};
}
void Navigator::commit_preferences(Preferences next){if(persist_&&!persist_(next)){error_=tr("ASSETS_PREFS_FAIL","Could not save navigator preferences.");return;}preferences_=next;error_.clear();pressed_.reset();rebuild();}
Command Navigator::handle(const InputEvent& e,int w,int h){
  const auto l=Layout::make(w,h);pointer_=e.position;Command out;out.generation=view_.generation;out.observer=view_.observer;
  if(e.type==InputEventType::PointerCancelled){cancel_input();return out;}
  if(preferences_.hidden){
    if(e.type==InputEventType::KeyPressed&&e.key){
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      if(e.key==kHome||e.key==kEnd){focus_=0;out.captured=true;}
      else if(e.key==kTab||e.key==kRight||e.key==kDown||e.key==kLeft||e.key==kUp){
        // Single-item ring: the first nav key lands on restore, the next
        // wraps out so the map focus chain can advance to the next group.
        if(focus_<0){focus_=0;out.captured=true;}
        else focus_=-1;}
      else if((e.key==kReturn||e.key==kSpace)&&focus_==0){InputEvent press{InputEventType::LeftPressed,{l.restore.x+l.restore.width*.5f,l.restore.y+l.restore.height*.5f}};(void)handle(press,w,h);focus_=-1;out.captured=true;}
      return out;
    }
    if(e.type==InputEventType::LeftPressed)focus_=-1;
    if(l.restore.contains(e.position)){out.captured=true;if(e.type==InputEventType::LeftPressed){auto p=preferences_;p.hidden=false;commit_preferences(p);}}
    return out;
  }
  if(e.type==InputEventType::EscapePressed&&search_focused_){search_focused_=false;out.captured=true;return out;}
  if(search_focused_&&(e.type==InputEventType::TextEntered||e.type==InputEventType::BackspacePressed)){
    if(e.type==InputEventType::TextEntered&&search_.size()+e.text.size()<=120)search_+=e.text;
    else if(e.type==InputEventType::BackspacePressed&&!search_.empty()){auto n=search_.size()-1;while(n>0&&(static_cast<unsigned char>(search_[n])&0xc0)==0x80)--n;search_.resize(n);}
    scroll_=0;pressed_.reset();rebuild();out.captured=true;return out;
  }
  if(search_focused_&&e.type==InputEventType::KeyPressed){
    // While editing, the search field owns the keyboard — Tab or Return
    // commit out of it; every other key stays captured.
    if(e.key==9u||e.key==13u)search_focused_=false;
    out.captured=true;return out;
  }
  if(e.type==InputEventType::KeyPressed&&e.key){
    // SDL_Keycode: Tab/arrows ring the (y,x)-ordered focusables — hide,
    // search, clear, then every visible header/row — Home/End jump to the
    // ends, and Return/Space replay the press/release pair through the
    // same dispatch a click takes.
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const auto targets=focusables(l);
    const int count=static_cast<int>(targets.size());
    const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
    const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
    if(count>0&&(e.key==kHome||e.key==kEnd))focus_=e.key==kHome?0:count-1;
    else if(count>0&&(fwd||bwd)){
      if(focus_<0||focus_>=count)focus_=bwd?count-1:0;
      else{
        // Walking past a boundary releases the ring so the dispatcher can
        // hand the same key to the next map focus group.
        const int next=focus_+(bwd?-1:1);
        if(next<0||next>=count){focus_=-1;return out;}
        focus_=next;
      }
    }
    else if((e.key==kReturn||e.key==kSpace)&&focus_>=0&&focus_<count){
      const auto& r=targets[static_cast<std::size_t>(focus_)].bounds;
      InputEvent press{InputEventType::LeftPressed,{r.x+r.width*.5f,r.y+r.height*.5f}};
      InputEvent release=press;release.type=InputEventType::LeftReleased;
      const int keep=focus_;
      (void)handle(press,w,h);
      out=handle(release,w,h);
      if(!preferences_.hidden)focus_=keep;
      out.captured=true;return out;
    }
    else return out;
    out.captured=true;
    // Scroll-follow: keep the focused entry fully inside the viewport.
    if(const auto entry=targets[static_cast<std::size_t>(focus_)].entry){
      const auto bounds=entry_bounds(*entry,l);
      if(bounds.y<l.list.y)scroll_=std::max(0.f,scroll_+bounds.y-l.list.y);
      else if(bounds.y+bounds.height>l.list.y+l.list.height)scroll_+=bounds.y+bounds.height-l.list.y-l.list.height;
      scroll_=std::clamp(scroll_,0.f,std::max(0.f,extent(l)-l.list.height));
    }
    return out;
  }
  if(e.type==InputEventType::LeftReleased&&pressed_){const auto index=*pressed_;pressed_.reset();out.captured=true;
    if(pressed_generation_!=view_.generation||pressed_observer_!=view_.observer||index>=entries_.size()||!l.list.contains(e.position)||!entry_bounds(index,l).contains(e.position))return out;
    const auto entry=entries_[index];if(!entry.row){auto p=preferences_;p.collapsed[static_cast<int>(entry.category)]=!p.collapsed[static_cast<int>(entry.category)];temporary_reveal_.reset();commit_preferences(p);return out;}
    const auto& row=view_.rows[*entry.row];if(!row.actionable)return out;out.key=row.key;out.manage=click_count_>=2||e.position.x>l.list.x+l.list.width-32*l.scale;set_selection(row.key,false);return out;
  }
  if(e.type==InputEventType::LeftPressed&&!l.panel.contains(e.position)){search_focused_=false;pressed_.reset();focus_=-1;return out;}
  if(!l.panel.contains(e.position))return out;
  out.captured=true;
  if(e.type==InputEventType::LeftPressed)focus_=-1;
  if(e.type==InputEventType::Wheel){scroll_=std::clamp(scroll_-e.wheel_y*52*l.scale,0.f,std::max(0.f,extent(l)-l.list.height));pressed_.reset();}
  if(e.type==InputEventType::LeftPressed){
    if(l.hide.contains(e.position)){auto p=preferences_;p.hidden=true;search_focused_=false;commit_preferences(p);return out;}
    search_focused_=l.search.contains(e.position);
    if(l.clear.contains(e.position)){search_.clear();scroll_=0;rebuild();return out;}
    if(l.list.contains(e.position))for(std::size_t i=0;i<entries_.size();++i)if(entry_bounds(i,l).contains(e.position)){pressed_=i;pressed_generation_=view_.generation;pressed_observer_=view_.observer;click_count_=e.click_count;break;}
  }
  return out;
}
void Navigator::render(DrawList& out,int w,int h,const Art& art){
  const auto l=Layout::make(w,h);const auto s=l.scale;const int normal=std::max(11,static_cast<int>(15*s)),small=std::max(10,static_cast<int>(12*s));
  if(preferences_.hidden){native_menu_style::button(out,l.restore,tr("ASSETS_RESTORE","Assets ›"),normal,l.restore.contains(pointer_),true,s);if(focus_>=0)out.overlay.emplace_back(StrokedRectangle{l.restore,cyan});return;}
  scroll_=std::clamp(scroll_,0.f,std::max(0.f,extent(l)-l.list.height));
  if(reveal_selection_&&selected_){for(std::size_t i=0;i<entries_.size();++i)if(entries_[i].row&&view_.rows[*entries_[i].row].key==selected_){const auto r=entry_bounds(i,l);if(r.y<l.list.y)scroll_=std::max(0.f,scroll_+r.y-l.list.y);else if(r.y+r.height>l.list.y+l.list.height)scroll_+=r.y+r.height-l.list.y-l.list.height;break;}reveal_selection_=false;}
  native_menu_style::panel(out,l.panel,s);label(out,l.header,trf("ASSETS_TITLE",{std::to_string(view_.rows.size())},"CONTROLLED ASSETS · {0}"),cyan,normal,l.panel);
  native_menu_style::button(out,l.hide,"›",normal,l.hide.contains(pointer_),true,s);
  out.overlay.emplace_back(FilledRectangle{l.search,{3,13,22,245}});out.overlay.emplace_back(StrokedRectangle{l.search,search_focused_?cyan:Color{54,111,140,255}});
  label(out,{l.search.x+8*s,l.search.y+6*s,l.search.width-40*s,24*s},search_.empty()?tr("ASSETS_SEARCH","Search assets…"):search_,search_.empty()?muted:ink,small,l.search);
  if(!search_.empty())label(out,l.clear,"×",ink,normal,l.search);
  float y=l.list.y-scroll_;std::optional<std::pair<const Row*,UiRect>> tooltip;
  for(const auto& e:entries_){const float height=e.row?l.row_height:l.category_height;const UiRect r{l.list.x,y,l.list.width-5*s,height};y+=height;
    const auto clip=intersect(r,l.list);if(clip.height<=0)continue;
    if(!e.row){const int c=static_cast<int>(e.category);const bool expanded=!preferences_.collapsed[c]||!search_.empty()||(temporary_reveal_&&temporary_reveal_->category==e.category);
      out.overlay.emplace_back(FilledRectangle{clip,{7,27,41,244}});label(out,{r.x+8*s,r.y+8*s,r.width-16*s,24*s},std::string(expanded?"⌄  ":"›  ")+tr(name_keys[c],names[c])+"  "+std::to_string(counts_[c])+(search_.empty()?"":trf("ASSETS_MATCHES",{std::to_string(matches_[c])}," ({0})")),cyan,small,l.list);continue;}
    const auto& row=view_.rows[*e.row];const bool selected=selected_==row.key,hovered=r.contains(pointer_)&&l.list.contains(pointer_);
    if(selected||hovered)stellar::engine::ui_skin::control(out,r,hovered,selected,true,s,l.list);
    else stellar::engine::ui_skin::gradient(out,r,{4,22,32,240},{2,12,19,240},4*s,l.list);
    if(selected)out.overlay.emplace_back(FilledRectangle{{clip.x,clip.y,2*s,clip.height},cyan});
    if(art)if(auto picture=art(row))out.overlay.emplace_back(Image{picture,{r.x+6*s,r.y+10*s,40*s,40*s},{},{255,255,255,255},clip});
    const float x=r.x+52*s,tw=r.width-83*s;
    label(out,{x,r.y+5*s,tw,21*s},row.name,ink,normal,clip);
    label(out,{x,r.y+27*s,tw,18*s},row.detail,muted,small,clip);
    label(out,{x,r.y+46*s,tw,18*s},row.progress?row.activity:row.activity+" · "+tr(row.controlled?"ASSETS_CONTROLLED":"ASSETS_OWNED",row.controlled?"Controlled":"Owned"),row.controlled?cyan:muted,small,clip);
    label(out,{r.x+r.width-25*s,r.y+7*s,24*s,25*s},"›",row.actionable?cyan:muted,normal,clip);
    label(out,{r.x+r.width-24*s,r.y+34*s,22*s,23*s},row.severity?"!":"•",row.severity>1?Color{255,113,105,255}:row.severity?amber:green,normal,clip);
    if(row.progress){const UiRect bar{x,r.y+r.height-5*s,tw,3*s};stellar::engine::ui_skin::progress(out,bar,static_cast<float>(*row.progress),s,clip);}
    if(hovered)tooltip=std::pair{&row,r};
  }
  if(entries_.empty())label(out,{l.list.x+10*s,l.list.y+18*s,l.list.width-20*s,150*s},view_.rows.empty()?tr("ASSETS_EMPTY","No controlled assets.\n\nExplore the galaxy or establish a colony to begin expanding your civilization."):tr("ASSETS_EMPTY_SEARCH","No assets match your search."),muted,normal,l.list);
  const auto content=extent(l);if(content>l.list.height){const float thumb=std::max(24*s,l.list.height*l.list.height/content);out.overlay.emplace_back(FilledRectangle{{l.list.x+l.list.width-2*s,l.list.y+(l.list.height-thumb)*scroll_/(content-l.list.height),2*s,thumb},{72,158,192,255}});}
  if(tooltip){const UiRect box{l.panel.x-300*s-8*s,std::clamp(tooltip->second.y,80*s,h-174*s),300*s,158*s};native_menu_style::panel(out,box,s);label(out,{box.x+12*s,box.y+12*s,box.width-24*s,box.height-24*s},tooltip->first->tooltip,ink,small,box);}
  if(!error_.empty())label(out,{l.panel.x+10*s,l.panel.y+l.panel.height-22*s,l.panel.width-20*s,22*s},error_,amber,small,l.panel);
  if(focus_>=0){const auto targets=focusables(l);if(focus_<static_cast<int>(targets.size()))out.overlay.emplace_back(StrokedRectangle{targets[static_cast<std::size_t>(focus_)].bounds,cyan});}
}
}
