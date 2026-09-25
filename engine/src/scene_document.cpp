#include <stellar/engine/scene_document.hpp>

#include <stellar/engine/atomic_file_write.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <span>

namespace stellar::engine {
namespace {

nlohmann::json keys_json(const std::vector<std::pair<float, float>> &keys) {
  nlohmann::json out = nlohmann::json::array();
  for (const auto &[t, v] : keys) out.push_back({t, v});
  return out;
}

nlohmann::json emitters_to_json(const std::vector<SceneEmitterDef> &emitters) {
  nlohmann::json list = nlohmann::json::array();
  for (const auto &em : emitters) {
    nlohmann::json e;
    e["id"] = em.id;
    if (!em.sprite.empty()) e["sprite"] = em.sprite;
    e["rate"] = em.rate;
    e["lifetime"] = em.lifetime;
    e["vmin"] = {em.vx_min, em.vy_min};
    e["vmax"] = {em.vx_max, em.vy_max};
    if (em.spread_deg != 0.f) e["spread"] = em.spread_deg;
    if (em.gx != 0.f || em.gy != 0.f) e["egravity"] = {em.gx, em.gy};
    if (!em.scale_keys.empty()) e["scale"] = keys_json(em.scale_keys);
    if (!em.opacity_keys.empty()) e["opacity"] = keys_json(em.opacity_keys);
    if (!em.tint_r.empty()) e["tintR"] = keys_json(em.tint_r);
    if (!em.tint_g.empty()) e["tintG"] = keys_json(em.tint_g);
    if (!em.tint_b.empty()) e["tintB"] = keys_json(em.tint_b);
    if (em.max_particles != 256) e["max"] = em.max_particles;
    if (em.lod_fade_distance != 0.f) e["lodFade"] = em.lod_fade_distance;
    if (em.lod_min_rate_scale != 0.f) e["lodMin"] = em.lod_min_rate_scale;
    list.push_back(std::move(e));
  }
  return list;
}

// Shared emitter-array parse; errors go to `error` like the doc parsers.
bool parse_emitters(const nlohmann::json &list,
                    std::vector<SceneEmitterDef> &out, std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error != nullptr) *error = message;
    return false;
  };
  if (!list.is_array()) return fail("emitters must be an array");
  const auto keys_of = [&fail](const nlohmann::json &em, const char *name,
                               auto &out) -> bool {
    if (!em.contains(name)) return true;
    const auto &arr = em.at(name);
    if (!arr.is_array())
      return fail(std::string("emitter curve ") + name + " must be an array");
    for (const auto &k : arr) {
      if (!k.is_array() || k.size() != 2)
        return fail(std::string("emitter curve ") + name +
                    " keys must be [time,value] pairs");
      out.emplace_back(k[0].get<float>(), k[1].get<float>());
    }
    return true;
  };
  const auto vec2_of = [&fail](const nlohmann::json &em, const char *name,
                               float &x, float &y) -> bool {
    if (!em.contains(name)) return true;
    const auto &arr = em.at(name);
    if (!arr.is_array() || arr.size() != 2)
      return fail(std::string("emitter ") + name + " must be [x,y]");
    x = arr[0].get<float>();
    y = arr[1].get<float>();
    return true;
  };
  for (const auto &item : list) {
    if (!item.is_object()) return fail("emitter entry is not an object");
    SceneEmitterDef em;
    em.id = item.value("id", std::string{});
    if (em.id.empty()) return fail("emitter requires an id");
    em.sprite = item.value("sprite", std::string{});
    em.rate = item.value("rate", 0.0f);
    em.lifetime = item.value("lifetime", 1.0f);
    em.spread_deg = item.value("spread", 0.0f);
    em.max_particles = item.value("max", 256u);
    em.lod_fade_distance = item.value("lodFade", 0.0f);
    em.lod_min_rate_scale = item.value("lodMin", 0.0f);
    if (!vec2_of(item, "vmin", em.vx_min, em.vy_min) ||
        !vec2_of(item, "vmax", em.vx_max, em.vy_max) ||
        !vec2_of(item, "egravity", em.gx, em.gy) ||
        !keys_of(item, "scale", em.scale_keys) ||
        !keys_of(item, "opacity", em.opacity_keys) ||
        !keys_of(item, "tintR", em.tint_r) ||
        !keys_of(item, "tintG", em.tint_g) ||
        !keys_of(item, "tintB", em.tint_b))
      return false;
    if (em.rate < 0.f || em.lifetime <= 0.f || em.max_particles == 0)
      return fail("emitter requires rate>=0, lifetime>0, max>0");
    out.push_back(std::move(em));
  }
  return true;
}

