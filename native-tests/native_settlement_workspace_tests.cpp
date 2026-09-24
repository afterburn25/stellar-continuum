#include "native_settlement_workspace.hpp"
#include "native_system_workspace.hpp"
#include "native_system_view.hpp"

#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_ui;
void require(bool condition,std::string_view expression,int line){if(!condition)throw std::runtime_error("Gate133 check failed at line "+std::to_string(line)+": "+std::string(expression));}
#define REQUIRE(x) require((x),#x,__LINE__)
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
bool has_text(const DrawList&draw,std::string_view value){return std::ranges::any_of(draw.overlay,[&](const auto&item){const auto*text=std::get_if<Text>(&item);return text&&text->value.find(value)!=std::string::npos;});}
bool contains(UiRect outer,UiRect inner){return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.width<=outer.x+outer.width&&inner.y+inner.height<=outer.y+outer.height;}
NativeSettlementTargetPreview preview(bool accepted=true){NativeSettlementTargetPreview p;p.campaign_generation=4;p.revision=8;p.player_civilization_id=7;p.fleet_id=31;p.mission_order_revision=2;p.destination_system_id=6;p.body_id=12;p.kind=NativeSettlementMissionKind::Colony;p.fleet_name="Hopeful Passage";p.personnel_species_name="Terran";p.personnel_millions=120.;p.requires_new_authorization=true;p.formatted_authorization="$1.2B UED";p.formatted_treasury="$5B UED";p.accepted=accepted;p.message=accepted?"Mission can depart.":"Current treasury cannot authorize this mission.";NativeSettlementCandidate c;c.system_id=6;c.body_id=12;c.system_name="Known Haven";c.body_name="Known Haven II";c.natural_habitability=.72;c.unprotected_operational_capacity=.64;c.reach.is_supported=true;c.reach.is_authoritative=true;c.reach.route_distance_light_years=8.5;c.reach.route_system_ids=std::vector<int>{2,4,6};p.candidate=std::move(c);return p;}
NativeSystemSnapshot system(){NativeSystemSnapshot s;s.campaign_generation=4;s.observer_civilization_id=7;s.system_id=2;s.catalog_name="Known";s.survey_level=stellar::core::SystemSurveyLevel::fully_surveyed;NativeSystemBody b;b.id=12;b.orbit_index=2;b.name="Known Haven II";b.kind=stellar::core::PlanetaryBodyKind::Planet;b.radius_earth=1.;b.visual_class=NativeSystemBodyVisualClass::rocky;s.bodies.push_back(std::move(b));return s;}

void active_mission_selection(){NativeSettlementMissionView mission;REQUIRE(!has_active_settlement_target(mission));mission.destination_body_id=12;REQUIRE(has_active_settlement_target(mission));mission.destination_body_id.reset();mission.settlement_body_id=12;REQUIRE(has_active_settlement_target(mission));}

