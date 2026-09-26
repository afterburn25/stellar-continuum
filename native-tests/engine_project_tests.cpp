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
    check(text.find("engine::SimulationExecutor") != std::string::npos &&
              text.find("sim.advance()") != std::string::npos,
          "windowed starter demonstrates the simulation executor");
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
    scene.entities[0].fcols = 2;
    scene.entities[0].anim_loop = false;
    scene.entities[0].vfx = "ember";
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
    scene.entities[0].parent = "carrier";
    scene.tilemaps.push_back(engine::SceneTilemap{});
    auto &tm0 = scene.tilemaps.back();
    tm0.tileset = "sprites/tiles.png";
    tm0.tile_w = 32;
    tm0.tile_h = 32;
    tm0.columns = 4;
    tm0.collide = true;
    tm0.cells = {0, -1, -1, 0, 0, 1, 1, 0};
    // A second grid layer — parallaxed decor above the collision map.
    scene.tilemaps.push_back(engine::SceneTilemap{});
    auto &tm1 = scene.tilemaps.back();
    tm1.tileset = "sprites/deco.png";
    tm1.x = 64.f;
    tm1.y = -128.f;
    tm1.tile_w = 16;
    tm1.tile_h = 16;
    tm1.columns = 8;
    tm1.layer = 5;
    tm1.parallax = 0.5f;
    tm1.cells.assign(16, 2);
    // Declarative emitter definition — entity `vfx` references resolve
    // against these without game code.
    scene.emitters.push_back(engine::SceneEmitterDef{});
    auto &em = scene.emitters.back();
    em.id = "ember";
    em.sprite = "sprites/dot.png";
    em.rate = 40.f;
    em.lifetime = 0.5f;
    em.vx_min = -30.f;
    em.vx_max = 30.f;
    em.vy_min = -60.f;
    em.vy_max = -20.f;
    em.spread_deg = 15.f;
    em.gy = 140.f;
    em.scale_keys = {{0.f, 1.f}, {1.f, 0.4f}};
    em.opacity_keys = {{0.f, 1.f}, {1.f, 0.f}};
    em.tint_r = {{0.f, 1.f}, {1.f, 0.9f}};
    em.tint_g = {{0.f, 0.85f}, {1.f, 0.2f}};
    em.tint_b = {{0.f, 0.3f}, {1.f, 0.05f}};
    em.max_particles = 64;
    em.lod_fade_distance = 800.f;
    em.lod_min_rate_scale = 0.25f;
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
              reparsed->entities[0].fcols == 2 &&
              !reparsed->entities[0].anim_loop &&
              reparsed->entities[0].vfx == "ember" &&
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
              reparsed->entities[0].parent == "carrier" &&
              reparsed->tilemaps.size() == 2 &&
              reparsed->tilemaps[0].tileset == "sprites/tiles.png" &&
              reparsed->tilemaps[0].columns == 4 &&
              reparsed->tilemaps[0].collide &&
              reparsed->tilemaps[0].cells.size() == 8 &&
              reparsed->tilemaps[0].cells[5] == 1 &&
              reparsed->tilemaps[1].tileset == "sprites/deco.png" &&
              reparsed->tilemaps[1].x == 64.f &&
              reparsed->tilemaps[1].y == -128.f &&
              reparsed->tilemaps[1].tile_w == 16 &&
              reparsed->tilemaps[1].layer == 5 &&
              reparsed->tilemaps[1].parallax == 0.5f &&
              reparsed->tilemaps[1].cells.size() == 16 &&
              reparsed->tilemaps[1].cells[0] == 2 &&
              reparsed->bg_r == 4 && reparsed->bg_g == 8 &&
              reparsed->bg_b == 40 && reparsed->gravity == 600.f &&
              reparsed->music == "audio/level1.ogg" &&
              reparsed->world_w == 2560.f && reparsed->world_h == 1440.f &&
              reparsed->emitters.size() == 1 &&
              reparsed->emitters[0].id == "ember" &&
              reparsed->emitters[0].sprite == "sprites/dot.png" &&
              reparsed->emitters[0].rate == 40.f &&
              reparsed->emitters[0].lifetime == 0.5f &&
              reparsed->emitters[0].vx_min == -30.f &&
              reparsed->emitters[0].vy_max == -20.f &&
              reparsed->emitters[0].spread_deg == 15.f &&
              reparsed->emitters[0].gy == 140.f &&
              reparsed->emitters[0].scale_keys.size() == 2 &&
              reparsed->emitters[0].scale_keys[1].second == 0.4f &&
              reparsed->emitters[0].opacity_keys.size() == 2 &&
              reparsed->emitters[0].tint_g.size() == 2 &&
              reparsed->emitters[0].max_particles == 64 &&
              reparsed->emitters[0].lod_fade_distance == 800.f &&
              reparsed->emitters[0].lod_min_rate_scale == 0.25f,
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
    // Legacy single-"tilemap" documents still parse, becoming a
    // one-element tilemaps array.
    const auto legacy_doc = engine::SceneDocument::from_json(
        R"({"entities":[],"tilemap":{"tileset":"sprites/tiles.png","tileW":32,"tileH":32,"columns":4,"collide":true,"cells":[0,-1,-1,0,0,1,1,0]}})");
    check(legacy_doc && legacy_doc->tilemaps.size() == 1 &&
              legacy_doc->tilemaps[0].tileset == "sprites/tiles.png" &&
              legacy_doc->tilemaps[0].columns == 4 &&
              legacy_doc->tilemaps[0].cells.size() == 8,
          "legacy singular tilemap parses into tilemaps");
    // A malformed entry inside the new tilemaps array rejects too.
    check(!engine::SceneDocument::from_json(
              R"({"entities":[],"tilemaps":[{"tileW":32,"tileH":32,"columns":4,"cells":[0,1,2]}]})")
              .has_value(),
          "bad tilemaps-array entry rejected");
    check(!engine::SceneDocument::from_json(
              R"({"entities":[],"emitters":[{"rate":10}]})")
              .has_value(),
          "id-less emitter rejected");
    check(!engine::SceneDocument::from_json(
              R"({"entities":[],"emitters":[{"id":"x","scale":[[0,1,2]]}]})")
              .has_value(),
          "malformed emitter curve rejected");
  }

  // Scene3dDocument: round-trip, malformed rejection, save/load.
  {
    engine::Scene3dDocument scene;
    engine::Scene3dEntity cube;
    cube.name = "crate";
    cube.mesh = "box:2,1,1";
    cube.x = 1.f;
    cube.y = 2.f;
    cube.z = -3.f;
    cube.yaw_deg = 45.f;
    cube.pitch_deg = 10.f;
    cube.roll_deg = -5.f;
    cube.scale = 2.f;
    cube.vx = 3.f;
    cube.vy = -1.f;
    cube.vz = 0.5f;
    cube.r = 200;
    cube.g = 100;
    cube.b = 50;
    cube.a = 200;
    cube.texture = "models/crate.png";
    cube.opacity = 0.75f;
    cube.double_sided = true;
    cube.gravity_scale = 0.5f;
    cube.solid = true;
    cube.ttl = 12.f;
    cube.data = "loot:gold";
    cube.parent = "ship";
    cube.vfx = "trail";
    cube.metallic = 0.9f;
    cube.roughness = 0.3f;
    cube.metallic_roughness = "models/crate_mr.png";
    cube.emissive = "models/crate_glow.png";
    cube.emissive_strength = 2.5f;
    cube.emissive_r = 1.f;
    cube.emissive_g = 0.4f;
    cube.emissive_b = 0.1f;
    cube.night_emissive = 1.f;
    cube.environment = "sky/nebula.png";
    cube.environment_strength = 0.8f;
    cube.alpha_cutout = 0.5f;
    cube.uv_tile_x = 2.f;
    cube.uv_tile_y = 4.f;
    cube.atmo_strength = 1.5f;
    cube.atmo_power = 2.5f;
    cube.atmo_night = 0.1f;
    cube.atmo_r = 0.3f;
    cube.atmo_g = 0.5f;
    cube.atmo_b = 0.9f;
    cube.visible_range = 250.f;
    cube.visible_fade = .1f;
    cube.normal_map = "maps/crate_n.png";
    cube.properties_map = "maps/crate_p.png";
    cube.cloud_map = "maps/crate_clouds.png";
    cube.normal_strength = 0.8f;
    cube.relief = 0.01f;
    cube.cloud_opacity = 0.6f;
    cube.cloud_albedo = 0.7f;
    cube.cloud_height = 0.04f;
    cube.cloud_offset_x = 0.25f;
    cube.cloud_offset_y = -0.5f;
    cube.terminator_wrap = 0.4f;
    cube.limb_darkening = 0.6f;
    cube.limb_darkening_q = 0.35f;
    cube.band_shear = -0.25f;
    cube.band_waves = 0.6f;
    cube.band_drift = 0.08f;
    cube.band_turbulence = 1.25f;
    cube.orbital_beaming = 0.7f;
    cube.star_kelvin = 3200.0;
    cube.accretion = {0.4f, 1.f, 9000.f, 0.8f};
    cube.forward_scatter = 0.5f;
    cube.volume_depth = 0.3f;
    cube.volume_density = 6.f;
    cube.volume_seed = 2.f;
    cube.volume_steps = 24;
    cube.volume_scatter = 0.5f;
    cube.volume_flow = 1.5f;
    cube.volume_distort = 0.05f;
    cube.volume_blend = 0.4f;
    cube.volume_image2 = "maps/nebula_b.png";
    cube.volume_occlude = 0.6f;
    cube.volume_flow_rate = 0.4f;
    cube.lod_meshes = {"models/crate_mid.obj", "models/crate_low.obj"};
    cube.lod_pixels = 48.f;
    cube.lod_fade = 0.3f;
    cube.lod_group = "fleet";
    cube.lod_proxy = "card:4,4";
    cube.lod_proxy_pixels = 24.f;
    scene.entities.push_back(cube);
    engine::Scene3dEntity ship;
    ship.name = "ship";
    ship.mesh = "models/ship.obj";
    ship.z = 10.f;
    scene.entities.push_back(ship);
    scene.cam_x = 0.f;
    scene.cam_y = 2.f;
    scene.cam_z = 8.f;
    scene.cam_yaw_deg = 30.f;
    scene.cam_pitch_deg = -15.f;
    scene.fov_deg = 75.f;
    scene.near_plane = 0.05f;
    scene.far_plane = 500.f;
    scene.light_x = -0.3f;
    scene.light_y = 0.8f;
    scene.light_z = 0.4f;
    scene.light_intensity = 1.4f;
    scene.bg_r = 4;
    scene.bg_g = 8;
    scene.bg_b = 20;
    scene.gravity = 9.8f;
    scene.ground_y = -2.f;
    scene.bounds = 40.f;
    scene.music = "audio/space.ogg";
    scene.emitters.push_back(engine::SceneEmitterDef{});
    scene.emitters.back().id = "trail";
    scene.emitters.back().rate = 12.f;
    engine::Scene3dLight fill;
    fill.dir_x = -0.5f;
    fill.dir_y = 0.1f;
    fill.dir_z = -0.8f;
    fill.r = 0.3f;
    fill.g = 0.5f;
    fill.b = 1.0f;
    fill.intensity = 0.6f;
    scene.lights.push_back(fill);
    engine::Scene3dPointLight lamp;
    lamp.x = 1.f; lamp.y = 2.f; lamp.z = -1.f;
    lamp.r = 0.2f; lamp.g = 1.f; lamp.b = 0.4f;
    lamp.intensity = 3.f;
    lamp.range = 12.f;
    lamp.spot_x = 0.f; lamp.spot_y = 0.f; lamp.spot_z = -1.f;
    lamp.spot_inner = 0.97f; lamp.spot_outer = 0.9f;
    scene.point_lights.push_back(lamp);
    scene.exposure = 1.25f;
    scene.bloom = 0.6f;
    scene.bloom_threshold = 0.8f;
    scene.contrast = 1.1f;
    scene.saturation = 0.9f;
    scene.sharpen = 0.3f;
    scene.vignette = 0.4f;
    scene.quality = "ultra";
    scene.debug_view = "normals";
    scene.shadow_extent = 32.f;
    scene.shadow_distance = 48.f;
    scene.shadow_depth = 128.f;
    scene.shadow_strength = 0.7f;
    scene.shadow_bias = 0.001f;
    scene.shadow_resolution = 2048;
    const auto reparsed =
        engine::Scene3dDocument::from_json(scene.to_json());
    check(reparsed.has_value(), "scene3d json round-trips");
    if (reparsed) {
      check(reparsed->entities.size() == 2, "scene3d entity count");
      const auto &rc = reparsed->entities[0];
      check(rc.name == "crate" && rc.mesh == "box:2,1,1" &&
                rc.x == 1.f && rc.y == 2.f && rc.z == -3.f &&
                rc.yaw_deg == 45.f && rc.pitch_deg == 10.f &&
                rc.roll_deg == -5.f && rc.scale == 2.f &&
                rc.vx == 3.f && rc.vy == -1.f && rc.vz == 0.5f &&
                rc.r == 200 && rc.g == 100 && rc.b == 50 &&
                rc.a == 200 && rc.texture == "models/crate.png" &&
                rc.opacity == 0.75f && rc.double_sided &&
                rc.gravity_scale == 0.5f && rc.solid &&
                rc.ttl == 12.f && rc.data == "loot:gold" &&
                rc.parent == "ship" && rc.vfx == "trail",
            "scene3d entity fields round-trip");
      check(reparsed->cam_y == 2.f && reparsed->cam_z == 8.f &&
                reparsed->cam_yaw_deg == 30.f &&
                reparsed->cam_pitch_deg == -15.f &&
                reparsed->fov_deg == 75.f &&
                reparsed->near_plane == 0.05f &&
                reparsed->far_plane == 500.f,
            "scene3d camera round-trips");
      check(reparsed->light_x == -0.3f && reparsed->light_y == 0.8f &&
                reparsed->light_z == 0.4f &&
                reparsed->light_intensity == 1.4f,
            "scene3d light round-trips");
      check(reparsed->bg_r == 4 && reparsed->bg_g == 8 &&
                reparsed->bg_b == 20 && reparsed->gravity == 9.8f &&
                reparsed->ground_y == -2.f && reparsed->bounds == 40.f &&
                reparsed->music == "audio/space.ogg" &&
                reparsed->emitters.size() == 1 &&
                reparsed->emitters[0].id == "trail" &&
                reparsed->emitters[0].rate == 12.f,
            "scene3d world fields round-trip");
      check(reparsed->lights.size() == 1 &&
                reparsed->lights[0].dir_x == -0.5f &&
                reparsed->lights[0].dir_y == 0.1f &&
                reparsed->lights[0].dir_z == -0.8f &&
                reparsed->lights[0].r == 0.3f &&
                reparsed->lights[0].g == 0.5f &&
                reparsed->lights[0].b == 1.0f &&
                reparsed->lights[0].intensity == 0.6f,
            "scene3d fill light round-trips");
      check(rc.metallic == 0.9f && rc.roughness == 0.3f &&
                rc.metallic_roughness == "models/crate_mr.png" &&
                rc.emissive == "models/crate_glow.png" &&
                rc.emissive_strength == 2.5f && rc.emissive_r == 1.f &&
                rc.emissive_g == 0.4f && rc.emissive_b == 0.1f &&
                rc.night_emissive == 1.f &&
                rc.environment == "sky/nebula.png" &&
                rc.environment_strength == 0.8f && rc.alpha_cutout == 0.5f &&
                rc.uv_tile_x == 2.f && rc.uv_tile_y == 4.f &&
                rc.atmo_strength == 1.5f && rc.atmo_power == 2.5f &&
                rc.atmo_night == 0.1f && rc.atmo_r == 0.3f &&
                rc.atmo_g == 0.5f && rc.atmo_b == 0.9f &&
                rc.visible_range == 250.f && rc.visible_fade == .1f,
            "scene3d pbr/atmosphere/cull fields round-trip");
      check(rc.normal_map == "maps/crate_n.png" &&
                rc.properties_map == "maps/crate_p.png" &&
                rc.cloud_map == "maps/crate_clouds.png" &&
                rc.normal_strength == 0.8f && rc.relief == 0.01f &&
                rc.cloud_opacity == 0.6f && rc.cloud_albedo == 0.7f &&
                rc.cloud_height == 0.04f &&
                rc.cloud_offset_x == 0.25f && rc.cloud_offset_y == -0.5f &&
                rc.terminator_wrap == 0.4f && rc.limb_darkening == 0.6f &&
                rc.limb_darkening_q == 0.35f &&
                rc.band_shear == -0.25f && rc.band_waves == 0.6f &&
                rc.band_drift == 0.08f && rc.band_turbulence == 1.25f &&
                rc.orbital_beaming == 0.7f &&
                rc.star_kelvin == 3200.0 && rc.accretion[0] == 0.4f &&
                rc.accretion[1] == 1.f && rc.accretion[2] == 9000.f &&
                rc.accretion[3] == 0.8f && rc.forward_scatter == 0.5f,
            "scene3d surface-response fields round-trip");
      check(rc.lod_meshes.size() == 2 &&
                rc.lod_meshes[0] == "models/crate_mid.obj" &&
                rc.lod_meshes[1] == "models/crate_low.obj" &&
                rc.lod_pixels == 48.f && rc.lod_fade == 0.3f,
            "scene3d mesh LOD chain round-trips");
      check(rc.lod_group == "fleet" && rc.lod_proxy == "card:4,4" &&
                rc.lod_proxy_pixels == 24.f,
            "scene3d group proxy fields round-trip");
      check(rc.volume_depth == 0.3f && rc.volume_density == 6.f &&
                rc.volume_seed == 2.f && rc.volume_steps == 24 &&
                rc.volume_scatter == 0.5f && rc.volume_flow == 1.5f &&
                rc.volume_distort == 0.05f && rc.volume_blend == 0.4f &&
                rc.volume_image2 == "maps/nebula_b.png" &&
                rc.volume_occlude == 0.6f && rc.volume_flow_rate == 0.4f,
            "scene3d emission-volume block round-trips");
      check(reparsed->point_lights.size() == 1 &&
                reparsed->point_lights[0].x == 1.f &&
                reparsed->point_lights[0].z == -1.f &&
                reparsed->point_lights[0].g == 1.f &&
                reparsed->point_lights[0].intensity == 3.f &&
                reparsed->point_lights[0].range == 12.f &&
                reparsed->point_lights[0].spot_z == -1.f &&
                reparsed->point_lights[0].spot_inner == 0.97f &&
                reparsed->point_lights[0].spot_outer == 0.9f,
            "scene3d point lights round-trip");
      check(reparsed->exposure == 1.25f && reparsed->bloom == 0.6f &&
                reparsed->bloom_threshold == 0.8f &&
                reparsed->contrast == 1.1f && reparsed->saturation == 0.9f &&
                reparsed->sharpen == 0.3f && reparsed->vignette == 0.4f &&
                reparsed->quality == "ultra" &&
                reparsed->debug_view == "normals",
            "scene3d render options round-trip");
      check(reparsed->shadow_extent == 32.f && reparsed->shadow_distance == 48.f &&
                reparsed->shadow_depth == 128.f && reparsed->shadow_strength == 0.7f &&
                reparsed->shadow_bias == 0.001f && reparsed->shadow_resolution == 2048,
            "scene3d shadow map settings round-trip");
      check(reparsed->entities[1].metallic == 0.f &&
                reparsed->entities[1].emissive_strength == 0.f &&
                reparsed->entities[1].atmo_strength == 0.f &&
                reparsed->entities[1].environment.empty(),
            "unset material fields keep neutral defaults");
      const auto path = root / "editor" / "scene3d.json";
      scene.save(path);
      const auto loaded = engine::Scene3dDocument::load(path);
      check(loaded.has_value() && loaded->entities.size() == 2 &&
                loaded->entities[1].mesh == "models/ship.obj",
            "scene3d save/load round-trips");
    }
    check(!engine::Scene3dDocument::from_json("{not json").has_value(),
          "scene3d malformed json rejected");
    check(!engine::Scene3dDocument::from_json(R"({"camera":{}})")
              .has_value(),
          "scene3d missing entities rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x"}]})")
              .has_value(),
          "scene3d entity without pos rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2]}]})")
              .has_value(),
          "scene3d short pos rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"camera":{"fov":190}})")
              .has_value(),
          "scene3d bad fov rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"camera":{"near":10,"far":5}})")
              .has_value(),
          "scene3d near>=far rejected");
    check(!engine::Scene3dDocument::load(root / "nonexistent3d.json")
              .has_value(),
          "scene3d missing file rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"render":{"quality":"extreme"}})")
              .has_value(),
          "scene3d unknown quality tier rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"render":{"shadow":{"extent":4,"depth":0}}})")
              .has_value(),
          "scene3d nonpositive shadow depth rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"render":{"shadow":{"extent":4,"strength":2}}})")
              .has_value(),
          "scene3d shadow strength above one rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"render":{"shadow":{"extent":4,"resolution":16}}})")
              .has_value(),
          "scene3d undersized shadow resolution rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[],"pointLights":[{"spotDir":[0,0,-1],"spotInner":0.9,"spotOuter":0.95}]})")
              .has_value(),
          "scene3d spot cone with outer>inner rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[],"pointLights":[{"spotDir":[0,0,-1],"spotInner":1.5}]})")
              .has_value(),
          "scene3d spot inner above 1 rejected");
    check(engine::Scene3dDocument::from_json(
              R"({"entities":[],"pointLights":[{"spotDir":[0,0,-1],"spotInner":0.97,"spotOuter":0.9}]})")
              .has_value(),
          "scene3d valid spot cone rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[],"pointLights":[{"castShadow":true}]})")
              .has_value(),
          "scene3d omni castShadow rejected");
    if (auto shadowed = engine::Scene3dDocument::from_json(
            R"({"entities":[],"pointLights":[{"spotDir":[0,0,-1],"spotInner":0.97,"spotOuter":0.9,"castShadow":true}]})")) {
      check(shadowed->point_lights.size() == 1 &&
                shadowed->point_lights[0].cast_shadow,
            "scene3d dropped a valid shadowed spot");
      check(shadowed->to_json().find("castShadow") != std::string::npos,
            "scene3d did not round-trip castShadow");
    } else {
      check(false, "scene3d shadowed spot light rejected");
    }
    if (auto probed = engine::Scene3dDocument::from_json(
            R"({"entities":[],"environment":"visual/starfield-equirect.png"})")) {
      check(probed->environment == "visual/starfield-equirect.png",
            "scene3d dropped its environment probe path");
      check(probed->to_json().find("environment") != std::string::npos,
            "scene3d did not round-trip the environment key");
    } else {
      check(false, "scene3d environment probe rejected");
    }
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"pointLights":[{},{},{},{},{}]})")
              .has_value(),
          "scene3d over-budget point lights rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3]}],"render":{"debug":"wireframe"}})")
              .has_value(),
          "scene3d unknown debug view rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"range":-5}]})")
              .has_value(),
          "scene3d negative visible range rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"range":250,"visibleFade":0.7}]})")
              .has_value(),
          "scene3d visibleFade above 0.5 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"range":250,"visibleFade":-0.1}]})")
              .has_value(),
          "scene3d visibleFade below zero rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"uvTile":[2]}]})")
              .has_value(),
          "scene3d short uvTile rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"surface":{"cloudOpacity":0.5}}]})")
              .has_value(),
          "scene3d surface without maps rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"surface":{"cloud":"c.png","cloudAlbedo":2}}]})")
              .has_value(),
          "scene3d cloud albedo above one rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"surface":{"cloud":"c.png","cloudHeight":0.5}}]})")
              .has_value(),
          "scene3d cloud height above bound rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"terminatorWrap":3}]})")
              .has_value(),
          "scene3d terminator wrap above one rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"limbDarken":1.5}]})")
              .has_value(),
          "scene3d limb darkening above one rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"limbDarkenQ":1.5}]})")
              .has_value(),
          "scene3d quadratic limb darkening above one rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"limbDarkenQ":-0.1}]})")
              .has_value(),
          "scene3d quadratic limb darkening below zero rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lods":"a,b"}]})")
              .has_value(),
          "scene3d non-array lods rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lods":["a","b"],"lodPixels":0}]})")
              .has_value(),
          "scene3d lodPixels below range rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lodFade":0.7}]})")
              .has_value(),
          "scene3d lodFade above 0.5 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lodFade":-0.1}]})")
              .has_value(),
          "scene3d lodFade below zero rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lodGroup":"f","lodProxyPixels":0}]})")
              .has_value(),
          "scene3d lodProxyPixels below range rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"lodGroup":"f","lodProxyPixels":8192}]})")
              .has_value(),
          "scene3d lodProxyPixels above range rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandShear":0.9}]})")
              .has_value(),
          "scene3d band shear above 0.5 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandWaves":1.2}]})")
              .has_value(),
          "scene3d band waves above 1 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandWaves":-0.1}]})")
              .has_value(),
          "scene3d band waves below 0 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandDrift":0.3}]})")
              .has_value(),
          "scene3d band drift above 0.25 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandDrift":-0.3}]})")
              .has_value(),
          "scene3d band drift below -0.25 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandTurbulence":9}]})")
              .has_value(),
          "scene3d band turbulence above 8 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"bandTurbulence":-9}]})")
              .has_value(),
          "scene3d band turbulence below -8 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"orbitalBeam":-1.2}]})")
              .has_value(),
          "scene3d orbital beaming below -1 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"starKelvin":80}]})")
              .has_value(),
          "scene3d star kelvin below 100 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"accretion":[0.5,0.2,8000,0.8]}]})")
              .has_value(),
          "scene3d accretion with inverted radii rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"accretion":[0.4,1,8000,1.5]}]})")
              .has_value(),
          "scene3d accretion beaming above 1 rejected");
    check(engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"accretion":[0.4,1,8000,0.8]}]})")
              .has_value(),
          "scene3d accretion preset rejected a legal disc");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"forwardScatter":1.4}]})")
              .has_value(),
          "scene3d forward scatter above 1 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.9}}]})")
              .has_value(),
          "scene3d volume depth above 0.75 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"volume":{"depth":0.3}}]})")
              .has_value(),
          "scene3d volume without an emission texture rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"steps":4}}]})")
              .has_value(),
          "scene3d volume steps below 8 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"distort":0.5}}]})")
              .has_value(),
          "scene3d volume distort above 0.1 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"flow":2e5}}]})")
              .has_value(),
          "scene3d volume flow above bound rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"flowRate":80}}]})")
              .has_value(),
          "scene3d volume flowRate above bound rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"blend":1.2,"image2":"a.png"}}]})")
              .has_value(),
          "scene3d volume blend above 1 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"blend":0.5}}]})")
              .has_value(),
          "scene3d volume blend without image2 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"image2":5}}]})")
              .has_value(),
          "scene3d volume non-string image2 rejected");
    check(!engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"occlude":-1}}]})")
              .has_value(),
          "scene3d volume negative occlude rejected");
    check(engine::Scene3dDocument::from_json(
              R"({"entities":[{"name":"x","pos":[1,2,3],"texture":"t.png","volume":{"depth":0.3,"density":8,"steps":48,"scatter":0.7}}]})")
              .has_value(),
          "scene3d volume rejected a legal nebula block");
  }

  if (failures == 0) std::cout << "engine_project tests passed\n";
  return failures == 0 ? 0 : 1;
}
