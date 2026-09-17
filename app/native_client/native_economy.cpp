#include "native_economy.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/sovereign_currency.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace stellar::native_economy {
namespace {
using namespace stellar::core;

constexpr std::size_t maximum_diagnostic_bytes = 256;

[[nodiscard]] bool finite(const double value) noexcept { return std::isfinite(value); }

[[nodiscard]] std::string bounded(std::string value) {
  if (value.size() <= maximum_diagnostic_bytes) return value;
  value.resize(maximum_diagnostic_bytes - 3);
  return value + "...";
}

[[nodiscard]] NativeEconomyView unavailable(std::string diagnostic) {
  NativeEconomyView view;
  view.message = "Economy information is unavailable. Refresh when the campaign is ready.";
  view.diagnostic = bounded(std::move(diagnostic));
  return view;
}

[[nodiscard]] NativeEconomyView failed(std::exception const& error) {
  NativeEconomyView view;
  view.state = EconomyState::Failed;
  view.message = "Economy information failed to load. Retry the panel; if it persists, export diagnostics.";
  view.diagnostic = bounded(error.what());
  return view;
}

class EconomyUnavailable final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

template <class Range, class Member>
[[nodiscard]] auto *find_exactly_one(Range& range, const int id, Member member,
                                     const char *missing, const char *duplicate) {
  auto first = std::ranges::find(range, id, member);
  if (first == range.end()) throw std::runtime_error(missing);
  if (std::ranges::find(std::next(first), range.end(), id, member) != range.end())
    throw std::runtime_error(duplicate);
  return &*first;
}

struct EconomyContext {
  const Civilization& player;
  const CivilizationEconomy& economy;
};

[[nodiscard]] EconomyContext validate(const FreshCampaignState& campaign) {
  const auto player_count = std::ranges::count(campaign.civilizations,
                                               campaign.player_civilization_id,
                                               &Civilization::id);
  if (player_count != 1) throw EconomyUnavailable("The actual player observer is missing or duplicated.");
  const auto player = std::ranges::find(campaign.civilizations,
                                        campaign.player_civilization_id, &Civilization::id);
  if (!player->is_player) throw EconomyUnavailable("The actual player observer is invalid.");
  if (std::ranges::count(campaign.systems, player->home_system_id, &StellarSystem::id) != 1)
    throw EconomyUnavailable("The actual player home system is missing or duplicated.");
  if (std::ranges::count(campaign.economies, player->id, &CivilizationEconomy::civilization_id) != 1)
    throw EconomyUnavailable("The actual player economy is missing or duplicated.");
  if (std::ranges::count(campaign.construction, player->id, &ConstructionState::civilization_id) != 1)
    throw EconomyUnavailable("The actual player construction state is missing or duplicated.");
  const auto *economy = find_exactly_one(
      campaign.economies, player->id,
      &CivilizationEconomy::civilization_id, "unreachable", "unreachable");
  (void)find_exactly_one(campaign.construction,
                         player->id, &ConstructionState::civilization_id,
                         "unreachable", "unreachable");
  const double values[] = {economy->credits, economy->industry,
                           economy->last_industry_per_second,
                           economy->operating_arrears};
  if (!std::ranges::all_of(values, finite))
    throw std::runtime_error("The actual player economy contains a non-finite value.");
  return {*player, *economy};
}

[[nodiscard]] EconomyWorldView world_for(const FreshCampaignState& campaign,
                                          std::vector<EconomyConstructionState>& construction,
                                          std::vector<EconomyFleetState>& fleets) {
  construction = economic_construction_projection(campaign.construction);
  fleets = economic_fleet_projection(campaign.fleets);
  return {campaign.civilizations, campaign.bodies, construction, fleets};
}

[[nodiscard]] std::string grouped(const double value) {
  if (!finite(value)) throw std::runtime_error("A displayed material value is non-finite.");
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(0) << value;
  auto result = out.str();
  const auto decimal = result.find('.');
  const auto whole_end = decimal == std::string::npos ? result.size() : decimal;
  const auto offset = result.starts_with('-') ? 1u : 0u;
  for (auto at = static_cast<std::ptrdiff_t>(whole_end) - 3;
       at > static_cast<std::ptrdiff_t>(offset); at -= 3)
    result.insert(static_cast<std::size_t>(at), 1, ',');
  return result;
}

[[nodiscard]] std::string material_rate(const double value) {
  if (!finite(value)) throw std::runtime_error("A displayed material rate is non-finite.");
  return std::format("{:+.2f} / DAY", value);
}

[[nodiscard]] std::string rate(const SovereignCurrencyDefinition& currency,
                               const double value) {
  if (!finite(value)) throw std::runtime_error("A displayed credit rate is non-finite.");
  if (std::abs(value) > std::numeric_limits<double>::max() / currency.local_units_per_budget_unit)
    throw std::runtime_error("A displayed credit rate exceeds safe currency formatting range.");
  return currency.format_rate(value);
}

[[nodiscard]] std::string amount(const SovereignCurrencyDefinition& currency,
                                 const double value) {
  if (!finite(value)) throw std::runtime_error("A displayed credit amount is non-finite.");
  if (std::abs(value) > std::numeric_limits<double>::max() / currency.local_units_per_budget_unit)
    throw std::runtime_error("A displayed credit amount exceeds safe currency formatting range.");
  return currency.format(value);
}

[[nodiscard]] std::string priority_name(const IndustryPriority priority) {
  switch (priority) {
  case IndustryPriority::Balanced: return "Balanced";
  case IndustryPriority::InfrastructureFirst: return "Infrastructure first";
  case IndustryPriority::ShipbuildingFirst: return "Shipbuilding first";
  }
  throw std::runtime_error("The actual player economy has an unknown industry priority.");
}

[[nodiscard]] bool valid_priority(const IndustryPriority value) noexcept {
  return value == IndustryPriority::Balanced ||
         value == IndustryPriority::InfrastructureFirst ||
         value == IndustryPriority::ShipbuildingFirst;
}

[[nodiscard]] bool same_rendered(const NativeEconomyView& left,
                                  const NativeEconomyView& right) {
  return left.state == right.state &&
         left.player_civilization_id == right.player_civilization_id &&
         left.message == right.message && left.diagnostic == right.diagnostic &&
         left.cards == right.cards && left.treasury_status == right.treasury_status &&
         left.priority_status == right.priority_status &&
         left.priority_guidance == right.priority_guidance &&
         left.treasury_healthy == right.treasury_healthy &&
         left.industry_priority == right.industry_priority &&
         left.income_rows == right.income_rows && left.cost_rows == right.cost_rows;
}

}  // namespace

