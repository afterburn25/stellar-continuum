#pragma once
#include <stellar/engine/native_scene3d.hpp>
#include <stellar/engine/physics3d.hpp>
#include <functional>
#include <cmath>
#include <numbers>

namespace stellar::native_map {
// Orthonormal frame: local +Z follows forward, +Y follows the projected up.
// Useful for synchronous bodies, cameras and other direction-attached geometry.
inline Quaternion rotation_frame(Vec3 forward,Vec3 up){
  const auto unit=[](Vec3 v){const float n=std::hypot(v.x,v.y,v.z);if(!std::isfinite(n)||n<1e-8f)throw std::invalid_argument("Degenerate rotation frame");return Vec3{v.x/n,v.y/n,v.z/n};};
  const auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
  const auto z=unit(forward),x=unit(cross(up,z)),y=cross(z,x);Quaternion q;
  const float trace=x.x+y.y+z.z;
  if(trace>0){const float s=std::sqrt(trace+1)*2;q={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s*.25f};}
  else if(x.x>y.y&&x.x>z.z){const float s=std::sqrt(1+x.x-y.y-z.z)*2;q={s*.25f,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};}
  else if(y.y>z.z){const float s=std::sqrt(1+y.y-x.x-z.z)*2;q={(y.x+x.y)/s,s*.25f,(z.y+y.z)/s,(z.x-x.z)/s};}
  else{const float s=std::sqrt(1+z.z-x.x-y.y)*2;q={(z.x+x.z)/s,(z.y+y.z)/s,s*.25f,(x.y-y.x)/s};}
  return q;
}
// Flat XZ annulus, +Y normal, with radial U and angular V coordinates.
// Immutable geometry works with the existing depth-tested, double-sided material.
inline std::shared_ptr<const Mesh3D> annulus_mesh(float inner,float outer,int segments=256,float thickness=0){
  if(!std::isfinite(inner)||!std::isfinite(outer)||inner<=0||outer<=inner||segments<8||segments>4096||!std::isfinite(thickness)||thickness<0||thickness>outer)
    throw std::invalid_argument("Invalid annulus geometry");
  std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
  vertices.reserve(static_cast<std::size_t>(segments+1)*2);indices.reserve(static_cast<std::size_t>(segments)*6);
  for(int i=0;i<=segments;++i){
    const float angle=i*2.f*std::numbers::pi_v<float>/segments;
    for(int edge=0;edge<2;++edge){const float r=edge?outer:inner;
      vertices.push_back({{r*std::cos(angle),thickness*.5f,r*std::sin(angle)},{0,1,0},{static_cast<float>(edge),static_cast<float>(i)/segments}});}
    if(i<segments){const auto j=static_cast<std::uint32_t>(i*2);indices.insert(indices.end(),{j,j+2,j+1,j+1,j+2,j+3});}
  }
  if(thickness>0){
    // A closed annular slab. Cull the back faces so translucent materials do
    // not blend both parallel faces into an artificially opaque ring.
    const auto top=static_cast<std::uint32_t>(vertices.size());const auto count=indices.size();
    for(std::uint32_t i=0;i<top;++i){auto v=vertices[i];v.position.y=-thickness*.5f;v.normal.y=-1;vertices.push_back(v);}
    for(std::size_t i=0;i<count;i+=3)indices.insert(indices.end(),{indices[i]+top,indices[i+2]+top,indices[i+1]+top});
    for(int edge=0;edge<2;++edge){const float r=edge?outer:inner,sign=edge?1.f:-1.f;const auto start=static_cast<std::uint32_t>(vertices.size());
      for(int i=0;i<=segments;++i){const float angle=i*2.f*std::numbers::pi_v<float>/segments,c=std::cos(angle),s=std::sin(angle);
        for(float y:{-thickness*.5f,thickness*.5f})vertices.push_back({{r*c,y,r*s},{sign*c,0,sign*s},{static_cast<float>(edge),static_cast<float>(i)/segments}});
        if(i<segments){const auto j=start+static_cast<std::uint32_t>(i*2);if(edge)indices.insert(indices.end(),{j,j+1,j+2,j+1,j+3,j+2});else indices.insert(indices.end(),{j,j+2,j+1,j+1,j+2,j+3});}
      }
    }
  }
  return Mesh3D::create(std::move(vertices),std::move(indices));
}

// Convex counter-clockwise XY profile, extruded along Z. Flat face normals
// keep hard hull edges; top UVs address a nose-right unit-square texture.
inline std::shared_ptr<const Mesh3D> extruded_convex_mesh(
    std::span<const Point> profile, float thickness) {
  if(profile.size()<3||profile.size()>256||!std::isfinite(thickness)||thickness<=0)
    throw std::invalid_argument("Invalid extrusion");
  for(std::size_t i=0;i<profile.size();++i){
    const auto a=profile[i],b=profile[(i+1)%profile.size()],c=profile[(i+2)%profile.size()];
    if(!std::isfinite(a.x)||!std::isfinite(a.y)||(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x)<=0)
      throw std::invalid_argument("Extrusion requires a strictly convex CCW profile");
  }
  std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
  for(const auto sign:{1.f,-1.f}){
    const auto start=static_cast<std::uint32_t>(vertices.size());
    for(auto p:profile)vertices.push_back({{p.x,p.y,sign*thickness*.5f},{0,0,sign},{p.x+.5f,.5f-p.y}});
    for(std::uint32_t i=1;i+1<profile.size();++i)
      if(sign>0)indices.insert(indices.end(),{start,start+i,start+i+1});
      else indices.insert(indices.end(),{start,start+i+1,start+i});
  }
  for(std::size_t i=0;i<profile.size();++i){
    auto a=profile[i],b=profile[(i+1)%profile.size()];const auto n=std::hypot(b.x-a.x,b.y-a.y);
    const Vec3 normal{(b.y-a.y)/n,(a.x-b.x)/n,0};
    const auto start=static_cast<std::uint32_t>(vertices.size());
    for(auto p:{a,b})for(auto z:{-thickness*.5f,thickness*.5f})vertices.push_back({{p.x,p.y,z},normal,{p.x+.5f,.5f-p.y}});
    indices.insert(indices.end(),{start,start+2,start+1,start+1,start+2,start+3});
  }
  return Mesh3D::create(std::move(vertices),std::move(indices));
}

// Regular XY heightfield with +Z elevation. The caller owns the authoritative
// height rule; rendering and collision consume the same immutable triangles.
inline std::shared_ptr<const Mesh3D> heightfield_mesh(int cells,float half_extent,
    const std::function<float(float,float)>& height) {
  if(cells<1||cells>256||!std::isfinite(half_extent)||half_extent<=0||!height)
    throw std::invalid_argument("Invalid heightfield");
  const auto step=2*half_extent/static_cast<float>(cells);
  std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
  for(int y=0;y<=cells;++y)for(int x=0;x<=cells;++x){
    const auto px=-half_extent+static_cast<float>(x)*step,py=-half_extent+static_cast<float>(y)*step;
    const auto dx=(height(px+step*.5f,py)-height(px-step*.5f,py))/step;
    const auto dy=(height(px,py+step*.5f)-height(px,py-step*.5f))/step;
    const auto n=std::sqrt(1+dx*dx+dy*dy);
    vertices.push_back({{px,py,height(px,py)},{-dx/n,-dy/n,1/n},
      {static_cast<float>(x)/static_cast<float>(cells),1.f-static_cast<float>(y)/static_cast<float>(cells)}});
  }
  for(int y=0;y<cells;++y)for(int x=0;x<cells;++x){
    const auto a=static_cast<std::uint32_t>(y*(cells+1)+x),b=a+1,c=a+static_cast<std::uint32_t>(cells+1),d=c+1;
    indices.insert(indices.end(),{a,b,c,b,d,c});
  }
  return Mesh3D::create(std::move(vertices),std::move(indices));
}

// Radial terrain in unit-sphere coordinates. Elevation is a fraction of the
// body's radius. A shared immutable mesh serves both rendering and picking.
inline std::shared_ptr<const Mesh3D> radial_terrain_mesh(
    const std::function<float(Vec3)>& elevation,int columns=128,int rows=64) {
  if(!elevation)throw std::invalid_argument("Missing radial elevation");
  const auto sphere=Mesh3D::uv_sphere(columns,rows);
  auto vertices=sphere->vertices();
  const auto point=[&](Vec3 n){
    const float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    n={n.x/length,n.y/length,n.z/length};const auto h=elevation(n);
    if(!std::isfinite(h)||h<-.25f||h>.25f)throw std::invalid_argument("Radial elevation outside supported range");
    return Vec3{n.x*(1+h),n.y*(1+h),n.z*(1+h)};
  };
  for(auto& v:vertices){
    const auto n=v.normal;
    // Tangents chosen away from the pole avoid a singular normal derivative.
    const Vec3 reference=std::abs(n.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0};
    const auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    auto t=cross(reference,n);const float len=std::sqrt(t.x*t.x+t.y*t.y+t.z*t.z);t={t.x/len,t.y/len,t.z/len};
    const auto b=cross(n,t);constexpr float d=.001f;
    const auto derivative=[&](Vec3 axis){const auto a=point({n.x+d*axis.x,n.y+d*axis.y,n.z+d*axis.z}),b=point({n.x-d*axis.x,n.y-d*axis.y,n.z-d*axis.z});return Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    auto normal=cross(derivative(t),derivative(b));const float size=std::sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
    v.position=point(n);v.normal={normal.x/size,normal.y/size,normal.z/size};
  }
  return Mesh3D::create(std::move(vertices),sphere->indices());
}

struct MeshSegmentHit3D { double fraction{};std::size_t triangle{};stellar::engine::CollisionVector3 position; };
inline std::optional<MeshSegmentHit3D> intersect_mesh_segment(
    const Mesh3D& mesh,stellar::engine::CollisionVector3 from,stellar::engine::CollisionVector3 to) {
  using namespace stellar::engine;
  if(!segment_sphere(from,to,{},mesh.bounding_radius()))return {};
  std::optional<MeshSegmentHit3D> closest;
  const auto& indices=mesh.indices();const auto& vertices=mesh.vertices();
  const auto vertex=[&](std::uint32_t i){const auto p=vertices[i].position;return CollisionVector3{p.x,p.y,p.z};};
  for(std::size_t i=0;i<indices.size();i+=3){
    const auto t=segment_triangle(from,to,vertex(indices[i]),vertex(indices[i+1]),vertex(indices[i+2]));
    if(t&&(!closest||*t<closest->fraction))closest=MeshSegmentHit3D{*t,i/3,
      {from.x+(to.x-from.x)* *t,from.y+(to.y-from.y)* *t,from.z+(to.z-from.z)* *t}};
  }
  return closest;
}
} // namespace stellar::native_map
