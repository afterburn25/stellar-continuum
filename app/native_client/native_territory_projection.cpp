#include "native_territory_projection.hpp"
#include <stellar/engine/spatial_point_index.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace stellar::native_territory {
namespace {

using native_map::Point;

[[nodiscard]] float distance_squared(Point a, Point b) {
  const float dx = a.x - b.x, dy = a.y - b.y;
  return dx * dx + dy * dy;
}
[[nodiscard]] Point lerp(Point a, Point b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
[[nodiscard]] Point vmin(Point a, Point b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y)};
}
[[nodiscard]] Point vmax(Point a, Point b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

struct GridPoint {
  int x{}, y{};
  bool operator==(const GridPoint &) const = default;
  bool operator<(const GridPoint &other) const {
    return y != other.y ? y < other.y : x < other.x;
  }
};
struct GridPointHash {
  std::size_t operator()(GridPoint p) const noexcept {
    return (static_cast<std::size_t>(static_cast<unsigned>(p.x)) << 20) ^
           static_cast<std::size_t>(static_cast<unsigned>(p.y));
  }
};

struct TerritoryGrid {
  Point origin;
  int width{}, height{};
  float cell_size{};

  [[nodiscard]] static TerritoryGrid create(std::span<const Point> positions) {
    if (positions.empty())
      throw std::invalid_argument(
          "Territory projection needs at least one system.");
    Point min = positions.front(), max = min;
    for (const auto position : positions.subspan(1)) {
      min = vmin(min, position);
      max = vmax(max, position);
    }
    constexpr float minimum_cell = 12.f, margin = 105.f,
                    maximum_cells_per_axis = 160.f;
    const Point span{max.x - min.x + margin * 2.f,
                     max.y - min.y + margin * 2.f};
    const float cell =
        std::max(minimum_cell,
                 std::max(span.x, span.y) / maximum_cells_per_axis);
    return {{min.x - margin, min.y - margin},
            std::max(1, static_cast<int>(std::ceil(span.x / cell))),
            std::max(1, static_cast<int>(std::ceil(span.y / cell))), cell};
  }

  [[nodiscard]] Point center(int x, int y) const {
    return {origin.x + (x + .5f) * cell_size,
            origin.y + (y + .5f) * cell_size};
  }
  [[nodiscard]] Point node(GridPoint p) const {
    return {origin.x + p.x * cell_size, origin.y + p.y * cell_size};
  }
  [[nodiscard]] NativeTerritoryFillRun run(int x, int y, int w) const {
    return {{origin.x + x * cell_size, origin.y + y * cell_size},
            {w * cell_size, cell_size}};
  }
};

struct AnchorKey {
  int civilization{}, system{};
  bool operator<(const AnchorKey &other) const {
    return civilization != other.civilization
               ? civilization < other.civilization
               : system < other.system;
  }
};

struct AnchorMap {
  // Ordered by (civilization, system) like the reference's
  // anchors.Values.OrderBy(CivilizationId).ThenBy(SystemId).
  std::map<AnchorKey, NativeTerritoryAnchor> entries;
  [[nodiscard]] std::vector<NativeTerritoryAnchor> all() const {
    std::vector<NativeTerritoryAnchor> out;
    out.reserve(entries.size());
    for (const auto &[key, anchor] : entries) out.push_back(anchor);
    return out;
  }
};

using PointIndex = engine::SpatialPointIndex;

struct TerritoryInfluenceIndex {
  PointIndex all;
  std::map<int, PointIndex> owners;

  TerritoryInfluenceIndex(std::span<const NativeTerritoryAnchor> anchors,
                          float cell_size) {
    std::map<int, std::vector<PointIndex::Point>> groups;
    for (const auto &anchor : anchors)
      groups[anchor.civilization_id].push_back(
          {anchor.position.x, anchor.position.y, 0.f, anchor.civilization_id});
    std::vector<PointIndex::Point> weighted;
    weighted.reserve(anchors.size());
    for (auto &[owner, points] : groups) {
      const PointIndex neighbors(points);
      for (std::size_t i = 0; i < points.size(); ++i) {
        auto &point = points[i];
        const auto nearest = neighbors.maximum_influence(point.x, point.y,
                                                         {i, std::nullopt});
        const float radius = std::clamp(
            nearest ? std::max(48.f, -nearest->value * .58f) : 58.f, 42.f, 118.f);
        point.weight = std::max(radius, cell_size * .72f);
      }
      // Owners and their anchors are sorted just like the original scan,
      // preserving the lowest-civilization tie break for cell assignment.
      weighted.insert(weighted.end(), points.begin(), points.end());
      owners.emplace(owner, PointIndex(points));
    }
    all = PointIndex(weighted);
  }

  [[nodiscard]] float dominance(Point point, int owner) const {
    const auto own = owners.at(owner).maximum_influence(point.x, point.y);
    const auto rival = all.maximum_influence(point.x, point.y,
                                             {std::nullopt, owner});
    return own->value - (rival ? std::max(0.f, rival->value) : 0.f);
  }
};

struct CellGrid {
  int width{}, height{};
  std::vector<int> cells; // row-major [y * width + x], -1 = unowned
  [[nodiscard]] int &at(int x, int y) { return cells[y * width + x]; }
  [[nodiscard]] int at(int x, int y) const { return cells[y * width + x]; }
};

[[nodiscard]] CellGrid assign(const TerritoryGrid &grid,
                              std::span<const NativeTerritoryAnchor> anchors,
                              const TerritoryInfluenceIndex &influence) {
  CellGrid out{grid.width, grid.height,
               std::vector<int>(
                   static_cast<std::size_t>(grid.width) * grid.height, -1)};
  for (int x = 0; x < grid.width; ++x)
    for (int y = 0; y < grid.height; ++y) {
      const Point point = grid.center(x, y);
      const auto best = influence.all.maximum_influence(point.x, point.y);
      out.at(x, y) = best && best->value > 0.f
                         ? anchors[best->index].civilization_id : -1;
    }
  return out;
}

void preserve_visible_owners(const TerritoryGrid &grid, CellGrid &cells,
                             const TerritoryInfluenceIndex &influence) {
  std::unordered_map<int, int> counts;
  for (int x = 0; x < grid.width; ++x)
    for (int y = 0; y < grid.height; ++y) {
      const int owner = cells.at(x, y);
      if (owner >= 0) ++counts[owner];
    }

  for (const auto &[civilization, group] : influence.owners) {
    if (counts[civilization] > 0) continue;
    int best_x = -1, best_y = -1;
    float best_distance = std::numeric_limits<float>::infinity();
    for (int x = 0; x < grid.width; ++x)
      for (int y = 0; y < grid.height; ++y) {
        const int previous_owner = cells.at(x, y);
        if (previous_owner >= 0 && counts[previous_owner] <= 1) continue;
        const Point center = grid.center(x, y);
        const float nearest = group.nearest(center.x, center.y)->value;
        if (nearest >= best_distance) continue;
        best_distance = nearest;
        best_x = x;
        best_y = y;
      }
    if (best_x < 0) continue;
    const int displaced = cells.at(best_x, best_y);
    if (displaced >= 0) --counts[displaced];
    cells.at(best_x, best_y) = civilization;
    counts[civilization] = 1;
  }
}

struct FieldVertex {
  Point point;
  float value{};
};

[[nodiscard]] std::vector<Point>
normalize_fill_polygon(std::span<const FieldVertex> source) {
  constexpr float duplicate_distance_squared = .00000001f;
  std::vector<Point> points;
  points.reserve(4);
  for (const auto &vertex : source)
    if (points.empty() ||
        distance_squared(points.back(), vertex.point) >
            duplicate_distance_squared)
      points.push_back(vertex.point);
  if (points.size() > 1 &&
      distance_squared(points.front(), points.back()) <=
          duplicate_distance_squared)
    points.pop_back();
  if (points.size() < 3) return {};
  double twice_area = 0;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto &next = points[(index + 1) % points.size()];
    twice_area += static_cast<double>(points[index].x) * next.y -
                  static_cast<double>(next.x) * points[index].y;
  }
  return std::abs(twice_area) > .000001 ? points : std::vector<Point>{};
}

