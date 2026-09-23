#include <stellar/engine/project.hpp>

#include <stellar/engine/atomic_file_write.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <system_error>

namespace stellar::engine {
namespace {

constexpr int kSchemaVersion = 1;

std::string read_text(const std::filesystem::path &path, bool &ok) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    ok = false;
    return {};
  }
  ok = true;
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool write_text(const std::filesystem::path &path, const std::string &text,
                std::string *error) {
  try {
    write_file_atomically(path, std::as_bytes(std::span(text)));
    return true;
  } catch (const std::exception &ex) {
    if (error != nullptr) *error = ex.what();
    return false;
  }
}

} // namespace

std::string sanitize_project_id(std::string_view name) {
  std::string slug;
  slug.reserve(name.size());
  bool last_dash = true; // trim leading separators
  for (const unsigned char c : name) {
    if (std::isalnum(c)) {
      slug.push_back(static_cast<char>(std::tolower(c)));
      last_dash = false;
    } else if (!last_dash) {
      slug.push_back('-');
      last_dash = true;
    }
  }
  while (!slug.empty() && slug.back() == '-')
    slug.pop_back();
  if (slug.empty()) slug = "project";
  return "game." + slug;
}

std::optional<EngineProject>
EngineProject::load(const std::filesystem::path &root, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  const auto path = root / std::string(manifest_filename);
  bool ok = false;
  const auto text = read_text(path, ok);
  if (!ok) return fail("cannot read " + path.string());

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(text);
  } catch (const std::exception &ex) {
    return fail(std::string{"manifest parse failed: "} + ex.what());
  }
  if (!doc.is_object()) return fail("manifest must be a JSON object");
  if (doc.value("schemaVersion", 0) != kSchemaVersion)
    return fail("unsupported project schemaVersion");

  EngineProject project;
  project.root = root;
  project.name = doc.value("name", std::string{});
  project.id = doc.value("id", std::string{});
  project.engine_version = doc.value("engine", std::string{});
  if (project.name.empty()) return fail("manifest requires a 'name'");
  if (project.id.empty()) return fail("manifest requires an 'id'");

  if (doc.contains("content")) {
    if (!doc.at("content").is_array())
      return fail("'content' must be an array of directory paths");
    for (const auto &entry : doc.at("content")) {
      if (!entry.is_string()) return fail("'content' entries must be strings");
      const auto dir = entry.get<std::string>();
      if (dir.empty() || std::filesystem::path(dir).is_absolute() ||
          dir.find("..") != std::string::npos)
        return fail("'content' entries must be relative in-root paths");
      project.content_dirs.push_back(dir);
    }
  }
  if (project.content_dirs.empty()) project.content_dirs.push_back("packages");
  return project;
}

std::string EngineProject::to_json() const {
  nlohmann::json doc;
  doc["schemaVersion"] = kSchemaVersion;
  doc["name"] = name;
  doc["id"] = id;
  doc["engine"] = engine_version;
  doc["content"] = content_dirs;
  return doc.dump(2) + "\n";
}

void EngineProject::save() const {
  const std::string text = to_json();
  write_file_atomically(root / std::string(manifest_filename),
                        std::as_bytes(std::span(text)));
}

