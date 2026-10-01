#pragma once

#include <stellar/engine/animation.hpp>
#include <stellar/engine/foundation.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// General particle/VFX framework. Emitter definitions are data (spawn rate,
// lifetime, curves, sprite); each running emitter is a deterministic
// fixed-capacity particle pool stepped on simulation time. Rendering reads
// the pools each frame — this module owns simulation only.
//
// Determinism: each emitter instance owns a DeterministicRandom seeded from
// (definition hash, spawn ordinal), so identical event streams produce
// identical particles regardless of thread scheduling.

struct VfxVec3 {
  float x{}, y{}, z{};
};

struct EmitterDefinition {
  std::string id;
  std::string sprite;           // texture/sprite content id
  float spawn_rate_per_second{};
  float particle_lifetime_seconds{1.0f};
  // Velocity range: each axis samples uniform(min,max).
  VfxVec3 velocity_min, velocity_max;
  float spread_radians{};       // cone spread around velocity dir
  VfxVec3 gravity{};

  FloatCurve scale_over_life;   // default constant 1
  FloatCurve opacity_over_life; // default constant 1
  // RGB tint curves; empty = white.
  FloatCurve tint_r, tint_g, tint_b;

  std::uint32_t max_particles{256};
  // LOD: beyond `lod_fade_distance` the emitter's effective rate scales down
  // to `lod_min_rate_scale`; attachments keep a full-rate option.
  float lod_fade_distance{0.0f};
  float lod_min_rate_scale{0.0f};
};

struct Particle {
  VfxVec3 position;
  VfxVec3 velocity;
  float age{};
  float lifetime{};
  float seed{};
};

using VfxInstanceId = std::uint64_t;
inline constexpr VfxInstanceId invalid_vfx_instance = 0;
// RuntimeHost's default cross-emitter particle cap; unlimited stays
// opt-in via set_particle_budget(0).
inline constexpr std::size_t default_particle_budget = 65536;

struct VfxStats {
  std::size_t live_instances{};
  std::size_t live_particles{};
  // Global soft budget across all live emitters (0 = unlimited) and the
  // rate scale the last advance() applied as residency neared the cap.
  std::size_t particle_budget{};
  float budget_scale{1.0f};
};

class VfxSystem {
public:
  explicit VfxSystem(std::size_t particle_budget = 0)
      : particle_budget_(particle_budget) {}

  void define(EmitterDefinition definition);
  const EmitterDefinition *definition(std::string_view id) const;

  // Spawns an emitter at a world position, optionally attached to an entity.
  VfxInstanceId spawn(std::string_view emitter_id, VfxVec3 position,
                      EntityId attached = {});
  void stop(VfxInstanceId instance);
  bool alive(VfxInstanceId instance) const;

  // Moves the anchor position (for attached or manually-driven emitters).
  void set_position(VfxInstanceId instance, VfxVec3 position);

  // Advances every live emitter by dt simulation seconds. `camera_distance`
  // per-instance LOD uses set_lod_distance below (0 = full rate).
  void advance(double dt_seconds);
  void set_lod_distance(VfxInstanceId instance, float distance);

  // Global particle budget across all live emitters (0 = unlimited).
  // Each advance() computes the frame's total spawn demand, tapers every
  // emitter's rate by the share the remaining headroom admits, and enforces
  // the leftover as a hard counter — live particles never exceed the cap.
  void set_particle_budget(std::size_t particles);
  [[nodiscard]] std::size_t particle_budget() const noexcept;

  // Read-only particle access for rendering.
  std::span<const Particle> particles(VfxInstanceId instance) const;
  // Evaluates presentation curves for a particle's normalized age.
  struct Visual {
    float scale{1.0f}, opacity{1.0f};
    float r{1.0f}, g{1.0f}, b{1.0f};
  };
  Visual visual_for(const EmitterDefinition &def, float age) const;

  VfxStats stats() const;
  std::size_t live_instance_count() const;

private:
  struct Instance {
    std::string definition_id;
    VfxVec3 position;
    EntityId attached{};
    std::vector<Particle> pool;
    DeterministicRandom rng{0};
    float spawn_credit{};
    float lod_distance{};
    bool active{true};
  };

  std::unordered_map<std::string, EmitterDefinition> definitions_;
  std::unordered_map<VfxInstanceId, Instance> instances_;
  VfxInstanceId next_instance_id_{1};
  std::uint64_t spawn_ordinal_{};
  std::size_t particle_budget_{};
  float budget_scale_{1.0f};
};

} // namespace stellar::engine
