#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/exploration_advance.hpp>
#include "native_phenomena.hpp"
#include "native_phenomena_debug.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace stellar::core;
using namespace stellar::native_phenomena;
using namespace stellar::native_map;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Invalid saved region accepted");}
int main(int argc,char** argv)try{
  check(argc==2,"Catalog required");const auto catalog=load_nearby_catalog(argv[1]);
  const auto& art=phenomenon_art_catalog();check(art.size()==48,"Not all supplied files mapped");
  std::map<PhenomenonType,int> families;std::set<std::string> filenames;
  for(const auto& a:art){++families[a.functional_type];check(filenames.insert(a.filename).second,"Duplicate filename mapping");check(std::filesystem::exists(a.path),"Missing supplied source file");
    if(a.functional_type==PhenomenonType::SupernovaRemnant||a.functional_type==PhenomenonType::RareEnergetic)check(!a.system_view_eligible,"Map-only type became a system background");
    const auto source=decode_rgba_image(a.path);const auto image=prepare_decal_texture(*source,256,a.obscuring?DecalBlendProfile::Obscuring:DecalBlendProfile::Luminous);
    check(image->width()==256&&std::abs(image->width()/static_cast<double>(image->height())-a.aspect_ratio)<.02,"LOD stretched supplied artwork");
    for(int x=0;x<image->width();++x)check(image->pixels()[x*4+3]==0&&image->pixels()[((image->height()-1)*image->width()+x)*4+3]==0,"Visible horizontal rectangular edge");
    for(int y=0;y<image->height();++y)check(image->pixels()[y*image->width()*4+3]==0&&image->pixels()[(y*image->width()+image->width()-1)*4+3]==0,"Visible vertical rectangular edge");
  }
  check(families[PhenomenonType::DiffuseGas]==12&&families[PhenomenonType::MixedNebula]==8&&families.size()==9,"Filename family classification failed");
  std::vector<std::uint8_t> black(32*32*4);for(std::size_t i=3;i<black.size();i+=4)black[i]=255;const auto black_source=RgbaImage::create(32,32,black);
  const auto dark_mask=prepare_decal_texture(*black_source,32,DecalBlendProfile::Obscuring),light_mask=prepare_decal_texture(*black_source,32,DecalBlendProfile::Luminous);const auto middle=(16*32+16)*4;
  check(dark_mask->pixels()[middle]==0&&dark_mask->pixels()[middle+3]>200&&light_mask->pixels()[middle+3]==0,"Dark dust was erased by black transparency");
  const auto mature_weights=phenomenon_weights(GalaxyMorphology::Spiral,PopulationState::Mature);check(std::abs(mature_weights[12].normalized-.01)<1e-9&&std::abs(mature_weights[5].normalized-.02)<1e-9&&std::abs(mature_weights[10].normalized-.26)<1e-9,"Configured rarity weights changed");
  check(phenomenon_weights(GalaxyMorphology::Elliptical,PopulationState::Quiescent)[4].normalized<mature_weights[4].normalized*.02,"Old elliptical retained bright star formation");
  double previous=0;for(int size:{250,2500,10000,30000,75000,150000}){double mean=0;for(int seed=0;seed<1000;++seed)mean+=natural_phenomenon_count(size,GalaxyMorphology::Spiral,PopulationState::Mature,seed);check(mean>previous,"Larger galaxies do not generally have more major phenomena");previous=mean;}
  std::array<double,3> totals{};for(int seed=0;seed<2000;++seed)for(int i=0;i<3;++i)totals[i]+=natural_phenomenon_count(10000,GalaxyMorphology::Spiral,std::array{PopulationState::Starburst,PopulationState::Mature,PopulationState::Quiescent}[i],seed);check(totals[0]>totals[1]&&totals[1]>totals[2]*3,"Population count ordering failed");
  GalaxyGenerationConfig qa;qa.base_seed=312;qa.system_count=250;qa.developer_full_coverage=true;qa=resolve_galaxy_configuration(qa);
  const auto qa_world=seed_persistable_fresh_campaign(qa.base_seed,catalog,{"2050-03-21T00:00:00Z",250,6,1,"terran_baseline",StellarPopulationOptions{qa.morphology,qa.resolved_population},true,qa});
  std::set<PhenomenonType> qa_types;for(const auto& r:qa_world.generation_metadata->phenomena->regions)qa_types.insert(r.type);check(qa_types.size()==9,"Full Content Coverage omitted a functional phenomenon family");
  std::set<PhenomenonType> types;std::size_t burst=0,quiet=0,spiral=0,elliptical=0;int empty=0;
  GalaxyPhenomena largest;GalaxyGenerationConfig largest_config;std::vector<StellarSystem> largest_systems;
  for(int morph=0;morph<6;++morph)for(int pop:{1,3,5})for(int seed=0;seed<3;++seed){
    GalaxyGenerationConfig c;c.base_seed=91234+seed;c.system_count=250;c.morphology=static_cast<GalaxyMorphology>(morph);c.requested_population=static_cast<PopulationSelection>(pop);c=resolve_galaxy_configuration(c);
    auto world=seed_persistable_fresh_campaign(c.base_seed,catalog,{"2050-03-21T00:00:00Z",250,6,1,"terran_baseline",StellarPopulationOptions{c.morphology,c.resolved_population},false,c});
    const auto& field=*world.generation_metadata->phenomena;validate_galaxy_phenomena(field,c,world.systems);
    check(field==generate_galaxy_phenomena(c,world.systems),"Region generation rerolled");
    for(const auto& r:field.regions)types.insert(r.type);
    if(pop==1)burst+=field.regions.size();if(pop==5)quiet+=field.regions.size();if(morph==0)spiral+=field.regions.size();if(morph==3)elliptical+=field.regions.size();if(field.regions.empty())++empty;
    if(field.regions.size()>largest.regions.size()){largest=field;largest_config=c;largest_systems=world.systems;}
    const GalaxyPayloadCaptureOptions capture{0,"test","2050-03-21T00:00:00Z"};const auto encoded=encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(world,capture));
    if(!field.regions.empty()&&morph==0&&pop==1&&seed==0){
      auto malformed=nlohmann::json::parse(encoded);
      malformed["Galaxy"]["GenerationMetadata"]["Phenomena"]["regions"][0]["type"]=4294967296ULL;
      rejects([&]{(void)decode_galaxy_payload_v16_json(malformed.dump());});
      malformed=nlohmann::json::parse(encoded);
      malformed["Galaxy"]["GenerationMetadata"]["Phenomena"]["regions"][0]["shape"]["shape"]=4294967296ULL;
      rejects([&]{(void)decode_galaxy_payload_v16_json(malformed.dump());});
    }
    const auto loaded=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(encoded)).galaxy;
    check(loaded.generation_metadata->phenomena==world.generation_metadata->phenomena,"Save/load changed geometry, intensity, effects or membership");
  }
  check(burst>quiet*4&&spiral>elliptical*3,"Population/morphology tendencies failed");check(empty>0,"Quiescent zero-region case missing");check(types.size()>=6,"Procedural type variety missing");
  auto forged=largest;forged.regions[0].shape.x=1e6;rejects([&]{validate_galaxy_phenomena(forged,largest_config,largest_systems);});forged=largest;forged.regions[0].systems_contained.push_back(-1);rejects([&]{validate_galaxy_phenomena(forged,largest_config,largest_systems);});
  // v2 is deliberately not regenerated or backfilled during loading.
  GalaxyGenerationConfig old;old.generator_version="galaxy-configuration-v2";old.system_count=250;old=resolve_galaxy_configuration(old);
  auto legacy=seed_persistable_fresh_campaign(old.base_seed,catalog,{"2050-03-21T00:00:00Z",250,6,1,"terran_baseline",StellarPopulationOptions{old.morphology,old.resolved_population},false,old});
  check(!legacy.generation_metadata->phenomena,"Legacy seed silently changed");
  const auto old_loaded=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(encode_galaxy_payload_v16_json(capture_galaxy_payload_v16(legacy,{0,"test","2050-03-21T00:00:00Z"})))).galaxy;
  check(old_loaded.generation_metadata->configuration==legacy.generation_metadata->configuration&&!old_loaded.generation_metadata->phenomena,"Legacy save changed on load");
  GalaxyPhenomena fixture;GalaxyPhenomenon r;r.id=1;r.type=PhenomenonType::Reflection;r.color={80,160,250};r.shape={0,0,20,20,0,.15,987,stellar::engine::OrganicShape::Cloud};r.intensity=.9;r.opacity=.5;r.effects.sensor=.65;r.effects.scanning=1.35;fixture.regions.push_back(r);
  const auto center=phenomenon_context(&fixture,0,0,7),edge=phenomenon_context(&fixture,17,0,8),outside=phenomenon_context(&fixture,40,0,9);
  check(center.dominant==1u&&center.visual_intensity>edge.visual_intensity&&outside.overlaps.empty(),"Center/edge/outside membership failed");
  check(center.effects.sensor<1&&center.effects.scanning>1&&outside.effects.sensor==1,"Gameplay context does not match overlap");
  const auto nearest=nearest_phenomenon(&fixture,40,0);check(nearest&&nearest->id==1u&&nearest->edge_distance<0&&nearest->center_distance==40,"Outside nearest-edge query failed");
  auto shell=r.shape;shell.shape=stellar::engine::OrganicShape::Shell;
  const auto hollow=stellar::engine::sample_region(shell,0,0,false),rim=stellar::engine::sample_region(shell,11,0,false);
  check(hollow.density==0&&hollow.edge_distance<0&&rim.density>0&&rim.edge_distance>0,"Remnant inner edge or hollow membership is incorrect");
  // Actual survey advancement, rather than only checking the modifier value.
  std::vector<StellarSystem> survey_systems(1);survey_systems[0].id=7;survey_systems[0].name="Survey test";
  std::vector<Civilization> civilizations(1);civilizations[0].id=0;civilizations[0].is_player=true;
  std::vector<FleetState> clear_fleets(1);clear_fleets[0].id=1;clear_fleets[0].civilization_id=0;clear_fleets[0].role=FleetRole::Scout;clear_fleets[0].current_system_id=7;
  auto cloud_fleets=clear_fleets;CivilizationKnowledgeState clear_knowledge,cloud_knowledge;InterstellarLaneNetwork lanes(&survey_systems);ExplorationSimulation survey;
  (void)survey.advance({survey_systems,{},civilizations,clear_fleets,{},{},clear_knowledge,lanes,{},nullptr},.5);
  (void)survey.advance({survey_systems,{},civilizations,cloud_fleets,{},{},cloud_knowledge,lanes,{},&fixture},.5);
  check(cloud_fleets[0].reconnaissance_days_completed<clear_fleets[0].reconnaissance_days_completed,"Survey simulation ignored phenomenon interference");
  auto same=make_local_environment(fixture,center);check(same->pixels()==make_local_environment(fixture,phenomenon_context(&fixture,0,0,7))->pixels(),"Local clouds rerolled on entry");
  const auto alpha_sum=[](const RgbaImage& image){std::uint64_t sum=0;for(std::size_t i=3;i<image.pixels().size();i+=4)sum+=image.pixels()[i];return sum;};
  auto weaker=edge;weaker.local_seed=center.local_seed;
  check(alpha_sum(*make_local_environment(fixture,weaker))<alpha_sum(*same),"Edge system does not render weaker inherited clouds");
  check(alpha_sum(*make_local_environment(fixture,outside))==0,"Clear-space system inherited a cloud");
  std::size_t blue=0,colored=0;for(std::size_t i=0;i<same->pixels().size();i+=4)if(same->pixels()[i+3]>0){++colored;if(same->pixels()[i+2]>same->pixels()[i])++blue;}check(blue>colored*.9,"Reflection artwork lost its original blue color");
  auto new_seed=fixture;new_seed.regions[0].shape.seed++;const auto changed=make_local_environment(new_seed,phenomenon_context(&new_seed,0,0,7));check(same->pixels()!=changed->pixels(),"Local clouds ignored changed seed");
  for(int i=2;i<20;++i){auto copy=r;copy.id=i;fixture.regions.push_back(copy);}const auto overlap=phenomenon_context(&fixture,0,0,7);
  check(overlap.overlaps.size()==19&&overlap.visual_intensity<=.42&&overlap.effects.sensor>=.45&&overlap.effects.scanning<=1.8,"Overlap cap failed");
  const auto layered=make_local_environment(fixture,overlap);for(std::size_t i=3;i<layered->pixels().size();i+=4)check(layered->pixels()[i]<=72,"Opaque local clouds violate readability budget");
  auto atlas=make_phenomenon_atlas(fixture);const auto layout=phenomenon_atlas_layout(fixture.regions.size());for(std::size_t i=0;i<fixture.regions.size();++i)for(int x=0;x<layout.tile;++x){const auto at=(i/layout.columns*layout.tile*atlas->width()+i%layout.columns*layout.tile+x)*4+3;check(atlas->pixels()[at]==0,"Atlas tile has a rectangular frame");}
  check(same->width()==1536&&same->height()==864,"Local clouds lost close-view detail");
  for(std::size_t count=1;count<=160;++count){const auto a=phenomenon_atlas_layout(count);check(a.columns*a.rows*a.tile*a.tile*8<=24*1024*1024,"Atlas exceeds the shared preparation budget");}
  NativePhenomena native;native.bind(&fixture);DrawList map;native.append_map(map,{{0,0},10},1280,720,{},{});check(!map.world.empty(),"Map phenomena not rendered");
  DrawList zoomed_map;native.append_map(zoomed_map,{{0,0},40},1280,720,{},{});
  const auto& near_gas=std::get<TriangleMesh>(zoomed_map.world.front());
  const auto& far_gas=std::get<TriangleMesh>(map.world.front());
  check(near_gas.texture->width()>=far_gas.texture->width(),"Close zoom selected a lower detail asset");
  const auto mapping=phenomenon_art_mapping(fixture.regions[0],phenomenon_art(fixture,fixture.regions[0]));
  for(int zoom=0;zoom<2;++zoom){const auto& mesh=zoom?near_gas:far_gas;const Camera camera{{0,0},zoom?40.:10.};
    check(!mesh.vertices.empty()&&mesh.texture_coordinates.size()==mesh.vertices.size(),"Cloud mask has no textured geometry");
    for(std::size_t i=0;i<mesh.vertices.size();++i){const auto world=camera.unproject(mesh.vertices[i],1280,720),unused=world;(void)unused;const auto expected=mapping.uv(world.x,world.y),uv=mesh.texture_coordinates[i];check(std::abs(std::clamp(expected.x,0.f,1.f)-uv.x)<1e-5&&std::abs(std::clamp(expected.y,0.f,1.f)-uv.y)<1e-5,"Artwork slipped from its world coordinates while zooming");}
  }
  DrawList overview;native.append_map(overview,{{0,0},.15},1280,720,{},{});check(!overview.world.empty(),"Major phenomena disappeared at full zoom out");
  VisualOptions debug;debug.labels=true;debug.bounds=true;debug.filenames=true;DrawList inspected;native.append_map(inspected,{{0,0},10},1280,720,debug,{});
  check(std::holds_alternative<TriangleMesh>(inspected.world.front())&&std::holds_alternative<Text>(inspected.world.back()),"Developer overlays hidden beneath artwork");
  const auto& label=std::get<Text>(inspected.world.back());check(label.value.find("Reflection Nebula")!=std::string::npos&&label.value.find(".png")!=std::string::npos,"Developer type/filename toggle concealed unknown artwork");
  const auto texts=[&](const DrawList&d){std::vector<std::string> v;for(const auto&c:d.world)if(const auto*t=std::get_if<Text>(&c))v.push_back(t->value);return v;};
  check(texts(map).empty(),"Unsurveyed phenomenon disclosed a map label");
  auto named_fixture=fixture;named_fixture.regions[0].designation="RC-1";NativePhenomena charted;charted.bind(&named_fixture);
  DrawList charted_map;charted.append_map(charted_map,{{0,0},10},1280,720,{},{named_fixture.regions[0].id});
  const auto player_labels=texts(charted_map);
  check(player_labels.size()==1&&player_labels[0].find("RC-1")!=std::string::npos&&player_labels[0].find("Reflection Nebula")!=std::string::npos,"Surveyed phenomenon lost its designation label");
  DrawList charted_overview;charted.append_map(charted_overview,{{0,0},.15},1280,720,{},{named_fixture.regions[0].id});
  check(texts(charted_overview).empty(),"Phenomenon label cluttered the overview zoom");
  for(const auto size:{std::pair{1280,720},std::pair{1920,1080}}){PhenomenaDebug panel;const float s=size.first/1280.f,px=(size.first-940*s)*.5f,py=(size.second-650*s)*.5f;
    const auto click=[&](float x,float y){InputEvent event{};event.type=InputEventType::LeftPressed;event.position={px+x*s,py+y*s};check(panel.handle(event,size.first,size.second),"Developer control did not capture input");};
    panel.toggle();click(490,128);check(panel.options.membership,"System overlap toggle failed");click(800,128);check(panel.options.filenames,"Artwork filename toggle failed");
    click(460,72);check(!panel.visible&&panel.take_navigation(3)==2,"Previous phenomenon did not wrap");check(!panel.take_navigation(3),"Navigation repeated without input");
    panel.toggle();click(620,72);check(panel.take_navigation(3)==0,"Next phenomenon did not wrap");panel.toggle();click(780,72);check(panel.take_navigation(3)==0,"Go To changed selected phenomenon");
    // Keyboard ring: close, density dropdown, region navigation and the six
    // option toggles walk in (y,x) order; toggles announce as CheckBox and
    // activation replays the same dispatch as a pointer press.
    {
      panel.toggle();
      const auto press=[&](std::uint32_t key,bool shift=false){InputEvent ev{};ev.type=InputEventType::KeyPressed;ev.key=key;ev.shift=shift;return panel.handle(ev,size.first,size.second);};
      check(panel.focus()<0,"Phenomena debug retained keyboard focus.");
      check(press(9)&&panel.focus()>=0,"Tab did not enter the phenomena ring.");
      check(panel.focused_label(size.first,size.second)=="Close phenomena debug","First phenomena target is not the close control.");
      check(panel.focused_bounds(size.first,size.second).has_value(),"Focused phenomena control lacks bounds.");
      int ring_guard=0;
      while(panel.focused_control(size.first,size.second)!=stellar::engine::AnnouncementControl::CheckBox&&ring_guard++<32)
        check(press(9),"Phenomena ring navigation leaked.");
      check(panel.focused_control(size.first,size.second)==stellar::engine::AnnouncementControl::CheckBox,"Option toggle was not classified as a CheckBox.");
      const bool bounds_before=panel.options.bounds;
      check(press(13),"Phenomena toggle activation leaked.");
      check(panel.options.bounds!=bounds_before,"Keyboard activation did not flip the toggle.");
      check(press(9)&&panel.focus()>=0,"Phenomena ring did not stay live after activation.");
      InputEvent pointer_press{};pointer_press.type=InputEventType::LeftPressed;pointer_press.position={4,4};
      check(panel.handle(pointer_press,size.first,size.second),"Phenomena debug dropped pointer input.");
      check(panel.focus()<0,"Pointer press did not clear the phenomena ring.");
      check(press(9)&&panel.focus()>=0,"Phenomena ring did not re-enter.");
      check(panel.handle({InputEventType::EscapePressed},size.first,size.second)&&panel.focus()<0&&panel.visible,"Escape closed the panel instead of releasing its ring.");
      check(panel.handle({InputEventType::EscapePressed},size.first,size.second)&&!panel.visible,"Second Escape did not close the phenomena panel.");
    }
    // Scroll contract: the engine VirtualizedList clamps the dump window —
    // the old unbounded int offset could scroll past the end into blank space.
    {
      PhenomenaDebug dump_panel;dump_panel.visible=true;
      std::string dump;for(int i=0;i<40;++i)dump+="phenomena line "+std::to_string(i)+"\n";
      dump_panel.data(std::move(dump));
      const auto texts=[](const DrawList &d){std::vector<std::string> v;for(const auto &c:d.overlay)if(const auto *t=std::get_if<Text>(&c);t&&t->clip)v.push_back(t->value);std::ranges::sort(v);return v;};
      const auto render=[&]{DrawList d;dump_panel.render(d,size.first,size.second);return d;};
      const auto top=texts(render());
      InputEvent wheel{InputEventType::Wheel,{},{},-3.f};
      check(dump_panel.handle(wheel,size.first,size.second),"Phenomena debug dropped wheel input.");
      check(texts(render())!=top,"Phenomena debug wheel did not scroll the dump.");
      InputEvent up{InputEventType::Wheel,{},{},3.f};
      (void)dump_panel.handle(up,size.first,size.second);
      check(texts(render())==top,"Phenomena debug scroll did not return to the head.");
      InputEvent far{InputEventType::Wheel,{},{},-10000.f};
      (void)dump_panel.handle(far,size.first,size.second);
      const auto tail=texts(render());
      (void)dump_panel.handle(far,size.first,size.second);
      check(texts(render())==tail,"Phenomena debug scroll did not clamp at the tail.");
    }
  }
  DrawList system;native.append_system(system,7,0,0,1280,720,.2,{});check(system.overlay.empty(),"Phenomena drew over UI");system.world.emplace_back(Circle{{640,360},16,{255,255,255,255}});check(std::holds_alternative<Circle>(system.world.back()),"Objects cannot render above clouds");
  DrawList close_system;native.append_system(close_system,7,0,0,1280,720,10,{});
  const auto& close_cloud=std::get<Image>(close_system.world.back());
  const auto& far_cloud=std::get<Image>(system.world[system.world.size()-2]);
  check(close_cloud.resource==far_cloud.resource&&close_cloud.destination.x==far_cloud.destination.x&&close_cloud.destination.y==far_cloud.destination.y&&close_cloud.destination.width==far_cloud.destination.width&&close_cloud.destination.height==far_cloud.destination.height,"System zoom enlarges gas layers");
  auto before=phenomenon_context(&fixture,0,0,7);for(int setting=0;setting<3;++setting){VisualOptions visual;visual.density=setting;DrawList view;native.append_system(view,7,0,0,1280,720,.2,visual);check(before.effects==phenomenon_context(&fixture,0,0,7).effects,"Visual preferences changed gameplay");}
  check(visual_multiplier({0})<visual_multiplier({1})&&visual_multiplier({1})<visual_multiplier({2}),"Density levels are identical");
  check(local_visual_multiplier({},.2,true)<local_visual_multiplier({},.2,false)&&local_visual_multiplier({},10)<local_visual_multiplier({},.2),"Combat/close-camera clutter attenuation is missing");
  VisualOptions disabled;disabled.developer_percent=0;check(local_visual_multiplier(disabled,.2)==0,"Developer zero-density override does not hide clouds");
  auto excluded=fixture;excluded.regions.resize(1);for(auto type:{PhenomenonType::SupernovaRemnant,PhenomenonType::RareEnergetic}){excluded.regions[0].type=type;const auto c=phenomenon_context(&excluded,0,0,7);check(!c.overlaps.empty()&&local_art_overlaps(excluded,c).empty(),"Map-only overlap inherited a background");check(alpha_sum(*make_local_environment(excluded,c))==0,"Excluded map artwork leaked into system view");}
  auto cycling=fixture;assign_phenomenon_art(cycling,1234);auto repeat=fixture;assign_phenomenon_art(repeat,1234);check(cycling==repeat,"Artwork selection is nondeterministic");
  for(std::size_t start=0;start+4<=cycling.regions.size();start+=4){std::set<std::string> ids;for(std::size_t i=start;i<start+4;++i)ids.insert(cycling.regions[i].asset_id);check(ids.size()==4,"Artwork repeated before exhausting the matching pool");}
  check(phenomenon_art_usage(cycling).find("SUPPLIED ART NOT USED")!=std::string::npos,"Unused artwork diagnostics missing");
  check(decal_lod_width(30)==256&&decal_lod_width(700)==768&&decal_lod_width(1800)==2944,"LOD levels missing");
  const auto start=std::chrono::steady_clock::now();for(int i=0;i<10000;++i)(void)phenomenon_context(&largest,static_cast<double>(i%230)-80.,static_cast<double>(i%170)-60.,i);
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();check(ms<2000,"Spatial queries exceed bounded CPU budget");
  std::cout<<"Regions: burst="<<burst<<" quiet="<<quiet<<" spiral="<<spiral<<" elliptical="<<elliptical<<" zero cases="<<empty<<" types="<<types.size()<<"; 10,000 queries="<<ms<<"ms; texture cache="<<native.cache_bytes()<<" bytes\n";
  // Locate a naturally cloud-embedded player home for the real UI replay. No
  // regions or player knowledge are injected to make the screenshot pass.
  for(int seed=8000;seed<8500;++seed){GalaxyGenerationConfig c;c.base_seed=seed;c.system_count=250;c.player_species_id="pelagic_high_pressure";c=resolve_galaxy_configuration(c);
    const auto world=seed_persistable_fresh_campaign(seed,catalog,{"2050-03-21T00:00:00Z",250,6,1,c.player_species_id,StellarPopulationOptions{c.morphology,c.resolved_population},false,c});
    const auto& player=*std::ranges::find(world.civilizations,world.player_civilization_id,&Civilization::id);const auto& home=*std::ranges::find(world.systems,player.home_system_id,&StellarSystem::id);const auto context=phenomenon_context(&*world.generation_metadata->phenomena,home.position.x,home.position.y,home.id);
    if(context.visual_intensity>.12&&!local_art_overlaps(*world.generation_metadata->phenomena,context).empty()){std::cout<<"Natural visual replay seed="<<seed<<" home="<<home.id<<" localIntensity="<<context.visual_intensity<<"\n";break;}
  }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
