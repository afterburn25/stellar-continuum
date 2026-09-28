#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <array>
#include <span>

namespace stellar::native_map {
struct Vec3 { float x{}, y{}, z{}; };
// Three-band Planck approximation, normalized against a reference illuminant.
// This is a rendering color, not a spectroscopic atmosphere calculation.
[[nodiscard]] Vec3 blackbody_light_color(double kelvin,double reference_kelvin=5772.);
// Absolute positions remain double precision; subtraction happens before the
// GPU float conversion. Render state never writes back to simulation state.
struct Position3 { double x{}, y{}, z{}; };
struct Quaternion { float x{}, y{}, z{}, w{1}; };
struct Matrix4 { std::array<float,16> values{}; }; // column major
[[nodiscard]] Quaternion rotation_axis_angle(Vec3 axis,float radians);
[[nodiscard]] Quaternion compose_rotation(Quaternion left,Quaternion right);
// v' = q ⊗ (v,0) ⊗ q* for a unit quaternion q.
[[nodiscard]] inline Vec3 rotate_vec(Quaternion q,Vec3 v) noexcept {
  const float tx=2.f*(q.y*v.z-q.z*v.y),ty=2.f*(q.z*v.x-q.x*v.z),
      tz=2.f*(q.x*v.y-q.y*v.x);
  return {v.x+q.w*tx+q.y*tz-q.z*ty,v.y+q.w*ty+q.z*tx-q.x*tz,
          v.z+q.w*tz+q.x*ty-q.y*tx};
}
[[nodiscard]] Matrix4 multiply(Matrix4 left,Matrix4 right) noexcept;
[[nodiscard]] std::array<float,4> transform(Matrix4 matrix,std::array<float,4> point) noexcept;

struct Vertex3D { Vec3 position,normal; Point uv; };
inline constexpr std::size_t maximum_mesh3d_vertices=262144;
inline constexpr std::size_t maximum_mesh3d_indices=786432;
inline constexpr std::size_t maximum_scene3d_instances=4096;
inline constexpr std::size_t maximum_scene3d_views=8;
inline constexpr std::size_t maximum_scene3d_resource_entries=128;
inline constexpr std::size_t maximum_mesh3d_cache_bytes=64u*1024u*1024u;
inline constexpr std::size_t maximum_scene3d_texture_cache_bytes=192u*1024u*1024u;
inline constexpr std::size_t maximum_scene3d_target_bytes=128u*1024u*1024u;
struct TextureMipLayout3D {
  std::uint32_t levels{};
  std::size_t gpu_bytes{},resident_bytes{};
};
// Full RGBA8 mip chain plus retained CPU base pixels; null is the white 1x1
// fallback. Shared by scene/frame admission and renderer cache accounting.
[[nodiscard]] TextureMipLayout3D texture_mip_layout3d(const RgbaImage* image) noexcept;
class Mesh3D final {
 public:
  // Validates once; immutable resources can be prepared on workers and shared.
  [[nodiscard]] static std::shared_ptr<const Mesh3D> create(
      std::vector<Vertex3D> vertices,std::vector<std::uint32_t> indices);
  [[nodiscard]] static std::shared_ptr<const Mesh3D> uv_sphere(int columns=128,int rows=64);
  [[nodiscard]] const auto& vertices()const noexcept{return vertices_;}
  [[nodiscard]] const auto& indices()const noexcept{return indices_;}
  [[nodiscard]] float bounding_radius()const noexcept{return radius_;}
  // Local-space axis-aligned bounds — collision and ground resting use
  // these (scaled by instance scale) instead of the bounding sphere so
  // boxes collide as boxes.
  [[nodiscard]] Vec3 bounds_min()const noexcept{return bounds_min_;}
  [[nodiscard]] Vec3 bounds_max()const noexcept{return bounds_max_;}
  [[nodiscard]] std::size_t byte_size()const noexcept{return vertices_.size()*sizeof(Vertex3D)+indices_.size()*sizeof(std::uint32_t);}
 private:
  Mesh3D(std::vector<Vertex3D> vertices,std::vector<std::uint32_t> indices,float radius,Vec3 bounds_min,Vec3 bounds_max)
      :vertices_(std::move(vertices)),indices_(std::move(indices)),radius_(radius),bounds_min_(bounds_min),bounds_max_(bounds_max){}
  std::vector<Vertex3D> vertices_;std::vector<std::uint32_t> indices_;float radius_{};
  Vec3 bounds_min_{},bounds_max_{};
};
enum class Projection3D { Perspective,Orthographic };
struct Camera3D {
  Position3 position{0,0,3};
  Quaternion orientation; // local +X right, +Y up, -Z forward
  Projection3D projection{Projection3D::Perspective};
  float vertical_fov_radians{1.04719755f},orthographic_height{2.5f}; // height >= 1e-6
  float near_plane{.01f},far_plane{1000.f}; // 1e-6 <= near < far <= 1e7
};
// Environment-mapped dielectric optics. Environment is a linear-radiance
// equirectangular map fixed in world orientation; it never contains UI pixels.
struct Dielectric3D {
  std::shared_ptr<const RgbaImage> environment;
  // Optional RGB multipliers: roughness, transmission, optical thickness;
  // alpha is surface height when surface_relief is nonzero.
  std::shared_ptr<const RgbaImage> surface;
  float index_of_refraction{1.31f},roughness{.35f},transmission{.5f},thickness{1.f};
  Vec3 absorption{.35f,.12f,.05f}; // Beer-Lambert coefficients, per optical unit
  float environment_strength{1.f},specular_strength{1.f};
  float surface_relief{}; // height as a fraction of the instance scale
};
// Optional opaque surface response. Packed properties are roughness, liquid,
// ice, height. Maps are immutable and count against the shared texture budget.
struct SurfaceResponse3D {
  std::shared_ptr<const RgbaImage> normal,properties,cloud_shadow;
  float normal_strength{.35f},relief{},cloud_opacity{};
  Point cloud_offset{};
};
enum class AnalyticShadowShape3D { Ellipsoid,Annulus };
// One analytic blocker of the directional light, in world coordinates. Scale
// normalizes the intersection math independently of camera zoom or distance.
// Annuli lie in local XZ; alpha U is normalized radius, V is azimuth (as in
// annulus_mesh). A null map is fully opaque. Ambient/emission remain unshadowed.
struct AnalyticShadow3D {
  AnalyticShadowShape3D shape{AnalyticShadowShape3D::Ellipsoid};
  Position3 position;Quaternion rotation;float scale{1};
  Vec3 radii{1,1,1}; // ellipsoid semi-axes, in units of scale
  float inner_radius{1.2f},outer_radius{2.4f},opacity{1};
  std::shared_ptr<const RgbaImage> opacity_map;
};
// Reusable emissive image sequence on surface-attached geometry. A camera-space
// sphere masks fragments behind an existing photosphere, including its limb.
struct SurfaceEffect3D {
  std::shared_ptr<const RgbaImage> next_texture;
  float blend{},flow_phase{},distortion{};
  Vec3 view_sphere_center;float sphere_radius{};
  // Optional image-shaped emission volume. The closed proxy spans local
  // [-.5,.5] x [-.22,.78] x [-volume_depth,volume_depth]; the camera is outside.
  // Zero retains surface rendering. Integration uses local optical distance,
  // so changing zoom/instance scale does not change the plasma density.
  float volume_depth{},volume_density{5.f},volume_seed{};
  int volume_steps{32}; // 8..64 bounded front-to-back emission/absorption samples
};
struct DirectionalLight3D { Vec3 direction{0,0,1},color{1,1,1};float intensity{}; };
struct Material3D {
  std::shared_ptr<const RgbaImage> texture;
  Color tint{255,255,255,255};
  float ambient{.12f},diffuse{.88f},opacity{1.f};
  // Generic emissive overlay mask: alpha *= clamp(1 - N.L * strength,0,1).
  float dark_side_strength{};
  bool transparent{},double_sided{};
  // Optional camera-space incident light for spatially separated objects in a
  // shared depth-tested view. Unset materials inherit the scene's direction.
  std::optional<Vec3> light_direction;
  std::optional<Dielectric3D> dielectric;
  std::optional<SurfaceResponse3D> surface_response;
  Vec3 light_color{1,1,1};float light_intensity{1};
  // Zero uses ordinary alpha; positive values render a grazing-angle shell.
  float rim_power{};
  std::optional<AnalyticShadow3D> shadow;
  // Thin particulate sheets can receive diffuse light from either normal side.
  bool two_sided_diffuse{};
  // Decode authored sRGB color before illumination; encode the final output.
  bool linear_light{};
  std::optional<SurfaceEffect3D> surface_effect;
  // At most three stellar sources, all evaluated against the same blocker.
  std::array<DirectionalLight3D,2> additional_lights{};
  // Preserve detail across unequal projected texture axes (e.g. tilted rings).
  // Still uses mipmaps; bounded 8x anisotropy instead of a sharpening bias.
  bool anisotropic_texture{};
  // Source-limited Catmull-Rom reconstruction during magnification only.
  // Minification keeps the usual mip/anisotropic anti-aliasing path.
  bool cubic_magnification{};
};
struct MeshInstance3D {
  std::shared_ptr<const Mesh3D> mesh;
  Position3 position;
  Quaternion rotation;
  float scale{1.f}; // 1e-8..1e5 uniform scale keeps normals well-defined
  Material3D material;
};
class Scene3D final {
 public:
  [[nodiscard]] static std::shared_ptr<const Scene3D> create(
      Camera3D camera,std::vector<MeshInstance3D> instances,Vec3 light_direction={.42f,.2f,.87f});
  [[nodiscard]] const auto& camera()const noexcept{return camera_;}
  [[nodiscard]] const auto& instances()const noexcept{return instances_;}
  [[nodiscard]] Vec3 light_direction()const noexcept{return light_;} // camera space
 private:
  Scene3D(Camera3D camera,std::vector<MeshInstance3D> instances,Vec3 light)
      :camera_(camera),instances_(std::move(instances)),light_(light){}
  Camera3D camera_;std::vector<MeshInstance3D> instances_;Vec3 light_;
};
struct PreparedInstance3D { Matrix4 model_view,model_view_projection;float camera_depth{};bool visible{}; };
// Conservative sphere/frustum test, camera-relative matrices; no GPU required.
[[nodiscard]] PreparedInstance3D prepare_instance3d(const Camera3D&,const MeshInstance3D&,float aspect);
struct PreparedShadow3D { Matrix4 from_model;Vec3 light; };
// Light is the resolved camera-space direction. Local geometry stays precise
// even when both blocker and receiver are at astronomical world coordinates.
[[nodiscard]] PreparedShadow3D prepare_shadow3d(const Camera3D&,const MeshInstance3D&,Vec3 light);
struct Scene3DStatistics {
  std::uint64_t mesh_uploads{},texture_uploads{},draw_calls{},culled_instances{};
  std::size_t mesh_cache_entries{},mesh_cache_bytes{},texture_cache_entries{},texture_cache_bytes{},target_bytes{};
};
} // namespace stellar::native_map
