#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace stellar::engine {

// Immutable 2D point index. Partitions are opaque caller labels; results refer
// to the original input order, which also breaks exact distance/score ties.
// No query allocations or shared mutable scratch; concurrent reads are safe.
class SpatialPointIndex {
public:
  struct Point {
    float x{}, y{}, weight{};
    int partition{};
  };
  struct Match {
    std::size_t index{};
    float value{}; // squared distance for nearest, weight - distance otherwise
  };
  struct Filter {
    std::optional<std::size_t> excluded_index;
    std::optional<int> excluded_partition;
  };

  SpatialPointIndex() = default;
  explicit SpatialPointIndex(std::span<const Point> points)
      : points_(points.begin(), points.end()), order_(points.size()) {
    for (const auto &point : points_)
      if (!valid(point.x) || !valid(point.y) || !valid(point.weight))
        throw std::invalid_argument("Invalid spatial point or weight.");
    std::iota(order_.begin(), order_.end(), std::size_t{});
    nodes_.reserve(points.size());
    if (!points.empty()) (void)build(0, points.size());
  }

  [[nodiscard]] std::optional<Match> nearest(
      float x, float y, Filter filter = {},
      std::size_t *evaluated_points = nullptr) const {
    return query(x, y, false, filter, evaluated_points);
  }
  // Includes negative scores: callers can sample a continuous radial field
  // outside every point's radius without clipping it at zero.
  [[nodiscard]] std::optional<Match> maximum_influence(
      float x, float y, Filter filter = {},
      std::size_t *evaluated_points = nullptr) const {
    return query(x, y, true, filter, evaluated_points);
  }

private:
  static constexpr std::size_t no_node = std::numeric_limits<std::size_t>::max();
  struct Node {
    float left{}, top{}, right{}, bottom{}, maximum_weight{};
    std::size_t begin{}, end{}, first{no_node}, second{no_node};
    std::optional<int> uniform_partition;
  };
  // This bound keeps float subtraction, squared distances and scores finite.
  static bool valid(float value) {
    return std::isfinite(value) && std::abs(value) <= 1.e7f;
  }
  std::size_t build(std::size_t begin, std::size_t end) {
    const auto &point = points_[order_[begin]];
    Node node{point.x, point.y, point.x, point.y, point.weight,
              begin, end, no_node, no_node, point.partition};
    for (auto i = begin + 1; i < end; ++i) {
      const auto &p = points_[order_[i]];
      node.left = std::min(node.left, p.x);
      node.right = std::max(node.right, p.x);
      node.top = std::min(node.top, p.y);
      node.bottom = std::max(node.bottom, p.y);
      node.maximum_weight = std::max(node.maximum_weight, p.weight);
      if (node.uniform_partition != p.partition) node.uniform_partition.reset();
    }
    const auto id = nodes_.size();
    nodes_.push_back(node);
    if (end - begin > 8) {
      const bool use_x = node.right - node.left >= node.bottom - node.top;
      const auto middle = begin + (end - begin) / 2;
      std::nth_element(order_.begin() + begin, order_.begin() + middle,
                       order_.begin() + end, [&](auto a, auto b) {
        const float av = use_x ? points_[a].x : points_[a].y;
        const float bv = use_x ? points_[b].x : points_[b].y;
        return av != bv ? av < bv : a < b;
      });
      const auto first = build(begin, middle);
      const auto second = build(middle, end);
      nodes_[id].first = first;
      nodes_[id].second = second;
    }
    return id;
  }

  float bound(const Node &node, float x, float y, bool influence) const {
    const float dx = x < node.left ? node.left - x
                     : x > node.right ? x - node.right : 0.f;
    const float dy = y < node.top ? node.top - y
                     : y > node.bottom ? y - node.bottom : 0.f;
    // Round outward, and never prune equality: a later branch can contain
    // an earlier input index with the same floating-point result.
    return influence
        ? std::nextafter(node.maximum_weight - std::hypot(dx, dy),
                         std::numeric_limits<float>::infinity())
        : std::nextafter(dx * dx + dy * dy,
                         -std::numeric_limits<float>::infinity());
  }
  void visit(std::size_t id, float x, float y, bool influence,
             const Filter &filter, std::optional<Match> &best,
             std::size_t *evaluated_points) const {
    const auto &node = nodes_[id];
    if (filter.excluded_partition &&
        node.uniform_partition == filter.excluded_partition) return;
    const float limit = bound(node, x, y, influence);
    if (best && (influence ? limit < best->value : limit > best->value)) return;
    if (node.first != no_node) {
      auto first = node.first, second = node.second;
      const float a = bound(nodes_[first], x, y, influence);
      const float b = bound(nodes_[second], x, y, influence);
      if (influence ? b > a : b < a) std::swap(first, second);
      visit(first, x, y, influence, filter, best, evaluated_points);
      visit(second, x, y, influence, filter, best, evaluated_points);
      return;
    }
    for (auto i = node.begin; i < node.end; ++i) {
      const auto index = order_[i];
      const auto &point = points_[index];
      if (filter.excluded_index == index ||
          filter.excluded_partition == point.partition) continue;
      if (evaluated_points) ++*evaluated_points;
      const float dx = x - point.x, dy = y - point.y;
      const float value = influence ? point.weight - std::hypot(dx, dy)
                                    : dx * dx + dy * dy;
      if (!best || (influence ? value > best->value : value < best->value) ||
          (value == best->value && index < best->index)) best = Match{index, value};
    }
  }
  std::optional<Match> query(float x, float y, bool influence,
                             const Filter &filter,
                             std::size_t *evaluated_points) const {
    if (!valid(x) || !valid(y))
      throw std::invalid_argument("Invalid spatial point query.");
    if (evaluated_points) *evaluated_points = 0;
    std::optional<Match> best;
    if (!nodes_.empty()) visit(0, x, y, influence, filter, best, evaluated_points);
    return best;
  }
  std::vector<Point> points_;
  std::vector<std::size_t> order_;
  std::vector<Node> nodes_;
};

} // namespace stellar::engine
