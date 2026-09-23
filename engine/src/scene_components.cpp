#include "stellar/engine/scene_components.hpp"

#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/save_history.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace stellar::engine {
namespace {

template <class T> std::vector<std::uint8_t> encode_pod(const T &v) {
  std::vector<std::uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &v, sizeof(T));
  return bytes;
}

template <class T> T decode_pod(const std::vector<std::uint8_t> &b) {
  T v{};
  if (b.size() == sizeof(T)) std::memcpy(&v, b.data(), sizeof(T));
  return v;
}

std::vector<std::uint8_t> encode_name(const EntityName &n) {
  return {n.value.begin(), n.value.end()};
}

std::vector<std::uint8_t> encode_sprite(const SpriteRef &s) {
  return {s.value.begin(), s.value.end()};
}

EntityName decode_name(const std::vector<std::uint8_t> &b) {
  return EntityName{{b.begin(), b.end()}};
}

SpriteRef decode_sprite(const std::vector<std::uint8_t> &b) {
  return SpriteRef{{b.begin(), b.end()}};
}

} // namespace

void register_scene_components(World &world) {
  world.register_component<Transform2D>("transform", encode_pod<Transform2D>,
                                        decode_pod<Transform2D>);
  world.register_component<Velocity2D>("velocity", encode_pod<Velocity2D>,
                                       decode_pod<Velocity2D>);
  world.register_component<Extent2D>("extent", encode_pod<Extent2D>,
                                     decode_pod<Extent2D>);
  world.register_component<Tint>("tint", encode_pod<Tint>, decode_pod<Tint>);
  world.register_component<EntityName>("name", encode_name, decode_name);
  world.register_component<SpriteRef>("sprite", encode_sprite, decode_sprite);
}

std::vector<EntityId> spawn_scene(World &world, const SceneDocument &doc) {
  std::vector<EntityId> spawned;
  spawned.reserve(doc.entities.size());
  for (const auto &s : doc.entities) {
    const auto entity = world.create();
    world.add(entity, Transform2D{s.x, s.y});
    world.add(entity, Velocity2D{s.vx, s.vy});
    world.add(entity, Extent2D{s.w, s.h});
    world.add(entity, Tint{s.r, s.g, s.b});
    world.add(entity, EntityName{s.name});
    if (!s.sprite.empty()) world.add(entity, SpriteRef{s.sprite});
    spawned.push_back(entity);
  }
  return spawned;
}

SceneDocument scene_from_world(const World &world) {
  SceneDocument doc;
  for (const auto entity : world.entities()) {
    const auto *name = world.get<EntityName>(entity);
    const auto *t = world.get<Transform2D>(entity);
    if (name == nullptr && t == nullptr) continue;
    SceneEntity s;
    if (name) s.name = name->value;
    if (t) {
      s.x = t->x;
      s.y = t->y;
    }
    if (const auto *v = world.get<Velocity2D>(entity)) {
      s.vx = v->dx;
      s.vy = v->dy;
    }
    if (const auto *e = world.get<Extent2D>(entity)) {
      s.w = e->w;
      s.h = e->h;
    }
    if (const auto *tint = world.get<Tint>(entity)) {
      s.r = tint->r;
      s.g = tint->g;
      s.b = tint->b;
    }
    if (const auto *sp = world.get<SpriteRef>(entity)) s.sprite = sp->value;
    doc.entities.push_back(std::move(s));
  }
  return doc;
}

std::optional<EntityId> find_entity_by_name(const World &world,
                                            std::string_view name) {
  for (const auto entity : world.entities()) {
    if (const auto *n = world.get<EntityName>(entity);
        n != nullptr && n->value == name)
      return entity;
  }
  return std::nullopt;
}

void save_world_to_file(const World &world,
                        const std::filesystem::path &path) {
  const auto bytes = world.snapshot();
  // Shift the .bak chain first so the atomic replace's outgoing-primary
  // backup becomes slot 1; then sidecar every slot for integrity checks.
  rotate_save_history(path);
  write_file_atomically(
      path,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()));
  write_history_sidecars(path);
}

bool load_world_from_file(World &world, const std::filesystem::path &path) {
  const auto try_load = [&world](const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    try {
      world.restore(bytes);
    } catch (const std::exception &) {
      return false;
    }
    return true;
  };
  if (try_load(path)) return true;
  // Durable recovery: walk the rotated .bak chain newest-first.
  for (std::size_t slot = 1; slot <= k_default_save_history_depth; ++slot)
    if (try_load(history_slot_path(path, slot))) return true;
  return false;
}

} // namespace stellar::engine
