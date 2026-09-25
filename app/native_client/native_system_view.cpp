#include "native_system_view.hpp"
#include <stellar/core/campaign_observation.hpp>
#include <stellar/engine/localization.hpp>

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
  if(body.stellar_exposure&&body.stellar_exposure->baked&&body.environment.has_solid_surface)
    return NativeSystemBodyVisualClass::hot_rocky;
  if (body.kind == PlanetaryBodyKind::Moon)
    return NativeSystemBodyVisualClass::moon;
  if (canonical_sol) {
    if (body.name == "Jupiter" || body.name == "Saturn")
      return NativeSystemBodyVisualClass::gas_giant;
    if (body.name == "Uranus" || body.name == "Neptune")
      return NativeSystemBodyVisualClass::ice_giant;
  }
  if (!body.environment.has_solid_surface)
    return classify_planetary_world(body, canonical_sol) == PlanetaryWorldClass::IceGiant
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
std::string NativeSystemViewController::tr(std::string_view key,
                                           std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
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
  const auto level = observation_survey_level(world, observer, selected_system_id);
  if (level < SystemSurveyLevel::partially_surveyed)
    return {std::nullopt, tr("SYSTEM_DENY_SURVEY", "Orbital details require reconnaissance of this system.")};

  const auto system = std::ranges::find(world.systems, selected_system_id,
                                         &StellarSystem::id);
  if (system == world.systems.end())
    return {std::nullopt, tr("SYSTEM_DENY_UNAVAILABLE", "The known system is unavailable in this campaign.")};
  const bool detailed = level == SystemSurveyLevel::fully_surveyed;
  const bool canonical_sol = detailed && system->catalog_preset_id &&
                             *system->catalog_preset_id == sol_catalog_preset_id;
  NativeSystemSnapshot result{.campaign_generation = generation,
      .observer_civilization_id = observer, .system_id = system->id,
      .catalog_name = system->name, .survey_level = level,
      .survey_progress = developer_observation(world, observer) ? 1. : world.knowledge.system_survey_progress(observer, system->id)};
  if (detailed) {
    result.small_body_fields=system->small_body_fields.value_or(std::vector<SmallBodyField>{});
    result.stellar_object=system->stellar_object;
    result.stellar_orbits=system->stellar_orbits;
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
    if (detailed){
      item.appearance=body.appearance;
      item.details = NativeSystemBodyDetails{body.mass_earth,
          body.environment.gravity_g, body.environment.temperature_kelvin,
          body.environment.pressure_kpa, body.environment.atmosphere,
          body.environment.available_solvent, body.environment.radiation_hazard,
          body.environment.is_immersed_environment,
          body.environment.has_solid_surface,body.stellar_exposure};
    }
    if (detailed) item.world_class = classify_planetary_world(body, canonical_sol);
    item.visual_class = visual_class(body, detailed, canonical_sol);
    item.sol_texture_key = sol_texture(body, canonical_sol);
    item.orbit_au=planetary_orbit_au(*system,body);
    if(item.appearance){const auto host=stellar_host_physics(*system,planetary_stellar_host(*system,body.id));item.appearance->tidally_locked=planet_is_tidally_locked(body,&host);}
    if(detailed){item.stellar_host=planetary_stellar_host(*system,body.id);if(!body.parent_body_id)item.stellar_orbit=planetary_stellar_orbit(*system,body);
      else {const auto parent=std::ranges::find(world.bodies,*body.parent_body_id,&PlanetaryBody::id);
        if(parent==world.bodies.end())throw std::logic_error("Missing satellite parent");
        item.satellite_orbit=planetary_satellite_orbit(*parent,body);item.stellar_host=planetary_stellar_host(*system,parent->id);}
    }
    result.bodies.push_back(std::move(item));
  }
  std::ranges::sort(result.bodies, {}, &NativeSystemBody::id);
  result.simulation_days=frame.clock().simulation_days();
  result.developer=world.developer_provenance.has_value();
  return {std::move(result), {}};
}

