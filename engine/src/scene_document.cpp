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
    items.push_back(std::move(item));
  }
  if (bg_r != 8 || bg_g != 16 || bg_b != 26)
    doc["background"] = {bg_r, bg_g, bg_b};
  if (gravity != 0.0f) doc["gravity"] = gravity;
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