[[nodiscard]] std::vector<Point>
clip_positive_triangle(const std::array<Point, 3> &points,
                       const std::array<float, 3> &values) {
  std::array<FieldVertex, 3> input;
  for (int index = 0; index < 3; ++index)
    input[index] = {points[index], values[index]};
  std::vector<FieldVertex> output;
  output.reserve(4);
  for (int index = 0; index < 3; ++index) {
    const auto from = input[index], to = input[(index + 1) % 3];
    const bool from_inside = from.value > 0.f, to_inside = to.value > 0.f;
    if (from_inside) output.push_back(from);
    if (from_inside == to_inside) continue;
    const float amount = from.value / (from.value - to.value);
    output.push_back({lerp(from.point, to.point, amount), 0.f});
  }
  return normalize_fill_polygon(output);
}

[[nodiscard]] Point canonical_intersection(Point first, float first_value,
                                           Point second, float second_value) {
  if (first.x > second.x || (first.x == second.x && first.y > second.y)) {
    std::swap(first, second);
    std::swap(first_value, second_value);
  }
  return lerp(first, second, first_value / (first_value - second_value));
}

[[nodiscard]] std::vector<Point>
triangle_crossings(const std::array<Point, 3> &points,
                   const std::array<float, 3> &values) {
  std::vector<Point> result;
  result.reserve(2);
  for (int index = 0; index < 3; ++index) {
    const int next = (index + 1) % 3;
    if ((values[index] > 0.f) == (values[next] > 0.f)) continue;
    result.push_back(canonical_intersection(points[index], values[index],
                                            points[next], values[next]));
  }
  return result;
}

