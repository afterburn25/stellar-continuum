#include "native_new_game_workspace.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace {
using namespace stellar::native_map;
using namespace stellar::native_setup;
using namespace stellar::native_setup_ui;
void require(bool ok,std::string_view message){if(!ok)throw std::runtime_error(std::string(message));}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
bool contains(UiRect a,UiRect b){return b.x>=a.x&&b.y>=a.y&&b.x+b.width<=a.x+a.width&&b.y+b.height<=a.y+a.height;}
TextExtent measure(const Text&value){
  const int character_width=std::max(1,value.font_pixel_size*3/5);
  const int columns=std::max(1,static_cast<int>(value.wrap_width)/character_width);
  int lines=1,column=0,maximum=0;
  for(const char character:value.value){if(character=='\n'||column==columns){maximum=std::max(maximum,column);++lines;column=character=='\n'?0:1;}else ++column;}
  maximum=std::max(maximum,column);return {maximum*character_width,lines*(value.font_pixel_size+2)};
}

NativeSpeciesSetupOption species(std::string id,std::string name){
  NativeSpeciesSetupOption v;v.id=std::move(id);v.display_name=std::move(name);
  v.biochemistry=stellar::core::BiochemicalBasis::CarbonWater;
  v.gravity_g={1.,.2,.5};v.temperature_kelvin={288.,25.,60.};v.pressure_kpa={101.,30.,80.};
  v.preferred_atmosphere=stellar::core::SpeciesAtmosphere::OxygenNitrogen;
  v.biological_solvent=stellar::core::SpeciesSolvent::Water;v.radiation_tolerance=.42;
  v.biochemistry_label="Carbon / water";v.preferred_atmosphere_label="Oxygen / nitrogen";v.biological_solvent_label="Water";
  v.breathable_atmospheres={stellar::core::SpeciesAtmosphere::OxygenNitrogen};
  v.compatible_solvents={stellar::core::SpeciesSolvent::Water};return v;
}
NativeNewCampaignSetupView setup(){
  NativeNewCampaignSetupView v;
  v.species={species("terran_baseline","Terran Baseline"),species("pelagic_high_pressure","Pelagic High Pressure"),species("compact_high_gravity","Compact High Gravity"),species("cryogenic_hydrocarbon","Cryogenic Hydrocarbon")};
  v.species[1].requires_immersion=true;
  v.species[3].gravity_g={.14,.08,.25};
  v.size_presets={{250,"Small - 250 systems",false},{500,"Medium - 500 systems",true},{1000,"Large - 1,000 systems",false},{2500,"Huge - 2,500 systems",false}};
  v.default_species_id="terran_baseline";v.default_system_count=500;
  v.pre_warp_civilization_presets={{1,"None",false},{4,"Sparse · 3",false},{6,"Standard · 5",true},{9,"Crowded · 8",false},{13,"Packed · 12",false}};
  v.ancient_civilization_presets={{0,"None",false},{1,"Rare",true},{2,"Standard",false}};
  v.default_pre_warp_civilization_count=6;v.default_ancient_civilization_count=1;return v;
}

