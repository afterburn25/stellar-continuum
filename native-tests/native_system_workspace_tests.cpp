#include "native_system_workspace.hpp"
#include "native_system_view.hpp"
#include "native_system_travel.hpp"
#include "native_fleet_controller.hpp"
#include "native_ui_layout.hpp"
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <ranges>
#include <stdexcept>
#include <vector>

using namespace stellar::core;
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::native_system_ui;
using namespace stellar::native_system_travel;
using namespace stellar::native_fleet;
namespace fs=std::filesystem;
namespace {
void require(bool value,const std::string&message){if(!value)throw std::runtime_error(message);}
CampaignFrame make_frame(const fs::path&root,const fs::path&catalog){return {IntegratedAdaptiveCampaignRuntime::create_fresh(load_adaptive_research_strategic_runtime(root),seed_persistable_fresh_campaign(120500,load_nearby_catalog(catalog),{"2044-05-06T07:08:09Z",500,6,1,"terran_baseline"})),StrategicClock{},CampaignFramePolicy::Player};}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
bool same_camera(const SystemSpatialViewport&a,const SystemSpatialViewport&b){return std::abs(a.center_x-b.center_x)<.001f&&std::abs(a.center_y-b.center_y)<.001f&&std::abs(a.scale-b.scale)<.00001f;}
bool overlaps(UiRect left,UiRect right){return left.x<right.x+right.width&&left.x+left.width>right.x&&left.y<right.y+right.height&&left.y+left.height>right.y;}
std::vector<UiRect> visible_body_label_bounds(const DrawList&draw,const NativeSystemSnapshot&snapshot){std::vector<UiRect> result;for(const auto&item:draw.world){const auto*label=std::get_if<Text>(&item);if(!label||std::ranges::none_of(snapshot.bodies,[&](const auto&body){return body.name==label->value;}))continue;result.push_back({label->at.x,label->at.y,static_cast<float>(label->value.size()*7u),14.f});}return result;}
bool has_body_label(const DrawList&draw,const std::string&value){return std::ranges::any_of(draw.world,[&](const WorldCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value==value;});}
bool has_overlay_text(const DrawList&draw,const std::string&value){return std::ranges::any_of(draw.overlay,[&](const UiOverlayCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value.find(value)!=std::string::npos;});}
bool has_central_stellar_image(const DrawList&draw,const SystemSpatialViewport&camera){return std::ranges::any_of(draw.world,[&](const WorldCommand&item){const auto*image=std::get_if<Image>(&item);return image&&image->resource&&image->resource->width()==NativeCelestialAppearanceRenderer::stellar_texture_size&&std::abs(image->destination.x+image->destination.width*.5f-camera.center_x)<.001f&&std::abs(image->destination.y+image->destination.height*.5f-camera.center_y)<.001f;});}
std::size_t filled_lane_count(const DrawList&draw,UiRect field){return static_cast<std::size_t>(std::ranges::count_if(draw.overlay,[&](const UiOverlayCommand&item){const auto*mesh=std::get_if<TriangleMesh>(&item);return mesh&&mesh->vertices.size()==3&&mesh->indices==std::vector<int>({0,1,2})&&mesh->clip&&mesh->clip->x==field.x&&mesh->clip->y==field.y&&mesh->clip->width==field.width&&mesh->clip->height==field.height;}));}
}

