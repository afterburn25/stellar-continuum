#include "native_surface_construction_controller.hpp"

#include <stellar/core/adaptive_research_capability_adapters.hpp>

#include <ranges>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stellar::native_colony {
namespace {
using namespace stellar::core;

template <class Range, class Member>
auto *find_one(Range &range, const int id, Member member) {
  const auto found = std::ranges::find(range, id, member);
  return found == range.end() ? nullptr : &*found;
}

struct Context {
  IntegratedAdaptiveCampaignRuntime &runtime;
  FreshCampaignState &world;
  const Civilization &player;
  ConstructionWorld command;
};

Context context(CampaignFrame &frame) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto *player = find_one(world.civilizations,
                                world.player_civilization_id,
                                &Civilization::id);
  if (!player || !player->is_player)
    throw std::runtime_error(
        "The campaign has no valid player surface-construction owner.");
  const auto *research = &runtime.research();
  auto capability = [research](const int civilization_id,
                               const std::string_view capability_id) {
    return AdaptiveResearchConstructionCapabilityView(*research)
        .has_civilization_capability(civilization_id, capability_id);
  };
  return {runtime,
          world,
          *player,
          {world.civilizations, world.bodies, world.construction,
           world.colonies, world.economies, {}, std::move(capability)}};
}

const Colony *bound_colony(const Context &current,
                           const NativeColonyView &view,
                           const std::uint64_t generation) {
  if (view.campaign_generation != generation ||
      view.player_civilization_id != current.player.id ||
      !current.world.knowledge.is_system_known(current.player.id,
                                                view.system_id) ||
      current.world.knowledge.system_survey_level(current.player.id,
                                                   view.system_id) <
          SystemSurveyLevel::partially_surveyed)
    return nullptr;
  const auto *colony = find_one(current.world.colonies, view.colony_id,
                                &Colony::id);
  if (!colony || colony->civilization_id != current.player.id ||
      colony->system_id != view.system_id ||
      colony->planetary_body_id != view.body_id)
    return nullptr;
  return colony;
}

bool live_binding(const Context &current, const int player_id,
                  const int system_id, const int body_id,
                  const int colony_id) {
  if (player_id != current.player.id ||
      !current.world.knowledge.is_system_known(player_id, system_id) ||
      current.world.knowledge.system_survey_level(player_id, system_id) <
          SystemSurveyLevel::partially_surveyed)
    return false;
  const auto *colony = find_one(current.world.colonies, colony_id, &Colony::id);
  return colony && colony->civilization_id == player_id &&
         colony->system_id == system_id && colony->planetary_body_id == body_id;
}

NativeSurfaceCommandOutcome stale() {
  return {false,
          "The campaign or surface quote changed; review it before confirming."};
}
} // namespace

void NativeSurfaceConstructionController::require_owner() const {
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error(
        "Native surface construction must run on the simulation owner thread.");
}

