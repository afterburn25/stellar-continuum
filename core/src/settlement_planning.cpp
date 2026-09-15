#include <stellar/core/settlement_planning.hpp>

#include <stellar/core/fleet_transit.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace stellar::core {
namespace {
constexpr std::string_view outpost_design = "resource_outpost_ship";
constexpr double colony_cost = 120, outpost_cost = 90;
std::string duplicate(int id) {
  return "An item with the same key has already been added. Key: " +
         std::to_string(id);
}
template <class T>
std::unordered_map<int, const T *> unique(std::span<const T> values) {
  std::unordered_map<int, const T *> r;
  for (const auto &v : values)
    if (!r.emplace(v.id, &v).second)
      throw std::invalid_argument(duplicate(v.id));
  return r;
}
const FleetState *colony_fleet(SettlementPlanningWorldView w, int id) {
  for (const auto &f : w.fleets)
    if (f.id == id && f.is_active && f.role == FleetRole::Colony &&
        f.design_id != outpost_design && f.embarked_population_millions > 0)
      return &f;
  return nullptr;
}
const FleetState *outpost_fleet(SettlementPlanningWorldView w, int id) {
  for (const auto &f : w.fleets)
    if (f.id == id && ResourceOutpostOpportunityPlanner::is_outpost_fleet(f))
      return &f;
  return nullptr;
}
const SpeciesEnvironmentProfile *species(std::optional<std::string> const &id) {
  if (!id || id->empty())
    return nullptr;
  for (const auto &s : species_environment_profiles())
    if (s.id == *id)
      return &s;
  return nullptr;
}
const CivilizationEconomy &economy(SettlementPlanningWorldView w, int id) {
  for (const auto &e : w.economies)
    if (e.civilization_id == id)
      return e;
  throw std::runtime_error("Sequence contains no matching element");
}
MissionReachAssessment reach(const SettlementReachAssessment &cb,
                             SettlementPlanningWorldView w, const FleetState &f,
                             int target) {
  return cb ? cb({w.systems, w.colonies, w.lanes}, f.civilization_id, f, target,
                 InterstellarMissionKind::Colony)
            : assess_operational_reach({w.systems, w.colonies, w.lanes},
                                       f.civilization_id, f, target,
                                       InterstellarMissionKind::Colony);
}
std::string percent(double v) {
  if (!std::isfinite(v))
    return std::isnan(v) ? "NaN %" : v > 0 ? "Infinity %" : "-Infinity %";
  return std::to_string(static_cast<long long>(std::nearbyint(v * 100))) + "%";
}
int dcmp(double a, double b, bool desc = false) {
  const bool an = std::isnan(a), bn = std::isnan(b);
  if (an != bn)
    return desc ? (an ? 1 : -1) : (an ? -1 : 1);
  if (an || a == b)
    return 0;
  if (desc)
    return a > b ? -1 : 1;
  return a < b ? -1 : 1;
}
bool occupied(SettlementPlanningWorldView w, int id) {
  return std::any_of(w.colonies.begin(), w.colonies.end(),
                     [&](auto &c) { return c.system_id == id; });
}
std::string money(SettlementPlanningWorldView w, int civ, double amount) {
  return sovereign_currency_for_civilization(w.civilizations, civ)
      .format(amount);
}

ColonizationOpportunityCandidate colony_candidate(
    SettlementPlanningWorldView w, const FleetState &f, const StellarSystem &s,
    const PlanetaryBody &b, const KnownSpeciesPlanetarySuitability &v,
    const MissionReachAssessment &r, std::string_view species_name,
    const std::vector<FriendlyColonyMissionReservation> &reservations) {
  const bool occ = occupied(w, s.id);
  const auto ri = std::find_if(reservations.begin(), reservations.end(),
                               [&](auto &x) { return x.system_id == s.id; });
  const bool reserved = ri != reservations.end(),
             surface = b.environment.has_solid_surface,
             native = b.has_pre_warp_civilization,
             bio = surface && !native &&
                   v.colonization_viability !=
                       SpeciesColonizationViability::Unsuitable;
  const bool affordable =
      f.destination_system_id ||
      economy(w, f.civilization_id).credits + .0001 >= colony_cost;
  const bool can = bio && !occ && !reserved && r.is_supported && affordable;
  std::string reason;
  if (!surface)
    reason = b.name +
             " has no solid settlement surface in the current colony model.";
  else if (native)
    reason = "A native pre-warp civilization already inhabits " + b.name + ".";
  else if (v.colonization_viability == SpeciesColonizationViability::Unsuitable)
    reason = b.name + " is not currently viable for " +
             std::string(species_name) + ": natural habitability " +
             percent(v.natural_habitability) +
             ", unprotected operational capacity " +
             percent(v.unprotected_operational_capacity) +
             ". Required habitat-support capabilities are not yet connected to "
             "colony construction/logistics.";
  else if (occ)
    reason = "That system already contains a founded colony in the current "
             "single-colony early-release model.";
  else if (reserved)
    reason = "Another friendly colony ship (fleet " +
             std::to_string(ri->fleet_id) +
             ") is already committed to that system.";
  else if (!r.is_supported)
    reason = r.reason;
  else if (!affordable)
    reason = money(w, f.civilization_id, colony_cost) +
             " is required to fund the colony expedition.";
  else
    reason = b.name + " is " +
             (v.colonization_viability ==
                      SpeciesColonizationViability::NaturallyViable
                  ? "naturally viable"
                  : "currently available through the prototype "
                    "habitat-supported fallback") +
             " for " + std::string(species_name) + ". " + r.reason;
  return {s.id,
          s.name,
          b.id,
          b.name,
          b.kind,
          f.id,
          v.species_id,
          v.colonization_viability,
          v.natural_habitability,
          v.unprotected_operational_capacity,
          v.limiting_factor,
          v.requires_gravity_mitigation,
          v.requires_thermal_control,
          v.requires_pressure_control,
          v.requires_sealed_habitat,
          v.requires_artificial_biosphere,
          v.requires_radiation_shielding,
          surface,
          native,
          b.has_rare_resource,
          occ,
          interstellar_distance_from_fleet(w.systems, f, s),
          r,
          can,
          std::move(reason),
          reserved,
          reserved ? std::optional<int>(ri->fleet_id) : std::nullopt};
}
} // namespace

