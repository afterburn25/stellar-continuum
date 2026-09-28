#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {
namespace {
const Colony *colony_for(const ConstructionReadView &w, int civ, int id) {
  auto p =
      std::find_if(w.colonies.begin(), w.colonies.end(), [=](const Colony &c) {
        return c.id == id && c.civilization_id == civ;
      });
  return p == w.colonies.end() ? nullptr : &*p;
}
Colony *colony_for(ConstructionWorld w, int civ, int id) {
  auto p =
      std::find_if(w.colonies.begin(), w.colonies.end(), [=](const Colony &c) {
        return c.id == id && c.civilization_id == civ;
      });
  return p == w.colonies.end() ? nullptr : &*p;
}
CivilizationEconomy *economy_for(ConstructionWorld w, int civ) {
  auto p =
      std::find_if(w.economies.begin(), w.economies.end(),
                   [=](const auto &e) { return e.civilization_id == civ; });
  return p == w.economies.end() ? nullptr : &*p;
}
const PlanetaryBody *body_for(ConstructionReadView w, const Colony &c) {
  auto p = std::find_if(w.bodies.begin(), w.bodies.end(), [&](const auto &b) {
    return c.planetary_body_id && b.id == *c.planetary_body_id &&
           b.system_id == c.system_id;
  });
  return p == w.bodies.end() ? nullptr : &*p;
}
SovereignCurrencyDefinition currency_for(ConstructionReadView w,
                                         int civilization_id) {
  try {
    return sovereign_currency_for_civilization(w.civilizations,
                                               civilization_id);
  } catch (const std::out_of_range &error) {
    // The C# catalog reports a missing civilization as
    // InvalidOperationException.
    throw std::runtime_error(error.what());
  }
}

std::string money(ConstructionReadView w, int civ, double v) {
  return currency_for(w, civ).format(v);
}
double away2(double n) { return std::round(n * 100.0) / 100.0; }
std::string fixed_number(double value, int precision) {
  if (std::isnan(value))
    return "NaN";
  if (std::isinf(value))
    return value < 0.0 ? "-Infinity" : "Infinity";
  std::array<char, 512> text{};
  const auto [end, error] =
      std::to_chars(text.data(), text.data() + text.size(), value,
                    std::chars_format::fixed, precision);
  if (error != std::errc{})
    throw std::runtime_error(
        "Surface construction number is too large to format.");
  return {text.data(), end};
}
std::string n0(double value) {
  if (!std::isfinite(value))
    return fixed_number(value, 0);
  auto result = fixed_number(value, 0);
  const auto first_digit = !result.empty() && result.front() == '-'
                               ? std::size_t{1}
                               : std::size_t{0};
  for (auto position = result.size(); position > first_digit + 3; position -= 3)
    result.insert(position - 3, 1, ',');
  return result;
}
bool complete_project(ConstructionReadView w, int civ, std::string_view id) {
  auto s =
      std::find_if(w.construction.begin(), w.construction.end(),
                   [=](const auto &x) { return x.civilization_id == civ; });
  return s != w.construction.end() &&
         std::find(s->completed_project_ids.begin(),
                   s->completed_project_ids.end(),
                   id) != s->completed_project_ids.end();
}
} // namespace