nlohmann::json animations_to_json(
    const std::vector<SceneAnimationDef> &animations) {
  nlohmann::json list = nlohmann::json::array();
  for (const auto &def : animations) {
    nlohmann::json a;
    a["id"] = def.id;
    if (def.loop != "loop") a["loop"] = def.loop;
    nlohmann::json tracks = nlohmann::json::object();
    for (const auto &[channel, keys] : def.tracks)
      tracks[channel] = keys_json(keys);
    a["tracks"] = std::move(tracks);
    if (!def.events.empty()) {
      nlohmann::json events = nlohmann::json::array();
      for (const auto &[t, name] : def.events)
        events.push_back({t, name});
      a["events"] = std::move(events);
    }
    list.push_back(std::move(a));
  }
  return list;
}

bool parse_animations(const nlohmann::json &list,
                      std::vector<SceneAnimationDef> &out,
                      std::string *error) {
  auto fail = [&](const std::string &message) {
    if (error != nullptr) *error = message;
    return false;
  };
  if (!list.is_array()) return fail("animations must be an array");
  for (const auto &item : list) {
    if (!item.is_object()) return fail("animation entry is not an object");
    SceneAnimationDef def;
    def.id = item.value("id", std::string{});
    if (def.id.empty()) return fail("animation requires an id");
    def.loop = item.value("loop", std::string{"loop"});
    if (def.loop != "once" && def.loop != "loop" && def.loop != "pingpong")
      return fail("animation loop must be once|loop|pingpong");
    const auto &tracks = item.value("tracks", nlohmann::json::object());
    if (!tracks.is_object())
      return fail("animation tracks must be an object");
    for (const auto &[channel, keys] : tracks.items()) {
      if (!keys.is_array())
        return fail("animation track " + channel + " must be an array");
      std::vector<std::pair<float, float>> parsed;
      for (const auto &k : keys) {
        if (!k.is_array() || k.size() != 2)
          return fail("animation track " + channel +
                      " keys must be [time,value] pairs");
        parsed.emplace_back(k[0].get<float>(), k[1].get<float>());
      }
      def.tracks.emplace_back(channel, std::move(parsed));
    }
    for (const auto &e :
         item.value("events", nlohmann::json::array())) {
      if (!e.is_array() || e.size() != 2)
        return fail("animation events must be [time,name] pairs");
      def.events.emplace_back(e[0].get<float>(), e[1].get<std::string>());
    }
    out.push_back(std::move(def));
  }
  return true;
}

} // namespace

std::string SceneDocument::to_json() const {
  nlohmann::json doc;
  doc["schemaVersion"] = 1;
  auto &items = doc["entities"] = nlohmann::json::array();
  for (const auto &e : entities) {
    nlohmann::json item;
    item["name"] = e.name;
    item["x"] = e.x;
    item["y"] = e.y;
    item["w"] = e.w;
    item["h"] = e.h;
    item["vx"] = e.vx;
    item["vy"] = e.vy;
    item["color"] = {e.r, e.g, e.b};
    if (!e.sprite.empty()) item["sprite"] = e.sprite;
    if (e.layer != 0) item["layer"] = e.layer;
    if (e.parallax != 1.0f) item["parallax"] = e.parallax;
    if (!e.text.empty()) item["text"] = e.text;
    if (e.gravity_scale != 1.0f) item["gravityScale"] = e.gravity_scale;
    if (e.solid) item["solid"] = true;
    if (e.frames != 1) item["frames"] = e.frames;
    if (e.fps != 0.0f) item["fps"] = e.fps;
    if (e.fcols != 0) item["frameCols"] = e.fcols;
    if (!e.anim_loop) item["animLoop"] = false;
    if (e.rotation != 0.0f) item["rotation"] = e.rotation;
    if (e.ttl != 0.0f) item["ttl"] = e.ttl;
    if (e.flip_x) item["flipX"] = true;
    if (e.flip_y) item["flipY"] = true;
    if (!e.visible) item["visible"] = false;
    if (e.oneway) item["oneway"] = true;
    if (!e.data.empty()) item["data"] = e.data;
    if (e.opacity != 1.0f) item["opacity"] = e.opacity;
    if (e.spin != 0.0f) item["spin"] = e.spin;
    if (!e.bounce) item["bounce"] = false;
    if (!e.parent.empty()) item["parent"] = e.parent;
    if (!e.vfx.empty()) item["vfx"] = e.vfx;
    if (!e.anim.empty()) item["anim"] = e.anim;
    items.push_back(std::move(item));
  }
  if (bg_r != 8 || bg_g != 16 || bg_b != 26)
    doc["background"] = {bg_r, bg_g, bg_b};
  if (gravity != 0.0f) doc["gravity"] = gravity;
  if (!music.empty()) doc["music"] = music;
  if (world_w > 0.f || world_h > 0.f) doc["worldSize"] = {world_w, world_h};
  if (!tilemaps.empty()) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &tilemap : tilemaps) {
      nlohmann::json tm;
      tm["tileset"] = tilemap.tileset;
      if (tilemap.x != 0.f) tm["x"] = tilemap.x;
      if (tilemap.y != 0.f) tm["y"] = tilemap.y;
      tm["tileW"] = tilemap.tile_w;
      tm["tileH"] = tilemap.tile_h;
      tm["columns"] = tilemap.columns;
      if (tilemap.layer != -100) tm["layer"] = tilemap.layer;
      if (tilemap.parallax != 1.0f) tm["parallax"] = tilemap.parallax;
      if (tilemap.collide) tm["collide"] = true;
      tm["cells"] = tilemap.cells;
      if (!tilemap.name.empty()) tm["name"] = tilemap.name;
      list.push_back(std::move(tm));
    }
    doc["tilemaps"] = std::move(list);
  }
  if (!emitters.empty()) doc["emitters"] = emitters_to_json(emitters);
  if (!animations.empty())
    doc["animations"] = animations_to_json(animations);
  return doc.dump(2) + "\n";
}