int main(int argc,char**argv)try{
  require(argc==3,"Usage: native_system_workspace_tests <research-root> <catalog>");
  auto campaign=make_frame(fs::absolute(argv[1]),fs::absolute(argv[2]));
  NativeSystemViewController controller;auto built=controller.build(campaign,1,sol_system_id);
  require(built.snapshot.has_value(),"fresh Sol view unavailable");
  const auto reference=*built.snapshot;auto display=reference;
  {
    NativeSystemWorkspace camera_test;camera_test.open(reference,1920,1080);
    const auto uranus=std::ranges::find_if(reference.bodies,[](const auto& b){return b.sol_texture_key==std::optional<std::string>{"uranus"};});
    require(uranus!=reference.bodies.end()&&camera_test.select_body(uranus->id),"Outer-planet camera fixture missing");
    camera_test.focus_selected_body(1920,1080);const auto area=SystemWorkspaceLayout::for_viewport(1920,1080).world_field;
    const Point focus{area.x+area.width*.5f,area.y+area.height*.5f};
    (void)camera_test.handle({.type=InputEventType::Wheel,.position=focus,.wheel_y=2},1920,1080);
    const auto projected=project_system(reference);
    require(camera_test.viewport()->hit_body(projected,focus.x,focus.y)==uranus->id,"Expanded orbit camera clamped away from its anchor");
    (void)camera_test.handle({.type=InputEventType::Wheel,.position=focus,.wheel_y=100},1920,1080);
    require(camera_test.viewport()->scale==55.f&&camera_test.viewport()->hit_body(projected,focus.x,focus.y)==uranus->id,"High planet zoom lost its selected anchor");
    DrawList magnified;camera_test.render(magnified,1920,1080);require(has_overlay_text(magnified,"Zoom ")&&camera_test.magnification()>55.f,"Magnification indicator did not follow planet zoom");
    require(camera_test.tracked_body_id()==uranus->id,"Close planet zoom did not acquire tracking");
    const auto assert_tracked=[&]{const auto geometry=project_system(*camera_test.snapshot());const auto body=std::ranges::find(geometry.bodies,uranus->id,&SystemSpatialBodyMarker::body_id);const auto point=camera_test.viewport()->world_to_screen(body->offset_x,body->offset_y);
      require(std::hypot(point.x-focus.x,point.y-focus.y)<.1f,"Orbiting planet drifted out of the close camera");};
    camera_test.set_simulation_days(reference.simulation_days+400);assert_tracked();
    auto refreshed=reference;refreshed.simulation_days+=800;camera_test.refresh(refreshed);assert_tracked();
    const auto paused_camera=*camera_test.viewport();camera_test.set_simulation_days(refreshed.simulation_days);require(same_camera(paused_camera,*camera_test.viewport()),"Paused clock moved the tracking camera");
    camera_test.reset_fit(1920,1080);DrawList fitted;camera_test.render(fitted,1920,1080);
    require(!camera_test.tracked_body_id(),"Fit System did not release planet tracking");
    require(has_overlay_text(fitted,"Zoom 1.00x"),"Magnification indicator did not follow Fit System");
    camera_test.inspect_small_body(1);camera_test.focus_small_body(1920,1080);DrawList draw;camera_test.render(draw,1920,1080);
    require(camera_test.small_body_statistics().visible>0&&camera_test.viewport()->scale>5,"Small ice-body focus failed to magnify the actual body");
    const auto focused=camera_test.focused_small_body();
    require(focused&&focused->id==small_body_instance(camera_test.snapshot()->small_body_fields[1],0).id,"Focused small body did not resolve to the inspected instance");
    const auto solids=camera_test.small_body_scene();
    require(solids&&solids->instances().size()==camera_test.small_body_statistics().solid_bodies,"Submitted small-body scene did not match the render statistics");
    if(focused->material>=stellar::core::SmallBodyMaterial::WaterIce)
      require(std::ranges::any_of(solids->instances(),[](const auto& instance){return instance.material.dielectric.has_value();}),"Focused icy solid lost its dielectric optics");
  }
  {
    NativeSystemWorkspace tracked;tracked.open(reference,1920,1080);
    const auto earth=std::ranges::find_if(reference.bodies,[](const auto& b){return b.sol_texture_key==std::optional<std::string>{"earth"};});require(earth!=reference.bodies.end(),"Earth missing for hourly motion test");
    require(tracked.select_body(earth->id),"Could not select Earth");
    require(tracked.tracked_body_id()==earth->id,"Selecting an overview planet did not acquire tracking");tracked.focus_selected_body(1920,1080);
    const auto field=SystemWorkspaceLayout::for_viewport(1920,1080).world_field;const Point target=center(field);
    (void)tracked.handle({.type=InputEventType::Wheel,.position=target,.wheel_y=30},1920,1080);
    require(tracked.tracked_body_id()==earth->id,"Wheel zoom did not track Earth");
    StrategicClock hourly;hourly.restore(reference.simulation_days);hourly.set_days_per_second(1./24.);hourly.set_speed(StrategicSpeed::Normal);
    (void)hourly.advance(1);require(std::abs(hourly.simulation_days()-reference.simulation_days-1./24.)<1e-9,"1x advanced more than an in-game hour");
    tracked.set_simulation_days(hourly.simulation_days());const auto after=project_system(*tracked.snapshot());const auto before=project_system(reference);
    const auto b0=std::ranges::find(before.bodies,earth->id,&SystemSpatialBodyMarker::body_id),b1=std::ranges::find(after.bodies,earth->id,&SystemSpatialBodyMarker::body_id);
    const double angle=std::abs(std::atan2(b0->offset_x*b1->offset_y-b0->offset_y*b1->offset_x,b0->offset_x*b1->offset_x+b0->offset_y*b1->offset_y));
    require(angle>.00068&&angle<.00076,"Earth does not advance roughly 0.041 degrees per game hour");
    const auto p=tracked.viewport()->world_to_screen(b1->offset_x,b1->offset_y);require(std::hypot(p.x-target.x,p.y-target.y)<.1f,"Hourly motion displaced tracked Earth");
    tracked.reset_fit(1920,1080);require(tracked.select_body(earth->id),"Earth selection lost");tracked.focus_selected_body(1920,1080);
    const Point empty{field.x+2,field.y+2};(void)tracked.handle({.type=InputEventType::LeftPressed,.position=empty},1920,1080);
    (void)tracked.handle({.type=InputEventType::PointerMove,.position={empty.x+20,empty.y+10},.delta={20,10}},1920,1080);
    (void)tracked.handle({.type=InputEventType::LeftReleased,.position=empty},1920,1080);require(!tracked.tracked_body_id(),"Manual panning did not release tracking");
    const auto free_camera=*tracked.viewport();tracked.set_simulation_days(hourly.simulation_days()+10);require(same_camera(free_camera,*tracked.viewport()),"Released tracking snapped camera back to planet");
  }
  // Both legacy spectral-only Sol and modern physical records select supplied
  // artwork through the same observer boundary used by the galaxy map.
  NativeSystemWorkspace artwork_ui;std::string observed_art;float observed_radius{};
  const auto sentinel=RgbaImage::create(1,1,{255,200,80,255});
  artwork_ui.set_stellar_art([&](DrawList& draw,Point p,float radius,const stellar::native_stellar::ObservedStellarArtwork& art,double,UiRect clip){
    observed_art=art.id;observed_radius=radius;draw.world.emplace_back(Image{sentinel,{p.x-radius,p.y-radius,radius*2,radius*2},{},{255,255,255,255},clip});
  });
  auto legacy=reference;legacy.stellar_object.reset();legacy.primary_stellar_class=StellarClass::GYellowDwarf;
  artwork_ui.open(legacy,1280,720);DrawList legacy_draw;artwork_ui.render(legacy_draw,1280,720);
  require(observed_art=="g-yellow","Spectral-only Sol still uses the obsolete procedural star");
  for(const auto& command:legacy_draw.world)if(const auto* image=std::get_if<Image>(&command)){
    const auto* vp=artwork_ui.viewport();
    if(std::abs(image->destination.x+image->destination.width*.5f-vp->center_x)<1&&std::abs(image->destination.y+image->destination.height*.5f-vp->center_y)<1)
      require(image->resource==sentinel,"Procedural star was painted over supplied artwork");
  }
  const auto* star_viewport=artwork_ui.viewport();const Point star_position{star_viewport->center_x,star_viewport->center_y};
  (void)artwork_ui.handle({InputEventType::Wheel,star_position,{},1000},1280,720);
  legacy_draw={};artwork_ui.render(legacy_draw,1280,720);
  require(artwork_ui.viewport()->scale==55.f&&observed_radius<=350.f&&observed_radius>=300.f,"Maximum system zoom invalid: scale="+std::to_string(artwork_ui.viewport()->scale)+" star radius="+std::to_string(observed_radius));
  legacy.stellar_object=generate_stellar_physics(1,StellarObjectType::OHotBlueStar);
  artwork_ui.refresh(legacy);observed_art.clear();legacy_draw={};artwork_ui.render(legacy_draw,1280,720);
  require(observed_art=="o-hot-blue","Physical stellar identity must take precedence over legacy class");
  for(const auto survey:{SystemSurveyLevel::unknown,SystemSurveyLevel::detected,SystemSurveyLevel::partially_surveyed}){
    require(!stellar::native_stellar::observed_stellar_artwork(survey,legacy.stellar_object,legacy.primary_stellar_class),"Incomplete survey leaked supplied stellar identity");
    if(survey==SystemSurveyLevel::partially_surveyed){
      legacy.survey_level=survey;artwork_ui.refresh(legacy);observed_art.clear();legacy_draw={};artwork_ui.render(legacy_draw,1280,720);
      require(observed_art.empty(),"Reconnaissance view leaked supplied stellar identity");
    }
  }
  for(int spectral=0;spectral<=static_cast<int>(StellarClass::Pulsar);++spectral){
    const auto value=static_cast<StellarClass>(spectral);
    const auto art=stellar::native_stellar::observed_stellar_artwork(SystemSurveyLevel::fully_surveyed,std::nullopt,value);
    require(art.has_value()==(value!=StellarClass::Protostar),"Legacy stellar class lost its supplied artwork mapping");
  }
  // Read-only preparation is bound to the exact admitted body and observer.
  NativeSystemWorkspace preparation_ui;
  preparation_ui.open(reference,1280,720);
  require(preparation_ui.select_body(earth_body_id),"preparation fixture lacks Earth");
  stellar::native_settlement_preparation::View preparation;
  preparation.campaign_generation=reference.campaign_generation;
  preparation.player_civilization_id=reference.observer_civilization_id;
  preparation.system_id=reference.system_id;preparation.body_id=earth_body_id;
  preparation.species_id="terran_baseline";preparation.species_name="Humans";
  preparation.suitability.can_found_current_colony=true;
  preparation_ui.set_settlement_preparation(preparation);
  require(preparation_ui.settlement_preparation().has_value(),"admitted preparation was rejected");
  DrawList status_draw;preparation_ui.render(status_draw,1280,720);
  require(!has_overlay_text(status_draw,"SETTLEMENT IN PROGRESS"),"missing settlement status appeared in the body inspector");
  preparation_ui.set_settlement_status(NativeSystemSettlementStatus{91,"Establishing colony",std::optional<int>{reference.system_id},std::optional<int>{earth_body_id+1},4.,30.});
  status_draw={};preparation_ui.render(status_draw,1280,720);
  require(!has_overlay_text(status_draw,"SETTLEMENT IN PROGRESS"),"other-body settlement status leaked into selected body inspector");
  preparation_ui.set_settlement_status(NativeSystemSettlementStatus{91,"Establishing colony",std::nullopt,std::optional<int>{earth_body_id},7.5,30.});
  status_draw={};preparation_ui.render(status_draw,1280,720);
  require(has_overlay_text(status_draw,"SETTLEMENT IN PROGRESS")&&has_overlay_text(status_draw,"7.5 / 30.0 days")&&!has_overlay_text(status_draw,"Prepare a colony vessel"),"matched owned settlement status did not show timed progress or suppress stale readiness guidance");
  const auto status_panel=SystemWorkspaceLayout::for_viewport(1280,720).inspector;
  bool current_guidance{};
  for(int step=0;step<128&&!current_guidance;++step){
    status_draw={};preparation_ui.render(status_draw,1280,720);
    current_guidance=has_overlay_text(status_draw,"Settlement expedition is establishing this body.");
    require(!has_overlay_text(status_draw,"Prepare a colony vessel"),"active expedition retained stale vessel preparation guidance");
    if(!current_guidance)(void)preparation_ui.handle({InputEventType::Wheel,center(status_panel),{},-1},1280,720);
  }
  require(current_guidance,"settlement status did not replace the readiness next step after scrolling to that section");
  (void)preparation_ui.handle({InputEventType::Wheel,center(status_panel),{},1000},1280,720);
  preparation_ui.set_settlement_status(NativeSystemSettlementStatus{91,"Establishing colony",std::nullopt,std::optional<int>{earth_body_id},9.,30.});
  status_draw={};preparation_ui.render(status_draw,1280,720);
  require(has_overlay_text(status_draw,"9.0 / 30.0 days"),"settlement progress refresh did not update the exact selected body");
  preparation_ui.set_settlement_status(std::nullopt);
  status_draw={};preparation_ui.render(status_draw,1280,720);
  require(!has_overlay_text(status_draw,"SETTLEMENT IN PROGRESS")&&has_overlay_text(status_draw,"Prepare a colony vessel"),"clearing settlement status did not restore readiness guidance");
  for(const auto [w,h]:std::array<std::pair<int,int>,2>{{{1280,720},{1920,1080}}}){
    const auto panel=SystemWorkspaceLayout::for_viewport(w,h);
    DrawList scene;preparation_ui.render(scene,w,h);
    const auto action=center(panel.colony_action);
    require(preparation_ui.handle({InputEventType::LeftReleased,action},w,h).kind==SystemWorkspaceCommandKind::none,"release alone opened shipyard");
    require(preparation_ui.handle({InputEventType::LeftPressed,action},w,h).kind==SystemWorkspaceCommandKind::none,"press alone opened shipyard");
    auto outcome=preparation_ui.handle({InputEventType::LeftReleased,action},w,h);
    require(outcome.kind==SystemWorkspaceCommandKind::open_shipyard&&outcome.target_id==earth_body_id,"preparation footer did not navigate on matching release");
    (void)preparation_ui.handle({InputEventType::LeftPressed,action},w,h);
    (void)preparation_ui.handle({InputEventType::PointerCancelled},w,h);
    require(preparation_ui.handle({InputEventType::LeftReleased,action},w,h).kind==SystemWorkspaceCommandKind::none,"cancelled input opened shipyard");
    preparation_ui.set_colony_body(earth_body_id);
    require(preparation_ui.handle({InputEventType::LeftPressed,action},w,h).kind==SystemWorkspaceCommandKind::open_colony,"preparation stole owned colony navigation");
    preparation_ui.set_colony_body(std::nullopt);
  }
  auto wrong_preparation=preparation;++wrong_preparation.player_civilization_id;
  preparation_ui.set_settlement_preparation(wrong_preparation);
  require(!preparation_ui.settlement_preparation(),"foreign preparation accepted");
  wrong_preparation=preparation;++wrong_preparation.campaign_generation;
  preparation_ui.set_settlement_preparation(wrong_preparation);
  require(!preparation_ui.settlement_preparation(),"stale campaign preparation accepted");
  wrong_preparation=preparation;++wrong_preparation.body_id;
  preparation_ui.set_settlement_preparation(wrong_preparation);
  require(!preparation_ui.settlement_preparation(),"different body preparation accepted");
  preparation_ui.set_settlement_preparation(preparation);
  auto partial_preparation=reference;partial_preparation.survey_level=SystemSurveyLevel::partially_surveyed;
  preparation_ui.refresh(partial_preparation);
  require(!preparation_ui.settlement_preparation(),"survey downgrade kept settlement intelligence");
  preparation_ui.set_settlement_preparation(preparation);
  require(!preparation_ui.settlement_preparation(),"partial survey admitted settlement intelligence");
  preparation_ui.close();
  // Pending body artwork keeps mouse navigation alive but is never capture-ready.
  bool deferred_ready{};
  const auto prepared_body=RgbaImage::create(1,1,{120,150,180,255});
  NativeSystemWorkspace deferred([&](const SystemBodyAppearance&){return deferred_ready?prepared_body:nullptr;});
  deferred.open(reference,1280,720);
  DrawList deferred_draw;deferred.render(deferred_draw,1280,720);
  const auto preparing=[](const DrawList&scene){return std::ranges::any_of(scene.overlay,[](const UiOverlayCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value=="Preparing system imagery...";});};
  require(!deferred.artwork_ready()&&preparing(deferred_draw),"pending body artwork was reported complete or lacked progress feedback");
  const auto deferred_layout=SystemWorkspaceLayout::for_viewport(1280,720);
  require(deferred.handle({InputEventType::LeftPressed,center(deferred_layout.reset)},1280,720).captured,"pending artwork blocked mouse navigation");
  deferred_ready=true;deferred_draw={};deferred.render(deferred_draw,1280,720);
  require(deferred.artwork_ready()&&!preparing(deferred_draw),"finished body artwork retained a pending capture or loading indicator");
  deferred.close();require(deferred.artwork_ready(),"closed workspace retained a pending capture");
  const auto display_earth=std::ranges::find(display.bodies,earth_body_id,&NativeSystemBody::id);
  require(display_earth!=display.bodies.end(),"Sol view lacks Earth");
  display_earth->positive_signatures={NativePositiveSignature::rare_resource,NativePositiveSignature::anomaly,NativePositiveSignature::activity};
  const auto projected=project_system(display);
  int image_requests{},texture_requests{};auto body_image=RgbaImage::create(2,2,std::vector<std::uint8_t>(16,255));
  {
    NativeSystemWorkspace zoom_limit;zoom_limit.open(reference,1280,720);const auto field=SystemWorkspaceLayout::for_viewport(1280,720).world_field;
    (void)zoom_limit.handle({InputEventType::Wheel,center(field),{},-100},1280,720);
    require(std::abs(zoom_limit.magnification()-.05f)<1e-6f,"System zoom-out did not stop at 0.05x");
    DrawList far_view;zoom_limit.render(far_view,1280,720);require(has_overlay_text(far_view,"Zoom 0.05x"),"Minimum magnification was not displayed");
    zoom_limit.advance_tumble(1./60,true);const auto normal=zoom_limit.visual_seconds();
    zoom_limit.advance_tumble(1./60,false);require(zoom_limit.visual_seconds()==normal,"Paused planet display rotation advanced");
  }
  int measurements{};NativeSystemWorkspace workspace([&](const SystemBodyAppearance&appearance){++image_requests;if(appearance.texture_key)++texture_requests;return body_image;},[&](const Text&label){++measurements;return TextExtent{static_cast<int>(label.value.size()*7u),14};});
  workspace.open(std::move(display),1280,720);
  require(workspace.visible()&&workspace.system_id()==sol_system_id,"workspace did not open fresh Sol");
  DrawList draw;workspace.render(draw,1280,720);
  require(!draw.world.empty()&&image_requests>0&&texture_requests>0,"workspace did not request eligible Sol appearances");
  require(has_central_stellar_image(draw,*workspace.viewport()),"full-survey Sol did not use a cached native stellar image");
  for(const auto key:{"saturn","uranus"}){
    const auto body=std::ranges::find_if(projected.bodies,[&](const auto& b){return b.sol_texture_key==std::optional<std::string>{key};});
    require(body!=projected.bodies.end(),"Ringed world missing from Sol");const auto p=workspace.viewport()->world_to_screen(body->offset_x,body->offset_y);
    bool found=false;for(std::size_t i=1;i+1<draw.world.size();++i)if(const auto* image=std::get_if<Image>(&draw.world[i]);image&&image->resource==body_image&&std::abs(image->destination.x+image->destination.width*.5f-p.x)<.01f&&std::abs(image->destination.y+image->destination.height*.5f-p.y)<.01f){
      const auto* back=std::get_if<TriangleMesh>(&draw.world[i-1]);const auto* front=std::get_if<TriangleMesh>(&draw.world[i+1]);
      require(back&&front&&!back->indices.empty()&&!front->indices.empty()&&back->indices!=front->indices,"Ring back/body/front ordering or depth split is wrong");found=true;
    }require(found,"Ringed world never submitted its body disc");
  }
  const auto earth=std::ranges::find(projected.bodies,earth_body_id,&SystemSpatialBodyMarker::body_id);
  require(earth!=projected.bodies.end(),"Sol projection lacks Earth");
  auto earth_screen=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
  auto command=workspace.handle({InputEventType::LeftPressed,{earth_screen.x,earth_screen.y}},1280,720);
  require(command.captured&&workspace.selected_body_id()==earth_body_id,"drawn Earth was not selected");
  (void)workspace.handle({InputEventType::LeftReleased,{earth_screen.x,earth_screen.y}},1280,720);
  const auto earth_body=std::ranges::find(reference.bodies,earth_body_id,&NativeSystemBody::id);require(earth_body!=reference.bodies.end(),"Sol snapshot lacks Earth label metadata");
  require(workspace.viewport()->scale>=.01f&&workspace.viewport()->scale<=1.15f,"measured system fit escaped the supported range");
  for(const auto [viewport_width,viewport_height]:std::array<std::pair<int,int>,4>{{{1280,720},{1920,1080},{2560,1440},{3840,2160}}}){
    workspace.reset_fit(viewport_width,viewport_height);const auto responsive_layout=SystemWorkspaceLayout::for_viewport(viewport_width,viewport_height);const auto navigation=NativeUiLayout::for_viewport(viewport_width,viewport_height);const auto responsive_earth=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);require(responsive_layout.world_field.x>=navigation.inspect.x+navigation.inspect.width,"system content overlaps navigation rail");require(responsive_layout.world_field.contains({responsive_earth.x,responsive_earth.y}),"responsive fit placed Earth outside the drawable field");
    command=workspace.handle({InputEventType::LeftPressed,{responsive_earth.x,responsive_earth.y}},viewport_width,viewport_height);require(command.captured&&workspace.selected_body_id()==earth_body_id,"responsive viewport lost exact body selection");(void)workspace.handle({InputEventType::LeftReleased,{responsive_earth.x,responsive_earth.y}},viewport_width,viewport_height);
    draw={};workspace.render(draw,viewport_width,viewport_height);require(has_body_label(draw,earth_body->name),"selected body label was hidden by collision resolution");const auto labels=visible_body_label_bounds(draw,*workspace.snapshot());for(std::size_t left=0;left<labels.size();++left)for(std::size_t right=left+1;right<labels.size();++right)require(!overlaps(labels[left],labels[right]),"visible body labels overlapped");
  }
  workspace.reset_fit(1280,720);earth_screen=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);command=workspace.handle({InputEventType::LeftPressed,{earth_screen.x,earth_screen.y}},1280,720);require(command.captured&&workspace.selected_body_id()==earth_body_id,"720p reset lost Earth selection");(void)workspace.handle({InputEventType::LeftReleased,{earth_screen.x,earth_screen.y}},1280,720);

  draw={};workspace.render(draw,1280,720);const auto layout720=SystemWorkspaceLayout::for_viewport(1280,720);
  int clipped_inspector_text{};
  for(const auto&item:draw.overlay)if(const auto*label=std::get_if<Text>(&item);label&&label->clip&&layout720.inspector.contains(label->at)){++clipped_inspector_text;require(label->at.y<layout720.inspector.y+layout720.inspector.height,"720p inspector content escaped its panel");}
  require(clipped_inspector_text>=12,"full three-signature inspector was not rendered compactly and clipped");
  // Focusing a body is a camera operation only: it remains available without a
  // colony, preserves zoom, and centers the selected marker in the world field.
  const auto focus_scale=workspace.viewport()->scale;
  command=workspace.handle({InputEventType::LeftPressed,center(layout720.focus_action)},1280,720);
  const auto focused_earth=workspace.viewport()->world_to_screen(earth->offset_x,earth->offset_y);
  DrawList focus_without_colony;workspace.render(focus_without_colony,1280,720);
  const auto has_open_colony=std::ranges::any_of(focus_without_colony.overlay,[](const UiOverlayCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value=="OPEN COLONY";});
  require(command.kind==SystemWorkspaceCommandKind::none&&command.captured&&
              !workspace.selected_fleet_id()&&!has_open_colony&&std::abs(workspace.viewport()->scale-focus_scale)<.00001f&&
              std::abs(focused_earth.x-(layout720.world_field.x+layout720.world_field.width*.5f))<.001f&&
              std::abs(focused_earth.y-(layout720.world_field.y+layout720.world_field.height*.5f))<.001f,
          "Focus planet did not center the selected marker without issuing a fleet command");
  const auto inspector_camera=*workspace.viewport();
  const auto scroll_before=workspace.inspection_scroll();
  command=workspace.handle({InputEventType::Wheel,center(layout720.inspector),{},-100},1280,720);
  require(command.kind==SystemWorkspaceCommandKind::none&&command.captured&&
              workspace.inspection_scroll()>scroll_before&&same_camera(inspector_camera,*workspace.viewport()),
          "inspector scrolling moved the camera or failed to reach detailed facts");
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
  const Point drag_start{layout720.world_field.x+20.f,300.f};const Point drag_end{drag_start.x+37.f,323.f};(void)workspace.handle({InputEventType::LeftPressed,drag_start},1280,720);(void)workspace.handle({InputEventType::PointerMove,drag_end,{37,23}},1280,720);(void)workspace.handle({InputEventType::LeftReleased,drag_end},1280,720);
  require(std::abs(workspace.viewport()->center_x-prior_center_x-37)<.01,"workspace drag did not pan");
  draw={};workspace.render(draw,1920,1080);
  require(workspace.viewport()->scale==prior_scale&&std::abs(workspace.viewport()->center_x-prior_center_x-37)>.01,"resize discarded or failed to relocate the existing camera");
  const auto system_layout=SystemWorkspaceLayout::for_viewport(1920,1080);const auto global_layout=NativeUiLayout::for_viewport(1920,1080);
  require(global_layout.hit(center(system_layout.back),false)==UiAction::None&&global_layout.hit(center(system_layout.reset),false)==UiAction::None,"system controls overlap the actual main toolbar routing");
  (void)workspace.handle({InputEventType::LeftPressed,center(system_layout.reset)},1920,1080);
  require(std::abs(workspace.viewport()->center_x-(system_layout.world_field.x+system_layout.world_field.width*.5f))<.001f&&std::abs(workspace.viewport()->center_y-(system_layout.world_field.y+system_layout.world_field.height*.5f))<.001f&&workspace.viewport()->scale>0,"reset fit did not restore the system field camera");
  command=workspace.handle({InputEventType::LeftPressed,center(system_layout.back)},1920,1080);
  require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Back did not route to the system workspace");
  auto downgraded=reference;downgraded.survey_level=SystemSurveyLevel::partially_surveyed;
  const auto downgraded_earth=std::ranges::find(downgraded.bodies,earth_body_id,&NativeSystemBody::id);
  require(downgraded_earth!=downgraded.bodies.end()&&downgraded_earth->details,"Sol downgrade fixture lacks detailed Earth values");
  downgraded_earth->radius_earth=42.;downgraded_earth->orbital_eccentricity=.91;
  downgraded_earth->details->mass_earth=999.;downgraded_earth->details->temperature_kelvin=9999.;
  workspace.refresh(downgraded);draw={};workspace.render(draw,1920,1080);
  const auto overlay_has=[&](std::string_view value){return std::ranges::any_of(draw.overlay,[&](const UiOverlayCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value==value;});};
  require(overlay_has("Unconfirmed")&&!overlay_has("267,582 km")&&!overlay_has("9999 K"),
          "refresh downgrade retained exact body inspection values");
  auto changed_observer=downgraded;++changed_observer.observer_civilization_id;workspace.refresh(std::move(changed_observer));
  require(!workspace.visible()&&!workspace.viewport()&&!workspace.selected_body_id(),
          "observer change did not close the system workspace");
  workspace.discard_campaign();require(!workspace.visible()&&!workspace.selected_body_id()&&!workspace.campaign_generation(),"campaign replacement retained system state or image eligibility");

  auto recon=reference;recon.campaign_generation=2;recon.survey_level=SystemSurveyLevel::partially_surveyed;recon.archetype.reset();recon.primary_stellar_class.reset();
  for(auto&body:recon.bodies){body.details.reset();body.sol_texture_key.reset();body.visual_class=body.kind==PlanetaryBodyKind::Moon?NativeSystemBodyVisualClass::unknown_moon:NativeSystemBodyVisualClass::unknown_planet;}
  image_requests=texture_requests=0;workspace.open(std::move(recon),1280,720);draw={};workspace.render(draw,1280,720);
  require(image_requests>0&&texture_requests==0,"reconnaissance workspace requested hidden texture eligibility");
  require(has_central_stellar_image(draw,*workspace.viewport()),"reconnaissance system fell back to a placeholder instead of the approved stellar appearance");
  command=workspace.handle({InputEventType::EscapePressed},1280,720);require(command.kind==SystemWorkspaceCommandKind::close&&command.captured,"Escape did not request galaxy return");
  workspace.close();require(!workspace.visible(),"workspace close retained the system");
  auto empty_system=reference;empty_system.bodies.clear();workspace.open(std::move(empty_system),1280,720);const auto empty_field=SystemWorkspaceLayout::for_viewport(1280,720).world_field;const auto empty_boundary=std::max(local_orbital_boundary_radius(project_system(*workspace.snapshot()),*workspace.viewport()),star_screen_radius(workspace.viewport()->scale)*1.75f);require(workspace.viewport()->center_x-empty_boundary>=empty_field.x&&workspace.viewport()->center_x+empty_boundary<=empty_field.x+empty_field.width&&workspace.viewport()->center_y-empty_boundary>=empty_field.y&&workspace.viewport()->center_y+empty_boundary<=empty_field.y+empty_field.height,"empty system fit cropped the stellar or orbital envelope");workspace.close();
  auto eccentric_system=reference;const auto eccentric_body=std::ranges::find_if(eccentric_system.bodies,[](const auto&body){return !body.parent_body_id;});require(eccentric_body!=eccentric_system.bodies.end(),"Sol fixture lacks a primary orbit for eccentric fit coverage");eccentric_body->orbital_eccentricity=.82;workspace.open(std::move(eccentric_system),1280,720);const auto eccentric_field=SystemWorkspaceLayout::for_viewport(1280,720).world_field;const auto eccentric_boundary=std::max(local_orbital_boundary_radius(project_system(*workspace.snapshot()),*workspace.viewport()),star_screen_radius(workspace.viewport()->scale)*1.75f);require(workspace.viewport()->center_x-eccentric_boundary>=eccentric_field.x&&workspace.viewport()->center_x+eccentric_boundary<=eccentric_field.x+eccentric_field.width&&workspace.viewport()->center_y-eccentric_boundary>=eccentric_field.y&&workspace.viewport()->center_y+eccentric_boundary<=eccentric_field.y+eccentric_field.height,"eccentric system fit cropped its orbital envelope");workspace.close();

  auto&simulation=campaign.runtime().world();auto&world=simulation.campaign();const auto player=world.player_civilization_id;std::map<int,std::vector<int>> connected;for(const auto&lane:simulation.lanes().build()){connected[lane.first_system_id].push_back(lane.second_system_id);connected[lane.second_system_id].push_back(lane.first_system_id);}int local_system=-1;std::vector<int> destinations;for(const auto&[candidate,neighbors]:connected)if(neighbors.size()>destinations.size()&&world.knowledge.system_survey_level(player,candidate)<SystemSurveyLevel::partially_surveyed){local_system=candidate;destinations=neighbors;}require(local_system>=0&&destinations.size()>=2,"actual lane graph lacks a useful observer-hidden local system fixture");world.knowledge.record_reconnaissance(player,local_system,.35);world.knowledge.record_reconnaissance(player,destinations.front(),.35);const auto known_destination=destinations.front();const auto unknown_found=std::ranges::find_if(destinations,[&](int id){return id!=known_destination&&world.knowledge.system_survey_level(player,id)<SystemSurveyLevel::partially_surveyed;});require(unknown_found!=destinations.end(),"actual lane graph lacks an unknown connected destination");const auto unknown_destination=*unknown_found;
  FleetState local_fleet;local_fleet.id=99001;local_fleet.civilization_id=player;local_fleet.name="ISS Green Horizon";local_fleet.role=FleetRole::Scout;local_fleet.design_id="scout_probe";local_fleet.current_system_id=local_system;local_fleet.local_transit_position={.10f,.08f};local_fleet.local_transit_target={.82f,.18f};local_fleet.transit_phase=FleetTransitPhase::LocalDeparture;local_fleet.mission_order_revision=7;local_fleet.is_active=true;world.fleets.push_back(local_fleet);auto held_fleet=local_fleet;held_fleet.id=99002;held_fleet.name="ISS Patient Horizon";held_fleet.hold_requested=true;world.fleets.push_back(held_fleet);const auto original_revision=local_fleet.mission_order_revision;const auto original_route=local_fleet.planned_route_system_ids;
  auto local_view=controller.build(campaign,3,local_system);require(local_view.snapshot.has_value(),"observer-safe local system view was denied");NativeSystemTravelController travel_controller;auto travel=travel_controller.build(campaign,3,*local_view.snapshot);require(travel.snapshot.has_value(),"actual local fleet/lane projection was denied");const auto known_lane=std::ranges::find(travel.snapshot->lanes,known_destination,&NativeLocalLaneMarker::destination_system_id),unknown_lane=std::ranges::find(travel.snapshot->lanes,unknown_destination,&NativeLocalLaneMarker::destination_system_id);require(known_lane!=travel.snapshot->lanes.end()&&known_lane->known_label&&known_lane->known_length_light_years,"known connected lane omitted observer-safe label or length");require(unknown_lane!=travel.snapshot->lanes.end()&&!unknown_lane->known_label&&!unknown_lane->known_length_light_years,"unknown connected lane leaked a label or length");
  workspace.open(std::move(*local_view.snapshot),1280,720);const auto measurements_before_refresh=measurements;workspace.refresh_travel(std::move(*travel.snapshot),std::nullopt);require(measurements-measurements_before_refresh==static_cast<int>(workspace.travel_snapshot()->lanes.size()+workspace.snapshot()->bodies.size()),"initial travel fit did not measure each lane and body label exactly once");const auto local_spatial=project_system(*workspace.snapshot());const auto initial_layout=SystemWorkspaceLayout::for_viewport(1280,720);const auto initial_geometry=workspace.lane_geometry();require(!initial_geometry.empty(),"initial travel refresh did not lay out real lanes");for(const auto&lane:initial_geometry)require(local_lane_visible(lane,initial_layout.world_field)&&initial_layout.world_field.contains(lane.center),"initial travel refresh did not fit a real lane inside the field");const auto initial_boundary=local_orbital_boundary_radius(local_spatial,*workspace.viewport());for(const auto&lane:initial_geometry)require(std::hypot(lane.center.x-workspace.viewport()->center_x,lane.center.y-workspace.viewport()->center_y)>initial_boundary,"initial travel arrow was not outside the orbital boundary");const auto fleet=std::ranges::find(workspace.travel_snapshot()->fleets,99001,&NativeLocalFleetMarker::fleet_id);require(fleet!=workspace.travel_snapshot()->fleets.end(),"owned local fleet was not projected");
  const Point pan_origin{initial_layout.world_field.x+16,initial_layout.world_field.y+16};(void)workspace.handle({InputEventType::LeftPressed,pan_origin},1280,720);(void)workspace.handle({InputEventType::PointerMove,{pan_origin.x+19,pan_origin.y+13},{19,13}},1280,720);(void)workspace.handle({InputEventType::LeftReleased,{pan_origin.x+19,pan_origin.y+13}},1280,720);const auto panned_camera=*workspace.viewport();const auto refreshed_travel=travel_controller.build(campaign,3,*workspace.snapshot());require(refreshed_travel.snapshot.has_value(),"second local travel snapshot was unavailable");workspace.refresh_travel(*refreshed_travel.snapshot,std::nullopt);require(same_camera(panned_camera,*workspace.viewport()),"second travel refresh reset a panned camera");const auto refreshed_fleet=std::ranges::find(workspace.travel_snapshot()->fleets,99001,&NativeLocalFleetMarker::fleet_id);require(refreshed_fleet!=workspace.travel_snapshot()->fleets.end(),"second travel refresh lost owned fleet");const auto refreshed_fleet_point=local_fleet_anchor(*refreshed_fleet,local_spatial,*workspace.viewport());
  command=workspace.handle({InputEventType::LeftPressed,refreshed_fleet_point},1280,720);require(command.kind==SystemWorkspaceCommandKind::select_fleet&&command.target_id==99001&&command.captured&&command.hit_fleet_ids==std::vector<int>({99001,99002}),"owned co-located fleet click did not preserve the sorted hit group");NativeFleetController fleet_controller;(void)fleet_controller.build(campaign,3);const auto selected=fleet_controller.select_next_hit(campaign,3,command.hit_fleet_ids);require(selected.accepted&&fleet_controller.selection()==std::optional<int>{99001},"first local fleet click was not accepted by the existing fleet controller");const auto cycled=fleet_controller.select_next_hit(campaign,3,command.hit_fleet_ids);require(cycled.accepted&&fleet_controller.selection()==std::optional<int>{99002},"co-located local fleet click did not cycle selection");for(const auto id:{99001,99002}){const auto current=std::ranges::find(world.fleets,id,&FleetState::id);require(current!=world.fleets.end()&&current->mission_order_revision==original_revision&&current->planned_route_system_ids==original_route,"local fleet selection mutated a canonical order");}
  draw={};workspace.render(draw,1280,720);require(std::ranges::any_of(draw.world,[](const WorldCommand&item){const auto*label=std::get_if<Text>(&item);return label&&label->value=="SC";}),"owned scout role icon was not visible");const auto thrusters=std::ranges::count_if(draw.world,[](const WorldCommand&item){const auto*line=std::get_if<Line>(&item);return line&&line->color.r==92&&line->color.g==225&&line->color.b==154&&line->color.a==170;});require(thrusters==2,"moving fleet thrusters were missing or rendered for a held fleet");require(filled_lane_count(draw,SystemWorkspaceLayout::for_viewport(1280,720).world_field)==workspace.lane_geometry().size(),"connected local lanes were not submitted as solid clipped arrows");
  const auto layout=SystemWorkspaceLayout::for_viewport(1280,720);const auto geometry=workspace.lane_geometry();const auto known_geometry=std::ranges::find(geometry,known_destination,&NativeLocalLaneGeometry::destination_system_id),unknown_geometry=std::ranges::find(geometry,unknown_destination,&NativeLocalLaneGeometry::destination_system_id);require(known_geometry!=geometry.end()&&unknown_geometry!=geometry.end(),"connected lanes lacked stable arrow geometry");require(layout.world_field.contains(known_geometry->center)&&layout.world_field.contains(unknown_geometry->center),"actual lane fixture arrows were not reachable at 720p");const auto protected_command=workspace.handle({InputEventType::LeftPressed,center(layout.inspector)},1280,720);require(protected_command.kind==SystemWorkspaceCommandKind::none&&protected_command.captured,"inspector click reached local travel geometry");
  (void)workspace.handle({InputEventType::PointerMove,known_geometry->center},1280,720);
  workspace.refresh_travel(*refreshed_travel.snapshot,std::nullopt);
  draw={};workspace.render(draw,1280,720);
  require(std::ranges::any_of(draw.overlay,[](const UiOverlayCommand& item){
    const auto* mesh=std::get_if<TriangleMesh>(&item);
    return mesh&&mesh->vertices.size()==3&&mesh->color.r==245&&mesh->color.g==157&&mesh->color.b==70;
  }),"Lane hover orange disappeared during fleet refresh");
  (void)workspace.handle({InputEventType::LeftPressed,known_geometry->center},1280,720);
  draw={};workspace.render(draw,1280,720);
  require(std::ranges::any_of(draw.overlay,[](const UiOverlayCommand& item){
    const auto* mesh=std::get_if<TriangleMesh>(&item);
    return mesh&&mesh->vertices.size()==3&&mesh->color.r==201&&mesh->color.g==113;
  }),"Lane mouse-down did not show pressed feedback");
  (void)workspace.handle({InputEventType::PointerCancelled},1280,720);
  command=workspace.handle({InputEventType::LeftReleased,known_geometry->center},1280,720);
  require(command.kind==SystemWorkspaceCommandKind::none,"Cancelled arrow press navigated on release");
  command=workspace.handle({InputEventType::LeftPressed,unknown_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::none,"Lane activated before mouse release");command=workspace.handle({InputEventType::LeftReleased,unknown_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::reconnaissance_required&&command.target_id==-1&&command.captured&&workspace.notice().find("Telemetry unavailable")!=std::string::npos,"unknown lane click did not give observer-safe scout guidance");require(workspace.notice().find(std::to_string(unknown_destination))==std::string::npos,"unknown lane notice disclosed its opaque destination identity");command=workspace.handle({InputEventType::LeftPressed,known_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::none,"Lane activated before mouse release");command=workspace.handle({InputEventType::LeftReleased,known_geometry->center},1280,720);require(command.kind==SystemWorkspaceCommandKind::open_destination&&command.target_id==known_destination&&command.captured,"known lane click did not request observer-gated navigation");const auto destination_view=controller.build(campaign,3,command.target_id);require(destination_view.snapshot.has_value(),"known lane navigation failed the observer gate");for(const auto id:{99001,99002}){const auto current=std::ranges::find(world.fleets,id,&FleetState::id);require(current!=world.fleets.end()&&current->mission_order_revision==original_revision&&current->planned_route_system_ids==original_route,"lane navigation issued or changed a fleet order");}
  for(const auto [viewport_width,viewport_height]:std::array<std::pair<int,int>,4>{{{1280,720},{1920,1080},{2560,1440},{3840,2160}}}){workspace.reset_fit(viewport_width,viewport_height);const auto responsive_layout=SystemWorkspaceLayout::for_viewport(viewport_width,viewport_height);const auto responsive_geometry=workspace.lane_geometry();const auto boundary=local_orbital_boundary_radius(local_spatial,*workspace.viewport());require(!responsive_geometry.empty(),"responsive reset fit discarded local lanes");for(const auto&lane:responsive_geometry)require(local_lane_visible(lane,responsive_layout.world_field)&&responsive_layout.world_field.contains(lane.center)&&std::hypot(lane.center.x-workspace.viewport()->center_x,lane.center.y-workspace.viewport()->center_y)>boundary,"responsive fit did not keep a local travel arrow and label in the world field");const auto responsive_known=std::ranges::find(responsive_geometry,known_destination,&NativeLocalLaneGeometry::destination_system_id);require(responsive_known!=responsive_geometry.end(),"known lane disappeared after responsive fit");command=workspace.handle({InputEventType::LeftPressed,responsive_known->center},viewport_width,viewport_height);require(command.kind==SystemWorkspaceCommandKind::none,"Lane activated before mouse release");command=workspace.handle({InputEventType::LeftReleased,responsive_known->center},viewport_width,viewport_height);require(command.kind==SystemWorkspaceCommandKind::open_destination&&command.target_id==known_destination&&command.captured,"responsive known lane lost observer-gated navigation");}
  draw={};workspace.render(draw,1920,1080);require(!workspace.lane_geometry().empty(),"1080p resize discarded local lanes");workspace.discard_campaign();require(!workspace.travel_snapshot()&&!workspace.selected_fleet_id()&&workspace.notice().empty(),"generation discard retained local travel presentation");
  auto triple=reference;triple.secondary_stellar_class=StellarClass::MRedDwarf;triple.tertiary_stellar_class=StellarClass::KOrangeDwarf;
  triple.stellar_object=generate_stellar_physics(17,StellarObjectType::GYellowStar);
  StellarOrbitArchitecture architecture;
  architecture.companions={generate_stellar_physics(18,StellarObjectType::MRedDwarf),generate_stellar_physics(19,StellarObjectType::KOrangeStar)};
  const double inner_mass=triple.stellar_object->mass_solar+architecture.companions[0].mass_solar;
  architecture.relative_orbits={{100,.1,.02,0,.3,1,kepler_rate(100,inner_mass)},{3000,.1,.06,0,.15,.7,kepler_rate(3000,inner_mass+architecture.companions[1].mass_solar)}};
  triple.stellar_orbits=architecture;workspace.open(triple,1280,720);
  for(const auto days:{0.,10000.,100000.}){
    workspace.set_simulation_days(days);workspace.reset_fit(1280,720);draw={};workspace.render(draw,1280,720);
    const auto chart=project_system(*workspace.snapshot());const auto field=SystemWorkspaceLayout::for_viewport(1280,720).world_field;
    for(int component=0;component<3;++component){const auto p=workspace.viewport()->world_to_screen(chart.stellar_hosts[component].x,chart.stellar_hosts[component].y);
      require(field.contains({p.x,p.y}),"Fit System cropped a moving stellar component");
      require(std::ranges::any_of(draw.world,[&](const auto& command){const auto* text=std::get_if<Text>(&command);return text&&text->value.find(stellar_host_name(component))!=std::string::npos&&text->align==TextAlign::Center&&text->at.y>p.y;}),"Triple omitted its below-object component label");
    }
  }
  const auto fitted_scale=workspace.viewport()->scale;const auto field=SystemWorkspaceLayout::for_viewport(1280,720).world_field;
  (void)workspace.handle({.type=InputEventType::Wheel,.position=center(field),.wheel_y=.1f},1280,720);
  require(workspace.viewport()->scale>fitted_scale&&workspace.viewport()->scale<fitted_scale*1.1f,"Wide-system zoom jumped to the old minimum scale");
  std::cout<<"native system workspace cases passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"native system workspace failed: "<<e.what()<<'\n';return 1;}
