#include "native_orbital_structure.hpp"

#include "native_scene_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace stellar::native_orbital {
namespace {
using native_map::RgbaImage;
using native_scene::Vec3, native_scene::Material, native_scene::Mesh,
    native_scene::hex, native_scene::deg, native_scene::rasterize;

const Material hull = hex(0x71858d), hull_light = hex(0xaab8bd),
             structure = hex(0x263845), glass = hex(0x164d75),
             solar = hex(0x102b4c), rock = hex(0x6e6254),
             beacon = hex(0x8bdaca, true), warning = hex(0xe99b52, true);

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
std::shared_ptr<const RgbaImage> render(const Mesh &mesh, int size) {
  native_scene::RasterCamera camera;
  camera.eye = {16, 18, 24};
  camera.target = {0, 1.2f, 0};
  camera.half_extent = 9.f;
  return rasterize(mesh, size, camera);
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
  auto rendered = render(build_mesh(project_id, phase), texture_size);
  if (!rendered)
    throw std::runtime_error("Orbital structure rasterization failed.");
  cache_.emplace(std::move(key), rendered);
  return rendered;
}
} // namespace stellar::native_orbital
