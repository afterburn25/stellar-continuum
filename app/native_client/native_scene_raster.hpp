#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace stellar::native_scene {

// Shared CPU mesh rasterizer for bounded presentation sprites: the same
// primitive grammar the reference client builds as Godot meshes, rasterized
// into small textures so the 2D map pipeline can present staged structures
// and surface buildings without an unbounded 3D subsystem.
struct Vec3 {
  float x{}, y{}, z{};
  Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3 &a, const Vec3 &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 normalized(const Vec3 &v) {
  const auto length = std::sqrt(dot(v, v));
  return length > 0.f ? v * (1.f / length) : Vec3{};
}
inline constexpr float deg = 3.14159265358979323846f / 180.f;
inline Vec3 euler(const Vec3 &v, const Vec3 &degrees_xyz) {
  // Godot's YXZ intrinsic order: roll, then pitch, then yaw.
  const auto rz = degrees_xyz.z * deg, rx = degrees_xyz.x * deg,
             ry = degrees_xyz.y * deg;
  Vec3 out{v.x * std::cos(rz) - v.y * std::sin(rz),
           v.x * std::sin(rz) + v.y * std::cos(rz), v.z};
  out = {out.x, out.y * std::cos(rx) - out.z * std::sin(rx),
         out.y * std::sin(rx) + out.z * std::cos(rx)};
  return {out.x * std::cos(ry) + out.z * std::sin(ry), out.y,
          -out.x * std::sin(ry) + out.z * std::cos(ry)};
}

struct Material {
  float r{}, g{}, b{};
  bool emissive{};
};
inline Material hex(std::uint32_t value, bool emissive = false) {
  return {static_cast<float>((value >> 16) & 0xffu) / 255.f,
          static_cast<float>((value >> 8) & 0xffu) / 255.f,
          static_cast<float>(value & 0xffu) / 255.f, emissive};
}

struct Triangle {
  Vec3 a, b, c;
  Material material;
};

struct Mesh {
  std::vector<Triangle> triangles;

