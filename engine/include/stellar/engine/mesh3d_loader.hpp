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

class ContentResolver;

// Resolves a MeshRef-style spec to a mesh: primitives "box[:sx,sy,sz]",
// "sphere[:cols,rows]", "annulus:inner,outer[,segments]", or a
// content-relative .obj path read through `content` (nullptr allowed —
// OBJ specs then resolve to nullptr). Never throws; unresolvable specs
// return nullptr. Shared by RuntimeHost mesh caching and tools.
[[nodiscard]] std::shared_ptr<const stellar::native_map::Mesh3D>
resolve_mesh_spec(std::string_view spec, const ContentResolver *content);

} // namespace stellar::engine