struct FieldSegment {
  Point a, b;
};

struct ContourPointKey {
  int x{}, y{};
  bool operator==(const ContourPointKey &) const = default;
};
struct ContourPointKeyHash {
  std::size_t operator()(ContourPointKey p) const noexcept {
    return (static_cast<std::size_t>(static_cast<unsigned>(p.x)) << 20) ^
           static_cast<std::size_t>(static_cast<unsigned>(p.y));
  }
};

[[nodiscard]] std::vector<std::vector<Point>>
stitch_contours(const std::vector<FieldSegment> &segments) {
  const auto key = [](Point point) {
    return ContourPointKey{static_cast<int>(std::lround(point.x * 1000.f)),
                           static_cast<int>(std::lround(point.y * 1000.f))};
  };
  std::unordered_map<ContourPointKey, std::vector<int>, ContourPointKeyHash>
      by_end;
  for (std::size_t index = 0; index < segments.size(); ++index) {
    by_end[key(segments[index].a)].push_back(static_cast<int>(index));
    by_end[key(segments[index].b)].push_back(static_cast<int>(index));
  }
  std::unordered_set<int> remaining;
  for (std::size_t index = 0; index < segments.size(); ++index)
    remaining.insert(static_cast<int>(index));

  std::vector<std::vector<Point>> contours;
  while (!remaining.empty()) {
    const int first_index = *std::ranges::min_element(remaining);
    remaining.erase(first_index);
    const auto &first = segments[first_index];
    const ContourPointKey start = key(first.a);
    ContourPointKey current = key(first.b);
    std::vector<Point> points{first.a, first.b};
    while (!(current == start)) {
      const auto found = by_end.find(current);
      int next_index = -1;
      if (found != by_end.end())
        for (const int candidate : found->second)
          if (remaining.contains(candidate)) {
            next_index = candidate;
            break;
          }
      if (next_index < 0) break;
      remaining.erase(next_index);
      const auto &next = segments[next_index];
      const Point next_point =
          key(next.a) == current ? next.b : next.a;
      points.push_back(next_point);
      current = key(next_point);
    }
    if (current == start && points.size() >= 4) {
      points.pop_back();
      contours.push_back(std::move(points));
    }
  }
  return contours;
}

[[nodiscard]] std::vector<NativeTerritoryFillRun>
fill_runs(const TerritoryGrid &grid, const std::vector<bool> &cells) {
  std::vector<NativeTerritoryFillRun> result;
  for (int y = 0; y < grid.height; ++y) {
    int start = -1;
    for (int x = 0; x <= grid.width; ++x) {
      const bool match =
          x < grid.width && cells[static_cast<std::size_t>(y) * grid.width + x];
      if (match && start < 0) start = x;
      if (!match && start >= 0) {
        result.push_back(grid.run(start, y, x - start));
        start = -1;
      }
    }
  }
  return result;
}

