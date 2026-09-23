#include "stellar/engine/scene_components.hpp"

#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/save_history.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

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

std::vector<std::uint8_t> encode_label(const Label &l) {
  return {l.value.begin(), l.value.end()};
}

Label decode_label(const std::vector<std::uint8_t> &b) {
  return Label{{b.begin(), b.end()}};
}

std::vector<std::uint8_t> encode_user_data(const UserData &d) {
  return {d.value.begin(), d.value.end()};
}

UserData decode_user_data(const std::vector<std::uint8_t> &b) {
  return UserData{{b.begin(), b.end()}};
}

// Tilemap codec: tileset string, fixed dims/flags, then the cell array —
// the snapshot format is byte-stable for determinism checks.
void put_u32(std::vector<std::uint8_t> &out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xff));
}

void put_i32(std::vector<std::uint8_t> &out, std::int32_t v) {
  put_u32(out, static_cast<std::uint32_t>(v));
}

void put_f32(std::vector<std::uint8_t> &out, float v) {
  std::uint32_t bits;
  std::memcpy(&bits, &v, 4);
  put_u32(out, bits);
}

std::uint32_t get_u32(const std::vector<std::uint8_t> &b,
                      std::size_t &at) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4 && at < b.size(); ++i)
    v |= static_cast<std::uint32_t>(b[at++]) << (i * 8);
  return v;
}

std::vector<std::uint8_t> encode_tilemap(const Tilemap &t) {
  std::vector<std::uint8_t> out;
  put_u32(out, static_cast<std::uint32_t>(t.tileset.size()));
  out.insert(out.end(), t.tileset.begin(), t.tileset.end());
  put_i32(out, t.tile_w);
  put_i32(out, t.tile_h);
  put_i32(out, t.columns);
  put_i32(out, t.layer);
  put_f32(out, t.parallax);
  out.push_back(t.collide ? 1 : 0);
  put_u32(out, static_cast<std::uint32_t>(t.cells.size()));
  for (const int c : t.cells) put_i32(out, c);
  // Appended fields decode as 0 on pre-origin payloads.
  put_f32(out, t.x);
  put_f32(out, t.y);
  return out;
}

Tilemap decode_tilemap(const std::vector<std::uint8_t> &b) {
  Tilemap t;
  std::size_t at = 0;
  const std::uint32_t len = get_u32(b, at);
  if (len <= b.size() - at)
    t.tileset.assign(reinterpret_cast<const char *>(b.data() + at), len);
  at += len;
  t.tile_w = static_cast<int>(get_u32(b, at));
  t.tile_h = static_cast<int>(get_u32(b, at));
  t.columns = static_cast<int>(get_u32(b, at));
  t.layer = static_cast<int>(get_u32(b, at));
  const std::uint32_t pbits = get_u32(b, at);
  std::memcpy(&t.parallax, &pbits, 4);
  t.collide = at < b.size() && b[at++] != 0;
  const std::uint32_t count = get_u32(b, at);
  t.cells.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i)
    t.cells.push_back(static_cast<int>(get_u32(b, at)));
  // Grid origin was appended after the cell array — absent on old saves.
  const std::uint32_t xb = get_u32(b, at);
  std::memcpy(&t.x, &xb, 4);
  const std::uint32_t yb = get_u32(b, at);
  std::memcpy(&t.y, &yb, 4);
  return t;
}

// Parent codec: name string, offset, last-resolved parent pos, flag.
std::vector<std::uint8_t> encode_parent(const Parent &p) {
  std::vector<std::uint8_t> out;
  put_u32(out, static_cast<std::uint32_t>(p.name.size()));
  out.insert(out.end(), p.name.begin(), p.name.end());
  put_f32(out, p.off_x);
  put_f32(out, p.off_y);
  put_f32(out, p.last_px);
  put_f32(out, p.last_py);
  out.push_back(p.resolved ? 1 : 0);
  return out;
}

Parent decode_parent(const std::vector<std::uint8_t> &b) {
  Parent p;
  std::size_t at = 0;
  const std::uint32_t len = get_u32(b, at);
  if (len <= b.size() - at)
    p.name.assign(reinterpret_cast<const char *>(b.data() + at), len);
  at += len;
  const auto f = [&b, &at] {
    float v = 0.f;
    const std::uint32_t bits = get_u32(b, at);
    std::memcpy(&v, &bits, 4);
    return v;
  };
  p.off_x = f();
  p.off_y = f();
  p.last_px = f();
  p.last_py = f();
  p.resolved = at < b.size() && b[at] != 0;
  return p;
}

// Anim codec: frames/fps/cols — the appended cols field decodes as 0
// (horizontal strip) on pre-grid payloads.
std::vector<std::uint8_t> encode_anim(const Anim &a) {
  std::vector<std::uint8_t> out;
  put_i32(out, a.frames);
  put_f32(out, a.fps);
  put_i32(out, a.cols);
  return out;
}

