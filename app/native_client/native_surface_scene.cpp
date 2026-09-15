#include "native_surface_scene.hpp"

#include "native_scene_raster.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::native_surface {
namespace {
using native_map::RgbaImage;
using native_scene::Vec3, native_scene::Material, native_scene::Mesh,
    native_scene::hex, native_scene::deg;

// SurfaceBuildingVisuals material palette.
const Material shell = hex(0x35444d), metal = hex(0x18242b),
             bronze = hex(0x9a7040), solar = hex(0x071d34),
             glass = hex(0x071823), light = hex(0x70bed0, true),
             amber = hex(0xe39a42, true), concrete = hex(0x596064),
             offline = hex(0xc84c3f, true), scaffold = hex(0xbd954c),
             offline_ring = hex(0x6e2a20);

void generator(Mesh &m) {
  m.cylinder({0, 5, 0}, 3.1f, 3.8f, 7.f, shell);
  for (int i = 0; i < 3; ++i)
    m.cylinder({0, 3.4f + static_cast<float>(i) * 2.f, 0}, 3.35f, 3.35f, .38f,
               light);
  for (const int side : {-1, 1}) {
    const auto s = static_cast<float>(side);
    m.box({s * 7.f, 3.f, 0}, {1.f, 3.6f, 1.f}, bronze);
    m.box({s * 7.f, 5.f, 0}, {6.2f, .25f, 10.f}, solar, {0, 0, s * -12.6f});
  }
}

void lab(Mesh &m) {
  m.cylinder({0, 3, 0}, 8, 9, 3.5f, shell);
  m.ellipsoid({0, 4.2f, 0}, {7.4f, 7.4f * .75f, 7.4f}, glass);
  m.cylinder({0, 4.6f, 0}, 8, 8, .3f, light);
  for (const int side : {-1, 1}) {
    const auto s = static_cast<float>(side);
    m.box({s * 9.f, 3.1f, 0}, {5.f, 3.4f, 8.f}, shell);
    m.box({s * 11.55f, 3.8f, 0}, {.15f, 1.4f, 5.f}, glass);
  }
  m.cylinder({5, 10, 5}, .2f, .35f, 6.f, metal, {}, 12);
  m.ellipsoid({5, 13, 5}, {2.2f, 2.2f * .25f, 2.2f}, shell, {20, 0, 17});
}

void fabricator(Mesh &m) {
  m.box({0, 4.5f, 0}, {20.f, 6.f, 15.f}, shell);
  m.box({0, 7.85f, 0}, {21.f, .7f, 16.f}, metal);
  m.box({0, 3.8f, 7.65f}, {9.f, 4.5f, .25f}, glass);
  for (int i = -2; i <= 2; ++i)
    m.box({static_cast<float>(i) * 1.8f, 3.8f, 7.85f}, {.3f, 4.5f, .4f}, bronze);
  for (const int side : {-1, 1}) {
    const auto s = static_cast<float>(side);
    m.box({s * 12.f, 7.2f, 0}, {.8f, 13.f, .8f}, bronze);
    m.cylinder({s * 5.f, 10.f, -4.f}, 1.7f, 1.7f, 4.f, metal);
  }
  m.box({0, 13.6f, 0}, {25.f, 1.f, 1.8f}, bronze);
  m.box({2.f, 12.7f, 0}, {3.f, 1.f, 2.3f}, metal);
  m.box({2.f, 10.7f, 0}, {.15f, 3.f, .15f}, light);
}

void trade_hub(Mesh &m) {
  m.cylinder({0, 1.7f, 0}, 9, 10, 2.2f, shell, {}, 12);
  for (int level = 0; level < 3; ++level) {
    const auto radius = 7.2f - static_cast<float>(level) * 1.25f;
    m.cylinder({0, 4.2f + static_cast<float>(level) * 2.35f, 0}, radius,
               radius + .45f, 2.4f, level == 1 ? glass : metal, {}, 12);
  }
  for (int side = 0; side < 4; ++side) {
    const auto angle = static_cast<float>(side) * 6.283185307179586f / 4.f;
    m.box({std::cos(angle) * 10.5f, 3.2f, std::sin(angle) * 10.5f},
          {5.5f, 3.8f, 3.2f}, glass, {0, -angle / deg, 0});
  }
  m.cylinder({0, 12, 0}, .35f, .5f, 7.f, bronze, {}, 10);
  m.ellipsoid({0, 16, 0}, {1.25f, 1.25f, 1.25f}, light);
}

void habitat(Mesh &m) {
  m.cylinder({0, 1.3f, 0}, 10, 11, 1.8f, metal, {}, 20);
  for (int index = 0; index < 3; ++index) {
    const auto angle = static_cast<float>(index) * 6.283185307179586f / 3.f;
    m.ellipsoid({std::cos(angle) * 6.5f, 4.1f, std::sin(angle) * 6.5f},
                {5.4f, 5.4f * .62f, 5.4f}, index == 0 ? glass : shell);
  }
  m.cylinder({0, 7, 0}, 2.2f, 2.8f, 8.f, bronze, {}, 12);
  m.ellipsoid({0, 11.5f, 0}, {1.1f, 1.1f, 1.1f}, light);
}

void battery(Mesh &m) {
  for (int bank = -1; bank <= 1; ++bank) {
    const auto x = static_cast<float>(bank) * 5.8f;
    m.box({x, 4.3f, 0}, {5.2f, 5.8f, 3.4f}, shell);
    for (int level = 0; level < 4; ++level)
      m.box({x, 2.0f + static_cast<float>(level) * 1.55f, 0},
            {4.5f, .18f, 3.48f}, light);
  }
  m.cylinder({0, 6.1f, 0}, 1.1f, 1.35f, 8.5f, bronze, {}, 12);
}

void cargo_terminal(Mesh &m) {
  m.box({0, 1.2f, 0}, {22.f, 1.2f, 15.f}, metal);
  for (int row = -1; row <= 1; ++row)
    for (int column = -2; column <= 2; ++column)
      m.box({static_cast<float>(column) * 3.7f, 2.9f,
             static_cast<float>(row) * 3.3f},
            {3.2f, 2.2f, 2.6f}, (row + column) % 2 == 0 ? bronze : shell);
  for (const int side : {-1, 1}) {
    const auto s = static_cast<float>(side);
    m.box({s * 10.5f, 7.f, -6.f}, {.8f, 11.f, .8f}, bronze);
    m.box({s * 6.5f, 12.f, -6.f}, {9.f, .7f, .8f}, bronze);
    m.box({s * 2.5f, 8.5f, -6.f}, {.5f, 7.f, .5f}, metal);
  }
  m.box({0, 4.1f, 7.f}, {7.f, 4.5f, 5.f}, glass);
  m.ellipsoid({0, 8.2f, 7.f}, {.9f, .9f, .9f}, light);
}

void generic(Mesh &m, float radius) {
  m.cylinder({0, 4, 0}, radius * .5f, radius * .62f, 6.f, shell, {}, 8);
  m.box({0, 8.4f, 0}, {radius * .7f, 1.2f, radius * .7f}, metal);
  m.ellipsoid({0, 9.6f, 0}, {.8f, .8f, .8f}, light);
}

// Reference build stages: pad + scaffold posts while materials arrive, the
// structure with scaffold while industry completes, then beacon on completion.
void scaffolding(Mesh &m, float radius, int phase) {
  for (const auto x : {-radius * .72f, radius * .72f})
    for (const auto z : {-radius * .72f, radius * .72f})
      m.box({x, 6.5f, z}, {.24f, 13.f, .24f}, scaffold);
  for (int level = 1; level <= 3; ++level)
    for (const int side : {-1, 1}) {
      const auto s = static_cast<float>(side);
      m.box({0, static_cast<float>(level) * 4.f, s * radius * .72f},
            {radius * 1.44f, .2f, .2f}, scaffold);
      m.box({s * radius * .72f, static_cast<float>(level) * 4.f, 0},
            {.2f, .2f, radius * 1.44f}, scaffold);
    }
  if (phase >= 2) {
    m.box({0, 13.6f, 0}, {radius * 1.45f, .12f, .3f}, light);
    m.box({0, 13.6f, 0}, {.28f, .28f, radius * 1.18f}, amber);
  }
}

Mesh building_mesh(std::string_view type_id, int phase, bool powered,
                   bool prioritized) {
  Mesh m;
  const auto advanced = type_id.starts_with("advanced_");
  const auto base = advanced ? type_id.substr(9) : type_id;
  const auto radius = (base == "fabricator" || base == "controlled_agriculture" ||
                       base == "cargo_terminal")
                          ? 17.f
                      : (base == "science_lab" || base == "trade_hub" ||
                         base == "habitat_complex" || base == "water_reclamation")
                          ? 15.f
                          : 12.f;
  m.cylinder({0, .7f, 0}, radius * .85f, radius * .91f, 1.4f, metal, {}, 8);
  if (phase >= 2) {
    if (base == "power_generator") generator(m);
    else if (base == "science_lab") lab(m);
    else if (base == "fabricator" || base == "water_reclamation") fabricator(m);
    else if (base == "trade_hub") trade_hub(m);
    else if (base == "habitat_complex" || base == "controlled_agriculture")
      habitat(m);
    else if (base == "grid_battery") battery(m);
    else if (base == "cargo_terminal") cargo_terminal(m);
    else generic(m, radius);
    if (advanced) {
      m.cylinder({0, 12.4f, 0}, radius * .52f, radius * .52f, .3f, light, {},
                 24);
      for (int index = 0; index < 4; ++index) {
        const auto angle =
            static_cast<float>(index) * 6.283185307179586f / 4.f;
        m.ellipsoid({std::cos(angle) * radius * .62f, 10.6f,
                     std::sin(angle) * radius * .62f},
                    {.7f, .7f, .7f}, amber);
      }
    }
    m.ellipsoid({0, 13, 0}, {.6f, .6f, .6f}, powered ? light : offline);
  }
  if (phase < 3) scaffolding(m, radius, phase);
  if (prioritized)
    m.cylinder({0, 14.2f, 0}, radius * .55f, radius * .55f, .25f, amber, {}, 24);
  if (!powered)
    m.cylinder({0, 1.55f, 0}, radius * .94f, radius * .94f, .2f, offline_ring,
               {}, 24);
  return m;
}

Mesh hub_mesh(int level, bool capital, bool outpost) {
  Mesh m;
  m.cylinder({0, .6f, 0}, 19, 20, 1.2f, metal, {}, 8);
  m.cylinder({0, 3.7f, 0}, 12, 14, 5.f, shell, {}, 8);
  m.cylinder({0, 6.7f, 0}, 8, 11, 1.f, metal, {}, 8);
  m.cylinder({0, 8.7f, 0}, 5, 7, 3.f, glass, {}, 8);
  m.cylinder({0, 10.4f, 0}, 5.8f, 5.8f, .35f, light, {}, 8);
  m.cylinder({0, 15.7f, 0}, .35f, .65f, 11.f, shell, {}, 12);
  m.box({0, 19.f, 0}, {7.f, .4f, .4f}, metal);
  m.ellipsoid({0, 22.f, 0}, {1.5f, 1.5f, 1.5f}, light);
  for (int step = 0; step < 3; ++step)
    m.box({0, .18f + static_cast<float>(step) * .26f,
           19.8f + static_cast<float>(step) * 1.7f},
          {7.5f + static_cast<float>(step) * 1.7f, .34f, 2.4f}, concrete);
  m.box({0, 3.f, 14.15f}, {5.2f, 3.4f, .32f}, glass);
  for (int i = 0; i < 4; ++i) {
    const auto angle = static_cast<float>(i) * 1.57079632679f;
    m.box({std::sin(angle) * 16.f, 1.35f, std::cos(angle) * 16.f},
          {4.5f, .22f, 8.f}, bronze, {0, angle / deg, 0});
  }
  if (level >= 2) {
    m.cylinder({0, 1.25f, 0}, 22.5f, 22.5f, .22f, bronze, {}, 48);
    for (int i = 0; i < 4; ++i) {
      const auto angle = static_cast<float>(i) * 1.57079632679f + .78539816f;
      const auto x = std::sin(angle) * 14.5f, z = std::cos(angle) * 14.5f;
      m.cylinder({x, 5.3f, z}, 2.2f, 2.8f, 8.5f, shell, {}, 10);
      m.cylinder({x, 9.9f, z}, 1.1f, 1.35f, .7f, metal, {}, 12);
    }
  }
  if (level >= 3) {
    m.cylinder({0, 12.2f, 0}, 8.8f, 8.8f, .45f, capital ? bronze : metal, {},
               32);
    for (int i = 0; i < 8; ++i) {
      const auto angle = static_cast<float>(i) * 6.283185307179586f / 8.f;
      m.ellipsoid({std::sin(angle) * 8.2f, 12.7f, std::cos(angle) * 8.2f},
                  {.42f, .42f, .42f}, light);
    }
    m.cylinder({3.4f, 16.2f, 0}, .12f, .18f, 6.f, metal, {}, 8);
    m.ellipsoid({3.4f, 19.3f, 0}, {1.25f * 1.7f, 1.25f * .3f, 1.25f * 1.7f},
                glass);
  }
  if (outpost)
    for (int i = 0; i < 4; ++i) {
      const auto angle = static_cast<float>(i) * 1.57079632679f;
      m.ellipsoid({std::sin(angle) * 20.f, 1.8f, std::cos(angle) * 20.f},
                  {.38f, .38f, .38f}, hex(0xd77d32, true));
    }
  return m;
}

native_scene::RasterCamera surface_camera(float half_extent, bool powered) {
  native_scene::RasterCamera camera;
  camera.eye = {26, 30, 26};
  camera.target = {0, 4.f, 0};
  camera.half_extent = half_extent;
  if (!powered) camera.brightness = .45f;
  return camera;
}
} // namespace

