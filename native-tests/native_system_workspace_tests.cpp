#include "native_system_workspace.hpp"
#include "native_system_view.hpp"
#include "native_ui_layout.hpp"
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>

using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_ui;
namespace fs=std::filesystem;
namespace {
void require(bool value,const std::string&message){if(!value)throw std::runtime_error(message);}
CampaignFrame make_frame(const fs::path&root,const fs::path&catalog){return {IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),seed_persistable_fresh_campaign(120500,load_nearby_catalog(catalog),{"2044-05-06T07:08:09Z",500,6,1,"terran_baseline"})),StrategicClock{},CampaignFramePolicy::Player};}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
bool same_camera(const SystemSpatialViewport&a,const SystemSpatialViewport&b){return std::abs(a.center_x-b.center_x)<.001f&&std::abs(a.center_y-b.center_y)<.001f&&std::abs(a.scale-b.scale)<.00001f;}
bool has_central_star(const DrawList&draw,const SystemSpatialViewport&camera,Color color){return std::ranges::any_of(draw.world,[&](const WorldCommand&item){const auto*circle=std::get_if<Circle>(&item);return circle&&std::abs(circle->center.x-camera.center_x)<.001f&&std::abs(circle->center.y-camera.center_y)<.001f&&circle->color.r==color.r&&circle->color.g==color.g&&circle->color.b==color.b&&circle->color.a==color.a;});}
}