double surface_construction_cost_multiplier(ConstructionReadView w,
                                            const Colony &c) {
  const auto *b = body_for(w, c);
  if (!b)
    return 1.;
  const auto &e = b->environment;
  double v = 1.;
  v += std::min(.35, std::abs(e.gravity_g - 1.) * .20);
  if (e.atmosphere == PlanetaryAtmosphereRegime::Vacuum)
    v += .20;
  if (e.pressure_kpa < 20 || e.pressure_kpa > 300)
    v += .15;
  if (e.temperature_kelvin < 240 || e.temperature_kelvin > 330)
    v += .15;
  if (e.radiation_hazard > .10)
    v += std::min(.15, (e.radiation_hazard - .10) * .30);
  return away2(std::clamp(v, 1., 2.));
}
double surface_authorization_cost(ConstructionReadView w, const Colony &c,
                                  const SurfaceBuildingDefinition &d) {
  return d.credit_cost * surface_construction_cost_multiplier(w, c);
}
double surface_upgrade_authorization_cost(ConstructionReadView w,
                                          const Colony &c,
                                          const SurfaceBuildingDefinition &d) {
  return d.upgrade_credit_cost * surface_construction_cost_multiplier(w, c);
}
std::optional<ConstructionCost> surface_hub_upgrade_cost(ConstructionReadView w,
                                                         const Colony &c) {
  if ((c.kind == SettlementKind::ResourceOutpost && c.surface_hub_level > 0) || c.surface_hub_level >= 3)
    return {};
  auto [cr, in] =
      c.surface_hub_level == 0 ? std::pair{25., 120.} :
      c.surface_hub_level == 1 ? std::pair{60., 250.} : std::pair{140., 600.};
  auto m = surface_construction_cost_multiplier(w, c);
  return ConstructionCost{away2(cr * m), std::ceil(in * m)};
}
std::optional<std::string>
surface_building_upgrade_lock_reason(ConstructionReadView w, int civ,
                                     const SurfaceBuildingDefinition &d) {
  if (d.upgrade_requirement_id &&
      !construction_has_capability(w, civ, *d.upgrade_requirement_id))
    return "Research " +
           d.upgrade_requirement_name.value_or(*d.upgrade_requirement_id) +
           " before authorizing this upgrade.";
  return {};
}
std::optional<std::string>
surface_hub_upgrade_lock_reason(ConstructionReadView w, int civ,
                                const Colony &c) {
  if (c.surface_hub_level == 1 &&
      !complete_project(w, civ, "industrial_automation"))
    return "Complete the Industrial Automation Program before expanding this "
           "command center.";
  if (c.surface_hub_level == 2 &&
      !construction_has_capability(w, civ, "orbital_industry"))
    return "Establish Orbital Manufacturing before expanding to a level-3 "
           "planetary hub.";
  if (c.surface_hub_level == 2) {
    if (auto b = body_for(w, c); b && b->radius_earth < .35)
      return b->name +
             " is too small for a 64-module regional hub. Keep this settlement "
             "at level 2 or expand through orbital infrastructure.";
  }
  return {};
}
bool surface_available_for_settlement(const Colony &c,
                                      const SurfaceBuildingDefinition &d) {
  return d.available_for_placement &&
         (c.kind != SettlementKind::ResourceOutpost || d.id != "trade_hub");
}
float surface_terrain_height(float x, float z) {
  const float d = std::sqrt(x * x + z * z),
              rise = std::clamp((d - 190.F) / 280.F, 0.F, 1.F);
  return rise * (18.F + 12.F * std::sin(x * .012F) * std::cos(z * .014F)) +
         1.4F * std::sin(x * .025F) * std::sin(z * .019F);
}
std::optional<std::string>
surface_placement_error(std::span<const SurfaceBuilding> bs,
                        std::string_view type, float x, float z, float rot) {
  const auto *d = find_surface_building(type);
  if (!d)
    return "Unknown building type.";
  if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(rot))
    return "Building position and rotation must be finite.";
  auto r = d->footprint_radius;
  if (std::abs(x) + r > surface_area_half_size ||
      std::abs(z) + r > surface_area_half_size)
    return "Choose a position inside the colony boundary.";
  if (x * x + z * z < std::pow(surface_hub_radius + r + 3.F, 2.F))
    return "Leave room around the colony hub.";
  float sx = std::abs(surface_terrain_height(x + r, z) -
                      surface_terrain_height(x - r, z)) /
             (2 * r),
        sz = std::abs(surface_terrain_height(x, z + r) -
                      surface_terrain_height(x, z - r)) /
             (2 * r);
  if (std::max(sx, sz) > .28F)
    return "This slope is too steep. Choose flatter ground.";
  for (auto &b : bs) {
    auto *o = find_surface_building(b.type_id);
    if (!o)
      return "The colony contains an unknown building type.";
    auto dx = b.x - x, dz = b.z - z;
    if (dx * dx + dz * dz < std::pow(r + o->footprint_radius + 3.F, 2.F))
      return "That position overlaps another building or construction site.";
  }
  if (bs.size() >= 64)
    return "This demo colony has reached its 64-building limit.";
  return {};
}
SurfaceBuildingPlacementAssessment assess_surface_building_placement(
    ConstructionReadView w, int civ, int id, std::string_view type, float x,
    float z, float rot) {
  SurfaceBuildingPlacementAssessment result;
  result.civilization_id = civ;
  result.colony_id = id;
  result.type_id = std::string(type);
  result.x = x;
  result.z = z;
  result.normalized_rotation_degrees = rot;
  const auto deny = [&](std::string message) {
    result.message = std::move(message);
    return result;
  };
  const auto *c = colony_for(w, civ, id);
  if (!c)
    return deny("You can build only in a colony you own.");
  const auto *b = body_for(w, *c);
  if (b && b->stellar_exposure && b->stellar_exposure->baked)
    return deny("Extreme stellar irradiation prohibits all surface construction.");
  if (!b || !b->environment.has_solid_surface)
    return deny("A surveyed colony on a solid planetary surface is required.");
  const auto economy = std::find_if(
      w.economies.begin(), w.economies.end(),
      [=](const auto &candidate) { return candidate.civilization_id == civ; });
  if (economy == w.economies.end())
    return deny("The colony has no construction economy.");
  const auto *d = find_surface_building(type);
  if (!d || !surface_available_for_settlement(*c, *d)) {
    if (c->kind == SettlementKind::ResourceOutpost && d &&
        d->id == "trade_hub")
      return deny("A sealed resource outpost cannot support a civilian trade "
                  "hub. Deliver extracted material by freighter.");
    return deny("That building type is available only as an upgrade.");
  }
  if (c->surface_hub_level <= 0)
    return deny("Complete the Command Center before constructing planetary buildings.");
  const auto capacity = surface_building_capacity(*c);
  if (c->surface_buildings.size() >= static_cast<std::size_t>(capacity))
    return deny("This settlement hub has reached its " +
                std::to_string(capacity) + "-module capacity.");
  if (auto error =
          surface_placement_error(c->surface_buildings, type, x, z, rot))
    return deny(*error);
  const auto currency = currency_for(w, civ);
  result.building_name = d->name;
  result.authorization_cost = surface_authorization_cost(w, *c, *d);
  result.industry_cost = d->industry_cost;
  result.formatted_authorization =
      currency.format(result.authorization_cost);
  if (economy->credits + .0001 < result.authorization_cost)
    return deny(result.formatted_authorization +
                " is required to authorize this " + d->name + " here.");
  int next = 1;
  if (!c->surface_buildings.empty()) {
    int maximum = c->surface_buildings.front().id;
    for (const auto &site : c->surface_buildings)
      maximum = std::max(maximum, site.id);
    if (maximum == std::numeric_limits<int>::max())
      return deny("No building identifier is available.");
    next = maximum + 1;
  }
  if (next <= 0)
    return deny("No building identifier is available.");
  result.accepted = true;
  result.prepared_building_id = next;
  result.normalized_rotation_degrees =
      std::fmod(std::fmod(rot, 360.F) + 360.F, 360.F);
  result.message = d->name + " placed and authorized for " +
                   result.formatted_authorization +
                   ". Construction uses available materials.";
  return result;
}
namespace {
bool same_placement_quote(const SurfaceBuildingPlacementAssessment &left,
                          const SurfaceBuildingPlacementAssessment &right) {
  return left.accepted == right.accepted &&
         left.civilization_id == right.civilization_id &&
         left.colony_id == right.colony_id && left.type_id == right.type_id &&
         left.building_name == right.building_name &&
         left.x == right.x && left.z == right.z &&
         left.normalized_rotation_degrees ==
             right.normalized_rotation_degrees &&
         left.prepared_building_id == right.prepared_building_id &&
         left.slot_index == right.slot_index &&
         left.authorization_cost == right.authorization_cost &&
         left.industry_cost == right.industry_cost &&
         left.formatted_authorization == right.formatted_authorization &&
         left.message == right.message;
}
ConstructionOrderResult apply_surface_building_placement(
    ConstructionWorld w, const SurfaceBuildingPlacementAssessment &prepared) {
  auto *c = colony_for(w, prepared.civilization_id, prepared.colony_id);
  auto *e = economy_for(w, prepared.civilization_id);
  if (!c || !e)
    throw std::logic_error("Prepared surface placement lost its owner.");
  const auto slots = planetary_building_slots(*c);
  e->credits -= prepared.authorization_cost;
  for (auto& building : c->surface_buildings) building.slot_index = slots.at(building.id);
  c->surface_buildings.push_back(
      {prepared.prepared_building_id, prepared.type_id, prepared.x, prepared.z,
       prepared.normalized_rotation_degrees});
  auto& placed = c->surface_buildings.back();
  if (prepared.slot_index) placed.slot_index = prepared.slot_index;
  else for(int slot=0;slot<surface_building_capacity(*c);++slot) {
    if(std::none_of(slots.begin(),slots.end(),[&](const auto& pair){return pair.second==slot;})){placed.slot_index=slot;break;}
  }
  return {true, prepared.message};
}
} // namespace
ConstructionOrderResult commit_surface_building_placement(
    ConstructionWorld w,
    const SurfaceBuildingPlacementAssessment &assessment) {
  const auto current = assessment.slot_index
      ? assess_planetary_building_slot(w.read(), assessment.civilization_id, assessment.colony_id, *assessment.slot_index, assessment.type_id)
      : assess_surface_building_placement(w.read(), assessment.civilization_id, assessment.colony_id, assessment.type_id, assessment.x, assessment.z, assessment.normalized_rotation_degrees);
  if (!current.accepted)
    return {false, current.message};
  if (!assessment.accepted || !same_placement_quote(assessment, current))
    return {false,
            "Surface placement terms changed; review the current quote."};
  return apply_surface_building_placement(w, current);
}
ConstructionOrderResult place_surface_building(ConstructionWorld w, int civ,
                                               int id, std::string_view type,
                                               float x, float z, float rot) {
  const auto assessment =
      assess_surface_building_placement(w.read(), civ, id, type, x, z, rot);
  if (!assessment.accepted)
    return {false, assessment.message};
  return apply_surface_building_placement(w, assessment);
}
namespace {
SurfaceBuildingRemovalAssessment prepare_surface_building_removal(
    ConstructionReadView w, int civ, int id, int building_id) {
  SurfaceBuildingRemovalAssessment result;
  result.civilization_id = civ;
  result.colony_id = id;
  result.building_id = building_id;
  const auto deny = [&](std::string message) {
    result.message = std::move(message);
    return result;
  };
  const auto *c = colony_for(w, civ, id);
  if (!c)
    return deny("You can remove buildings only from a colony you own.");
  const auto site = std::find_if(
      c->surface_buildings.begin(), c->surface_buildings.end(),
      [=](const auto &candidate) { return candidate.id == building_id; });
  if (site == c->surface_buildings.end())
    return deny("That surface building no longer exists.");
  const auto *d = find_surface_building(site->type_id);
  if (!d)
    return deny("That surface building has an unknown type and cannot be "
                "removed safely.");
  const auto economy = std::find_if(
      w.economies.begin(), w.economies.end(),
      [=](const auto &candidate) { return candidate.civilization_id == civ; });
  if (economy == w.economies.end())
    return deny("The colony has no construction economy.");
  result.type_id = site->type_id;
  result.building_name = d->name;
  result.cancellation = !site->is_complete;
  if (result.cancellation)
    result.refund = surface_authorization_cost(w, *c, *d) * .5;
  result.accepted = true;
  return result;
}
SurfaceBuildingRemovalAssessment finish_surface_building_removal_assessment(
    ConstructionReadView w, SurfaceBuildingRemovalAssessment result) {
  if (!result.accepted)
    return result;
  if (result.cancellation) {
    result.formatted_refund =
        money(w, result.civilization_id, result.refund);
    result.message = result.building_name + " construction cancelled. " +
                     result.formatted_refund +
                     " was recovered; spent industry was not recoverable.";
  } else {
    result.message = result.building_name +
                     " demolished. Its power use and production have stopped.";
  }
  return result;
}
bool same_removal_quote(const SurfaceBuildingRemovalAssessment &left,
                        const SurfaceBuildingRemovalAssessment &right) {
  return left.accepted == right.accepted &&
         left.civilization_id == right.civilization_id &&
         left.colony_id == right.colony_id &&
         left.building_id == right.building_id &&
         left.type_id == right.type_id &&
         left.building_name == right.building_name &&
         left.cancellation == right.cancellation &&
         left.refund == right.refund &&
         left.formatted_refund == right.formatted_refund &&
         left.message == right.message;
}
ConstructionOrderResult apply_surface_building_removal(
    ConstructionWorld w, const SurfaceBuildingRemovalAssessment &prepared) {
  auto *c = colony_for(w, prepared.civilization_id, prepared.colony_id);
  auto *e = economy_for(w, prepared.civilization_id);
  if (!c || !e)
    throw std::logic_error("Prepared surface removal lost its owner.");
  const auto site = std::find_if(
      c->surface_buildings.begin(), c->surface_buildings.end(),
      [&](const auto &candidate) { return candidate.id == prepared.building_id; });
  if (site == c->surface_buildings.end())
    throw std::logic_error("Prepared surface removal lost its building.");
  const auto slots=planetary_building_slots(*c);
  for(auto& building:c->surface_buildings)building.slot_index=slots.at(building.id);
  c->surface_buildings.erase(site);
  if (prepared.cancellation) {
    e->credits += prepared.refund;
    const auto formatted =
        money(w.read(), prepared.civilization_id, prepared.refund);
    return {true, prepared.building_name + " construction cancelled. " +
                      formatted +
                      " was recovered; spent industry was not recoverable."};
  }
  return {true, prepared.building_name +
                    " demolished. Its power use and production have stopped."};
}
} // namespace
SurfaceBuildingRemovalAssessment assess_surface_building_removal(
    ConstructionReadView w, int civ, int id, int building_id) {
  return finish_surface_building_removal_assessment(
      w, prepare_surface_building_removal(w, civ, id, building_id));
}
ConstructionOrderResult commit_surface_building_removal(
    ConstructionWorld w, const SurfaceBuildingRemovalAssessment &assessment) {
  const auto current = assess_surface_building_removal(
      w.read(), assessment.civilization_id, assessment.colony_id,
      assessment.building_id);
  if (!current.accepted)
    return {false, current.message};
  if (!assessment.accepted || !same_removal_quote(assessment, current))
    return {false, "Surface removal terms changed; review the current quote."};
  return apply_surface_building_removal(w, current);
}
ConstructionOrderResult remove_surface_building(ConstructionWorld w, int civ,
                                                int id, int building_id) {
  const auto prepared =
      prepare_surface_building_removal(w.read(), civ, id, building_id);
  if (!prepared.accepted)
    return {false, prepared.message};
  return apply_surface_building_removal(w, prepared);
}
ConstructionOrderResult upgrade_surface_building(ConstructionWorld w, int civ,
                                                 int id, int bid) {
  auto *c = colony_for(w, civ, id);
  if (!c)
    return {false, "You can upgrade buildings only in a colony you own."};
  if(const auto* body=body_for(w.read(),*c);body&&body->stellar_exposure&&body->stellar_exposure->baked)
    return {false,"Extreme stellar irradiation prevents surface operations."};
  auto p =
      std::find_if(c->surface_buildings.begin(), c->surface_buildings.end(),
                   [=](auto &b) { return b.id == bid; });
  if (p == c->surface_buildings.end())
    return {false, "That surface building no longer exists."};
  if (!p->is_complete)
    return {false, "Complete construction before upgrading this building."};
  if (p->pending_upgrade_type_id)
    return {false, "This building is already being upgraded."};
  auto *cur = find_surface_building(p->type_id);
  auto *up = cur && cur->upgrade_type_id
                 ? find_surface_building(*cur->upgrade_type_id)
                 : nullptr;
  if (!cur || !up)
    return {false, "This building has no further upgrade available."};
  if (auto l = surface_building_upgrade_lock_reason(w.read(), civ, *cur))
    return {false, *l};
  auto *e = economy_for(w, civ);
  if (!e)
    return {false, "The colony has no construction economy."};
  auto cr = surface_upgrade_authorization_cost(w.read(), *c, *cur);
  if (e->credits + .0001 < cr ||
      e->industry + .0001 < cur->upgrade_industry_cost)
    return {false, "Upgrading to " + up->name + " requires " +
                       money(w.read(), civ, cr) + " and " +
                       n0(cur->upgrade_industry_cost) +
                       " available materials."};
  e->credits -= cr;
  e->industry -= cur->upgrade_industry_cost;
  p->pending_upgrade_type_id = up->id;
  p->upgrade_days_remaining =
      cur->upgrade_industry_cost / surface_industry_per_site_per_day;
  return {true,
          up->name + " upgrade started: " +
              detail::legacy_custom_fixed(p->upgrade_days_remaining, 1, 1) +
              " game days. Existing facilities remain operational until "
              "completion."};
}
ConstructionOrderResult upgrade_surface_hub(ConstructionWorld w, int civ,
                                            int id) {
  auto *c = colony_for(w, civ, id);
  if (!c)
    return {false, "You can upgrade only a colony you own."};
  if(const auto* body=body_for(w.read(),*c);body&&body->stellar_exposure&&body->stellar_exposure->baked)
    return {false,"Extreme stellar irradiation prevents surface operations."};
  if (const auto* body=body_for(w.read(),*c); !body || !body->environment.has_solid_surface)
    return {false,"A Command Center requires an owned settlement on a solid surface."};
  if (c->surface_hub_upgrade_days_remaining > 0)
    return {false, "The hub expansion is already under construction."};
  auto cost = surface_hub_upgrade_cost(w.read(), *c);
  if (!cost)
    return {false, c->kind == SettlementKind::ResourceOutpost
                       ? "A sealed resource outpost must be terraformed before "
                         "it can become a full colony command center."
                       : "This planetary hub is already at maximum capacity."};
  if (auto l = surface_hub_upgrade_lock_reason(w.read(), civ, *c))
    return {false, *l};
  auto *e = economy_for(w, civ);
  if (!e)
    return {false, "The colony has no construction economy."};
  auto currency = currency_for(w.read(), civ);
  if (e->credits + .0001 < cost->credit_cost ||
      e->industry + .0001 < cost->industry_cost)
    return {false, "Hub expansion requires " +
                       currency.format(cost->credit_cost) + " and " +
                       n0(cost->industry_cost) + " materials."};
  e->credits -= cost->credit_cost;
  e->industry -= cost->industry_cost;
  c->surface_hub_upgrade_days_remaining =
      cost->industry_cost / surface_industry_per_site_per_day;
  return {true,
          "Hub expansion authorized: " +
              detail::legacy_custom_fixed(c->surface_hub_upgrade_days_remaining,
                                          1, 1) +
              " game days. Capacity increases when construction completes."};
}
double surface_repair_industry_cost(const SurfaceBuilding &b) {
  auto *d = find_surface_building(b.type_id);
  if (!d || !b.is_complete || b.condition >= 1. - .0000001)
    return 0;
  return std::max(1., std::ceil(d->industry_cost * .25 * (1 - b.condition)));
}
ConstructionOrderResult repair_surface_building(ConstructionWorld w, int civ,
                                                int id, int bid) {
  auto *c = colony_for(w, civ, id);
  if (!c)
    return {false, "You can repair buildings only in a colony you own."};
  if(const auto* body=body_for(w.read(),*c);body&&body->stellar_exposure&&body->stellar_exposure->baked)
    return {false,"Extreme stellar irradiation prevents surface operations."};
  auto p =
      std::find_if(c->surface_buildings.begin(), c->surface_buildings.end(),
                   [=](auto &b) { return b.id == bid; });
  if (p == c->surface_buildings.end())
    return {false, "That surface building no longer exists."};
  if (!p->is_complete)
    return {false, "Complete construction before repairing this building."};
  auto *d = find_surface_building(p->type_id);
  if (!d)
    return {false, "That surface building has an unknown type and cannot be "
                   "repaired safely."};
  auto cost = surface_repair_industry_cost(*p);
  if (cost <= 0)
    return {false, d->name + " is already at full condition."};
  auto *e = economy_for(w, civ);
  if (!e)
    return {false, "The colony has no maintenance economy."};
  if (e->industry + .0001 < cost)
    return {false, "Repairing " + d->name + " requires " + n0(cost) +
                       " stored materials; " + n0(e->industry) +
                       " are available."};
  e->industry -= cost;
  p->condition = 1;
  return {true, d->name + " restored to full condition using " + n0(cost) +
                    " materials."};
}
ConstructionOrderResult set_surface_building_enabled(ConstructionWorld w,
                                                     int civ, int id, int bid,
                                                     bool on) {
  auto *c = colony_for(w, civ, id);
  if (!c)
    return {false, "You can manage buildings only in a colony you own."};
  auto p =
      std::find_if(c->surface_buildings.begin(), c->surface_buildings.end(),
                   [=](auto &b) { return b.id == bid; });
  if (p == c->surface_buildings.end())
    return {false, "That surface building no longer exists."};
  if (!p->is_complete)
    return {false, "Complete construction before changing operating status."};
  auto *d = find_surface_building(p->type_id);
  if (!d)
    return {false, "That surface building has an unknown type and cannot be "
                   "managed safely."};
  if (p->is_enabled == on)
    return {false,
            d->name + " is already " + (on ? "operating." : "shut down.")};
  p->is_enabled = on;
  return {true, on ? d->name + " restarted. Staffing, power demand, output and "
                               "upkeep resume when capacity is available."
                   : d->name + " shut down. Its staffing, power demand, output "
                               "and upkeep are suspended."};
}
ConstructionOrderResult set_surface_building_priority(ConstructionWorld w,
                                                      int civ, int id, int bid,
                                                      bool pr) {
  auto *c = colony_for(w, civ, id);
  if (!c)
    return {false, "You can prioritize buildings only in a colony you own."};
  auto p =
      std::find_if(c->surface_buildings.begin(), c->surface_buildings.end(),
                   [=](auto &b) { return b.id == bid; });
  if (p == c->surface_buildings.end())
    return {false, "That surface building no longer exists."};
  if (!p->is_complete)
    return {false,
            "Complete construction before assigning operating priority."};
  int want = pr ? 1 : 0;
  if (p->operating_priority == want)
    return {false, "This building already has " +
                       std::string(pr ? "priority." : "normal priority.")};
  p->operating_priority = want;
  auto *d = find_surface_building(p->type_id);
  auto n = d ? d->name : "Surface building";
  return {true, pr ? n + " prioritized. It receives available workers and "
                         "power before normal buildings."
                   : n + " returned to normal operating priority."};
}
double surface_construction_industry_demand(ConstructionReadView w, int civ,
                                            double days) {
  double sum = 0;
  for (auto &c : w.colonies)
    if (c.civilization_id == civ)
      for (auto &b : c.surface_buildings)
        if (!b.is_complete) {
          auto *d = find_surface_building(b.type_id);
          if (!d)
            throw std::invalid_argument(
                "The surface building has an unknown type.");
          sum +=
              std::min(surface_industry_per_site_per_day * std::max(0., days),
                       std::max(0., d->industry_cost - b.industry_progress));
        }
  return sum;
}
void advance_surface_construction(ConstructionWorld w, int civ, double budget,
                                  double days) {
  if (!std::isfinite(budget) || budget < 0 || !std::isfinite(days) || days < 0)
    throw std::out_of_range("Surface construction requires finite nonnegative "
                            "industry and elapsed days.");
  auto work = days * civilization_operating_funding(w.economies, civ);
  if (work > 0)
    for (auto &c : w.colonies)
      if (c.civilization_id == civ) {
        if (c.surface_hub_upgrade_days_remaining > 0) {
          c.surface_hub_upgrade_days_remaining =
              std::max(0., c.surface_hub_upgrade_days_remaining - work);
          if (c.surface_hub_upgrade_days_remaining <= 1e-9) {
            c.surface_hub_upgrade_days_remaining = 0;
            c.surface_hub_level++;
          }
        }
        for (auto &b : c.surface_buildings)
          if (b.pending_upgrade_type_id) {
            b.upgrade_days_remaining =
                std::max(0., b.upgrade_days_remaining - work);
            if (b.upgrade_days_remaining <= 1e-9) {
              auto *d = find_surface_building(*b.pending_upgrade_type_id);
              if (!d)
                throw std::invalid_argument(
                    "The surface building has an unknown type.");
              b.type_id = d->id;
              b.industry_progress = d->industry_cost;
              b.condition = 1;
              b.pending_upgrade_type_id.reset();
              b.upgrade_days_remaining = 0;
            }
          }
      }
  if (budget <= 0)
    return;
  auto *e = economy_for(w, civ);
  if (!e)
    throw std::runtime_error("Sequence contains no matching element");
  auto demand = surface_construction_industry_demand(w.read(), civ, days);
  if (demand <= 0)
    return;
  auto avail = std::min({demand, e->industry, budget});
  double spent = 0;
  std::vector<std::pair<int, SurfaceBuilding *>> sites;
  for (auto &c : w.colonies)
    if (c.civilization_id == civ)
      for (auto &b : c.surface_buildings)
        sites.emplace_back(c.id, &b);
  std::stable_sort(sites.begin(), sites.end(),
                   [](const auto &a, const auto &b) {
                     return a.first != b.first ? a.first < b.first
                                               : a.second->id < b.second->id;
                   });
  for (auto &[_, site] : sites) {
    auto &b = *site;
    if (b.is_complete)
      continue;
    auto *d = find_surface_building(b.type_id);
    if (!d)
      throw std::invalid_argument("The surface building has an unknown type.");
    auto remaining = std::max(0., d->industry_cost - b.industry_progress),
         sd = std::min(remaining, surface_industry_per_site_per_day * days),
         sp = std::min(sd, avail * (sd / demand));
    b.industry_progress += sp;
    spent += sp;
    if (b.industry_progress + .0000001 >= d->industry_cost) {
      b.industry_progress = d->industry_cost;
      b.is_complete = true;
    }
  }
  e->industry = std::max(0., e->industry - spent);
}
void validate_surface_construction(const Colony &c) {
  if(c.surface_hub_level<0 || c.surface_hub_level>3)
    throw std::runtime_error("Invalid planetary Command Center capacity.");
  if(c.surface_buildings.size()>static_cast<std::size_t>(surface_building_capacity(c)))
    throw std::runtime_error("Settlement " + std::to_string(c.id) + " exceeds its represented hub module capacity.");
  (void)planetary_building_slots(c);
  if (!std::isfinite(c.surface_hub_upgrade_days_remaining) ||
      c.surface_hub_upgrade_days_remaining < 0 ||
      (c.surface_hub_upgrade_days_remaining > 0 &&
       (c.surface_hub_level >= 3 || (c.kind == SettlementKind::ResourceOutpost && c.surface_hub_level > 0))))
    throw std::runtime_error("Colony " + std::to_string(c.id) +
                             " has invalid hub expansion progress.");
  std::vector<SurfaceBuilding> ok;
  std::unordered_set<int> ids;
  for (auto &b : c.surface_buildings) {
    if (b.id <= 0 || !ids.insert(b.id).second)
      throw std::runtime_error(
          "Colony " + std::to_string(c.id) +
          " has a missing or duplicate surface building identifier.");
    if (auto x = surface_placement_error(ok, b.type_id, b.x, b.z,
                                         b.rotation_degrees))
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", surface building " + std::to_string(b.id) +
                               ": " + *x);
    auto *d = find_surface_building(b.type_id);
    if (!std::isfinite(b.industry_progress) || b.industry_progress < 0 ||
        b.industry_progress > d->industry_cost ||
        b.is_complete != (b.industry_progress >= d->industry_cost))
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", surface building " + std::to_string(b.id) +
                               " has inconsistent construction progress.");
    if (b.operating_priority < 0 || b.operating_priority > 1)
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", surface building " + std::to_string(b.id) +
                               " has an invalid operating priority.");
    if (!std::isfinite(b.condition) || b.condition < 0 || b.condition > 1)
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", surface building " + std::to_string(b.id) +
                               " has invalid physical condition.");
    if (!std::isfinite(b.upgrade_days_remaining) ||
        b.upgrade_days_remaining < 0 ||
        (bool(b.pending_upgrade_type_id) != (b.upgrade_days_remaining > 0)) ||
        (b.pending_upgrade_type_id &&
         (!b.is_complete || d->upgrade_type_id != b.pending_upgrade_type_id)))
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", building " + std::to_string(b.id) +
                               " has invalid upgrade progress.");
    if (!std::isfinite(b.stored_power_days) || b.stored_power_days < 0 ||
        b.stored_power_days > d->power_storage_days + .0000001)
      throw std::runtime_error("Colony " + std::to_string(c.id) +
                               ", surface building " + std::to_string(b.id) +
                               " has invalid stored grid energy.");
    ok.push_back(b);
  }
}

