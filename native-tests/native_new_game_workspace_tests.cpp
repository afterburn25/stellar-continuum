#include "native_new_game_workspace.hpp"
#include "native_developer_access.hpp"
#include <algorithm>
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
void galaxy_flow(){
  using namespace stellar::core;
  for(const auto [width,height]:{std::pair{1280,720},{1920,1080},{2560,1440},{3440,1440},{3840,2160}}){
    NativeNewGameWorkspace w;w.set_view(setup());w.begin_sandbox();
    const auto l=GalaxyChoiceLayout::for_viewport(width,height);
    const auto click=[&](UiRect r){return w.handle({InputEventType::LeftPressed,center(r)},width,height,measure);};
    require(w.page()==SandboxPage::GalaxyType&&!w.selected_morphology()&&w.requested_population()==PopulationSelection::Random,"Sandbox initial page or selection is wrong");
    (void)click(l.next);require(w.page()==SandboxPage::GalaxyType,"Next accepted no morphology");
    for(int i=0;i<6;++i){require(contains(l.panel,l.cards[i]),"Galaxy card escaped panel");if(i%3)require(l.cards[i].y==l.cards[i-1].y&&l.cards[i].x>=l.cards[i-1].x+l.cards[i-1].width,"Galaxy cards are not 3 by 2");}
    require(l.cards[3].y>=l.cards[0].y+l.cards[0].height,"Galaxy rows overlap");
    std::unordered_set<std::string> paths;auto resource=RgbaImage::create(2,1,std::vector<std::uint8_t>(8,255));
    NativeNewGameWorkspace::PortraitProvider provider=[&](std::string_view path){require(path.find("stars_included")!=std::string_view::npos,"Gas-dust used on a selection card");paths.emplace(path);return resource;};
    DrawList draw;w.render(draw,width,height,measure,&provider);require(paths.size()==6,"Six distinct morphology previews were not rendered");
    const std::array order{GalaxyMorphology::Spiral,GalaxyMorphology::BarredSpiral,GalaxyMorphology::Elliptical,GalaxyMorphology::Lenticular,GalaxyMorphology::Irregular,GalaxyMorphology::Ring};
    for(int i=0;i<6;++i){(void)click(l.cards[i]);require(w.selected_morphology()==order[i],"Card selection or order is wrong");}
    (void)click(l.next);require(w.page()==SandboxPage::Population,"Next did not open population page");
    const auto seed=w.seed_text();const auto original=w.generation_configuration();
    (void)click(l.population);DrawList menu;w.render(menu,width,height,measure,&provider);
    std::unordered_set<std::string> options;for(const auto& item:menu.overlay)if(const auto* t=std::get_if<Text>(&item))for(int i=0;i<6;++i)if(t->value==population_selection_label(static_cast<PopulationSelection>(i)))options.emplace(t->value);
    require(options.size()==6,"Population dropdown does not contain all six choices");
    stellar::native_ui::Dropdown dropdown;dropdown.open(0,{"Random","Starburst","Active","Mature","Aging","Quiescent"},0);
    (void)click(dropdown.layout(l.population,width,height).rows[5]);require(w.requested_population()==PopulationSelection::Quiescent,"Population choice was not retained");
    require(w.generation_configuration()->resolved_population==PopulationState::Quiescent,"Explicit population did not reach canonical configuration");
    (void)click(l.back);require(w.page()==SandboxPage::GalaxyType&&w.selected_morphology()==GalaxyMorphology::Ring&&w.seed_text()==seed,"Back lost configuration");
    (void)click(l.next);(void)click(l.next);require(w.page()==SandboxPage::Configuration,"Population Next did not open existing settings");
    const auto setup_layout=NativeNewGameLayout::for_viewport(width,height);
    const auto create=click(setup_layout.create);require(create.requested_population==PopulationSelection::Quiescent&&create.stellar_population.morphology==GalaxyMorphology::Ring,"Create lost authoritative choices");
    (void)click(setup_layout.restore_defaults);require(!w.selected_morphology()&&w.page()==SandboxPage::GalaxyType&&w.requested_population()==PopulationSelection::Random,"Reset did not reset the complete workflow");
  }
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
  const auto flow=GalaxyChoiceLayout::for_viewport(1280,720);
  for(auto r:{flow.cards[0],flow.next,flow.next})(void)w.handle({InputEventType::LeftPressed,center(r)},1280,720,measure);
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
  require(w.handle({InputEventType::EscapePressed},1280,720,measure).captured&&!w.seed_focused()&&w.page()==SandboxPage::Population,"Escape did not go back and clear focus");
  (void)w.handle({InputEventType::LeftPressed,center(flow.next)},1280,720,measure);
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
void developer_research_controls(){
  stellar::engine::DeveloperAccess denied;
  require(!denied.set_active(true)&&!denied.active(),"Ordinary process enabled developer access.");
  stellar::engine::DeveloperAccess allowed(true);
  require(!allowed.active()&&allowed.set_active(true)&&allowed.active(),"Developer gate skipped eligibility or explicit activation.");
  InputEvent chord;chord.type=InputEventType::KeyPressed;chord.key=0x40000045u;chord.control=true;chord.shift=true;
  require(is_developer_shortcut(chord),"Developer chord not recognized.");
  chord.shift=false;require(!is_developer_shortcut(chord),"Partial shortcut enabled developer tools.");
  for(const auto dimensions:{std::pair{1280,720},std::pair{1920,1080},std::pair{3840,2160}}){
    const auto [width,height]=dimensions;NativeNewGameWorkspace w;
    auto view=setup();w.set_view(view);const auto l=NativeNewGameLayout::for_viewport(width,height);
    const auto click=[&](UiRect r){return w.handle({InputEventType::LeftPressed,center(r)},width,height,measure);};
    (void)click(l.developer_normal_research);
    require(!click(l.create).developer_research.complete_normal_research,"Invisible developer checkbox accepted ordinary input.");
    (void)click(l.developer_exploration);
    require(!click(l.create).developer_full_exploration,"Hidden exploration option accepted player input.");
    view.developer_mode=true;w.set_view(view);
    require(!click(l.create).developer_research.complete_normal_research,"Developer setup defaulted to fully researched.");
    (void)click(l.developer_normal_research);
    require(click(l.create).developer_research.complete_normal_research,"Normal research checkbox did not reach create intent.");
    (void)click(l.developer_special_research);
    require(click(l.create).developer_research.complete_special_research,"Special research checkbox did not reach create intent.");
    (void)click(l.developer_coverage);
    require(click(l.create).developer_full_coverage&&contains(l.panel,l.developer_coverage),"Coverage checkbox did not reach create intent or escaped viewport.");
    require(!click(l.create).developer_full_exploration,"Coverage silently enabled full exploration.");
    (void)click(l.developer_exploration);
    require(click(l.create).developer_full_exploration&&click(l.copy_setup).developer_full_exploration&&contains(l.panel,l.developer_exploration),
        "Exploration option did not reach Create/Copy or escaped the viewport.");
    require(l.developer_exploration.y+l.developer_exploration.height<=l.create.y&&
        l.developer_coverage.x+l.developer_coverage.width<=l.developer_exploration.x,
        "Exploration option overlaps another control.");
    require(contains(l.panel,l.developer_normal_research)&&contains(l.panel,l.developer_special_research),"Developer controls escaped setup viewport.");
    view.developer_mode=false;w.set_view(view);
    require(!click(l.create).developer_full_coverage,"Developer coverage leaked into ordinary setup.");
    require(!click(l.create).developer_full_exploration,"Full exploration leaked into ordinary setup.");
    const auto cleared=click(l.create).developer_research;
    require(!cleared.complete_normal_research&&!cleared.complete_special_research,"Developer settings leaked into ordinary setup.");
  }
}

void keyboard_focus(){
  constexpr std::uint32_t kTab=9u,kReturn=13u,kHome=0x4000004au,kEnd=0x4000004du,kRight=0x4000004fu;
  const int width=1280,height=720;
  NativeNewGameWorkspace w;w.set_view(setup());w.begin_sandbox();
  int cues=0;w.set_hover_callback([&]{++cues;});
  const auto key=[&](std::uint32_t k,bool shift=false){InputEvent e{InputEventType::KeyPressed};e.key=k;e.shift=shift;return w.handle(e,width,height,measure);};
  require(w.page()==SandboxPage::GalaxyType&&w.focus()<0,"sandbox did not open on the galaxy page without focus");
  require(w.focused_label(width,height,measure).empty(),"unfocused workspace reported a label");
  (void)key(kTab);require(w.focus()==0,"Tab did not focus the first galaxy card");
  require(w.focused_label(width,height,measure)=="Spiral Galaxy","galaxy card label mismatch");
  require(cues==1,"focus change did not play the hover cue");
  (void)key(kTab);(void)key(kTab);(void)key(kTab);
  (void)key(kTab,true);require(w.focus()==2,"Shift+Tab did not retreat the focus ring");
  (void)key(kHome);require(w.focus()==0,"Home did not select the first focusable");
  (void)key(kReturn);require(w.selected_morphology().has_value(),"Return did not select the focused galaxy card");
  require(w.focus()==0,"card selection moved the focus ring");
  (void)key(kEnd);(void)key(kReturn);
  require(w.page()==SandboxPage::Population,"Next activation did not reach the population page");
  require(w.focus()<0,"page transition kept stale focus");
  (void)key(kHome);(void)key(kReturn);
  (void)key(kTab);require(w.focus()==0,"open dropdown did not capture navigation");
  (void)w.handle({InputEventType::EscapePressed},width,height,measure);
  (void)key(kEnd);(void)key(kReturn);
  require(w.page()==SandboxPage::Configuration,"Next activation did not reach the configuration page");
  require(w.focus()<0,"configuration transition kept stale focus");
  const auto measured=w.measure_layout(width,height,measure);const auto&l=measured.base;
  std::vector<UiRect> order{l.cancel,l.morphology,l.population,l.mode_story,l.mode_sandbox};
  for(const auto&row:measured.species_rows)if(l.species_rows.contains(center(row)))order.push_back(row);
  const auto sizes=std::min(std::size_t{4},l.size_buttons.size());
  for(std::size_t i=0;i<sizes;++i)order.push_back(l.size_buttons[i]);
  order.insert(order.end(),{l.seed_input,l.randomize_seed,l.restore_defaults,l.copy_setup,l.create});
  std::stable_sort(order.begin(),order.end(),[](const UiRect&a,const UiRect&b){return a.y!=b.y?a.y<b.y:a.x<b.x;});
  const auto index_of=[&](UiRect r){const auto at=std::ranges::find_if(order,[&](const UiRect&a){return a.x==r.x&&a.y==r.y;});return at==order.end()?-1:static_cast<int>(at-order.begin());};
  const int seed_index=index_of(l.seed_input),species_index=index_of(measured.species_rows[0]),create_index=index_of(l.create);
  require(seed_index>=0&&species_index>=0&&create_index>=0,"focus order replication missed a control");
  (void)key(kHome);require(w.focus()==0,"Home did not focus the first configuration control");
  require(w.focused_label(width,height,measure)=="Back","configuration first control label mismatch");
  for(int i=0;i<seed_index;++i)(void)key(kTab);
  require(w.focus()==seed_index,"Tab did not reach the seed field");
  require(w.focused_label(width,height,measure)=="Galaxy seed","seed field label mismatch");
  (void)key(kReturn);require(w.seed_focused(),"Return on the seed field did not enter edit mode");
  require(w.focus()==seed_index,"entering edit mode moved the focus ring");
  (void)w.handle({InputEventType::TextEntered,{},{},0,"42"},width,height,measure);
  require(w.seed_text()=="42","edit-mode typing did not reach the seed field");
  (void)key(kRight);require(w.focus()==seed_index&&w.seed_focused(),"arrow key escaped edit mode");
  (void)key(kTab);require(!w.seed_focused(),"Tab did not leave edit mode");
  require(w.focus()==seed_index+1,"Tab after edit mode did not advance the ring");
  (void)key(kHome);for(int i=0;i<species_index;++i)(void)key(kTab);
  const auto chosen=key(kReturn);
  require(chosen.kind==NativeNewGameIntentKind::SelectSpecies,"focused species row did not activate selection");
  (void)key(kHome);for(int i=0;i<create_index;++i)(void)key(kTab);
  const auto created=key(kReturn);require(created.kind==NativeNewGameIntentKind::Create,"focused create button did not dispatch the intent");
  (void)w.handle({InputEventType::LeftPressed,{10,10}},width,height,measure);
  require(w.focus()<0,"pointer interaction kept the keyboard focus ring");
}
}
int main()try{galaxy_flow();developer_research_controls();responsive_layout();presentations();mouse_and_text();rendered_facts();portrait_provider();physical_range_presentation();keyboard_focus();std::cout<<"native new-game workspace tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
