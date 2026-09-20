#include "native_developer_diagnostics.hpp"
#include "native_developer_simulation_panel.hpp"
#include "native_developer_empire_monitor.hpp"
#include "native_stellar_observation.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <iostream>
using namespace stellar::core;
using namespace stellar::native_map;
void check(bool v,const char *s){if(!v)throw std::runtime_error(s);}
Point control(const DrawList &draw,std::string_view value){
  for(const auto &c:draw.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->value.starts_with(value)){
    const auto &r=*t->clip;return {r.x+r.width*.5f,r.y+std::min(10.f,r.height*.5f)};
  }
  throw std::runtime_error("Missing diagnostic control: "+std::string(value));
}
int main(int argc,char **argv)try{
  check(argc==3,"Expected catalog/research paths.");
  auto world=seed_persistable_fresh_campaign(42,load_nearby_catalog(argv[1]),{"2050-03-21T00:00:00Z",250,3,0,"terran_baseline",StellarPopulationOptions{},true});
  const int player=world.player_civilization_id;
  const auto alien=*std::ranges::find_if(world.civilizations,[&](const auto &c){return c.id!=player;});
  auto diplomacy=DiplomacyState{}.snapshot();
  diplomacy.claims.push_back({1,alien.id,alien.home_system_id,0,true,{alien.id}});diplomacy.next_claim_id=2;
  CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(argv[2]),std::move(world),DiplomacyState::restore(diplomacy)),{},CampaignFramePolicy::Developer);
  check(campaign_territorial_claims(frame.runtime(),player).empty(),"Unrevealed developer map leaked foreign claims.");
  auto &live=frame.runtime().world().campaign();
  check(!stellar::native_stellar::observed_central_artwork(live,player),"Unexplored core artwork leaked.");
  fully_explore_developer_galaxy(live);
  check(campaign_territorial_claims(frame.runtime(),player).size()==1,"Full exploration failed to show undisclosed claims.");
  check(campaign_territorial_claims(frame.runtime(),99999).empty(),"Full exploration leaked to another observer.");
  check(frame.runtime().diplomacy().build_view_for(player).claims.empty(),"Developer visibility changed diplomatic claim audiences.");
  check(stellar::native_stellar::observed_central_artwork(live,player).has_value(),"Full exploration did not show the central black hole.");
  auto summaries=developer_empire_summaries(frame.runtime());
  check(summaries.size()==live.civilizations.size(),"Empire monitor omitted civilizations.");
  const auto find_alien=[&](const auto &rows)->const DeveloperEmpireSummary&{return *std::ranges::find(rows,alien.id,&DeveloperEmpireSummary::civilization_id);};
  check(find_alien(summaries).active_claims==1,"Monitor omitted undisclosed active claim.");
  auto colony=std::ranges::find(live.colonies,alien.id,&Colony::civilization_id);
  const auto population=find_alien(summaries).population_millions;
  colony->population_millions+=7.;
  check(developer_empire_summaries(frame.runtime()).size()==summaries.size()&&find_alien(developer_empire_summaries(frame.runtime())).population_millions==population+7.,"Monitor retained stale population.");
  colony->population_millions-=7.;
  const auto provenance=*live.developer_provenance;live.developer_provenance.reset();
  bool denied=false;try{(void)developer_empire_summaries(frame.runtime());}catch(const std::invalid_argument&){denied=true;}
  check(denied&&campaign_territorial_claims(frame.runtime(),player).empty(),"Player campaign accepted omniscient monitoring/claims.");
  live.developer_provenance=provenance;
  frame.set_developer_speed(1);
  stellar::app_diagnostics::CampaignDiagnosticMonitor monitor;
  monitor.observe(frame,{},"timestamp");
  const auto before=capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"});
  for(const auto [w,h]:std::array<std::pair<int,int>,4>{{{1280,720},{1920,1080},{3440,1440},{3840,2160}}}){
    NativeDeveloperSimulationPanel panel;DrawList banner;panel.render(banner,w,h,frame);
    const auto hud=NativeUiLayout::for_viewport(w,h);
    for(const auto &command:banner.overlay)if(const auto *text=std::get_if<Text>(&command);text&&text->value.starts_with("DEV CONTROLS")&&text->clip){
      const auto &r=*text->clip;
      check(r.y+r.height<=hud.navigation_bar.y&&r.x>=580.f*hud.scale&&r.x+r.width<hud.day_text.x,"Developer controls overlap navigation, resources or date controls.");
    }
    const auto banner_point=control(banner,"DEV CONTROLS");
    (void)panel.handle({InputEventType::LeftPressed,banner_point},w,h,frame);(void)panel.handle({InputEventType::LeftReleased,banner_point},w,h,frame);
    DrawList controls;panel.render(controls,w,h,frame);
    auto point=control(controls,"PERFORMANCE & DIAGNOSTICS");
    (void)panel.handle({InputEventType::LeftPressed,point},w,h,frame);(void)panel.handle({InputEventType::LeftReleased,point},w,h,frame);
    check(panel.take_diagnostics_request()&&!panel.take_diagnostics_request(),"Diagnostics request missing/sticky.");
    NativeDeveloperDiagnostics window;window.open(monitor);
    const auto draw=[&]{DrawList list;window.render(list,w,h,frame,monitor);return list;};
    const auto click=[&](std::string_view name){auto p=control(draw(),name);(void)window.handle({InputEventType::LeftPressed,p},w,h,monitor);(void)window.handle({InputEventType::LeftReleased,p},w,h,monitor);};
    auto view=draw();(void)control(view,"Unmeasured");
    for(const auto &c:view.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip){const auto &r=*t->clip;
      check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Diagnostics text escaped viewport.");}
    click("RECENT EVENTS");(void)control(draw(),"Observing the isolated");
    click("Record:");click("Errors only");check(monitor.history().detail()==stellar::engine::DiagnosticDetail::ErrorsOnly,"Recording dropdown did not commit.");
    click("Record:");click("Normal");
    check(capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"})==before,"Diagnostics UI modified world/discovery/time.");
    click("CLOSE");check(!window.visible()&&!window.handle({InputEventType::LeftPressed},w,h,monitor),"Closed diagnostics captured gameplay.");
    panel.toggle();controls={};panel.render(controls,w,h,frame);point=control(controls,"EMPIRE MONITOR");
    (void)panel.handle({InputEventType::LeftPressed,point},w,h,frame);(void)panel.handle({InputEventType::LeftReleased,point},w,h,frame);
    check(panel.take_empires_request()&&!panel.take_empires_request(),"Empire monitor request missing/sticky.");
    NativeDeveloperEmpireMonitor empires;empires.open(frame);
    const auto empire_draw=[&]{DrawList list;empires.render(list,w,h,frame);return list;};
    auto empire_view=empire_draw();(void)control(empire_view,"Active claims: 1");
    for(const auto &c:empire_view.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip){const auto &r=*t->clip;
      check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Empire monitor text escaped viewport.");}
    point=control(empire_view,"SHOW HOME SYSTEM");
    (void)empires.handle({InputEventType::LeftPressed,point},w,h,frame);(void)empires.handle({InputEventType::PointerCancelled},w,h,frame);(void)empires.handle({InputEventType::LeftReleased,point},w,h,frame);
    check(empires.visible()&&!empires.take_focus_request(),"Cancelled monitor input navigated.");
    (void)empires.handle({InputEventType::LeftPressed,point},w,h,frame);(void)empires.handle({InputEventType::LeftReleased,point},w,h,frame);
    check(!empires.visible()&&empires.take_focus_request()==alien.home_system_id&&!empires.take_focus_request(),"Monitor did not focus the selected alien home.");
    check(capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"})==before,"Empire monitor changed research, diplomacy, discovery or time.");
  }
  frame.clock().set_speed(StrategicSpeed::Paused);
  NativeDeveloperEmpireMonitor live_monitor;live_monitor.open(frame);
  const auto tick=frame.step_developer();
  check(!tick.completed_substeps.empty(),"Developer monitoring fixture did not advance a real tick.");
  DrawList live_draw;live_monitor.render(live_draw,1920,1080,frame);
  const auto refresh=control(live_draw,"REFRESH NOW");
  (void)live_monitor.handle({InputEventType::LeftPressed,refresh},1920,1080,frame);(void)live_monitor.handle({InputEventType::LeftReleased,refresh},1920,1080,frame);
  live_draw={};live_monitor.render(live_draw,1920,1080,frame);(void)control(live_draw,"Live simulation day 0.25");
  const auto &state=frame.runtime().research().get_civilization(alien.id);
  const auto expected=frame.runtime().research_runtime().authority().build_view(state);
  const auto observed=developer_empire_research(frame.runtime(),alien.id);
  check(expected.active_projects.size()==observed.active_projects.size(),"Monitor disagreed with the authoritative research view.");
  for(std::size_t i=0;i<expected.active_projects.size();++i)check(expected.active_projects[i].stage_progress==observed.active_projects[i].stage_progress,"Monitor changed a research percentage.");
  std::cout<<"Developer diagnostics UI bounds/routing/read-only/live projection checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
