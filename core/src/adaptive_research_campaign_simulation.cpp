#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <stellar/core/adaptive_research_campaign_simulation.hpp>
#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/detail/adaptive_research_campaign_state_access.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/core/surface_economy.hpp>
namespace stellar::core {
namespace {
constexpr std::string_view network_id = "construction:research_network",
                           surface_prefix = "construction:surface:";
std::string invariant_percent_zero(double value) {
  const double scaled = value * 100.0;
  const double lower = std::floor(scaled);
  const double fraction = scaled - lower;
  double rounded = lower;
  if (fraction > 0.5)
    rounded = lower + 1.0;
  else if (fraction == 0.5) {
    const double product_error = std::fma(value, 100.0, -scaled);
    if (product_error > 0.0 ||
        (product_error == 0.0 && std::fmod(lower, 2.0) != 0.0))
      rounded = lower + 1.0;
  }
  return std::to_string(static_cast<long long>(rounded)) + " %";
}

double source_max(double left, double right) noexcept {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return left > right ? left : right;
}

double source_min(double left, double right) noexcept {
  if (std::isnan(left) || std::isnan(right))
    return std::numeric_limits<double>::quiet_NaN();
  return left < right ? left : right;
}

double source_clamp(double value, double minimum, double maximum) noexcept {
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}
std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t point{};
    std::size_t width{};
    if (first <= 0x7f) {
      point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      point = first & 7;
      width = 4;
    } else {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
    if (value.size() < width)
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      point = (point << 6) | (byte & 0x3f);
    }
    value.remove_prefix(width);
    if (point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(point));
    } else {
      point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
  }
  return result;
}
template <class R> auto *first(R &&r, auto pred) {
  auto i = std::find_if(r.begin(), r.end(), pred);
  return i == r.end() ? nullptr : &*i;
}
void append(std::vector<AdaptiveResearchCampaignEvent> &out, int civ,
            std::span<const AdaptiveResearchRuntimeEvent> events) {
  for (auto &e : events)
    if (e.node_id)
      out.push_back({civ, *e.node_id, e.message, false});
}
void surface_sync(FreshCampaignState &w, int id, AdaptiveResearchCampaignState &campaign,
                  AdaptiveResearchCivilizationState &state) {
  struct Desired {
    std::string key, archetype, context;
  };
  std::vector<Desired> desired;
  std::vector<const Colony *> colonies;
  for (auto &c : w.colonies)
    if (c.civilization_id == id)
      colonies.push_back(&c);
  std::stable_sort(colonies.begin(), colonies.end(), [](auto *a, auto *b) { return a->id < b->id; });
  for (auto *c : colonies) {
    auto output = surface_colony_output(*c);
    auto spec = surface_colony_specialization(*c);
    bool district = spec.id == "science_lab" && spec.active;
    std::vector<const SurfaceBuilding *> buildings;
    for (auto &b : c->surface_buildings)
      if (b.is_complete &&
          std::find(output.powered_building_ids.begin(), output.powered_building_ids.end(), b.id) !=
              output.powered_building_ids.end() &&
          surface_functional_family(b.type_id) == "science_lab")
        buildings.push_back(&b);
    std::stable_sort(buildings.begin(), buildings.end(), [](auto *a, auto *b) { return a->id < b->id; });
    for (auto *b : buildings) {
      bool advanced = b->type_id == "advanced_science_lab";
      std::string archetype = advanced ? (district ? "advanced_surface_science_campus_district"
                                                   : "advanced_surface_science_campus")
                                       : (district ? "surface_science_laboratory_district"
                                                   : "surface_science_laboratory");
      Desired replacement{
          std::string(surface_prefix) + std::to_string(c->id) + ":" + std::to_string(b->id),
          std::move(archetype), "colony:" + std::to_string(c->id)};
      auto existing = std::find_if(desired.begin(), desired.end(), [&](const Desired &value) {
        return value.key == replacement.key;
      });
      if (existing == desired.end())
        desired.push_back(std::move(replacement));
      else
        *existing = std::move(replacement);
    }
  }
  std::vector<ResearchInstitutionRuntimeState> obsolete;
  for (auto &i : state.expertise().institutions())
    if (i.institution_instance_id.starts_with(surface_prefix) &&
        std::none_of(desired.begin(), desired.end(),
                     [&](auto &d) { return d.key == i.institution_instance_id; }))
      obsolete.push_back(i);
  for (auto &i : obsolete)
    campaign.runtime().authority().set_research_institution(
        state, i.institution_instance_id, i.institution_archetype_id, 0, 0,
        i.context_id ? std::optional<std::string_view>(*i.context_id) : std::nullopt);
  for (auto &d : desired) {
    auto *i = first(state.expertise().institutions(),
                    [&](auto &v) { return v.institution_instance_id == d.key; });
    if (i && i->institution_archetype_id == d.archetype && i->total_count == 1 &&
        i->active_count == 1 && i->context_id == std::optional<std::string>(d.context))
      continue;
    campaign.runtime().authority().set_research_institution(state, d.key, d.archetype, 1, 1,
                                                            d.context);
  }
}
void facilities(FreshCampaignState &w, int id, AdaptiveResearchCampaignState &campaign,
                AdaptiveResearchCivilizationState &state) {
  auto *c = first(w.construction, [&](auto &v) { return v.civilization_id == id; });
  if (!c)
    throw AdaptiveResearchCampaignOperationError("Sequence contains no matching element");
  bool complete = std::find(c->completed_project_ids.begin(), c->completed_project_ids.end(),
                            "research_network") != c->completed_project_ids.end();
  auto *i = first(state.expertise().institutions(),
                  [](auto &v) { return v.institution_instance_id == network_id; });
  if (complete && (!i || i->institution_archetype_id != "general_research_laboratory" ||
                   i->total_count != 4 || i->active_count != 4))
    campaign.runtime().authority().set_research_institution(state, network_id,
                                                            "general_research_laboratory", 4, 4);
  else if (!complete && i)
    campaign.runtime().authority().set_research_institution(
        state, network_id, i->institution_archetype_id, 0, 0,
        i->context_id ? std::optional<std::string_view>(*i->context_id) : std::nullopt);
  surface_sync(w, id, campaign, state);
  if (std::find(c->completed_project_ids.begin(), c->completed_project_ids.end(),
                "warp_test_facility") != c->completed_project_ids.end())
    for (auto cap : {"precision_measurement", "high_energy_experimentation",
                     "field_physics_experimentation", "large_scale_prototyping"})
      campaign.runtime().authority().kernel().add_facility_capability(state, cap);
}
bool milestone(AdaptiveResearchRuntimeEventType t) {
  return t == AdaptiveResearchRuntimeEventType::stage_advanced ||
         t == AdaptiveResearchRuntimeEventType::technology_matured ||
         t == AdaptiveResearchRuntimeEventType::hypothesis_disproven;
}
void milestones(FreshCampaignState &w, AdaptiveResearchCampaignState &campaign, int id,
                std::span<const AdaptiveResearchRuntimeEvent> input,
                std::vector<AdaptiveResearchCampaignEvent> &out) {
  for (auto &e : input)
    if (e.node_id && milestone(e.type)) {
      bool final = e.type == AdaptiveResearchRuntimeEventType::technology_matured ||
                   e.type == AdaptiveResearchRuntimeEventType::hypothesis_disproven;
      auto used = detail::AdaptiveResearchCampaignStateAccess::consume_project_milestone(
          campaign, id, *e.node_id, final);
      if (!used.found)
        continue;
      auto &node = campaign.runtime().authority().catalog().get_node(*e.node_id);
      auto currency = sovereign_currency_for_civilization(w.civilizations, id);
      std::string message =
          e.type == AdaptiveResearchRuntimeEventType::hypothesis_disproven
              ? "Research milestone closed: " + node.name + " consumed its remaining " +
                    currency.format(used.consumed_credits) +
                    " reserve during experimental resolution."
              : "Research milestone funded: " + node.name + " consumed " +
                    currency.format(used.consumed_credits) + "; " +
                    currency.format(used.remaining_credits) + " remains committed.";
      out.push_back({id, node.id, std::move(message), false});
    }
}
} // namespace
std::vector<AdaptiveResearchCampaignEvent> AdaptiveResearchCampaignSimulation::advance(
    FreshCampaignState &w, AdaptiveResearchCampaignState &campaign, double days, double now) const {
  if (!std::isfinite(days) || days < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter 'elapsedDays')");
  if (!std::isfinite(now) || now < 0)
    throw std::out_of_range("Specified argument was out of the range of valid values. "
                            "(Parameter 'currentSimulationDay')");
  if (days == 0)
    return {};
  double years = days / 365.25, current = 2050 + now / 365.25;
  std::vector<AdaptiveResearchCampaignEvent> out;
  std::vector<Civilization *> civs;
  for (auto &c : w.civilizations)
    if (!c.is_seeded_ancient)
      civs.push_back(&c);
  std::stable_sort(civs.begin(), civs.end(), [](auto *a, auto *b) { return a->id < b->id; });
  for (auto *c : civs) {
    auto &state = detail::AdaptiveResearchCampaignStateAccess::get_civilization(campaign, c->id);
    facilities(w, c->id, campaign, state);
    if (c->development_stage == CivilizationDevelopmentStage::PreWarp &&
        state.get_pressure("interstellar_distance") < 45) {
      auto events = campaign.runtime().authority().set_pressure(state, "interstellar_distance", 45);
      append(out, c->id, events);
    }
    auto *economy = first(w.economies, [&](auto &e) { return e.civilization_id == c->id; });
    if (!economy)
      throw AdaptiveResearchCampaignOperationError("Sequence contains no matching element");
    bool allpaused = std::all_of(state.active_projects().begin(), state.active_projects().end(),
                                 [](auto &p) { return p.paused; });
    if (!c->is_player && allpaused) {
      for (auto &candidate : campaign.runtime().agenda().build_visible_shortlist(state)) {
        if (!candidate.can_start)
          continue;
        auto quote = AdaptiveResearchFundingPolicy::quote(
            campaign.runtime().authority().catalog().get_node(candidate.node_id),
            candidate.requested_effective_labs, campaign.runtime().authority().catalog());
        if (economy->credits + .000001 >=
            AdaptiveResearchCampaignCommands::credits_needed_to_start(quote)) {
          auto &start = campaign.get_start(c->id);
          AdaptiveResearchFundingWorldView view{w.civilizations, w.economies};
          auto result = AdaptiveResearchCampaignCommands::start_directed_research(
              view, campaign, c->id, candidate.node_id, candidate.requested_effective_labs,
              start.applicability_context_id);
          if (!result.accepted)
            throw AdaptiveResearchCampaignOperationError(
                "Adaptive Research AI selected invalid project '" + candidate.node_id +
                "': " + result.message);
          append(out, c->id, result.events);
          break;
        }
      }
    }
    std::vector<ResearchProjectRuntimeState> active;
    for (auto &p : state.active_projects())
      if (!p.paused)
        active.push_back(p);
    double perday = 0;
    for (auto &p : active)
      perday += AdaptiveResearchFundingPolicy::quote(
                    campaign.runtime().authority().catalog().get_node(p.node_id),
                    p.assigned_effective_labs, campaign.runtime().authority().catalog())
                    .operating_credits_per_day;
    double requested = perday * days,
           funded = source_min(source_max(0., economy->credits), requested),
           fraction = requested <= .0000001 ? 1. : source_clamp(funded / requested, 0., 1.),
           previous = economy->last_research_funding_fraction;
    economy->credits = source_max(0., economy->credits - funded);
    economy->last_research_spending_per_day = days <= 0 ? 0 : funded / days;
    economy->last_research_funding_fraction = fraction;
    auto ec = economic_construction_projection(w.construction);
    auto ef = economic_fleet_projection(w.fleets);
    EconomyWorldView ew{w.civilizations, w.bodies, ec, ef};
    economy->last_credits_per_second =
        economy_credit_flow(ew, w.colonies, w.economies, c->id, false).net_credits_per_day -
        economy->last_research_spending_per_day;
    if (!active.empty() && previous >= .999999 && fraction < .999999)
      for (auto &p : active)
        out.push_back({c->id, p.node_id,
                       "Research funding shortfall: " +
                           campaign.runtime().authority().catalog().get_node(p.node_id).name +
                           " is operating at " +
                         invariant_percent_zero(fraction) +
                         "; progress is reduced until funding recovers.",
                       false});
    else if (!active.empty() && previous < .999999 && fraction >= .999999)
      for (auto &p : active)
        out.push_back({c->id, p.node_id,
                       "Research funding restored: " +
                           campaign.runtime().authority().catalog().get_node(p.node_id).name +
                           " has resumed fully funded operations.",
                       false});
    auto events = campaign.runtime().authority().advance_projects(state, years * fraction, current);
    append(out, c->id, events);
    milestones(w, campaign, c->id, events, out);
    std::vector<ResearchProjectRuntimeState> pending;
    for (auto &p : state.active_projects())
      if (p.paused &&
          p.pause_reason == std::optional<std::string>("hypothesis_resolution_required"))
        pending.push_back(p);
    std::stable_sort(pending.begin(), pending.end(), [](auto &a, auto &b) {
      return utf16(a.node_id) < utf16(b.node_id);
    });
    for (auto &p : pending) {
      auto resolution = campaign.runtime().outcomes().resolve_pending_hypothesis(
          state, p.node_id, std::to_string(w.seed), current,
          p.target_applicability_context_id
              ? std::optional<std::string_view>(*p.target_applicability_context_id)
              : std::nullopt);
      if (!resolution.accepted)
        throw AdaptiveResearchCampaignOperationError("Pending hypothesis '" + p.node_id +
                                                      "' could not resolve: " +
                                                      resolution.message);
      append(out, c->id, resolution.research_events);
      milestones(w, campaign, c->id, resolution.research_events, out);
      for (auto &e : resolution.outcome_events)
        out.push_back({c->id, e.node_id, e.message, true});
    }
  }
  AdaptiveResearchCampaignProgression::synchronize_development_stages(w, campaign);
  return out;
}
void AdaptiveResearchCampaignProgression::synchronize_development_stages(
    FreshCampaignState &w, const AdaptiveResearchCampaignState &campaign) {
  for (auto &c : w.civilizations) {
    auto &state = campaign.get_civilization(c.id);
    if (state.has_capability("experimental_interstellar_transit") &&
        c.development_stage == CivilizationDevelopmentStage::PreWarp)
      c.development_stage = CivilizationDevelopmentStage::WarpCapable;
  }
}
} // namespace stellar::core
