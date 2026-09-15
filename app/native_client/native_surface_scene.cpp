#include "native_surface_scene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
#include <stellar/core/surface_construction.hpp>
#include <stellar/core/surface_economy.hpp>
#include <string_view>

namespace stellar::native_colony_ui {
namespace {
using namespace stellar::native_map;
using stellar::native_colony::NativeSurfaceSite;
constexpr std::size_t max_triangles = 8192, max_road_segments = 192;
constexpr std::size_t max_sites = 128, max_type_id_bytes = 256;
constexpr float road_half_width = 2.7f, clearance = 4.5f;
enum class Layer : std::size_t {
  RoadEdge,
  Roads,
  RoadMarking,
  Ground,
  Structure,
  Roof,
  Detail,
  Selection,
  Status,
  Count
};
struct ColorOrder {
  bool operator()(Color a, Color b) const noexcept {
    return std::array{a.r, a.g, a.b, a.a} < std::array{b.r, b.g, b.b, b.a};
  }
};
struct Batch {
  std::vector<Point> vertices;
  std::vector<int> indices;
};
using Layers = std::array<std::map<Color, Batch, ColorOrder>,
                          static_cast<std::size_t>(Layer::Count)>;
constexpr Color road{31, 39, 44, 235}, apron{76, 84, 87, 235},
    road_edge{88, 98, 101, 235}, road_marking{145, 135, 99, 225},
    shadow{18, 25, 29, 145}, metal{105, 118, 123, 250},
    shell{139, 151, 153, 250}, roof{69, 80, 85, 250}, glass{54, 116, 134, 245},
    bronze{177, 119, 55, 250}, solar{45, 83, 116, 250},
    light{125, 222, 218, 255}, warning{224, 169, 71, 255},
    selection{250, 221, 111, 255};

bool finite(Point p) noexcept {
  return std::isfinite(p.x) && std::isfinite(p.y);
}
bool valid(const NativeSurfaceSite &s) noexcept {
  return std::isfinite(s.x) && std::isfinite(s.z) &&
         std::isfinite(s.rotation_degrees);
}
float distance(Point a, Point b) noexcept {
  return std::hypot(a.x - b.x, a.y - b.y);
}
bool intersects(UiRect a, UiRect b) noexcept {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height &&
         a.y + a.height > b.y;
}
Point rotated(Point p, float a) noexcept {
  return {p.x * std::cos(a) - p.y * std::sin(a),
          p.x * std::sin(a) + p.y * std::cos(a)};
}
Point local(Point c, Point p, float a) noexcept {
  auto q = rotated(p, a);
  return {c.x + q.x, c.y + q.y};
}
float angle(const NativeSurfaceSite &s) noexcept {
  return std::remainder(s.rotation_degrees, 360.f) * std::numbers::pi_v<float> /
         180.f;
}
void triangle(Layers &l, Layer z, Color color, Point a, Point b, Point c,
              std::size_t &n) {
  if (n >= max_triangles || !finite(a) || !finite(b) || !finite(c))
    return;
  auto &x = l[static_cast<std::size_t>(z)][color];
  if (x.vertices.size() > 65530)
    return;
  int base = static_cast<int>(x.vertices.size());
  x.vertices.insert(x.vertices.end(), {a, b, c});
  x.indices.insert(x.indices.end(), {base, base + 1, base + 2});
  ++n;
}
void quad(Layers &l, Layer z, Color color, Point a, Point b, Point c, Point d,
          std::size_t &n) {
  triangle(l, z, color, a, b, c, n);
  triangle(l, z, color, a, c, d, n);
}
void rect(Layers &l, Layer z, Color color, Point c, float x, float y, float a,
          std::size_t &n) {
  if (!(x > 0 && y > 0 && std::isfinite(x) && std::isfinite(y)))
    return;
  quad(l, z, color, local(c, {-x, -y}, a), local(c, {x, -y}, a),
       local(c, {x, y}, a), local(c, {-x, y}, a), n);
}
void polygon(Layers &l, Layer z, Color color, Point c, float r, int sides,
             float a, std::size_t &n) {
  if (!(r > 0 && std::isfinite(r)) || sides < 3)
    return;
  for (int i = 0; i < sides; ++i) {
    float p = a + 2 * std::numbers::pi_v<float> * i / sides,
          q = a + 2 * std::numbers::pi_v<float> * (i + 1) / sides;
    triangle(l, z, color, c, {c.x + std::cos(p) * r, c.y + std::sin(p) * r},
             {c.x + std::cos(q) * r, c.y + std::sin(q) * r}, n);
  }
}
void ring(Layers &l, Layer z, Color color, Point c, float inner, float outer,
          int sides, float a, std::size_t &n) {
  if (!(inner > 0 && outer > inner))
    return;
  for (int i = 0; i < sides; ++i) {
    float p = a + 2 * std::numbers::pi_v<float> * i / sides,
          q = a + 2 * std::numbers::pi_v<float> * (i + 1) / sides;
    quad(l, z, color, {c.x + std::cos(p) * inner, c.y + std::sin(p) * inner},
         {c.x + std::cos(p) * outer, c.y + std::sin(p) * outer},
         {c.x + std::cos(q) * outer, c.y + std::sin(q) * outer},
         {c.x + std::cos(q) * inner, c.y + std::sin(q) * inner}, n);
  }
}
void segment(Layers &l, Layer z, Color color, Point a, Point b, float half,
             std::size_t &n) {
  float d = distance(a, b);
  if (!(d > .001f) || !finite(a) || !finite(b) || !(half > 0))
    return;
  Point s{-(b.y - a.y) / d * half, (b.x - a.x) / d * half};
  quad(l, z, color, {a.x - s.x, a.y - s.y}, {b.x - s.x, b.y - s.y},
       {b.x + s.x, b.y + s.y}, {a.x + s.x, a.y + s.y}, n);
}
float site_radius(const NativeSurfaceSite &s) noexcept {
  if (auto *d = stellar::core::find_surface_building(s.type_id))
    return d->footprint_radius;
  return 15.f;
}
bool segment_avoids(Point a, Point b, Point center, float radius) {
  float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy,
        t = l2 > .0001f
                ? std::clamp(((center.x - a.x) * dx + (center.y - a.y) * dy) /
                                 l2,
                             0.f, 1.f)
                : 0;
  constexpr float geometric_epsilon = 1e-3f;
  return distance({a.x + dx * t, a.y + dy * t}, center) + geometric_epsilon >=
         radius;
}
bool clear(Point a, Point b, const std::vector<NativeSurfaceSite> &sites,
           int target) {
  if (!segment_avoids(a, b, {0, 0}, stellar::core::surface_hub_radius))
    return false;
  for (auto &s : sites)
    if (s.building_id != target && valid(s) &&
        !segment_avoids(a, b, {s.x, s.z}, site_radius(s) + clearance))
      return false;
  return true;
}
std::vector<Point> routed(Point start, Point end,
                          const std::vector<NativeSurfaceSite> &sites,
                          int target) {
  if (clear(start, end, sites, target))
    return {start, end};
  float d = distance(start, end);
  if (d < 1)
    return {};
  Point normal{-(end.y - start.y) / d, (end.x - start.x) / d};
  for (float offset : {48.f, -48.f, 84.f, -84.f, 120.f, -120.f}) {
    Point mid{(start.x + end.x) / 2 + normal.x * offset,
              (start.y + end.y) / 2 + normal.y * offset};
    if (clear(start, mid, sites, target) && clear(mid, end, sites, target))
      return {start, mid, end};
  }
  return {};
}
Color status(const NativeSurfaceSite &s) {
  if (!s.complete)
    return warning;
  if (!s.enabled || !s.powered)
    return {188, 74, 83, 255};
  if (!s.staffed)
    return {85, 159, 179, 255};
  return light;
}

void building(Layers &l, const NativeSurfaceSite &s, Point c, float scale,
              bool dense, std::size_t &n) {
  float r = site_radius(s) * scale, a = angle(s);
  auto f = stellar::core::surface_functional_family(s.type_id);
  polygon(l, Layer::Ground, shadow, {c.x + r * .1f, c.y + r * .12f}, r * .88f,
          dense ? 6 : 12, a, n);
  polygon(l, Layer::Ground, apron, c, r, dense ? 6 : 12, a, n);
  if (dense) {
    polygon(l, Layer::Structure, s.complete ? shell : warning, c, r * .62f, 6,
            a, n);
    polygon(l, Layer::Roof, s.complete ? roof : metal, c, r * .34f, 6, a + .2f,
            n);
    return;
  }
  if (!s.complete) {
    for (Point p : std::array<Point, 4>{{{-r * .52f, -r * .52f},
                                         {r * .52f, -r * .52f},
                                         {r * .52f, r * .52f},
                                         {-r * .52f, r * .52f}}})
      polygon(l, Layer::Structure, metal, local(c, p, a), r * .1f, 6, a, n);
    float progress =
        std::isfinite(s.progress_fraction)
            ? static_cast<float>(std::clamp(s.progress_fraction, 0., 1.))
            : 0;
    rect(l, Layer::Structure, shell, local(c, {0, r * .25f}, a), r * .62f,
         std::max(r * .05f, r * .34f * progress), a, n);
    segment(l, Layer::Detail, warning, local(c, {-r * .64f, -r * .66f}, a),
            local(c, {r * .64f, -r * .66f}, a), std::max(.7f, r * .025f), n);
    return;
  }
  if (f == "power_generator") {
    polygon(l, Layer::Structure, metal, c, r * .25f, 12, a, n);
    polygon(l, Layer::Roof, light, c, r * .17f, 12, a, n);
    for (float x : {-.57f, .57f}) {
      rect(l, Layer::Structure, solar, local(c, {x * r, 0}, a), r * .23f,
           r * .58f, a, n);
      for (float y : {-.38f, 0.f, .38f})
        segment(l, Layer::Detail, shell, local(c, {x * r - r * .22f, y * r}, a),
                local(c, {x * r + r * .22f, y * r}, a),
                std::max(.5f, r * .012f), n);
    }
  } else if (f == "science_lab") {
    polygon(l, Layer::Structure, shell, c, r * .48f, 16, a, n);
    polygon(l, Layer::Roof, glass, c, r * .36f, 16, a, n);
    for (float x : {-.62f, .62f})
      rect(l, Layer::Structure, shell, local(c, {x * r, 0}, a), r * .18f,
           r * .36f, a, n);
    polygon(l, Layer::Detail, bronze, local(c, {r * .32f, r * .34f}, a),
            r * .1f, 10, a, n);
    segment(l, Layer::Detail, bronze, local(c, {r * .32f, r * .34f}, a),
            local(c, {r * .48f, r * .5f}, a), std::max(.6f, r * .018f), n);
  } else if (f == "fabricator") {
    rect(l, Layer::Structure, shell, c, r * .62f, r * .43f, a, n);
    rect(l, Layer::Roof, roof, c, r * .65f, r * .3f, a, n);
    for (float x : {-.38f, .38f})
      polygon(l, Layer::Detail, metal, local(c, {x * r, -r * .18f}, a),
              r * .11f, 10, a, n);
    for (float x : {-.45f, -.22f, 0.f, .22f, .45f})
      segment(l, Layer::Detail, bronze, local(c, {x * r, r * .4f}, a),
              local(c, {x * r, r * .58f}, a), std::max(.5f, r * .012f), n);
  } else if (f == "habitat_complex") {
    for (int i = 0; i < 3; ++i) {
      float q = a + i * 2 * std::numbers::pi_v<float> / 3;
      Point p{c.x + std::cos(q) * r * .38f, c.y + std::sin(q) * r * .38f};
      polygon(l, Layer::Structure, i ? shell : glass, p, r * .29f, 14, a, n);
      polygon(l, Layer::Roof, roof, p, r * .12f, 10, a, n);
    }
    polygon(l, Layer::Detail, bronze, c, r * .13f, 10, a, n);
  } else if (f == "controlled_agriculture") {
    for (float x : {-.48f, -.16f, .16f, .48f}) {
      rect(l, Layer::Structure, glass, local(c, {x * r, 0}, a), r * .11f,
           r * .66f, a, n);
      segment(l, Layer::Detail, shell, local(c, {x * r, -r * .62f}, a),
              local(c, {x * r, r * .62f}, a), std::max(.55f, r * .014f), n);
    }
  } else if (f == "water_reclamation") {
    for (Point p : std::array<Point, 3>{
             {{-r * .34f, -r * .18f}, {r * .34f, -r * .18f}, {0, r * .36f}}}) {
      auto q = local(c, p, a);
      polygon(l, Layer::Structure, metal, q, r * .25f, 14, a, n);
      polygon(l, Layer::Roof, glass, q, r * .18f, 14, a, n);
    }
  } else if (f == "grid_battery") {
    for (float x : {-.42f, 0.f, .42f}) {
      rect(l, Layer::Structure, shell, local(c, {x * r, 0}, a), r * .17f,
           r * .48f, a, n);
      for (float y : {-.3f, -.1f, .1f, .3f})
        segment(l, Layer::Detail, light, local(c, {(x - .14f) * r, y * r}, a),
                local(c, {(x + .14f) * r, y * r}, a), std::max(.45f, r * .012f),
                n);
    }
  } else if (f == "cargo_terminal") {
    rect(l, Layer::Ground, metal, c, r * .72f, r * .48f, a, n);
    for (int y = -1; y <= 1; ++y)
      for (int x = -2; x <= 2; ++x)
        rect(l, Layer::Structure, (x + y) % 2 ? bronze : shell,
             local(c, {x * r * .24f, y * r * .23f}, a), r * .095f, r * .085f, a,
             n);
  } else if (f == "trade_hub") {
    polygon(l, Layer::Structure, shell, c, r * .47f, 12, a, n);
    polygon(l, Layer::Roof, glass, c, r * .32f, 12, a, n);
    for (int i = 0; i < 4; ++i) {
      float q = a + i * std::numbers::pi_v<float> / 2;
      rect(l, Layer::Structure, glass,
           {c.x + std::cos(q) * r * .63f, c.y + std::sin(q) * r * .63f},
           r * .16f, r * .11f, q, n);
    }
    polygon(l, Layer::Detail, light, c, r * .08f, 10, a, n);
  } else {
    polygon(l, Layer::Structure, shell, c, r * .62f, 8, a, n);
    polygon(l, Layer::Roof, roof, c, r * .38f, 8, a + .2f, n);
  }
}
template <class T> void bytes(std::vector<std::byte> &v, const T &x) {
  auto *p = reinterpret_cast<const std::byte *>(&x);
  v.insert(v.end(), p, p + sizeof x);
}
} // namespace

float NativeSurfaceScene::footprint_radius(
    const NativeSurfaceSite &s) noexcept {
  return site_radius(s);
}
bool NativeSurfaceScene::contains_site(const NativeSurfaceSite &s, Point p,
                                       const SurfaceViewport &v,
                                       UiRect terrain) noexcept {
  if (!valid(s) || !finite(p) || !std::isfinite(v.pixels_per_unit) ||
      v.pixels_per_unit <= 0)
    return false;
  auto c = v.world_to_screen(s.x, s.z, terrain);
  float r = site_radius(s) * static_cast<float>(v.pixels_per_unit);
  return finite(c) && std::isfinite(r) && distance(c, p) <= r;
}
void NativeSurfaceScene::refresh_roads(
    const std::vector<NativeSurfaceSite> &sites) const {
  std::vector<std::byte> key;
  for (auto &s : sites) {
    bytes(key, s.building_id);
    bytes(key, s.x);
    bytes(key, s.z);
    bytes(key, s.rotation_degrees);
    std::uint64_t size = s.type_id.size();
    bytes(key, size);
    for (char c : s.type_id)
      bytes(key, c);
  }
  if (key == route_key_)
    return;
  route_key_ = std::move(key);
  roads_.clear();
  roads_.reserve(sites.size());
  for (auto &s : sites) {
    Point c{s.x, s.z};
    float d = distance(c, {0, 0}), r = site_radius(s);
    if (!(d > stellar::core::surface_hub_radius + r + 2 * clearance))
      continue;
    Point u{c.x / d, c.y / d},
        start{u.x * stellar::core::surface_hub_radius,
              u.y * stellar::core::surface_hub_radius},
        end{c.x - u.x * r, c.y - u.y * r};
    auto path = routed(start, end, sites, s.building_id);
    if (path.size() > 1) {
      CachedRoad out{s.building_id, {}};
      for (auto p : path)
        out.points.emplace_back(p.x, p.y);
      roads_.push_back(std::move(out));
    }
  }
}
NativeSurfaceSceneDiagnostics
NativeSurfaceScene::append(DrawList &out, const SurfaceViewport &v,
                           UiRect terrain,
                           const std::vector<NativeSurfaceSite> &sites,
                           std::optional<int> selected, int hub_level) const {
  NativeSurfaceSceneDiagnostics result{};
  if (sites.size() > max_sites)
    throw std::invalid_argument("Surface scene site limit exceeded.");
  if (!std::isfinite(v.center_x) || !std::isfinite(v.center_z) ||
      std::abs(v.center_x) > 4096 || std::abs(v.center_z) > 4096 ||
      !std::isfinite(v.pixels_per_unit) || v.pixels_per_unit < .01 ||
      v.pixels_per_unit > 16 || !std::isfinite(terrain.x) ||
      !std::isfinite(terrain.y) || std::abs(terrain.x) > 16384 ||
      std::abs(terrain.y) > 16384 || !std::isfinite(terrain.width) ||
      !std::isfinite(terrain.height) || terrain.width <= 0 ||
      terrain.height <= 0 || terrain.width > 16384 || terrain.height > 16384)
    throw std::invalid_argument("Invalid surface scene viewport.");
  for (const auto &s : sites)
    if (!valid(s) || std::abs(s.x) > stellar::core::surface_area_half_size ||
        std::abs(s.z) > stellar::core::surface_area_half_size ||
        s.type_id.size() > max_type_id_bytes)
      throw std::invalid_argument("Invalid persisted surface site geometry.");
  Layers layers;
  refresh_roads(sites);
  float scale = static_cast<float>(v.pixels_per_unit);
  const bool dense = sites.size() > 24;
  for (auto &r : roads_)
    for (std::size_t i = 1;
         i < r.points.size() && result.road_segments < max_road_segments; ++i) {
      auto a = v.world_to_screen(r.points[i - 1].first, r.points[i - 1].second,
                                 terrain),
           b = v.world_to_screen(r.points[i].first, r.points[i].second,
                                 terrain);
      float hw = road_half_width * scale;
      if (!finite(a) || !finite(b) ||
          !intersects({std::min(a.x, b.x) - hw, std::min(a.y, b.y) - hw,
                       std::abs(a.x - b.x) + 2 * hw,
                       std::abs(a.y - b.y) + 2 * hw},
                      terrain))
        continue;
      segment(layers, Layer::RoadEdge, road_edge, a, b, hw + .8f * scale,
              result.triangles);
      segment(layers, Layer::Roads, road, a, b, hw, result.triangles);
      segment(layers, Layer::RoadMarking, road_marking, a, b, .22f * scale,
              result.triangles);
      ++result.road_segments;
    }
  auto hub = v.world_to_screen(0, 0, terrain);
  float hr = stellar::core::surface_hub_radius * scale;
  if (finite(hub) &&
      intersects({hub.x - hr, hub.y - hr, 2 * hr, 2 * hr}, terrain)) {
    polygon(layers, Layer::Ground, apron, hub, hr, 16, 0, result.triangles);
    polygon(layers, Layer::Structure, metal, hub, hr * .72f, 12, 0,
            result.triangles);
    const int hub_sides = hub_level >= 3 ? 16 : hub_level >= 2 ? 12 : 8;
    polygon(layers, Layer::Roof, glass, hub, hr * .42f, hub_sides, .25f,
            result.triangles);
    if (hub_level >= 3)
      polygon(layers, Layer::Roof, light, hub, hr * .18f, 8, 0,
              result.triangles);
    ring(layers, Layer::Detail, light, hub, hr * .76f, hr * .82f, 16, 0,
         result.triangles);
  }
  for (auto &s : sites) {
    auto c = v.world_to_screen(s.x, s.z, terrain);
    float rr = site_radius(s) * scale;
    if (!finite(c) || !std::isfinite(rr) ||
        !intersects({c.x - rr, c.y - rr, 2 * rr, 2 * rr}, terrain))
      continue;
    building(layers, s, c, scale, dense, result.triangles);
    ++result.sites;
    float a = angle(s);
    if (selected && *selected == s.building_id)
      ring(layers, Layer::Selection, selection, c, rr * 1.06f, rr * 1.13f,
           dense ? 8 : 16, a, result.triangles);
    auto indicator = local(c, {0, -rr * .83f}, a);
    polygon(layers, Layer::Status, status(s), indicator,
            std::clamp(rr * .07f, 1.4f, 5.f), dense ? 4 : 8, a,
            result.triangles);
    if (!s.complete && !dense) {
      float p =
          std::isfinite(s.progress_fraction)
              ? static_cast<float>(std::clamp(s.progress_fraction, 0., 1.))
              : 0;
      auto left = local(c, {-rr * .55f, rr * .78f}, a),
           right = local(c, {rr * .55f, rr * .78f}, a),
           done = local(c, {rr * (-.55f + 1.1f * p), rr * .78f}, a);
      segment(layers, Layer::Status, roof, left, right,
              std::max(.7f, rr * .035f), result.triangles);
      if (p > 0)
        segment(layers, Layer::Status, light, left, done,
                std::max(.7f, rr * .035f), result.triangles);
    }
  }
  for (auto &layer : layers)
    for (auto &[color, batch] : layer)
      if (!batch.indices.empty()) {
        out.overlay.emplace_back(TriangleMesh{std::move(batch.vertices),
                                              std::move(batch.indices), color,
                                              terrain});
        ++result.meshes;
      }
  return result;
}
} // namespace stellar::native_colony_ui