float body_display_radius(const double value, const PlanetaryBodyKind kind) {
  const auto radius = std::isfinite(value) ? std::clamp(value, .01, 100.) : .01;
  return kind == PlanetaryBodyKind::Moon
             ? static_cast<float>(small_body_configuration().moon_scale)*std::clamp(14.f * static_cast<float>(radius), 1.8f, 24.f)
             : static_cast<float>(small_body_configuration().planet_scale)*std::clamp(14.f * std::pow(static_cast<float>(radius), .9f), 4.f, 160.f);
}
float star_screen_radius(const float scale,const float visual_scale) {
  return std::min(350.f,std::max(4.f,560.f*scale)*visual_scale);
}
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
  const auto orbit_au=[](const NativeSystemBody& b){return b.orbit_au>0?b.orbit_au:.4*std::pow(1.85,b.orbit_index);};
  std::ranges::sort(planets, [&](const auto *left, const auto *right) {
    return std::pair{orbit_au(*left),left->id}<std::pair{orbit_au(*right),right->id};
  });
  std::map<int, float> family_extents;
  auto parent_extent = [](const NativeSystemBody &body) {
    const auto radius = body_display_radius(body.radius_earth, body.kind);
    if(body.appearance&&body.appearance->rings.enabled)return radius*static_cast<float>(body.appearance->rings.outer_radius);
    return body.sol_texture_key == "saturn" ? radius * 2.8f : radius;
  };
  const auto moon_radius=[&](const NativeSystemBody& parent,const NativeSystemBody& moon){
    if(!moon.satellite_orbit)return parent_extent(parent)+40.f+moon.orbit_index*24.f+body_display_radius(moon.radius_earth,moon.kind);
    const auto relative=moon.satellite_orbit->relative.radius/(parent.radius_earth*6371.);
    return parent_extent(parent)+body_display_radius(parent.radius_earth,parent.kind)*static_cast<float>(std::pow(relative,.6))+24.f;
  };
  for (const auto *planet : planets) {
    auto extent = parent_extent(*planet);
    for (const auto &moon : system.bodies)
      if (moon.kind == PlanetaryBodyKind::Moon && moon.parent_body_id == planet->id) {
        extent = std::max(extent, moon_radius(*planet,moon)*static_cast<float>(1+moon.orbital_eccentricity)+2*body_display_radius(moon.radius_earth,moon.kind));
      }
    family_extents.emplace(planet->id, extent);
  }
  const auto& spacing=small_body_configuration();
  // Build one monotonic AU mapping for planets, orbit guides and belt particles.
  // Reserve display space at the edges of each entire eccentric moon family;
  // exaggerated planet and asteroid sizes must not visually erase a real gap.
  struct Knot{float left{},right{};};std::map<double,Knot> knots;
  for(const auto* b:planets){const double a=orbit_au(*b),e=std::clamp(b->orbital_eccentricity,0.,.9);
    knots[a];knots[a*(1-e)].left=std::max(knots[a*(1-e)].left,family_extents.at(b->id)+110.f);
    knots[a*(1+e)].right=std::max(knots[a*(1+e)].right,family_extents.at(b->id)+110.f);
  }
  for(const auto& f:system.small_body_fields)if(!f.planet_centered){knots[f.inner_radius_au];knots[f.outer_radius_au];}
  double prior_au=0;float prior_display=0,prior_padding=1100;
  for(const auto& [a,knot]:knots){
    const auto delta=static_cast<float>(stellar::engine::stretched_orbit_radius(a,spacing.orbit_unit,spacing.orbit_exponent)-stellar::engine::stretched_orbit_radius(prior_au,spacing.orbit_unit,spacing.orbit_exponent));
    const float display=prior_display+std::max(delta,prior_padding+knot.left);
    result.orbit_anchors.emplace_back(a,display);prior_au=a;prior_display=display;prior_padding=knot.right;
  }
  struct ParentPosition { float x{}, y{}, extent{}; };
  std::unordered_map<int, ParentPosition> positions;
  for (std::size_t index = 0; index < planets.size(); ++index) {
    const auto &body = *planets[index];
    const double au=orbit_au(body);const float next_orbit=system_display_orbit_radius(result,au);
    const auto point = projected_orbit_point(result,au, static_cast<float>(body.orbital_eccentricity),
                                   static_cast<float>(body.orbital_inclination_degrees),
                                   stable_angle(body.id));
    const auto radius = body_display_radius(body.radius_earth, body.kind);
    result.bodies.push_back({body.id, body.parent_body_id, body.orbit_index,
        body.name, body.kind, body.visual_class, point.x, point.y, point.height,
        next_orbit, radius, body.orbital_eccentricity,
        body.orbital_inclination_degrees, body.positive_signatures,
        body.sol_texture_key,au,body.stellar_host});
    positions.emplace(body.id, ParentPosition{point.x, point.y, parent_extent(body)});
    result.bodies.back().stellar_orbit=body.stellar_orbit;
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
    const auto parent_state=std::ranges::find(system.bodies,*body->parent_body_id,&NativeSystemBody::id);
    const auto orbit = moon_radius(*parent_state,*body);
    const auto angle = stable_angle(body->id ^ *body->parent_body_id);
    result.bodies.push_back({body->id, body->parent_body_id, body->orbit_index,
        body->name, body->kind, body->visual_class,
        parent.x + std::cos(angle) * orbit, parent.y + std::sin(angle) * orbit,
        0.f, orbit, radius, body->orbital_eccentricity,
        body->orbital_inclination_degrees, body->positive_signatures,
        body->sol_texture_key});
    result.bodies.back().satellite_orbit=body->satellite_orbit;
  }
  float orbital_extent = 150.f;
  for (const auto &marker : result.bodies) {
    float extent{};
    if (marker.kind != PlanetaryBodyKind::Moon)
      extent = system_display_orbit_radius(result,marker.physical_orbit_au*(1+marker.orbital_eccentricity)) +
               family_extents.at(marker.body_id);
    else if (marker.parent_body_id && positions.contains(*marker.parent_body_id)) {
      const auto parent = positions.at(*marker.parent_body_id);
      extent = std::hypot(parent.x, parent.y) + marker.orbit_radius + marker.display_radius;
    } else extent = std::hypot(marker.offset_x, marker.offset_y);
    orbital_extent = std::max(orbital_extent, extent);
  }
  result.design_radius = std::max(180.f, orbital_extent + 30.f);
  for(const auto& field:system.small_body_fields)if(!field.planet_centered)
    result.design_radius=std::max(result.design_radius,system_display_orbit_radius(result,field.outer_radius_au)*1.02f);
  std::ranges::sort(result.bodies, {}, &SystemSpatialBodyMarker::body_id);
  if(system.stellar_orbits){const auto& a=*system.stellar_orbits;
    // Physical star orbits retain their mass ratios. Display AU are expanded
    // locally so exaggerated globes and their moon families never overlap.
    const float local_extent=result.design_radius;
    const float inner_display=a.belt_host==3?std::max(1500.f,system_display_orbit_radius(result,a.relative_orbits[0].radius)):local_extent*3.f;
    result.stellar_orbit_scales[0]=inner_display/static_cast<float>(a.relative_orbits[0].radius);
    result.design_radius=inner_display*(1+static_cast<float>(a.relative_orbits[0].eccentricity))+local_extent;
    if(a.relative_orbits.size()>1){const float outer_display=result.design_radius*2.7f;
      result.stellar_orbit_scales[1]=outer_display/static_cast<float>(a.relative_orbits[1].radius);
      result.design_radius+=outer_display*(1+static_cast<float>(a.relative_orbits[1].eccentricity));}
    result.belt_host=a.belt_host;
  }
  update_system_motion(system,result);
  return result;
}

