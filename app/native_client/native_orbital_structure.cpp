#include "native_orbital_structure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace stellar::native_orbital {
namespace {
using native_map::RgbaImage;

struct Vec3 {
  float x{}, y{}, z{};
  Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
Vec3 cross(const Vec3 &a, const Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalized(const Vec3 &v) {
  const auto length = std::sqrt(dot(v, v));
  return length > 0.f ? v * (1.f / length) : Vec3{};
}
constexpr float deg = 3.14159265358979323846f / 180.f;
Vec3 euler(const Vec3 &v, const Vec3 &degrees_xyz) {
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
Material hex(std::uint32_t value, bool emissive = false) {
  return {static_cast<float>((value >> 16) & 0xffu) / 255.f,
          static_cast<float>((value >> 8) & 0xffu) / 255.f,
          static_cast<float>(value & 0xffu) / 255.f, emissive};
}
const Material hull = hex(0x71858d), hull_light = hex(0xaab8bd),
             structure = hex(0x263845), glass = hex(0x164d75),
             solar = hex(0x102b4c), rock = hex(0x6e6254),
             beacon = hex(0x8bdaca, true), warning = hex(0xe99b52, true);

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
    for (int i = 0; i < segments; ++i) f.push_back({0, vertex(1, i + 1), vertex(1, i)});
    for (int ring = 1; ring < rings - 1; ++ring)
      for (int i = 0; i < segments; ++i) {
        f.push_back({vertex(ring, i), vertex(ring, i + 1), vertex(ring + 1, i + 1)});
        f.push_back({vertex(ring, i), vertex(ring + 1, i + 1), vertex(ring + 1, i)});
      }
    for (int i = 0; i < segments; ++i)
      f.push_back({vertex(rings - 1, i), vertex(rings - 1, i + 1), bottom});
    emit(at, rotation_degrees, radii, v, f, material);
  }
};

void solar_wing(Mesh &m, const Vec3 &at, int direction, float length,
                float width) {
  m.box(at, {length, .10f, width}, solar);
  for (int i = 1; i < 4; ++i)
    m.box(at + Vec3{0, .08f, -width / 2 + static_cast<float>(i) * width / 4},
          {length, .04f, .05f}, hull_light);
  m.box(at + Vec3{static_cast<float>(direction) * length / 2, -.15f, 0},
        {.14f, .42f, .22f}, structure);
}
void panel_band(Mesh &m, const Vec3 &start, int count, float spacing,
                const Material &material) {
  for (int i = 0; i < count; ++i)
    m.box(start + Vec3{static_cast<float>(i) * spacing, 0, 0},
          {.76f, .32f, .10f}, material);
}
void docking_gantries(Mesh &m, int side) {
  const auto x = static_cast<float>(side) * 5.6f;
  m.box({x, .62f, 0}, {1.0f, .28f, 11.5f}, hull);
  for (int z = -5; z <= 5; z += 2) {
    const auto zf = static_cast<float>(z);
    m.box({x, 1.75f, zf}, {.22f, 2.05f, .22f}, hull_light);
    m.box({x - side * .52f, 1.82f, zf + .62f}, {.14f, 1.75f, .14f}, structure,
          {0, 0, static_cast<float>(-side) * 28.f});
    m.box({x + side * .52f, 1.82f, zf - .62f}, {.14f, 1.75f, .14f}, structure,
          {0, 0, static_cast<float>(side) * 28.f});
    m.box({x, 2.78f, zf}, {1.25f, .15f, .28f}, structure);
  }
  m.box({x, .96f, -5.35f}, {.32f, .18f, .32f}, beacon);
  m.box({x, .96f, 5.35f}, {.32f, .18f, .32f}, beacon);
}
void docking_cradle(Mesh &m, const Vec3 &at, int side) {
  m.box(at, {1.25f, .18f, 2.4f}, structure);
  m.box(at + Vec3{static_cast<float>(side) * .48f, .38f, -.76f},
        {.14f, .78f, .14f}, hull_light);
  m.box(at + Vec3{static_cast<float>(side) * .48f, .38f, .76f},
        {.14f, .78f, .14f}, hull_light);
  m.box(at + Vec3{0, .52f, 0}, {1.02f, .12f, .22f}, beacon);
}
void shipyard(Mesh &m, int phase) {
  m.box({0, 0, 0}, {15, .72f, 1.5f}, hull);
  m.box({0, .58f, 0}, {11.8f, .35f, 2.55f}, structure);
  m.box({0, 1.05f, 0}, {5.6f, 1.05f, 2.1f}, hull);
  m.cylinder({0, 1.7f, 0}, .82f, .82f, 2.2f, structure);
  m.box({0, 2.72f, 0}, {2.0f, .28f, 2.35f}, glass);
  panel_band(m, {-4.6f, 1.0f, 1.37f}, 6, 1.35f, hull_light);
  if (phase >= 2)
    for (int side = -1; side <= 1; side += 2) docking_gantries(m, side);
  if (phase >= 3)
    for (int side = -1; side <= 1; side += 2) {
      solar_wing(m, {static_cast<float>(side) * 5.25f, .85f, -4.15f}, side,
                 4.0f, 3.2f);
      for (int bay = -1; bay <= 1; ++bay)
        docking_cradle(m,
                       {static_cast<float>(side) *
                                (3.6f + static_cast<float>(bay + 1) * 1.4f),
                            1.12f, 3.1f},
                       side);
    }
  if (phase == 4) {
    for (int bay = -2; bay <= 2; ++bay) {
      const auto x = static_cast<float>(bay) * 2.65f;
      m.box({x, 1.95f, .05f}, {1.65f, .42f, 3.1f}, hull);
      m.box({x, 2.19f, -1.48f}, {1.24f, .16f, .10f}, warning);
      m.box({x, 2.19f, 1.48f}, {1.24f, .16f, .10f}, beacon);
    }
    for (int x = -6; x <= 6; x += 3) {
      const auto xf = static_cast<float>(x);
      m.box({xf, 3.15f, 0}, {.13f, 2.1f, .13f}, hull_light);
      m.box({xf, 4.12f, 0}, {.55f, .10f, .55f}, beacon);
    }
  }
}
void launch_complex(Mesh &m, int phase) {
  m.cylinder({0, 0, 0}, 2.35f, 2.7f, 1.15f, hull);
  m.cylinder({0, .88f, 0}, 1.65f, 2.15f, 1.05f, structure);
  m.cylinder({0, 1.66f, 0}, 1.22f, 1.55f, .72f, glass);
  for (int arm = 0; arm < 4; ++arm) {
    const auto angle = static_cast<float>(arm * 90 + 45) * deg;
    const Vec3 p{std::cos(angle) * 3.5f, .28f, std::sin(angle) * 3.5f};
    m.box(p, {3.8f, .26f, .72f}, hull,
          {0, -angle / deg, 0});
  }
  if (phase >= 2)
    for (int side = -1; side <= 1; side += 2) {
      const auto s = static_cast<float>(side);
      m.box({s * 5.3f, .75f, 0}, {4.8f, .22f, 1.15f}, structure);
      m.box({s * 7.3f, 1.4f, 0}, {.24f, 1.55f, .24f}, hull_light);
      m.box({s * 7.3f, 2.25f, 0}, {.70f, .12f, .70f}, beacon);
    }
  if (phase >= 3)
    for (int side = -1; side <= 1; side += 2) {
      const auto s = static_cast<float>(side);
      solar_wing(m, {s * 3.9f, .35f, -3.1f}, side, 3.2f, 2.25f);
      m.box({s * 3.35f, 1.4f, 2.25f}, {.22f, 1.9f, 3.15f}, hull_light);
      m.box({s * 4.25f, 1.4f, 2.25f}, {.22f, 1.9f, 3.15f}, hull_light);
      m.box({s * 3.8f, 2.2f, 2.25f}, {1.2f, .18f, 3.4f}, structure);
    }
  if (phase == 4) {
    m.cylinder({0, 3.0f, 0}, .15f, .25f, 2.9f, hull_light);
    m.box({0, 4.48f, 0}, {1.5f, .13f, 1.5f}, beacon);
    for (int i = 0; i < 6; ++i) {
      const auto a = static_cast<float>(i * 60) * deg;
      m.box({std::cos(a) * 2.65f, 1.9f, std::sin(a) * 2.65f},
            {.35f, .12f, .35f}, warning);
    }
  }
}
void asteroid_network(Mesh &m, int phase) {
  m.ellipsoid({-2.1f, 0, 0}, {3.45f * 1.16f, 3.0f * .74f, 3.45f * 1.08f}, rock,
              {17, 22, 31});
  m.box({3.0f, .1f, 0}, {8.4f, .65f, 1.65f}, hull);
  m.box({3.6f, .68f, 0}, {5.4f, .34f, 2.5f}, structure);
  for (int x = 1; x <= 6; x += 2)
    m.box({static_cast<float>(x), .95f, 1.35f}, {1.15f, .22f, .14f},
          hull_light);
  if (phase >= 2) {
    m.box({4.3f, 1.65f, 0}, {2.6f, 1.7f, 2.25f}, hull);
    m.cylinder({5.85f, 0, -.92f}, .56f, .56f, 2.8f, structure, {90, 0, 0});
    m.cylinder({5.85f, 0, .92f}, .56f, .56f, 2.8f, structure, {90, 0, 0});
    m.box({-.1f, .45f, 0}, {3.25f, .24f, .24f}, hull_light, {0, 0, -18});
  }
  if (phase >= 3)
    for (int side = -1; side <= 1; side += 2) {
      const auto s = static_cast<float>(side);
      solar_wing(m, {4.65f, .7f, s * 3.0f}, 1, 3.2f, 1.8f);
      m.box({-1.0f, .7f, s * 1.75f}, {2.1f, .26f, .35f}, structure,
            {0, 0, s * 27.f});
      m.box({-1.9f, .18f, s * 2.22f}, {.92f, .62f, .92f}, hull_light);
    }
  if (phase == 4) {
    for (int i = 0; i < 4; ++i) {
      const auto x = 2.15f + static_cast<float>(i) * 1.5f;
      m.box({x, 2.45f, -.86f}, {1.08f, 1.08f, .84f}, hull_light);
      m.box({x, 2.45f, .86f}, {1.08f, 1.08f, .84f}, hull_light);
      m.box({x, 3.05f, 0}, {.74f, .14f, 2.3f}, structure);
    }
    m.cylinder({-3.3f, .2f, 0}, .14f, .34f, 2.5f, warning, {0, 0, 72});
  }
}
void outpost(Mesh &m, int phase) {
  m.box({0, 0, 0}, {12, .8f, 1.8f}, hull);
  m.cylinder({0, 1.3f, 0}, 1.25f, 1.7f, 2.4f, structure);
  if (phase >= 2) {
    solar_wing(m, {-3.5f, .6f, -2.4f}, -1, 3, 2);
    solar_wing(m, {3.5f, .6f, -2.4f}, 1, 3, 2);
  }
  if (phase >= 3) panel_band(m, {-3.5f, .6f, 1.0f}, 5, 1.45f, hull_light);
  if (phase == 4) m.box({0, 3.15f, 0}, {.36f, 2.6f, .36f}, beacon);
}

Mesh build_mesh(std::string_view project_id, int phase) {
  Mesh m;
  if (project_id == "orbital_shipyard")
    shipyard(m, phase);
  else if (project_id == "orbital_launch_complex")
    launch_complex(m, phase);
  else if (project_id == "asteroid_resource_network")
    asteroid_network(m, phase);
  else
    outpost(m, phase);
  return m;
}

// Orthographic camera and lighting matching the reference inspector viewport.
std::shared_ptr<const RgbaImage> rasterize(const Mesh &mesh, int size) {
  const Vec3 eye{16, 18, 24}, target{0, 1.2f, 0}, world_up{0, 1, 0};
  const Vec3 forward = normalized(target - eye);
  const Vec3 right = normalized(cross(forward, world_up));
  const Vec3 up = cross(right, forward);
  constexpr float half_extent = 9.f;
  const Vec3 light_dir = normalized(euler({0, 0, -1}, {-40, -28, 0}));
  const Vec3 ambient{0.459f * .65f, 0.580f * .65f, 0.702f * .65f};
  const Vec3 light_color{1.f * 2.2f, 0.941f * 2.2f, 0.847f * 2.2f};

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
      const Vec3 v = source[static_cast<std::size_t>(i)] - eye;
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
        std::clamp(mat.r * (ambient.x + light_color.x * ndotl) +
                       (mat.emissive ? mat.r * 1.3f : 0.f),
                   0.f, 1.f),
        std::clamp(mat.g * (ambient.y + light_color.y * ndotl) +
                       (mat.emissive ? mat.g * 1.3f : 0.f),
                   0.f, 1.f),
        std::clamp(mat.b * (ambient.z + light_color.z * ndotl) +
                       (mat.emissive ? mat.b * 1.3f : 0.f),
                   0.f, 1.f)};
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
  return RgbaImage::create(size, size, std::move(pixels));
}
} // namespace

int NativeOrbitalStructureRenderer::phase_for_progress(
    const double progress) noexcept {
  return std::clamp(static_cast<int>(std::ceil(progress * 4)), 1, 4);
}

std::shared_ptr<const RgbaImage>
NativeOrbitalStructureRenderer::image(std::string_view project_id, int phase) {
  phase = std::clamp(phase, 1, 4);
  std::string key(project_id);
  key += ':';
  key += static_cast<char>('0' + phase);
  if (const auto found = cache_.find(key); found != cache_.end())
    return found->second;
  if (cache_.size() >= maximum_cached_images)
    cache_.erase(cache_.begin());
  auto rendered = rasterize(build_mesh(project_id, phase), texture_size);
  if (!rendered)
    throw std::runtime_error("Orbital structure rasterization failed.");
  cache_.emplace(std::move(key), rendered);
  return rendered;
}
} // namespace stellar::native_orbital
