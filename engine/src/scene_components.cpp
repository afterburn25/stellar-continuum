#include "stellar/engine/scene_components.hpp"

#include "stellar/engine/atomic_file_write.hpp"
#include "stellar/engine/native_geometry3d.hpp"
#include "stellar/engine/native_scene3d.hpp"
#include "stellar/engine/save_history.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::engine {
namespace {

// Snapshots must be byte-deterministic — memcpy'ing an object with
// padding (or an empty marker struct) leaks uninitialized bytes into
// snapshots and replay checkpoint hashes. encode_fields serializes each
// listed member instead of the object representation: the sizeof check
// statically requires the member list to cover the whole struct, so a
// new member trips it until added to the codec and padding can never
// reach the output. For the current padding-free layouts the byte
// stream is identical to a whole-struct memcpy.
template <class T, auto M>
using member_t = std::remove_cvref_t<decltype(std::declval<T &>().*M)>;

template <class T, auto... M>
std::vector<std::uint8_t> encode_fields(const T &v) {
  static_assert(sizeof...(M) > 0,
                "encode_fields cannot serialize empty markers — use a "
                "fixed-byte codec");
  static_assert(sizeof(T) == (sizeof(member_t<T, M>) + ...),
                "encode_fields must list every member in order; an "
                "omitted member or layout padding trips this check");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(sizeof(T));
  const auto put = [&bytes](const auto &f) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(&f);
    bytes.insert(bytes.end(), p, p + sizeof(f));
  };
  (put(v.*M), ...);
  return bytes;
}

template <class T, auto... M>
T decode_fields(const std::vector<std::uint8_t> &b) {
  T v{};
  if (b.size() != sizeof(T)) return v;
  std::size_t at = 0;
  const auto get = [&b, &at](auto &f) {
    std::memcpy(&f, b.data() + at, sizeof(f));
    at += sizeof(f);
  };
  (get(v.*M), ...);
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

// Anim codec: frames/fps/cols/loop — appended fields decode as
// cols=0 (strip) and loop=true on older payloads.
std::vector<std::uint8_t> encode_anim(const Anim &a) {
  std::vector<std::uint8_t> out;
  put_i32(out, a.frames);
  put_f32(out, a.fps);
  put_i32(out, a.cols);
  out.push_back(a.loop ? 1 : 0);
  return out;
}

Anim decode_anim(const std::vector<std::uint8_t> &b) {
  Anim a;
  std::size_t at = 0;
  a.frames = static_cast<int>(get_u32(b, at));
  const std::uint32_t fbits = get_u32(b, at);
  std::memcpy(&a.fps, &fbits, 4);
  a.cols = static_cast<int>(get_u32(b, at));
  a.loop = at >= b.size() || b[at] != 0;
  return a;
}

} // namespace

