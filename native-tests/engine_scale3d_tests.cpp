#include <stellar/engine/physics3d.hpp>
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/spatial_index3d.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/developer_campaign.hpp>
#include <stellar/core/lane_network.hpp>
#include "galaxy_payload_json_internal.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

using namespace stellar::core;
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void geometry_and_physics(){
  std::string json;
  detail::JsonStreamWriter writer([&](std::string_view chunk){json+=chunk;});
  writer.begin_object();writer.field("owned_sink",true);writer.end_object();
  require(json=="{\n  \"owned_sink\": true\n}","Writer lost its temporary sink after construction");
  bool empty_sink=false;try{detail::JsonStreamWriter invalid_writer({});}catch(const std::invalid_argument&){empty_sink=true;}
  require(empty_sink,"Empty JSON sink accepted");
  using namespace stellar::engine;using namespace stellar::native_map;
  const auto moved=move_toward_velocity({{1,2,3},{}},{0,0,100},2,.1f);
  require(moved.velocity.z==2&&std::abs(moved.position.z-3.2f)<1e-6f,"3D acceleration/motion failed");
  const auto hit=segment_sphere({0,0,-100},{0,0,100},{0,0,0},1);
  require(hit&&std::abs(*hit-.495)<1e-12,"Fast segment tunnelled through sphere");
  require(!segment_sphere({2,0,-100},{2,0,100},{0,0,0},1),"Sphere false positive");
  require(segment_sphere({0,0,0},{0,0,0},{0,0,0},1)==0.,"Initial overlap lost");
  const auto terrain=heightfield_mesh(32,16,[](float x,float y){return x*.25f+y*.5f;});
  const auto& a=terrain->vertices()[0];require(std::abs(a.normal.z-.8728716f)<1e-5f,"Heightfield normals incorrect");
  const auto collision=segment_triangle({0,0,10},{0,0,-10},{-1,-1,0},{1,-1,0},{0,1,0});
  require(collision&&std::abs(*collision-.5)<1e-12,"Surface triangle collision failed");
  const auto globe=radial_terrain_mesh([](Vec3){return .125f;});
  const auto globe_hit=intersect_mesh_segment(*globe,{0,0,2},{0,0,-2});
  require(globe_hit&&std::abs(globe_hit->position.z-1.125)<1e-6,"Radial mesh picking disagrees with rendered radius");
  require(!intersect_mesh_segment(*globe,{2,0,2},{2,0,-2}),"Radial mesh false hit");
  for(const auto& v:globe->vertices())require(v.normal.x*v.position.x+v.normal.y*v.position.y+v.normal.z*v.position.z>1.124f,"Radial normals point inward");
  bool bad_elevation=false;try{(void)radial_terrain_mesh([](Vec3){return 1.f;});}catch(const std::invalid_argument&){bad_elevation=true;}
  require(bad_elevation,"Out-of-range terrain was accepted");
  bool invalid=false;try{(void)move_toward_velocity({}, {},-1,.1f);}catch(const std::invalid_argument&){invalid=true;}
  require(invalid,"Invalid physics accepted");
}
void indexed_neighbors(){
  using Index=stellar::engine::SpatialIndex3D;std::vector<Index::Point> p;std::mt19937 random(941);
  for(int i=0;i<1200;++i)p.push_back({double(random()%101),double(random()%101),double(random()%7)});
  p[1]=p[0];Index index(p);std::vector<std::size_t> labels(p.size());
  for(std::size_t i=0;i<p.size();++i)labels[i]=i%4;index.partitions(labels);
  const auto metric=[&](std::size_t a,std::size_t b){double d=0;for(int axis=0;axis<3;++axis){auto x=p[a][axis]-p[b][axis];d+=x*x;}return d;};
  for(std::size_t q=0;q<p.size();q+=7)for(bool outside:{false,true}){
    std::vector<std::pair<double,std::size_t>> brute;
    for(std::size_t i=0;i<p.size();++i)if(i!=q&&(!outside||labels[i]!=labels[q]))brute.emplace_back(metric(q,i),i);
    std::sort(brute.begin(),brute.end());const auto matches=index.nearest(q,3,outside,metric);
    require(matches.size()==3,"Missing spatial neighbors");for(std::size_t i=0;i<3;++i)require(matches[i].index==brute[i].second,"3D index disagrees with brute force");
  }
  std::fill(labels.begin(),labels.end(),0);index.partitions(labels);require(index.nearest(0,1,true,metric).empty(),"Uniform partition was not excluded");
}
int main(int argc,char** argv)try{
  require(argc>=3,"Catalog and count required");geometry_and_physics();indexed_neighbors();
  const int count=std::stoi(argv[2]);GalaxyGenerationConfig config;config.base_seed=8057;config.system_count=count;config=resolve_galaxy_configuration(config);
  const auto start=std::chrono::steady_clock::now();
  auto world=seed_persistable_fresh_campaign(config.base_seed,load_nearby_catalog(argv[1]),
    {"2050-03-21T00:00:00Z",count,6,1,"terran_baseline",StellarPopulationOptions{config.morphology,config.resolved_population},false,config});
  require(world.systems.size()==static_cast<std::size_t>(count),"Galaxy truncated");
  if(argc==4&&std::string_view(argv[3])=="--generation-benchmark"){
    const auto generated=std::chrono::steady_clock::now();
    auto snapshot=capture_galaxy_payload_v16(world,{0,"generation-parity","2050-03-21T00:00:00Z"});
    std::uint64_t fingerprint=14695981039346656037ULL;std::size_t bytes=0;
    stellar::engine::AtomicTextSink sink=[&](std::string_view chunk){bytes+=chunk.size();for(unsigned char c:chunk){fingerprint^=c;fingerprint*=1099511628211ULL;}};
    detail::JsonStreamWriter out(sink);
    out.begin_object();detail::stream_galaxy_members(out,snapshot,16);out.end_object();
    if(count==50000)require(bytes==440690994&&fingerprint==15864570924479060606ULL,"Indexed generation changed the complete baseline campaign");
    std::cout<<"systems="<<count<<" generation_ms="<<std::chrono::duration<double,std::milli>(generated-start).count()<<" bytes="<<bytes<<" fingerprint="<<fingerprint<<'\n';return 0;
  }
  if(argc==4){
    const std::string_view mode=argv[3];require(mode=="--memory-stream"||mode=="--memory-dom","Unknown benchmark mode");
    auto snapshot=capture_galaxy_payload_v16(world,{0,"memory-test","2050-03-21T00:00:00Z"});
    const auto path=std::filesystem::temp_directory_path()/("stellar-memory-"+std::to_string(start.time_since_epoch().count())+".json");
    if(mode=="--memory-stream")stellar::engine::write_file_atomically_stream(path,[&](const stellar::engine::AtomicTextSink& sink){detail::JsonStreamWriter out(sink);out.begin_object();detail::stream_galaxy_members(out,snapshot,16);out.end_object();});
    else {const auto json=detail::encode_galaxy_payload_v16_document(snapshot).dump(2);stellar::engine::write_file_atomically(path,std::as_bytes(std::span(json)));}
    std::cout<<"save_bytes="<<std::filesystem::file_size(path)<<'\n';std::filesystem::remove(path);return 0;
  }
  const auto generated=std::chrono::steady_clock::now();InterstellarLaneNetwork network(world.systems);const auto lanes=network.build();
  const auto built=std::chrono::steady_clock::now();
  for(int i:{0,count/2,count-1}){auto route=network.find_shortest_route(i,(i+count/3)%count,1e9);require(route.size()>1,"Disconnected large galaxy");}
  auto shuffled=world.systems;std::reverse(shuffled.begin(),shuffled.end());InterstellarLaneNetwork second(shuffled);const auto other=second.build();
  require(lanes.size()==other.size(),"Order-dependent graph size");
  for(std::size_t i=0;i<lanes.size();++i)require(lanes[i].first_system_id==other[i].first_system_id&&lanes[i].second_system_id==other[i].second_system_id&&lanes[i].length_light_years==other[i].length_light_years,"Order-dependent lane graph");
  const auto routed=std::chrono::steady_clock::now();
  auto snapshot=capture_galaxy_payload_v16(world,{0,"scale-3d-test","2050-03-21T00:00:00Z"});
  const auto scratch=std::filesystem::temp_directory_path()/("stellar-scale-"+std::to_string(count)+"-"+std::to_string(start.time_since_epoch().count())+".json");
  std::size_t bytes=0,max_chunk=0;
  stellar::engine::write_file_atomically_stream(scratch,[&](const stellar::engine::AtomicTextSink& sink){
    stellar::engine::AtomicTextSink counted=[&](std::string_view text){bytes+=text.size();max_chunk=std::max(max_chunk,text.size());sink(text);};
    detail::JsonStreamWriter out(counted);out.begin_object();detail::stream_galaxy_members(out,snapshot,16);out.end_object();
  });
  const auto saved=std::chrono::steady_clock::now();
  {
    std::ifstream input(scratch,std::ios::binary);std::string json((std::istreambuf_iterator<char>(input)),{});
    auto loaded=restore_galaxy_payload_v16(decode_galaxy_payload_v16_json(json)).galaxy;
    require(loaded.systems.size()==world.systems.size()&&loaded.bodies.size()==world.bodies.size(),"Large streamed save lost bodies/systems");
    for(std::size_t i=0;i<world.systems.size();++i)require(loaded.systems[i].name==world.systems[i].name&&loaded.systems[i].position.depth_light_years==world.systems[i].position.depth_light_years,"Large save changed identity/depth");
  }
  if(count==250){
    PlayerCampaignPayloadV17Dto player;player.galaxy=snapshot;player.galaxy.game_version="Unicode \xc3\xa9";
    std::string streamed;stream_player_campaign_v17_json(player,[&](std::string_view text){streamed+=text;});
    require(streamed==encode_player_campaign_v17_json(player),"Player stream bytes differ");
    DeveloperCampaignPayload developer{player,{},{}};streamed.clear();stream_developer_campaign_json(developer,[&](std::string_view text){streamed+=text;});
    require(streamed==encode_developer_campaign_json(developer),"Developer stream bytes differ");
  }
  bool threw=false;try{stellar::engine::write_file_atomically_stream(scratch,[](const stellar::engine::AtomicTextSink& sink){sink(std::string(70000,'x'));throw std::runtime_error("producer failed");});}catch(const std::runtime_error&){threw=true;}
  require(threw&&std::filesystem::file_size(scratch)==bytes,"Failed stream changed the saved file");
  std::filesystem::remove(scratch);
  const auto end=std::chrono::steady_clock::now();const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
  std::cout<<"systems="<<count<<" bodies="<<world.bodies.size()<<" lanes="<<lanes.size()<<" generation_ms="<<ms(start,generated)<<" lanes_ms="<<ms(generated,built)<<" route_and_repeat_ms="<<ms(built,routed)<<" capture_stream_ms="<<ms(routed,saved)<<" reload_and_checks_ms="<<ms(saved,end)<<" save_bytes="<<bytes<<" largest_emitted_chunk="<<max_chunk<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
