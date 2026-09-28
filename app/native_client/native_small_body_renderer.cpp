#include "native_small_body_renderer.hpp"
#include <stellar/engine/billboard_batch.hpp>
#include <algorithm>
#include <numbers>

namespace stellar::native_system_ui {
using namespace stellar::native_map;
using namespace stellar::native_system;
using namespace stellar::core;
namespace {
constexpr double tau=2*std::numbers::pi;
Color color(SmallBodyFieldType t){switch(t){
  case SmallBodyFieldType::Ice:return {167,218,248,255};
  case SmallBodyFieldType::Metallic:return {215,195,154,255};
  case SmallBodyFieldType::Carbonaceous:return {134,126,121,255};
  case SmallBodyFieldType::CrackedCluster:case SmallBodyFieldType::Shattered:return {196,167,145,255};
  default:return {176,171,161,255};}}
Point projected(const SmallBodyField& f,const SystemSpatialSnapshot& s,const SystemSpatialViewport& v,std::array<double,3> p){
  double x=0,y=0;const double radius=std::hypot(p[0],p[1],p[2]);float display=0;
  if(f.planet_centered){const auto parent=std::ranges::find(s.bodies,f.associated_planet_id,&SystemSpatialBodyMarker::body_id);if(parent==s.bodies.end())return {-1e6f,-1e6f};x=parent->offset_x;y=parent->offset_y;
    display=parent->display_radius*static_cast<float>(radius/f.inner_radius_au)*3.f;
  }else {display=system_display_orbit_radius(s,radius);x=s.stellar_hosts[s.belt_host].x;y=s.stellar_hosts[s.belt_host].y;}
  // Flatten inclination onto the schematic orbital band. Height belongs to
  // the shared 3D depth buffer, not a second skew of the chart's radial scale.
  const double planar=std::hypot(p[0],p[1]),factor=planar>0?display/planar:0;
  const auto screen=v.world_to_screen(static_cast<float>(x+p[0]*factor),static_cast<float>(y+p[1]*factor));return {screen.x,screen.y};
}
bool visible(UiRect clip,Point p,float r){return p.x+r>=clip.x&&p.x-r<=clip.x+clip.width&&p.y+r>=clip.y&&p.y-r<=clip.y+clip.height;}
}
Point NativeSmallBodyRenderer::position(const SmallBodyField& f,const SmallBodyInstance& b,const SystemSpatialSnapshot& s,const SystemSpatialViewport& v,double days){return projected(f,s,v,stellar::engine::analytic_orbit_position(b.orbit,days-f.epoch_days));}
void NativeSmallBodyRenderer::render(DrawList& out,const NativeSystemSnapshot& snapshot,const SystemSpatialSnapshot& spatial,const SystemSpatialViewport& view,UiRect clip,double days,bool debug){
  statistics_={};hits_.clear();last_scene_.reset();
  if(snapshot.survey_level!=SystemSurveyLevel::fully_surveyed){clear();return;}
  if(generation_!=snapshot.campaign_generation||system_!=snapshot.system_id){clear();generation_=snapshot.campaign_generation;system_=snapshot.system_id;}
  if(clip.width<=0||clip.height<=0)return;
  struct Solid{const SmallBodyInstance* body;int field;Point p;float radius,z;};
  std::vector<Solid> solids;
  TriangleMesh particles;particles.color={255,255,255,255};particles.clip=clip;
  for(const auto& f:snapshot.small_body_fields){
    ++statistics_.fields;auto it=cache_.find(f.id);if(it==cache_.end()||it->second.field!=f){cache_[f.id]={f,small_body_instances(f,2048)};it=cache_.find(f.id);}
    const auto& bodies=it->second.bodies;statistics_.instances+=bodies.size();const Color tint=color(f.type);
    // All visible rocks come from the canonical instances below. The former
    // far/dust photographs contained large painted asteroids that slid around
    // the star as flat strips and could never tumble, hiding the solid bodies.
    // Visit every representative: index truncation used to hide rare large
    // bodies at overview zoom. Geometry detail follows projected size instead.
    const std::size_t limit=bodies.size();
    for(std::size_t i=0;i<limit;++i){const auto& body=bodies[i];const auto p=position(f,body,spatial,view,days);
      const float radius=small_body_display_radius(body)*view.scale;
      if(!visible(clip,p,std::max(.55f,radius)))continue;++statistics_.visible;
      if(radius>=13.f*view.scale)++statistics_.large_bodies;
      Color material=body.material>=SmallBodyMaterial::WaterIce?Color{210,237,255,255}:tint;
      if(body.material==SmallBodyMaterial::Metal)material={235,213,175,255};if(body.material==SmallBodyMaterial::Carbon)material={140,134,128,255};
      for(auto* c:{&material.r,&material.g,&material.b})*c=static_cast<std::uint8_t>(std::clamp(double(*c)*body.material_brightness,0.,255.));
      if(radius<2.2f){
        // Keep unresolved material faint; it does not become a full-screen fog.
        material.a=static_cast<std::uint8_t>(std::clamp(radius*90.f,28.f,170.f));
        if(particles.vertices.size()+4<=maximum_triangle_mesh_vertices&&particles.indices.size()+6<=maximum_triangle_mesh_indices)
          append_billboard(particles,p,std::clamp(radius,.35f,1.1f),std::clamp(radius,.35f,1.1f),{0,0,0,1},material);
      }else {
        const auto orbit=stellar::engine::analytic_orbit_position(body.orbit,days-f.epoch_days);
        const float depth=static_cast<float>(orbit[2]/std::max(.000001,std::hypot(orbit[0],orbit[1])))*system_display_orbit_radius(spatial,std::hypot(orbit[0],orbit[1]))*view.scale;
        solids.push_back({&body,f.id,p,radius,std::clamp(depth,-clip.height*10,clip.height*10)});
      }
    }
    if(debug){for(double radius:{f.inner_radius_au,f.outer_radius_au}){TriangleMesh lines;lines.color={255,255,255,255};lines.clip=clip;
        for(int i=0;i<128;++i){const double a=tau*i/128,b=tau*(i+1)/128;const auto p=projected(f,spatial,view,{radius*std::cos(a),radius*std::sin(a),0}),q=projected(f,spatial,view,{radius*std::cos(b),radius*std::sin(b),0});
          if(!visible(clip,p,2)&&!visible(clip,q,2))continue;const int start=static_cast<int>(lines.vertices.size());lines.vertices.insert(lines.vertices.end(),{{p.x,p.y-.4f},{q.x,q.y-.4f},{q.x,q.y+.4f},{p.x,p.y+.4f}});for(int k:{0,1,2,0,2,3})lines.indices.push_back(start+k);}
        lines.color={tint.r,tint.g,tint.b,150};if(!lines.indices.empty())out.world.emplace_back(std::move(lines));}}
  }
  if(!particles.indices.empty()){++statistics_.batches;out.world.emplace_back(std::move(particles));}
  if(!solids.empty()){
    std::stable_sort(solids.begin(),solids.end(),[](const auto& a,const auto& b){return a.radius>b.radius;});
    // One depth buffer for the entire field; no per-object render targets.
    if(solids.size()>768)solids.resize(768);
    const float unit=2.f/clip.height;
    Camera3D camera;camera.projection=Projection3D::Orthographic;camera.orthographic_height=2;camera.position={0,0,100};camera.near_plane=1;camera.far_plane=200;
    std::vector<MeshInstance3D> instances;instances.reserve(solids.size());
    const auto host=spatial.stellar_hosts[spatial.belt_host];
    const auto star=view.world_to_screen(host.x,host.y);
    for(const auto& s:solids){
      const auto& b=*s.body;auto spin=b.spin;
      // One revolution in roughly 90–180 real seconds, regardless of strategic
      // speed or accumulated campaign age. Retain seeded axes and irregularity.
      const double rate=std::copysign(.035+.035*std::clamp(std::abs(spin.rate),0.,1.),spin.rate);
      spin.precession=std::abs(spin.rate)>1e-9?spin.precession*rate/spin.rate:0;
      spin.rate=rate;
      const auto q=stellar::engine::analytic_spin_rotation(spin,tumble_seconds_);
      Material3D surface;surface.texture=geometry_.texture(b);surface.ambient=.12f;surface.diffuse=.88f;
      surface.dielectric=geometry_.optics(b);
      const float dx=star.x-s.p.x,dy=s.p.y-star.y,len=std::max(1.f,std::hypot(dx,dy));
      surface.light_direction=Vec3{dx/len,dy/len,.65f};
      const auto brightness=static_cast<std::uint8_t>(std::clamp(b.material_brightness*235,0.,255.));surface.tint={brightness,brightness,brightness,255};
      instances.push_back({geometry_.mesh(b,s.radius>=28),{(s.p.x-clip.x-clip.width*.5f)*unit,(clip.y+clip.height*.5f-s.p.y)*unit,s.z*unit},
        {float(q[0]),float(q[1]),float(q[2]),float(q[3])},s.radius*unit,surface});
      hits_.push_back({s.field,b.id,s.p,s.radius});
    }
    last_scene_=Scene3D::create(camera,std::move(instances));
    out.world.emplace_back(Scene3DView{last_scene_,clip});
    statistics_.solid_bodies=solids.size();statistics_.batches+=solids.size();
  }
  std::erase_if(cache_,[&](const auto& item){return std::ranges::none_of(snapshot.small_body_fields,[&](const auto& f){return f.id==item.first;});});
}
std::optional<SmallBodyHit> NativeSmallBodyRenderer::hit(Point point)const{
  std::optional<SmallBodyHit> result;float best=1e9f;for(const auto& h:hits_){const float d=std::hypot(point.x-h.screen.x,point.y-h.screen.y);if(d<std::max(5.f,h.radius)&&d<best){best=d;result=h;}}return result;
}
}
