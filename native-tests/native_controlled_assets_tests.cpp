#include "native_controlled_assets.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace stellar::native_assets;
using namespace stellar::native_map;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
Command click(Navigator& n,UiRect r,int count=1,int w=1920,int h=1080){InputEvent e{InputEventType::LeftPressed,center(r)};e.click_count=static_cast<std::uint8_t>(count);(void)n.handle(e,w,h);return n.handle({InputEventType::LeftReleased,center(r)},w,h);}
View sample(){View v{7,3,{}};for(int c:{0,1,2,4}){Row r;r.key={static_cast<Category>(c),10+c};r.name="Owned "+std::to_string(c);r.detail="Sol";r.search="owned sol "+std::to_string(c);v.rows.push_back(r);}return v;}
void projection(){
  using namespace stellar::core;FreshCampaignState world;world.player_civilization_id=3;
  Colony colony;colony.id=10;colony.civilization_id=3;colony.stability=.9;world.colonies.push_back(colony);
  colony.id=11;colony.kind=SettlementKind::ResourceOutpost;world.colonies.push_back(colony);colony.id=99;colony.civilization_id=8;world.colonies.push_back(colony);
  FleetState fleet;fleet.id=12;fleet.civilization_id=3;fleet.current_system_id=0;world.fleets.push_back(fleet);fleet.id=99;fleet.civilization_id=8;world.fleets.push_back(fleet);
  stellar::native_colony_roster::View colonies;colonies.generation=7;colonies.player_id=3;colonies.available=true;
  for(int id:{10,11,99}){stellar::native_colony_roster::Row row;row.colony_id=id;row.name="Colony";row.system_name="Sol";row.population="1M";row.can_open=true;colonies.rows.push_back(row);}
  stellar::native_fleet::NativeFleetMapView fleets;fleets.campaign_generation=7;fleets.player_civilization_id=3;
  for(int id:{12,99}){stellar::native_fleet::NativeOwnFleet f;f.id=id;f.name="Fleet";f.current_system_id=0;fleets.own_fleets.push_back(f);}
  stellar::native_shipyard::NativeShipyardView yard;yard.campaign_generation=7;yard.player_civilization_id=3;yard.home_system_id=0;yard.orbital_shipyard_complete=true;yard.maximum_pending_builds=8;
  const auto project=[&]{return build(world,colonies,fleets,&yard,[](int){return "Sol";});};
  auto v=project();require(v.rows.size()==4,"Enemy record leaked or asset missing");
  require(v.rows[0].key.category==Category::Planets&&v.rows[1].key.category==Category::Outposts&&v.rows[2].key.category==Category::Fleets&&v.rows[3].key.category==Category::Shipyards,"Stable categories or yard deduplication failed");
  require(!v.rows.back().progress,"Idle yard displayed fake progress");
  stellar::native_shipyard::NativeShipyardOrder order;order.active=true;order.design_name="Scout";order.progress_fraction=.42;yard.orders.push_back(order);v=project();require(v.rows.back().progress==.42,"Construction progress not live");
  world.colonies.front().stability=.1;world.fleets.front().transit_phase=FleetTransitPhase::InterstellarWarp;v=project();require(v.rows.front().severity==2&&v.rows[2].activity.find("Moving")!=std::string::npos,"Live alert/order lost");
  // Urgency ordering inside Fleets: an in-transit fleet surfaces ahead of
  // an idle hull even when the idle fleet carries the lower canonical id.
  FleetState idle;idle.id=5;idle.civilization_id=3;idle.current_system_id=0;world.fleets.push_back(idle);
  stellar::native_fleet::NativeOwnFleet idle_view;idle_view.id=5;idle_view.name="Idle";idle_view.current_system_id=0;fleets.own_fleets.push_back(idle_view);
  v=project();std::vector<int> fleet_ids;for(const auto& r:v.rows)if(r.key.category==Category::Fleets)fleet_ids.push_back(r.key.id);
  require(fleet_ids==std::vector<int>({12,5}),"Fleet list did not surface the in-transit fleet ahead of idle hulls");
  world.colonies.front().civilization_id=8;v=project();require(v.rows.size()==4&&v.rows.front().key.category==Category::Outposts,"Lost ownership retained");
  yard.orbital_shipyard_complete=false;v=project();require(v.rows.size()==3,"Unbuilt yard exposed");
  fleets.campaign_generation=8;require(project().rows.empty(),"Mixed generation accepted");
}
void interactions(){
  Navigator n;n.set_view(sample());auto p=n.preferences();p.collapsed.fill(false);n.set_preferences(p);Preferences persisted;
  n.set_persist([&](const Preferences& value){persisted=value;return true;});
  require(n.category_bounds(Category::Stations,1920,1080).width==0,"Empty category visible");
  (void)click(n,n.category_bounds(Category::Planets,1920,1080));require(n.preferences().collapsed[0]&&!n.preferences().collapsed[2]&&persisted.collapsed[0],"Independent collapse persistence failed");
  (void)click(n,n.category_bounds(Category::Planets,1920,1080));require(n.row_bounds({Category::Planets,10},1920,1080).has_value()&&!n.preferences().collapsed[0],"Tree re-expand lost its rows");
  (void)click(n,n.category_bounds(Category::Planets,1920,1080));require(n.preferences().collapsed[0]&&!n.row_bounds({Category::Planets,10},1920,1080),"Tree re-collapse kept its rows");
  const auto l=Layout::make(1920,1080);(void)n.handle({InputEventType::LeftPressed,center(l.search)},1920,1080);
  InputEvent typing{InputEventType::TextEntered};typing.text="sol";(void)n.handle(typing,1920,1080);
  require(n.row_bounds({Category::Planets,10},1920,1080).has_value(),"Search hid collapsed result");
  (void)n.handle({InputEventType::LeftPressed,center(l.clear)},1920,1080);
  require(!n.row_bounds({Category::Planets,10},1920,1080),"Search destroyed collapse preference");
  n.set_selection(Key{Category::Planets,10});DrawList d;n.render(d,1920,1080,{});require(n.row_bounds({Category::Planets,10},1920,1080).has_value()&&n.preferences().collapsed[0],"External selection failed temporary reveal");
  n.set_selection(Key{Category::Fleets,12});require(!n.row_bounds({Category::Planets,10},1920,1080),"Temporary reveal not restored");
  auto row=n.row_bounds({Category::Shipyards,14},1920,1080);require(row.has_value(),"Yard missing");
  auto command=click(n,*row);require(command.key==Key{Category::Shipyards,14}&&!command.manage,"Single click did not focus exact yard");
  command=click(n,*row,2);require(command.manage&&command.generation==7&&command.observer==3,"Double click lost identity");
  (void)n.handle({InputEventType::LeftPressed,center(*row)},1920,1080);
  auto progressed=sample();progressed.rows.back().progress=.6;progressed.rows.back().activity="Building scout";n.set_view(progressed);
  require(n.handle({InputEventType::LeftReleased,center(*row)},1920,1080).key==Key{Category::Shipyards,14},"Live progress refresh swallowed click");
  UiRect manage=*row;manage.x+=manage.width-20;manage.width=12;require(click(n,manage).manage,"Manage arrow unavailable");
  (void)n.handle({InputEventType::LeftPressed,center(*row)},1920,1080);auto changed=sample();changed.generation=8;n.set_view(changed);require(!n.handle({InputEventType::LeftReleased,center(*row)},1920,1080).key,"Stale generation click accepted");
  (void)click(n,l.hide);require(n.preferences().hidden,"Hide failed");(void)click(n,l.restore);require(!n.preferences().hidden,"Restore failed");
  auto empty=sample();empty.rows.clear();n.set_view(empty);require(n.category_bounds(Category::Planets,1920,1080).width==0,"Last asset category retained");n.set_view(sample());require(n.category_bounds(Category::Planets,1920,1080).width>0,"First asset category absent");
}
void scale_and_virtualization(){
  for(auto size:std::array{std::pair{1280,720},std::pair{1920,1080},std::pair{2560,1440},std::pair{3440,1440},std::pair{3840,2160}}){Navigator n;auto v=sample();v.rows.clear();for(int i=0;i<1500;++i){Row r;r.key={Category::Fleets,i};r.name="Fleet "+std::to_string(i);r.search="fleet";v.rows.push_back(r);}n.set_view(v);
    const auto l=Layout::make(size.first,size.second);require(l.panel.x>size.first*.65f&&l.panel.y+l.panel.height<size.second,"Panel consumes map or clips");
    int thumbnails=0;DrawList draw;const auto start=std::chrono::steady_clock::now();n.render(draw,size.first,size.second,[&](const Row&){++thumbnails;return Navigator::Picture{};});
    require(thumbnails<25&&draw.overlay.size()<300,"Offscreen assets rendered eagerly");require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(200),"Large roster rendering too slow");
    n.set_selection(Key{Category::Fleets,1499});draw={};n.render(draw,size.first,size.second,{});require(n.row_bounds({Category::Fleets,1499},size.first,size.second).has_value(),"Selected asset not scrolled into view");
    (void)n.handle({InputEventType::Wheel,center(l.list),{},10000},size.first,size.second);require(n.scroll_offset()==0,"Scroll did not return to top");
  }
}
}
void keyboard_focus(){
  // Keyboard-focus contract: Tab/arrows ring the actionable rects in
  // (y,x) order — hide, search, the conditional clear button, then every
  // visible header/row clipped to the list viewport — Home/End jump to
  // the ends, Return/Space replay the press/release pair through the
  // same dispatch, and the ring scrolls entries into view. The search
  // field owns its keys while editing; hidden mode narrows the ring to
  // the restore control.
  Navigator n;n.set_view(sample());auto p=n.preferences();p.collapsed.fill(false);n.set_preferences(p);
  const auto key=[&](std::uint32_t code,bool shift=false){InputEvent e{};e.type=InputEventType::KeyPressed;e.key=code;e.shift=shift;return n.handle(e,1920,1080);};
  constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u,kDown=0x40000051u;
  constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du,kF5=0x4000003fu;
  const auto l=Layout::make(1920,1080);
  require(n.focus()<0,"ring present before any key");
  require(n.focused_label(1920,1080).empty(),"unfocused ring reported a label");
  require(key(kTab).captured&&n.focus()==0,"Tab did not focus the hide control");
  require(n.focused_label(1920,1080)=="Hide assets panel","focused control label mismatch");
  require(key(kDown).captured&&n.focus()==1,"Down did not advance to search");
  require(key(kTab,true).captured&&n.focus()==0,"Shift+Tab did not walk back");
  require(key(kEnd).captured&&n.focus()==9,"End did not land on the last row");
  require(n.focused_label(1920,1080)=="Owned 4","focused row label mismatch");
  // Boundary wrap-out releases the ring so the dispatcher can hand the same
  // key to the next map focus group (fleet workspace, HUD chrome).
  require(!key(kTab).captured&&n.focus()<0,"Tab past the last row did not release the ring");
  require(key(kTab,true).captured&&n.focus()==9,"Shift+Tab did not re-enter at the tail");
  require(key(kHome).captured&&n.focus()==0,"Home did not return to the head");
  require(!key(kTab,true).captured&&n.focus()<0,"Shift+Tab at the head did not release the ring");
  require(!key(kF5).captured,"unrelated key was captured");
  // Search activation enters edit mode; the field owns keys until commit.
  (void)key(kTab);(void)key(kDown);require(n.focus()==1,"ring did not reach search");
  require(!n.focused_expanded(1920,1080).has_value(),"search field reported an expandable state");
  require(key(kReturn).captured&&n.wants_text_input(),"Return on search did not enter edit mode");
  require(key(kDown).captured&&n.focus()==1,"editing search leaked a key to the ring");
  require(key(kTab).captured&&!n.wants_text_input(),"Tab did not commit out of search editing");
  // A row selects through the replayed dispatch; headers toggle collapse.
  (void)key(kEnd);
  auto command=key(kSpace);
  require(command.captured&&command.key==Key{Category::Shipyards,14}&&!command.manage&&n.selection()==Key{Category::Shipyards,14},"Space on a row did not select it");
  (void)key(kHome);(void)key(kDown);(void)key(kDown);
  require(n.focus()==2,"ring did not reach the Planets header");
  Preferences persisted;n.set_persist([&](const Preferences& v){persisted=v;return true;});
  command=key(kReturn);
  require(command.captured&&n.preferences().collapsed[0]&&persisted.collapsed[0]&&n.focus()==2,"Return on a header did not toggle collapse or lost the ring");
  // Expand/collapse pattern route: headers report their effective state
  // and accept programmatic direction through the persisted path; leaf
  // rows and non-list controls report no state and refuse writes.
  const auto expanded=n.focused_expanded(1920,1080);
  require(expanded.has_value()&&!*expanded,"collapsed header did not report its expandable state");
  require(n.set_focused_expanded(true,1920,1080)&&!n.preferences().collapsed[0]&&!persisted.collapsed[0]&&n.focus()==2,"programmatic expand did not reopen the category or lost the ring");
  require(n.focused_expanded(1920,1080)==std::optional<bool>{true},"re-expanded header did not report its state");
  require(n.set_focused_expanded(false,1920,1080)&&n.preferences().collapsed[0]&&persisted.collapsed[0],"programmatic collapse did not persist");
  (void)key(kEnd);
  require(!n.focused_expanded(1920,1080).has_value(),"leaf row reported an expandable state");
  require(!n.set_focused_expanded(true,1920,1080),"leaf row accepted an expansion request");
  // The ring renders over the focused rect.
  DrawList draw;n.render(draw,1920,1080,{});
  const auto* ring=std::get_if<StrokedRectangle>(&draw.overlay.back());
  require(ring&&ring->color.r==94&&ring->color.g==212,"focused navigator control rendered no ring");
  // A pointer press hands ownership back to the pointer.
  (void)n.handle({InputEventType::LeftPressed,center(l.panel)},1920,1080);
  require(n.focus()<0,"pointer press did not clear the ring");
  // Hidden mode: the restore control rings and unhides on Return.
  auto prefs=n.preferences();prefs.hidden=true;n.set_preferences(prefs);
  require(key(kTab).captured&&n.focus()==0,"hidden mode did not ring the restore control");
  require(!key(kTab).captured&&n.focus()<0,"hidden-mode ring did not release on the next Tab");
  require(key(kTab).captured&&n.focus()==0,"hidden mode did not re-ring the restore control");
  command=key(kReturn);
  require(command.captured&&!n.preferences().hidden,"Return on restore did not unhide");
}
int main(){try{projection();interactions();scale_and_virtualization();keyboard_focus();std::cout<<"Controlled Assets ownership, live projection, preferences, input, search and virtualization passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