Anim decode_anim(const std::vector<std::uint8_t> &b) {
  Anim a;
  std::size_t at = 0;
  a.frames = static_cast<int>(get_u32(b, at));
  const std::uint32_t fbits = get_u32(b, at);
  std::memcpy(&a.fps, &fbits, 4);
  a.cols = static_cast<int>(get_u32(b, at));
  return a;
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
  world.register_component<Layer>("layer", encode_pod<Layer>,
                                  decode_pod<Layer>);
  world.register_component<Parallax>("parallax", encode_pod<Parallax>,
                                     decode_pod<Parallax>);
  world.register_component<Label>("label", encode_label, decode_label);
  world.register_component<GravityScale>("gravityScale",
                                         encode_pod<GravityScale>,
                                         decode_pod<GravityScale>);
  world.register_component<Solid>("solid",
                                  [](const Solid &) {
                                    return std::vector<std::uint8_t>{1};
                                  },
                                  [](const std::vector<std::uint8_t> &) {
                                    return Solid{};
                                  });
  world.register_component<Anim>("anim", encode_anim, decode_anim);
  world.register_component<Rotation>("rotation", encode_pod<Rotation>,
                                     decode_pod<Rotation>);
  world.register_component<Spin>("spin", encode_pod<Spin>,
                                 decode_pod<Spin>);
  world.register_component<Lifetime>("lifetime", encode_pod<Lifetime>,
                                     decode_pod<Lifetime>);
  world.register_component<Flip>("flip", encode_pod<Flip>,
                                 decode_pod<Flip>);
  world.register_component<Hidden>("hidden", encode_pod<Hidden>,
                                   decode_pod<Hidden>);
  world.register_component<Oneway>("oneway", encode_pod<Oneway>,
                                   decode_pod<Oneway>);
  world.register_component<NoBounce>("nobounce", encode_pod<NoBounce>,
                                     decode_pod<NoBounce>);
  world.register_component<UserData>("userdata", encode_user_data,
                                     decode_user_data);
  world.register_component<Opacity>("opacity", encode_pod<Opacity>,
                                    decode_pod<Opacity>);
  world.register_component<Tilemap>("tilemap", encode_tilemap,
                                    decode_tilemap);
  world.register_component<Parent>("parent", encode_parent, decode_parent);
}

std::vector<EntityId> spawn_scene(World &world, const SceneDocument &doc) {
  std::vector<EntityId> spawned;
  spawned.reserve(doc.entities.size());
  // Authored positions by name — parent offsets derive from the document
  // so chains resolve identically regardless of spawn order.
  std::unordered_map<std::string, const SceneEntity *> authored;
  authored.reserve(doc.entities.size());
  for (const auto &s : doc.entities) authored.try_emplace(s.name, &s);
  for (const auto &s : doc.entities) {
    const auto entity = world.create();
    world.add(entity, Transform2D{s.x, s.y});
    world.add(entity, Velocity2D{s.vx, s.vy});
    world.add(entity, Extent2D{s.w, s.h});
    world.add(entity, Tint{s.r, s.g, s.b});
    world.add(entity, EntityName{s.name});
    world.add(entity, Layer{s.layer});
    world.add(entity, Parallax{s.parallax});
    if (!s.text.empty()) world.add(entity, Label{s.text});
    world.add(entity, GravityScale{s.gravity_scale});
    if (s.solid) world.add(entity, Solid{});
    if (s.frames != 1 || s.fps != 0.f)
      world.add(entity, Anim{s.frames, s.fps, s.fcols});
    if (s.rotation != 0.f) world.add(entity, Rotation{s.rotation});
    if (s.spin != 0.f) world.add(entity, Spin{s.spin});
    if (s.ttl > 0.f) world.add(entity, Lifetime{s.ttl});
    if (s.flip_x || s.flip_y) world.add(entity, Flip{s.flip_x, s.flip_y});
    if (!s.visible) world.add(entity, Hidden{});
    if (s.oneway) world.add(entity, Oneway{});
    if (!s.bounce) world.add(entity, NoBounce{});
    if (!s.data.empty()) world.add(entity, UserData{s.data});
    if (s.opacity != 1.f) world.add(entity, Opacity{s.opacity});
    if (!s.sprite.empty()) world.add(entity, SpriteRef{s.sprite});
    if (!s.parent.empty()) {
      // Offset derives from the parent's authored position, or its live
      // world position when the parent isn't in this document (runtime
      // spawn_entity against an existing parent).
      float px = s.x, py = s.y;
      if (const auto it = authored.find(s.parent); it != authored.end()) {
        px = it->second->x;
        py = it->second->y;
      } else if (const auto pe = find_entity_by_name(world, s.parent)) {
        if (const auto *pt = world.get<Transform2D>(*pe)) {
          px = pt->x;
          py = pt->y;
        }
      }
      world.add(entity,
                Parent{s.parent, s.x - px, s.y - py, px, py, true});
    }
    spawned.push_back(entity);
  }
  // Each tilemap lives on its own entity (not returned) so runtime cell
  // edits snapshot with the world; order matches the document so layer
  // semantics stay stable.
  for (const auto &s : doc.tilemaps)
    world.add(world.create(),
              Tilemap{s.tileset, s.x, s.y, s.tile_w, s.tile_h, s.columns,
                      s.layer, s.parallax, s.collide, s.cells});
  return spawned;
}