ColonizationOpportunityPlanner::ColonizationOpportunityPlanner(
    SettlementReachAssessment r)
    : reach_(std::move(r)) {}
ColonizationOpportunityPlan
ColonizationOpportunityPlanner::build_plan(SettlementPlanningWorldView w,
                                           int id, int maximum) const {
  maximum = std::clamp(maximum, 1, 64);
  const auto *f = colony_fleet(w, id);
  if (!f)
    return {id,
            "",
            -1,
            "",
            "",
            0,
            false,
            "No active colony ship carrying reserved colonists with that fleet "
            "ID is available.",
            {}};
  const auto *sp = species(f->embarked_population_species_id);
  if (!sp)
    return {f->id, "",
            -1,    "",
            "",    0,
            false, "The colony ship's passenger species identity is invalid.",
            {}};
  const auto systems = unique(w.systems);
  const auto bodies = unique(w.bodies);
  (void)bodies;
  auto views = build_known_suitability_for_species(w.knowledge_view(),
                                                   f->civilization_id, sp->id);
  auto reservations =
      build_friendly_colony_mission_reservations(w.knowledge_view(), *f);
  std::unordered_map<int, MissionReachAssessment> reaches;
  std::vector<ColonizationOpportunityCandidate> c;
  for (const auto &v : views) {
    if (!systems.contains(v.system_id) || !bodies.contains(v.planetary_body_id))
      continue;
    auto [it, added] = reaches.try_emplace(v.system_id);
    if (added)
      it->second = reach(reach_, w, *f, v.system_id);
    c.push_back(colony_candidate(w, *f, *systems.at(v.system_id),
                                 *bodies.at(v.planetary_body_id), v, it->second,
                                 sp->display_name, reservations));
  }
  std::stable_sort(c.begin(), c.end(), [](auto &a, auto &b) {
    if (a.can_order != b.can_order)
      return a.can_order;
    if (a.colonization_viability != b.colonization_viability)
      return a.colonization_viability > b.colonization_viability;
    int x = dcmp(a.natural_habitability, b.natural_habitability, true);
    if (x)
      return x < 0;
    x = dcmp(a.unprotected_operational_capacity,
             b.unprotected_operational_capacity, true);
    if (x)
      return x < 0;
    x = dcmp(a.distance_from_fleet, b.distance_from_fleet);
    if (x)
      return x < 0;
    if (a.system_id != b.system_id)
      return a.system_id < b.system_id;
    return a.planetary_body_id < b.planetary_body_id;
  });
  if (c.size() > static_cast<size_t>(maximum))
    c.resize(maximum);
  const auto n =
      std::count_if(c.begin(), c.end(), [](auto &x) { return x.can_order; });
  std::string status =
      c.empty() ? "No fully surveyed planetary bodies are currently available "
                  "for settlement evaluation."
      : n == 0
          ? std::to_string(c.size()) + " fully surveyed settlement candidate" +
                (c.size() == 1 ? "" : "s") +
                " evaluated; none are currently orderable."
          : std::to_string(n) + " currently orderable settlement opportunit" +
                (n == 1 ? "y" : "ies") + " in a " + std::to_string(c.size()) +
                "-body planning window.";
  return {f->id,  f->name,           f->civilization_id,
          sp->id, sp->display_name,  f->embarked_population_millions,
          true,   std::move(status), std::move(c)};
}
ColonizationOrderAssessment
ColonizationOpportunityPlanner::assess_order(SettlementPlanningWorldView w,
                                             int id, int sid, int bid) const {
  const auto *f = colony_fleet(w, id);
  if (!f)
    return {false,
            "No active colony ship carrying reserved colonists with that fleet "
            "ID is available.",
            {}};
  const auto *sp = species(f->embarked_population_species_id);
  if (!sp)
    return {
        false, "The colony ship's passenger species identity is invalid.", {}};
  const StellarSystem *s = nullptr;
  for (const auto &x : w.systems)
    if (x.id == sid) {
      s = &x;
      break;
    }
  if (!s)
    return {false, "Unknown destination.", {}};
  if (!w.knowledge.is_system_known(f->civilization_id, sid))
    return {false, "That astronomical target has not been detected yet.", {}};
  if (w.knowledge.system_survey_level(f->civilization_id, sid) !=
      SystemSurveyLevel::fully_surveyed)
    return {false,
            "A completed science survey is required before a colony mission "
            "can be prepared.",
            {}};
  const PlanetaryBody *b = nullptr;
  for (const auto &x : w.bodies)
    if (x.id == bid && x.system_id == sid) {
      b = &x;
      break;
    }
  if (!b)
    return {
        false,
        "That planetary body is not part of the surveyed destination system.",
        {}};
  auto views = build_known_suitability_for_species(w.knowledge_view(),
                                                   f->civilization_id, sp->id);
  const auto vi = std::find_if(views.begin(), views.end(), [&](auto &x) {
    return x.planetary_body_id == b->id;
  });
  if (vi == views.end())
    return {false,
            "Detailed planetary suitability is not legitimately known for that "
            "body.",
            {}};
  auto r = reach(reach_, w, *f, sid);
  auto rs = build_friendly_colony_mission_reservations(w.knowledge_view(), *f);
  auto c = colony_candidate(w, *f, *s, *b, *vi, r, sp->display_name, rs);
  if (!c.can_order)
    return {false, c.reason, c};
  return {true,
          f->name + ": colony mission approved for " + b->name + " in " +
              s->name + " with " +
              detail::legacy_custom_fixed(f->embarked_population_millions, 1,
                                          1) +
              " million " +
              sp->display_name + " colonists aboard. " + c.reason,
          c};
}
MissionReachAssessment ColonizationOpportunityPlanner::assess_operational_reach(
    SettlementPlanningWorldView w, int id, int sid) const {
  const auto *f = colony_fleet(w, id);
  return f ? reach(reach_, w, *f, sid)
           : unsupported_mission_reach(
                 "No populated colony ship is available.");
}