NativeEconomyView build_economy_view(
    const FreshCampaignState& campaign, const AdaptiveResearchCampaignState* research,
    const std::optional<CivilizationIndustryAllocation>& last_allocation) {
  try {
    const auto context = validate(campaign);
    const auto player = context.player.id;
    std::vector<EconomyConstructionState> construction;
    std::vector<EconomyFleetState> fleets;
    const auto world = world_for(campaign, construction, fleets);
    const auto flow = economy_credit_flow(world, campaign.colonies, campaign.economies, player);
    const auto capacity = industry_storage_capacity(world, campaign.colonies, player);
    const double flow_values[] = {flow.net_credits_per_day, flow.gross_income_per_day,
        flow.operating_costs_per_day, flow.colony_revenue_per_day, flow.trade_revenue_per_day,
        flow.colony_administration_per_day, flow.population_services_per_day,
        flow.habitat_support_per_day, flow.fleet_operations_per_day,
        flow.orbital_maintenance_per_day, flow.surface_maintenance_per_day,
        flow.research_operations_per_day, capacity};
    if (!std::ranges::all_of(flow_values, finite))
      throw std::runtime_error("The canonical economy projection contains a non-finite value.");
    const auto currency = sovereign_currency_for_civilization(campaign.civilizations, player);
    if (!finite(currency.local_units_per_budget_unit) || currency.local_units_per_budget_unit <= 0.)
      throw std::runtime_error("The actual player currency definition is invalid.");

    NativeEconomyView view;
    view.state = EconomyState::Ready;
    view.player_civilization_id = player;
    view.message = "Live civilian revenue and operating commitments.";
    view.cards = {{{"RESERVES", amount(currency, context.economy.credits)},
                   {"NET / DAY", rate(currency, flow.net_credits_per_day), flow.net_credits_per_day < 0.},
                   {"INCOME / DAY", rate(currency, flow.gross_income_per_day)},
                   {"COSTS / DAY", rate(currency, -flow.operating_costs_per_day), true},
                   {"MATERIALS IN STORAGE", grouped(context.economy.industry) + " / " + grouped(capacity)},
                   {"MATERIALS / DAY", material_rate(context.economy.last_industry_per_second)}}};
    const auto health = assess_treasury(context.economy.credits, flow.net_credits_per_day,
                                        context.economy.operating_arrears);
    view.treasury_healthy = health.state == TreasuryHealthState::Surplus;
    switch (health.state) {
    case TreasuryHealthState::Surplus: view.treasury_status = "SURPLUS: Current income covers operating commitments."; break;
    case TreasuryHealthState::Deficit:
      if (!finite(health.runway_days)) throw std::runtime_error("Treasury runway is non-finite.");
      view.treasury_status = std::format("DEFICIT: Treasury runway {:.1f} days.", health.runway_days); break;
    case TreasuryHealthState::Depleted: view.treasury_status = "TREASURY DEPLETED: New authorizations are blocked."; break;
    case TreasuryHealthState::Arrears: view.treasury_status = "OPERATING ARREARS: New income repays arrears before rebuilding reserves."; break;
    }
    view.industry_priority = context.economy.industry_priority.value_or(IndustryPriority::Balanced);
    if (!valid_priority(view.industry_priority))
      throw std::runtime_error("The actual player economy has an unknown industry priority.");
    const auto weights = campaign_industry_weights(campaign.economies, player);
    view.priority_guidance = "Free to change. Infrastructure : ships when materials compete — ";
    for (const auto choice : {IndustryPriority::Balanced, IndustryPriority::InfrastructureFirst,
                              IndustryPriority::ShipbuildingFirst}) {
      auto preview = context.economy;
      preview.industry_priority = choice;
      const auto candidate = campaign_industry_weights(std::span{&preview, 1}, player);
      if (choice != IndustryPriority::Balanced) view.priority_guidance += "; ";
      view.priority_guidance += std::format("{} {:.0f}:{:.0f}", priority_name(choice),
          candidate.construction_weight, candidate.shipbuilding_weight);
    }
    if (!finite(weights.construction_weight) || !finite(weights.shipbuilding_weight))
      throw std::runtime_error("The actual player industry weights are non-finite.");
    view.priority_status = std::format("Current choice: {} ({:.0f}:{:.0f}).",
                                       priority_name(view.industry_priority),
                                       weights.construction_weight, weights.shipbuilding_weight);
    if (last_allocation && last_allocation->civilization_id == player &&
        finite(last_allocation->construction_allocated) && finite(last_allocation->shipbuilding_allocated))
      view.priority_status += std::format(" Most recent allocation: {:.1f} materials to infrastructure and {:.1f} to shipbuilding.",
                                          last_allocation->construction_allocated,
                                          last_allocation->shipbuilding_allocated);
    else
      view.priority_status += " It applies when demand competes; spare materials go to other work.";
    view.income_rows = {{"COLONY ECONOMY", rate(currency, flow.colony_revenue_per_day), {}, true},
                        {"SURFACE TRADE", rate(currency, flow.trade_revenue_per_day), {}, true}};
    double reserved{};
    if (research) for (const auto& funding : research->project_funding(player)) {
      const auto remaining = funding.reserved_milestone_credits - funding.consumed_milestone_credits;
      if (!finite(remaining)) throw std::runtime_error("Research reservation is non-finite.");
      reserved += std::max(0., remaining);
    }
    if (!finite(reserved)) throw std::runtime_error("Research reservation is non-finite.");
    view.cost_rows = {{"COLONY ADMINISTRATION", rate(currency, -flow.colony_administration_per_day)},
      {"POPULATION SERVICES", rate(currency, -flow.population_services_per_day)},
      {"HABITAT SUPPORT", rate(currency, -flow.habitat_support_per_day)},
      {"FLEET OPERATIONS", rate(currency, -flow.fleet_operations_per_day)},
      {"ORBITAL MAINTENANCE", rate(currency, -flow.orbital_maintenance_per_day)},
      {"SURFACE MAINTENANCE", rate(currency, -flow.surface_maintenance_per_day)},
      {"RESEARCH PROGRAMS", rate(currency, -flow.research_operations_per_day), " · " + amount(currency, reserved) + " RESERVED"}};
    return view;
  } catch (const EconomyUnavailable& error) {
    auto view = unavailable(error.what());
    view.player_civilization_id = campaign.player_civilization_id;
    return view;
  } catch (const std::exception& error) {
    auto view = failed(error);
    view.player_civilization_id = campaign.player_civilization_id;
    return view;
  }
}