void register_scene_components(World &world) {
  world.register_component<Transform2D>(
      "transform", encode_fields<Transform2D, &Transform2D::x, &Transform2D::y>,
      decode_fields<Transform2D, &Transform2D::x, &Transform2D::y>);
  world.register_component<Velocity2D>(
      "velocity",
      encode_fields<Velocity2D, &Velocity2D::dx, &Velocity2D::dy>,
      decode_fields<Velocity2D, &Velocity2D::dx, &Velocity2D::dy>);
  world.register_component<Extent2D>(
      "extent", encode_fields<Extent2D, &Extent2D::w, &Extent2D::h>,
      decode_fields<Extent2D, &Extent2D::w, &Extent2D::h>);
  world.register_component<Tint>(
      "tint", encode_fields<Tint, &Tint::r, &Tint::g, &Tint::b>,
      decode_fields<Tint, &Tint::r, &Tint::g, &Tint::b>);
  world.register_component<EntityName>("name", encode_name, decode_name);
  world.register_component<SpriteRef>("sprite", encode_sprite, decode_sprite);
  world.register_component<Layer>("layer", encode_fields<Layer, &Layer::value>,
                                  decode_fields<Layer, &Layer::value>);
  world.register_component<Parallax>(
      "parallax", encode_fields<Parallax, &Parallax::value>,
      decode_fields<Parallax, &Parallax::value>);
  world.register_component<Label>("label", encode_label, decode_label);
  world.register_component<GravityScale>(
      "gravityScale", encode_fields<GravityScale, &GravityScale::value>,
      decode_fields<GravityScale, &GravityScale::value>);
  world.register_component<Solid>("solid",
                                  [](const Solid &) {
                                    return std::vector<std::uint8_t>{1};
                                  },
                                  [](const std::vector<std::uint8_t> &) {
                                    return Solid{};
                                  });
  world.register_component<Anim>("anim", encode_anim, decode_anim);
  world.register_component<Rotation>(
      "rotation", encode_fields<Rotation, &Rotation::value>,
      decode_fields<Rotation, &Rotation::value>);
  world.register_component<Spin>("spin", encode_fields<Spin, &Spin::value>,
                                 decode_fields<Spin, &Spin::value>);
  world.register_component<Lifetime>(
      "lifetime", encode_fields<Lifetime, &Lifetime::remaining>,
      decode_fields<Lifetime, &Lifetime::remaining>);
  world.register_component<Flip>("flip",
                                 encode_fields<Flip, &Flip::x, &Flip::y>,
                                 decode_fields<Flip, &Flip::x, &Flip::y>);
  // Marker components carry no data — encode_fields cannot serialize an
  // empty struct (it would emit zero bytes, indistinguishable from a
  // truncated payload). Emit a fixed byte like Solid/DoubleSided;
  // decode ignores the payload so existing saves still load.
  const auto encode_marker = [](const auto &) {
    return std::vector<std::uint8_t>{1};
  };
  world.register_component<Hidden>(
      "hidden", encode_marker,
      [](const std::vector<std::uint8_t> &) { return Hidden{}; });
  world.register_component<Oneway>(
      "oneway", encode_marker,
      [](const std::vector<std::uint8_t> &) { return Oneway{}; });
  world.register_component<NoBounce>(
      "nobounce", encode_marker,
      [](const std::vector<std::uint8_t> &) { return NoBounce{}; });
  world.register_component<UserData>("userdata", encode_user_data,
                                     decode_user_data);
  world.register_component<Opacity>(
      "opacity", encode_fields<Opacity, &Opacity::value>,
      decode_fields<Opacity, &Opacity::value>);
  world.register_component<Tilemap>("tilemap", encode_tilemap,
                                    decode_tilemap);
  world.register_component<Parent>("parent", encode_parent, decode_parent);
  world.register_component<VfxRef>(
      "vfxref",
      [](const VfxRef &v) {
        return std::vector<std::uint8_t>{v.name.begin(), v.name.end()};
      },
      [](const std::vector<std::uint8_t> &b) {
        return VfxRef{{b.begin(), b.end()}};
      });
  // AnimTimeline codec: NUL-terminated clip id, then the playhead time and
  // playing flag into saved_* scratch — the timeline re-attaches by id on
  // restore. A still-detached component re-encodes its scratch verbatim.
  world.register_component<AnimTimeline>(
      "animtimeline",
      [](const AnimTimeline &a) {
        std::vector<std::uint8_t> out{a.id.begin(), a.id.end()};
        out.push_back(0);
        const float time =
            a.player.timeline() != nullptr ? a.player.time() : a.saved_time;
        const bool playing = a.player.timeline() != nullptr
                                 ? !a.player.paused()
                                 : a.saved_playing;
        put_f32(out, time);
        out.push_back(playing ? 1 : 0);
        return out;
      },
      [](const std::vector<std::uint8_t> &b) {
        AnimTimeline a;
        const auto nul = std::find(b.begin(), b.end(), std::uint8_t{0});
        a.id.assign(b.begin(), nul);
        std::size_t at = static_cast<std::size_t>(nul - b.begin()) + 1;
        if (at + 4 <= b.size()) {
          const std::uint32_t bits = get_u32(b, at);
          std::memcpy(&a.saved_time, &bits, 4);
        }
        if (at < b.size())
          a.saved_playing = b[at] != 0;
        return a;
      });
  world.register_component<Camera3DState>(
      "camera3d",
      encode_fields<Camera3DState, &Camera3DState::x, &Camera3DState::y,
                    &Camera3DState::z, &Camera3DState::yaw_deg,
                    &Camera3DState::pitch_deg, &Camera3DState::fov_deg>,
      decode_fields<Camera3DState, &Camera3DState::x, &Camera3DState::y,
                    &Camera3DState::z, &Camera3DState::yaw_deg,
                    &Camera3DState::pitch_deg, &Camera3DState::fov_deg>);
  world.register_component<Transform3D>(
      "transform3",
      encode_fields<Transform3D, &Transform3D::x, &Transform3D::y,
                    &Transform3D::z, &Transform3D::qx, &Transform3D::qy,
                    &Transform3D::qz, &Transform3D::qw, &Transform3D::scale>,
      decode_fields<Transform3D, &Transform3D::x, &Transform3D::y,
                    &Transform3D::z, &Transform3D::qx, &Transform3D::qy,
                    &Transform3D::qz, &Transform3D::qw, &Transform3D::scale>);
  world.register_component<Velocity3D>(
      "velocity3",
      encode_fields<Velocity3D, &Velocity3D::dx, &Velocity3D::dy,
                    &Velocity3D::dz>,
      decode_fields<Velocity3D, &Velocity3D::dx, &Velocity3D::dy,
                    &Velocity3D::dz>);
  world.register_component<MeshRef>(
      "meshref",
      [](const MeshRef &m) {
        return std::vector<std::uint8_t>{m.spec.begin(), m.spec.end()};
      },
      [](const std::vector<std::uint8_t> &b) {
        return MeshRef{{b.begin(), b.end()}};
      });
  world.register_component<TextureRef>(
      "textureref",
      [](const TextureRef &t) {
        return std::vector<std::uint8_t>{t.value.begin(), t.value.end()};
      },
      [](const std::vector<std::uint8_t> &b) {
        return TextureRef{{b.begin(), b.end()}};
      });
  world.register_component<Parent3D>(
      "parent3",
      [](const Parent3D &p) {
        std::vector<std::uint8_t> out;
        put_u32(out, static_cast<std::uint32_t>(p.name.size()));
        out.insert(out.end(), p.name.begin(), p.name.end());
        put_f32(out, p.off_x);
        put_f32(out, p.off_y);
        put_f32(out, p.off_z);
        put_f32(out, p.last_px);
        put_f32(out, p.last_py);
        put_f32(out, p.last_pz);
        out.push_back(p.resolved ? 1 : 0);
        return out;
      },
      [](const std::vector<std::uint8_t> &b) {
        Parent3D p;
        std::size_t at = 0;
        const std::uint32_t len = get_u32(b, at);
        if (at + len > b.size()) return p;
        p.name.assign(reinterpret_cast<const char *>(b.data() + at), len);
        at += len;
        const auto f = [&] {
          const std::uint32_t bits = get_u32(b, at);
          float v{};
          std::memcpy(&v, &bits, sizeof(v));
          return v;
        };
        p.off_x = f();
        p.off_y = f();
        p.off_z = f();
        p.last_px = f();
        p.last_py = f();
        p.last_pz = f();
        p.resolved = at < b.size() && b[at] != 0;
        return p;
      });
  world.register_component<DoubleSided>(
      "doublesided",
      [](const DoubleSided &) { return std::vector<std::uint8_t>{1}; },
      [](const std::vector<std::uint8_t> &) { return DoubleSided{}; });
  // Scalars first (fixed-size head), then the three map paths as
  // length-prefixed strings — decode tolerates a truncated tail.
  world.register_component<MaterialPbr>(
      "materialpbr",
      [](const MaterialPbr &m) {
        std::vector<std::uint8_t> out;
        for (const float f :
             {m.metallic, m.roughness, m.emissive_strength,
              m.night_emissive, m.environment_strength, m.emissive_r,
              m.emissive_g, m.emissive_b, m.alpha_cutout, m.uv_tile_x,
              m.uv_tile_y})
          put_f32(out, f);
        for (const std::string *s :
             {&m.metallic_roughness, &m.emissive, &m.environment}) {
          put_u32(out, static_cast<std::uint32_t>(s->size()));
          out.insert(out.end(), s->begin(), s->end());
        }
        return out;
      },
      [](const std::vector<std::uint8_t> &b) {
        MaterialPbr m;
        std::size_t at = 0;
        const auto f = [&b, &at] {
          float v = 0.f;
          const std::uint32_t bits = get_u32(b, at);
          std::memcpy(&v, &bits, 4);
          return v;
        };
        m.metallic = f();
        m.roughness = f();
        m.emissive_strength = f();
        m.night_emissive = f();
        m.environment_strength = f();
        m.emissive_r = f();
        m.emissive_g = f();
        m.emissive_b = f();
        m.alpha_cutout = f();
        m.uv_tile_x = f();
        m.uv_tile_y = f();
        for (std::string *s :
             {&m.metallic_roughness, &m.emissive, &m.environment}) {
          const std::uint32_t len = get_u32(b, at);
          if (len > b.size() - at) break;
          s->assign(reinterpret_cast<const char *>(b.data() + at), len);
          at += len;
        }
        return m;
      });
  // Same layout convention as MaterialPbr: fixed-size float head, then
  // length-prefixed map paths — decode tolerates a truncated tail.
  world.register_component<MaterialSurface>(
      "materialsurface",
      [](const MaterialSurface &m) {
        std::vector<std::uint8_t> out;
        for (const float f :
             {m.normal_strength, m.relief, m.cloud_opacity, m.cloud_albedo,
              m.cloud_offset_x, m.cloud_offset_y, m.terminator_wrap,
              m.limb_darkening, m.band_shear, m.orbital_beaming,
              m.forward_scatter})
          put_f32(out, f);
        for (const std::string *s :
             {&m.normal_map, &m.properties_map, &m.cloud_map}) {
          put_u32(out, static_cast<std::uint32_t>(s->size()));
          out.insert(out.end(), s->begin(), s->end());
        }
        return out;
      },
      [](const std::vector<std::uint8_t> &b) {
        MaterialSurface m;
        std::size_t at = 0;
        const auto f = [&b, &at] {
          float v = 0.f;
          const std::uint32_t bits = get_u32(b, at);
          std::memcpy(&v, &bits, 4);
          return v;
        };
        m.normal_strength = f();
        m.relief = f();
        m.cloud_opacity = f();
        m.cloud_albedo = f();
        m.cloud_offset_x = f();
        m.cloud_offset_y = f();
        m.terminator_wrap = f();
        m.limb_darkening = f();
        m.band_shear = f();
        m.orbital_beaming = f();
        m.forward_scatter = f();
        for (std::string *s :
             {&m.normal_map, &m.properties_map, &m.cloud_map}) {
          const std::uint32_t len = get_u32(b, at);
          if (len > b.size() - at) break;
          s->assign(reinterpret_cast<const char *>(b.data() + at), len);
          at += len;
        }
        return m;
      });
  world.register_component<AtmosphereShell>(
      "atmosphere",
      encode_fields<AtmosphereShell, &AtmosphereShell::r,
                    &AtmosphereShell::g, &AtmosphereShell::b,
                    &AtmosphereShell::strength, &AtmosphereShell::power,
                    &AtmosphereShell::night_floor>,
      decode_fields<AtmosphereShell, &AtmosphereShell::r,
                    &AtmosphereShell::g, &AtmosphereShell::b,
                    &AtmosphereShell::strength, &AtmosphereShell::power,
                    &AtmosphereShell::night_floor>);
  world.register_component<VisibleRange>(
      "visiblerange",
      encode_fields<VisibleRange, &VisibleRange::range>,
      decode_fields<VisibleRange, &VisibleRange::range>);
  world.register_component<StarPhotosphere>(
      "starphotosphere",
      encode_fields<StarPhotosphere, &StarPhotosphere::kelvin>,
      decode_fields<StarPhotosphere, &StarPhotosphere::kelvin>);
  world.register_component<AccretionDisc>(
      "accretiondisc",
      encode_fields<AccretionDisc, &AccretionDisc::inner,
                    &AccretionDisc::outer, &AccretionDisc::kelvin,
                    &AccretionDisc::beaming>,
      decode_fields<AccretionDisc, &AccretionDisc::inner,
                    &AccretionDisc::outer, &AccretionDisc::kelvin,
                    &AccretionDisc::beaming>);
  // u32 count + length-prefixed spec strings + f32 switch size — decode
  // tolerates a truncated tail like MaterialSurface.
  world.register_component<MeshLods>(
      "meshlods",
      [](const MeshLods &m) {
        std::vector<std::uint8_t> out;
        put_u32(out, static_cast<std::uint32_t>(m.specs.size()));
        for (const auto &s : m.specs) {
          put_u32(out, static_cast<std::uint32_t>(s.size()));
          out.insert(out.end(), s.begin(), s.end());
        }
        put_f32(out, m.pixels);
        return out;
      },
      [](const std::vector<std::uint8_t> &b) {
        MeshLods m;
        std::size_t at = 0;
        const std::uint32_t count = get_u32(b, at);
        for (std::uint32_t i = 0; i < count && i < 8; ++i) {
          const std::uint32_t len = get_u32(b, at);
          if (len > b.size() - at) break;
          m.specs.emplace_back(reinterpret_cast<const char *>(b.data() + at),
                               len);
          at += len;
        }
        if (b.size() - at >= 4) {
          const std::uint32_t bits = get_u32(b, at);
          std::memcpy(&m.pixels, &bits, 4);
        }
        return m;
      });
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
    if (s.frames != 1 || s.fps != 0.f || !s.anim_loop)
      world.add(entity, Anim{s.frames, s.fps, s.fcols, s.anim_loop});
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
    if (!s.vfx.empty()) world.add(entity, VfxRef{s.vfx});
    if (!s.anim.empty()) world.add(entity, AnimTimeline{s.anim});
    spawned.push_back(entity);
  }
  // Each tilemap lives on its own entity (not returned) so runtime cell
  // edits snapshot with the world; order matches the document so layer
  // semantics stay stable.
  for (const auto &s : doc.tilemaps) {
    const auto entity = world.create();
    world.add(entity,
              Tilemap{s.tileset, s.x, s.y, s.tile_w, s.tile_h, s.columns,
                      s.layer, s.parallax, s.collide, s.cells});
    if (!s.name.empty()) world.add(entity, EntityName{s.name});
  }
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
      auto &map = doc.tilemaps.emplace_back(
          SceneTilemap{tm->tileset, tm->x, tm->y, tm->tile_w, tm->tile_h,
                       tm->columns, tm->layer, tm->parallax, tm->collide,
                       tm->cells});
      if (const auto *n = world.get<EntityName>(entity))
        map.name = n->value;
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
      s.anim_loop = a->loop;
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
    if (const auto *vr = world.get<VfxRef>(entity)) s.vfx = vr->name;
    if (const auto *at = world.get<AnimTimeline>(entity)) s.anim = at->id;
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

