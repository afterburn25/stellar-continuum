#include <stellar/engine/spatial_point_index.hpp>

#include <array>
#include <iostream>
#include <random>
#include <string>

using Index = stellar::engine::SpatialPointIndex;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::optional<Index::Match> brute(std::span<const Index::Point> points,
                                 float x, float y, bool influence,
                                 Index::Filter filter) {
  std::optional<Index::Match> best;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto &p = points[i];
    if (filter.excluded_index == i || filter.excluded_partition == p.partition) continue;
    const float dx = x - p.x, dy = y - p.y;
    const float value = influence ? p.weight - std::hypot(dx, dy) : dx * dx + dy * dy;
    if (!best || (influence ? value > best->value : value < best->value)) best = Index::Match{i, value};
  }
  return best;
}

void compare(const Index &index, std::span<const Index::Point> points,
              float x, float y, Index::Filter filter = {}) {
  for (const bool influence : {false, true}) {
    const auto expected = brute(points, x, y, influence, filter);
    const auto actual = influence ? index.maximum_influence(x, y, filter) : index.nearest(x, y, filter);
    require(actual.has_value() == expected.has_value(), "Index changed empty/filtered result.");
    if (actual) require(actual->index == expected->index && actual->value == expected->value,
                         "Index disagreed with original-order exhaustive float scan.");
  }
}

int main() {
  try {
    compare(Index{}, {}, 0.f, 0.f);
    std::vector<Index::Point> points;
    std::mt19937 rng(88172);
    for (int i = 0; i < 10000; ++i)
      points.push_back({static_cast<float>(rng() % 300000) / 10.f - 15000.f,
                        static_cast<float>(rng() % 300000) / 10.f - 15000.f,
                        static_cast<float>(rng() % 1200) / 10.f - 20.f, i % 7});
    const Index index(points);
    std::size_t evaluated = 0;
    for (int i = 0; i < 300; ++i) {
      const float x = static_cast<float>(rng() % 350000) / 10.f - 17500.f;
      const float y = static_cast<float>(rng() % 350000) / 10.f - 17500.f;
      compare(index, points, x, y);
      compare(index, points, x, y, {static_cast<std::size_t>(i), i % 7});
      compare(index, points, points[i].x, points[i].y, {static_cast<std::size_t>(i), std::nullopt});
      std::size_t count = 0;
      (void)index.maximum_influence(x, y, {}, &count);
      evaluated += count;
    }
    require(evaluated < 300 * points.size() / 8, "Ordinary 10,000-point queries reverted to broad scans.");
    // Degenerate trees, duplicate sites, exact ties, all-filtered queries,
    // negative radial fields, and extreme valid coordinates.
    for (int i = 0; i < 100; ++i) points[i] = {1.e7f, -1.e7f, -10.f, -9};
    points.resize(100);
    const Index duplicate(points);
    compare(duplicate, points, 1.e7f, -1.e7f);
    compare(duplicate, points, -1.e7f, 1.e7f, {0, std::nullopt});
    compare(duplicate, points, 0.f, 0.f, {std::nullopt, -9});
    points.clear();
    for (int i = 0; i < 100; ++i) points.push_back({i % 2 ? 1.f : -1.f, 0.f, 0.f, i % 2});
    compare(Index(points), points, 0.f, 0.f);
    compare(Index(points), points, 0.f, 0.f, {0, std::nullopt});
    for (float invalid : {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(), 1.e8f}) {
      bool rejected = false;
      try { (void)index.nearest(invalid, 0.f); } catch (const std::invalid_argument &) { rejected = true; }
      require(rejected, "Invalid query coordinate accepted.");
      for (int field = 0; field < 3; ++field) {
        Index::Point bad;
        if (field == 0) bad.x = invalid;
        if (field == 1) bad.y = invalid;
        if (field == 2) bad.weight = invalid;
        rejected = false;
        try { const Index bad_index(std::array{bad}); } catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "Invalid indexed coordinate/weight accepted.");
      }
    }
    std::cout << "spatial_point_index passed; evaluated " << evaluated
              << " of 3000000 possible radial candidates\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