void modal_layout_and_input(){for(auto [w,h]:{std::pair{1280,720},std::pair{1920,1080},std::pair{3840,2160}}){const auto l=SettlementWorkspaceLayout::for_viewport(w,h);REQUIRE(l.panel.x>=0&&l.panel.y>=0&&l.panel.x+l.panel.width<=w&&l.panel.y+l.panel.height<=h);REQUIRE(l.panel.contains(center(l.confirm))&&l.panel.contains(center(l.cancel)));}NativeSettlementWorkspace ui;ui.set_preview(preview());DrawList draw;ui.render(draw,1280,720);const auto l=SettlementWorkspaceLayout::for_viewport(1280,720);REQUIRE(has_text(draw,"COLONY EXPEDITION")&&has_text(draw,"New authorization")&&has_text(draw,"Current treasury")&&has_text(draw,"km")&&has_text(draw,"2 hop(s)")&&has_text(draw,"Travel duration estimate unavailable")&&has_text(draw,"1 month"));for(const auto&item:draw.overlay)if(const auto*text=std::get_if<Text>(&item)){REQUIRE(text->clip.has_value());REQUIRE(contains(l.panel,*text->clip));}REQUIRE(ui.handle({InputEventType::RightPressed,{0,0}},1280,720).captured);REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::Confirm);auto retarget=preview();retarget.requires_new_authorization=false;retarget.authorization_budget_units=0.;retarget.formatted_authorization="$0 UED";ui.set_preview(std::move(retarget));draw={};ui.render(draw,1280,720);REQUIRE(has_text(draw,"Retarget authorization retained")&&has_text(draw,"New charge  $0 UED"));ui.set_preview(preview(false));REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::EscapePressed},1280,720).kind==SettlementWorkspaceCommandKind::Cancel&&!ui.visible());}
void exact_body_right_click_and_panel_refusal(){NativeSystemWorkspace ui;ui.open(system(),1280,720);stellar::native_system_travel::NativeSystemTravelSnapshot travel;travel.campaign_generation=4;travel.observer_civilization_id=7;travel.system_id=2;travel.fleets.push_back({.fleet_id=31,.name="Hopeful Passage",.role=stellar::core::FleetRole::Colony});ui.refresh_travel(travel,31);ui.set_settlement_status(NativeSystemSettlementStatus{31,"Establishing colony",std::optional<int>{2},std::optional<int>{12},7.5,30.});DrawList status_draw;ui.render(status_draw,1280,720);REQUIRE(has_text(status_draw,"Establishing colony")&&has_text(status_draw,"7.5 / 30 days"));const auto spatial=project_system(*ui.snapshot());const auto point=ui.viewport()->world_to_screen(spatial.bodies.front().offset_x,spatial.bodies.front().offset_y);auto command=ui.handle({InputEventType::RightPressed,{point.x,point.y}},1280,720);REQUIRE(command.kind==SystemWorkspaceCommandKind::settlement_target&&command.target_id==12&&command.captured);const auto panel=SystemWorkspaceLayout::for_viewport(1280,720).inspector;command=ui.handle({InputEventType::RightPressed,center(panel)},1280,720);REQUIRE(command.kind==SystemWorkspaceCommandKind::none&&command.captured);ui.set_notice("Selected vessel cannot establish this surveyed body because its personnel or mission role is unavailable.");DrawList notice_draw;ui.render(notice_draw,1280,720);const auto notice=std::ranges::find_if(notice_draw.overlay,[](const auto&item){const auto*text=std::get_if<Text>(&item);return text&&text->value.find("Selected vessel")!=std::string::npos;});REQUIRE(notice!=notice_draw.overlay.end());const auto*notice_text=std::get_if<Text>(&*notice);REQUIRE(notice_text&&notice_text->clip&&contains(panel,*notice_text->clip));REQUIRE(notice_text->at.y>=notice_text->clip->y&&notice_text->at.y<notice_text->clip->y+notice_text->clip->height);ui.discard_campaign();REQUIRE(!ui.visible());}
void matched_gestures(){for(auto [w,h]:{std::pair{1280,720},std::pair{1920,1080},std::pair{3840,2160}}){auto l=SettlementWorkspaceLayout::for_viewport(w,h);REQUIRE(l.panel.contains(center(l.confirm))&&l.panel.contains(center(l.cancel)));}NativeSettlementWorkspace ui;ui.set_preview(preview());auto l=SettlementWorkspaceLayout::for_viewport(1280,720);REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::Confirm);ui.set_preview(preview());l=SettlementWorkspaceLayout::for_viewport(1280,720);REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.cancel)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.cancel)},1280,720).kind==SettlementWorkspaceCommandKind::Cancel&&!ui.visible());ui.set_preview(preview());l=SettlementWorkspaceLayout::for_viewport(1280,720);REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,{1,1}},1280,720).kind==SettlementWorkspaceCommandKind::None&&ui.visible());ui.set_preview(preview());REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::PointerCancelled},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);ui.set_preview(preview());REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1920,1080).kind==SettlementWorkspaceCommandKind::None);ui.set_preview(preview());REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);auto replacement=preview();replacement.revision=9;ui.set_preview(std::move(replacement));REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);ui.set_preview(preview(false));REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::EscapePressed},1280,720).kind==SettlementWorkspaceCommandKind::Cancel&&!ui.visible());}
void drag_out_and_modal_capture(){NativeSettlementWorkspace ui;ui.set_preview(preview());auto l=SettlementWorkspaceLayout::for_viewport(1280,720);REQUIRE(ui.handle({InputEventType::LeftPressed,center(l.confirm)},1280,720).captured);REQUIRE(ui.handle({InputEventType::PointerMove,{1,1}},1280,720).captured);REQUIRE(ui.handle({InputEventType::PointerMove,center(l.confirm)},1280,720).captured);REQUIRE(ui.handle({InputEventType::LeftReleased,center(l.confirm)},1280,720).kind==SettlementWorkspaceCommandKind::None);REQUIRE(ui.handle({InputEventType::LeftReleased,{1,1}},1280,720).captured);}
void keyboard_focus(){
  constexpr std::uint32_t kTab=9u,kReturn=13u,kHome=0x4000004au,kEnd=0x4000004du;
  const int width=1280,height=720;
  const auto key=[&](NativeSettlementWorkspace&ui,std::uint32_t k,bool shift=false){InputEvent e{InputEventType::KeyPressed};e.key=k;e.shift=shift;return ui.handle(e,width,height);};
  NativeSettlementWorkspace ui;ui.set_preview(preview());
  REQUIRE(ui.focus()<0);
  (void)key(ui,kTab);REQUIRE(ui.focus()==0);
  (void)key(ui,kTab);REQUIRE(ui.focus()==1);
  (void)key(ui,kTab,true);REQUIRE(ui.focus()==0);
  (void)key(ui,kEnd);REQUIRE(ui.focus()==1);
  (void)key(ui,kHome);REQUIRE(ui.focus()==0);
  ui.set_preview(preview(false));REQUIRE(ui.focus()<0);
  (void)key(ui,kTab);(void)key(ui,kTab);REQUIRE(ui.focus()==0);
  (void)key(ui,kEnd);REQUIRE(ui.focus()==0);
  const auto cancelled=key(ui,kReturn);
  REQUIRE(cancelled.kind==SettlementWorkspaceCommandKind::Cancel&&!ui.visible());
  ui.set_preview(preview());(void)key(ui,kEnd);REQUIRE(ui.focus()==1);
  const auto confirmed=key(ui,kReturn);
  REQUIRE(confirmed.kind==SettlementWorkspaceCommandKind::Confirm&&ui.visible());
  REQUIRE(ui.focus()==1);
  const auto l=SettlementWorkspaceLayout::for_viewport(width,height);
  (void)ui.handle({InputEventType::LeftPressed,center(l.cancel)},width,height);
  REQUIRE(ui.focus()<0);
  (void)ui.handle({InputEventType::LeftReleased,{1,1}},width,height);
  REQUIRE(ui.visible());
}
}
int main(){try{active_mission_selection();modal_layout_and_input();exact_body_right_click_and_panel_refusal();matched_gestures();drag_out_and_modal_capture();keyboard_focus();std::cout<<"native settlement workspace tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
