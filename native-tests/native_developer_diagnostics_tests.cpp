#include "native_developer_diagnostics.hpp"
#include "native_developer_simulation_panel.hpp"
#include "native_developer_empire_monitor.hpp"
#include "native_stellar_observation.hpp"
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/engine/profiler.hpp>
#include <iostream>
#include <limits>
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
    {
      auto &profiler=stellar::engine::Profiler::instance();
      profiler.set_enabled(true);
      {const auto scope=profiler.span("test.phase","client");}
      profiler.begin_frame();(void)profiler.end_frame();
      InputEvent scroll{InputEventType::Wheel,control(draw(),"Unmeasured"),{},-100.f};
      (void)window.handle(scroll,w,h,monitor);
      (void)control(draw(),"client/test.phase");
      profiler.set_enabled(false);profiler.reset_aggregates();
    }
    // The phase table sorts through TableModel — clicking a column
    // header cycles ascending→descending and resets the scroll. The
    // first rendered row's phase text is the oracle.
    {
      const auto top_phase=[&](const DrawList &d){
        // Rows sit below the "Phase" header; the first row's phase text
        // is the leftmost cell at the smallest row y.
        float header_y=-1.f;
        for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->value.starts_with("Phase"))header_y=t->clip->y;
        float top=std::numeric_limits<float>::max();
        for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->clip->y>header_y)top=std::min(top,t->clip->y);
        std::string phase;float left=std::numeric_limits<float>::max();
        for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->clip->y==top&&t->clip->x<left){left=t->clip->x;phase=t->value;}
        return phase;};
      click("Phase");
      const auto ascending_top=top_phase(draw());
      click("Phase");
      const auto descending_top=top_phase(draw());
      check(!ascending_top.empty()&&!descending_top.empty()&&ascending_top<descending_top,"Column sort did not reorder the phase table.");
      bool marker=false;
      for(const auto &c:draw().overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->value=="Phase v")marker=true;
      check(marker,"Sort direction marker missing from the phase header.");
    }
    for(const auto &c:view.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip){const auto &r=*t->clip;
      check(r.x>=0&&r.y>=0&&r.x+r.width<=w&&r.y+r.height<=h,"Diagnostics text escaped viewport.");}
    click("RECENT EVENTS");
    // The newest-first list renders eight rows; the session-start record
    // is the oldest entry, so scroll to the tail before asserting it.
    {const auto anchor=control(draw(),"Colony logistics");InputEvent scroll{InputEventType::Wheel,anchor,{},-1000.f};(void)window.handle(scroll,w,h,monitor);}
    (void)control(draw(),"Observing the isolated");
    click("Record:");click("Errors only");check(monitor.history().detail()==stellar::engine::DiagnosticDetail::ErrorsOnly,"Recording dropdown did not commit.");
    click("Record:");click("Normal");
    check(capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"})==before,"Diagnostics UI modified world/discovery/time.");
    click("ENTITIES");
    {
      const auto entity_view=draw();bool census=false,domain_row=false;
      for(const auto &c:entity_view.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip){
        if(t->value.find("projected entities")!=std::string::npos&&t->value.find("KiB")!=std::string::npos)census=true;
        if(t->value.find("system ")!=std::string::npos&&t->value.find("·")!=std::string::npos)domain_row=true;
      }
      check(census&&domain_row,"Entities inspector did not project the campaign world.");
    }
    // A second refresh reconciles through sync_campaign_world — the
    // header reports drift counts instead of a fresh projection.
    click("ENTITIES");
    {
      const auto synced=draw();bool sync_counts=false;
      for(const auto &c:synced.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)
        if(t->value.find("synced +")!=std::string::npos)sync_counts=true;
      check(sync_counts,"Entities inspector did not reuse the projected world.");
    }
    // Entity rows form the projected hierarchy — clicking a parent row
    // collapses its subtree, and the collapse survives sync refreshes.
    // The list clamps to 14 rendered rows and the projection is larger,
    // so row identity — not row count — is the oracle: clip.x encodes
    // the depth indent, making a parent's rendered children the
    // contiguous deeper-indented run below it.
    {
      const auto rows=[](const DrawList &d){
        std::vector<std::pair<std::string,float>> out;
        for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)
          if(t->value.starts_with("▾ ")||t->value.starts_with("› ")||t->value.starts_with("· "))out.emplace_back(t->value,t->clip->x);
        return out;};
      const auto expanded_view=rows(draw());
      bool pane_hint=false;
      for(const auto &c:draw().overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->value.starts_with("Select a row"))pane_hint=true;
      check(pane_hint,"Entities inspector rendered no selection hint.");
      std::size_t pi=expanded_view.size();
      for(std::size_t i=0;i<expanded_view.size();++i)if(expanded_view[i].first.starts_with("▾ ")){pi=i;break;}
      check(pi<expanded_view.size(),"Entities tree rendered no expanded parent rows.");
      const std::string collapsed_text="› "+expanded_view[pi].first.substr(std::string_view("▾ ").size());
      std::size_t children=0;
      for(std::size_t k=pi+1;k<expanded_view.size()&&expanded_view[k].second>expanded_view[pi].second;++k)++children;
      check(children>0,"Expanded parent row rendered no children.");
      const auto parent_point=control(draw(),expanded_view[pi].first);
      (void)window.handle({InputEventType::LeftPressed,parent_point},w,h,monitor);
      (void)window.handle({InputEventType::LeftReleased,parent_point},w,h,monitor);
      const auto collapsed_view=rows(draw());
      check(collapsed_view.size()>pi&&collapsed_view[pi].first==collapsed_text,"Collapsing a parent row did not collapse its glyph.");
      bool hidden=std::ranges::none_of(collapsed_view,[&](const auto &r){return r.first==expanded_view[pi+1].first;});
      for(std::size_t k=pi+1;hidden&&k<collapsed_view.size()&&k+children<expanded_view.size();++k)
        hidden=collapsed_view[k].first==expanded_view[k+children].first;
      check(hidden,"Collapsing a parent row did not hide its subtree.");
      // Sync rebuild keeps the user's collapse.
      click("ENTITIES");
      (void)control(draw(),collapsed_text);
      const auto collapsed_point=control(draw(),collapsed_text);
      (void)window.handle({InputEventType::LeftPressed,collapsed_point},w,h,monitor);
      (void)window.handle({InputEventType::LeftReleased,collapsed_point},w,h,monitor);
      const auto restored=rows(draw());
      bool same=restored.size()==expanded_view.size();
      for(std::size_t k=0;same&&k<expanded_view.size();++k)same=restored[k].first==expanded_view[k].first;
      check(same,"Re-expanding a parent row did not restore its subtree.");
      // Keyboard contract: arrows move a cyan-marked selection over the
      // flattened rows, Left collapses an expanded parent or jumps to
      // the parent row, Right expands or descends, Home/End jump the
      // ends and the scroll window follows the selection.
      {
        const auto selected=[](const DrawList &d){
          for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&
              t->color.g==221&&t->color.b==240&&
              (t->value.starts_with("▾ ")||t->value.starts_with("› ")||t->value.starts_with("· ")))return t->value;
          return std::string{};};
        const auto key=[&](std::uint32_t k){InputEvent e{InputEventType::KeyPressed};e.key=k;(void)window.handle(e,w,h,monitor);};
        constexpr std::uint32_t kReturn=13u,kSpace=32u;
        constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
        constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
        key(kEnd);check(selected(draw())==rows(draw()).back().first,"End did not select the last entity row.");
        key(kHome);check(selected(draw())==expanded_view[0].first,"Home did not select the first entity row.");
        key(kDown);check(selected(draw())==expanded_view[1].first,"Down did not advance the entity selection.");
        key(kUp);check(selected(draw())==expanded_view[0].first,"Up did not move the entity selection back.");
        for(std::size_t i=0;i<pi;++i)key(kDown);
        check(selected(draw())==expanded_view[pi].first,"Arrows did not reach the parent row.");
        key(kLeft);check(selected(draw())==collapsed_text,"Left did not collapse the selected parent row.");
        key(kRight);check(selected(draw())==expanded_view[pi].first,"Right did not re-expand the selected row.");
        key(kDown);check(selected(draw())==expanded_view[pi+1].first,"Down did not descend to the first child row.");
        key(kLeft);check(selected(draw())==expanded_view[pi].first,"Left on a child row did not jump to its parent.");
        key(kSpace);check(selected(draw())==collapsed_text,"Space did not toggle the selected parent row.");
        key(kReturn);check(selected(draw())==expanded_view[pi].first,"Return did not re-expand the selected row.");
        // The selection feeds a detail pane — the projected entity's
        // tag fields list beside the rows.
        const auto tag_line=[&](std::string_view row_text){
          const auto label_part=std::string(row_text.substr(row_text.find(' ')+1));
          return "tag campaign."+label_part.substr(0,label_part.find(' '));};
        const auto has_text=[](const DrawList &d,const std::string &v){
          for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip&&t->value==v)return true;
          return false;};
        check(has_text(draw(),tag_line(expanded_view[pi].first)),"Selected entity did not surface its tag fields.");
        // A plain row click selects without toggling — a leaf's tag
        // fields list too (the glyph may flip if the child is a parent).
        const auto child_point=control(draw(),expanded_view[pi+1].first);
        (void)window.handle({InputEventType::LeftPressed,child_point},w,h,monitor);
        (void)window.handle({InputEventType::LeftReleased,child_point},w,h,monitor);
        const auto sel=selected(draw());
        check(sel.substr(sel.find(' ')+1)==expanded_view[pi+1].first.substr(expanded_view[pi+1].first.find(' ')+1),"Clicking a row did not select it.");
        check(has_text(draw(),tag_line(expanded_view[pi+1].first)),"Clicked row did not surface its tag fields.");
      }
    }
    check(capture_developer_campaign_json(frame.runtime(),{0,"test","2050-03-21T00:00:00Z"})==before,"Entities inspector modified world state.");
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
