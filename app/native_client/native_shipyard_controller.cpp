#include "native_shipyard_controller.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>
#include <stellar/core/ship_designs.hpp>
#include <stellar/core/shipbuilding.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace stellar::native_shipyard {
namespace {
using namespace stellar::core;

struct Projection {
  NativeShipyardView view;
  std::string player_species_id;
  std::string signature;
};

template <class Range, class ProjectionMember>
[[nodiscard]] auto *find_one(Range &range, int id,
                             ProjectionMember member) {
  const auto found = std::ranges::find(range, id, member);
  return found == range.end() ? nullptr : &*found;
}

void append(std::ostringstream &out, std::string_view value) {
  out << value.size() << ':' << value << ';';
}

std::string resolve(const stellar::engine::LocalizationTable *locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string resolved(const stellar::engine::LocalizationTable *locale,
                     std::string_view key,
                     std::initializer_list<std::string> args,
                     std::string_view fallback) {
  if (locale && locale->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale->format(key, std::span<const std::string>(values));
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

[[nodiscard]] Projection project(CampaignFrame &frame,
                                 std::uint64_t generation,
                                 const stellar::engine::LocalizationTable *locale) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto *player = find_one(world.civilizations,
                                world.player_civilization_id,
                                &Civilization::id);
  if (!player || !player->is_player)
    throw std::runtime_error(
        "The campaign has no valid player civilization.");
  const auto *yard = find_one(world.shipyards, player->id,
                              &ShipyardState::civilization_id);
  const auto *economy = find_one(world.economies, player->id,
                                 &CivilizationEconomy::civilization_id);
  const auto *construction = find_one(
      world.construction, player->id, &ConstructionState::civilization_id);
  if (!yard || !economy || !construction)
    throw std::runtime_error(
        "The player campaign has no complete shipyard economy state.");

  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  auto query = [&capability](int civilization_id, std::string_view id) {
    return capability.has_civilization_capability(civilization_id, id);
  };
  const ShipbuildingReadView read{
      world.civilizations, world.systems, world.construction, world.shipyards,
      world.colonies,      world.economies, world.fleets,       {},
      {},                  query,           {}};

  Projection result;
  auto &view = result.view;
  result.player_species_id = player->species_id;
  view.campaign_generation = generation;
  view.player_civilization_id = player->id;
  view.home_system_id = player->home_system_id;
  const auto home=std::ranges::find(world.systems,player->home_system_id,&StellarSystem::id);
  view.yard_name=resolved(locale,"SHIPYARD_YARD_NAME",
      {home==world.systems.end()?std::string("Home"):home->name},
      "{0} Orbital Shipyard");
  view.orbital_shipyard_complete = std::ranges::contains(
      construction->completed_project_ids, std::string("orbital_shipyard"));
  view.currency = sovereign_currency_for_civilization(world.civilizations,
                                                       player->id);
  view.treasury_credits = economy->credits;
  view.formatted_treasury = view.currency.format(economy->credits);
  view.available_industry = economy->industry;
  view.maximum_pending_builds = maximum_pending_ship_builds;
  view.pending_build_count = yard->pending_build_count();
  for (const auto &colony : world.colonies)
    if (colony.civilization_id == player->id &&
        (!std::isfinite(view.largest_owned_colony_population_millions) ||
         colony.population_millions >
             view.largest_owned_colony_population_millions))
      view.largest_owned_colony_population_millions =
          colony.population_millions;

  for (const auto &design : available_ship_designs(read.designs(), player->id)) {
    const auto propulsion =
        effective_ship_propulsion(read.designs(), player->id, design);
    const auto readiness =
        assess_start_ship_build(read, player->id, design.id);
    view.available_designs.push_back(
        {.id = design.id,
         .name = design.name,
         .description = design.description,
         .role = design.role,
         .industry_cost = design.industry_cost,
         .minimum_build_days_at_full_shipyard_rate =
             design.industry_cost / shipbuilding_industry_per_day,
         .credit_cost = design.credit_cost,
         .population_cost_millions = design.population_cost_millions,
         .strategic_speed = propulsion.strategic_speed,
         .maximum_leg_range_light_years =
             propulsion.maximum_leg_range_light_years,
         .fuel_endurance_light_years = propulsion.fuel_endurance_light_years,
         .sensor_range = design.sensor_range,
         .propulsion_generation = propulsion.propulsion_generation,
         .formatted_credit_cost = view.currency.format(design.credit_cost),
         .can_start = readiness.can_start,
         .will_queue = readiness.will_queue,
         .start_blocker = readiness.blocker,
         .minimum_source_population_millions =
             readiness.minimum_source_population_millions,
         .population_source_colony_id =
             readiness.population_source_colony_id,
         .population_species_id = readiness.population_species_id,
         .population_source_current_millions =
             readiness.population_source_current_millions});
    auto& projected=view.available_designs.back();
    const auto& combat=get_combat_profile(design.combat_profile_id.value_or(std::string(default_combat_profile_id(design.role))));
    projected.hull=combat.max_hull;projected.armor=combat.max_armor;projected.shields=combat.max_shields;projected.weapon_damage=combat.weapon_damage;projected.weapon_interval_days=combat.weapon_interval_days;
    projected.cargo_capacity=design.cargo_material_capacity;projected.crew=design.crew_complement_individuals;
    projected.batch_quotes=assess_ship_build_batches(read,player->id,design.id);
  }

  auto add_order = [&](std::string order_id, std::string design_id,
                       bool active, double progress, double authorization,
                       double population,
                       std::optional<std::string> population_species,
                       std::optional<int> source) {
    const auto *design = find_ship_design(design_id);
    const auto cancellation = assess_ship_build_cancellation(
        read, player->id, order_id);
    const auto industry_cost = design ? design->industry_cost : 0.;
    view.orders.push_back(
        {.order_id = std::move(order_id),
         .design_id = std::move(design_id),
         .design_name = design ? design->name : resolve(locale,"SHIPYARD_DESIGN_UNAVAILABLE","Unavailable design"),
         .active = active,
         .progress_fraction = industry_cost <= 0.
                                  ? (active ? 1. : 0.)
                                  : std::clamp(progress / industry_cost, 0., 1.),
         .industry_progress = progress,
         .industry_remaining = std::max(0., industry_cost - progress),
         .authorization_credits = authorization,
         .reserved_population_millions = population,
         .reserved_population_species_id = std::move(population_species),
         .reserved_population_source_colony_id = source,
         .can_cancel = cancellation.can_cancel,
         .refund_credits = cancellation.refund_credits,
         .formatted_refund = view.currency.format(cancellation.refund_credits),
         .cancellation_blocker = cancellation.blocker});
  };
  if (yard->active_design_id)
    add_order(yard->active_order_id.value_or(""), *yard->active_design_id,
              true, yard->active_build_progress,
              yard->active_authorization_credits,
              yard->reserved_population_millions,
              yard->reserved_population_species_id,
              yard->reserved_population_source_colony_id);
  for (const auto &queued : yard->queued_builds)
    add_order(queued.order_id, queued.design_id, false, 0.,
              queued.authorization_credits,
              queued.reserved_population_millions,
              queued.reserved_population_species_id,
              queued.reserved_population_source_colony_id);

  std::ostringstream signature;
  signature.imbue(std::locale::classic());
  signature << generation << ';' << player->id << ';'
            << player->home_system_id << ';'
            << view.orbital_shipyard_complete << ';' << std::hexfloat
            << economy->credits << ';' << economy->industry << ';'
            << view.largest_owned_colony_population_millions << ';'
            << yard->next_order_sequence << ';' << yard->active_build_progress
            << ';' << yard->active_authorization_credits << ';'
            << yard->reserved_population_millions << ';';
  append(signature, player->species_id);
  append(signature, view.currency.name);
  append(signature, view.currency.code);
  append(signature, view.currency.symbol);
  signature << view.currency.local_units_per_budget_unit << ';';
  append(signature, yard->active_design_id.value_or(""));
  append(signature, yard->active_order_id.value_or(""));
  for (const auto &id : construction->completed_project_ids)
    append(signature, id);
  for (const auto &colony : world.colonies)
    if (colony.civilization_id == player->id) {
      signature << colony.id << ';' << colony.population_millions << ';';
      append(signature, colony.population_species_id);
    }
  for (const auto &design : view.available_designs) {
    append(signature, design.id);
    signature << design.strategic_speed << ';'
              << design.maximum_leg_range_light_years << ';'
              << design.fuel_endurance_light_years << ';'
              << design.can_start << ';' << design.will_queue << ';'
              << design.minimum_source_population_millions << ';';
    append(signature, design.start_blocker.value_or(""));
    if (design.population_source_colony_id)
      signature << *design.population_source_colony_id;
    signature << ';';
    append(signature, design.population_species_id.value_or(""));
    if (design.population_source_current_millions)
      signature << *design.population_source_current_millions;
    signature << ';';
  }
  for (const auto &order : view.orders) {
    append(signature, order.order_id);
    append(signature, order.design_id);
    signature << order.active << ';' << order.industry_progress << ';'
              << order.authorization_credits << ';'
              << order.reserved_population_millions << ';';
    append(signature, order.reserved_population_species_id.value_or(""));
    if (order.reserved_population_source_colony_id)
      signature << *order.reserved_population_source_colony_id;
    signature << ';' << order.can_cancel << ';' << order.refund_credits << ';';
    append(signature, order.cancellation_blocker.value_or(""));
  }
  result.signature = signature.str();
  return result;
}

[[nodiscard]] bool currency_matches(
    const SovereignCurrencyDefinition &left,
    const SovereignCurrencyDefinition &right) noexcept {
  return left.name == right.name && left.code == right.code &&
         left.symbol == right.symbol &&
         left.local_units_per_budget_unit == right.local_units_per_budget_unit;
}

[[nodiscard]] bool same_authorization_terms(
    const NativeShipDesign &left, const NativeShipDesign &right) noexcept {
  return left.id == right.id && left.industry_cost == right.industry_cost &&
         left.credit_cost == right.credit_cost &&
         left.population_cost_millions == right.population_cost_millions &&
         left.minimum_source_population_millions ==
             right.minimum_source_population_millions;
}

[[nodiscard]] bool same_admission_selection(
    const NativeShipDesign &left, const NativeShipDesign &right) noexcept {
  return left.will_queue == right.will_queue &&
         left.population_source_colony_id == right.population_source_colony_id &&
         left.population_species_id == right.population_species_id;
}

[[nodiscard]] bool valid_bound_command(
    std::uint64_t generation, std::uint64_t expected_revision,
    std::optional<std::uint64_t> bound_generation, std::uint64_t revision,
    const std::optional<NativeShipyardView> &projected_view) noexcept {
  if (!bound_generation || *bound_generation != generation ||
      expected_revision != revision || !projected_view)
    return false;
  return true;
}

} // namespace

void NativeShipyardController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native shipyard control must run on the simulation owner thread.");
}

std::string NativeShipyardController::tr(std::string_view key,
                                         std::string_view fallback) const {
  return resolve(locale_, key, fallback);
}

NativeShipyardView NativeShipyardController::build(
    CampaignFrame &frame, const std::uint64_t campaign_generation) {
  require_owner();
  if (generation_ && campaign_generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace the current shipyard view.");
  if (!generation_ || *generation_ != campaign_generation) {
    generation_ = campaign_generation;
    revision_ = 0;
    signature_.reset();
    projected_player_species_id_.reset();
    projected_view_.reset();
  }
  auto current = project(frame, campaign_generation, locale_);
  if (!signature_ || *signature_ != current.signature) {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Native shipyard revision space is exhausted.");
    ++revision_;
    signature_ = current.signature;
  }
  current.view.shipyard_revision = revision_;
  projected_player_species_id_ = current.player_species_id;
  projected_view_ = current.view;
  return current.view;
}

NativeShipyardCommandOutcome NativeShipyardController::start(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::uint64_t expected_shipyard_revision,
    const std::string_view design_id,int quantity) {
  require_owner();
  if (!valid_bound_command(campaign_generation, expected_shipyard_revision,
                           generation_, revision_, projected_view_))
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  const auto current = project(frame, campaign_generation, locale_);
  const auto expected = std::ranges::find(
      projected_view_->available_designs, design_id, &NativeShipDesign::id);
  const auto design = std::ranges::find(
      current.view.available_designs, design_id, &NativeShipDesign::id);
  if (expected == projected_view_->available_designs.end() ||
      design == current.view.available_designs.end() ||
      projected_view_->player_civilization_id !=
          current.view.player_civilization_id ||
      projected_view_->home_system_id != current.view.home_system_id ||
      projected_view_->orbital_shipyard_complete !=
          current.view.orbital_shipyard_complete ||
      !projected_player_species_id_ ||
      *projected_player_species_id_ != current.player_species_id ||
      !currency_matches(projected_view_->currency, current.view.currency) ||
      !same_authorization_terms(*expected, *design))
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  if (!design->can_start)
    return {false, design->start_blocker.value(), 0.};
  if (!same_admission_selection(*expected, *design))
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  auto query = [&capability](int civilization_id, std::string_view id) {
    return capability.has_civilization_capability(civilization_id, id);
  };
  ShipbuildingWorld command{
      world.civilizations, world.systems, world.construction,
      world.shipyards,     world.colonies, world.economies,
      world.fleets,        {},             {},
      query,               {}};
  const auto result = start_ship_build_batch(
      command, current.view.player_civilization_id, design_id,quantity);
  if (result.accepted) {
    signature_.reset();
    projected_player_species_id_.reset();
    projected_view_.reset();
  }
  return {result.accepted, result.message, 0.};
}

NativeShipyardCommandOutcome NativeShipyardController::cancel(
    CampaignFrame &frame, const std::uint64_t campaign_generation,
    const std::uint64_t expected_shipyard_revision,
    const std::string_view order_id) {
  require_owner();
  if (!valid_bound_command(campaign_generation, expected_shipyard_revision,
                           generation_, revision_, projected_view_))
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  const auto current = project(frame, campaign_generation, locale_);
  if (projected_view_->player_civilization_id !=
          current.view.player_civilization_id ||
      projected_view_->home_system_id != current.view.home_system_id)
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  const auto order = std::ranges::find(
      current.view.orders, order_id, &NativeShipyardOrder::order_id);
  if (order == current.view.orders.end())
    return {false, tr("SHIPYARD_MSG_ORDER_GONE", "That shipyard order is no longer pending."), 0.};
  if (!signature_ || current.signature != *signature_)
    return {false, tr("SHIPYARD_MSG_CHANGED", "The shipyard changed; refresh it before issuing an order."),
            0.};
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  AdaptiveResearchShipbuildingCapabilityView capability(runtime.research());
  auto query = [&capability](int civilization_id, std::string_view id) {
    return capability.has_civilization_capability(civilization_id, id);
  };
  ShipbuildingWorld command{
      world.civilizations, world.systems, world.construction,
      world.shipyards,     world.colonies, world.economies,
      world.fleets,        {},             {},
      query,               {}};
  const auto result = cancel_ship_build(
      command, current.view.player_civilization_id, order_id);
  if (result.accepted) {
    signature_.reset();
    projected_player_species_id_.reset();
    projected_view_.reset();
  }
  return {result.accepted, result.message, result.refunded_credits};
}

NativeShipyardCommandOutcome NativeShipyardController::reorder(CampaignFrame& frame,std::uint64_t generation,std::uint64_t revision,std::string_view id,int direction){
  require_owner();if(!valid_bound_command(generation,revision,generation_,revision_,projected_view_))return {false,tr("SHIPYARD_MSG_QUEUE_CHANGED","The queue changed; review it again.")};
  const auto current=project(frame,generation,locale_);
  if(current.view.player_civilization_id!=projected_view_->player_civilization_id||current.view.home_system_id!=projected_view_->home_system_id||
      !std::ranges::equal(current.view.orders,projected_view_->orders,{},&NativeShipyardOrder::order_id,&NativeShipyardOrder::order_id))return {false,tr("SHIPYARD_MSG_QUEUE_CHANGED","The queue changed; review it again.")};
  auto& world=frame.runtime().world().campaign();
  ShipbuildingWorld command{world.civilizations,world.systems,world.construction,world.shipyards,world.colonies,world.economies,world.fleets,{},{},{},{}};
  const auto result=move_queued_ship_build(command,current.view.player_civilization_id,id,direction);
  if(result.accepted){signature_.reset();projected_view_.reset();projected_player_species_id_.reset();}
  return {result.accepted,result.message};
}
} // namespace stellar::native_shipyard