std::vector<EntityId> tilemap_entities(const World &world) {
  std::vector<EntityId> out;
  for (const auto entity : world.entities())
    if (world.get<Tilemap>(entity) != nullptr) out.push_back(entity);
  return out;
}

std::optional<EntityId> tilemap_entity(const World &world) {
  const auto all = tilemap_entities(world);
  return all.empty() ? std::nullopt : std::optional<EntityId>{all.front()};
}

SceneDocument scene_from_world(const World &world) {
  SceneDocument doc;
  for (const auto entity : world.entities()) {
    if (const auto *tm = world.get<Tilemap>(entity)) {
      doc.tilemaps.push_back(SceneTilemap{tm->tileset, tm->x, tm->y,
                                          tm->tile_w, tm->tile_h,
                                          tm->columns, tm->layer,
                                          tm->parallax, tm->collide,
                                          tm->cells});
      continue;
    }
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
    if (const auto *l = world.get<Layer>(entity)) s.layer = l->value;
    if (const auto *p = world.get<Parallax>(entity)) s.parallax = p->value;
    if (const auto *l = world.get<Label>(entity)) s.text = l->value;
    if (const auto *g = world.get<GravityScale>(entity))
      s.gravity_scale = g->value;
    s.solid = world.get<Solid>(entity) != nullptr;
    if (const auto *a = world.get<Anim>(entity)) {
      s.frames = a->frames;
      s.fps = a->fps;
      s.fcols = a->cols;
    }
    if (const auto *rot = world.get<Rotation>(entity))
      s.rotation = rot->value;
    if (const auto *sp = world.get<Spin>(entity)) s.spin = sp->value;
    if (const auto *lt = world.get<Lifetime>(entity)) s.ttl = lt->remaining;
    if (const auto *fl = world.get<Flip>(entity)) {
      s.flip_x = fl->x;
      s.flip_y = fl->y;
    }
    s.visible = world.get<Hidden>(entity) == nullptr;
    s.oneway = world.get<Oneway>(entity) != nullptr;
    s.bounce = world.get<NoBounce>(entity) == nullptr;
    if (const auto *d = world.get<UserData>(entity)) s.data = d->value;
    if (const auto *o = world.get<Opacity>(entity)) s.opacity = o->value;
    if (const auto *par = world.get<Parent>(entity)) s.parent = par->name;
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

void resolve_hierarchy(World &world) {
  // Resolve chains root-first with memoization; the on-stack set breaks
  // cycles (a cyclic child keeps its last position).
  std::unordered_set<std::uint64_t> done;
  const std::function<void(EntityId, std::unordered_set<std::uint64_t> &)>
      resolve = [&](EntityId e, std::unordered_set<std::uint64_t> &stack) {
        const auto key = e.value();
        if (done.contains(key) || !stack.insert(key).second) return;
        auto *p = world.get<Parent>(e);
        auto *t = world.get<Transform2D>(e);
        if (p != nullptr && t != nullptr && !p->name.empty()) {
          if (const auto pe = find_entity_by_name(world, p->name);
              pe && *pe != e) {
            resolve(*pe, stack);
            // Skip when the parent is still on the stack (cycle back
            // through this entity) — its position isn't resolved yet.
            if (!stack.contains(pe->value()))
              if (const auto *pt = world.get<Transform2D>(*pe)) {
                if (p->resolved) {
                  // World-space edits since the last resolve (velocity,
                  // clamps, game writes) re-bake into the local offset.
                  p->off_x = t->x - p->last_px;
                  p->off_y = t->y - p->last_py;
                }
                p->last_px = pt->x;
                p->last_py = pt->y;
                p->resolved = true;
                t->x = pt->x + p->off_x;
                t->y = pt->y + p->off_y;
              }
          }
        }
        stack.erase(key);
        done.insert(key);
      };
  std::unordered_set<std::uint64_t> stack;
  for (const auto e : world.entities()) resolve(e, stack);
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