int main(int argc,char**argv)try{
  require(argc==3,"Usage: native_system_workspace_tests <research-root> <catalog>");
  auto campaign=make_frame(fs::absolute(argv[1]),fs::absolute(argv[2]));
  NativeSystemViewController controller;auto built=controller.build(campaign,1,sol_system_id);
  require(built.snapshot.has_value(),"fresh Sol view unavailable");
  const auto reference=*built.snapshot;auto display=reference;
  const auto display_earth=std::ranges::find(display.bodies,earth_body_id,&NativeSystemBody::id);
  require(display_earth!=display.bodies.end(),"Sol view lacks Earth");
  display_earth->positive_signatures={NativePositiveSignature::rare_resource,NativePositiveSignature::anomaly,NativePositiveSignature::activity};
  const auto projected=project_system(display);
  int image_requests{},texture_requests{};
  NativeSystemWorkspace workspace([&](const SystemBodyAppearance&appearance){++image_requests;if(appearance.texture_key)++texture_requests;return std::shared_ptr<const RgbaImage>{};});
  workspace.open(std::move(display),1280,720);
  require(workspace.visible()&&workspace.system_id()==sol_system_id,"workspace did not open fresh Sol");
  DrawList draw;workspace.render(draw,1280,720);
  require(!draw.world.empty()&&image_requests>0&&texture_requests>0,"workspace did not request eligible Sol appearances");
  require(has_central_star(draw,*workspace.viewport(),{255,230,150,245}),"full-survey Sol did not use the native G-class palette");
  const auto earth=std::ranges::find(projected.bodies,earth_body_id,&SystemSpatialBodyMarker::body_id);
  require(earth!=projected.bodies.end(),"Sol projection lacks Earth");
  auto earth_screen=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
  auto command=workspace.handle({InputEventType::LeftPressed,{earth_screen.x,earth_screen.y}},1280,720);
  require(command.captured&&workspace.selected_body_id()==earth_body_id,"drawn Earth was not selected");
  (void)workspace.handle({InputEventType::LeftReleased,{earth_screen.x,earth_screen.y}},1280,720);

  draw={};workspace.render(draw,1280,720);const auto layout720=SystemWorkspaceLayout::for_viewport(1280,720);
  int clipped_inspector_text{};
  for(const auto&item:draw.overlay)if(const auto*label=std::get_if<Text>(&item);label&&label->clip&&layout720.inspector.contains(label->at)){++clipped_inspector_text;require(label->at.y<layout720.inspector.y+layout720.inspector.height,"720p inspector content escaped its panel");}
  require(clipped_inspector_text>=12,"full three-signature inspector was not rendered compactly and clipped");
  const auto protected_camera=*workspace.viewport();const auto protected_selection=workspace.selected_body_id();const auto panel_point=center(layout720.inspector);
  (void)workspace.handle({InputEventType::LeftPressed,panel_point},1280,720);
  (void)workspace.handle({InputEventType::PointerMove,{panel_point.x+24,panel_point.y+16},{24,16}},1280,720);
  (void)workspace.handle({InputEventType::Wheel,panel_point,{},3},1280,720);
  (void)workspace.handle({InputEventType::LeftReleased,panel_point},1280,720);
  require(same_camera(protected_camera,*workspace.viewport())&&workspace.selected_body_id()==protected_selection,"inspector gestures changed camera or selected a covered body");

  const Point anchor{500,350};const auto before=workspace.viewport()->screen_to_world(anchor.x,anchor.y);
  (void)workspace.handle({InputEventType::Wheel,anchor,{},2},1280,720);const auto after=workspace.viewport()->screen_to_world(anchor.x,anchor.y);
  require(std::abs(before.x-after.x)<.01&&std::abs(before.y-after.y)<.01,"workspace zoom lost its pointer anchor");
  const auto prior_center_x=workspace.viewport()->center_x,prior_scale=workspace.viewport()->scale;
  (void)workspace.handle({InputEventType::LeftPressed,{20,300}},1280,720);(void)workspace.handle({InputEventType::PointerMove,{57,323},{37,23}},1280,720);(void)workspace.handle({InputEventType::LeftReleased,{57,323}},1280,720);
  require(std::abs(workspace.viewport()->center_x-prior_center_x-37)<.01,"workspace drag did not pan");
  draw={};workspace.render(draw,1920,1080);
  require(workspace.viewport()->scale==prior_scale&&std::abs(workspace.viewport()->center_x-prior_center_x-37)>.01,"resize discarded or failed to relocate the existing camera");
  const auto system_layout=SystemWorkspaceLayout::for_viewport(1920,1080);const auto global_layout=NativeUiLayout::for_viewport(1920,1080);
  require(global_layout.hit(center(system_layout.back),false)==UiAction::None&&global_layout.hit(center(system_layout.reset),false)==UiAction::None,"system controls overlap the actual main toolbar routing");
  (void)workspace.handle({InputEventType::LeftPressed,center(system_layout.reset)},1920,1080);const auto fitted=SystemSpatialViewport::fit(projected,1920,1080);
  require(std::abs(workspace.viewport()->scale-fitted.scale)<.0001,"reset fit did not restore the fitted camera");
  command=workspace.handle({InputEventType::LeftPressed,center(system_layout.back)},1920,1080);
  require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Back did not route to the system workspace");
  workspace.discard_campaign();require(!workspace.visible()&&!workspace.selected_body_id()&&!workspace.campaign_generation(),"campaign replacement retained system state or image eligibility");

  auto recon=reference;recon.campaign_generation=2;recon.survey_level=SystemSurveyLevel::partially_surveyed;recon.archetype.reset();recon.primary_stellar_class.reset();
  for(auto&body:recon.bodies){body.details.reset();body.sol_texture_key.reset();body.visual_class=body.kind==PlanetaryBodyKind::Moon?NativeSystemBodyVisualClass::unknown_moon:NativeSystemBodyVisualClass::unknown_planet;}
  image_requests=texture_requests=0;workspace.open(std::move(recon),1280,720);draw={};workspace.render(draw,1280,720);
  require(image_requests>0&&texture_requests==0,"reconnaissance workspace requested hidden texture eligibility");
  require(has_central_star(draw,*workspace.viewport(),{135,150,174,235}),"reconnaissance star exposed a known spectral palette");
  command=workspace.handle({InputEventType::EscapePressed},1280,720);require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Escape did not request galaxy return");
  workspace.close();require(!workspace.visible(),"workspace close retained the system");
  std::cout<<"native system workspace cases passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"native system workspace failed: "<<e.what()<<'\n';return 1;}