  void emit(const Vec3 &at, const Vec3 &rotation_degrees, const Vec3 &scale,
            const std::vector<Vec3> &vertices,
            const std::vector<std::array<int, 3>> &faces,
            const Material &material) {
    for (const auto &face : faces) {
      auto place = [&](int index) {
        const Vec3 v{vertices[static_cast<std::size_t>(index)].x * scale.x,
                     vertices[static_cast<std::size_t>(index)].y * scale.y,
                     vertices[static_cast<std::size_t>(index)].z * scale.z};
        return euler(v, rotation_degrees) + at;
      };
      triangles.push_back({place(face[0]), place(face[1]), place(face[2]),
                           material});
    }
  }
  void box(const Vec3 &at, const Vec3 &size, const Material &material,
           const Vec3 &rotation_degrees = {}) {
    static const std::vector<Vec3> v{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    static const std::vector<std::array<int, 3>> f{
        {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
        {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {0, 4, 7}, {0, 7, 3}};
    emit(at, rotation_degrees, size * .5f, v, f, material);
  }
  void cylinder(const Vec3 &at, float top_radius, float bottom_radius,
                float height, const Material &material,
                const Vec3 &rotation_degrees = {}, int segments = 20) {
    std::vector<Vec3> v;
    v.reserve(static_cast<std::size_t>(segments) * 2 + 2);
    for (int i = 0; i < segments; ++i) {
      const auto a = 2.f * 3.14159265358979323846f * static_cast<float>(i) /
                     static_cast<float>(segments);
      v.push_back({std::cos(a) * bottom_radius, -height * .5f,
                   std::sin(a) * bottom_radius});
      v.push_back({std::cos(a) * top_radius, height * .5f,
                   std::sin(a) * top_radius});
    }
    const int bottom_center = static_cast<int>(v.size()),
              top_center = bottom_center + 1;
    v.push_back({0, -height * .5f, 0});
    v.push_back({0, height * .5f, 0});
    std::vector<std::array<int, 3>> f;
    for (int i = 0; i < segments; ++i) {
      const int n = (i + 1) % segments;
      f.push_back({i * 2, n * 2, n * 2 + 1});
      f.push_back({i * 2, n * 2 + 1, i * 2 + 1});
      f.push_back({bottom_center, i * 2, n * 2});
      f.push_back({top_center, n * 2 + 1, i * 2 + 1});
    }
    emit(at, rotation_degrees, {1, 1, 1}, v, f, material);
  }
  void ellipsoid(const Vec3 &at, const Vec3 &radii, const Material &material,
                 const Vec3 &rotation_degrees = {}, int segments = 16,
                 int rings = 10) {
    std::vector<Vec3> v;
    v.push_back({0, -1, 0});
    for (int ring = 1; ring < rings; ++ring) {
      const auto phi = 3.14159265358979323846f * static_cast<float>(ring) /
                       static_cast<float>(rings);
      for (int i = 0; i < segments; ++i) {
        const auto theta = 2.f * 3.14159265358979323846f *
                           static_cast<float>(i) / static_cast<float>(segments);
        v.push_back({std::cos(theta) * std::sin(phi), -std::cos(phi),
                     std::sin(theta) * std::sin(phi)});
      }
    }
    const int bottom = static_cast<int>(v.size());
    v.push_back({0, 1, 0});
    auto vertex = [&](int ring, int i) {
      return 1 + (ring - 1) * segments + ((i % segments) + segments) % segments;
    };
    std::vector<std::array<int, 3>> f;
    for (int i = 0; i < segments; ++i)
      f.push_back({0, vertex(1, i + 1), vertex(1, i)});
    for (int ring = 1; ring < rings - 1; ++ring)
      for (int i = 0; i < segments; ++i) {
        f.push_back({vertex(ring, i), vertex(ring, i + 1),
                     vertex(ring + 1, i + 1)});
        f.push_back({vertex(ring, i), vertex(ring + 1, i + 1),
                     vertex(ring + 1, i)});
      }
    for (int i = 0; i < segments; ++i)
      f.push_back({vertex(rings - 1, i), vertex(rings - 1, i + 1), bottom});
    emit(at, rotation_degrees, radii, v, f, material);
  }
};

struct RasterCamera {
  Vec3 eye, target, world_up{0, 1, 0};
  float half_extent{9.f};
  // Lighting matching the reference inspector viewport.
  Vec3 light_direction{normalized(euler({0, 0, -1}, {-40, -28, 0}))};
  Vec3 ambient{0.459f * .65f, 0.580f * .65f, 0.702f * .65f};
  Vec3 light_color{2.2f, 0.941f * 2.2f, 0.847f * 2.2f};
  float brightness{1.f};
};

// Orthographic painter's rasterizer: back-face culled, depth-buffered,
// ambient + single-directional Lambert shading with emissive lift.
inline std::shared_ptr<const native_map::RgbaImage>
rasterize(const Mesh &mesh, int size, const RasterCamera &camera = {}) {
  const Vec3 forward = normalized(camera.target - camera.eye);
  const Vec3 right = normalized(cross(forward, camera.world_up));
  const Vec3 up = cross(right, forward);
  const auto half_extent = camera.half_extent;
  const Vec3 light_dir = camera.light_direction;

  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
  std::vector<float> depth(static_cast<std::size_t>(size) * size,
                           std::numeric_limits<float>::infinity());
  for (const auto &tri : mesh.triangles) {
    const Vec3 normal = normalized(cross(tri.b - tri.a, tri.c - tri.a));
    if (dot(normal, forward) >= 0.f) continue;
    std::array<Vec3, 3> p;
    const std::array<Vec3, 3> source{tri.a, tri.b, tri.c};
    bool clipped = false;
    for (int i = 0; i < 3; ++i) {
      const Vec3 v = source[static_cast<std::size_t>(i)] - camera.eye;
      const auto cx = dot(v, right), cy = dot(v, up);
      p[static_cast<std::size_t>(i)] = {
          (cx / half_extent + 1.f) * .5f * static_cast<float>(size),
          (1.f - (cy / half_extent + 1.f) * .5f) * static_cast<float>(size),
          dot(v, forward)};
      if (p[static_cast<std::size_t>(i)].z <= 0.f) clipped = true;
    }
    if (clipped) continue;
    const auto min_x = std::max(0, static_cast<int>(std::floor(
                                       std::min({p[0].x, p[1].x, p[2].x})))),
               min_y = std::max(0, static_cast<int>(std::floor(
                                       std::min({p[0].y, p[1].y, p[2].y})))),
               max_x = std::min(
                   size - 1,
                   static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x})))),
               max_y = std::min(
                   size - 1,
                   static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
    const auto edge = [](const Vec3 &a, const Vec3 &b, float x, float y) {
      return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    const auto area = edge(p[0], p[1], p[2].x, p[2].y);
    if (std::abs(area) < 1e-6f) continue;
    const auto ndotl = std::max(0.f, dot(normal, light_dir) * -1.f);
    const auto &mat = tri.material;
    const Vec3 shaded{
        std::clamp(mat.r * (camera.ambient.x + camera.light_color.x * ndotl) +
                       (mat.emissive ? mat.r * 1.3f : 0.f),
                   0.f, 1.f) * camera.brightness,
        std::clamp(mat.g * (camera.ambient.y + camera.light_color.y * ndotl) +
                       (mat.emissive ? mat.g * 1.3f : 0.f),
                   0.f, 1.f) * camera.brightness,
        std::clamp(mat.b * (camera.ambient.z + camera.light_color.z * ndotl) +
                       (mat.emissive ? mat.b * 1.3f : 0.f),
                   0.f, 1.f) * camera.brightness};
    for (int y = min_y; y <= max_y; ++y)
      for (int x = min_x; x <= max_x; ++x) {
        const auto px = static_cast<float>(x) + .5f,
                   py = static_cast<float>(y) + .5f;
        const auto w0 = edge(p[1], p[2], px, py) / area,
                   w1 = edge(p[2], p[0], px, py) / area,
                   w2 = edge(p[0], p[1], px, py) / area;
        if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
        const auto z = w0 * p[0].z + w1 * p[1].z + w2 * p[2].z;
        const auto texel = static_cast<std::size_t>(y) * size + x;
        if (z >= depth[texel]) continue;
        depth[texel] = z;
        const auto offset = texel * 4;
        pixels[offset] = static_cast<std::uint8_t>(shaded.x * 255.f);
        pixels[offset + 1] = static_cast<std::uint8_t>(shaded.y * 255.f);
        pixels[offset + 2] = static_cast<std::uint8_t>(shaded.z * 255.f);
        pixels[offset + 3] = 255;
      }
  }
  return native_map::RgbaImage::create(size, size, std::move(pixels));
}

} // namespace stellar::native_scene
