#include <stellar/engine/vfx.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace stellar::engine {
namespace {

std::uint64_t hash_seed(std::string_view text, std::uint64_t salt) noexcept {
  std::uint64_t hash = 14695981039346656037ULL ^ salt;
  for (const unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

} // namespace

void VfxSystem::define(EmitterDefinition definition) {
  definitions_[definition.id] = std::move(definition);
}

const EmitterDefinition *VfxSystem::definition(std::string_view id) const {
  const auto found = definitions_.find(std::string(id));
  return found == definitions_.end() ? nullptr : &found->second;
}

VfxInstanceId VfxSystem::spawn(std::string_view emitter_id, VfxVec3 position,
                               EntityId attached) {
  const auto def = definitions_.find(std::string(emitter_id));
  if (def == definitions_.end())
    return invalid_vfx_instance;
  const auto id = next_instance_id_++;
  Instance instance;
  instance.definition_id = def->first;
  instance.position = position;
  instance.attached = attached;
  instance.pool.reserve(def->second.max_particles);
  instance.rng = DeterministicRandom(
      hash_seed(def->first, ++spawn_ordinal_));
  instances_.emplace(id, std::move(instance));
  return id;
}

void VfxSystem::stop(VfxInstanceId instance) {
  const auto found = instances_.find(instance);
  if (found != instances_.end())
    found->second.active = false;
}

bool VfxSystem::alive(VfxInstanceId instance) const {
  const auto found = instances_.find(instance);
  return found != instances_.end() && found->second.active;
}

void VfxSystem::set_position(VfxInstanceId instance, VfxVec3 position) {
  const auto found = instances_.find(instance);
  if (found != instances_.end())
    found->second.position = position;
}

void VfxSystem::set_lod_distance(VfxInstanceId instance, float distance) {
  const auto found = instances_.find(instance);
  if (found != instances_.end())
    found->second.lod_distance = distance;
}

void VfxSystem::set_particle_budget(std::size_t particles) {
  particle_budget_ = particles;
}

std::size_t VfxSystem::particle_budget() const noexcept {
  return particle_budget_;
}

void VfxSystem::advance(double dt_seconds) {
  if (dt_seconds <= 0)
    return;
  // Phase 1: integrate, expire and retire emptied emitters while counting
  // live particles and this frame's total spawn demand — the budget scale
  // needs settled residency and demand before any instance spawns.
  std::size_t live = 0, demand = 0;
  const auto lod_scale = [](const Instance &instance,
                            const EmitterDefinition &def) {
    if (def.lod_fade_distance <= 0.0f || instance.lod_distance <= 0.0f)
      return 1.0f;
    const auto t =
        std::min(1.0f, instance.lod_distance / def.lod_fade_distance);
    return lerp(1.0f, def.lod_min_rate_scale, t);
  };
  for (auto it = instances_.begin(); it != instances_.end();) {
    auto &instance = it->second;
    const auto &def = definitions_.at(instance.definition_id);
    auto &pool = instance.pool;
    for (std::size_t i = 0; i < pool.size();) {
      auto &p = pool[i];
      p.age += static_cast<float>(dt_seconds);
      p.velocity.x += def.gravity.x * static_cast<float>(dt_seconds);
      p.velocity.y += def.gravity.y * static_cast<float>(dt_seconds);
      p.velocity.z += def.gravity.z * static_cast<float>(dt_seconds);
      p.position.x += p.velocity.x * static_cast<float>(dt_seconds);
      p.position.y += p.velocity.y * static_cast<float>(dt_seconds);
      p.position.z += p.velocity.z * static_cast<float>(dt_seconds);
      if (p.age >= p.lifetime) {
        // Swap-remove keeps the pool compact; iteration order inside a
        // frame is deterministic because compaction is deterministic.
        pool[i] = pool.back();
        pool.pop_back();
      } else {
        ++i;
      }
    }
    live += pool.size();
    if (const float lod = lod_scale(instance, def);
        instance.active && lod > 0.0f)
      demand += std::min(
          static_cast<std::size_t>(std::floor(
              instance.spawn_credit +
              def.spawn_rate_per_second * lod *
                  static_cast<float>(dt_seconds))),
          def.max_particles - pool.size());
    if (!instance.active && pool.empty())
      it = instances_.erase(it);
    else
      ++it;
  }

  // Global budget: taper every emitter's spawn rate by the share of demand
  // the headroom admits, then enforce the remainder as a hard counter —
  // the live count can never exceed the budget.
  std::size_t headroom = 0;
  budget_scale_ = 1.0f;
  if (particle_budget_ > 0) {
    headroom = live < particle_budget_ ? particle_budget_ - live : 0;
    if (demand > 0)
      budget_scale_ = std::min(
          1.0f, static_cast<float>(headroom) / static_cast<float>(demand));
  }

  // Phase 2: spawn with LOD and budget scaling applied.
  for (auto &[id, instance] : instances_) {
    (void)id;
    const auto &def = definitions_.at(instance.definition_id);
    auto &pool = instance.pool;

    const float rate_scale = lod_scale(instance, def) * budget_scale_;
    if (instance.active && rate_scale > 0.0f) {
      instance.spawn_credit += static_cast<float>(
          def.spawn_rate_per_second * rate_scale * dt_seconds);
      while (instance.spawn_credit >= 1.0f &&
             pool.size() < def.max_particles &&
             (particle_budget_ == 0 || headroom > 0)) {
        instance.spawn_credit -= 1.0f;
        if (particle_budget_ > 0)
          --headroom;
        Particle p;
        p.position = instance.position;
        const auto sample = [&](float lo, float hi) {
          return static_cast<float>(
              lo + (hi - lo) * instance.rng.unit_double());
        };
        p.velocity = {sample(def.velocity_min.x, def.velocity_max.x),
                      sample(def.velocity_min.y, def.velocity_max.y),
                      sample(def.velocity_min.z, def.velocity_max.z)};
        p.lifetime = def.particle_lifetime_seconds;
        p.seed = static_cast<float>(instance.rng.unit_double());
        pool.push_back(p);
      }
      // Drop fractional credit if the pool is saturated so it cannot bank
      // a burst after leaving the cap.
      if (pool.size() >= def.max_particles)
        instance.spawn_credit = std::min(instance.spawn_credit, 1.0f);
    }
  }
}

std::span<const Particle> VfxSystem::particles(VfxInstanceId instance) const {
  const auto found = instances_.find(instance);
  if (found == instances_.end())
    return {};
  return found->second.pool;
}

VfxSystem::Visual VfxSystem::visual_for(const EmitterDefinition &def,
                                        float age) const {
  const float t = def.particle_lifetime_seconds > 0.0f
                      ? std::clamp(age / def.particle_lifetime_seconds, 0.0f,
                                   1.0f)
                      : 1.0f;
  Visual v;
  if (def.scale_over_life.size() > 0)
    v.scale = def.scale_over_life.evaluate(t);
  if (def.opacity_over_life.size() > 0)
    v.opacity = def.opacity_over_life.evaluate(t);
  if (def.tint_r.size() > 0)
    v.r = def.tint_r.evaluate(t);
  if (def.tint_g.size() > 0)
    v.g = def.tint_g.evaluate(t);
  if (def.tint_b.size() > 0)
    v.b = def.tint_b.evaluate(t);
  return v;
}

VfxStats VfxSystem::stats() const {
  VfxStats result;
  result.particle_budget = particle_budget_;
  result.budget_scale = budget_scale_;
  for (const auto &[id, instance] : instances_) {
    (void)id;
    ++result.live_instances;
    result.live_particles += instance.pool.size();
  }
  return result;
}

std::size_t VfxSystem::live_instance_count() const {
  return instances_.size();
}

} // namespace stellar::engine