std::optional<SceneDocument> SceneDocument::from_json(std::string_view text,
                                                    std::string *error) {
  auto fail = [&](const std::string &message)
      -> std::optional<SceneDocument> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(text);
  } catch (const std::exception &e) {
    return fail(std::string("malformed scene json: ") + e.what());
  }
  if (!doc.is_object() || !doc.contains("entities") ||
      !doc["entities"].is_array())
    return fail("scene document requires an entities array");
  SceneDocument scene;
  try {
    for (const auto &item : doc["entities"]) {
      if (!item.is_object()) return fail("entity entry is not an object");
      SceneEntity entity;
      entity.name = item.value("name", std::string{});
      if (entity.name.empty()) return fail("entity requires a name");
      entity.x = item.at("x").get<float>();
      entity.y = item.at("y").get<float>();
      entity.w = item.value("w", 32.f);
      entity.h = item.value("h", 32.f);
      entity.vx = item.value("vx", 0.f);
      entity.vy = item.value("vy", 0.f);
      if (item.contains("color")) {
        const auto &color = item.at("color");
        if (!color.is_array() || color.size() != 3)
          return fail("entity color must be [r,g,b]");
        entity.r = color[0].get<std::uint8_t>();
        entity.g = color[1].get<std::uint8_t>();
        entity.b = color[2].get<std::uint8_t>();
      }
      entity.sprite = item.value("sprite", std::string{});
      entity.layer = item.value("layer", 0);
      entity.parallax = item.value("parallax", 1.0f);
      entity.text = item.value("text", std::string{});
      entity.gravity_scale = item.value("gravityScale", 1.0f);
      entity.solid = item.value("solid", false);
      entity.frames = item.value("frames", 1);
      entity.fps = item.value("fps", 0.0f);
      entity.fcols = item.value("frameCols", 0);
      entity.anim_loop = item.value("animLoop", true);
      entity.rotation = item.value("rotation", 0.0f);
      entity.ttl = item.value("ttl", 0.0f);
      entity.flip_x = item.value("flipX", false);
      entity.flip_y = item.value("flipY", false);
      entity.visible = item.value("visible", true);
      entity.oneway = item.value("oneway", false);
      entity.data = item.value("data", std::string{});
      entity.opacity = item.value("opacity", 1.0f);
      entity.spin = item.value("spin", 0.0f);
      entity.bounce = item.value("bounce", true);
      entity.parent = item.value("parent", std::string{});
      entity.vfx = item.value("vfx", std::string{});
      entity.anim = item.value("anim", std::string{});
      scene.entities.push_back(std::move(entity));
    }
    if (doc.contains("background")) {
      const auto &bg = doc.at("background");
      if (!bg.is_array() || bg.size() != 3)
        return fail("background must be [r,g,b]");
      scene.bg_r = bg[0].get<std::uint8_t>();
      scene.bg_g = bg[1].get<std::uint8_t>();
      scene.bg_b = bg[2].get<std::uint8_t>();
    }
    scene.gravity = doc.value("gravity", 0.0f);
    scene.music = doc.value("music", std::string{});
    if (doc.contains("worldSize")) {
      const auto &ws = doc.at("worldSize");
      if (!ws.is_array() || ws.size() != 2)
        return fail("worldSize must be [w,h]");
      scene.world_w = ws[0].get<float>();
      scene.world_h = ws[1].get<float>();
    }
    const auto parse_tilemap = [&fail](const nlohmann::json &tm,
                                       SceneTilemap &map) -> bool {
      if (!tm.is_object()) {
        fail("tilemap must be an object");
        return false;
      }
      map.tileset = tm.value("tileset", std::string{});
      map.x = tm.value("x", 0.0f);
      map.y = tm.value("y", 0.0f);
      map.tile_w = tm.value("tileW", 32);
      map.tile_h = tm.value("tileH", 32);
      map.columns = tm.value("columns", 0);
      map.layer = tm.value("layer", -100);
      map.parallax = tm.value("parallax", 1.0f);
      map.collide = tm.value("collide", false);
      map.name = tm.value("name", std::string{});
      if (tm.contains("cells")) {
        const auto &cells = tm.at("cells");
        if (!cells.is_array()) {
          fail("tilemap cells must be an array");
          return false;
        }
        map.cells.reserve(cells.size());
        for (const auto &c : cells) map.cells.push_back(c.get<int>());
      }
      if (map.tile_w <= 0 || map.tile_h <= 0 || map.columns <= 0 ||
          map.cells.empty() || map.cells.size() % map.columns != 0) {
        fail("tilemap requires positive tileW/tileH/columns and a "
             "cells array divisible by columns");
        return false;
      }
      return true;
    };
    // "tilemaps" is the layered form; legacy "tilemap" objects still load.
    if (doc.contains("tilemaps")) {
      const auto &list = doc.at("tilemaps");
      if (!list.is_array()) return fail("tilemaps must be an array");
      for (const auto &tm : list) {
        SceneTilemap map;
        if (!parse_tilemap(tm, map)) return std::nullopt;
        scene.tilemaps.push_back(std::move(map));
      }
    }
    if (doc.contains("tilemap")) {
      SceneTilemap map;
      if (!parse_tilemap(doc.at("tilemap"), map)) return std::nullopt;
      scene.tilemaps.push_back(std::move(map));
    }
    if (doc.contains("emitters")) {
      std::string emitter_error;
      if (!parse_emitters(doc.at("emitters"), scene.emitters,
                          &emitter_error))
        return fail(emitter_error);
    }
    if (doc.contains("animations")) {
      std::string anim_error;
      if (!parse_animations(doc.at("animations"), scene.animations,
                            &anim_error))
        return fail(anim_error);
    }
  } catch (const std::exception &e) {
    return fail(std::string("malformed entity: ") + e.what());
  }
  return scene;
}

