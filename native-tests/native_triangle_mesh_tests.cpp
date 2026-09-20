#include <stellar/engine/native_triangle_mesh.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace stellar::native_map;

namespace {
template <typename Change> void rejects(Change change) {
  TriangleMesh mesh{{{10.f, 20.f}, {60.f, 20.f}, {35.f, 75.f}}, {0, 1, 2},
                    {100, 180, 220, 220}, UiRect{10.25f, 20.5f, 100.f, 100.f}};
  change(mesh);
  try { validate_triangle_mesh(mesh); }
  catch (const std::invalid_argument &) { return; }
  throw std::runtime_error("Invalid geometry reached the renderer boundary.");
}
}

int main() {
  try {
    validate_triangle_mesh({});
    TriangleMesh valid{{{-5.f, -5.f}, {40.f, 0.f}, {0.f, 40.f}}, {0, 1, 2},
                        {255, 255, 255, 128}, UiRect{0.5f, 0.25f, 20.f, 10.f}};
    validate_triangle_mesh(valid); // Off-clip vertices are legal and clipped by GPU.
    valid.clip = UiRect{0.f, 0.f, 0.f, 20.f};
    validate_triangle_mesh(valid); // A zero-area clip intentionally draws nothing.
    rejects([](auto &m) { m.indices[0] = -1; });
    rejects([](auto &m) { m.indices[2] = 3; });
    rejects([](auto &m) { m.indices.pop_back(); });
    rejects([](auto &m) { m.indices.clear(); });
    rejects([](auto &m) { m.texture_coordinates={{0,0}}; });
    rejects([](auto &m) { m.vertex_colors.resize(2); });
    rejects([](auto &m) { m.texture_coordinates={{0,0},{1,0},{0,1.01f}}; });
    rejects([](auto &m) { m.texture_coordinates={{0,0},{1,0},{0,std::numeric_limits<float>::quiet_NaN()}}; });
    rejects([](auto &m) { m.vertices.clear(); });
    rejects([](auto &m) { m.vertices[0].x = std::numeric_limits<float>::quiet_NaN(); });
    rejects([](auto &m) { m.vertices[0].y = std::numeric_limits<float>::infinity(); });
    rejects([](auto &m) { m.vertices[0].x = maximum_triangle_mesh_coordinate * 2.f; });
    rejects([](auto &m) { m.clip->width = -1.f; });
    rejects([](auto &m) { m.clip->height = std::numeric_limits<float>::quiet_NaN(); });
    rejects([](auto &m) { m.clip->x = std::numeric_limits<float>::max(); });
    rejects([](auto &m) { m.clip->x = maximum_triangle_mesh_coordinate; });
    rejects([](auto &m) { m.vertices.resize(maximum_triangle_mesh_vertices + 1); });
    rejects([](auto &m) { m.indices.resize(maximum_triangle_mesh_indices + 3, 0); });
    valid.vertices.resize(maximum_triangle_mesh_vertices, {1.f, 1.f});
    valid.indices.resize(maximum_triangle_mesh_indices, 0);
    validate_triangle_mesh(valid);
    std::cout << "Native triangle geometry boundary checks passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native triangle geometry checks failed: " << error.what() << '\n';
    return 1;
  }
}