void responsive_layout(){
  for(const auto [w,h]:{std::pair{1280,720},{1920,1080},{2560,1440},{3840,2160},{1280,1080}}){
    NativeNewGameWorkspace workspace;workspace.set_view(setup());const auto measured=workspace.measure_layout(w,h,measure);const auto&l=measured.base;
    for(const auto r:{l.heading,l.cancel,l.mode_story,l.mode_sandbox,l.species,l.details,l.size_group,l.seed_input,l.randomize_seed,l.restore_defaults,l.create})require(contains(l.panel,r),"setup control escaped its panel");
    require(l.mode_story.x+l.mode_story.width<=l.mode_sandbox.x,"campaign mode cards overlap");
    require(l.species.x+l.species.width<=l.details.x,"species list overlaps details");
    require(l.species.y<l.mode_story.y&&l.details.y<l.mode_sandbox.y,"species selection is not the first setup decision");
    require(l.species.y+l.species.height<=l.seed_label.y,"species list overlaps setup controls");
    require(l.randomize_seed.x+l.randomize_seed.width<=l.size_group.x,"seed controls overlap sizes");
    for(const auto button:l.size_buttons)require(contains(l.size_group,button),"size button escaped its group");
    for(std::size_t index=1;index<measured.species_rows.size();++index)require(measured.species_rows[index-1].y+measured.species_rows[index-1].height<measured.species_rows[index].y,"measured species rows overlap");
  }
}
void presentations(){
  const auto t=species_presentation("terran_baseline");
  require(t&&t->portrait_asset_path=="assets/visual/species/terran-baseline.jpg","Terran portrait mapping changed");
  require(t->biography=="An oxygen-breathing, water-based people shaped for open terrestrial worlds.","Terran biography changed");
  require(species_presentation("pelagic_high_pressure").has_value()&&species_presentation("compact_high_gravity").has_value()&&species_presentation("cryogenic_hydrocarbon").has_value(),"preserved species presentation absent");
  require(!species_presentation("future_unknown"),"future species received invented presentation");
}
NativeNewGameIntent choose_count(NativeNewGameWorkspace& w,UiRect anchor,bool ancient){
  (void)w.handle({InputEventType::LeftPressed,center(anchor)},1280,720,measure);
  const auto choices=ancient?setup().ancient_civilization_presets:setup().pre_warp_civilization_presets;
  int index{};for(int i=0;i<static_cast<int>(choices.size());++i)if(choices[i].count==(ancient?2:9))index=i;
  stellar::native_ui::Dropdown menu;menu.open(0,std::vector<std::string>(choices.size(),"count"),0);
  return w.handle({InputEventType::LeftPressed,center(menu.layout(anchor,1280,720).rows[index])},1280,720,measure);
}
void mouse_and_text(){
  NativeNewGameWorkspace w;w.set_view(setup());require(w.selected_species_id()=="terran_baseline"&&w.selected_system_count()==500,"detached defaults not selected");
  const auto measured=w.measure_layout(1280,720,measure);const auto&l=measured.base;
  w.randomize_seed();
  require(!w.seed_text().empty(),"fresh sandbox setup did not receive a numeric seed");
  auto intent=w.handle({InputEventType::LeftPressed,center(l.randomize_seed)},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::RandomizeSeed&&intent.seed_text==w.seed_text()&&!w.seed_text().empty(),"Randomize did not provide a seed");
  require(choose_count(w,l.mode_story,false).kind==NativeNewGameIntentKind::SelectRivals&&w.selected_pre_warp_civilization_count()==9,"rival selection did not advance through supported counts");
  require(choose_count(w,l.mode_sandbox,true).kind==NativeNewGameIntentKind::SelectAncients&&w.selected_ancient_civilization_count()==2,"ancient selection did not advance through supported counts");
  const Point gap{measured.species_rows[0].x+10,measured.species_rows[0].y+measured.species_rows[0].height+2};
  require(w.handle({InputEventType::LeftPressed,gap},1280,720,measure).kind==NativeNewGameIntentKind::None&&w.selected_species_id()=="terran_baseline","species row gap selected a species");
  intent=w.handle({InputEventType::LeftPressed,center(measured.species_rows[1])},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::SelectSpecies&&intent.species_id=="pelagic_high_pressure","mouse species selection failed");
  intent=w.handle({InputEventType::LeftPressed,center(l.size_buttons[3])},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::SelectSize&&intent.system_count==2500,"mouse size selection failed");
  intent=w.handle({InputEventType::LeftPressed,center(l.restore_defaults)},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::RestoreDefaults&&intent.system_count==500&&intent.pre_warp_civilization_count==6&&intent.ancient_civilization_count==1&&intent.species_id.empty(),"Restore defaults did not return canonical selections");
  (void)w.handle({InputEventType::LeftPressed,center(measured.species_rows[1])},1280,720,measure);
  (void)w.handle({InputEventType::LeftPressed,center(l.size_buttons[3])},1280,720,measure);
  (void)choose_count(w,l.mode_story,false);
  (void)choose_count(w,l.mode_sandbox,true);
  (void)w.handle({InputEventType::LeftPressed,center(l.seed_input)},1280,720,measure);
  require(w.seed_focused(),"seed field did not focus");
  (void)w.handle({InputEventType::TextEntered,{}, {},0,"-9223372036854775808"},1280,720,measure);
  (void)w.handle({InputEventType::BackspacePressed},1280,720,measure);
  (void)w.handle({InputEventType::TextEntered,{}, {},0,"8"},1280,720,measure);
  require(w.seed_text()=="-9223372036854775808","seed editing was not exact");
  intent=w.handle({InputEventType::LeftPressed,center(l.copy_setup)},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::CopySetup&&intent.species_id=="pelagic_high_pressure"&&intent.system_count==2500&&intent.seed_text==w.seed_text()&&intent.pre_warp_civilization_count==9&&intent.ancient_civilization_count==2,"Copy setup omitted selected values");
  intent=w.handle({InputEventType::LeftPressed,center(l.create)},1280,720,measure);
  require(intent.kind==NativeNewGameIntentKind::Create&&intent.species_id=="pelagic_high_pressure"&&intent.system_count==2500&&intent.seed_text==w.seed_text()&&intent.pre_warp_civilization_count==9&&intent.ancient_civilization_count==2,"Create omitted setup input");
  require(w.handle({InputEventType::EscapePressed},1280,720,measure).kind==NativeNewGameIntentKind::Cancel&&!w.seed_focused(),"Escape did not cancel and clear focus");
  (void)w.handle({InputEventType::LeftPressed,center(l.seed_input)},1280,720,measure);
  require(w.handle({InputEventType::PointerCancelled},1280,720,measure).captured&&!w.seed_focused(),"focus loss retained seed focus or press ownership");
  (void)w.handle({InputEventType::LeftPressed,center(l.seed_input)},1280,720,measure);
  w.set_view(setup());require(!w.seed_focused(),"set_view retained stale text focus");
  w.clear();require(!w.view()&&!w.seed_focused(),"clear retained detached setup or interaction state");
}
void rendered_facts(){
  NativeNewGameWorkspace w;auto v=setup();v.species.front().display_name="Terran Baseline With A Deliberately Long Existing Display Name";
  v.species.front().biochemistry_label="Carbon and water chemistry with a deliberately long authored label that must wrap";w.set_view(std::move(v));
  DrawList draw;w.render(draw,1280,720,measure);const auto l=NativeNewGameLayout::for_viewport(1280,720);
  bool rivals=false,ancients=false,gravity=false,survival=false,size=false,bio=false,hint=false;
  for(const auto &c:draw.overlay)if(const auto *t=std::get_if<Text>(&c)){
    rivals|=t->value=="RIVAL EMPIRES";ancients|=t->value=="ANCIENT EMPIRES";gravity|=t->value.find("Comfortable gravity")!=std::string::npos;survival|=t->value.find("Survival gravity")!=std::string::npos;size|=t->value=="Huge · 2,500";bio|=t->value.find("oxygen-breathing")!=std::string::npos;hint|=t->value=="SCROLL FOR MORE";
    if(t->value.find("Comfortable ")!=std::string::npos||t->value.find("Radiation tolerance")!=std::string::npos)require(t->clip&&contains(l.details,*t->clip),"species fact escaped details clip");
  }
  require(rivals&&ancients&&gravity&&survival&&size&&bio&&hint,"render omitted an authoritative field or scroll affordance");
  (void)w.handle({InputEventType::Wheel,center(l.details),{},-100},1280,720,measure);
  require(w.detail_scroll()>0,"measured detail content did not produce scroll at 720p");
  DrawList scrolled;w.render(scrolled,1280,720,measure);bool last=false,chemistry=false,radiation=false,header=false;
  for(const auto&c:scrolled.overlay)if(const auto*t=std::get_if<Text>(&c)){last|=t->value=="Requires protection in vacuum.";chemistry|=t->value.find("Carbon and water chemistry")!=std::string::npos;radiation|=t->value.find("Radiation tolerance")!=std::string::npos;header|=t->value.find("Terran Baseline With")!=std::string::npos;}
  require(last&&chemistry&&radiation&&header,"scroll did not reach the last facts or retain the species header");
}
void portrait_provider(){
  NativeNewGameWorkspace w;w.set_view(setup());
  auto resource=RgbaImage::create(2,1,std::vector<std::uint8_t>(8,255));int calls=0;
  const std::unordered_set<std::string_view> approved{"assets/visual/species/terran-baseline.jpg","assets/visual/species/pelagic-high-pressure.jpg","assets/visual/species/compact-high-gravity.jpg","assets/visual/species/cryogenic-hydrocarbon.jpg"};
  NativeNewGameWorkspace::PortraitProvider provider=[&](std::string_view path){++calls;require(approved.contains(path),"provider received an unapproved portrait path");return resource;};
  DrawList draw;w.render(draw,1280,720,measure,&provider);const auto l=NativeNewGameLayout::for_viewport(1280,720);
  bool found_large=false;int images=0;for(const auto&command:draw.overlay)if(const auto*image=std::get_if<Image>(&command)){++images;require(image->resource==resource,"overlay did not retain immutable portrait ownership");require(image->destination.width/image->destination.height==2.f,"portrait was cropped or distorted");if(image->destination.width>80){found_large=true;require(contains(l.portrait,image->destination),"contained selected portrait escaped its frame");}}
  require(found_large&&images==5&&calls==5,"list thumbnails and selected portrait were not all requested");
}
void physical_range_presentation(){
  NativeNewGameWorkspace w;w.set_view(setup());auto layout=w.measure_layout(1280,720,measure);
  (void)w.handle({InputEventType::LeftPressed,center(layout.species_rows[3])},1280,720,measure);
  DrawList draw;w.render(draw,1280,720,measure);bool clamped=false,negative=false;
  for(const auto&command:draw.overlay)if(const auto*label=std::get_if<Text>(&command);label&&label->value.starts_with("Survival gravity")){clamped|=label->value.find("0.00–")!=std::string::npos;negative|=label->value.find("-")!=std::string::npos;}
  require(clamped&&!negative,"displayed survival range escaped the physical gravity domain");
}
}
int main()try{responsive_layout();presentations();mouse_and_text();rendered_facts();portrait_provider();physical_range_presentation();std::cout<<"native new-game workspace tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