bool create_project(const std::filesystem::path &root, std::string_view name,
                    std::string_view engine_version, std::string *error,
                    std::string_view starter_template) {
  auto fail = [&](const std::string &message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const bool windowed = starter_template == kTemplateWindowed;
  if (!windowed && starter_template != kTemplateBlank)
    return fail("unknown starter template: " + std::string(starter_template));
  std::error_code ec;
  if (std::filesystem::exists(root / std::string(EngineProject::manifest_filename), ec))
    return fail("a project manifest already exists at " + root.string());
  const std::string display{name.empty() ? "untitled" : std::string(name)};
  const std::string id = sanitize_project_id(name);

  EngineProject project;
  project.root = root;
  project.name = display;
  project.id = id;
  project.engine_version = std::string(engine_version);
  project.content_dirs = {"packages"};

  // Layout: manifest, one base content package that owns the project
  // namespace, an empty content directory inside it, and a starter host
  // source file.
  const auto package_dir = root / "packages" / id;
  std::filesystem::create_directories(package_dir / "content", ec);
  if (ec) return fail("cannot create " + package_dir.string() + ": " + ec.message());
  std::filesystem::create_directories(root / "src", ec);
  if (ec) return fail("cannot create src directory: " + ec.message());
  std::filesystem::create_directories(root / "mods", ec);
  if (ec) return fail("cannot create mods directory: " + ec.message());

  if (!write_text(root / std::string(EngineProject::manifest_filename),
                  project.to_json(), error))
    return false;
  if (!write_text(root / ".gitignore", "build/\ndist/\n", error))
    return false;

  nlohmann::json package;
  package["id"] = id;
  package["name"] = display;
  package["version"] = "0.1.0";
  package["priority"] = 0;
  package["provides"] = {id};
  if (!write_text(package_dir / "package.json", package.dump(2) + "\n",
                  error))
    return false;

  std::ostringstream stub;
  if (windowed) {
  stub << "// " << display << " - Stellar Engine game host.\n"
       << "// RuntimeHost owns the window, ECS world, scene hot-reload,\n"
       << "// WASD player input, sprites, audio and F5/F9 quicksave -\n"
       << "// this file only declares the project and your game logic.\n"
       << "#include <stellar/engine/runtime_host.hpp>\n"
       << "#include <stellar/engine/native_map_platform.hpp>\n"
       << "#include <stellar/engine/runtime_diagnostics.hpp>\n\n"
       << "namespace engine = stellar::engine;\n\n"
       << "int main(int argc, char **argv) {\n"
       << "  // Session log + crash minidumps land in the project's logs/ dir.\n"
       << "  engine::RuntimeDiagnostics diagnostics{\"" << display << "\",\n"
       << "                                         \"dev\", \"logs\"};\n"
       << "  engine::RuntimeHost host{\n"
       << "      {.package_id = \"" << id << "\",\n"
       << "       .window_title = \"" << display << "\"}};\n\n"
       << "  // Game logic lives here: it runs each frame after input,\n"
       << "  // before the built-in velocity/bounce integration. The ECS\n"
       << "  // world carries the canonical scene components (Transform2D,\n"
       << "  // Velocity2D, Extent2D, Tint, EntityName, SpriteRef, Layer).\n"
       << "  // This demo keeps the camera centered on the player entity.\n"
       << "  // Edge latches: fixed-step mode can call on_update several\n"
       << "  // times per rendered frame, so held-state is latched here to\n"
       << "  // fire once per physical press.\n"
       << "  bool fire_down = false, mine_down = false;\n"
       << "  host.on_update = [&host, &fire_down, &mine_down](\n"
       << "      engine::World &world, float dt) {\n"
       << "    (void)world; (void)dt;\n"
       << "    if (!host.player()) return;\n"
       << "    const auto *t =\n"
       << "        host.world().get<engine::Transform2D>(*host.player());\n"
       << "    const auto *ext =\n"
       << "        host.world().get<engine::Extent2D>(*host.player());\n"
       << "    if (t)\n"
       << "      host.set_camera(\n"
       << "          t->x + (ext ? ext->w : 0.f) * .5f -\n"
       << "              host.viewport_width() * .5f,\n"
       << "          t->y + (ext ? ext->h : 0.f) * .5f -\n"
       << "              host.viewport_height() * .5f);\n"
       << "    // The 'mine' action (C / pad West) clears the tile under the\n"
       << "    // player: tilemap cells are authoritative world state, so\n"
       << "    // edits persist via F5 saves. set_tile_at takes a document-order\n"
       << "    // map index (0 = the first/primary grid) and honors the map's\n"
       << "    // grid origin.\n"
       << "    const bool mining = host.input().pressed(\"mine\");\n"
       << "    if (mining && !mine_down && t)\n"
       << "      host.set_tile_at(\n"
       << "          0, t->x + (ext ? ext->w : 0.f) * .5f,\n"
       << "          t->y + (ext ? ext->h : 0.f) * .5f, -1);\n"
       << "    mine_down = mining;\n"
       << "    // The 'fire' action (Space / LMB / pad RB) launches a 'spark'\n"
       << "    // entity; contact events destroy sparks.\n"
       << "    const bool firing = host.input().pressed(\"fire\");\n"
       << "    if (firing && !fire_down) {\n"
       << "      engine::SceneEntity spark{};\n"
       << "      spark.name = \"spark\";\n"
       << "      // Spawn above the player so it does not instantly overlap\n"
       << "      // its own firer (contact events destroy sparks).\n"
       << "      spark.x = t->x;\n"
       << "      spark.y = t->y - (ext ? ext->h : 64.f) - 8.f;\n"
       << "      spark.w = spark.h = 12.f;\n"
       << "      spark.vx = 420.f; spark.vy = -420.f;\n"
       << "      spark.r = 255; spark.g = 220; spark.b = 80;\n"
       << "      // Sparks expire on their own if they never hit anything.\n"
       << "      spark.ttl = 3.f;\n"
       << "      const auto spark_id = host.spawn_entity(spark);\n"
       << "      // Attach a deterministic ember emitter to the spark - it\n"
       << "      // follows the entity and stops when the spark dies.\n"
       << "      host.spawn_emitter(\"ember\", spark.x + 6.f,\n"
       << "                         spark.y + 6.f, spark_id);\n"
       << "    }\n"
       << "    fire_down = firing;\n"
       << "  };\n\n"
       << "  // The status line under the built-in help text; shows the live\n"
       << "  // entity count and pause state. host.paused()/request_quit()/\n"
       << "  // set_scene() give game code the same control the keys do.\n"
       << "  host.on_status = [&host] {\n"
       << "    return std::to_string(host.world().entities().size()) +\n"
       << "           (host.paused() ? \" entities | PAUSED (P)\"\n"
       << "                          : \" entities\");\n"
       << "  };\n\n"
       << "  // A deterministic ember trail definition (VfxSystem runs in\n"
       << "  // sim time, so --fixed-hz runs stay reproducible).\n"
       << "  engine::EmitterDefinition ember{};\n"
       << "  ember.id = \"ember\";\n"
       << "  ember.spawn_rate_per_second = 40.f;\n"
       << "  ember.particle_lifetime_seconds = 0.5f;\n"
       << "  ember.velocity_min = {-30.f, -60.f, 0.f};\n"
       << "  ember.velocity_max = {30.f, -20.f, 0.f};\n"
       << "  ember.gravity = {0.f, 140.f, 0.f};\n"
       << "  ember.tint_r.add_key(0.f, 1.f);\n"
       << "  ember.tint_r.add_key(1.f, 0.9f);\n"
       << "  ember.tint_g.add_key(0.f, 0.85f);\n"
       << "  ember.tint_g.add_key(1.f, 0.2f);\n"
       << "  ember.tint_b.add_key(0.f, 0.3f);\n"
       << "  ember.tint_b.add_key(1.f, 0.05f);\n"
       << "  ember.opacity_over_life.add_key(0.f, 1.f);\n"
       << "  ember.opacity_over_life.add_key(1.f, 0.f);\n"
       << "  host.vfx().define(ember);\n\n"
       << "  // spawn_entity/destroy_entity join and leave the tracked set\n"
       << "  // (integration, bounce, render, collisions).\n"
       << "  host.on_collision = [&host](engine::EntityId a,\n"
       << "                              engine::EntityId b) {\n"
       << "    for (const auto id : {a, b})\n"
       << "      if (const auto *n =\n"
       << "              host.world().get<engine::EntityName>(id);\n"
       << "          n != nullptr && n->value == \"spark\")\n"
       << "        host.destroy_entity(id);\n"
       << "  };\n\n"
       << "  // '--frames N' renders N frames then exits (CI smoke tests);\n"
       << "  // '--fixed-hz N' runs deterministic fixed-timestep simulation;\n"
       << "  // '--scene <path>' picks a different editor scene document.\n"
       << "  // '--scene3d' runs editor/scene3d.json as a 3D world instead\n"
       << "  // (WASD flies the camera, right-drag looks, wheel zooms).\n"
       << "  try {\n"
       << "    return host.run(argc, argv);\n"
       << "  } catch (const std::exception &error) {\n"
       << "    diagnostics.fatal(error.what());\n"
       << "    return 1;\n"
       << "  }\n"
       << "}\n";
  } else {
    stub << "// " << display << " - Stellar Engine game host (blank "
            "template).\n"
         << "// Resolves the project's content packages and cooked manifest,\n"
         << "// then exits. Replace main() with your game.\n"
         << "#include <stellar/engine/asset_registry.hpp>\n"
         << "#include <stellar/engine/package.hpp>\n"
         << "#include <stellar/engine/runtime_paths.hpp>\n\n"
         << "#include <filesystem>\n"
         << "#include <iostream>\n\n"
         << "namespace engine = stellar::engine;\n\n"
         << "int main() {\n"
         << "  engine::PackageRegistry registry;\n"
         << "  engine::scan_packages(registry, \"packages\");\n"
         << "  registry.protect_namespace(\"" << id << "\");\n"
         << "  engine::scan_packages(registry, \"mods\");\n"
         << "  const auto plan = registry.resolve();\n\n"
         << "  // Content/ beside the exe (packaged layout), else the dev\n"
         << "  // build/cooked tree under the project root.\n"
         << "  std::size_t cooked_assets = 0;\n"
         << "  for (const auto manifest :\n"
         << "       {engine::executable_directory() / \"Content\" /\n"
         << "            \"runtime.stmanifest\",\n"
         << "        std::filesystem::path(\"build\") / \"cooked\" / \"Content\" /\n"
         << "            \"runtime.stmanifest\"}) {\n"
         << "    if (std::filesystem::is_regular_file(manifest)) {\n"
         << "      cooked_assets =\n"
         << "          engine::AssetRegistry(manifest).records().size();\n"
         << "      break;\n"
         << "    }\n"
         << "  }\n\n"
         << "  std::cout << \"" << display << " | \" << plan.order.size()\n"
         << "            << \" content package(s), \" << cooked_assets\n"
         << "            << \" cooked asset(s) - \"\n"
         << "            << (plan.ok ? \"load plan ok\" : \"load plan FAILED\")\n"
         << "            << '\\n';\n"
         << "  return plan.ok ? 0 : 1;\n"
         << "}\n";
  }
  if (!write_text(root / "src" / "main.cpp", stub.str(), error))
    return false;

  // Consumer build file: compiles the host against the exported engine SDK
  // (engine-sdk/, staged by the stellar-engine-sdk target next to the tools).
  std::ostringstream cmake;
  cmake << "cmake_minimum_required(VERSION 3.24)\n"
        << "project(" << id.substr(5) << " LANGUAGES CXX)\n"
        << "# The exported SDK ships Release-built static-CRT libraries only.\n"
        << "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded\")\n"
        << "if(NOT CMAKE_CONFIGURATION_TYPES)\n"
        << "  set(CMAKE_BUILD_TYPE Release CACHE STRING \"Build type\" FORCE)\n"
        << "endif()\n"
        << "if(NOT DEFINED STELLAR_ENGINE_SDK)\n"
        << "  if(DEFINED ENV{STELLAR_ENGINE_SDK})\n"
        << "    set(STELLAR_ENGINE_SDK \"$ENV{STELLAR_ENGINE_SDK}\")\n"
        << "  else()\n"
        << "    message(FATAL_ERROR \"Set -DSTELLAR_ENGINE_SDK=<engine-sdk dir> or the "
           "STELLAR_ENGINE_SDK environment variable\")\n"
        << "  endif()\n"
        << "endif()\n"
        << "include(\"${STELLAR_ENGINE_SDK}/cmake/StellarEngineSdk.cmake\")\n"
        << "add_executable(" << id.substr(5) << " src/main.cpp)\n"
        << "target_link_libraries(" << id.substr(5) << " PRIVATE "
        << (windowed ? "stellar::runtime" : "stellar::engine")
        << ")\n";
  if (windowed)
    cmake << "# SDL3.dll and the default font ship in the SDK's bin "
             "directory.\n"
          << "add_custom_command(TARGET " << id.substr(5) << " POST_BUILD\n"
          << "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n"
          << "    \"${STELLAR_ENGINE_SDK}/bin\" \"$<TARGET_FILE_DIR:"
          << id.substr(5) << ">\")\n";
  if (!write_text(root / "CMakeLists.txt", cmake.str(), error))
    return false;

  // 3D starter scene: `--scene3d` runs this world — a floor slab, a
  // gravity ball that lands on the ground plane, and a solid crate.
  if (windowed) {
    std::filesystem::create_directories(root / "editor", ec);
    const std::string scene3d = R"({
  "schemaVersion": 1,
  "entities": [
    {"name": "floor", "mesh": "box:20,0.5,20", "pos": [0, -0.25, 0], "color": [60, 90, 60], "solid": true, "gravityScale": 0},
    {"name": "ball", "mesh": "sphere:16,8", "pos": [0, 5, 0], "color": [230, 140, 60]},
    {"name": "crate", "mesh": "box", "pos": [3, 0.5, 0], "color": [120, 90, 200], "solid": true, "gravityScale": 0}
  ],
  "camera": {"pos": [0, 3, 10], "yaw": 0, "pitch": -15, "fov": 60},
  "light": {"dir": [-0.3, -0.8, -0.5], "intensity": 1.2},
  "gravity": 9.8,
  "groundY": 0,
  "bounds": 30
}
)";
    if (!write_text(root / "editor" / "scene3d.json", scene3d, error))
      return false;
  }
  return true;
}

std::vector<std::filesystem::path>
find_projects(const std::filesystem::path &directory) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec)) return out;
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    if (!entry.is_directory(ec)) continue;
    if (std::filesystem::exists(
            entry.path() / std::string(EngineProject::manifest_filename), ec))
      out.push_back(entry.path());
  }
  std::ranges::sort(out);
  return out;
}

} // namespace stellar::engine