[[nodiscard]] std::vector<NativeTerritoryFillRun>
owner_runs(const TerritoryGrid &grid, const CellGrid &cells, int owner) {
  std::vector<NativeTerritoryFillRun> result;
  for (int y = 0; y < grid.height; ++y) {
    int start = -1;
    for (int x = 0; x <= grid.width; ++x) {
      const bool match = x < grid.width && cells.at(x, y) == owner;
      if (match && start < 0) start = x;
      if (!match && start >= 0) {
        result.push_back(grid.run(start, y, x - start));
        start = -1;
      }
    }
  }
  return result;
}

struct FillGeometry {
  std::vector<NativeTerritoryFillRun> runs;
  std::vector<std::vector<Point>> polygons;
  std::vector<std::vector<Point>> contours;
};

[[nodiscard]] FillGeometry
smooth_fill(const TerritoryGrid &grid,
            const TerritoryInfluenceIndex &influence, int owner) {
  std::unordered_map<GridPoint, float, GridPointHash> values;
  const auto value = [&](GridPoint point) {
    const auto found = values.find(point);
    if (found != values.end()) return found->second;
    const float computed =
        influence.dominance(grid.node(point), owner);
    values.emplace(point, computed);
    return computed;
  };

  std::vector<bool> full(static_cast<std::size_t>(grid.width) * grid.height);
  std::vector<std::vector<Point>> polygons;
  std::vector<FieldSegment> boundary;
  for (int x = -2; x <= grid.width + 1; ++x)
    for (int y = -2; y <= grid.height + 1; ++y) {
      const std::array<GridPoint, 4> corners{GridPoint{x, y}, {x + 1, y},
                                             {x + 1, y + 1}, {x, y + 1}};
      const std::array<Point, 4> points{
          grid.node(corners[0]), grid.node(corners[1]), grid.node(corners[2]),
          grid.node(corners[3])};
      const std::array<float, 4> samples{value(corners[0]), value(corners[1]),
                                         value(corners[2]), value(corners[3])};
      const Point center{(points[0].x + points[2].x) * .5f,
                         (points[0].y + points[2].y) * .5f};
      const float center_value = influence.dominance(center, owner);
      if (center_value > 0.f &&
          std::ranges::all_of(samples, [](float v) { return v > 0.f; })) {
        if (x >= 0 && y >= 0 && x < grid.width && y < grid.height)
          full[static_cast<std::size_t>(y) * grid.width + x] = true;
        else
          polygons.emplace_back(points.begin(), points.end());
        continue;
      }
      for (int side = 0; side < 4; ++side) {
        const int next = (side + 1) % 4;
        const std::array<Point, 3> triangle_points{points[side], points[next],
                                                   center};
        const std::array<float, 3> triangle_values{samples[side], samples[next],
                                                   center_value};
        auto clipped = clip_positive_triangle(triangle_points, triangle_values);
        if (clipped.size() >= 3) polygons.push_back(std::move(clipped));
        auto crossings =
            triangle_crossings(triangle_points, triangle_values);
        if (crossings.size() == 2 &&
            distance_squared(crossings[0], crossings[1]) > .0001f)
          boundary.push_back({crossings[0], crossings[1]});
      }
    }
  return {fill_runs(grid, full), std::move(polygons), stitch_contours(boundary)};
}

[[nodiscard]] std::vector<Point> smooth(std::span<const Point> points) {
  std::vector<Point> result;
  result.reserve(points.size() * 3);
  for (std::size_t index = 0; index < points.size(); ++index) {
    const Point previous = points[(index + points.size() - 1) % points.size()];
    const Point current = points[index];
    const Point next = points[(index + 1) % points.size()];
    const Point entry = lerp(current, previous, .28f);
    const Point exit = lerp(current, next, .28f);
    result.push_back(entry);
    result.push_back(lerp(lerp(entry, current, .5f), lerp(current, exit, .5f),
                          .5f));
    result.push_back(exit);
  }
  return result;
}

