#include "native_startup_workspace.hpp"
#include "native_settings_hub.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_map;using namespace stellar::native_setup;using namespace stellar::native_startup;using namespace stellar::native_startup_ui;
void require(bool ok,std::string_view message){if(!ok)throw std::runtime_error(std::string(message));}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
TextExtent measure(const Text&t){const int cw=std::max(1,t.font_pixel_size*3/5),cols=std::max(1,static_cast<int>(t.wrap_width)/cw);int lines=1,col=0,maxcol=0;for(char c:t.value){if(c=='\n'||col==cols){maxcol=std::max(maxcol,col);++lines;col=c=='\n'?0:1;}else ++col;}return {std::max(maxcol,col)*cw,lines*(t.font_pixel_size+2)};}
NativeNewCampaignSetupView setup(){NativeNewCampaignSetupView v;NativeSpeciesSetupOption s;s.id="terran_baseline";s.display_name="Terran Baseline";s.biochemistry_label="Carbon water";s.preferred_atmosphere_label="Oxygen nitrogen";s.biological_solvent_label="Water";s.gravity_g={1,.1,.5};s.temperature_kelvin={288,20,60};s.pressure_kpa={101,30,80};v.species.push_back(s);v.size_presets={{250,"Small - 250 systems",false},{500,"Medium - 500 systems",true},{1000,"Large - 1,000 systems",false},{2500,"Huge - 2,500 systems",false}};v.pre_warp_civilization_presets={{1,"None",false},{4,"Sparse · 3",false},{6,"Standard · 5",true},{9,"Crowded · 8",false},{13,"Packed · 12",false}};v.ancient_civilization_presets={{0,"None",false},{1,"Rare",true},{2,"Standard",false}};v.default_species_id=s.id;v.default_system_count=500;v.default_pre_warp_civilization_count=6;v.default_ancient_civilization_count=1;return v;}
void responsive(){for(const auto [w,h]:{std::pair{1280,720},{1920,1080},{2560,1440},{3840,2160}}){const auto l=StartupLayout::for_viewport(w,h);for(const auto r:{l.new_campaign,l.load_campaign,l.exit,l.settings,l.return_to_campaign})require(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"entry link escaped viewport");for(const auto r:{l.back,l.primary,l.story_campaign,l.sandbox_campaign})require(l.panel.contains(center(r)),"centered startup control escaped panel");require(l.return_to_campaign.y+l.return_to_campaign.height<=l.new_campaign.y,"Continue overlaps New Game");require(l.load_campaign.y+l.load_campaign.height<=l.new_campaign.y,"entry links overlap");}}
void entry_setup_create(){NativeStartupWorkspace ui;ui.set_setup(setup());auto l=StartupLayout::for_viewport(1280,720);require(ui.handle({InputEventType::LeftPressed,center(l.settings)},1280,720,measure).kind==StartupIntentKind::OpenSettings&&ui.screen()==StartupScreen::Entry,"Settings should retain the startup screen behind its overlay");auto command=ui.handle({InputEventType::LeftPressed,center(l.new_campaign)},1280,720,measure);require(command.kind==StartupIntentKind::OpenModeSelection&&ui.screen()==StartupScreen::ModeSelection,"New Game did not open game type selection");require(ui.handle({InputEventType::LeftPressed,center(l.story_campaign)},1280,720,measure).kind==StartupIntentKind::None&&ui.screen()==StartupScreen::ModeSelection,"Story Campaign must remain unavailable");command=ui.handle({InputEventType::LeftPressed,center(l.sandbox_campaign)},1280,720,measure);require(command.kind==StartupIntentKind::OpenSetup&&ui.screen()==StartupScreen::Setup,"Sandbox did not open setup");const auto flow=stellar::native_setup_ui::GalaxyChoiceLayout::for_viewport(1280,720);
DrawList choices;ui.render(choices,1280,720,measure);bool galaxy_page=false;for(const auto& item:choices.overlay)if(const auto* text=std::get_if<Text>(&item))galaxy_page|=text->value=="CHOOSE GALAXY TYPE";require(galaxy_page,"Sandbox skipped galaxy selection");
for(auto r:{flow.cards[0],flow.next,flow.next})(void)ui.handle({InputEventType::LeftPressed,center(r)},1280,720,measure);
const auto setup_layout=stellar::native_setup_ui::NativeNewGameLayout::for_viewport(1280,720);(void)ui.handle({InputEventType::LeftPressed,center(setup_layout.seed_input)},1280,720,measure);(void)ui.handle({InputEventType::TextEntered,{}, {},0,"142500"},1280,720,measure);command=ui.handle({InputEventType::LeftPressed,center(setup_layout.create)},1280,720,measure);require(command.kind==StartupIntentKind::Create&&command.seed_text=="142500"&&command.species_id=="terran_baseline"&&command.system_count==500,"setup Create did not retain selected DTO input");(void)ui.handle({InputEventType::PointerCancelled},1280,720,measure);require(!ui.wants_text_input(),"focus loss retained startup text input");for(int i=0;i<2;++i)(void)ui.handle({InputEventType::EscapePressed},1280,720,measure);command=ui.handle({InputEventType::EscapePressed},1280,720,measure);require(command.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::ModeSelection,"setup Escape did not return to game type selection");require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,"game type Escape did not return to entry");require(ui.handle({InputEventType::LeftPressed,center(l.exit)},1280,720,measure).kind==StartupIntentKind::Exit,"pre-session Exit was not routed directly");}
void load_and_failure(){NativeStartupWorkspace ui;ui.set_setup(setup());auto l=StartupLayout::for_viewport(1280,720);require(ui.handle({InputEventType::LeftPressed,center(l.load_campaign)},1280,720,measure).kind==StartupIntentKind::OpenLoad,"Load entry intent missing");NativeStartupSaveSlots slots;slots.slots={{"first.player17.json","C:/one/first.player17.json",2},{"second.player17.json","C:/one/second.player17.json",1}};ui.set_slots(std::move(slots));const Point second{l.list.x+10,l.list.y+46*l.scale+10};(void)ui.handle({InputEventType::LeftPressed,second},1280,720,measure);const auto load=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(load.kind==StartupIntentKind::LoadSelected&&load.save_path.filename()=="second.player17.json","Load did not use only the selected slot");ui.begin_operation({7,1,NativeStartupPhase::LoadingSave,"Reading selected save",.4,true,false},StartupOperationOrigin::SavedCampaign);require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==StartupIntentKind::CancelOperation,"busy Escape did not request cancellation");ui.show_failure("Selected save is invalid.");DrawList draw;ui.render(draw,1280,720,measure);bool detail=false;for(const auto&item:draw.overlay)if(const auto*t=std::get_if<Text>(&item);t&&t->value=="Selected save is invalid.")detail=true;require(detail,"failure detail was not visible");}
void long_load_list_scrolls(){NativeStartupWorkspace ui;NativeStartupSaveSlots slots;for(int i=0;i<16;++i)slots.slots.push_back({"slot-"+std::to_string(i)+".player17.json",std::filesystem::path("C:/slots")/("slot-"+std::to_string(i)+".player17.json"),i});ui.set_slots(std::move(slots));const auto l=StartupLayout::for_viewport(1280,720);const float pitch=46*l.scale;const Point gap{l.list.x+20,l.list.y+pitch-2*l.scale};(void)ui.handle({InputEventType::LeftPressed,gap},1280,720,measure);auto before=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(before.save_path.filename()=="slot-0.player17.json","list gap changed the selected save");for(int i=0;i<8;++i)(void)ui.handle({InputEventType::Wheel,center(l.list),{},-1},1280,720,measure);const Point last{l.list.x+20,l.list.y+l.list.height-12};(void)ui.handle({InputEventType::LeftPressed,last},1280,720,measure);const auto load=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(load.save_path.filename()=="slot-15.player17.json","last scrolled save was not selectable at 720p");DrawList draw;ui.render(draw,1280,720,measure);for(const auto&item:draw.overlay)if(const auto*t=std::get_if<Text>(&item);t&&t->value.starts_with("slot-"))require(t->clip&&l.list.contains({t->clip->x+t->clip->width*.5f,t->clip->y+t->clip->height*.5f}),"save text escaped the clipped list viewport");}
void live_campaign_return_lifecycle(){
  NativeStartupWorkspace ui;
  ui.set_setup(setup());
  ui.set_return_to_campaign_available(true);
  ui.show_setup();
  const auto layout=StartupLayout::for_viewport(1280,720);

  auto intent=ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(intent.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::ModeSelection,
          "setup Back did not return to game type selection");
  intent=ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(intent.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,
          "game type Back did not reveal the live-campaign return route");
  DrawList entry;
  ui.render(entry,1280,720,measure);
  bool return_visible=false,exit_visible=false;
  for(const auto&item:entry.overlay)if(const auto*label=std::get_if<Text>(&item)){
    return_visible|=label->value=="Continue";
    exit_visible|=label->value=="Exit to Windows";
  }
  require(return_visible&&exit_visible,
          "live-campaign entry did not distinguish Return from Exit");
  intent=ui.handle({InputEventType::LeftPressed,center(layout.return_to_campaign)},1280,720,measure);
  require(intent.kind==StartupIntentKind::ReturnToCampaign,
          "Return to Campaign button did not emit its distinct result");
  intent=ui.handle({InputEventType::LeftPressed,center(layout.exit)},1280,720,measure);
  require(intent.kind==StartupIntentKind::Exit,
          "Exit to Windows was confused with Return to Campaign");
  intent=ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(intent.kind==StartupIntentKind::ReturnToCampaign,
          "entry Escape did not return to the live campaign");

  ui.show_setup();
  ui.begin_operation({31,1,NativeStartupPhase::Generating,"Seeding",std::nullopt,true,false},
                     StartupOperationOrigin::NewCampaign);
  intent=ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(intent.kind==StartupIntentKind::CancelOperation,
          "generation Escape bypassed explicit cancellation");
  ui.show_entry();
  require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==
              StartupIntentKind::ReturnToCampaign,
          "cancelled generation stranded the existing campaign");

  ui.show_failure("Generation failed without replacing the campaign.");
  intent=ui.handle({InputEventType::LeftPressed,center(layout.back)},1280,720,measure);
  require(intent.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,
          "generation failure Back did not restore the entry screen");
  require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==
              StartupIntentKind::ReturnToCampaign,
          "generation failure stranded the existing campaign");

  ui.set_return_to_campaign_available(false);
  require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==
              StartupIntentKind::Back,
          "cold startup exposed a live-campaign return result");
}
void continue_and_development(){
  NativeStartupWorkspace ui;ui.set_setup(setup());const auto layout=StartupLayout::for_viewport(1280,720);
  require(ui.handle({InputEventType::LeftPressed,center(layout.return_to_campaign)},1280,720,measure).kind==StartupIntentKind::None,"empty Continue started a campaign");
  ui.set_continue_save("C:/native/newest.player17.json");
  auto intent=ui.handle({InputEventType::LeftPressed,center(layout.return_to_campaign)},1280,720,measure);
  require(intent.kind==StartupIntentKind::LoadSelected&&intent.save_path.filename()=="newest.player17.json","Continue did not load the supplied recent save");
  (void)ui.handle({InputEventType::LeftPressed,center(layout.development)},1280,720,measure);
  require(ui.screen()==StartupScreen::Development,"Development menu was inaccessible");
  require(ui.handle({InputEventType::LeftPressed,center(layout.primary)},1280,720,measure).kind==StartupIntentKind::CopyDiagnostics,"Development diagnostics did not route");
  (void)ui.handle({InputEventType::EscapePressed},1280,720,measure);require(ui.screen()==StartupScreen::Entry,"Development did not return to main menu");
}
}
namespace {
void keyboard_focus_traversal(){
  NativeStartupWorkspace ui;ui.set_setup(setup());int cues=0;
  ui.set_hover_callback([&]{++cues;});
  const auto key=[&](std::uint32_t k,bool shift=false){
    InputEvent e{};e.type=InputEventType::KeyPressed;e.key=k;e.shift=shift;
    return ui.handle(e,1280,720,measure);};
  constexpr std::uint32_t kTab=9u,kReturn=13u,kHome=0x4000004au,kEnd=0x4000004du;
  require(ui.focused()<0,"startup workspace began focused");
  require(ui.focused_label(1280,720).empty(),"unfocused workspace reported a label");
  require(key(kTab).captured&&ui.focused()==0&&cues==1,"Tab did not focus New Game");
  require(ui.focused_label(1280,720)=="New Game","New Game label mismatch");
  require(key(kTab).captured&&ui.focused()==1,"Tab did not reach Load");
  require(ui.focused_label(1280,720)=="Load saved campaign","Load label mismatch");
  require(key(kEnd).captured&&ui.focused()==4,"End did not reach Exit");
  require(ui.focused_label(1280,720)=="Exit to Windows","Exit label mismatch");
  require(key(kTab).captured&&ui.focused()==0,"focus did not wrap to New Game");
  require(key(kTab,true).captured&&ui.focused()==4,"Shift+Tab did not wrap to Exit");
  require(key(kHome).captured&&ui.focused()==0,"Home did not focus New Game");
  require(key(kReturn).kind==StartupIntentKind::OpenModeSelection&&ui.screen()==StartupScreen::ModeSelection,
          "Return on New Game did not open mode selection");
  require(ui.focused()<0,"mode transition kept a stale focus index");
  require(key(kTab).captured&&ui.focused()==0,"Mode Selection Tab did not focus Sandbox");
  require(key(kReturn).kind==StartupIntentKind::OpenSetup&&ui.screen()==StartupScreen::Setup,
          "Return on Sandbox did not open setup");
  (void)ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(ui.screen()==StartupScreen::ModeSelection,"setup cancel did not return to mode selection");
  require(key(kTab).captured&&key(kTab).captured&&ui.focused()==1,
          "Mode Selection Tab did not reach Back");
  require(key(kReturn).kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,
          "Return on Back did not restore the entry screen");
  // Development screen: focused primary routes the diagnostics copy.
  for(int i=0;i<4;++i)(void)key(kTab);
  require(ui.focused()==3,"Tab chain did not reach Development");
  require(key(kReturn).kind==StartupIntentKind::None&&ui.screen()==StartupScreen::Development,
          "Return on Development did not open the diagnostics screen");
  require(key(kTab).captured&&ui.focused()==0,"Development Tab did not focus the copy button");
  require(ui.focused_label(1280,720)=="Copy system info","Development primary label mismatch");
  require(key(kReturn).kind==StartupIntentKind::CopyDiagnostics,
          "Return on the copy button did not route diagnostics");
  require(key(kTab).captured&&ui.focused()==1,"Development Tab did not reach Back");
  require(key(kReturn).kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,
          "Development Back did not restore the entry screen");
}
void menu_hover_feedback(){
  for(const auto [w,h]:{std::pair{1280,720},std::pair{1920,1080}}){
    NativeStartupWorkspace ui;ui.set_setup(setup());int cues=0;
    ui.set_hover_callback([&]{++cues;});const auto l=StartupLayout::for_viewport(w,h);
    const auto move=[&](Point point){(void)ui.handle({InputEventType::PointerMove,point},w,h,measure);};
    move(center(l.return_to_campaign));require(cues==0,"disabled Continue played hover audio");
    move(center(l.new_campaign));move(center(l.new_campaign));
    move({l.new_campaign.x+4,l.new_campaign.y+4});require(cues==1,"one control repeated its hover audio");
    move({1,1});move(center(l.new_campaign));require(cues==2,"re-entered menu item was silent");
    (void)ui.handle({InputEventType::PointerCancelled},w,h,measure);
    move(center(l.new_campaign));require(cues==3,"focus loss did not reset hover");
    (void)ui.handle({InputEventType::LeftPressed,center(l.new_campaign)},w,h,measure);
    move(center(l.story_campaign));require(cues==3,"Coming Soon played enabled-item audio");
    move(center(l.sandbox_campaign));require(cues==4,"Sandbox card lacked hover audio");
    (void)ui.handle({InputEventType::LeftPressed,center(l.sandbox_campaign)},w,h,measure);
    const auto flow=stellar::native_setup_ui::GalaxyChoiceLayout::for_viewport(w,h);
    for(auto r:{flow.cards[0],flow.next,flow.next})(void)ui.handle({InputEventType::LeftPressed,center(r)},w,h,measure);
    const auto n=stellar::native_setup_ui::NativeNewGameLayout::for_viewport(w,h);
    move(center(n.randomize_seed));move(center(n.randomize_seed));require(cues==5,"setup hover failed or repeated");
    move(center(n.size_buttons[0]));require(cues==6,"galaxy size hover was silent");
    DrawList draw;ui.render(draw,w,h,measure);ui.render(draw,w,h,measure);require(cues==6,"rendering played audio");
    stellar::native_settings::NativeSettingsHub hub;bool child=false;
    hub.set_callbacks([&](auto){child=true;},[&]{return child;});hub.set_hover_callback([&]{++cues;});hub.open();
    const auto hub_layout=stellar::native_settings::HubLayout::for_viewport(w,h);
    const auto hp=center(hub_layout.categories[1]);
    (void)hub.handle({InputEventType::PointerMove,hp},w,h);(void)hub.handle({InputEventType::PointerMove,hp},w,h);
    require(cues==7,"settings category audio failed or repeated");
    (void)hub.handle({InputEventType::LeftPressed,hp},w,h);
    (void)hub.handle({InputEventType::PointerMove,center(hub_layout.categories[2])},w,h);
    require(cues==7,"covered settings categories played through a modal");
    child=false;hub.close();hub.open();
    (void)hub.handle({InputEventType::LeftPressed,center(hub_layout.categories[4])},w,h);
    (void)hub.handle({InputEventType::PointerMove,hp},w,h);require(cues==7,"Controls help text played hover audio");
    // Keyboard focus: Tab/arrows ring the buttons, Return/Space activate.
    auto key=[&](std::uint32_t k,bool shift=false){
      InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=k;ev.shift=shift;
      return hub.handle(ev,w,h);};
    hub.close();hub.open();require(hub.focused()<0,"hub opened with stale focus");
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    require(key(kTab),"Tab was not consumed by the settings hub");
    require(hub.focused()==0,"Tab did not focus the first category");
    require(cues==8,"focus change did not play the hover cue");
    require(key(kTab)&&hub.focused()==1&&cues==9,"second Tab did not advance focus");
    require(key(kTab,true)&&hub.focused()==0,"Shift+Tab did not move focus back");
    require(key(kDown)&&hub.focused()==1&&key(kRight)&&hub.focused()==2,"arrow keys did not advance focus");
    require(key(kUp)&&hub.focused()==1&&key(kLeft)&&hub.focused()==0,"arrow keys did not retreat focus");
    require(key(kTab,true)&&hub.focused()==5,"Shift+Tab did not wrap focus to Back");
    require(key(kReturn),"Return on focused Back was not consumed");
    require(!hub.visible(),"Return on focused Back did not close the hub");
    hub.open();require(key(kTab)&&key(kSpace),"keyboard activation sequence failed");
    require(child,"Space on a focused category did not open it");child=false;
    require(hub.focused()==0,"activated category did not retain focus");
    // The Controls help view exposes Back as its only focusable; leaving it
    // lands back on the Controls category that invoked it.
    hub.close();hub.open();
    (void)hub.handle({InputEventType::LeftPressed,center(hub_layout.categories[4])},w,h);
    require(key(kTab)&&hub.focused()==0,"Controls view did not focus Back");
    require(key(kReturn)&&hub.focused()==4,"Controls Back did not restore focus to its invoker");
    require(key(kTab)&&hub.focused()==5,"focus did not resume on the category list");
    // Pointer clicks take over from the focus ring.
    (void)hub.handle({InputEventType::LeftPressed,center(hub_layout.categories[0])},w,h);child=false;
    require(hub.focused()<0,"pointer activation did not clear keyboard focus");
    hub.close();hub.open();require(key(kTab)&&hub.focused()==0,"focus restart failed");
    (void)hub.handle({InputEventType::LeftPressed,{1,1}},w,h);
    require(hub.focused()<0&&key(kReturn)&&hub.focused()<0,"activation ran without focus");
  }
}
}
int main()try{menu_hover_feedback();keyboard_focus_traversal();responsive();entry_setup_create();load_and_failure();long_load_list_scrolls();live_campaign_return_lifecycle();continue_and_development();std::cout<<"native startup workspace tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
