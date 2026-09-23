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
       << "// Opens a native window through stellar::platform, resolves the\n"
       << "// project's content packages, and renders. Escape quits.\n"
       << "#include <stellar/engine/asset_registry.hpp>\n"
       << "#include <stellar/engine/foundation.hpp>\n"
       << "#include <stellar/engine/native_map_platform.hpp>\n"
       << "#include <stellar/engine/package.hpp>\n"
       << "#include <stellar/engine/runtime_paths.hpp>\n"
       << "#include <stellar/engine/native_audio.hpp>\n"
       << "#include <stellar/engine/scene_document.hpp>\n"
       << "#include <stellar/engine/texture_cook.hpp>\n"
       << "#include <stellar/engine/world.hpp>\n\n"
       << "#include <algorithm>\n"
       << "#include <chrono>\n"
       << "#include <cstdint>\n"
       << "#include <filesystem>\n"
       << "#include <memory>\n"
       << "#include <optional>\n"
       << "#include <unordered_set>\n"
       << "#include <vector>\n\n"
       << "using namespace stellar::native_map;\n"
       << "namespace engine = stellar::engine;\n\n"
       << "// Game components live in your game's headers; World stores any type.\n"
       << "struct Transform { float x, y; };\n"
       << "struct Velocity { float dx, dy; };\n"
       << "struct Extent { float w, h; };\n"
       << "struct Tint { std::uint8_t r, g, b; };\n\n"
       << "int main() {\n"
       << "  // Content roots resolve relative to the working directory (the\n"
       << "  // project root when launched from the engine tools). The base\n"
       << "  // package registers first; the namespace is then protected so mod\n"
       << "  // packages under mods/ cannot override it.\n"
       << "  engine::PackageRegistry registry;\n"
       << "  engine::scan_packages(registry, \"packages\");\n"
       << "  registry.protect_namespace(\"" << id << "\");\n"
       << "  engine::scan_packages(registry, \"mods\");\n"
       << "  const auto plan = registry.resolve();\n\n"
       << "  // Cooked content validates when present: Content/ beside the\n"
       << "  // executable (packaged layout from the tools' PACKAGE step) or\n"
       << "  // build/cooked/ under the project root (dev layout from COOK).\n"
       << "  // A local registry — not the global mount — so loose resources like\n"
       << "  // the bundled font keep resolving beside the executable.\n"
       << "  std::size_t cooked_assets = 0;\n"
       << "  std::unique_ptr<engine::AssetRegistry> cooked_registry;\n"
       << "  const auto exe_dir = engine::executable_directory();\n"
       << "  for (const auto manifest :\n"
       << "       {exe_dir / \"Content\" / \"runtime.stmanifest\",\n"
       << "        std::filesystem::path(\"build\") / \"cooked\" / \"Content\" /\n"
       << "            \"runtime.stmanifest\"}) {\n"
       << "    if (std::filesystem::is_regular_file(manifest)) {\n"
       << "      cooked_registry =\n"
       << "          std::make_unique<engine::AssetRegistry>(manifest);\n"
       << "      cooked_assets = cooked_registry->records().size();\n"
       << "      break;\n"
       << "    }\n"
       << "  }\n\n"
       << "  Window window(\"" << display << "\", 1280, 720, false,\n"
       << "                exe_dir / \"engine-default-font.ttf\");\n"
       << "  window.set_auto_frame_cap();\n\n"
       << "  // Optional audio under the base package's content dir:\n"
       << "  // audio/music.wav|mp3 loops as background music and\n"
       << "  // audio/bounce.wav|mp3 plays when the player bounces off a wall.\n"
       << "  engine::audio::AudioOutput audio;\n"
       << "  auto load_clip = [&](const char *name)\n"
       << "      -> std::shared_ptr<const engine::audio::AudioClip> {\n"
       << "    const auto path = std::filesystem::path(\"packages\") / \""
       << id << "\" / \"content\" / name;\n"
       << "    if (!std::filesystem::is_regular_file(path)) return {};\n"
       << "    try {\n"
       << "      return engine::audio::decode_audio_clip(path);\n"
       << "    } catch (const std::exception &) {\n"
       << "      return {};\n"
       << "    }\n"
       << "  };\n"
       << "  auto bounce_clip = load_clip(\"audio/bounce.wav\");\n"
       << "  if (!bounce_clip) bounce_clip = load_clip(\"audio/bounce.mp3\");\n"
       << "  if (const auto music = load_clip(\"audio/music.wav\"))\n"
       << "    audio.play_music(music);\n"
       << "  else if (const auto mp3 = load_clip(\"audio/music.mp3\"))\n"
       << "    audio.play_music(mp3);\n\n"
       << "  // The entity/component world: create entities, attach components,\n"
       << "  // tick systems each frame.\n"
       << "  engine::World world;\n"
       << "  std::vector<engine::EntityId> entities;\n"
       << "  std::vector<std::shared_ptr<const RgbaImage>> sprites;\n"
       << "  // The entity named \"player\" is driven by WASD/arrow keys.\n"
       << "  std::optional<engine::EntityId> player;\n"
       << "  std::unordered_set<std::uint32_t> held_keys;\n"
       << "  // (Re)spawns World entities from a scene document; sprites decode\n"
       << "  // from the base package's content dir (sprite \"data/logo.png\" ->\n"
       << "  // packages/" << id << "/content/data/logo.png).\n"
       << "  auto spawn_entities = [&](const engine::SceneDocument &doc) {\n"
       << "    for (const auto e : entities) world.destroy(e);\n"
       << "    entities.clear();\n"
       << "    player.reset();\n"
       << "    sprites.assign(doc.entities.size(), {});\n"
       << "    for (std::size_t i = 0; i < doc.entities.size(); ++i) {\n"
       << "      const auto &s = doc.entities[i];\n"
       << "      const auto entity = world.create();\n"
       << "      world.add(entity, Transform{s.x, s.y});\n"
       << "      world.add(entity, Velocity{s.vx, s.vy});\n"
       << "      world.add(entity, Extent{s.w, s.h});\n"
       << "      world.add(entity, Tint{s.r, s.g, s.b});\n"
       << "      if (s.name == \"player\") player = entity;\n"
       << "      if (!s.sprite.empty()) {\n"
       << "        // Prefer the cooked package (scan_content aliases are\n"
       << "        // \"<id>/content/<relative-path>\"); fall back to the loose\n"
       << "        // source file when the project has not been cooked.\n"
       << "        if (cooked_registry) {\n"
       << "          auto alias = s.sprite;\n"
       << "          std::replace(alias.begin(), alias.end(), '\\\\', '/');\n"
       << "          if (const auto *rec = cooked_registry->find(\n"
       << "                  std::string(\"" << id << "/content/\") + alias)) {\n"
       << "            try {\n"
       << "              // Cooked textures store one chunk per BC7 mip level.\n"
       << "              if (rec->type == \"texture\" && rec->format == \"BC7\") {\n"
       << "                std::vector<Bc1MipLevel> mips;\n"
       << "                for (std::size_t c = 0; c < rec->chunks.size(); ++c) {\n"
       << "                  const auto &chunk = rec->chunks[c];\n"
       << "                  mips.push_back(Bc1MipLevel{chunk.width, chunk.height,\n"
       << "                                             cooked_registry->read(*rec, c)});\n"
       << "                }\n"
       << "                sprites[i] = RgbaImage::create_cooked(\n"
       << "                    TextureFormat::Bc7, std::move(mips));\n"
       << "              }\n"
       << "            } catch (const std::exception &) {\n"
       << "            }\n"
       << "          }\n"
       << "        }\n"
       << "        if (!sprites[i]) {\n"
       << "          try {\n"
       << "            sprites[i] = decode_rgba_image(\n"
       << "                std::filesystem::path(\"packages\") / \"" << id << "\" /\n"
       << "                    \"content\" / s.sprite,\n"
       << "                2048);\n"
       << "          } catch (const std::exception &) {\n"
       << "          }\n"
       << "        }\n"
       << "      }\n"
       << "      entities.push_back(entity);\n"
       << "    }\n"
       << "  };\n\n"
       << "  // Authored entities come from editor/scene.json (the engine tools'\n"
       << "  // Scene tool); the file is polled so saved edits apply live to the\n"
       << "  // running game. One demo entity when the document is absent.\n"
       << "  const auto scene_file = std::filesystem::path(\"editor/scene.json\");\n"
       << "  auto scene_stamp = std::filesystem::file_time_type{};\n"
       << "  auto reload_scene = [&] {\n"
       << "    std::error_code ec;\n"
       << "    const auto stamp = std::filesystem::last_write_time(scene_file, ec);\n"
       << "    if (ec || stamp == scene_stamp) return;\n"
       << "    if (const auto doc = engine::SceneDocument::load(scene_file)) {\n"
       << "      scene_stamp = stamp;\n"
       << "      spawn_entities(*doc);\n"
       << "    }\n"
       << "  };\n"
       << "  reload_scene();\n"
       << "  if (entities.empty())\n"
       << "    spawn_entities(engine::SceneDocument{{engine::SceneEntity{\n"
       << "        \"demo\", 120.f, 160.f, 96.f, 96.f, 240.f, 150.f}}});\n\n"
       << "  auto last = std::chrono::steady_clock::now();\n"
       << "  auto scene_poll = last;\n"
       << "  for (;;) {\n"
       << "    const auto snapshot = window.poll();\n"
       << "    if (snapshot.quit_requested) break;\n"
       << "    for (const auto &event : snapshot.events) {\n"
       << "      if (event.type == InputEventType::EscapePressed) return 0;\n"
       << "      if (event.type == InputEventType::KeyPressed)\n"
       << "        held_keys.insert(event.key);\n"
       << "      if (event.type == InputEventType::KeyReleased)\n"
       << "        held_keys.erase(event.key);\n"
       << "    }\n"
       << "    if (!snapshot.renderable()) continue;\n\n"
       << "    const auto now = std::chrono::steady_clock::now();\n"
       << "    const float dt = std::chrono::duration<float>(now - last).count();\n"
       << "    last = now;\n"
       << "    if (now - scene_poll > std::chrono::milliseconds(500)) {\n"
       << "      scene_poll = now;\n"
       << "      reload_scene();\n"
       << "    }\n"
       << "    const float w = static_cast<float>(snapshot.drawable_width);\n"
       << "    const float h = static_cast<float>(snapshot.drawable_height);\n\n"
       << "    // Input system: WASD/arrow keys drive the entity named \"player\"\n"
       << "    // (SDL keycodes: arrows are 0x4000004f-0x40000052).\n"
       << "    if (player) {\n"
       << "      auto *v = world.get<Velocity>(*player);\n"
       << "      if (v) {\n"
       << "        const auto held = [&](std::uint32_t k) {\n"
       << "          return held_keys.count(k) != 0;\n"
       << "        };\n"
       << "        const float dx =\n"
       << "            (held('d') || held(0x4000004f) ? 1.f : 0.f) -\n"
       << "            (held('a') || held(0x40000050) ? 1.f : 0.f);\n"
       << "        const float dy =\n"
       << "            (held('s') || held(0x40000051) ? 1.f : 0.f) -\n"
       << "            (held('w') || held(0x40000052) ? 1.f : 0.f);\n"
       << "        v->dx = dx * 320.f;\n"
       << "        v->dy = dy * 320.f;\n"
       << "      }\n"
       << "    }\n\n"
       << "    // Movement system: integrate velocity, bounce off the frame.\n"
       << "    for (const auto entity : entities) {\n"
       << "      auto *t = world.get<Transform>(entity);\n"
       << "      auto *v = world.get<Velocity>(entity);\n"
       << "      const auto *ext = world.get<Extent>(entity);\n"
       << "      if (!t || !v || !ext) continue;\n"
       << "      t->x += v->dx * dt; t->y += v->dy * dt;\n"
       << "      bool bounced = false;\n"
       << "      if (t->x < 0 || t->x > w - ext->w) {\n"
       << "        v->dx = -v->dx; bounced = true;\n"
       << "      }\n"
       << "      if (t->y < 0 || t->y > h - ext->h) {\n"
       << "        v->dy = -v->dy; bounced = true;\n"
       << "      }\n"
       << "      if (bounced && player && entity == *player && bounce_clip)\n"
       << "        audio.play_effect(bounce_clip);\n"
       << "    }\n"
       << "    audio.service();\n\n"
       << "    DrawList draw;\n"
       << "    draw.overlay.push_back(\n"
       << "        FilledRectangle{{0, 0, w, h}, {8, 16, 26, 255}});\n"
       << "    for (std::size_t i = 0; i < entities.size(); ++i) {\n"
       << "      const auto *t = world.get<Transform>(entities[i]);\n"
       << "      const auto *ext = world.get<Extent>(entities[i]);\n"
       << "      const auto *tint = world.get<Tint>(entities[i]);\n"
       << "      if (!t || !ext || !tint) continue;\n"
       << "      if (i < sprites.size() && sprites[i])\n"
       << "        draw.overlay.push_back(\n"
       << "            Image{sprites[i], {t->x, t->y, ext->w, ext->h}});\n"
       << "      else\n"
       << "        draw.overlay.push_back(FilledRectangle{\n"
       << "            {t->x, t->y, ext->w, ext->h},\n"
       << "            {tint->r, tint->g, tint->b, 255}});\n"
       << "    }\n"
       << "    draw.overlay.push_back(Text{\n"
       << "        {w * .5f, h * .5f - 80.f}, \"" << display << "\",\n"
       << "        {86, 196, 255, 255}, 42, 0, std::nullopt, TextAlign::Center,\n"
       << "        FontFace::Heading});\n"
       << "    draw.overlay.push_back(Text{\n"
       << "        {w * .5f, h * .5f + 12.f},\n"
       << "        std::to_string(plan.order.size()) +\n"
       << "            \" content package(s), \" +\n"
       << "            std::to_string(cooked_assets) + \" cooked asset(s) - \" +\n"
       << "            (plan.ok ? std::string(\"load plan ok\")\n"
       << "                     : std::string(\"load plan FAILED\")),\n"
       << "        {210, 230, 244, 255}, 16, 0, std::nullopt,\n"
       << "        TextAlign::Center});\n"
       << "    draw.overlay.push_back(Text{\n"
       << "        {w * .5f, h * .5f + 40.f},\n"
       << "        \"drop content into packages/" << id << "/content/ and cook; \"\n"
       << "        \"an entity named 'player' follows WASD/arrow keys\",\n"
       << "        {122, 170, 190, 255}, 14, 0, std::nullopt,\n"
       << "        TextAlign::Center});\n"
       << "    window.draw(draw);\n"
       << "  }\n"
       << "  return plan.ok ? 0 : 1;\n"
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
        << (windowed ? "stellar::platform stellar::audio" : "stellar::engine")
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
