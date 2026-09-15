#include <stellar/core/legacy_research.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace stellar::core {
namespace {

double max_preserving_nan(double first, double second) {
  return std::isnan(first) || std::isnan(second)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(first, second);
}

double min_preserving_nan(double first, double second) {
  return std::isnan(first) || std::isnan(second)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(first, second);
}

template <class T, class Predicate>
T &first(std::span<T> values, Predicate predicate) {
  const auto found = std::find_if(values.begin(), values.end(), predicate);
  if (found == values.end())
    throw std::runtime_error("Sequence contains no matching element");
  return *found;
}

template <class T, class Predicate>
const T &first(std::span<const T> values, Predicate predicate) {
  const auto found = std::find_if(values.begin(), values.end(), predicate);
  if (found == values.end())
    throw std::runtime_error("Sequence contains no matching element");
  return *found;
}

double score(const TechnologyDefinition &definition,
             const Civilization &civilization) {
  const auto &traits = civilization.traits;
  switch (definition.category) {
  case TechnologyCategory::Industry:
    return 1.0 + traits.greed * 0.50 + traits.territoriality * 0.20;
  case TechnologyCategory::Propulsion:
    return 1.0 + traits.aggression * 0.35 + traits.risk_tolerance * 0.20;
  case TechnologyCategory::Sensors:
    return 1.0 + traits.scientific_curiosity * 0.55 +
           (1.0 - traits.risk_tolerance) * 0.15;
  case TechnologyCategory::Physics:
    return 1.0 + traits.scientific_curiosity * 0.80;
  case TechnologyCategory::Ftl:
    return 1.2 + traits.scientific_curiosity * 0.40 +
           traits.aggression * 0.20 + traits.territoriality * 0.20;
  }
  return 1.0;
}

const TechnologyDefinition *select_ai_research(
    const Civilization &civilization, const TechnologyState &state,
    const ConstructionState &construction) {
  auto available = available_legacy_technologies(state, construction);
  std::stable_sort(available.begin(), available.end(),
                   [&](const auto &left, const auto &right) {
                     const auto left_score = score(left, civilization);
                     const auto right_score = score(right, civilization);
                     const auto left_nan = std::isnan(left_score);
                     const auto right_nan = std::isnan(right_score);
                     if (left_nan != right_nan)
                       return !left_nan;
                     if (!left_nan && left_score != right_score)
                       return left_score > right_score;
                     return left.research_cost < right.research_cost;
                   });
  if (available.empty())
    return nullptr;
  return &get_legacy_technology(available.front().id);
}

void replace_first_civilization(std::span<Civilization> civilizations,
                                const Civilization &replacement) {
  const auto found = std::find_if(
      civilizations.begin(), civilizations.end(), [&](const auto &value) {
        return value.id == replacement.id;
      });
  if (found != civilizations.end())
    *found = replacement;
}

} // namespace

std::vector<LegacyResearchEvent>
LegacyResearchSimulation::advance(LegacyResearchWorldView world) const {
  return advance_core(world, std::nullopt);
}

std::vector<LegacyResearchEvent> LegacyResearchSimulation::advance_for_civilization(
    LegacyResearchWorldView world, int civilization_id) const {
  return advance_core(world, civilization_id);
}

std::vector<LegacyResearchEvent> LegacyResearchSimulation::advance_core(
    LegacyResearchWorldView world,
    std::optional<int> only_civilization_id) const {
  std::vector<LegacyResearchEvent> events;
  const std::vector<Civilization> snapshot(world.civilizations.begin(),
                                           world.civilizations.end());
  for (const auto &civilization : snapshot) {
    if (only_civilization_id && civilization.id != *only_civilization_id)
      continue;
    if (civilization.development_stage ==
        CivilizationDevelopmentStage::AncientSpacefaring)
      continue;

    auto &state = first<TechnologyState>(
        world.technologies,
        [&](const auto &value) { return value.civilization_id == civilization.id; });
    const auto &construction = first<ConstructionState>(
        world.construction,
        [&](const auto &value) { return value.civilization_id == civilization.id; });
    auto &economy = first<CivilizationEconomy>(
        world.economies,
        [&](const auto &value) { return value.civilization_id == civilization.id; });

    if (!state.active_research_id && !civilization.is_player) {
      const auto *selection =
          select_ai_research(civilization, state, construction);
      if (selection)
        state.active_research_id = selection->id;
    }
    if (!state.active_research_id)
      continue;

    const auto &definition =
        get_legacy_technology(*state.active_research_id);
    const auto remaining = max_preserving_nan(
        0.0, definition.research_cost - state.active_research_progress);
    const auto spend = min_preserving_nan(
        remaining, max_preserving_nan(0.0, economy.science));
    economy.science -= spend;
    state.active_research_progress += spend;
    if (state.active_research_progress + 0.0001 < definition.research_cost)
      continue;

    state.completed_technology_ids.insert(definition.id);
    state.active_research_id.reset();
    state.active_research_progress = 0.0;
    events.push_back({civilization.id, definition.id,
                      civilization.name + " completed " + definition.name + "."});
    if (definition.id == "prototype_warp_drive" &&
        civilization.development_stage ==
            CivilizationDevelopmentStage::PreWarp) {
      auto replacement = civilization;
      replacement.development_stage =
          CivilizationDevelopmentStage::WarpCapable;
      replace_first_civilization(world.civilizations, replacement);
      events.push_back(
          {civilization.id, definition.id,
           civilization.name +
               " has become warp-capable. Interstellar ship designs are now "
               "available, but vessels must be constructed in an Orbital "
               "Shipyard."});
    }
  }
  return events;
}

LegacyResearchOrderResult LegacyResearchSimulation::start_research(
    LegacyResearchWorldView world, int civilization_id,
    std::optional<std::string_view> technology_id) const {
  const auto civilization = std::find_if(
      world.civilizations.begin(), world.civilizations.end(),
      [&](const auto &value) { return value.id == civilization_id; });
  if (civilization == world.civilizations.end())
    return {false, "Unknown civilization."};
  if (civilization->development_stage ==
      CivilizationDevelopmentStage::AncientSpacefaring)
    return {false,
            "This civilization has already mastered interstellar flight."};

  auto &state = first<TechnologyState>(
      world.technologies,
      [&](const auto &value) { return value.civilization_id == civilization_id; });
  if (state.active_research_id)
    return {false, "Research is already in progress."};
  const auto &construction = first<ConstructionState>(
      world.construction,
      [&](const auto &value) { return value.civilization_id == civilization_id; });
  const auto available = available_legacy_technologies(state, construction);
  const auto definition = std::find_if(
      available.begin(), available.end(),
      [&](const auto &value) {
        return technology_id && value.id == *technology_id;
      });
  if (definition == available.end())
    return {false,
            "That technology is not currently available; a prerequisite "
            "technology or project may still be missing."};
  state.active_research_id = definition->id;
  state.active_research_progress = 0.0;
  return {true, "Research started: " + definition->name + "."};
}

} // namespace stellar::core
