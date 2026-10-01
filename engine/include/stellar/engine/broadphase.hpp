#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace stellar::engine {

// Uniform-grid broadphase producing candidate AABB-overlap pairs for a
// narrow-phase consumer. Deterministic: cells iterate in sorted order and
// the emitted pair list is sorted and deduplicated, so consumers see a
// stable pair sequence for a given input set regardless of insertion
// order or hash-table layout. Overlapping AABBs always share at least one
// cell, so no true pair is ever missed; non-overlapping pairs may share a
// cell and are left for the narrow phase to reject.
template <int Dim> class UniformBroadphase {
public:
  static_assert(Dim == 2 || Dim == 3);

  // Clears the grid and fixes the cell size for the coming inserts. A
  // nonpositive size falls back to 1 so callers may pass a degenerate
  // measured extent.
  void reset(float cell_size) {
    cell_ = cell_size > 0.f && std::isfinite(cell_size) ? cell_size : 1.f;
    cells_.clear();
  }

  // Registers `key` against every grid cell its [lo,hi] AABB touches.
  void insert(std::uint64_t key, const std::array<float, Dim> &lo,
              const std::array<float, Dim> &hi) {
    std::array<std::int64_t, Dim> first, last;
    for (int d = 0; d < Dim; ++d) {
      first[d] = cell_index(lo[d]);
      last[d] = cell_index(std::max(lo[d], hi[d]));
    }
    visit(first, last, [&](const Cell &cell) { cells_[cell].push_back(key); });
  }

  // Sorted unique candidate pairs (a < b by key) sharing at least one
  // cell. Callers re-test the real volumes — sharing a cell is necessary
  // for overlap, not sufficient.
  [[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint64_t>>
  pairs() const {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
    for (const auto &[cell, keys] : cells_) {
      for (std::size_t i = 0; i < keys.size(); ++i)
        for (std::size_t j = i + 1; j < keys.size(); ++j)
          if (keys[i] != keys[j])
            out.push_back(std::minmax(keys[i], keys[j]));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }

private:
  using Cell = std::array<std::int64_t, Dim>;

  [[nodiscard]] std::int64_t cell_index(float v) const {
    return static_cast<std::int64_t>(std::floor(static_cast<double>(v) /
                                              static_cast<double>(cell_)));
  }

  template <class F> void visit(const Cell &first, const Cell &last,
                                F &&emit) {
    if constexpr (Dim == 2) {
      for (auto x = first[0]; x <= last[0]; ++x)
        for (auto y = first[1]; y <= last[1]; ++y) emit(Cell{x, y});
    } else {
      for (auto x = first[0]; x <= last[0]; ++x)
        for (auto y = first[1]; y <= last[1]; ++y)
          for (auto z = first[2]; z <= last[2]; ++z) emit(Cell{x, y, z});
    }
  }

  float cell_{1.f};
  // std::map keeps cell iteration sorted; insertion order inside a cell is
  // preserved but pairs() sorts and dedupes anyway.
  std::map<Cell, std::vector<std::uint64_t>> cells_;
};

using Broadphase2D = UniformBroadphase<2>;
using Broadphase3D = UniformBroadphase<3>;

} // namespace stellar::engine
