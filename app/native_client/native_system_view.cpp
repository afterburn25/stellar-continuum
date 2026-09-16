#include "native_system_view.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/construction_projects.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <stdexcept>
#include <unordered_map>

namespace stellar::native_system {
namespace {
using namespace stellar::core;
constexpr float pi = 3.14159265358979323846f;

NativeSystemBodyVisualClass visual_class(const PlanetaryBody &body,
                                          const bool detailed,
                                          const bool canonical_sol) {
  if (!detailed)
    return body.kind == PlanetaryBodyKind::Moon
               ? NativeSystemBodyVisualClass::unknown_moon
               : NativeSystemBodyVisualClass::unknown_planet;
  if (body.kind == PlanetaryBodyKind::Moon)
    return NativeSystemBodyVisualClass::moon;
  if (canonical_sol) {
    if (body.name == "Jupiter" || body.name == "Saturn")
      return NativeSystemBodyVisualClass::gas_giant;
    if (body.name == "Uranus" || body.name == "Neptune")
      return NativeSystemBodyVisualClass::ice_giant;
  }
  if (!body.environment.has_solid_surface)
    return body.environment.temperature_kelvin < 170.
               ? NativeSystemBodyVisualClass::ice_giant
               : NativeSystemBodyVisualClass::gas_giant;
  if (body.environment.is_immersed_environment)
    return NativeSystemBodyVisualClass::oceanic;
  if (body.environment.temperature_kelvin < 200.)
    return NativeSystemBodyVisualClass::frozen;
  if (body.environment.temperature_kelvin > 410.)
    return NativeSystemBodyVisualClass::hot_rocky;
  return NativeSystemBodyVisualClass::rocky;
}

std::optional<std::string> sol_texture(const PlanetaryBody &body,
                                       const bool eligible) {
  if (!eligible || body.id == pluto_body_id) return std::nullopt;
  static constexpr std::string_view names[]{"Mercury", "Venus", "Earth", "Mars",
      "Jupiter", "Saturn", "Uranus", "Neptune", "Moon"};
  if (std::ranges::find(names, body.name) == std::end(names)) return std::nullopt;
  std::string key = body.name;
  std::ranges::transform(key, key.begin(), [](const unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return key;
}

float stable_angle(const int body_id) {
  auto value = (static_cast<std::uint32_t>(body_id) + 1u) * 2654435761u;
  value ^= value >> 16u;
  return static_cast<float>(value & 0x00ffffffu) / 16777216.f * pi * 2.f;
}
float distance_squared(float ax, float ay, float bx, float by) {
  const auto dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}
} // namespace

void NativeSystemViewController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native system views must be built on the simulation owner thread.");
}
void NativeSystemViewController::bind_generation(const std::uint64_t value) {
  if (generation_ && value < *generation_)
    throw std::invalid_argument("A stale campaign generation cannot replace the current system view.");
  generation_ = value;
}
bool NativeSystemViewController::is_current_generation(const std::uint64_t value) const noexcept {
  return generation_ && *generation_ == value;
}

NativeSystemViewResult NativeSystemViewController::build(
    CampaignFrame &frame, const std::uint64_t generation,
    const int selected_system_id) {
  require_owner();
  bind_generation(generation);
  auto &world = frame.runtime().world().campaign();
  const auto observer = world.player_civilization_id;
  const auto player = std::ranges::find(world.civilizations, observer, &Civilization::id);
  if (player == world.civilizations.end() || !player->is_player)
    throw std::runtime_error("The campaign has no valid player civilization.");

  // This is the secrecy boundary. Do not find or copy raw system/body records first.
  const auto level = world.knowledge.system_survey_level(observer, selected_system_id);
  if (!world.knowledge.is_system_known(observer, selected_system_id) ||
      level < SystemSurveyLevel::partially_surveyed)
    return {std::nullopt, "Orbital details require reconnaissance of this system."};

  const auto system = std::ranges::find(world.systems, selected_system_id,
                                         &StellarSystem::id);
  if (system == world.systems.end())
    return {std::nullopt, "The known system is unavailable in this campaign."};
  const bool detailed = level == SystemSurveyLevel::fully_surveyed;
  const bool canonical_sol = detailed && system->catalog_preset_id &&
                             *system->catalog_preset_id == sol_catalog_preset_id;
  NativeSystemSnapshot result{.campaign_generation = generation,
      .observer_civilization_id = observer, .system_id = system->id,
      .catalog_name = system->name, .survey_level = level,
      .survey_progress = world.knowledge.system_survey_progress(observer, system->id)};
  if (detailed) {
    result.archetype = system->archetype;
    result.primary_stellar_class = system->primary;
    result.secondary_stellar_class = system->secondary;
    result.tertiary_stellar_class = system->tertiary;
  }
  for (const auto &body : world.bodies) {
    if (body.system_id != system->id) continue;
    NativeSystemBody item{.id = body.id, .parent_body_id = body.parent_body_id,
        .orbit_index = body.orbit_index, .name = body.name, .kind = body.kind,
        .radius_earth = body.radius_earth,
        .orbital_eccentricity = body.orbital_eccentricity,
        .orbital_inclination_degrees = body.orbital_inclination_degrees};
    if (body.has_rare_resource)
      item.positive_signatures.push_back(NativePositiveSignature::rare_resource);
    if (body.has_anomaly)
      item.positive_signatures.push_back(NativePositiveSignature::anomaly);
    if (body.has_pre_warp_civilization)
      item.positive_signatures.push_back(NativePositiveSignature::activity);
    if (detailed)
      item.details = NativeSystemBodyDetails{body.mass_earth,
          body.environment.gravity_g, body.environment.temperature_kelvin,
          body.environment.pressure_kpa, body.environment.atmosphere,
          body.environment.available_solvent, body.environment.radiation_hazard,
          body.environment.is_immersed_environment,
          body.environment.has_solid_surface};
    item.visual_class = visual_class(body, detailed, canonical_sol);
    item.sol_texture_key = sol_texture(body, canonical_sol);
    result.bodies.push_back(std::move(item));
  }
  std::ranges::sort(result.bodies, {}, &NativeSystemBody::id);
  if (system->id == player->home_system_id) {
    // Mirrors the reference spatial canvas: every orbital construction project
    // is marked in the player's home system, including locked and planned
    // sites, hosted at the most populous colony body (the resource network is
    // positioned on the outer chart instead).
    const auto &research = frame.runtime().research();
    const auto query = [&research](int civilization, std::string_view id) {
      return AdaptiveResearchConstructionCapabilityView(research)
          .has_civilization_capability(civilization, id);
    };
    const ConstructionReadView read{world.civilizations, world.bodies,
        world.construction, world.colonies, world.economies, {}, query};
    const auto state = std::ranges::find(world.construction, observer,
                                          &ConstructionState::civilization_id);
    const Colony *host = nullptr;
    for (const auto &colony : world.colonies) {
      if (colony.civilization_id != observer || colony.system_id != system->id ||
          !colony.planetary_body_id)
        continue;
      if (!host || colony.population_millions > host->population_millions)
        host = &colony;
    }
    for (const auto &project : construction_project_catalog()) {
      if (project.category != ConstructionCategory::Orbital) continue;
      const bool complete =
          state != world.construction.end() &&
          std::ranges::find(state->completed_project_ids, project.id) !=
              state->completed_project_ids.end();
      const bool active = state != world.construction.end() &&
                          state->active_project_id == project.id;
      const bool available =
          state != world.construction.end() &&
          !construction_project_lock_reason(read, observer, project);
      NativeSystemInfrastructureMarker marker{.project_id = project.id,
          .label = project.name,
          .state = complete ? NativeInfrastructureState::complete
                   : active ? NativeInfrastructureState::active
                   : available ? NativeInfrastructureState::available
                               : NativeInfrastructureState::locked,
          .progress = complete ? 1.
                      : active && project.industry_cost > 0
                          ? std::clamp(state->active_project_progress /
                                           project.industry_cost,
                                       0., 1.)
                          : 0.};
      if (project.id != "asteroid_resource_network" && host)
        marker.host_body_id = host->planetary_body_id;
      result.infrastructure.push_back(std::move(marker));
    }
  }
  return {std::move(result), {}};
}

float body_display_radius(const double value, const PlanetaryBodyKind kind) {
  const auto radius = std::isfinite(value) ? std::clamp(value, .01, 100.) : .01;
  return kind == PlanetaryBodyKind::Moon
             ? std::clamp(14.f * static_cast<float>(radius), 1.8f, 24.f)
             : std::clamp(14.f * std::pow(static_cast<float>(radius), .9f), 4.f, 160.f);
}
float star_screen_radius(const float scale) { return std::max(4.f, 560.f * scale); }
SystemSpatialPoint orbit_point(const float radius, const float eccentricity,
    const float inclination_degrees, const float eccentric_anomaly) {
  const auto e = std::clamp(eccentricity, 0.f, .95f);
  const auto inclination = inclination_degrees * pi / 180.f;
  const auto x = radius * (std::cos(eccentric_anomaly) - e);
  const auto z = radius * std::sqrt(1.f - e * e) * std::sin(eccentric_anomaly);
  const auto y = z * std::cos(inclination);
  const auto angle = e > 0.f ? .62f : 0.f;
  return {x * std::cos(angle) - y * std::sin(angle),
          x * std::sin(angle) + y * std::cos(angle), z * std::sin(inclination)};
}
std::vector<SystemSpatialPoint> orbit_path(const float radius,
    const float eccentricity, const float inclination_degrees) {
  std::vector<SystemSpatialPoint> result;
  result.reserve(193);
  for (int index = 0; index <= 192; ++index)
    result.push_back(orbit_point(radius, eccentricity, inclination_degrees,
                                2.f * pi * static_cast<float>(index) / 192.f));
  return result;
}

SystemSpatialSnapshot project_system(const NativeSystemSnapshot &system) {
  if (system.survey_level < SystemSurveyLevel::partially_surveyed)
    throw std::invalid_argument("System projection requires reconnaissance-grade knowledge.");
  SystemSpatialSnapshot result{.system_id = system.system_id};
  std::vector<const NativeSystemBody *> planets;
  for (const auto &body : system.bodies)
    if (body.kind != PlanetaryBodyKind::Moon) planets.push_back(&body);
  std::ranges::sort(planets, [](const auto *left, const auto *right) {
    return left->orbit_index < right->orbit_index ||
           (left->orbit_index == right->orbit_index && left->id < right->id);
  });
  std::map<int, float> family_extents;
  auto parent_extent = [](const NativeSystemBody &body) {
    const auto radius = body_display_radius(body.radius_earth, body.kind);
    return body.sol_texture_key == "saturn" ? radius * 2.8f : radius;
  };
  for (const auto *planet : planets) {
    auto extent = parent_extent(*planet);
    for (const auto &moon : system.bodies)
      if (moon.kind == PlanetaryBodyKind::Moon && moon.parent_body_id == planet->id) {
        const auto moon_radius = body_display_radius(moon.radius_earth, moon.kind);
        extent = std::max(extent, parent_extent(*planet) + 40.f +
            static_cast<float>(moon.orbit_index) * 24.f + 2.f * moon_radius);
      }
    family_extents.emplace(planet->id, extent);
  }
  float extent_sum = 0.f;
  for (const auto &[id, extent] : family_extents) { (void)id; extent_sum += extent; }
  auto next_orbit = std::max(1700.f, 3.f * extent_sum);
  float previous_extent = 0.f;
  struct ParentPosition { float x{}, y{}, extent{}; };
  std::unordered_map<int, ParentPosition> positions;
  for (std::size_t index = 0; index < planets.size(); ++index) {
    const auto &body = *planets[index];
    if (index > 0) next_orbit += previous_extent + family_extents.at(body.id) + 160.f;
    previous_extent = family_extents.at(body.id);
    const auto point = orbit_point(next_orbit, static_cast<float>(body.orbital_eccentricity),
                                   static_cast<float>(body.orbital_inclination_degrees),
                                   stable_angle(body.id));
    const auto radius = body_display_radius(body.radius_earth, body.kind);
    result.bodies.push_back({body.id, body.parent_body_id, body.orbit_index,
        body.name, body.kind, body.visual_class, point.x, point.y, point.height,
        next_orbit, radius, body.orbital_eccentricity,
        body.orbital_inclination_degrees, body.positive_signatures,
        body.sol_texture_key});
    positions.emplace(body.id, ParentPosition{point.x, point.y, parent_extent(body)});
  }
  std::vector<const NativeSystemBody *> moons;
  for (const auto &body : system.bodies)
    if (body.kind == PlanetaryBodyKind::Moon) moons.push_back(&body);
  std::ranges::sort(moons, [](const auto *left, const auto *right) {
    return left->parent_body_id < right->parent_body_id ||
           (left->parent_body_id == right->parent_body_id &&
            (left->orbit_index < right->orbit_index ||
             (left->orbit_index == right->orbit_index && left->id < right->id)));
  });
  for (const auto *body : moons) {
    if (!body->parent_body_id || !positions.contains(*body->parent_body_id))
      throw std::invalid_argument("Observer-safe moon has no visible parent planet.");
    const auto parent = positions.at(*body->parent_body_id);
    const auto radius = body_display_radius(body->radius_earth, body->kind);
    const auto orbit = parent.extent + 40.f + body->orbit_index * 24.f + radius;
    const auto angle = stable_angle(body->id ^ *body->parent_body_id);
    result.bodies.push_back({body->id, body->parent_body_id, body->orbit_index,
        body->name, body->kind, body->visual_class,
        parent.x + std::cos(angle) * orbit, parent.y + std::sin(angle) * orbit,
        0.f, orbit, radius, body->orbital_eccentricity,
        body->orbital_inclination_degrees, body->positive_signatures,
        body->sol_texture_key});
  }
  float orbital_extent = 150.f;
  for (const auto &marker : result.bodies) {
    float extent{};
    if (marker.kind != PlanetaryBodyKind::Moon)
      extent = marker.orbit_radius * (1.f + static_cast<float>(marker.orbital_eccentricity)) +
               family_extents.at(marker.body_id);
    else if (marker.parent_body_id && positions.contains(*marker.parent_body_id)) {
      const auto parent = positions.at(*marker.parent_body_id);
      extent = std::hypot(parent.x, parent.y) + marker.orbit_radius + marker.display_radius;
    } else extent = std::hypot(marker.offset_x, marker.offset_y);
    orbital_extent = std::max(orbital_extent, extent);
  }
  result.design_radius = std::max(180.f, orbital_extent + 30.f);
  std::ranges::sort(result.bodies, {}, &SystemSpatialBodyMarker::body_id);
  return result;
}

SystemSpatialViewport SystemSpatialViewport::fit(const SystemSpatialSnapshot &snapshot,
                                                  const float width,
                                                  const float height) {
  const auto left = 104.f, right = std::max(left + 2.f, width - 224.f);
  const auto top = std::min(146.f, std::max(0.f, height - 131.f));
  const auto bottom = std::max(top + 1.f, height - 130.f);
  const auto x = (left + right) * .5f, y = (top + bottom) * .5f;
  const auto available = std::max(1.f, std::min({x - left, right - x, y - top, bottom - y}));
  return {x, y, std::min(.95f * available / snapshot.design_radius, 1.15f)};
}
SystemSpatialPoint SystemSpatialViewport::world_to_screen(float x, float y) const {
  return {center_x + x * scale, center_y + y * scale, 0.f};
}
SystemSpatialPoint SystemSpatialViewport::screen_to_world(float x, float y) const {
  if (!std::isfinite(scale) || scale <= 0.f) throw std::logic_error("Viewport scale must be positive.");
  return {(x - center_x) / scale, (y - center_y) / scale, 0.f};
}
float SystemSpatialViewport::body_radius(const SystemSpatialBodyMarker &body) const {
  return std::max(.9f, body.display_radius * scale);
}
bool SystemSpatialViewport::is_body_visible(const SystemSpatialSnapshot &snapshot,
                                             const SystemSpatialBodyMarker &body) const {
  if (body.kind != PlanetaryBodyKind::Moon) return true;
  if (!body.parent_body_id) return false;
  const auto parent = std::ranges::find(snapshot.bodies, *body.parent_body_id,
                                         &SystemSpatialBodyMarker::body_id);
  return parent != snapshot.bodies.end() &&
         body.orbit_radius * scale > body_radius(*parent) + body_radius(body) + 5.f;
}
std::optional<int> SystemSpatialViewport::hit_body(const SystemSpatialSnapshot &snapshot,
                                                    float x, float y) const {
  std::optional<int> nearest;
  auto nearest_distance = std::numeric_limits<float>::max();
  for (const auto &body : snapshot.bodies) {
    if (!is_body_visible(snapshot, body)) continue;
    const auto point = world_to_screen(body.offset_x, body.offset_y);
    const auto distance = distance_squared(x, y, point.x, point.y);
    const auto radius = body_radius(body) *
        (body.sol_texture_key == "saturn" ? 2.25f : 1.f) + 4.f;
    if (distance <= radius * radius && distance < nearest_distance) {
      nearest = body.body_id;
      nearest_distance = distance;
    }
  }
  return nearest;
}
bool SystemSpatialViewport::hits_star(float x, float y) const {
  const auto radius = star_screen_radius(scale) * 1.75f;
  return distance_squared(x, y, center_x, center_y) <= radius * radius;
}
SystemSpatialViewport SystemSpatialViewport::translated(float dx, float dy) const {
  return {center_x + dx, center_y + dy, scale};
}
SystemSpatialViewport SystemSpatialViewport::zoomed_at(float factor, float x, float y,
    float minimum_scale, float maximum_scale) const {
  if (!std::isfinite(factor) || factor <= 0.f || !std::isfinite(minimum_scale) ||
      !std::isfinite(maximum_scale) || minimum_scale <= 0.f || maximum_scale < minimum_scale)
    throw std::invalid_argument("Zoom bounds and factor must be positive and ordered.");
  const auto anchor = screen_to_world(x, y);
  const auto next = std::clamp(scale * factor, minimum_scale, maximum_scale);
  return {x - anchor.x * next, y - anchor.y * next, next};
}
} // namespace stellar::native_system