int NativeSurfaceSceneRenderer::phase_for_progress(const double progress) noexcept {
  return std::clamp(static_cast<int>(std::ceil(progress * 3.)), 1, 3);
}

std::shared_ptr<const RgbaImage>
NativeSurfaceSceneRenderer::image(std::string_view type_id, int phase,
                                  bool powered, bool prioritized) {
  phase = std::clamp(phase, 1, 3);
  std::string key(type_id);
  key += ':';
  key += static_cast<char>('0' + phase);
  key += powered ? 'p' : 'o';
  if (prioritized) key += '!';
  if (const auto found = cache_.find(key); found != cache_.end())
    return found->second;
  if (cache_.size() >= maximum_cached_images) cache_.erase(cache_.begin());
  const auto advanced = type_id.starts_with("advanced_");
  const auto base = advanced ? type_id.substr(9) : type_id;
  const auto footprint = (base == "fabricator" ||
                          base == "controlled_agriculture" ||
                          base == "cargo_terminal")
                             ? 17.f
                         : (base == "science_lab" || base == "trade_hub" ||
                            base == "habitat_complex" ||
                            base == "water_reclamation")
                             ? 15.f
                             : 12.f;
  auto rendered = rasterize(building_mesh(type_id, phase, powered, prioritized),
                            texture_size,
                            surface_camera(footprint * 1.35f, powered));
  if (!rendered)
    throw std::runtime_error("Surface building rasterization failed.");
  cache_.emplace(std::move(key), rendered);
  return rendered;
}

std::shared_ptr<const RgbaImage>
NativeSurfaceSceneRenderer::hub_image(int level, bool capital, bool outpost) {
  level = std::clamp(level, 1, 3);
  std::string key = "hub:" + std::to_string(level) + (capital ? "c" : "") +
                    (outpost ? "o" : "");
  if (const auto found = cache_.find(key); found != cache_.end())
    return found->second;
  if (cache_.size() >= maximum_cached_images) cache_.erase(cache_.begin());
  auto rendered = rasterize(hub_mesh(level, capital, outpost), texture_size,
                            surface_camera(30.f, true));
  if (!rendered) throw std::runtime_error("Surface hub rasterization failed.");
  cache_.emplace(std::move(key), rendered);
  return rendered;
}
} // namespace stellar::native_surface