StellarSystem snapshot_stellar_system(const NativeSystemSnapshot& s){StellarSystem value;value.id=s.system_id;value.name=s.catalog_name;value.primary=s.primary_stellar_class;value.secondary=s.secondary_stellar_class;value.tertiary=s.tertiary_stellar_class;value.stellar_object=s.stellar_object;value.stellar_orbits=s.stellar_orbits;return value;}
void update_system_motion(const NativeSystemSnapshot& snapshot,SystemSpatialSnapshot& spatial){
  const auto system=snapshot_stellar_system(snapshot);
  const auto physical=stellar_positions(system,snapshot.simulation_days);
  const auto projected_star=[&](const std::array<StellarPosition,4>& positions,int i){
    const auto& center=positions[3];const float inner=spatial.stellar_orbit_scales[0],outer=spatial.stellar_orbit_scales[1];
    if(i<2)return SystemSpatialPoint{static_cast<float>((positions[i][0]-center[0])*inner+center[0]*outer),static_cast<float>((positions[i][1]-center[1])*inner+center[1]*outer),0};
    return SystemSpatialPoint{static_cast<float>(positions[i][0]*outer),static_cast<float>(positions[i][1]*outer),0};
  };
  for(int i=0;i<4;++i)spatial.stellar_hosts[i]=projected_star(physical,i);
  // The inner orbit is translated by the current outer barycentre; the outer
  // component traces its own full Kepler orbit about the system barycentre.
  if(snapshot.stellar_orbits){const auto& a=*snapshot.stellar_orbits;
    const double ma=stellar_host_physics(system,0).mass_solar,mb=a.companions[0].mass_solar,mab=ma+mb;
    for(int i=0;i<static_cast<int>(a.companions.size()+1);++i){auto& path=spatial.stellar_paths[i];path.clear();path.reserve(129);
      const auto orbit=a.relative_orbits[i==2?1:0];const double mass_factor=i==0?-mb/mab:i==1?ma/mab:mab/(mab+a.companions[1].mass_solar);
      for(int j=0;j<=128;++j){auto o=orbit;o.phase=2*std::numbers::pi*j/128;const auto p=stellar::engine::analytic_orbit_position(o,0);
        const auto c=i<2?spatial.stellar_hosts[3]:SystemSpatialPoint{};const double scale=spatial.stellar_orbit_scales[i==2?1:0]*mass_factor;
        path.push_back({c.x+static_cast<float>(p[0]*scale),c.y+static_cast<float>(p[1]*scale),0});}
    }
  }
  std::map<int,SystemSpatialPoint> parent_delta;
  for(auto& marker:spatial.bodies)if(!marker.parent_body_id){const auto found=std::ranges::find(snapshot.bodies,marker.body_id,&NativeSystemBody::id);
    if(found==snapshot.bodies.end()||!found->stellar_orbit)continue;
    const auto p=stellar::engine::analytic_orbit_position(*found->stellar_orbit,snapshot.simulation_days);
    const auto center=spatial.stellar_hosts[found->stellar_host];const double radius=std::hypot(p[0],p[1],p[2]);
    const double scale=radius>0?system_display_orbit_radius(spatial,radius)/radius:0;
    const float x=center.x+static_cast<float>(p[0]*scale),y=center.y+static_cast<float>(p[1]*scale);
    parent_delta[marker.body_id]={x-marker.offset_x,y-marker.offset_y,0};marker.offset_x=x;marker.offset_y=y;marker.offset_height=static_cast<float>(p[2]*scale);
  }
  for(auto& moon:spatial.bodies)if(moon.parent_body_id){
    auto parent=std::ranges::find(spatial.bodies,*moon.parent_body_id,&SystemSpatialBodyMarker::body_id);
    if(parent==spatial.bodies.end())continue;
    if(moon.satellite_orbit){
      const auto& o=*moon.satellite_orbit;const auto p=satellite_relative_position(o,snapshot.simulation_days);
      const double scale=moon.orbit_radius/o.relative.radius;
      // For Pluto-Charon the stellar position is the system barycentre.
      // Re-evaluate the primary above every time, preventing accumulated drift.
      if(o.binary){parent->offset_x-=static_cast<float>(p[0]*scale*o.parent_mass_fraction);parent->offset_y-=static_cast<float>(p[1]*scale*o.parent_mass_fraction);parent->offset_height-=static_cast<float>(p[2]*scale*o.parent_mass_fraction);}
      moon.offset_x=parent->offset_x+static_cast<float>(p[0]*scale);moon.offset_y=parent->offset_y+static_cast<float>(p[1]*scale);moon.offset_height=parent->offset_height+static_cast<float>(p[2]*scale);
    }else if(parent_delta.contains(*moon.parent_body_id)){const auto d=parent_delta.at(*moon.parent_body_id);moon.offset_x+=d.x;moon.offset_y+=d.y;}
  }
}

