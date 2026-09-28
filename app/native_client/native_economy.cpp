#include "native_economy.hpp"

#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/engine/localization.hpp>

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

[[nodiscard]] std::string tr_at(const stellar::engine::LocalizationTable *table,
                                std::string_view key, std::string_view fallback) {
  if (table && table->contains(key))
    return std::string(table->translate(key));
  return std::string(fallback);
}

[[nodiscard]] std::string trf_at(const stellar::engine::LocalizationTable *table,
                                 std::string_view key,
                                 std::initializer_list<std::string> args,
                                 std::string_view fallback) {
  if (table && table->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return table->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

[[nodiscard]] bool finite(const double value) noexcept { return std::isfinite(value); }

[[nodiscard]] std::string bounded(std::string value) {
  if (value.size() <= maximum_diagnostic_bytes) return value;
  value.resize(maximum_diagnostic_bytes - 3);
  return value + "...";
}

[[nodiscard]] NativeEconomyView unavailable(std::string diagnostic,
                                            const stellar::engine::LocalizationTable *locale) {
  NativeEconomyView view;
  view.message = tr_at(locale, "ECONOMY_VIEW_UNAVAILABLE",
      "Economy information is unavailable. Refresh when the campaign is ready.");
  view.diagnostic = bounded(std::move(diagnostic));
  return view;
}

[[nodiscard]] NativeEconomyView failed(std::exception const& error,
                                       const stellar::engine::LocalizationTable *locale) {
  NativeEconomyView view;
  view.state = EconomyState::Failed;
  view.message = tr_at(locale, "ECONOMY_VIEW_FAILED",
      "Economy information failed to load. Retry the panel; if it persists, export diagnostics.");
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

[[nodiscard]] std::string priority_name(const IndustryPriority priority,
                                        const stellar::engine::LocalizationTable *locale) {
  switch (priority) {
  case IndustryPriority::Balanced: return tr_at(locale, "ECONOMY_PRIORITY_NAME_BALANCED", "Balanced");
  case IndustryPriority::InfrastructureFirst: return tr_at(locale, "ECONOMY_PRIORITY_NAME_INFRASTRUCTURE", "Infrastructure first");
  case IndustryPriority::ShipbuildingFirst: return tr_at(locale, "ECONOMY_PRIORITY_NAME_SHIPBUILDING", "Shipbuilding first");
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
    const std::optional<CivilizationIndustryAllocation>& last_allocation,
    const stellar::engine::LocalizationTable* locale) {
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
    view.message = tr_at(locale, "ECONOMY_VIEW_READY",
        "Live civilian revenue and operating commitments.");
    view.cards = {{{tr_at(locale, "ECONOMY_CARD_RESERVES", "RESERVES"), amount(currency, context.economy.credits)},
                   {tr_at(locale, "ECONOMY_CARD_NET", "NET / DAY"), rate(currency, flow.net_credits_per_day), flow.net_credits_per_day < 0.},
                   {tr_at(locale, "ECONOMY_CARD_INCOME", "INCOME / DAY"), rate(currency, flow.gross_income_per_day)},
                   {tr_at(locale, "ECONOMY_CARD_COSTS", "COSTS / DAY"), rate(currency, -flow.operating_costs_per_day), true},
                   {tr_at(locale, "ECONOMY_CARD_MATERIALS", "MATERIALS IN STORAGE"), grouped(context.economy.industry) + " / " + grouped(capacity)},
                   {tr_at(locale, "ECONOMY_CARD_RATE", "MATERIALS / DAY"), material_rate(context.economy.last_industry_per_second)}}};
    const auto health = assess_treasury(context.economy.credits, flow.net_credits_per_day,
                                        context.economy.operating_arrears);
    view.treasury_healthy = health.state == TreasuryHealthState::Surplus;
    switch (health.state) {
    case TreasuryHealthState::Surplus: view.treasury_status = tr_at(locale, "ECONOMY_TREASURY_SURPLUS", "SURPLUS: Current income covers operating commitments."); break;
    case TreasuryHealthState::Deficit:
      if (!finite(health.runway_days)) throw std::runtime_error("Treasury runway is non-finite.");
      view.treasury_status = trf_at(locale, "ECONOMY_TREASURY_DEFICIT",
          {std::format("{:.1f}", health.runway_days)}, "DEFICIT: Treasury runway {0} days."); break;
    case TreasuryHealthState::Depleted: view.treasury_status = tr_at(locale, "ECONOMY_TREASURY_DEPLETED", "TREASURY DEPLETED: New authorizations are blocked."); break;
    case TreasuryHealthState::Arrears: view.treasury_status = tr_at(locale, "ECONOMY_TREASURY_ARREARS", "OPERATING ARREARS: New income repays arrears before rebuilding reserves."); break;
    }
    view.industry_priority = context.economy.industry_priority.value_or(IndustryPriority::Balanced);
    if (!valid_priority(view.industry_priority))
      throw std::runtime_error("The actual player economy has an unknown industry priority.");
    const auto weights = campaign_industry_weights(campaign.economies, player);
    view.priority_guidance = tr_at(locale, "ECONOMY_GUIDANCE_PREFIX",
        "Free to change. Infrastructure : ships when materials compete — ");
    for (const auto choice : {IndustryPriority::Balanced, IndustryPriority::InfrastructureFirst,
                              IndustryPriority::ShipbuildingFirst}) {
      auto preview = context.economy;
      preview.industry_priority = choice;
      const auto candidate = campaign_industry_weights(std::span{&preview, 1}, player);
      if (choice != IndustryPriority::Balanced) view.priority_guidance += "; ";
      view.priority_guidance += trf_at(locale, "ECONOMY_GUIDANCE_WEIGHTS",
          {priority_name(choice, locale), std::format("{:.0f}", candidate.construction_weight),
           std::format("{:.0f}", candidate.shipbuilding_weight)}, "{0} {1}:{2}");
    }
    if (!finite(weights.construction_weight) || !finite(weights.shipbuilding_weight))
      throw std::runtime_error("The actual player industry weights are non-finite.");
    view.priority_status = trf_at(locale, "ECONOMY_PRIORITY_CURRENT",
        {priority_name(view.industry_priority, locale),
         std::format("{:.0f}", weights.construction_weight),
         std::format("{:.0f}", weights.shipbuilding_weight)},
        "Current choice: {0} ({1}:{2}).");
    if (last_allocation && last_allocation->civilization_id == player &&
        finite(last_allocation->construction_allocated) && finite(last_allocation->shipbuilding_allocated))
      view.priority_status += trf_at(locale, "ECONOMY_PRIORITY_ALLOCATION",
          {std::format("{:.1f}", last_allocation->construction_allocated),
           std::format("{:.1f}", last_allocation->shipbuilding_allocated)},
          " Most recent allocation: {0} materials to infrastructure and {1} to shipbuilding.");
    else
      view.priority_status += tr_at(locale, "ECONOMY_PRIORITY_NOTE",
          " It applies when demand competes; spare materials go to other work.");
    view.income_rows = {{tr_at(locale, "ECONOMY_ROW_COLONY", "COLONY ECONOMY"), rate(currency, flow.colony_revenue_per_day), {}, true},
                        {tr_at(locale, "ECONOMY_ROW_TRADE", "SURFACE TRADE"), rate(currency, flow.trade_revenue_per_day), {}, true}};
    double reserved{};
    if (research) for (const auto& funding : research->project_funding(player)) {
      const auto remaining = funding.reserved_milestone_credits - funding.consumed_milestone_credits;
      if (!finite(remaining)) throw std::runtime_error("Research reservation is non-finite.");
      reserved += std::max(0., remaining);
    }
    if (!finite(reserved)) throw std::runtime_error("Research reservation is non-finite.");
    view.cost_rows = {{tr_at(locale, "ECONOMY_ROW_ADMIN", "COLONY ADMINISTRATION"), rate(currency, -flow.colony_administration_per_day)},
      {tr_at(locale, "ECONOMY_ROW_SERVICES", "POPULATION SERVICES"), rate(currency, -flow.population_services_per_day)},
      {tr_at(locale, "ECONOMY_ROW_HABITAT", "HABITAT SUPPORT"), rate(currency, -flow.habitat_support_per_day)},
      {tr_at(locale, "ECONOMY_ROW_FLEET", "FLEET OPERATIONS"), rate(currency, -flow.fleet_operations_per_day)},
      {tr_at(locale, "ECONOMY_ROW_ORBITAL", "ORBITAL MAINTENANCE"), rate(currency, -flow.orbital_maintenance_per_day)},
      {tr_at(locale, "ECONOMY_ROW_SURFACE", "SURFACE MAINTENANCE"), rate(currency, -flow.surface_maintenance_per_day)},
      {tr_at(locale, "ECONOMY_ROW_RESEARCH", "RESEARCH PROGRAMS"), rate(currency, -flow.research_operations_per_day),
       trf_at(locale, "ECONOMY_ROW_RESERVED", {amount(currency, reserved)}, " · {0} RESERVED")}};
    return view;
  } catch (const EconomyUnavailable& error) {
    auto view = unavailable(error.what(), locale);
    view.player_civilization_id = campaign.player_civilization_id;
    return view;
  } catch (const std::exception& error) {
    auto view = failed(error, locale);
    view.player_civilization_id = campaign.player_civilization_id;
    return view;
  }
}

NativeEconomyController::NativeEconomyController() = default;
NativeEconomyController::NativeEconomyController(Projector projector)
    : projector_(std::move(projector)) {}

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
  try { projected = projector_ ? projector_(campaign, &runtime.research(), last_allocation)
                               : build_economy_view(campaign, &runtime.research(), last_allocation, locale_); }
  catch (const std::exception& error) { projected = failed(error, locale_); }
  if (projected.state == EconomyState::Ready && projected.player_civilization_id != observer)
    projected = failed(std::runtime_error("Economy projection identity does not match the actual player observer."), locale_);
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
  const auto stale = [&] {
    return IndustryPriorityChangeResult{false, tr_at(locale_, "ECONOMY_ERR_STALE",
        "Economy changed; refresh before setting industry priority.")}; };
  if (!valid_priority(priority)) return {false, tr_at(locale_, "ECONOMY_ERR_UNKNOWN_PRIORITY", "Unknown industry priority.")};
  if (!generation_ || !observer_ || *generation_ != generation || view_.state != EconomyState::Ready ||
      view_.revision == 0 || view_.revision != view_revision)
    return stale();
  auto& campaign = frame.runtime().world().campaign();
  if (campaign.player_civilization_id != *observer_) return stale();
  try {
    const auto live = validate(campaign);
    if (live.player.id != *observer_) return stale();
  } catch (const std::exception&) {
    return stale();
  }
  const auto player = std::ranges::find(campaign.civilizations, *observer_, &Civilization::id);
  if (std::ranges::count(campaign.civilizations, *observer_, &Civilization::id) != 1 ||
      player == campaign.civilizations.end() || !player->is_player) return stale();
  if (std::ranges::count(campaign.economies, *observer_, &CivilizationEconomy::civilization_id) != 1)
    return stale();
  const auto economy = std::ranges::find(campaign.economies, *observer_, &CivilizationEconomy::civilization_id);
  if (economy == campaign.economies.end() ||
      economy->industry_priority.value_or(IndustryPriority::Balanced) != view_.industry_priority)
    return stale();
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
