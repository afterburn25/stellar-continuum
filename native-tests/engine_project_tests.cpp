// Engine project manifest/scaffold seam: sanitize_project_id,
// create_project layout, EngineProject::load validation, find_projects.

#include <stellar/engine/package.hpp>
#include <stellar/engine/project.hpp>
#include <stellar/engine/scene_document.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *label) {
  if (!condition) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}

std::filesystem::path make_temp_dir(const char *name) {
  const auto dir =
      std::filesystem::temp_directory_path() / "stellar_engine_project_tests" /
      name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

} // namespace

int main() {
  namespace engine = stellar::engine;

  check(engine::sanitize_project_id("My Cool Game") == "game.my-cool-game",
        "slugify basic name");
  check(engine::sanitize_project_id("") == "game.project",
        "empty name fallback");
  check(engine::sanitize_project_id("---") == "game.project",
        "separator-only fallback");
  check(engine::sanitize_project_id("Alpha  Protocol 2") ==
            "game.alpha-protocol-2",
        "collapsed separators");

  // Scaffold: manifest + owned-namespace package + starter source.
  const auto root = make_temp_dir("scaffold");
  std::string error;
  check(engine::create_project(root, "Test Game", "0.1.64", &error),
        "create_project succeeds");
  check(error.empty(), "no error on success");
  check(std::filesystem::exists(root / "project.stellar.json"),
        "manifest written");
  check(std::filesystem::exists(root / "packages" / "game.test-game" /
                                "package.json"),
        "base package manifest written");
  check(std::filesystem::is_directory(root / "packages" / "game.test-game" /
                                      "content"),
        "content directory created");
  check(std::filesystem::exists(root / "src" / "main.cpp"),
        "starter host source written");
  check(std::filesystem::exists(root / "CMakeLists.txt"),
        "consumer CMakeLists written");
  {
    std::ifstream input(root / "CMakeLists.txt");
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    check(text.find("StellarEngineSdk.cmake") != std::string::npos &&
              text.find("stellar::runtime") != std::string::npos,
          "CMakeLists consumes the engine SDK runtime");
  }
  check(std::filesystem::exists(root / ".gitignore"),
        "scaffold writes .gitignore");
  {
    std::ifstream input(root / "src" / "main.cpp");
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    check(text.find("engine::RuntimeHost") != std::string::npos &&
              text.find("host.run(argc, argv)") != std::string::npos &&
              text.find("on_update") != std::string::npos,
          "windowed starter is a RuntimeHost client");
  }

  // The blank template emits a console host linking stellar::engine only.
  const auto blank = make_temp_dir("blank");
  check(engine::create_project(blank, "Blank Game", "0.1.64", &error,
                               engine::kTemplateBlank),
        "blank template scaffolds");
  {
    std::ifstream input(blank / "CMakeLists.txt");
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    check(text.find("stellar::engine") != std::string::npos &&
              text.find("stellar::platform") == std::string::npos,
          "blank template links engine only");
  }
  {
    std::ifstream input(blank / "src" / "main.cpp");
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    check(text.find("native_map_platform") == std::string::npos,
          "blank host has no window dependency");
  }
  check(!engine::create_project(make_temp_dir("bogus"), "X", "0.1.64",
                                &error, "no-such-template"),
        "unknown template rejected");

  // The scaffolded package manifest parses and owns the project namespace.
  {
    std::ifstream input(root / "packages" / "game.test-game" / "package.json");
    const std::string text{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    const auto manifest = engine::PackageManifest::parse(text, &error);
    check(manifest.has_value(), "scaffolded package.json parses");
    check(manifest && manifest->id == "game.test-game", "package id matches");
    check(manifest && manifest->provides.size() == 1 &&
              manifest->provides[0] == "game.test-game",
          "package owns project namespace");
  }

  // Refuse to overwrite an existing project.
  check(!engine::create_project(root, "Other", "0.1.64", &error),
        "create refuses existing manifest");
  check(!error.empty(), "refusal reports error");

  // Load round-trips the scaffolded manifest.
  error.clear();
  const auto loaded = engine::EngineProject::load(root, &error);
  check(loaded.has_value(), "load parses scaffolded manifest");
  if (loaded) {
    check(loaded->name == "Test Game", "name round-trips");
    check(loaded->id == "game.test-game", "id round-trips");
    check(loaded->engine_version == "0.1.64", "engine version recorded");
    check(loaded->content_dirs.size() == 1 &&
              loaded->content_dirs[0] == "packages",
          "content dir defaults to packages");
    // save() atomically rewrites the manifest; rename round-trips.
    auto renamed = *loaded;
    renamed.name = "Renamed Game";
    renamed.save();
    const auto reloaded = engine::EngineProject::load(root, &error);
    check(reloaded && reloaded->name == "Renamed Game" &&
              reloaded->id == "game.test-game",
          "save persists rename, keeps id");
  }

  // Malformed inputs reject cleanly.
  const auto bad = make_temp_dir("bad");
  {
    std::ofstream out(bad / "project.stellar.json");
    out << "{not json";
  }
  check(!engine::EngineProject::load(bad, &error).has_value(),
        "malformed json rejected");
  check(!engine::EngineProject::load(bad / "nonexistent", &error).has_value(),
        "missing manifest rejected");

  const auto no_id = make_temp_dir("no_id");
  {
    std::ofstream out(no_id / "project.stellar.json");
    out << R"({"schemaVersion":1,"name":"x"})";
  }
  check(!engine::EngineProject::load(no_id).has_value(), "missing id rejected");

  const auto bad_content = make_temp_dir("bad_content");
  {
    std::ofstream out(bad_content / "project.stellar.json");
    out << R"({"schemaVersion":1,"name":"x","id":"game.x","content":["../up"]})";
  }
  check(!engine::EngineProject::load(bad_content).has_value(),
        "escaping content dir rejected");

  const auto legacy = make_temp_dir("legacy");
  {
    std::ofstream out(legacy / "project.stellar.json");
    out << R"({"schemaVersion":1,"name":"x","id":"game.x"})";
  }
  const auto legacy_loaded = engine::EngineProject::load(legacy);
  check(legacy_loaded && legacy_loaded->content_dirs.size() == 1,
        "missing content array defaults");

  // find_projects discovers manifest-bearing children only.
  const auto parents = make_temp_dir("parents");
  engine::create_project(parents / "alpha", "Alpha", "0.1.64", nullptr);
  std::filesystem::create_directories(parents / "not-a-project");
  const auto found = engine::find_projects(parents);
  check(found.size() == 1 && found[0].filename() == "alpha",
        "find_projects filters on manifest presence");

  // SceneDocument: round-trip, malformed rejection, atomic save/load.
  {
    engine::SceneDocument scene;
    scene.bg_r = 4; scene.bg_g = 8; scene.bg_b = 40;
    scene.gravity = 600.f;
    scene.music = "audio/level1.ogg";
    scene.world_w = 2560.f;
    scene.world_h = 1440.f;
    scene.entities.push_back(
        engine::SceneEntity{"box", 10.f, 20.f, 64.f, 32.f, 100.f, 50.f,
                            255, 128, 0, "data/logo.png", -2, 0.5f,
                            "score", 0.0f, true});
    scene.entities[0].frames = 4;
    scene.entities[0].fps = 6.0f;
    scene.entities[0].rotation = 45.0f;
    scene.entities[0].ttl = 2.5f;
    scene.entities[0].flip_x = true;
    scene.entities[0].flip_y = true;
    scene.entities[0].visible = false;
    scene.entities[0].oneway = true;
    scene.entities[0].data = "checkpoint-7";
    scene.entities[0].opacity = 0.5f;
    scene.entities[0].spin = 90.f;
    scene.entities[0].bounce = false;
    scene.tilemap = engine::SceneTilemap{};
    scene.tilemap->tileset = "sprites/tiles.png";
    scene.tilemap->tile_w = 32;
    scene.tilemap->tile_h = 32;
    scene.tilemap->columns = 4;
    scene.tilemap->collide = true;
    scene.tilemap->cells = {0, -1, -1, 0, 0, 1, 1, 0};
    const auto reparsed = engine::SceneDocument::from_json(scene.to_json());
    check(reparsed && reparsed->entities.size() == 1 &&
              reparsed->entities[0].name == "box" &&
              reparsed->entities[0].vx == 100.f &&
              reparsed->entities[0].r == 255 && reparsed->entities[0].g == 128 &&
              reparsed->entities[0].sprite == "data/logo.png" &&
              reparsed->entities[0].layer == -2 &&
              reparsed->entities[0].parallax == 0.5f &&
              reparsed->entities[0].text == "score" &&
              reparsed->entities[0].gravity_scale == 0.0f &&
              reparsed->entities[0].solid &&
              reparsed->entities[0].frames == 4 &&
              reparsed->entities[0].fps == 6.0f &&
              reparsed->entities[0].rotation == 45.0f &&
              reparsed->entities[0].ttl == 2.5f &&
              reparsed->entities[0].flip_x &&
              reparsed->entities[0].flip_y &&
              !reparsed->entities[0].visible &&
              reparsed->entities[0].oneway &&
              reparsed->entities[0].data == "checkpoint-7" &&
              reparsed->entities[0].opacity == 0.5f &&
              reparsed->entities[0].spin == 90.f &&
              !reparsed->entities[0].bounce &&
              reparsed->tilemap &&
              reparsed->tilemap->tileset == "sprites/tiles.png" &&
              reparsed->tilemap->columns == 4 && reparsed->tilemap->collide &&
              reparsed->tilemap->cells.size() == 8 &&
              reparsed->tilemap->cells[5] == 1 &&
              reparsed->bg_r == 4 && reparsed->bg_g == 8 &&
              reparsed->bg_b == 40 && reparsed->gravity == 600.f &&
              reparsed->music == "audio/level1.ogg" &&
              reparsed->world_w == 2560.f && reparsed->world_h == 1440.f,
          "scene document round-trips");
    const auto path = root / "editor" / "scene.json";
    scene.save(path);
    const auto loaded_scene = engine::SceneDocument::load(path);
    check(loaded_scene && loaded_scene->entities.size() == 1,
          "scene save/load round-trips");
    check(!engine::SceneDocument::from_json("{not json").has_value(),
          "malformed scene rejected");
    check(!engine::SceneDocument::from_json(R"({"entities":[{"x":1,"y":2}]})")
              .has_value(),
          "nameless entity rejected");
    check(!engine::SceneDocument::load(root / "nonexistent.json").has_value(),
          "missing scene file rejected");
    check(!engine::SceneDocument::from_json(
              R"({"entities":[],"tilemap":{"tileW":32,"tileH":32,"columns":4,"cells":[0,1,2]}})")
              .has_value(),
          "tilemap cell count not divisible by columns rejected");
    check(!engine::SceneDocument::from_json(
              R"({"entities":[],"tilemap":{"tileW":0,"tileH":32,"columns":4,"cells":[0,1,2,3]}})")
              .has_value(),
          "tilemap with non-positive tile size rejected");
  }

  if (failures == 0) std::cout << "engine_project tests passed\n";
  return failures == 0 ? 0 : 1;
}