[[nodiscard]] std::vector<std::vector<Point>>
owner_contours(const TerritoryGrid &grid, const CellGrid &cells, int owner) {
  const auto own = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < grid.width && y < grid.height &&
           cells.at(x, y) == owner;
  };
  std::map<GridPoint, std::vector<GridPoint>> edge_map;
  const auto add = [&](GridPoint from, GridPoint to) {
    edge_map[from].push_back(to);
  };
  for (int x = 0; x < grid.width; ++x)
    for (int y = 0; y < grid.height; ++y) {
      if (!own(x, y)) continue;
      if (!own(x, y - 1)) add({x, y}, {x + 1, y});
      if (!own(x + 1, y)) add({x + 1, y}, {x + 1, y + 1});
      if (!own(x, y + 1)) add({x + 1, y + 1}, {x, y + 1});
      if (!own(x - 1, y)) add({x, y + 1}, {x, y});
    }

  std::vector<std::vector<Point>> result;
  while (!edge_map.empty()) {
    // edges.Keys.OrderBy(Y).ThenBy(X).First() — GridPoint orders (y, x).
    const GridPoint first = edge_map.begin()->first;
    GridPoint current = first;
    std::vector<Point> loop;
    do {
      loop.push_back(grid.node(current));
      const auto found = edge_map.find(current);
      if (found == edge_map.end()) break;
      GridPoint next = found->second.front();
      found->second.erase(found->second.begin());
      if (found->second.empty()) edge_map.erase(found);
      current = next;
    } while (!(current == first) && edge_map.contains(current));
    if (loop.size() >= 3) result.push_back(smooth(loop));
  }
  return result;
}

[[nodiscard]] CellGrid build_fog_cells(
    const TerritoryGrid &grid,
    std::span<const NativeTerritorySystemInput> systems,
    const std::unordered_map<int, Point> &positions,
    const std::unordered_set<int> &unknown) {
  std::vector<PointIndex::Point> points;
  points.reserve(systems.size());
  for (const auto &system : systems) {
    const auto point = positions.at(system.id);
    points.push_back({point.x, point.y});
  }
  const PointIndex index(points);
  CellGrid out{grid.width, grid.height,
               std::vector<int>(
                   static_cast<std::size_t>(grid.width) * grid.height, 0)};
  for (int x = 0; x < grid.width; ++x)
    for (int y = 0; y < grid.height; ++y) {
      const Point point = grid.center(x, y);
      const int nearest = systems[index.nearest(point.x, point.y)->index].id;
      out.at(x, y) = unknown.contains(nearest) ? 1 : 0;
    }
  return out;
}

[[nodiscard]] NativeTerritoryFogMask build_fog_mask(const TerritoryGrid &grid,
                                                    const CellGrid &cells) {
  constexpr int padding = 8;
  const int width = grid.width + padding * 2;
  const int height = grid.height + padding * 2;
  std::vector<float> source(static_cast<std::size_t>(width) * height, 0.f);
  for (int y = 0; y < grid.height; ++y)
    for (int x = 0; x < grid.width; ++x)
      source[static_cast<std::size_t>(y + padding) * width + x + padding] =
          static_cast<float>(cells.at(x, y));
  std::vector<float> horizontal(source.size(), 0.f);
  std::vector<std::uint8_t> alpha(source.size(), 0);
  constexpr int weights[7] = {1, 6, 15, 20, 15, 6, 1};
  for (int y = 3; y < height - 3; ++y)
    for (int x = 3; x < width - 3; ++x) {
      float value = 0;
      for (int offset = -3; offset <= 3; ++offset)
        value += source[static_cast<std::size_t>(y) * width + x + offset] *
                 weights[offset + 3];
      horizontal[static_cast<std::size_t>(y) * width + x] = value / 64.f;
    }
  for (int y = 3; y < height - 3; ++y)
    for (int x = 3; x < width - 3; ++x) {
      float value = 0;
      for (int offset = -3; offset <= 3; ++offset)
        value += horizontal[static_cast<std::size_t>(y + offset) * width + x] *
                 weights[offset + 3];
      alpha[static_cast<std::size_t>(y) * width + x] =
          static_cast<std::uint8_t>(
              std::clamp(std::lround(value * 255.f / 64.f), 0l, 255l));
    }
  return {{grid.origin.x - padding * grid.cell_size,
           grid.origin.y - padding * grid.cell_size},
          {width * grid.cell_size, height * grid.cell_size},
          width,
          height,
          std::move(alpha)};
}

