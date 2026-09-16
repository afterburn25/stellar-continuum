#include "native_startup_workspace.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_map;using namespace stellar::native_setup;using namespace stellar::native_startup;using namespace stellar::native_startup_ui;
void require(bool ok,std::string_view message){if(!ok)throw std::runtime_error(std::string(message));}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
TextExtent measure(const Text&t){const int cw=std::max(1,t.font_pixel_size*3/5),cols=std::max(1,static_cast<int>(t.wrap_width)/cw);int lines=1,col=0,maxcol=0;for(char c:t.value){if(c=='\n'||col==cols){maxcol=std::max(maxcol,col);++lines;col=c=='\n'?0:1;}else ++col;}return {std::max(maxcol,col)*cw,lines*(t.font_pixel_size+2)};}
NativeNewCampaignSetupView setup(){NativeNewCampaignSetupView v;NativeSpeciesSetupOption s;s.id="terran_baseline";s.display_name="Terran Baseline";s.biochemistry_label="Carbon water";s.preferred_atmosphere_label="Oxygen nitrogen";s.biological_solvent_label="Water";s.gravity_g={1,.1,.5};s.temperature_kelvin={288,20,60};s.pressure_kpa={101,30,80};v.species.push_back(s);v.size_presets={{250,"Small - 250 systems",false},{500,"Medium - 500 systems",true},{1000,"Large - 1,000 systems",false},{2500,"Huge - 2,500 systems",false}};v.default_species_id=s.id;v.default_system_count=500;v.fixed_pre_warp_civilization_count=6;v.fixed_ancient_civilization_count=1;return v;}
void responsive(){for(const auto [w,h]:{std::pair{1280,720},{1920,1080},{2560,1440},{3840,2160}}){const auto l=StartupLayout::for_viewport(w,h);require(l.panel.contains(center(l.new_campaign))&&l.panel.contains(center(l.load_campaign))&&l.panel.contains(center(l.exit))&&l.panel.contains(center(l.settings))&&l.panel.contains(center(l.return_to_campaign))&&l.panel.contains(center(l.back))&&l.panel.contains(center(l.primary)),"startup control escaped panel");require(l.exit.y+l.exit.height<=l.return_to_campaign.y,"return-to-campaign control overlaps Exit to Windows");}}
void entry_setup_create(){NativeStartupWorkspace ui;ui.set_setup(setup());auto l=StartupLayout::for_viewport(1280,720);require(ui.handle({InputEventType::LeftPressed,center(l.settings)},1280,720,measure).kind==StartupIntentKind::OpenSettings&&ui.screen()==StartupScreen::Entry,"Settings should retain the startup screen behind its overlay");auto command=ui.handle({InputEventType::LeftPressed,center(l.new_campaign)},1280,720,measure);require(command.kind==StartupIntentKind::OpenSetup&&ui.screen()==StartupScreen::Setup,"New Campaign did not open setup");const auto setup_layout=stellar::native_setup_ui::NativeNewGameLayout::for_viewport(1280,720);(void)ui.handle({InputEventType::LeftPressed,center(setup_layout.seed_input)},1280,720,measure);(void)ui.handle({InputEventType::TextEntered,{}, {},0,"142500"},1280,720,measure);command=ui.handle({InputEventType::LeftPressed,center(setup_layout.create)},1280,720,measure);require(command.kind==StartupIntentKind::Create&&command.seed_text=="142500"&&command.species_id=="terran_baseline"&&command.system_count==500,"setup Create did not retain selected DTO input");(void)ui.handle({InputEventType::PointerCancelled},1280,720,measure);require(!ui.wants_text_input(),"focus loss retained startup text input");command=ui.handle({InputEventType::EscapePressed},1280,720,measure);require(command.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,"setup Escape did not return to entry");require(ui.handle({InputEventType::LeftPressed,center(l.exit)},1280,720,measure).kind==StartupIntentKind::Exit,"pre-session Exit was not routed directly");}
void load_and_failure(){NativeStartupWorkspace ui;ui.set_setup(setup());auto l=StartupLayout::for_viewport(1280,720);require(ui.handle({InputEventType::LeftPressed,center(l.load_campaign)},1280,720,measure).kind==StartupIntentKind::OpenLoad,"Load entry intent missing");NativeStartupSaveSlots slots;slots.slots={{"first.player17.json","C:/one/first.player17.json",2},{"second.player17.json","C:/one/second.player17.json",1}};ui.set_slots(std::move(slots));const Point second{l.list.x+10,l.list.y+46*l.scale+10};(void)ui.handle({InputEventType::LeftPressed,second},1280,720,measure);const auto load=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(load.kind==StartupIntentKind::LoadSelected&&load.save_path.filename()=="second.player17.json","Load did not use only the selected slot");ui.begin_operation({7,1,NativeStartupPhase::LoadingSave,"Reading selected save",.4,true,false},StartupOperationOrigin::SavedCampaign);require(ui.handle({InputEventType::EscapePressed},1280,720,measure).kind==StartupIntentKind::CancelOperation,"busy Escape did not request cancellation");ui.show_failure("Selected save is invalid.");DrawList draw;ui.render(draw,1280,720,measure);bool detail=false;for(const auto&item:draw.overlay)if(const auto*t=std::get_if<Text>(&item);t&&t->value=="Selected save is invalid.")detail=true;require(detail,"failure detail was not visible");}
void long_load_list_scrolls(){NativeStartupWorkspace ui;NativeStartupSaveSlots slots;for(int i=0;i<16;++i)slots.slots.push_back({"slot-"+std::to_string(i)+".player17.json",std::filesystem::path("C:/slots")/("slot-"+std::to_string(i)+".player17.json"),i});ui.set_slots(std::move(slots));const auto l=StartupLayout::for_viewport(1280,720);const float pitch=46*l.scale;const Point gap{l.list.x+20,l.list.y+pitch-2*l.scale};(void)ui.handle({InputEventType::LeftPressed,gap},1280,720,measure);auto before=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(before.save_path.filename()=="slot-0.player17.json","list gap changed the selected save");for(int i=0;i<8;++i)(void)ui.handle({InputEventType::Wheel,center(l.list),{},-1},1280,720,measure);const Point last{l.list.x+20,l.list.y+l.list.height-12};(void)ui.handle({InputEventType::LeftPressed,last},1280,720,measure);const auto load=ui.handle({InputEventType::LeftPressed,center(l.primary)},1280,720,measure);require(load.save_path.filename()=="slot-15.player17.json","last scrolled save was not selectable at 720p");DrawList draw;ui.render(draw,1280,720,measure);for(const auto&item:draw.overlay)if(const auto*t=std::get_if<Text>(&item);t&&t->value.starts_with("slot-"))require(t->clip&&l.list.contains({t->clip->x+t->clip->width*.5f,t->clip->y+t->clip->height*.5f}),"save text escaped the clipped list viewport");}
void live_campaign_return_lifecycle(){
  NativeStartupWorkspace ui;
  ui.set_setup(setup());
  ui.set_return_to_campaign_available(true);
  ui.show_setup();
  const auto layout=StartupLayout::for_viewport(1280,720);

  auto intent=ui.handle({InputEventType::EscapePressed},1280,720,measure);
  require(intent.kind==StartupIntentKind::Back&&ui.screen()==StartupScreen::Entry,
          "setup Back did not reveal the live-campaign return route");
  DrawList entry;
  ui.render(entry,1280,720,measure);
  bool return_visible=false,exit_visible=false;
  for(const auto&item:entry.overlay)if(const auto*label=std::get_if<Text>(&item)){
    return_visible|=label->value=="RETURN TO CAMPAIGN";
    exit_visible|=label->value=="EXIT TO WINDOWS";
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
}
int main()try{responsive();entry_setup_create();load_and_failure();long_load_list_scrolls();live_campaign_return_lifecycle();std::cout<<"native startup workspace tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