float system_display_orbit_radius(const SystemSpatialSnapshot& spatial,double au){
  const auto& c=small_body_configuration();
  const auto transform=[&](double r){return stellar::engine::stretched_orbit_radius(r,c.orbit_unit,c.orbit_exponent);};
  double previous_au=0;float previous_display=0;
  for(const auto& [radius,display]:spatial.orbit_anchors){
    if(radius<=previous_au)continue;
    if(au<=radius){const double t=(transform(au)-transform(previous_au))/(transform(radius)-transform(previous_au));return std::lerp(previous_display,display,static_cast<float>(t));}
    previous_au=radius;previous_display=display;
  }
  return previous_display+static_cast<float>(transform(au)-transform(previous_au));
}
SystemSpatialPoint projected_orbit_point(const SystemSpatialSnapshot& spatial,double au,float eccentricity,float inclination,float anomaly){
  const auto physical=orbit_point(static_cast<float>(au),eccentricity,inclination,anomaly);
  const double radius=std::hypot(physical.x,physical.y,physical.height);
  const float display=system_display_orbit_radius(spatial,radius),factor=radius>0?static_cast<float>(display/radius):0;
  return {physical.x*factor,physical.y*factor,physical.height*factor};
}
std::vector<SystemSpatialPoint> projected_orbit_path(const SystemSpatialSnapshot& spatial,const SystemSpatialBodyMarker& body){
  if(body.satellite_orbit){std::vector<SystemSpatialPoint> path;path.reserve(193);
    for(int i=0;i<=192;++i){auto o=*body.satellite_orbit;o.relative.phase=2*std::numbers::pi*i/192;const auto p=satellite_relative_position(o,0);const double scale=body.orbit_radius/o.relative.radius*(o.binary?1-o.parent_mass_fraction:1);
      path.push_back({static_cast<float>(p[0]*scale),static_cast<float>(p[1]*scale),static_cast<float>(p[2]*scale)});}
    return path;
  }
  if(body.parent_body_id||body.physical_orbit_au<=0)return orbit_path(body.orbit_radius,0,0);
  if(body.stellar_orbit){std::vector<SystemSpatialPoint> path;path.reserve(193);
    for(int i=0;i<=192;++i){auto o=*body.stellar_orbit;o.phase=2*std::numbers::pi*i/192;const auto p=stellar::engine::analytic_orbit_position(o,0);const auto r=std::hypot(p[0],p[1],p[2]);const auto scale=system_display_orbit_radius(spatial,r)/r;
      path.push_back({static_cast<float>(p[0]*scale),static_cast<float>(p[1]*scale),static_cast<float>(p[2]*scale)});}
    return path;
  }
  std::vector<SystemSpatialPoint> result;result.reserve(193);
  for(int i=0;i<=192;++i)result.push_back(projected_orbit_point(spatial,body.physical_orbit_au,static_cast<float>(body.orbital_eccentricity),static_cast<float>(body.orbital_inclination_degrees),2*pi*i/192));
  return result;
}

double parent_facing_bearing(const SystemSpatialSnapshot& spatial,const SystemSpatialBodyMarker& body){
 auto parent=spatial.stellar_hosts[body.stellar_host];
 if(body.parent_body_id){const auto found=std::ranges::find(spatial.bodies,*body.parent_body_id,&SystemSpatialBodyMarker::body_id);if(found!=spatial.bodies.end())parent={found->offset_x,found->offset_y,0};}
 return std::atan2(body.offset_y-parent.y,parent.x-body.offset_x);
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
  // Resolved satellites need a readable disc against distant star points.
  // Their physical size remains unchanged; separation still controls visibility.
  return std::max(body.kind==PlanetaryBodyKind::Moon?2.8f:.9f, body.display_radius * scale);
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
