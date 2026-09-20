#pragma once
#include "native_colony_controller.hpp"
#include "native_menu_style.hpp"
#include "native_planet_rings.hpp"
#include "native_planet_materials.hpp"
#include <stellar/engine/native_geometry3d.hpp>
#include <array>
#include <functional>
#include <numbers>

namespace stellar::native_colony_ui {
using namespace stellar::native_map;
// Geographic survey provinces are deterministic presentation data. Colony
// population and deposits are never divided into invented regional totals.
struct PlanetRegion { int id{};float longitude{},latitude{};std::string name,terrain; };
class NativePlanetGlobe {
 public:
  using Picture=std::shared_ptr<const RgbaImage>;
  // One bounded upload per map, kept beneath the renderer's shared image budget.
  static Picture prepare_map(Picture source,int layer){
    if(!source)return {};
    if(layer==0&&source->width()<=4096)return source;
    const int limit=layer==0?4096:2048,w=std::min(limit,source->width()),h=std::max(1,source->height()*w/source->width());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
      const float sx=(x+.5f)*source->width()/w-.5f,sy=(y+.5f)*source->height()/h-.5f;
      const int x0=std::clamp(static_cast<int>(sx),0,source->width()-1),y0=std::clamp(static_cast<int>(sy),0,source->height()-1),x1=std::min(x0+1,source->width()-1),y1=std::min(y0+1,source->height()-1);
      const float fx=std::max(0.f,sx-x0),fy=std::max(0.f,sy-y0);const auto at=(static_cast<std::size_t>(y)*w+x)*4;
      for(int c=0;c<3;++c){const auto& p=source->pixels();const auto sample=[&](int xx,int yy){return p[(static_cast<std::size_t>(yy)*source->width()+xx)*4+c];};pixels[at+c]=static_cast<std::uint8_t>((sample(x0,y0)*(1-fx)+sample(x1,y0)*fx)*(1-fy)+(sample(x0,y1)*(1-fx)+sample(x1,y1)*fx)*fy);}
      pixels[at+3]=255;
      if(layer){const float intensity=std::max({pixels[at],pixels[at+1],pixels[at+2]})/255.f;pixels[at+3]=static_cast<std::uint8_t>(std::clamp((intensity-(layer==1?.12f:.08f))*1.05f,0.f,1.f)*255);if(layer==2)pixels[at]=pixels[at+1]=pixels[at+2]=240;}
    }
    return RgbaImage::create(w,h,std::move(pixels));
  }
  void set_maps(std::function<Picture(std::string_view,int)> provider){reset();maps_=std::move(provider);}
  void set_materials(stellar::native_planets::MaterialProvider provider){reset();materials_=std::move(provider);}
  void set_visual_seconds(double seconds){visual_seconds_=seconds;hit_key_.reset();}
  void set_simulation_days(double days){days_=days;hit_key_.reset();}
  void reset(){identity_={};appearance_.reset();material_set_.reset();albedo_.reset();night_.reset();mesh_.reset();hit_key_.reset();regions_.clear();yaw_=.25f;pitch_=.18f;roll_=0;zoom_=1.f;dragging_=false;selected_=-1;night_view_=false;}
  void bind(const stellar::native_colony::NativeColonyView& v){
    parent_bearing_=v.rotation_parent_bearing;locked_rotation_=v.stellar_lighting?v.stellar_lighting->locked_rotation:std::nullopt;hit_key_.reset();
    if(albedo_&&identity_==std::pair{v.campaign_generation,v.body_id}&&known_==v.planet.details.has_value()&&kind_==v.planet.visual_class&&texture_key_==v.planet.sol_texture_key&&appearance_==v.planet.appearance)return;
    reset();identity_={v.campaign_generation,v.body_id};known_=v.planet.details.has_value();kind_=v.planet.visual_class;
    texture_key_=v.planet.sol_texture_key;
    appearance_=v.planet.appearance;
    if(known_&&appearance_&&materials_){material_set_=materials_(*appearance_,2048);if(!material_set_)return;albedo_=material_set_->albedo;night_=material_set_->night;yaw_=pitch_=roll_=0;}
    if(!appearance_&&known_&&texture_key_&&maps_){
      albedo_=maps_(*texture_key_,0);
      if(*texture_key_=="earth")night_=maps_(*texture_key_,1);
      if(albedo_){const auto pose=stellar::native_system_ui::planet_presentation_pose(*texture_key_);yaw_=pose.yaw;pitch_=pose.pitch;roll_=pose.roll;}
    }
    if(!albedo_)albedo_=generate(v);
    // Generated worlds use the same seeded surface field for colour and relief.
    // Authored Sol maps have no elevation data: preserve their spherical shape.
    using K=stellar::native_system::NativeSystemBodyVisualClass;
    relief_=!appearance_&&known_&&v.solid_surface&&!v.planet.sol_texture_key&&kind_!=K::gas_giant&&kind_!=K::ice_giant;
    mesh_=appearance_?stellar::native_planets::planet_mesh(appearance_->oblateness,1024):relief_?radial_terrain_mesh([this](Vec3 n){return elevation(n);}):sphere();
    if(!known_)return;
    for(int band=0;band<3;++band)for(int sector=0;sector<4;++sector){
      const float lon=-pi+(sector+.5f)*pi*.5f,lat=(1-band)*pi/3;
      const auto color=sample(lon,lat);
      const bool gas=kind_==stellar::native_system::NativeSystemBodyVisualClass::gas_giant||kind_==stellar::native_system::NativeSystemBodyVisualClass::ice_giant;
      std::string terrain=gas?"Atmospheric band":kind_==stellar::native_system::NativeSystemBodyVisualClass::frozen?"Ice province":kind_==stellar::native_system::NativeSystemBodyVisualClass::hot_rocky?"Volcanic province":color.b>color.r*1.25f?"Ocean basin":color.g>color.r*1.1f?"Vegetated province":"Rocky province";
      regions_.push_back({band*4+sector,lon,lat,(band==0?"Northern ":band==2?"Southern ":"Equatorial ")+std::string(gas?"band ":"province ")+std::to_string(sector+1),terrain});
    }
  }
  [[nodiscard]] const auto& regions()const{return regions_;}
  [[nodiscard]] int selected()const{return selected_;}
  [[nodiscard]] float zoom()const{return zoom_;}
  [[nodiscard]] float rotation()const{return yaw_;}
  void whole(){selected_=-1;zoom_=1.f;}
  void focus(){if(selected_<0)return;const auto& r=regions_[selected_];
    const Vec3 point{std::cos(r.latitude)*std::sin(r.longitude),std::sin(r.latitude)*(1-static_cast<float>(appearance_?appearance_->oblateness:0)),std::cos(r.latitude)*std::cos(r.longitude)};
    const auto p=appearance_?rotate(locked_rotation_.value_or(stellar::native_planets::display_orientation(*appearance_,visual_seconds_,parent_bearing_)),point):point;
    yaw_=std::atan2(p.x,p.z);pitch_=std::atan2(p.y,std::hypot(p.x,p.z));roll_=0;zoom_=1.55f;hit_key_.reset();}
  void set_night(bool enabled){night_view_=enabled;}
  [[nodiscard]] bool night()const{return night_view_;}
  void zoom_by(float factor){zoom_=std::clamp(zoom_*factor,.75f,3.f);}
  [[nodiscard]] Point center(UiRect area)const{return {area.x+area.width*.5f,area.y+area.height*.49f};}
  [[nodiscard]] float radius(UiRect area)const{const float extent=appearance_&&appearance_->rings.enabled?static_cast<float>(appearance_->rings.outer_radius):known_&&texture_key_?stellar::native_system_ui::planet_ring_extent(*texture_key_):1.f;return std::min(area.width*(extent>1?.47f:.54f),area.height*(extent>1?.46f:.50f))*zoom_/extent;}
  [[nodiscard]] std::optional<int> hit(Point p,UiRect area)const{
    if(!known_||!mesh_||!area.contains(p))return {};
    const std::array key{p.x,p.y,area.x,area.y,area.width,area.height,yaw_,pitch_,zoom_};
    if(hit_key_&&*hit_key_==key)return hit_value_;
    hit_key_=key;hit_value_.reset();
    const auto c=center(area);const float r=radius(area),sx=(p.x-c.x)/r,sy=-(p.y-c.y)/r;
    auto inverse=current_rotation();inverse.x=-inverse.x;inverse.y=-inverse.y;inverse.z=-inverse.z;
    const float cy=-.01f*area.height/r;
    const auto unrotate=[&](Vec3 point){const auto p=rotate(inverse,point);return stellar::engine::CollisionVector3{p.x,p.y,p.z};};
    const auto collision=intersect_mesh_segment(*mesh_,unrotate({0,cy,10}),unrotate({sx*1.2f,cy+(sy-cy)*1.2f,-2}));
    if(!collision)return {};
    const auto q=collision->position;const auto length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z);
    const float lon=static_cast<float>(std::atan2(q.x,q.z)),lat=static_cast<float>(std::asin(std::clamp(q.y/length,-1.,1.)));
    hit_value_=std::clamp(static_cast<int>((pi*.5f-lat)/(pi/3)),0,2)*4+std::clamp(static_cast<int>((lon+pi)/(pi*.5f)),0,3);
    return hit_value_;
  }
  bool handle(const InputEvent& e,UiRect area){
    if(e.type==InputEventType::PointerCancelled){dragging_=false;return false;}
    if(e.type==InputEventType::Wheel&&area.contains(e.position)){zoom_by(std::pow(1.12f,e.wheel_y));return true;}
    if(e.type==InputEventType::LeftPressed&&area.contains(e.position)){dragging_=true;clicks_=e.click_count;moved_=0;last_=e.position;return true;}
    if(e.type==InputEventType::PointerMove&&dragging_){const float dx=e.position.x-last_.x,dy=e.position.y-last_.y;moved_+=std::abs(dx)+std::abs(dy);yaw_-=dx*.006f/zoom_;pitch_=std::clamp(pitch_+dy*.004f/zoom_,-1.3f,1.3f);last_=e.position;return true;}
    if(e.type==InputEventType::LeftReleased&&dragging_){dragging_=false;if(moved_<5)if(const auto r=hit(e.position,area)){selected_=*r;if(clicks_>=2||e.click_count>=2)focus();}return true;}
    return false;
  }
  [[nodiscard]] std::optional<Point> project(float longitude,float latitude,UiRect area,float extent=1.f)const{
    const auto p=rotate(current_rotation(),{std::cos(latitude)*std::sin(longitude),std::sin(latitude)*(1-static_cast<float>(appearance_?appearance_->oblateness:0)),std::cos(latitude)*std::cos(longitude)});
    const float base=radius(area),cy=-.01f*area.height/base;
    if(p.z*10+p.y*cy<1.025f)return {};
    const float scale=extent*(1+surface_elevation(longitude,latitude)),perspective=10/(10-p.z*scale);
    const auto c=center(area);return Point{c.x+p.x*base*scale*perspective,c.y-base*((p.y*scale-cy)*perspective+cy)};
  }
  [[nodiscard]] float surface_elevation(float longitude,float latitude)const{return elevation({std::cos(latitude)*std::sin(longitude),std::sin(latitude),std::cos(latitude)*std::cos(longitude)});}
  void render(DrawList& out,UiRect area,const stellar::native_colony::NativeColonyView& v,int layer,int font){
    bind(v);if(!albedo_){native_menu_style::text(out,area,"Loading planet material...",font,native_menu_style::ink,TextAlign::Center);return;}
    const auto c=center(area);const float r=radius(area);
    TriangleMesh halo;halo.clip=area;halo.color={255,255,255,255};
    const bool atmosphere=v.planet.details&&v.planet.details->pressure_kpa>.1;
    if(atmosphere&&!appearance_){for(int i=0;i<=128;++i){const float a=i*2*pi/128;for(int edge=0;edge<2;++edge){const float rr=r*(edge?1.045f:1.f);halo.vertices.push_back({c.x+std::cos(a)*rr,c.y+std::sin(a)*rr});halo.vertex_colors.push_back({66,154,230,static_cast<std::uint8_t>(edge?0:90)});}if(i<128){const int j=i*2;for(int n:{j,j+1,j+2,j+1,j+3,j+2})halo.indices.push_back(n);}}out.overlay.emplace_back(std::move(halo));}
    append_scene(out,area,v);
    if(!known_)return;
    // Thin projected survey boundaries, never screen-aligned opaque grids.
    for(const auto& region:regions_){
      const bool chosen=region.id==selected_;
      const float lon0=region.longitude-pi/4,lon1=region.longitude+pi/4,lat0=region.latitude-pi/6,lat1=region.latitude+pi/6;
      const Color color=chosen?Color{106,231,255,225}:Color{93,177,206,55};
      const auto line=[&](float a,float b,float d,float e){auto p=project(a,b,area),q=project(d,e,area);if(p&&q&&area.contains(*p)&&area.contains(*q))out.overlay.emplace_back(Line{*p,*q,color});};
      if(layer==0||chosen)for(int i=0;i<16;++i){const float f=i/16.f,g=(i+1)/16.f;line(lon0+(lon1-lon0)*f,lat0,lon0+(lon1-lon0)*g,lat0);line(lon0,lat0+(lat1-lat0)*f,lon0,lat0+(lat1-lat0)*g);if(chosen){line(lon0+(lon1-lon0)*f,lat1,lon0+(lon1-lon0)*g,lat1);line(lon1,lat0+(lat1-lat0)*f,lon1,lat0+(lat1-lat0)*g);}}
      if(auto p=project(region.longitude,region.latitude,area);p&&area.contains(*p)&&(chosen||zoom_>1.35f)){
        UiRect label{p->x-85,p->y-10,170,42};label.x=std::clamp(label.x,area.x,area.x+std::max(0.f,area.width-label.width));
        out.overlay.emplace_back(FilledRectangle{label,{3,15,26,190}});native_menu_style::text(out,label,region.name,font,chosen?native_menu_style::cyan:native_menu_style::ink,TextAlign::Center);
      }
    }
    // Exact constructed sites live in the colony's local coordinate frame.
    // Their visible extent grows with actual hub development, not fake population.
    if(layer==1||layer==2||night_view_)for(const auto& b:v.construction_sites){
      if(!b.complete)continue;const float lon=.15f+b.x*.003f,lat=.2f+b.z*.003f;
      if(auto p=project(lon,lat,area);p&&area.contains(*p)){
        const Color light=layer==2?(b.enabled&&b.powered?Color{96,236,168,240}:Color{242,143,81,230}):Color{144,213,249,230};
        out.overlay.emplace_back(FilledRectangle{{p->x-2,p->y-2,4,4},light});
      }
    }
  }
 private:
  static constexpr float pi=std::numbers::pi_v<float>;
  std::function<Picture(std::string_view,int)> maps_;Picture albedo_,night_;
  stellar::native_planets::MaterialProvider materials_;
  std::shared_ptr<const stellar::native_planets::MaterialSet> material_set_;
  std::optional<stellar::core::PlanetAppearance> appearance_;double days_{},visual_seconds_{},parent_bearing_{};
  std::optional<Quaternion> locked_rotation_;
  static Vec3 rotate(Quaternion q,Vec3 v){const Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};}
  Quaternion manual_rotation()const{return compose_rotation(rotation_axis_angle({0,0,1},roll_),compose_rotation(rotation_axis_angle({1,0,0},pitch_),rotation_axis_angle({0,1,0},-yaw_)));}
  Quaternion current_rotation()const{return appearance_?compose_rotation(manual_rotation(),locked_rotation_.value_or(stellar::native_planets::display_orientation(*appearance_,visual_seconds_,parent_bearing_))):manual_rotation();}
  std::optional<std::string> texture_key_;
  std::shared_ptr<const Mesh3D> mesh_;bool relief_{};
  mutable std::optional<std::array<float,9>> hit_key_;mutable std::optional<int> hit_value_;
  std::pair<std::uint64_t,int> identity_{};std::vector<PlanetRegion> regions_;
  stellar::native_system::NativeSystemBodyVisualClass kind_{};
  float yaw_{.25f},pitch_{.18f},roll_{},zoom_{1},moved_{};bool dragging_{},known_{},night_view_{};int selected_{-1},clicks_{};Point last_{};
  static const std::shared_ptr<const Mesh3D>& sphere(){static const auto value=Mesh3D::uv_sphere();return value;}
  static float surface_field(Vec3 n,int body){const float seed=static_cast<float>(body%997)*.017f;
    return std::sin(n.x*8+seed+std::sin(n.y*11))*std::cos(n.z*9-seed)+.38f*std::sin(n.x*27+n.z*21+n.y*19+seed)+.12f*std::sin(n.x*117+n.y*101+n.z*109);}
  float elevation(Vec3 n)const{
    if(!relief_)return 0;
    // Presentation relief only. Colony construction keeps Core's slot rules;
    // geographic provinces do not fabricate terrain-dependent game statistics.
    return .006f*std::max(0.f,surface_field({-n.x,n.y,-n.z},identity_.second));
  }
  [[nodiscard]] Color sample(float lon,float lat)const{
    const int x=std::clamp(static_cast<int>((lon+pi)/(2*pi)*albedo_->width()),0,albedo_->width()-1),y=std::clamp(static_cast<int>((pi*.5f-lat)/pi*albedo_->height()),0,albedo_->height()-1);
    const auto i=(static_cast<std::size_t>(y)*albedo_->width()+x)*4;const auto& p=albedo_->pixels();return {p[i],p[i+1],p[i+2],p[i+3]};
  }
  static Picture generate(const stellar::native_colony::NativeColonyView& v){
    constexpr int w=1024,h=512;std::vector<std::uint8_t> pixels(w*h*4);
    using K=stellar::native_system::NativeSystemBodyVisualClass;
    const auto kind=v.planet.visual_class;const float seed=static_cast<float>(v.body_id%997)*.017f;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
      const float lon=x*2*pi/w,lat=y*pi/h;const float nx=std::sin(lon)*std::sin(lat),nz=std::cos(lon)*std::sin(lat),ny=std::cos(lat);
      const float n=surface_field({nx,ny,nz},v.body_id);
      Color color{104,94,82,255};
      if(!v.planet.details)color={32,44,57,255};
      else if(kind==K::gas_giant||kind==K::ice_giant){const float band=.5f+.5f*std::sin(lat*53+std::sin(lon*5+seed)*.7f);color=kind==K::ice_giant?Color{64,140,171,255}:Color{177,139,91,255};color.r=static_cast<std::uint8_t>(color.r*(.65f+.35f*band));color.g=static_cast<std::uint8_t>(color.g*(.65f+.35f*band));}
      else if(kind==K::frozen)color=n>0?Color{178,196,203,255}:Color{104,141,159,255};
      else if(kind==K::hot_rocky)color=n>.75f?Color{246,90,20,255}:Color{68,42,36,255};
      else if(kind==K::oceanic){color=n<.1f?Color{12,47,83,255}:n<.5f?Color{56,83,49,255}:Color{130,117,79,255};if(std::abs(ny)>.90f)color={205,214,220,255};}
      else if(v.planet.details->available_solvent==stellar::core::PlanetarySolventRegime::Water&&v.planet.details->temperature_kelvin>=260&&v.planet.details->temperature_kelvin<315){color=n<-.2f?Color{12,45,73,255}:n<.7f?Color{58,83,48,255}:Color{143,125,86,255};if(std::abs(ny)>.92f)color={207,218,223,255};}
      const float shade=.85f+.15f*std::clamp(n,-1.f,1.f);const auto i=(y*w+x)*4;pixels[i]=static_cast<std::uint8_t>(color.r*shade);pixels[i+1]=static_cast<std::uint8_t>(color.g*shade);pixels[i+2]=static_cast<std::uint8_t>(color.b*shade);pixels[i+3]=255;
    }return RgbaImage::create(w,h,std::move(pixels));
  }
  void append_scene(DrawList& out,UiRect area,const stellar::native_colony::NativeColonyView& v)const{
    // One immutable sphere stays resident on the GPU; interaction updates only
    // camera/material constants. No CPU vertex projection or lighting per frame.
    const auto& mesh=mesh_;
    const float height=area.height/radius(area);
    Camera3D camera;camera.projection=Projection3D::Perspective;
    camera.position={0,-.01*height,10};camera.vertical_fov_radians=2*std::atan(height/20);
    camera.near_plane=.1f;camera.far_plane=30;
    if(appearance_&&material_set_){std::vector<MeshInstance3D> instances;
      const auto light=v.stellar_lighting.value_or(stellar::native_planets::lighting(v.illumination_x,v.illumination_y,v.illumination_star?&*v.illumination_star:nullptr,v.planet.details&&v.planet.details->stellar_exposure?v.planet.details->stellar_exposure->incident_flux:1));
      stellar::native_planets::append_instances(instances,*appearance_,*material_set_,{},1,1024,days_,light,manual_rotation(),night_view_,v.population_millions>0,visual_seconds_,parent_bearing_);
      out.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(instances)),area});return;}
    const auto rotation=compose_rotation(rotation_axis_angle({0,0,1},roll_),compose_rotation(rotation_axis_angle({1,0,0},pitch_),rotation_axis_angle({0,1,0},-yaw_)));
    std::vector<MeshInstance3D> instances;instances.reserve(3);
    Material3D surface;surface.texture=albedo_;surface.ambient=night_view_?.08f:.12f;surface.diffuse=night_view_?.12f:.88f;
    instances.push_back({mesh,{},rotation,1,surface});
    if(night_&&v.infrastructure>0&&v.population_millions>0){
      Material3D emission;emission.texture=night_;emission.ambient=1;emission.diffuse=0;emission.transparent=true;
      emission.opacity=std::clamp(static_cast<float>(v.infrastructure)*(night_view_?.95f:1.f),0.f,1.f)*220.f/255.f;
      emission.dark_side_strength=night_view_?0:1.45f;
      instances.push_back({mesh,{},rotation,1,emission});
    }
    if(known_&&texture_key_)for(auto ring:stellar::native_system_ui::planet_ring_instances(*texture_key_)){ring.rotation=rotation;instances.push_back(std::move(ring));}
    out.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(instances)),area});
  }
};
}