ResourceOutpostOpportunityPlanner::ResourceOutpostOpportunityPlanner(
    SettlementReachAssessment r)
    : reach_(std::move(r)) {}
bool ResourceOutpostOpportunityPlanner::is_outpost_fleet(const FleetState &f) {
  return f.is_active && f.role == FleetRole::Colony &&
         f.design_id == outpost_design && f.embarked_population_millions > 0;
}
ResourceOutpostOpportunityPlan
ResourceOutpostOpportunityPlanner::build_plan(SettlementPlanningWorldView w,
                                              int id, int maximum) const {
  maximum = std::clamp(maximum, 1, 64);
  const auto *f = outpost_fleet(w, id);
  if (!f)
    return {id,
            "",
            -1,
            "",
            "",
            0,
            false,
            "No active staffed resource-outpost vessel with that fleet ID is "
            "available.",
            {}};
  const auto *sp = species(f->embarked_population_species_id);
  if (!sp)
    return {id,
            "",
            -1,
            "",
            "",
            0,
            false,
            "The outpost vessel's specialist personnel species identity is "
            "invalid.",
            {}};
  auto systems = unique(w.systems);
  auto bodies = unique(w.bodies);
  auto views = build_known_suitability_for_species(w.knowledge_view(),
                                                   f->civilization_id, sp->id);
  std::unordered_map<int, KnownSpeciesPlanetarySuitability> suit;
  for (auto &v : views)
    if (!suit.emplace(v.planetary_body_id, v).second)
      throw std::invalid_argument(duplicate(v.planetary_body_id));
  std::unordered_map<int, MissionReachAssessment> reaches;
  std::vector<ResourceOutpostOpportunityCandidate> c;
  for (const auto &body : w.bodies) {
    if (w.knowledge.system_survey_level(f->civilization_id, body.system_id) !=
            SystemSurveyLevel::fully_surveyed ||
        !systems.contains(body.system_id) || !suit.contains(body.id))
      continue;
    auto [it, a] = reaches.try_emplace(body.system_id);
    if (a)
      it->second = reach(reach_, w, *f, body.system_id);
    const auto &s = *systems.at(body.system_id);
    const auto &v = suit.at(body.id);
    auto dep = resource_deposit_profile(body);
    const bool occ = occupied(w, s.id),
               reserved = std::any_of(w.fleets.begin(), w.fleets.end(),
                                      [&](auto &o) {
                                        return o.id != f->id && o.is_active &&
                                               o.civilization_id ==
                                                   f->civilization_id &&
                                               o.role == FleetRole::Colony &&
                                               o.destination_system_id == s.id;
                                      }),
               harsh = v.colonization_viability ==
                       SpeciesColonizationViability::Unsuitable,
               aff = f->destination_system_id ||
                     economy(w, f->civilization_id).credits + .0001 >=
                         outpost_cost,
               can = body.environment.has_solid_surface &&
                     body.has_rare_resource &&
                     !body.has_pre_warp_civilization && harsh && !occ &&
                     !reserved && it->second.is_supported && aff;
    std::string reason;
    if (!body.environment.has_solid_surface)
      reason =
          body.name + " has no solid surface for the current outpost model.";
    else if (!body.has_rare_resource)
      reason = body.name + " has no confirmed rare-resource deposit.";
    else if (body.has_pre_warp_civilization)
      reason =
          "A native pre-warp civilization already inhabits " + body.name + ".";
    else if (!harsh)
      reason =
          body.name + " can support a colony for " + sp->display_name +
          "; use a colony ship instead of consuming a sealed outpost vessel.";
    else if (occ)
      reason = "That system already contains a settlement in the current "
               "single-settlement model.";
    else if (reserved)
      reason = "Another friendly settlement vessel is already committed to "
               "that system.";
    else if (!it->second.is_supported)
      reason = it->second.reason;
    else if (!aff)
      reason = money(w, f->civilization_id, outpost_cost) +
               " is required to fund the resource-outpost expedition.";
    else
      reason = body.name +
               " is too harsh for colonization but its confirmed deposit can "
               "support a sealed staffed outpost. " +
               it->second.reason;
    if (body.has_rare_resource)
      c.push_back({s.id,
                   s.name,
                   body.id,
                   body.name,
                   f->id,
                   v.species_id,
                   v.natural_habitability,
                   v.unprotected_operational_capacity,
                   v.limiting_factor,
                   true,
                   dep.material_name,
                   dep.grade,
                   dep.accessibility,
                   dep.extraction_yield_multiplier,
                   initial_deposit_reserve(body),
                   occ,
                   harsh,
                   interstellar_distance_from_fleet(w.systems, *f, s),
                   it->second,
                   can,
                   std::move(reason)});
  }
  std::stable_sort(c.begin(), c.end(), [](auto &a, auto &b) {
    if (a.can_order != b.can_order)
      return a.can_order;
    int x = dcmp(a.distance_from_fleet, b.distance_from_fleet);
    if (x)
      return x < 0;
    if (a.system_id != b.system_id)
      return a.system_id < b.system_id;
    return a.planetary_body_id < b.planetary_body_id;
  });
  if (c.size() > static_cast<size_t>(maximum))
    c.resize(maximum);
  const auto n =
      std::count_if(c.begin(), c.end(), [](auto &x) { return x.can_order; });
  std::string status =
      c.empty() ? "No fully surveyed rare-resource worlds are currently "
                  "available for outpost evaluation."
      : n == 0  ? std::to_string(c.size()) + " valuable world" +
                     (c.size() == 1 ? "" : "s") +
                     " evaluated; none can currently receive a staffed outpost."
               : std::to_string(n) + " staffed resource-outpost opportunit" +
                     (n == 1 ? "y" : "ies") + " available in a " +
                     std::to_string(c.size()) + "-world planning window.";
  return {f->id,  f->name,           f->civilization_id,
          sp->id, sp->display_name,  f->embarked_population_millions,
          true,   std::move(status), std::move(c)};
}
ResourceOutpostOrderAssessment ResourceOutpostOpportunityPlanner::assess_order(
    SettlementPlanningWorldView w, int id, int sid, int bid) const {
  auto p = build_plan(w, id, 64);
  if (!p.can_receive_orders)
    return {false, p.status, {}};
  auto i = std::find_if(p.candidates.begin(), p.candidates.end(), [&](auto &x) {
    return x.system_id == sid && x.planetary_body_id == bid;
  });
  if (i == p.candidates.end())
    return {
        false,
        "That body is not a fully surveyed rare-resource outpost candidate.",
        {}};
  if (!i->can_order)
    return {false, i->reason, *i};
  return {true,
          p.fleet_name + ": staffed resource outpost approved for " +
              i->planetary_body_name + " in " + i->system_name + ". " +
              i->reason,
          *i};
}
} // namespace stellar::core