[[nodiscard]] std::uint64_t fnv_mix(std::uint64_t hash, std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
  return hash;
}
[[nodiscard]] std::uint64_t fnv_float(std::uint64_t hash, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  return fnv_mix(hash, bits);
}
// Constant-memory framing for text read by the projection. The rendered name
// itself is never retained by the cache key, but any rename changes it.
[[nodiscard]] std::uint64_t fnv_text(std::uint64_t hash,
                                     std::string_view value) {
  hash = fnv_mix(hash, value.size());
  for (const unsigned char character : value)
    hash = fnv_mix(hash, character);
  return hash;
}

} // namespace

NativeTerritoryProjection build_native_territory_projection(
    const NativeTerritoryInput &input) {
  if (!std::isfinite(input.coordinate_scale) || input.coordinate_scale <= 0.f)
    throw std::invalid_argument("coordinate_scale must be finite and positive.");
  if (input.systems.empty())
    throw std::invalid_argument("Territory projection needs at least one system.");
  std::unordered_map<int, Point> positions;
  positions.reserve(input.systems.size());
  std::vector<Point> all_positions;
  all_positions.reserve(input.systems.size());
  for (const auto &system : input.systems) {
    const Point position{system.position.x * input.coordinate_scale,
                         system.position.y * input.coordinate_scale};
    // Contour stitching quantizes positions into signed millipixel keys.
    // Reject malformed snapshots before grid dimensions or keys can overflow.
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        std::max(std::abs(position.x), std::abs(position.y)) > 1.e6f)
      throw std::invalid_argument("Territory coordinates exceed the projection range.");
    positions.emplace(system.id, position);
    all_positions.push_back(position);
  }
  std::vector<NativeTerritoryAnchor> all;
  std::unordered_map<int, std::string_view> names;
  for (const auto &region : input.regions) {
    names.emplace(region.civilization_id, region.civilization_name);
    for (auto anchor : region.anchors) {
      const auto found = positions.find(anchor.system_id);
      if (found == positions.end()) continue;
      anchor.position = found->second;
      all.push_back(std::move(anchor));
    }
  }
  std::ranges::sort(all, {}, [](const auto &anchor) {
    return std::pair{anchor.civilization_id, anchor.system_id};
  });
  const TerritoryGrid grid = TerritoryGrid::create(all_positions);
  const TerritoryInfluenceIndex influence(all, grid.cell_size);
  CellGrid cells = assign(grid, all, influence);
  preserve_visible_owners(grid, cells, influence);
  NativeTerritoryProjection projection;
  projection.grid_cell_count = grid.width * grid.height;
  for (const auto &input_region : input.regions) {
    const int owner = input_region.civilization_id;
    const auto occupied = owner_runs(grid, cells, owner);
    if (occupied.empty()) continue;
    auto fill = smooth_fill(grid, influence, owner);
    const auto &largest = *std::ranges::max_element(
        occupied, {}, [](const NativeTerritoryFillRun &run) {
          return run.size.x * run.size.y;
        });
    NativeTerritoryRegion region;
    region.civilization_id = owner;
    region.civilization_name = input_region.civilization_name;
    region.anchors = input_region.anchors;
    for (auto &anchor : region.anchors) anchor.position = positions.at(anchor.system_id);
    region.label_position = {largest.position.x + largest.size.x * .5f,
                             largest.position.y + largest.size.y * .5f};
    region.fill_runs = std::move(fill.runs);
    region.fill_polygons = std::move(fill.polygons);
    region.contours = std::move(fill.contours);
    projection.territories.push_back(std::move(region));
  }
  for (const auto &claim : input.claims) {
    const auto position = positions.find(claim.system_id);
    if (position == positions.end() || !names.contains(claim.civilization_id))
      continue;
    projection.claims.push_back({claim.civilization_id, claim.system_id,
                                 position->second,
                                 std::max(28.f, grid.cell_size * 1.3f)});
  }
  std::ranges::sort(projection.claims, {}, [](const auto &claim) {
    return std::pair{claim.civilization_id, claim.system_id};
  });
  for (const auto &system : input.systems)
    if (!system.known) projection.unexplored_system_ids.insert(system.id);
  projection.unowned_cell_count = static_cast<int>(std::ranges::count(cells.cells, -1));
  const CellGrid fog_cells =
      build_fog_cells(grid, input.systems, positions, projection.unexplored_system_ids);
  projection.fog = build_fog_mask(grid, fog_cells);
  return projection;
}