NativeEconomyController::NativeEconomyController() : projector_(build_economy_view) {}
NativeEconomyController::NativeEconomyController(Projector projector)
    : projector_(projector ? std::move(projector) : Projector{build_economy_view}) {}

void NativeEconomyController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("Native economy control must run on the simulation owner thread.");
}

bool NativeEconomyController::refresh(CampaignFrame& frame, const std::uint64_t generation,
    const std::optional<CivilizationIndustryAllocation>& last_allocation, const bool explicit_retry) {
  require_owner();
  if (generation_ && generation < *generation_) return false;
  auto& runtime = frame.runtime();
  const auto& campaign = runtime.world().campaign();
  const auto observer = campaign.player_civilization_id;
  const bool same_identity = generation_ && observer_ && *generation_ == generation && *observer_ == observer;
  if (same_identity && failure_latched_ && !explicit_retry) return false;
  if (!same_identity) { failure_latched_ = false; generation_ = generation; observer_ = observer; view_ = {}; }
  ++attempted_refresh_count_;
  NativeEconomyView projected;
  try { projected = projector_(campaign, &runtime.research(), last_allocation); }
  catch (const std::exception& error) { projected = failed(error); }
  if (projected.state == EconomyState::Ready && projected.player_civilization_id != observer)
    projected = failed(std::runtime_error("Economy projection identity does not match the actual player observer."));
  if (projected.state != EconomyState::Ready) projected.player_civilization_id = observer;
  projected.campaign_generation = generation;
  if (projected.state == EconomyState::Failed) failure_latched_ = true;
  else failure_latched_ = false;
  if (!same_rendered(view_, projected) || view_.revision == 0) {
    if (next_revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Native economy revision is exhausted.");
    projected.revision = next_revision_++;
  } else projected.revision = view_.revision;
  view_ = std::move(projected);
  if (view_.state == EconomyState::Ready) ++successful_refresh_count_;
  return true;
}

IndustryPriorityChangeResult NativeEconomyController::change_priority(
    CampaignFrame& frame, const std::uint64_t generation, const std::uint64_t view_revision,
    const IndustryPriority priority) {
  require_owner();
  if (!valid_priority(priority)) return {false, "Unknown industry priority."};
  if (!generation_ || !observer_ || *generation_ != generation || view_.state != EconomyState::Ready ||
      view_.revision == 0 || view_.revision != view_revision)
    return {false, "Economy changed; refresh before setting industry priority."};
  auto& campaign = frame.runtime().world().campaign();
  if (campaign.player_civilization_id != *observer_) return {false, "Economy changed; refresh before setting industry priority."};
  try {
    const auto live = validate(campaign);
    if (live.player.id != *observer_) return {false, "Economy changed; refresh before setting industry priority."};
  } catch (const std::exception&) {
    return {false, "Economy changed; refresh before setting industry priority."};
  }
  const auto player = std::ranges::find(campaign.civilizations, *observer_, &Civilization::id);
  if (std::ranges::count(campaign.civilizations, *observer_, &Civilization::id) != 1 ||
      player == campaign.civilizations.end() || !player->is_player) return {false, "Economy changed; refresh before setting industry priority."};
  if (std::ranges::count(campaign.economies, *observer_, &CivilizationEconomy::civilization_id) != 1)
    return {false, "Economy changed; refresh before setting industry priority."};
  const auto economy = std::ranges::find(campaign.economies, *observer_, &CivilizationEconomy::civilization_id);
  if (economy == campaign.economies.end() ||
      economy->industry_priority.value_or(IndustryPriority::Balanced) != view_.industry_priority)
    return {false, "Economy changed; refresh before setting industry priority."};
  const auto result = set_industry_priority(campaign.economies, *observer_, *observer_, priority);
  if (result.accepted) view_.revision = 0;
  return result;
}

void NativeEconomyController::clear() {
  require_owner();
  view_ = {};
  generation_.reset();
  observer_.reset();
  failure_latched_ = false;
}

}  // namespace stellar::native_economy