void NativeSurfaceConstructionController::bind_generation(
    const std::uint64_t generation) {
  if (generation_ && generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace surface construction.");
  if (!generation_ || *generation_ != generation) {
    generation_ = generation;
    quotes_.clear();
  }
}

bool NativeSurfaceConstructionController::is_current_generation(
    const std::uint64_t generation) const noexcept {
  return generation_ && *generation_ == generation;
}

NativeSurfacePlacementQuote
NativeSurfaceConstructionController::preview_placement(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const std::string_view type_id, const float x,
    const float z, const float rotation_degrees) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  NativeSurfacePlacementQuote result;
  result.campaign_generation = generation;
  result.colony_revision = view.revision;
  if (next_quote_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("No surface quote revision is available.");
  result.quote_revision = next_quote_revision_++;
  result.player_civilization_id = view.player_civilization_id;
  result.system_id = view.system_id;
  result.body_id = view.body_id;
  result.colony_id = view.colony_id;
  auto current = context(frame);
  if (!bound_colony(current, view, generation)) {
    result.message =
        "The owned known settlement changed; refresh it before placement.";
    return result;
  }
  auto assessment = assess_surface_building_placement(
      current.command.read(), current.player.id, view.colony_id, type_id, x, z,
      rotation_degrees);
  result.type_id = assessment.type_id;
  result.building_name = assessment.building_name;
  result.x = assessment.x;
  result.z = assessment.z;
  result.normalized_rotation_degrees = assessment.normalized_rotation_degrees;
  result.prepared_building_id = assessment.prepared_building_id;
  result.authorization_budget_units = assessment.authorization_cost;
  result.industry_cost = assessment.industry_cost;
  result.formatted_authorization = assessment.formatted_authorization;
  result.accepted = assessment.accepted;
  result.message = assessment.message;
  if (assessment.accepted)
    quotes_.emplace(result.quote_revision,
                    PlacementRecord{view.system_id, view.body_id, view.revision,
                                    std::move(assessment)});
  return result;
}

NativeSurfaceRemovalQuote NativeSurfaceConstructionController::preview_removal(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  NativeSurfaceRemovalQuote result;
  result.campaign_generation = generation;
  result.colony_revision = view.revision;
  if (next_quote_revision_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("No surface quote revision is available.");
  result.quote_revision = next_quote_revision_++;
  result.player_civilization_id = view.player_civilization_id;
  result.system_id = view.system_id;
  result.body_id = view.body_id;
  result.colony_id = view.colony_id;
  result.building_id = building_id;
  auto current = context(frame);
  if (!bound_colony(current, view, generation)) {
    result.message =
        "The owned known settlement changed; refresh it before removal.";
    return result;
  }
  auto assessment = assess_surface_building_removal(
      current.command.read(), current.player.id, view.colony_id, building_id);
  result.type_id = assessment.type_id;
  result.building_name = assessment.building_name;
  result.accepted = assessment.accepted;
  result.cancellation = assessment.cancellation;
  result.refund_budget_units = assessment.refund;
  result.formatted_refund = assessment.formatted_refund;
  result.message = assessment.message;
  if (assessment.accepted)
    quotes_.emplace(result.quote_revision,
                    RemovalRecord{view.system_id, view.body_id, view.revision,
                                  std::move(assessment)});
  return result;
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::confirm_placement(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeSurfacePlacementQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != generation ||
      quote.campaign_generation != generation)
    return stale();
  const auto found = quotes_.find(quote.quote_revision);
  if (found == quotes_.end() ||
      !std::holds_alternative<PlacementRecord>(found->second))
    return stale();
  const auto record = std::get<PlacementRecord>(found->second);
  quotes_.erase(found); // confirmation tokens are single-use, including denial.
  const auto &assessment = record.assessment;
  if (!quote.accepted || quote.player_civilization_id != assessment.civilization_id ||
      quote.colony_revision != record.colony_revision ||
      quote.colony_id != assessment.colony_id || quote.system_id != record.system_id ||
      quote.body_id != record.body_id || quote.type_id != assessment.type_id ||
      quote.building_name != assessment.building_name || quote.x != assessment.x ||
      quote.z != assessment.z ||
      quote.normalized_rotation_degrees != assessment.normalized_rotation_degrees ||
      quote.prepared_building_id != assessment.prepared_building_id ||
      quote.authorization_budget_units != assessment.authorization_cost ||
      quote.industry_cost != assessment.industry_cost ||
      quote.formatted_authorization != assessment.formatted_authorization ||
      quote.message != assessment.message)
    return stale();
  auto current = context(frame);
  if (!live_binding(current, assessment.civilization_id, record.system_id,
                    record.body_id, assessment.colony_id))
    return stale();
  const auto result =
      commit_surface_building_placement(current.command, assessment);
  return {result.accepted, result.message};
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::confirm_removal(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeSurfaceRemovalQuote &quote) {
  require_owner();
  if (!generation_ || *generation_ != generation ||
      quote.campaign_generation != generation)
    return stale();
  const auto found = quotes_.find(quote.quote_revision);
  if (found == quotes_.end() ||
      !std::holds_alternative<RemovalRecord>(found->second))
    return stale();
  const auto record = std::get<RemovalRecord>(found->second);
  quotes_.erase(found);
  const auto &assessment = record.assessment;
  if (!quote.accepted || quote.player_civilization_id != assessment.civilization_id ||
      quote.colony_revision != record.colony_revision ||
      quote.colony_id != assessment.colony_id || quote.system_id != record.system_id ||
      quote.body_id != record.body_id || quote.building_id != assessment.building_id ||
      quote.type_id != assessment.type_id ||
      quote.building_name != assessment.building_name ||
      quote.cancellation != assessment.cancellation ||
      quote.refund_budget_units != assessment.refund ||
      quote.formatted_refund != assessment.formatted_refund ||
      quote.message != assessment.message)
    return stale();
  auto current = context(frame);
  if (!live_binding(current, assessment.civilization_id, record.system_id,
                    record.body_id, assessment.colony_id))
    return stale();
  const auto result = commit_surface_building_removal(current.command, assessment);
  return {result.accepted, result.message};
}

namespace {
template <class Order>
NativeSurfaceCommandOutcome run_order(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, Order &&order) {
  auto current = context(frame);
  if (!bound_colony(current, view, generation))
    return {false,
            "The owned known settlement changed; refresh it before ordering."};
  const auto result = order(current);
  return {result.accepted, result.message};
}
} // namespace

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::upgrade_building(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  return run_order(frame, generation, view, [&](const Context &current) {
    return upgrade_surface_building(current.command, current.player.id,
                                    view.colony_id, building_id);
  });
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::repair_building(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  return run_order(frame, generation, view, [&](const Context &current) {
    return repair_surface_building(current.command, current.player.id,
                                   view.colony_id, building_id);
  });
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::set_building_enabled(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id, const bool enabled) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  return run_order(frame, generation, view, [&](const Context &current) {
    return set_surface_building_enabled(current.command, current.player.id,
                                        view.colony_id, building_id, enabled);
  });
}

NativeSurfaceCommandOutcome
NativeSurfaceConstructionController::set_building_priority(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view, const int building_id,
    const bool prioritized) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  return run_order(frame, generation, view, [&](const Context &current) {
    return set_surface_building_priority(current.command, current.player.id,
                                         view.colony_id, building_id,
                                         prioritized);
  });
}

NativeSurfaceCommandOutcome NativeSurfaceConstructionController::upgrade_hub(
    CampaignFrame &frame, const std::uint64_t generation,
    const NativeColonyView &view) {
  require_owner();
  bind_generation(generation);
  quotes_.clear();
  return run_order(frame, generation, view, [&](const Context &current) {
    return upgrade_surface_hub(current.command, current.player.id,
                               view.colony_id);
  });
}

bool NativeSurfaceConstructionController::cancel_quote(
    const std::uint64_t generation, const std::uint64_t quote_revision) {
  require_owner();
  if (!generation_ || *generation_ != generation) return false;
  return quotes_.erase(quote_revision) == 1;
}

} // namespace stellar::native_colony