NativeTerritoryInput capture_native_territory_input(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale) {
  constexpr std::size_t maximum_catalog_systems = core::maximum_full_galaxy_system_count;
  constexpr std::size_t maximum_civilization_name_bytes = 256;
  if (world.systems.size() > maximum_catalog_systems)
    throw std::length_error("Native territory input exceeds the supported catalog size.");
  NativeTerritoryInput input;
  input.observer_civilization_id = observer_civilization_id;
  input.coordinate_scale = coordinate_scale;
  input.systems.reserve(world.systems.size());
  for (const auto &system : world.systems)
    input.systems.push_back({system.id, {system.position.x, system.position.y},
                             world.knowledge.is_system_known(
                                 observer_civilization_id, system.id)});
  std::unordered_map<int, const core::StellarSystem *> systems;
  std::unordered_map<int, const core::Civilization *> civilizations;
  std::map<int, std::vector<const core::Colony *>> settlements;
  for (const auto &system : world.systems) systems.emplace(system.id, &system);
  for (const auto &civilization : world.civilizations)
    civilizations.emplace(civilization.id, &civilization);
  for (const auto &colony : world.colonies) settlements[colony.system_id].push_back(&colony);
  const auto visible = [&](int civilization_id, int system_id) {
    return civilization_id == observer_civilization_id ||
           (world.knowledge.is_civilization_known(observer_civilization_id,
                                                  civilization_id) &&
            world.knowledge.is_system_fully_surveyed(observer_civilization_id,
                                                      system_id));
  };
  AnchorMap anchors;
  const auto add = [&](int civilization_id, int system_id,
                       NativeTerritoryAnchorKind kind) {
    if (!systems.contains(system_id) || !civilizations.contains(civilization_id) ||
        !visible(civilization_id, system_id))
      return;
    const AnchorKey key{civilization_id, system_id};
    const auto existing = anchors.entries.find(key);
    if (existing != anchors.entries.end() && kind >= existing->second.kind) return;
    anchors.entries[key] = {civilization_id, system_id, {}, kind};
  };
  for (const auto &[id, civilization] : civilizations) {
    const auto found = settlements.find(civilization->home_system_id);
    const core::Colony *owner = nullptr;
    if (found != settlements.end())
      owner = *std::ranges::min_element(
          found->second, {}, [](const core::Colony *colony) { return colony->id; });
    if (!owner || owner->civilization_id == civilization->id ||
        !visible(owner->civilization_id, civilization->home_system_id))
      add(civilization->id, civilization->home_system_id,
          NativeTerritoryAnchorKind::home);
  }
  for (const auto &colony : world.colonies)
    add(colony.civilization_id, colony.system_id,
        NativeTerritoryAnchorKind::settlement);
  std::map<int, std::size_t> region_index;
  const auto ensure_region = [&](int civilization_id) {
    auto [found, inserted] = region_index.emplace(civilization_id,
                                                  input.regions.size());
    if (inserted) {
      const auto &name = civilizations.at(civilization_id)->name;
      if (name.size() > maximum_civilization_name_bytes)
        throw std::length_error("Native territory civilization name exceeds its input cap.");
      input.regions.push_back({civilization_id, name, {}});
    }
    return found->second;
  };
  for (const auto &anchor : anchors.all()) {
    input.regions[ensure_region(anchor.civilization_id)].anchors.push_back(anchor);
  }
  for (const auto &claim : observer_claims)
    if (claim.active && civilizations.contains(claim.claimant_civilization_id) &&
        systems.contains(claim.system_id) &&
        visible(claim.claimant_civilization_id, claim.system_id))
      {
        (void)ensure_region(claim.claimant_civilization_id);
        input.claims.push_back(
            {claim.claimant_civilization_id, claim.system_id});
      }
  return input;
}

NativeTerritoryProjection build_native_territory_projection(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale) {
  return build_native_territory_projection(capture_native_territory_input(
      world, observer_civilization_id, observer_claims, coordinate_scale));
}

