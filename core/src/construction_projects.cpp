#include <stellar/core/construction_projects.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
const std::array<ConstructionProjectDefinition, 6> &catalog() {
  static const std::array<ConstructionProjectDefinition, 6> v{
      {{"research_network",
        "Planetary Research Network",
        "Expand universities, laboratories, compute infrastructure, and "
        "scientific coordination. Adds 4 Effective Research Labs.",
        700,
        {},
        ConstructionCategory::Science,
        150},
       {"industrial_automation",
        "Industrial Automation Program",
        "Modernize planetary production with autonomous fabrication and "
        "logistics.",
        900,
        {},
        ConstructionCategory::Industry,
        200},
       {"orbital_launch_complex",
        "Orbital Launch Complex",
        "Build permanent heavy-lift infrastructure needed for sustained "
        "orbital construction.",
        1100,
        {},
        ConstructionCategory::Orbital,
        250,
        0,
        .08},
       {"orbital_shipyard",
        "Orbital Shipyard",
        "Construct a permanent orbital yard capable of assembling large "
        "interplanetary and future interstellar vessels.",
        1600,
        {"orbital_industry"},
        ConstructionCategory::Orbital,
        350,
        0,
        .12,
        {"orbital_launch_complex"}},
       {"asteroid_resource_network",
        "Asteroid Resource Network",
        "Deploy prospectors, autonomous extraction platforms, and cargo tugs "
        "across the home system's resource belt.",
        1800,
        {"orbital_industry"},
        ConstructionCategory::Orbital,
        320,
        1.5,
        .18,
        {"orbital_launch_complex"}},
       {"warp_test_facility",
        "Warp Test Facility",
        "A remote hardened research and engineering complex for full-scale "
        "spacetime-field experiments.",
        2200,
        {"warp_field_control"},
        ConstructionCategory::Ftl,
        450,
        0,
        .15}}};
  return v;
}
ConstructionState *state_for(ConstructionWorld w, int id) {
  auto i = std::find_if(w.construction.begin(), w.construction.end(),
                        [=](auto &s) { return s.civilization_id == id; });
  return i == w.construction.end() ? nullptr : &*i;
}
const ConstructionState *state_for(ConstructionReadView w, int id) {
  auto i = std::find_if(w.construction.begin(), w.construction.end(),
                        [=](auto &s) { return s.civilization_id == id; });
  return i == w.construction.end() ? nullptr : &*i;
}
CivilizationEconomy *economy_for(ConstructionWorld w, int id) {
  auto i = std::find_if(w.economies.begin(), w.economies.end(),
                        [=](auto &e) { return e.civilization_id == id; });
  return i == w.economies.end() ? nullptr : &*i;
}
const Civilization *civ_for(std::span<const Civilization> cs, int id) {
  auto i =
      std::find_if(cs.begin(), cs.end(), [=](auto &c) { return c.id == id; });
  return i == cs.end() ? nullptr : &*i;
}
bool done(const ConstructionState &s, std::string_view id) {
  return std::find(s.completed_project_ids.begin(),
                   s.completed_project_ids.end(),
                   id) != s.completed_project_ids.end();
}
std::string lock(ConstructionReadView w, int id,
                 const ConstructionProjectDefinition &p) {
  std::vector<std::string> m;
  for (auto &x : p.required_technologies)
    if (!construction_has_capability(w, id, x))
      m.push_back(x == "orbital_industry"     ? "Orbital Industry"
                  : x == "warp_field_control" ? "Warp Field Control"
                                              : x);
  for (auto &x : p.required_projects)
    if (!done(*state_for(w, id), x))
      m.push_back(get_construction_project(x).name);
  if (m.empty())
    return {};
  std::string r = "requires ";
  for (size_t i = 0; i < m.size(); ++i) {
    if (i)
      r += " and ";
    r += m[i];
  }
  return r;
}
void promote(ConstructionWorld w, int id, ConstructionState &s) {
  if (s.active_project_id || s.queued_projects.empty())
    return;
  auto &o = s.queued_projects.front();
  auto *p = find_construction_project(o.project_id);
  if (!p || done(s, o.project_id) || !lock(w.read(), id, *p).empty())
    return;
  s.active_project_id = o.project_id;
  s.active_project_progress = 0;
  s.active_project_authorization_credits = o.authorization_credits;
  s.queued_projects.erase(s.queued_projects.begin());
}
ConstructionOrderResult authorize(ConstructionWorld w, int id,
                                  std::string_view pid, ConstructionState &s) {
  auto *p = find_construction_project(pid);
  if (!p)
    return {false, "Unknown construction project."};
  if (done(s, p->id))
    return {false, p->name + " is already complete."};
  auto l = lock(w.read(), id, *p);
  if (!l.empty())
    return {false, p->name + " is locked: " + l + "."};
  auto *e = economy_for(w, id);
  if (!e)
    throw std::out_of_range("Sequence contains no matching element");
  auto cur = sovereign_currency_for_civilization(w.civilizations, id);
  if (e->credits + .0001 < p->credit_cost)
    return {false, cur.format(p->credit_cost) + " is required to authorize " +
                       p->name + "."};
  e->credits -= p->credit_cost;
  s.active_project_id = p->id;
  s.active_project_progress = 0;
  s.active_project_authorization_credits = p->credit_cost;
  return {true, "Construction started: " + p->name + ". Authorized for " +
                    cur.format(p->credit_cost) + "."};
}
} // namespace
std::span<const ConstructionProjectDefinition> construction_project_catalog() {
  return catalog();
}
const ConstructionProjectDefinition *
find_construction_project(std::string_view id) {
  auto i = std::find_if(catalog().begin(), catalog().end(),
                        [=](auto &p) { return p.id == id; });
  return i == catalog().end() ? nullptr : &*i;
}
const ConstructionProjectDefinition &
get_construction_project(std::string_view id) {
  auto *p = find_construction_project(id);
  if (!p)
    throw std::out_of_range("Sequence contains no matching element");
  return *p;
}
std::optional<std::string>
construction_project_lock_reason(ConstructionReadView w, int id,
                                 const ConstructionProjectDefinition &p) {
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  auto x = lock(w, id, p);
  return x.empty() ? std::nullopt : std::optional<std::string>{x};
}
std::vector<ConstructionProjectDefinition>
available_construction_projects(ConstructionReadView w, int id) {
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  std::vector<ConstructionProjectDefinition> r;
  for (auto &p : catalog())
    if (!done(*s, p.id) && lock(w, id, p).empty())
      r.push_back(p);
  return r;
}
std::optional<std::string> construction_queue_blocker(ConstructionReadView w,
                                                      int id) {
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  if (s->active_project_id || s->queued_projects.empty())
    return {};
  auto *p = find_construction_project(s->queued_projects.front().project_id);
  if (!p)
    return "Queued project is unavailable.";
  auto l = lock(w, id, *p);
  return l.empty() ? std::optional<std::string>{}
                   : std::optional<std::string>{l};
}
ConstructionOrderResult start_construction_project(ConstructionWorld w, int id,
                                                   std::string_view pid) {
  if (!civ_for(w.civilizations, id))
    return {false, "Unknown civilization."};
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  promote(w, id, *s);
  if (s->active_project_id)
    return {false, "A construction project is already in progress."};
  if (!s->queued_projects.empty()) {
    auto b = construction_queue_blocker(w.read(), id);
    return {false, b ? "The queued construction head is blocked: " + *b +
                           ". Cancel it or restore its requirements first."
                     : "A queued construction project is waiting to start."};
  }
  return authorize(w, id, pid, *s);
}
ConstructionOrderResult queue_construction_project(ConstructionWorld w, int id,
                                                   std::string_view pid) {
  if (!civ_for(w.civilizations, id))
    return {false, "Unknown civilization."};
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  promote(w, id, *s);
  if (!s->active_project_id && s->queued_projects.empty())
    return authorize(w, id, pid, *s);
  if (s->queued_projects.size() >= maximum_queued_construction_projects)
    return {false, "The construction queue is full (8 projects maximum)."};
  auto *p = find_construction_project(pid);
  if (!p)
    return {false, "Unknown construction project."};
  if (done(*s, p->id))
    return {false, p->name + " is already complete."};
  auto l = lock(w.read(), id, *p);
  if (!l.empty())
    return {false, p->name + " is locked: " + l + "."};
  if (s->active_project_id == p->id ||
      std::any_of(s->queued_projects.begin(), s->queued_projects.end(),
                  [&](auto &o) { return o.project_id == p->id; }))
    return {false, p->name + " is already active or queued."};
  auto *e = economy_for(w, id);
  if (!e)
    throw std::out_of_range("Sequence contains no matching element");
  auto c = sovereign_currency_for_civilization(w.civilizations, id);
  if (e->credits + .0001 < p->credit_cost)
    return {false, c.format(p->credit_cost) + " is required to authorize " +
                       p->name + "."};
  e->credits -= p->credit_cost;
  s->queued_projects.push_back({p->id, p->credit_cost});
  return {true, "Queued " + p->name + ". Authorized for " +
                    c.format(p->credit_cost) + "."};
}
double construction_cancellation_refund_preview(const ConstructionState &s,
                                                std::string_view id) {
  if (s.active_project_id == id) {
    auto *p = find_construction_project(id);
    if (p)
      return std::max(0., s.active_project_authorization_credits) *
             std::clamp((p->industry_cost - s.active_project_progress) /
                            p->industry_cost,
                        0., 1.);
  }
  auto i = std::find_if(s.queued_projects.begin(), s.queued_projects.end(),
                        [&](auto &o) { return o.project_id == id; });
  return i == s.queued_projects.end() ? 0. : i->authorization_credits;
}
ConstructionCancellationResult
cancel_construction_project(ConstructionWorld w, int id, std::string_view pid) {
  auto *s = state_for(w, id);
  auto *e = economy_for(w, id);
  if (!s || !e)
    return {false, "Unknown civilization.", 0};
  if (s->active_project_id == pid) {
    auto *p = find_construction_project(pid);
    if (p) {
      double r = construction_cancellation_refund_preview(*s, pid);
      e->credits += r;
      s->active_project_id.reset();
      s->active_project_progress = 0;
      s->active_project_authorization_credits = 0;
      promote(w, id, *s);
      auto c = sovereign_currency_for_civilization(w.civilizations, id);
      return {true,
              "Cancelled " + p->name + "; refunded " + c.format(r) +
                  ". Consumed materials are not refunded.",
              r};
    }
  }
  auto i = std::find_if(s->queued_projects.begin(), s->queued_projects.end(),
                        [&](auto &o) { return o.project_id == pid; });
  if (i == s->queued_projects.end())
    return {false, "That project is not active or queued.", 0};
  auto o = *i;
  s->queued_projects.erase(i);
  e->credits += o.authorization_credits;
  promote(w, id, *s);
  auto name = get_construction_project(o.project_id).name;
  auto c = sovereign_currency_for_civilization(w.civilizations, id);
  return {true,
          "Cancelled queued " + name + "; refunded " +
              c.format(o.authorization_credits) + ".",
          o.authorization_credits};
}
double construction_industry_demand(ConstructionReadView w, int id,
                                    double days) {
  auto *s = state_for(w, id);
  if (!s)
    throw std::out_of_range("Sequence contains no matching element");
  double project_demand = 0;
  if (s->active_project_id) {
    auto &p = get_construction_project(*s->active_project_id);
    project_demand =
        std::min(std::max(0., p.industry_cost - s->active_project_progress),
                 construction_project_industry_per_day * std::max(0., days));
  }
  return project_demand + surface_construction_industry_demand(w, id, days);
}
void ensure_automatic_construction_orders(ConstructionWorld w) {
  for (auto &c : w.civilizations) {
    if (c.is_seeded_ancient)
      continue;
    auto *s = state_for(w, c.id);
    if (!s)
      throw std::out_of_range("Sequence contains no matching element");
    promote(w, c.id, *s);
    if (c.is_player || s->active_project_id || !s->queued_projects.empty())
      continue;
    auto *e = economy_for(w, c.id);
    if (!e)
      throw std::out_of_range("Sequence contains no matching element");
    auto a = available_construction_projects(w.read(), c.id);
    std::sort(a.begin(), a.end(), [&](auto &x, auto &y) {
      auto score = [&](auto &p) {
        switch (p.category) {
        case ConstructionCategory::Science:
          return 1 + c.traits.scientific_curiosity * .9;
        case ConstructionCategory::Industry:
          return 1 + c.traits.greed * .55 + c.traits.territoriality * .2;
        case ConstructionCategory::Orbital:
          return 1.15 + c.traits.territoriality * .3 +
                 c.traits.scientific_curiosity * .2;
        case ConstructionCategory::Ftl:
          return 1.3 + c.traits.scientific_curiosity * .4 +
                 c.traits.aggression * .2;
        }
        return 1.;
      };
      auto sx = score(x), sy = score(y);
      return sx == sy ? x.industry_cost < y.industry_cost : sx > sy;
    });
    for (auto &p : a)
      if (e->credits + .0001 >= p.credit_cost) {
        (void)start_construction_project(w, c.id, p.id);
        break;
      }
  }
}
std::vector<ConstructionEvent>
advance_construction_for_civilization(ConstructionWorld w, int id,
                                      double budget, double days) {
  if (!std::isfinite(days) || days < 0)
    throw std::out_of_range(
        "Construction requires finite nonnegative industry and elapsed days.");
  if (days == 0)
    return {};
  auto *c = civ_for(w.civilizations, id);
  if (!c)
    return {};
  if (c->is_seeded_ancient)
    return {};
  auto *s = state_for(w, id);
  auto *e = economy_for(w, id);
  if (!s || !e)
    throw std::out_of_range("Sequence contains no matching element");
  promote(w, id, *s);
  if (!std::isfinite(e->industry))
    throw std::out_of_range("Available Industry must be finite.");
  if (!std::isfinite(budget))
    throw std::out_of_range("Industry budgets must be finite.");
  double avail = std::min(std::max(0., budget), std::max(0., e->industry));
  double sd = surface_construction_industry_demand(w.read(), id, days),
         pd = s->active_project_id
                  ? std::min(construction_project_industry_per_day * days,
                             std::max(0., get_construction_project(
                                              *s->active_project_id)
                                                  .industry_cost -
                                              s->active_project_progress))
                  : 0;
  double sb =
      sd > 0 ? std::min(sd, std::min(avail, sd + pd) * sd / (sd + pd)) : 0;
  advance_surface_construction(w, id, sb, days);
  avail = std::min(e->industry, std::max(0., avail - sb));
  std::vector<ConstructionEvent> r;
  if (!s->active_project_id)
    return r;
  auto &p = get_construction_project(*s->active_project_id);
  double rem = std::max(0., p.industry_cost - s->active_project_progress),
         sp = std::min(
             rem,
             std::min(avail, construction_project_industry_per_day * days));
  if (sp <= 0 && rem > .0001)
    return r;
  e->industry -= sp;
  s->active_project_progress += sp;
  if (s->active_project_progress + .0001 < p.industry_cost)
    return r;
  if (!done(*s, p.id))
    s->completed_project_ids.push_back(p.id);
  s->active_project_id.reset();
  s->active_project_progress = 0;
  s->active_project_authorization_credits = 0;
  r.push_back({id, p.id, c->name + " completed " + p.name + "."});
  promote(w, id, *s);
  return r;
}
std::vector<ConstructionEvent> advance_construction(
    ConstructionWorld w,
    std::optional<std::span<const ConstructionIndustryBudget>> budgets,
    double days) {
  if (!std::isfinite(days) || days < 0)
    throw std::out_of_range(
        "Construction requires finite nonnegative industry and elapsed days.");
  if (days == 0)
    return {};
  ensure_automatic_construction_orders(w);
  std::vector<ConstructionEvent> r;
  for (auto &c : w.civilizations) {
    if (c.is_seeded_ancient)
      continue;
    auto *e = economy_for(w, c.id);
    if (!e)
      throw std::out_of_range("Sequence contains no matching element");
    double b = 0;
    if (budgets) {
      auto i = std::find_if(budgets->begin(), budgets->end(),
                            [&](auto &x) { return x.civilization_id == c.id; });
      if (i != budgets->end())
        b = i->industry;
    } else {
      if (!std::isfinite(e->industry))
        throw std::out_of_range("Available Industry must be finite.");
      b = e->industry;
    }
    auto x = advance_construction_for_civilization(w, c.id, b, days);
    r.insert(r.end(), x.begin(), x.end());
  }
  return r;
}
} // namespace stellar::core
