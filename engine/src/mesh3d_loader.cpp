#include <stellar/engine/mesh3d_loader.hpp>

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace stellar::engine {
namespace {

using stellar::native_map::Mesh3D;
using stellar::native_map::Point;
using stellar::native_map::Vec3;
using stellar::native_map::Vertex3D;

struct Corner {
  int v{-1}, vt{-1}, vn{-1};
  bool operator==(const Corner &) const = default;
};

struct CornerHash {
  std::size_t operator()(const Corner &c) const noexcept {
    std::size_t h = static_cast<std::size_t>(c.v + 1);
    h = h * 65599u + static_cast<std::size_t>(c.vt + 1);
    h = h * 65599u + static_cast<std::size_t>(c.vn + 1);
    return h;
  }
};

[[noreturn]] void fail(std::string_view what) {
  throw std::runtime_error(std::string("malformed OBJ mesh: ")
                               .append(what));
}

// 1-based OBJ indices; negative values address the tail of the list.
int resolve_index(int index, std::size_t count) {
  const int resolved = index > 0 ? index - 1
                                 : static_cast<int>(count) + index;
  if (resolved < 0 || resolved >= static_cast<int>(count))
    fail("face index out of range");
  return resolved;
}

Corner parse_corner(std::string_view token, std::size_t nv, std::size_t nt,
                    std::size_t nn) {
  Corner c;
  const auto first = token.find('/');
  const auto second =
      first == std::string_view::npos ? std::string_view::npos
                                      : token.find('/', first + 1);
  const auto to_int = [](std::string_view s) {
    int value{};
    if (s.empty() ||
        std::from_chars(s.data(), s.data() + s.size(), value).ec !=
            std::errc{})
      fail("bad face index");
    return value;
  };
  c.v = resolve_index(
      to_int(first == std::string_view::npos ? token
                                             : token.substr(0, first)),
      nv);
  if (first != std::string_view::npos) {
    const auto mid =
        second == std::string_view::npos
            ? token.substr(first + 1)
            : token.substr(first + 1, second - first - 1);
    if (!mid.empty()) c.vt = resolve_index(to_int(mid), nt);
    if (second != std::string_view::npos) {
      const auto tail = token.substr(second + 1);
      if (!tail.empty()) c.vn = resolve_index(to_int(tail), nn);
    }
  }
  return c;
}

} // namespace

std::shared_ptr<const Mesh3D> load_obj_mesh(std::string_view text) {
  std::vector<Vec3> positions, normals;
  std::vector<Point> texcoords;
  std::vector<Vertex3D> vertices;
  std::vector<std::uint32_t> indices;
  // Distinct corner tuples become one vertex each (dedup keeps index reuse).
  std::unordered_map<Corner, std::uint32_t, CornerHash> corner_map;

  std::istringstream stream{std::string(text)};
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream fields(line);
    std::string kind;
    if (!(fields >> kind) || kind.empty() || kind[0] == '#') continue;
    if (kind == "v") {
      Vec3 p;
      if (!(fields >> p.x >> p.y >> p.z)) fail("bad vertex");
      positions.push_back(p);
    } else if (kind == "vn") {
      Vec3 n;
      if (!(fields >> n.x >> n.y >> n.z)) fail("bad normal");
      normals.push_back(n);
    } else if (kind == "vt") {
      Point uv;
      if (!(fields >> uv.x >> uv.y)) fail("bad texcoord");
      texcoords.push_back(uv);
    } else if (kind == "f") {
      std::vector<Corner> corners;
      std::string token;
      while (fields >> token)
        corners.push_back(parse_corner(token, positions.size(),
                                       texcoords.size(), normals.size()));
      if (corners.size() < 3) fail("face needs at least 3 corners");
      // Flat fallback normal from the first triangle when the file has
      // no vn for a corner.
      const auto pos_of = [&](const Corner &c) { return positions[c.v]; };
      Vec3 flat{0, 1, 0};
      {
        const auto a = pos_of(corners[0]), b = pos_of(corners[1]),
                   c = pos_of(corners[2]);
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
               e1.x * e2.y - e1.y * e2.x};
        const float len =
            std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-12f) flat = {n.x / len, n.y / len, n.z / len};
      }
      const auto vertex_of = [&](const Corner &c) -> std::uint32_t {
        if (const auto it = corner_map.find(c); it != corner_map.end())
          return it->second;
        Vertex3D v{pos_of(c),
                   c.vn >= 0 ? normals[c.vn] : flat,
                   c.vt >= 0 ? texcoords[c.vt] : Point{0.f, 0.f}};
        const auto index = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v);
        corner_map.emplace(c, index);
        return index;
      };
      for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
        indices.push_back(vertex_of(corners[0]));
        indices.push_back(vertex_of(corners[i]));
        indices.push_back(vertex_of(corners[i + 1]));
      }
    }
    // o/g/s/usemtl/mtllib/l/p are intentionally ignored.
  }
  if (positions.empty() || indices.empty()) fail("no triangles");
  return Mesh3D::create(std::move(vertices), std::move(indices));
}

std::shared_ptr<const Mesh3D>
load_obj_mesh_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open mesh file " +
                             path.generic_string());
  std::ostringstream contents;
  contents << input.rdbuf();
  return load_obj_mesh(contents.str());
}

} // namespace stellar::engine