std::uint64_t native_territory_fingerprint(
    const NativeTerritoryInput &input) {
  std::uint64_t hash = 0xcbf29ce484222325ull;
  hash = fnv_mix(hash, static_cast<std::uint64_t>(input.observer_civilization_id));
  hash = fnv_float(hash, input.coordinate_scale);
  hash = fnv_mix(hash, input.systems.size());
  for (const auto &system : input.systems) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(system.id));
    hash = fnv_float(hash, system.position.x);
    hash = fnv_float(hash, system.position.y);
    hash = fnv_mix(hash, system.known ? 1ull : 0ull);
  }
  hash = fnv_mix(hash, input.regions.size());
  for (const auto &region : input.regions) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(region.civilization_id));
    hash = fnv_text(hash, region.civilization_name);
    hash = fnv_mix(hash, region.anchors.size());
    for (const auto &anchor : region.anchors) {
      hash = fnv_mix(hash, static_cast<std::uint64_t>(anchor.system_id));
      hash = fnv_mix(hash, static_cast<std::uint64_t>(anchor.kind));
    }
  }
  hash = fnv_mix(hash, input.claims.size());
  for (const auto &claim : input.claims) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(claim.civilization_id));
    hash = fnv_mix(hash, static_cast<std::uint64_t>(claim.system_id));
  }
  return hash;
}

std::uint64_t native_territory_fingerprint(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    float coordinate_scale) {
  std::uint64_t hash = 0xcbf29ce484222325ull;
  hash = fnv_mix(hash, static_cast<std::uint64_t>(observer_civilization_id));
  hash = fnv_float(hash, coordinate_scale);
  hash = fnv_mix(hash, world.civilizations.size());
  for (const auto &civilization : world.civilizations) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(civilization.id));
    hash = fnv_mix(hash, static_cast<std::uint64_t>(civilization.home_system_id));
    hash = fnv_text(hash, civilization.name);
    hash = fnv_mix(hash, world.knowledge.is_civilization_known(
                            observer_civilization_id, civilization.id)
                            ? 1ull
                            : 0ull);
  }
  hash = fnv_mix(hash, world.colonies.size());
  for (const auto &colony : world.colonies) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(colony.id));
    hash = fnv_mix(hash, static_cast<std::uint64_t>(colony.civilization_id));
    hash = fnv_mix(hash, static_cast<std::uint64_t>(colony.system_id));
  }
  hash = fnv_mix(hash, world.systems.size());
  for (const auto &system : world.systems) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(system.id));
    hash = fnv_float(hash, system.position.x);
    hash = fnv_float(hash, system.position.y);
    hash = fnv_mix(hash, world.knowledge.is_system_known(
                            observer_civilization_id, system.id) ? 1ull : 0ull);
    hash = fnv_mix(hash, static_cast<std::uint64_t>(
                            world.knowledge.system_survey_level(
                                observer_civilization_id, system.id)));
  }
  hash = fnv_mix(hash, observer_claims.size());
  for (const auto &claim : observer_claims) {
    hash = fnv_mix(hash, static_cast<std::uint64_t>(claim.claim_id));
    hash = fnv_mix(hash,
                   static_cast<std::uint64_t>(claim.claimant_civilization_id));
    hash = fnv_mix(hash, static_cast<std::uint64_t>(claim.system_id));
    hash = fnv_mix(hash, claim.active ? 1ull : 0ull);
  }
  return hash;
}

float native_territory_overview_blend(float scale, float fitted_scale) {
  constexpr float overview_blend_end_scale = .78f;
  const float end = std::min(fitted_scale * 5.f, overview_blend_end_scale);
  const float start = std::min(fitted_scale * 1.2f, end * .8f);
  const float progress = std::clamp(
      (scale - start) / std::max(.0000001f, end - start), 0.f, 1.f);
  return 1.f - progress * progress * (3.f - 2.f * progress);
}

float native_territory_detail(float overview_blend) {
  const float regional_opacity = std::clamp(1.f - overview_blend * 2.f, 0.f, 1.f);
  return .34f + .46f * regional_opacity;
}

native_map::Color native_territory_color(int civilization_id,
                                         int player_civilization_id) {
  if (civilization_id == player_civilization_id) return {0x58, 0xcf, 0xfb, 255};
  switch (civilization_id % 6) {
  case 0: return {0x5f, 0xd2, 0xc0, 255};
  case 1: return {0xaf, 0x8f, 0xff, 255};
  case 2: return {0xe9, 0xb6, 0x5c, 255};
  case 3: return {0xff, 0x77, 0x6e, 255};
  case 4: return {0x64, 0xd6, 0xa5, 255};
  default: return {0xd4, 0x84, 0xb8, 255};
  }
}

} // namespace stellar::native_territory
