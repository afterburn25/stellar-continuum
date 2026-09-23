#pragma once

#include <stellar/engine/native_scene3d.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace stellar::engine {

// Minimal Wavefront OBJ loader for authored 3D assets — parses v/vn/vt/f
// records (f supports v, v/vt, v//vn, v/vt/vn and negative indices),
// fan-triangulates polygons, and generates flat face normals when the file
// omits vn. Groups/materials are ignored: one OBJ file = one Mesh3D.
// Throws std::runtime_error on malformed geometry or budget overflow.
[[nodiscard]] std::shared_ptr<const stellar::native_map::Mesh3D>
load_obj_mesh(std::string_view text);

// Reads the file and forwards to load_obj_mesh.
[[nodiscard]] std::shared_ptr<const stellar::native_map::Mesh3D>
load_obj_mesh_file(const std::filesystem::path &path);

} // namespace stellar::engine