std::optional<std::size_t> tilemap_index(const World &world,
                                         std::string_view name) {
  const auto entity = find_entity_by_name(world, name);
  if (!entity) return std::nullopt;
  const auto all = tilemap_entities(world);
  const auto it = std::find(all.begin(), all.end(), *entity);
  return it != all.end()
             ? std::optional<std::size_t>{it - all.begin()}
             : std::nullopt;
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

native_map::Quaternion euler_to_quat3(float yaw_deg, float pitch_deg,
                                      float roll_deg) {
  constexpr float deg = 3.14159265358979f / 180.f;
  const auto yq = native_map::rotation_axis_angle({0, 1, 0},
                                                  yaw_deg * deg);
  const auto xq = native_map::rotation_axis_angle({1, 0, 0},
                                                  pitch_deg * deg);
  const auto zq = native_map::rotation_axis_angle({0, 0, 1},
                                                  roll_deg * deg);
  return native_map::compose_rotation(
      native_map::compose_rotation(yq, xq), zq);
}

// Inverse of euler_to_quat3 for the same Y∘X∘Z order.
void quat_to_euler3(const native_map::Quaternion &q, float &yaw_deg,
                    float &pitch_deg, float &roll_deg) {
  constexpr float rad = 180.f / 3.14159265358979f;
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  const float m02 = 2.f * (x * z + w * y);
  const float m12 = 2.f * (y * z - w * x);
  const float m22 = 1.f - 2.f * (x * x + y * y);
  const float m10 = 2.f * (x * y + w * z);
  const float m11 = 1.f - 2.f * (x * x + z * z);
  pitch_deg = -std::asin(std::clamp(m12, -1.f, 1.f)) * rad;
  yaw_deg = std::atan2(m02, m22) * rad;
  roll_deg = std::atan2(m10, m11) * rad;
}

std::vector<EntityId> spawn_scene3d(World &world,
                                    const Scene3dDocument &doc) {
  std::vector<EntityId> spawned;
  spawned.reserve(doc.entities.size());
  std::unordered_map<std::string, const Scene3dEntity *> authored;
  authored.reserve(doc.entities.size());
  for (const auto &s : doc.entities) authored.try_emplace(s.name, &s);
  for (const auto &s : doc.entities) {
    const auto entity = world.create();
    const auto q = euler_to_quat3(s.yaw_deg, s.pitch_deg, s.roll_deg);
    world.add(entity,
              Transform3D{s.x, s.y, s.z, q.x, q.y, q.z, q.w, s.scale});
    world.add(entity, Velocity3D{s.vx, s.vy, s.vz});
    world.add(entity, EntityName{s.name});
    world.add(entity, MeshRef{s.mesh});
    world.add(entity, Tint{s.r, s.g, s.b});
    if (s.a != 255 || s.opacity != 1.f)
      world.add(entity, Opacity{s.opacity * (s.a / 255.f)});
    if (!s.texture.empty()) world.add(entity, TextureRef{s.texture});
    if (s.double_sided) world.add(entity, DoubleSided{});
    // Material extension components ride the entity's snapshot stream —
    // they are only attached when the document opts into them.
    if (s.metallic != 0.f || s.roughness != 0.55f ||
        !s.metallic_roughness.empty() || !s.emissive.empty() ||
        s.emissive_strength != 0.f || s.night_emissive != 0.f ||
        !s.environment.empty() || s.environment_strength != 0.f ||
        s.alpha_cutout != 0.f || s.uv_tile_x != 1.f || s.uv_tile_y != 1.f)
      world.add(entity,
                MaterialPbr{s.metallic, s.roughness, s.emissive_strength,
                            s.night_emissive, s.environment_strength,
                            s.emissive_r, s.emissive_g, s.emissive_b,
                            s.alpha_cutout, s.uv_tile_x, s.uv_tile_y,
                            s.metallic_roughness, s.emissive,
                            s.environment});
    if (!s.normal_map.empty() || !s.properties_map.empty() ||
        !s.cloud_map.empty() || s.normal_strength != 0.35f ||
        s.relief != 0.f || s.cloud_opacity != 0.f || s.cloud_albedo != 0.f ||
        s.cloud_offset_x != 0.f || s.cloud_offset_y != 0.f ||
        s.terminator_wrap != 0.f || s.limb_darkening != 0.f ||
        s.band_shear != 0.f || s.orbital_beaming != 0.f ||
        s.forward_scatter != 0.f)
      world.add(entity,
                MaterialSurface{s.normal_strength, s.relief,
                                s.cloud_opacity, s.cloud_albedo,
                                s.cloud_offset_x, s.cloud_offset_y,
                                s.terminator_wrap, s.limb_darkening,
                                s.band_shear, s.orbital_beaming,
                                s.forward_scatter, s.normal_map,
                                s.properties_map, s.cloud_map});
    if (s.atmo_strength != 0.f)
      world.add(entity, AtmosphereShell{s.atmo_r, s.atmo_g, s.atmo_b,
                                        s.atmo_strength, s.atmo_power,
                                        s.atmo_night});
    if (s.visible_range > 0.f)
      world.add(entity, VisibleRange{s.visible_range});
    if (s.star_kelvin >= 100.0)
      world.add(entity, StarPhotosphere{s.star_kelvin});
    if (s.accretion[2] >= 100.f)
      world.add(entity, AccretionDisc{s.accretion[0], s.accretion[1],
                                      s.accretion[2], s.accretion[3]});
    if (!s.lod_meshes.empty())
      world.add(entity, MeshLods{s.lod_meshes, s.lod_pixels});
    world.add(entity, GravityScale{s.gravity_scale});
    if (s.solid) world.add(entity, Solid{});
    if (s.ttl > 0.f) world.add(entity, Lifetime{s.ttl});
    if (!s.data.empty()) world.add(entity, UserData{s.data});
    if (!s.parent.empty()) {
      float px = s.x, py = s.y, pz = s.z;
      if (const auto it = authored.find(s.parent); it != authored.end()) {
        px = it->second->x;
        py = it->second->y;
        pz = it->second->z;
      } else if (const auto pe = find_entity_by_name(world, s.parent)) {
        if (const auto *pt = world.get<Transform3D>(*pe)) {
          px = pt->x;
          py = pt->y;
          pz = pt->z;
        }
      }
      world.add(entity, Parent3D{s.parent, s.x - px, s.y - py, s.z - pz,
                                 px, py, pz, true});
    }
    if (!s.vfx.empty()) world.add(entity, VfxRef{s.vfx});
    spawned.push_back(entity);
  }
  return spawned;
}

std::vector<EntityId> entities3d(const World &world) {
  std::vector<EntityId> out;
  for (const auto entity : world.entities())
    if (world.get<Transform3D>(entity) != nullptr) out.push_back(entity);
  return out;
}

Scene3dDocument scene3d_from_world(const World &world) {
  Scene3dDocument doc;
  for (const auto entity : world.entities()) {
    const auto *t = world.get<Transform3D>(entity);
    if (t == nullptr) continue;
    Scene3dEntity s;
    if (const auto *n = world.get<EntityName>(entity)) s.name = n->value;
    if (const auto *m = world.get<MeshRef>(entity)) s.mesh = m->spec;
    s.x = t->x;
    s.y = t->y;
    s.z = t->z;
    quat_to_euler3({t->qx, t->qy, t->qz, t->qw}, s.yaw_deg, s.pitch_deg,
                   s.roll_deg);
    s.scale = t->scale;
    if (const auto *v = world.get<Velocity3D>(entity)) {
      s.vx = v->dx;
      s.vy = v->dy;
      s.vz = v->dz;
    }
    if (const auto *tint = world.get<Tint>(entity)) {
      s.r = tint->r;
      s.g = tint->g;
      s.b = tint->b;
    }
    if (const auto *o = world.get<Opacity>(entity)) s.opacity = o->value;
    if (const auto *tx = world.get<TextureRef>(entity))
      s.texture = tx->value;
    s.double_sided = world.get<DoubleSided>(entity) != nullptr;
    if (const auto *p = world.get<MaterialPbr>(entity)) {
      s.metallic = p->metallic;
      s.roughness = p->roughness;
      s.emissive_strength = p->emissive_strength;
      s.night_emissive = p->night_emissive;
      s.environment_strength = p->environment_strength;
      s.emissive_r = p->emissive_r;
      s.emissive_g = p->emissive_g;
      s.emissive_b = p->emissive_b;
      s.alpha_cutout = p->alpha_cutout;
      s.uv_tile_x = p->uv_tile_x;
      s.uv_tile_y = p->uv_tile_y;
      s.metallic_roughness = p->metallic_roughness;
      s.emissive = p->emissive;
      s.environment = p->environment;
    }
    if (const auto *sf = world.get<MaterialSurface>(entity)) {
      s.normal_map = sf->normal_map;
      s.properties_map = sf->properties_map;
      s.cloud_map = sf->cloud_map;
      s.normal_strength = sf->normal_strength;
      s.relief = sf->relief;
      s.cloud_opacity = sf->cloud_opacity;
      s.cloud_albedo = sf->cloud_albedo;
      s.cloud_offset_x = sf->cloud_offset_x;
      s.cloud_offset_y = sf->cloud_offset_y;
      s.terminator_wrap = sf->terminator_wrap;
      s.limb_darkening = sf->limb_darkening;
      s.band_shear = sf->band_shear;
      s.orbital_beaming = sf->orbital_beaming;
      s.forward_scatter = sf->forward_scatter;
    }
    if (const auto *at = world.get<AtmosphereShell>(entity)) {
      s.atmo_r = at->r;
      s.atmo_g = at->g;
      s.atmo_b = at->b;
      s.atmo_strength = at->strength;
      s.atmo_power = at->power;
      s.atmo_night = at->night_floor;
    }
    if (const auto *vr = world.get<VisibleRange>(entity))
      s.visible_range = vr->range;
    if (const auto *sp = world.get<StarPhotosphere>(entity))
      s.star_kelvin = sp->kelvin;
    if (const auto *ad = world.get<AccretionDisc>(entity))
      s.accretion = {ad->inner, ad->outer, ad->kelvin, ad->beaming};
    if (const auto *ml = world.get<MeshLods>(entity)) {
      s.lod_meshes = ml->specs;
      s.lod_pixels = ml->pixels;
    }
    if (const auto *g = world.get<GravityScale>(entity))
      s.gravity_scale = g->value;
    s.solid = world.get<Solid>(entity) != nullptr;
    if (const auto *lt = world.get<Lifetime>(entity)) s.ttl = lt->remaining;
    if (const auto *d = world.get<UserData>(entity)) s.data = d->value;
    if (const auto *p = world.get<Parent3D>(entity)) s.parent = p->name;
    if (const auto *v = world.get<VfxRef>(entity)) s.vfx = v->name;
    doc.entities.push_back(std::move(s));
  }
  return doc;
}

void resolve_hierarchy3d(World &world) {
  std::unordered_set<std::uint64_t> done;
  const std::function<void(EntityId, std::unordered_set<std::uint64_t> &)>
      resolve = [&](EntityId e, std::unordered_set<std::uint64_t> &stack) {
        const auto key = e.value();
        if (done.contains(key) || !stack.insert(key).second) return;
        auto *p = world.get<Parent3D>(e);
        auto *t = world.get<Transform3D>(e);
        if (p != nullptr && t != nullptr && !p->name.empty()) {
          if (const auto pe = find_entity_by_name(world, p->name);
              pe && *pe != e) {
            resolve(*pe, stack);
            if (!stack.contains(pe->value()))
              if (const auto *pt = world.get<Transform3D>(*pe)) {
                if (p->resolved) {
                  p->off_x = t->x - p->last_px;
                  p->off_y = t->y - p->last_py;
                  p->off_z = t->z - p->last_pz;
                }
                p->last_px = pt->x;
                p->last_py = pt->y;
                p->last_pz = pt->z;
                p->resolved = true;
                t->x = pt->x + p->off_x;
                t->y = pt->y + p->off_y;
                t->z = pt->z + p->off_z;
              }
          }
        }
        stack.erase(key);
        done.insert(key);
      };
  std::unordered_set<std::uint64_t> stack;
  for (const auto e : world.entities()) resolve(e, stack);
}

std::optional<WorldRayHit3D>
raycast_world3d(const World &world, std::span<const EntityId> set,
                const std::function<std::shared_ptr<
                    const native_map::Mesh3D>(const std::string &)>
                    &resolve,
                double ox, double oy, double oz, double dx, double dy,
                double dz, float max_distance) {
  using namespace stellar::native_map;
  const double dlen = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dlen < 1e-8 || max_distance <= 0.f || !resolve)
    return std::nullopt;
  dx /= dlen;
  dy /= dlen;
  dz /= dlen;
  std::optional<WorldRayHit3D> best;
  for (const auto e : set) {
    const auto *t = world.get<Transform3D>(e);
    const auto *mr = world.get<MeshRef>(e);
    if (!t || !mr) continue;
    const auto mesh = resolve(mr->spec);
    if (!mesh) continue;
    const float qlen = std::sqrt(t->qx * t->qx + t->qy * t->qy +
                                 t->qz * t->qz + t->qw * t->qw);
    if (qlen < 1e-8f || t->scale <= 0.f) continue;
    // Ray → mesh local space: local = R⁻¹·(world − pos) / scale.
    const Quaternion inv{-t->qx / qlen, -t->qy / qlen, -t->qz / qlen,
                         t->qw / qlen};
    Vec3 lo = rotate_vec(inv, {static_cast<float>(ox - t->x),
                               static_cast<float>(oy - t->y),
                               static_cast<float>(oz - t->z)});
    lo = {lo.x / t->scale, lo.y / t->scale, lo.z / t->scale};
    const Vec3 ld = rotate_vec(inv, {static_cast<float>(dx),
                                     static_cast<float>(dy),
                                     static_cast<float>(dz)});
    // World distance d maps to d/scale local units — so a local segment
    // of max_distance/scale covers the ray, and the returned fraction ×
    // max_distance is the world-space hit distance.
    const double local_max = max_distance / t->scale;
    const CollisionVector3 from{lo.x, lo.y, lo.z};
    const CollisionVector3 to{lo.x + ld.x * local_max,
                              lo.y + ld.y * local_max,
                              lo.z + ld.z * local_max};
    const auto hit = intersect_mesh_segment(*mesh, from, to);
    if (!hit) continue;
    const float dist = static_cast<float>(hit->fraction) * max_distance;
    if (!best || dist < best->distance)
      best = WorldRayHit3D{e, dist,
                           static_cast<float>(ox + dx * dist),
                           static_cast<float>(oy + dy * dist),
                           static_cast<float>(oz + dz * dist)};
  }
  return best;
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