void SceneDocument::save(const std::filesystem::path &path) const {
  const std::string text = to_json();
  std::filesystem::create_directories(path.parent_path());
  write_file_atomically(path, std::as_bytes(std::span(text)));
}

std::optional<SceneDocument>
SceneDocument::load(const std::filesystem::path &path, std::string *error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  const std::string text{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
  return from_json(text, error);
}

std::string Scene3dDocument::to_json() const {
  nlohmann::json doc;
  doc["schemaVersion"] = 1;
  auto &items = doc["entities"] = nlohmann::json::array();
  for (const auto &e : entities) {
    nlohmann::json item;
    item["name"] = e.name;
    item["mesh"] = e.mesh;
    item["pos"] = {e.x, e.y, e.z};
    if (e.yaw_deg != 0.f || e.pitch_deg != 0.f || e.roll_deg != 0.f)
      item["rot"] = {e.yaw_deg, e.pitch_deg, e.roll_deg};
    if (e.scale != 1.0f) item["scale"] = e.scale;
    if (e.vx != 0.f || e.vy != 0.f || e.vz != 0.f)
      item["vel"] = {e.vx, e.vy, e.vz};
    if (e.r != 255 || e.g != 255 || e.b != 255 || e.a != 255)
      item["color"] = {e.r, e.g, e.b, e.a};
    if (!e.texture.empty()) item["texture"] = e.texture;
    if (e.opacity != 1.0f) item["opacity"] = e.opacity;
    if (e.double_sided) item["doubleSided"] = true;
    if (e.gravity_scale != 1.0f) item["gravityScale"] = e.gravity_scale;
    if (e.solid) item["solid"] = true;
    if (e.ttl != 0.0f) item["ttl"] = e.ttl;
    if (!e.data.empty()) item["data"] = e.data;
    if (!e.parent.empty()) item["parent"] = e.parent;
    if (!e.vfx.empty()) item["vfx"] = e.vfx;
    if (e.metallic != 0.f) item["metallic"] = e.metallic;
    if (e.roughness != 0.55f) item["roughness"] = e.roughness;
    if (!e.metallic_roughness.empty())
      item["metallicRoughness"] = e.metallic_roughness;
    if (!e.emissive.empty()) item["emissive"] = e.emissive;
    if (e.emissive_strength != 0.f)
      item["emissiveStrength"] = e.emissive_strength;
    if (e.emissive_r != 1.f || e.emissive_g != 1.f || e.emissive_b != 1.f)
      item["emissiveTint"] = {e.emissive_r, e.emissive_g, e.emissive_b};
    if (e.night_emissive != 0.f) item["nightEmissive"] = e.night_emissive;
    if (!e.environment.empty()) item["environment"] = e.environment;
    if (e.environment_strength != 0.f)
      item["environmentStrength"] = e.environment_strength;
    if (e.alpha_cutout != 0.f) item["alphaCutout"] = e.alpha_cutout;
    if (e.uv_tile_x != 1.f || e.uv_tile_y != 1.f)
      item["uvTile"] = {e.uv_tile_x, e.uv_tile_y};
    if (e.atmo_strength != 0.f || e.atmo_power != 3.f ||
        e.atmo_night != 0.05f || e.atmo_r != 0.45f || e.atmo_g != 0.62f ||
        e.atmo_b != 1.f)
      item["atmosphere"] = {{"tint", {e.atmo_r, e.atmo_g, e.atmo_b}},
                            {"strength", e.atmo_strength},
                            {"power", e.atmo_power},
                            {"nightFloor", e.atmo_night}};
    if (e.visible_range != 0.f) item["range"] = e.visible_range;
    if (!e.normal_map.empty() || !e.properties_map.empty() ||
        !e.cloud_map.empty() || e.normal_strength != 0.35f ||
        e.relief != 0.f || e.cloud_opacity != 0.f ||
        e.cloud_albedo != 0.f || e.cloud_offset_x != 0.f ||
        e.cloud_offset_y != 0.f) {
      item["surface"] = {{"normal", e.normal_map},
                         {"properties", e.properties_map},
                         {"cloud", e.cloud_map},
                         {"normalStrength", e.normal_strength},
                         {"relief", e.relief},
                         {"cloudOpacity", e.cloud_opacity},
                         {"cloudAlbedo", e.cloud_albedo},
                         {"cloudOffset", {e.cloud_offset_x, e.cloud_offset_y}}};
    }
    if (e.terminator_wrap != 0.f) item["terminatorWrap"] = e.terminator_wrap;
    if (e.limb_darkening != 0.f) item["limbDarken"] = e.limb_darkening;
    if (e.band_shear != 0.f) item["bandShear"] = e.band_shear;
    if (e.orbital_beaming != 0.f) item["orbitalBeam"] = e.orbital_beaming;
    if (!e.lod_meshes.empty()) {
      item["lods"] = e.lod_meshes;
      item["lodPixels"] = e.lod_pixels;
    }
    items.push_back(std::move(item));
  }
  doc["camera"] = {{"pos", {cam_x, cam_y, cam_z}},
                   {"yaw", cam_yaw_deg},
                   {"pitch", cam_pitch_deg},
                   {"fov", fov_deg},
                   {"near", near_plane},
                   {"far", far_plane}};
  doc["light"] = {{"dir", {light_x, light_y, light_z}},
                  {"intensity", light_intensity}};
  if (!lights.empty()) {
    auto &ls = doc["lights"] = nlohmann::json::array();
    for (const auto &l : lights)
      ls.push_back({{"dir", {l.dir_x, l.dir_y, l.dir_z}},
                    {"color", {l.r, l.g, l.b}},
                    {"intensity", l.intensity}});
  }
  if (!point_lights.empty()) {
    auto &ls = doc["pointLights"] = nlohmann::json::array();
    for (const auto &l : point_lights)
      ls.push_back({{"pos", {l.x, l.y, l.z}},
                    {"color", {l.r, l.g, l.b}},
                    {"intensity", l.intensity},
                    {"range", l.range}});
  }
  if (exposure != 1.f || bloom != 0.f || bloom_threshold != 1.f ||
      contrast != 1.f || saturation != 1.f || sharpen != 0.f ||
      quality != "high" || debug_view != "lit")
    doc["render"] = {{"exposure", exposure},
                     {"bloom", bloom},
                     {"bloomThreshold", bloom_threshold},
                     {"contrast", contrast},
                     {"saturation", saturation},
                     {"sharpen", sharpen},
                     {"quality", quality},
                     {"debug", debug_view}};
  if (shadow_extent > 0.f)
    doc["render"]["shadow"] = {{"extent", shadow_extent},
                               {"distance", shadow_distance},
                               {"depth", shadow_depth},
                               {"strength", shadow_strength},
                               {"bias", shadow_bias},
                               {"resolution", shadow_resolution}};
  if (bg_r != 8 || bg_g != 16 || bg_b != 26)
    doc["background"] = {bg_r, bg_g, bg_b};
  if (gravity != 0.0f) doc["gravity"] = gravity;
  if (ground_y != 0.0f) doc["groundY"] = ground_y;
  if (bounds != 0.0f) doc["bounds"] = bounds;
  if (!music.empty()) doc["music"] = music;
  if (!emitters.empty()) doc["emitters"] = emitters_to_json(emitters);
  return doc.dump(2) + "\n";
}

std::optional<Scene3dDocument>
Scene3dDocument::from_json(std::string_view text, std::string *error) {
  auto fail = [&](const std::string &message)
      -> std::optional<Scene3dDocument> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(text);
  } catch (const std::exception &e) {
    return fail(std::string("malformed scene3d json: ") + e.what());
  }
  if (!doc.is_object() || !doc.contains("entities") ||
      !doc["entities"].is_array())
    return fail("scene3d document requires an entities array");
  Scene3dDocument scene;
  const auto vec3_of = [&fail](const nlohmann::json &node,
                               const char *name, float &x, float &y,
                               float &z) -> bool {
    const auto &arr = node.at(name);
    if (!arr.is_array() || arr.size() != 3) {
      fail(std::string(name) + " must be [x,y,z]");
      return false;
    }
    x = arr[0].get<float>();
    y = arr[1].get<float>();
    z = arr[2].get<float>();
    return true;
  };
  try {
    for (const auto &item : doc["entities"]) {
      if (!item.is_object()) return fail("entity entry is not an object");
      Scene3dEntity e;
      e.name = item.value("name", std::string{});
      if (e.name.empty()) return fail("entity requires a name");
      e.mesh = item.value("mesh", std::string{"box"});
      if (!item.contains("pos") ||
          !vec3_of(item, "pos", e.x, e.y, e.z))
        return fail("entity requires a pos [x,y,z]");
      if (item.contains("rot") &&
          !vec3_of(item, "rot", e.yaw_deg, e.pitch_deg, e.roll_deg))
        return std::nullopt;
      e.scale = item.value("scale", 1.0f);
      if (!(e.scale > 0.f)) return fail("entity scale must be > 0");
      if (item.contains("vel") && !vec3_of(item, "vel", e.vx, e.vy, e.vz))
        return std::nullopt;
      if (item.contains("color")) {
        const auto &c = item.at("color");
        if (!c.is_array() || c.size() < 3 || c.size() > 4)
          return fail("color must be [r,g,b] or [r,g,b,a]");
        e.r = c[0].get<std::uint8_t>();
        e.g = c[1].get<std::uint8_t>();
        e.b = c[2].get<std::uint8_t>();
        if (c.size() == 4) e.a = c[3].get<std::uint8_t>();
      }
      e.texture = item.value("texture", std::string{});
      e.opacity = item.value("opacity", 1.0f);
      e.double_sided = item.value("doubleSided", false);
      e.gravity_scale = item.value("gravityScale", 1.0f);
      e.solid = item.value("solid", false);
      e.ttl = item.value("ttl", 0.0f);
      e.data = item.value("data", std::string{});
      e.parent = item.value("parent", std::string{});
      e.vfx = item.value("vfx", std::string{});
      e.metallic = item.value("metallic", 0.0f);
      e.roughness = item.value("roughness", 0.55f);
      e.metallic_roughness =
          item.value("metallicRoughness", std::string{});
      e.emissive = item.value("emissive", std::string{});
      e.emissive_strength = item.value("emissiveStrength", 0.0f);
      if (item.contains("emissiveTint") &&
          !vec3_of(item, "emissiveTint", e.emissive_r, e.emissive_g,
                   e.emissive_b))
        return std::nullopt;
      e.night_emissive = item.value("nightEmissive", 0.0f);
      e.environment = item.value("environment", std::string{});
      e.environment_strength = item.value("environmentStrength", 0.0f);
      e.alpha_cutout = item.value("alphaCutout", 0.0f);
      if (item.contains("uvTile")) {
        const auto &t = item.at("uvTile");
        if (!t.is_array() || t.size() != 2)
          return fail("uvTile must be [u,v]");
        e.uv_tile_x = t[0].get<float>();
        e.uv_tile_y = t[1].get<float>();
      }
      if (item.contains("atmosphere")) {
        const auto &at = item.at("atmosphere");
        if (!at.is_object()) return fail("atmosphere must be an object");
        if (at.contains("tint") &&
            !vec3_of(at, "tint", e.atmo_r, e.atmo_g, e.atmo_b))
          return std::nullopt;
        e.atmo_strength = at.value("strength", 0.0f);
        e.atmo_power = at.value("power", 3.0f);
        e.atmo_night = at.value("nightFloor", 0.05f);
      }
      e.visible_range = item.value("range", 0.0f);
      if (!(e.visible_range >= 0.f))
        return fail("range must be non-negative");
      if (item.contains("surface")) {
        const auto &sf = item.at("surface");
        if (!sf.is_object()) return fail("surface must be an object");
        e.normal_map = sf.value("normal", std::string{});
        e.properties_map = sf.value("properties", std::string{});
        e.cloud_map = sf.value("cloud", std::string{});
        e.normal_strength = sf.value("normalStrength", 0.35f);
        e.relief = sf.value("relief", 0.0f);
        e.cloud_opacity = sf.value("cloudOpacity", 0.0f);
        e.cloud_albedo = sf.value("cloudAlbedo", 0.0f);
        if (sf.contains("cloudOffset")) {
          const auto &o = sf.at("cloudOffset");
          if (!o.is_array() || o.size() != 2)
            return fail("cloudOffset must be [x,y]");
          e.cloud_offset_x = o[0].get<float>();
          e.cloud_offset_y = o[1].get<float>();
        }
        if (e.normal_map.empty() && e.properties_map.empty() &&
            e.cloud_map.empty())
          return fail("surface requires at least one map");
        if (!(e.normal_strength >= 0.f && e.normal_strength <= 2.f) ||
            !(e.relief >= 0.f && e.relief <= 0.02f) ||
            !(e.cloud_opacity >= 0.f && e.cloud_opacity <= 1.f) ||
            !(e.cloud_albedo >= 0.f && e.cloud_albedo <= 1.f) ||
            !(std::abs(e.cloud_offset_x) <= 2.f &&
              std::abs(e.cloud_offset_y) <= 2.f))
          return fail("surface fields out of range");
      }
      e.terminator_wrap = item.value("terminatorWrap", 0.0f);
      if (!(e.terminator_wrap >= 0.f && e.terminator_wrap <= 1.f))
        return fail("terminatorWrap must be in [0,1]");
      e.limb_darkening = item.value("limbDarken", 0.0f);
      if (!(e.limb_darkening >= 0.f && e.limb_darkening <= 1.f))
        return fail("limbDarken must be in [0,1]");
      e.band_shear = item.value("bandShear", 0.0f);
      if (!(e.band_shear >= -0.5f && e.band_shear <= 0.5f))
        return fail("bandShear must be in [-0.5,0.5]");
      e.orbital_beaming = item.value("orbitalBeam", 0.0f);
      if (!(e.orbital_beaming >= -1.f && e.orbital_beaming <= 1.f))
        return fail("orbitalBeam must be in [-1,1]");
      if (item.contains("lods")) {
        const auto &lods = item.at("lods");
        if (!lods.is_array() || lods.size() > 8)
          return fail("lods must be an array of at most 8 mesh specs");
        for (const auto &spec : lods) {
          if (!spec.is_string() || spec.get<std::string>().size() > 256)
            return fail("lods entries must be bounded mesh spec strings");
          e.lod_meshes.push_back(spec.get<std::string>());
        }
        e.lod_pixels = item.value("lodPixels", 32.0f);
        if (!(e.lod_pixels >= 1.f && e.lod_pixels <= 4096.f))
          return fail("lodPixels must be in [1,4096]");
      }
      scene.entities.push_back(std::move(e));
    }
    if (doc.contains("camera")) {
      const auto &cam = doc.at("camera");
      if (!cam.is_object()) return fail("camera must be an object");
      if (cam.contains("pos") &&
          !vec3_of(cam, "pos", scene.cam_x, scene.cam_y, scene.cam_z))
        return std::nullopt;
      scene.cam_yaw_deg = cam.value("yaw", 0.0f);
      scene.cam_pitch_deg = cam.value("pitch", 0.0f);
      scene.fov_deg = cam.value("fov", 60.0f);
      scene.near_plane = cam.value("near", 0.01f);
      scene.far_plane = cam.value("far", 1000.0f);
      if (!(scene.fov_deg > 0.f && scene.fov_deg < 180.f))
        return fail("camera fov must be in (0,180)");
      if (!(scene.near_plane > 0.f && scene.near_plane < scene.far_plane))
        return fail("camera requires 0 < near < far");
    }
    if (doc.contains("light")) {
      const auto &li = doc.at("light");
      if (!li.is_object()) return fail("light must be an object");
      if (li.contains("dir") &&
          !vec3_of(li, "dir", scene.light_x, scene.light_y, scene.light_z))
        return std::nullopt;
      scene.light_intensity = li.value("intensity", 1.0f);
    }
    if (doc.contains("lights")) {
      const auto &ls = doc.at("lights");
      if (!ls.is_array()) return fail("lights must be an array");
      if (ls.size() > 2) return fail("at most two extra lights");
      for (const auto &li : ls) {
        if (!li.is_object()) return fail("light entry is not an object");
        Scene3dLight l;
        if (li.contains("dir") &&
            !vec3_of(li, "dir", l.dir_x, l.dir_y, l.dir_z))
          return std::nullopt;
        if (li.contains("color") &&
            !vec3_of(li, "color", l.r, l.g, l.b))
          return std::nullopt;
        l.intensity = li.value("intensity", 0.5f);
        scene.lights.push_back(l);
      }
    }
    if (doc.contains("pointLights")) {
      const auto &ls = doc.at("pointLights");
      if (!ls.is_array()) return fail("pointLights must be an array");
      if (ls.size() > 4) return fail("at most four point lights");
      for (const auto &li : ls) {
        if (!li.is_object())
          return fail("pointLight entry is not an object");
        Scene3dPointLight l;
        if (li.contains("pos") && !vec3_of(li, "pos", l.x, l.y, l.z))
          return std::nullopt;
        if (li.contains("color") && !vec3_of(li, "color", l.r, l.g, l.b))
          return std::nullopt;
        l.intensity = li.value("intensity", 1.0f);
        l.range = li.value("range", 0.0f);
        scene.point_lights.push_back(l);
      }
    }
    if (doc.contains("render")) {
      const auto &r = doc.at("render");
      if (!r.is_object()) return fail("render must be an object");
      scene.exposure = r.value("exposure", 1.0f);
      scene.bloom = r.value("bloom", 0.0f);
      scene.bloom_threshold = r.value("bloomThreshold", 1.0f);
      scene.contrast = r.value("contrast", 1.0f);
      scene.saturation = r.value("saturation", 1.0f);
      scene.sharpen = r.value("sharpen", 0.0f);
      scene.quality = r.value("quality", std::string{"high"});
      if (scene.quality != "low" && scene.quality != "medium" &&
          scene.quality != "high" && scene.quality != "ultra")
        return fail("render quality must be low|medium|high|ultra");
      scene.debug_view = r.value("debug", std::string{"lit"});
      if (scene.debug_view != "lit" && scene.debug_view != "unlit" &&
          scene.debug_view != "albedo" && scene.debug_view != "normals" &&
          scene.debug_view != "roughness" && scene.debug_view != "metallic" &&
          scene.debug_view != "emissive" && scene.debug_view != "lighting")
        return fail("render debug must be lit|unlit|albedo|normals|roughness"
                    "|metallic|emissive|lighting");
      if (r.contains("shadow")) {
        const auto &s = r.at("shadow");
        if (!s.is_object()) return fail("render shadow must be an object");
        scene.shadow_extent = s.value("extent", 0.0f);
        scene.shadow_distance = s.value("distance", 64.0f);
        scene.shadow_depth = s.value("depth", 256.0f);
        scene.shadow_strength = s.value("strength", 1.0f);
        scene.shadow_bias = s.value("bias", 0.0005f);
        scene.shadow_resolution = s.value("resolution", 0u);
        if (scene.shadow_extent < 0.f || scene.shadow_distance < 0.f ||
            scene.shadow_depth <= 0.f || scene.shadow_strength < 0.f ||
            scene.shadow_strength > 1.f || scene.shadow_bias < 0.f ||
            scene.shadow_bias > 0.1f ||
            (scene.shadow_resolution != 0u &&
             (scene.shadow_resolution < 64u || scene.shadow_resolution > 8192u)))
          return fail("render shadow fields out of range");
      }
    }
    if (doc.contains("background")) {
      const auto &bg = doc.at("background");
      if (!bg.is_array() || bg.size() != 3)
        return fail("background must be [r,g,b]");
      scene.bg_r = bg[0].get<std::uint8_t>();
      scene.bg_g = bg[1].get<std::uint8_t>();
      scene.bg_b = bg[2].get<std::uint8_t>();
    }
    scene.gravity = doc.value("gravity", 0.0f);
    scene.ground_y = doc.value("groundY", 0.0f);
    scene.bounds = doc.value("bounds", 0.0f);
    scene.music = doc.value("music", std::string{});
    if (doc.contains("emitters")) {
      std::string emitter_error;
      if (!parse_emitters(doc.at("emitters"), scene.emitters,
                          &emitter_error))
        return fail(emitter_error);
    }
  } catch (const std::exception &e) {
    return fail(std::string("malformed scene3d entity: ") + e.what());
  }
  return scene;
}

void Scene3dDocument::save(const std::filesystem::path &path) const {
  const std::string text = to_json();
  std::filesystem::create_directories(path.parent_path());
  write_file_atomically(path, std::as_bytes(std::span(text)));
}

std::optional<Scene3dDocument>
Scene3dDocument::load(const std::filesystem::path &path, std::string *error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) *error = "cannot open " + path.string();
    return std::nullopt;
  }
  const std::string text{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
  return from_json(text, error);
}

} // namespace stellar::engine
