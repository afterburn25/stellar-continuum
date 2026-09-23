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
       << "#include <stellar/engine/runtime_host.hpp>\n\n"
       << "namespace engine = stellar::engine;\n\n"
       << "int main(int argc, char **argv) {\n"
       << "  engine::RuntimeHost host{\n"
       << "      {.package_id = \"" << id << "\",\n"
       << "       .window_title = \"" << display << "\"}};\n\n"
       << "  // Game logic lives here: it runs each frame after input,\n"
       << "  // before the built-in velocity/bounce integration. The ECS\n"
       << "  // world carries the canonical scene components (Transform2D,\n"
       << "  // Velocity2D, Extent2D, Tint, EntityName, SpriteRef).\n"
       << "  host.on_update = [](engine::World &world, float dt) {\n"
       << "    (void)world; (void)dt;\n"
       << "  };\n\n"
       << "  // The status line under the built-in help text; shows the live\n"
       << "  // entity count and pause state. host.paused()/request_quit()/\n"
       << "  // set_scene() give game code the same control the keys do.\n"
       << "  host.on_status = [&host] {\n"
       << "    return std::to_string(host.world().entities().size()) +\n"
       << "           (host.paused() ? \" entities | PAUSED (P)\"\n"
       << "                          : \" entities\");\n"
       << "  };\n\n"
       << "  // '--frames N' renders N frames then exits (CI smoke tests);\n"
       << "  // '--fixed-hz N' runs deterministic fixed-timestep simulation;\n"
       << "  // '--scene <path>' picks a different editor scene document.\n"
       << "  return host.run(argc, argv);\n"
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
