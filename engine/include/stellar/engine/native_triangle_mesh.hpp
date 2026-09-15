#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cmath>
#include <stdexcept>

namespace stellar::native_map {

inline constexpr std::size_t maximum_triangle_mesh_vertices = 65'536;
inline constexpr std::size_t maximum_triangle_mesh_indices = 196'608;
// This also bounds the subsequent float-to-SDL-integer clip conversion.
inline constexpr float maximum_triangle_mesh_coordinate = 16'000'000.f;

inline void validate_triangle_mesh(const TriangleMesh &mesh) {
  if (mesh.vertices.size() > maximum_triangle_mesh_vertices ||
      mesh.indices.size() > maximum_triangle_mesh_indices ||
      mesh.indices.size() % 3 != 0 ||
      mesh.vertices.empty() != mesh.indices.empty())
    throw std::invalid_argument("Triangle mesh exceeds its budget or has incomplete triangles.");
  const auto bounded = [](float value) {
    return std::isfinite(value) &&
           std::abs(value) <= maximum_triangle_mesh_coordinate;
  };
  for (const auto point : mesh.vertices)
    if (!bounded(point.x) || !bounded(point.y))
      throw std::invalid_argument("Triangle mesh vertices must be finite drawable coordinates.");
  for (const auto index : mesh.indices)
    if (index < 0 || static_cast<std::size_t>(index) >= mesh.vertices.size())
      throw std::invalid_argument("Triangle mesh index is outside its vertex array.");
  if (mesh.clip) {
    const auto &clip = *mesh.clip;
    if (!bounded(clip.x) || !bounded(clip.y) ||
        !bounded(clip.width) || !bounded(clip.height) ||
        clip.width < 0 || clip.height < 0 ||
        !bounded(clip.x + clip.width) || !bounded(clip.y + clip.height))
      throw std::invalid_argument("Triangle mesh clip must be finite and nonnegative.");
  }
}

} // namespace stellar::native_map
