#include <stellar/engine/scene_document.hpp>

#include <stellar/engine/atomic_file_write.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <span>

namespace stellar::engine {

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
      tm["tileW"] = tilemap.tile_w;
      tm["tileH"] = tilemap.tile_h;
      tm["columns"] = tilemap.columns;
      if (tilemap.layer != -100) tm["layer"] = tilemap.layer;
      if (tilemap.parallax != 1.0f) tm["parallax"] = tilemap.parallax;
      if (tilemap.collide) tm["collide"] = true;
      tm["cells"] = tilemap.cells;
      list.push_back(std::move(tm));
    }
    doc["tilemaps"] = std::move(list);
  }
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
      map.tile_w = tm.value("tileW", 32);
      map.tile_h = tm.value("tileH", 32);
      map.columns = tm.value("columns", 0);
      map.layer = tm.value("layer", -100);
      map.parallax = tm.value("parallax", 1.0f);
      map.collide = tm.value("collide", false);
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

} // namespace stellar::engine
