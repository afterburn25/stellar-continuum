#include <stellar/engine/native_scene3d.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

namespace stellar::native_map {
TextureMipLayout3D texture_mip_layout3d(const RgbaImage* image) noexcept {
  int width=image?image->width():1,height=image?image->height():1;
  TextureMipLayout3D layout;
  for(;;){
    ++layout.levels;layout.gpu_bytes+=static_cast<std::size_t>(width)*height*4u;
    if(width==1&&height==1)break;
    width=std::max(1,width/2);height=std::max(1,height/2);
  }
  layout.resident_bytes=layout.gpu_bytes+(image?image->byte_size():4u);
  return layout;
}
Vec3 blackbody_light_color(double kelvin,double reference_kelvin){
  if(!std::isfinite(kelvin)||!std::isfinite(reference_kelvin)||kelvin<100||kelvin>100000||reference_kelvin<100||reference_kelvin>100000)
    throw std::invalid_argument("Light temperature must be between 100 and 100000 kelvin.");
  constexpr std::array<double,3> wavelength{610e-9,550e-9,460e-9};
  std::array<double,3> bands{};
  for(std::size_t i=0;i<3;++i)bands[i]=std::expm1(.01438776877/(wavelength[i]*reference_kelvin))/std::expm1(.01438776877/(wavelength[i]*kelvin));
  const double peak=*std::max_element(bands.begin(),bands.end());
  return {static_cast<float>(bands[0]/peak),static_cast<float>(bands[1]/peak),static_cast<float>(bands[2]/peak)};
}
namespace {
bool bounded(double x,double limit){return std::isfinite(x)&&std::abs(x)<=limit;}
bool valid(Vec3 v){return bounded(v.x,1e6)&&bounded(v.y,1e6)&&bounded(v.z,1e6);}
bool valid(Position3 v){return bounded(v.x,1e15)&&bounded(v.y,1e15)&&bounded(v.z,1e15);}
Vec3 normalized(Vec3 v){
  if(!valid(v))throw std::invalid_argument("3D direction must be finite and bounded.");
  const auto length=std::hypot(v.x,v.y,v.z);
  if(length<1e-8f)throw std::invalid_argument("3D direction must be nonzero.");
  return {v.x/length,v.y/length,v.z/length};
}
Quaternion normalized(Quaternion q){
  if(!bounded(q.x,1e6)||!bounded(q.y,1e6)||!bounded(q.z,1e6)||!bounded(q.w,1e6))throw std::invalid_argument("3D rotation must be finite and bounded.");
  const auto length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
  if(length<1e-8f)throw std::invalid_argument("3D rotation must be nonzero.");
  return {q.x/length,q.y/length,q.z/length,q.w/length};
}
Matrix4 rotation_matrix(Quaternion q){
  q=normalized(q);const auto [x,y,z,w]=q;
  return {{{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
            2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,0,0,0,1}}};
}
void validate_camera(const Camera3D& c){
  if(!valid(c.position)||!bounded(c.near_plane,1e7)||!bounded(c.far_plane,1e7)||c.near_plane<1e-6f||c.far_plane<=c.near_plane||
     !bounded(c.vertical_fov_radians,3.1)||c.vertical_fov_radians<.01f||
     !bounded(c.orthographic_height,1e7)||c.orthographic_height<1e-6f||
     (c.projection!=Projection3D::Perspective&&c.projection!=Projection3D::Orthographic))
    throw std::invalid_argument("3D camera has invalid position, projection or clipping planes.");
  (void)normalized(c.orientation);
}
void validate_instance(const MeshInstance3D& i){
  if(!i.mesh||!valid(i.position)||!bounded(i.scale,1e5)||i.scale<1e-8f||i.mesh->bounding_radius()*i.scale>1e7)
    throw std::invalid_argument("3D instance requires a mesh and finite bounded transform.");
  (void)normalized(i.rotation);const auto& m=i.material;
  if(!bounded(m.ambient,1)||m.ambient<0||!bounded(m.diffuse,1)||m.diffuse<0||
     !bounded(m.opacity,1)||m.opacity<0||!bounded(m.dark_side_strength,16)||m.dark_side_strength<0)
    throw std::invalid_argument("3D material lighting and opacity must be finite and bounded.");
  if(m.light_direction)(void)normalized(*m.light_direction);
  for(const auto& l:m.additional_lights){(void)normalized(l.direction);if(!valid(l.color)||l.color.x<0||l.color.y<0||l.color.z<0||l.color.x>4||l.color.y>4||l.color.z>4||!bounded(l.intensity,16)||l.intensity<0)throw std::invalid_argument("Invalid additional light");}
  if(!valid(m.light_color)||m.light_color.x<0||m.light_color.y<0||m.light_color.z<0||
     m.light_color.x>4||m.light_color.y>4||m.light_color.z>4||!bounded(m.light_intensity,16)||m.light_intensity<0||!bounded(m.rim_power,16)||m.rim_power<0)
    throw std::invalid_argument("3D light color, intensity and rim response must be bounded.");
  if(m.surface_effect){const auto& e=*m.surface_effect;
    if(!e.next_texture||!bounded(e.blend,1)||e.blend<0||!bounded(e.flow_phase,1e6)||!bounded(e.distortion,.1)||e.distortion<0||!valid(e.view_sphere_center)||!bounded(e.sphere_radius,1e5)||e.sphere_radius<0)
      throw std::invalid_argument("Invalid surface effect sequence or occlusion sphere");
    if(!bounded(e.volume_depth,.75)||e.volume_depth<0||!bounded(e.volume_density,32)||e.volume_density<=0||
       !bounded(e.volume_seed,1e4)||e.volume_steps<8||e.volume_steps>64||
       (e.volume_depth>0&&(!m.transparent||!m.texture)))
      throw std::invalid_argument("Invalid emission volume depth, density or integration budget");
  }
  if(m.surface_response){const auto& s=*m.surface_response;
    if(!s.properties||!s.normal||!bounded(s.normal_strength,2)||s.normal_strength<0||!bounded(s.relief,.02)||s.relief<0||!bounded(s.cloud_opacity,1)||s.cloud_opacity<0||!bounded(s.cloud_offset.x,2)||!bounded(s.cloud_offset.y,2))
      throw std::invalid_argument("3D surface response requires normal/properties maps and bounded parameters.");
  }
  if(m.shadow){const auto& s=*m.shadow;
    if(!valid(s.position)||!bounded(s.scale,1e5)||s.scale<1e-8f||
       !bounded(s.opacity,1)||s.opacity<0||
       (s.shape!=AnalyticShadowShape3D::Ellipsoid&&s.shape!=AnalyticShadowShape3D::Annulus))
      throw std::invalid_argument("3D shadow requires a bounded transform, opacity and shape.");
    (void)normalized(s.rotation);
    if(s.shape==AnalyticShadowShape3D::Ellipsoid&&
       (!valid(s.radii)||std::min({s.radii.x,s.radii.y,s.radii.z})<.001f||std::max({s.radii.x,s.radii.y,s.radii.z})>1e4f||s.opacity_map))
      throw std::invalid_argument("3D ellipsoid shadow requires positive bounded radii and no opacity map.");
    if(s.shape==AnalyticShadowShape3D::Annulus&&
       (!bounded(s.inner_radius,1e4)||s.inner_radius<.001f||!bounded(s.outer_radius,1e4)||s.outer_radius-s.inner_radius<.001f))
      throw std::invalid_argument("3D annulus shadow requires ordered bounded radii.");
    const double extent=std::hypot(i.position.x-s.position.x,i.position.y-s.position.y,i.position.z-s.position.z)+i.mesh->bounding_radius()*i.scale;
    if(extent/s.scale>1e6)throw std::invalid_argument("3D shadow exceeds its normalized receiver extent.");
  }
  if(m.dielectric){const auto& d=*m.dielectric;
    if(!d.environment||!bounded(d.index_of_refraction,3)||d.index_of_refraction<1||
       !bounded(d.roughness,1)||d.roughness<.04f||!bounded(d.transmission,1)||d.transmission<0||
       !bounded(d.thickness,100)||d.thickness<0||!valid(d.absorption)||
       d.absorption.x<0||d.absorption.y<0||d.absorption.z<0||
       !bounded(d.environment_strength,16)||d.environment_strength<0||
       !bounded(d.specular_strength,16)||d.specular_strength<0||!bounded(d.surface_relief,.1)||d.surface_relief<0)
      throw std::invalid_argument("3D dielectric requires an environment and bounded optical properties.");
  }
}
}
Quaternion rotation_axis_angle(Vec3 axis,float radians){
  axis=normalized(axis);if(!bounded(radians,1e6))throw std::invalid_argument("3D rotation angle must be finite and bounded.");
  const auto s=std::sin(radians*.5f);return {axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5f)};
}
Quaternion compose_rotation(Quaternion a,Quaternion b){
  a=normalized(a);b=normalized(b);
  return normalized(Quaternion{a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
Matrix4 multiply(Matrix4 a,Matrix4 b)noexcept{
  Matrix4 r;for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r.values[c*4+row]+=a.values[k*4+row]*b.values[c*4+k];return r;
}
std::array<float,4> transform(Matrix4 m,std::array<float,4> v)noexcept{
  std::array<float,4> r{};for(int row=0;row<4;++row)for(int c=0;c<4;++c)r[row]+=m.values[c*4+row]*v[c];return r;
}
std::shared_ptr<const Mesh3D> Mesh3D::create(std::vector<Vertex3D> vertices,std::vector<std::uint32_t> indices){
  if(vertices.empty()||indices.empty()||vertices.size()>maximum_mesh3d_vertices||indices.size()>maximum_mesh3d_indices||indices.size()%3)
    throw std::invalid_argument("3D mesh exceeds its budget or has incomplete triangles.");
  float radius=0;
  for(auto& v:vertices){
    if(!valid(v.position)||!bounded(v.uv.x,1)||!bounded(v.uv.y,1)||v.uv.x<0||v.uv.y<0)
      throw std::invalid_argument("3D mesh requires bounded vertices and normalized texture coordinates.");
    v.normal=normalized(v.normal);radius=std::max(radius,std::hypot(v.position.x,v.position.y,v.position.z));
  }
  for(auto i:indices)if(i>=vertices.size())throw std::invalid_argument("3D mesh index is outside its vertex array.");
  return std::shared_ptr<const Mesh3D>(new Mesh3D(std::move(vertices),std::move(indices),radius));
}
std::shared_ptr<const Mesh3D> Mesh3D::uv_sphere(int columns,int rows){
  if(columns<3||rows<2||columns>512||rows>256)throw std::invalid_argument("3D sphere tessellation is out of range.");
  std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
  vertices.reserve(static_cast<std::size_t>(columns+1)*(rows+1));indices.reserve(static_cast<std::size_t>(columns)*rows*6);
  constexpr float pi=std::numbers::pi_v<float>;
  for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x){
    const float lat=pi*.5f-y*pi/rows,lon=-pi+x*2*pi/columns;
    const Vec3 p{std::cos(lat)*std::sin(lon),std::sin(lat),std::cos(lat)*std::cos(lon)};
    vertices.push_back({p,p,{static_cast<float>(x)/columns,static_cast<float>(y)/rows}});
  }
  for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){
    const auto a=static_cast<std::uint32_t>(y*(columns+1)+x),b=a+1,d=a+columns+1,e=d+1;
    if(y>0)for(auto i:{a,d,b})indices.push_back(i);
    if(y<rows-1)for(auto i:{b,d,e})indices.push_back(i);
  }
  return create(std::move(vertices),std::move(indices));
}
std::shared_ptr<const Scene3D> Scene3D::create(Camera3D camera,std::vector<MeshInstance3D> instances,Vec3 light){
  validate_camera(camera);camera.orientation=normalized(camera.orientation);light=normalized(light);
  if(instances.size()>maximum_scene3d_instances)throw std::length_error("3D scene exceeds its instance budget.");
  std::unordered_set<const Mesh3D*> meshes;std::unordered_set<const RgbaImage*> textures;
  std::size_t geometry=0,images=0;
  for(auto& i:instances){
    validate_instance(i);i.rotation=normalized(i.rotation);
    if(i.material.light_direction)i.material.light_direction=normalized(*i.material.light_direction);
    for(auto& l:i.material.additional_lights)l.direction=normalized(l.direction);
    if(meshes.insert(i.mesh.get()).second)geometry+=i.mesh->byte_size()*2;
    if(textures.insert(i.material.texture.get()).second)images+=texture_mip_layout3d(i.material.texture.get()).resident_bytes;
    if(i.material.dielectric)for(const auto& image:{i.material.dielectric->environment,i.material.dielectric->surface})
      if(textures.insert(image.get()).second)images+=texture_mip_layout3d(image.get()).resident_bytes;
    if(i.material.surface_response)for(const auto& image:{i.material.surface_response->normal,i.material.surface_response->properties,i.material.surface_response->cloud_shadow})
      if(textures.insert(image.get()).second)images+=texture_mip_layout3d(image.get()).resident_bytes;
    if(i.material.surface_effect){const auto& image=i.material.surface_effect->next_texture;
      if(textures.insert(image.get()).second)images+=texture_mip_layout3d(image.get()).resident_bytes;}
    if(i.material.shadow&&i.material.shadow->opacity_map){const auto& image=i.material.shadow->opacity_map;
      if(textures.insert(image.get()).second)images+=texture_mip_layout3d(image.get()).resident_bytes;}
  }
  if(geometry>maximum_mesh3d_cache_bytes||images>maximum_scene3d_texture_cache_bytes||meshes.size()>maximum_scene3d_resource_entries||textures.size()>maximum_scene3d_resource_entries)
    throw std::length_error("3D scene exceeds its resident resource budget.");
  return std::shared_ptr<const Scene3D>(new Scene3D(camera,std::move(instances),light));
}
PreparedInstance3D prepare_instance3d(const Camera3D& camera,const MeshInstance3D& instance,float aspect){
  validate_camera(camera);validate_instance(instance);
  if(!bounded(aspect,1e4)||aspect<1e-4f)throw std::invalid_argument("3D camera aspect must be positive and bounded.");
  const Position3 delta{instance.position.x-camera.position.x,instance.position.y-camera.position.y,instance.position.z-camera.position.z};
  const double radius=instance.mesh->bounding_radius()*instance.scale;
  // Reject astronomical offsets in double precision before narrowing.
  const double half_height=camera.projection==Projection3D::Perspective?
      camera.far_plane*std::tan(camera.vertical_fov_radians*.5):camera.orthographic_height*.5;
  if(std::hypot(delta.x,delta.y,delta.z)>std::hypot(static_cast<double>(camera.far_plane),half_height,half_height*aspect)+radius)return {};
  auto model=rotation_matrix(instance.rotation);
  for(int c=0;c<3;++c)for(int row=0;row<3;++row)model.values[c*4+row]*=instance.scale;
  model.values[12]=static_cast<float>(delta.x);model.values[13]=static_cast<float>(delta.y);model.values[14]=static_cast<float>(delta.z);
  const auto q=normalized(camera.orientation);const auto view=rotation_matrix({-q.x,-q.y,-q.z,q.w});
  PreparedInstance3D result;result.model_view=multiply(view,model);
  const float x=result.model_view.values[12],y=result.model_view.values[13],z=-result.model_view.values[14];result.camera_depth=z;
  Matrix4 p;const float n=camera.near_plane,f=camera.far_plane;
  if(camera.projection==Projection3D::Perspective){
    const float ty=std::tan(camera.vertical_fov_radians*.5f),tx=ty*aspect;
    result.visible=z+radius>=n&&z-radius<=f&&std::abs(x)<=z*tx+radius*std::sqrt(1+tx*tx)&&std::abs(y)<=z*ty+radius*std::sqrt(1+ty*ty);
    p.values={1/tx,0,0,0,0,1/ty,0,0,0,0,f/(n-f),-1,0,0,n*f/(n-f),0};
  }else{
    const float hy=camera.orthographic_height*.5f,hx=hy*aspect;
    result.visible=z+radius>=n&&z-radius<=f&&std::abs(x)<=hx+radius&&std::abs(y)<=hy+radius;
    p.values={1/hx,0,0,0,0,1/hy,0,0,0,0,1/(n-f),0,0,0,n/(n-f),1};
  }
  result.model_view_projection=multiply(p,result.model_view);return result;
}
PreparedShadow3D prepare_shadow3d(const Camera3D& camera,const MeshInstance3D& instance,Vec3 light){
  validate_camera(camera);validate_instance(instance);light=normalized(light);
  if(!instance.material.shadow)return {};
  const auto& s=*instance.material.shadow;const auto q=normalized(s.rotation);
  const auto inverse=rotation_matrix({-q.x,-q.y,-q.z,q.w});
  auto model=rotation_matrix(instance.rotation);
  for(int c=0;c<3;++c)for(int r=0;r<3;++r)model.values[c*4+r]*=instance.scale/s.scale;
  // Subtract absolute doubles before scaling or narrowing to GPU floats.
  model.values[12]=static_cast<float>((instance.position.x-s.position.x)/s.scale);
  model.values[13]=static_cast<float>((instance.position.y-s.position.y)/s.scale);
  model.values[14]=static_cast<float>((instance.position.z-s.position.z)/s.scale);
  const auto direction=transform(multiply(inverse,rotation_matrix(camera.orientation)),{light.x,light.y,light.z,0});
  return {multiply(inverse,model),normalized(Vec3{direction[0],direction[1],direction[2]})};
}
} // namespace stellar::native_map