std::map<int,int> planetary_building_slots(const Colony& colony) {
  std::map<int,int> result;
  std::unordered_set<int> occupied;
  const int capacity=surface_building_capacity(colony);
  std::vector<int> legacy;
  for(const auto& building:colony.surface_buildings){
    if(building.slot_index){
      const int slot=*building.slot_index;
      if(slot<0||slot>=capacity||!occupied.insert(slot).second)
        throw std::runtime_error("Invalid or duplicate planetary building slot.");
      result.emplace(building.id,slot);
    }else legacy.push_back(building.id);
  }
  std::sort(legacy.begin(),legacy.end());
  for(const int id:legacy){
    int slot=0;while(occupied.contains(slot))++slot;
    if(slot>=capacity)throw std::runtime_error("Planetary buildings exceed Command Center capacity.");
    occupied.insert(slot);result.emplace(id,slot);
  }
  return result;
}
SurfaceBuildingPlacementAssessment assess_planetary_building_slot(
    ConstructionReadView world,int civ,int id,int slot,std::string_view type){
  SurfaceBuildingPlacementAssessment denied;
  denied.civilization_id=civ;denied.colony_id=id;denied.type_id=std::string(type);denied.slot_index=slot;
  const auto* colony=colony_for(world,civ,id);
  if(!colony){denied.message="You can build only on a planet you own.";return denied;}
  if(colony->surface_hub_level<=0){denied.message="Complete the Command Center to unlock building slots.";return denied;}
  if(slot<0||slot>=surface_building_capacity(*colony)){denied.message="Upgrade the Command Center to unlock this slot.";return denied;}
  const auto slots=planetary_building_slots(*colony);
  if(std::any_of(slots.begin(),slots.end(),[&](const auto& pair){return pair.second==slot;})){denied.message="This slot is occupied or reserved by construction.";return denied;}
  for(int ring=1;ring<=7;++ring)for(int n=0;n<ring*12;++n){
    const double angle=n*6.283185307179586/(ring*12);
    const float x=static_cast<float>(std::cos(angle)*ring*60),z=static_cast<float>(std::sin(angle)*ring*60);
    if(surface_placement_error(colony->surface_buildings,type,x,z,0))continue;
    auto result=assess_surface_building_placement(world,civ,id,type,x,z,0);
    result.slot_index=slot;return result;
  }
  denied.message="No compatible site is available for this building.";return denied;
}

} // namespace stellar::core
