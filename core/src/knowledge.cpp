#include <stellar/core/knowledge.hpp>

#include <stellar/core/interstellar_distance.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace stellar::core {
namespace {
double source_max(double first, double second) {
  return std::isnan(first) || std::isnan(second)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(first, second);
}
} // namespace

bool CivilizationKnowledgeState::has_galactic_core_access(int id) const {
  return core_access_.contains(id);
}

bool CivilizationKnowledgeState::is_galactic_core_discovered(int id) const {
  return has_galactic_core_access(id) && core_explored_.contains(id);
}

std::vector<int> CivilizationKnowledgeState::galactic_core_observers() const {
  return {core_access_.begin(), core_access_.end()};
}

void CivilizationKnowledgeState::unlock_galactic_core_access(int id) {
  if (id < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'civilizationId')");
  core_access_.insert(id);
}

bool CivilizationKnowledgeState::record_galactic_core_exploration(int id) {
  return has_galactic_core_access(id) && core_explored_.insert(id).second;
}

CivilizationKnowledgeState::Survey &
CivilizationKnowledgeState::ensure_survey(int civilization_id, int system_id) {
  return surveys_[civilization_id].try_emplace(system_id).first->second;
}

bool CivilizationKnowledgeState::is_system_known(int civilization_id,
                                                  int system_id) const {
  const auto found = systems_.find(civilization_id);
  return found != systems_.end() && found->second.contains(system_id);
}

SystemSurveyLevel CivilizationKnowledgeState::system_survey_level(
    int civilization_id, int system_id) const {
  const auto found = surveys_.find(civilization_id);
  if (found == surveys_.end() || !found->second.contains(system_id))
    return SystemSurveyLevel::unknown;
  return found->second.at(system_id).level;
}

double CivilizationKnowledgeState::system_survey_progress(
    int civilization_id, int system_id) const {
  const auto found = surveys_.find(civilization_id);
  return found == surveys_.end() || !found->second.contains(system_id)
             ? 0.0
             : found->second.at(system_id).progress;
}

bool CivilizationKnowledgeState::is_system_fully_surveyed(
    int civilization_id, int system_id) const {
  return system_survey_level(civilization_id, system_id) ==
         SystemSurveyLevel::fully_surveyed;
}

std::vector<SystemSurveyKnowledgeView>
CivilizationKnowledgeState::system_survey_knowledge(
    int civilization_id) const {
  std::vector<SystemSurveyKnowledgeView> result;
  const auto found = surveys_.find(civilization_id);
  if (found != surveys_.end())
    for (const auto &[id, value] : found->second)
      result.push_back({id, value.level, value.progress});
  return result;
}

std::vector<int>
CivilizationKnowledgeState::known_systems(int civilization_id) const {
  const auto found = systems_.find(civilization_id);
  return found == systems_.end()
             ? std::vector<int>{}
             : std::vector<int>{found->second.begin(), found->second.end()};
}

std::vector<int>
CivilizationKnowledgeState::known_civilizations(int civilization_id) const {
  const auto found = civilizations_.find(civilization_id);
  return found == civilizations_.end()
             ? std::vector<int>{}
             : std::vector<int>{found->second.begin(), found->second.end()};
}

bool CivilizationKnowledgeState::is_civilization_known(int observer_id,
                                                        int target_id) const {
  return observer_id == target_id ||
         (civilizations_.contains(observer_id) &&
          civilizations_.at(observer_id).contains(target_id));
}

bool CivilizationKnowledgeState::reveal_system(int civilization_id,
                                                int system_id) {
  ensure_survey(civilization_id, system_id);
  auto found = systems_.find(civilization_id);
  if (found == systems_.end()) {
    system_observer_order_.push_back(civilization_id);
    found = systems_.emplace(civilization_id, std::set<int>{}).first;
  }
  return found->second.insert(system_id).second;
}

bool CivilizationKnowledgeState::record_reconnaissance(int civilization_id,
                                                        int system_id,
                                                        double progress_floor) {
  reveal_system(civilization_id, system_id);
  auto &knowledge = ensure_survey(civilization_id, system_id);
  if (knowledge.level == SystemSurveyLevel::fully_surveyed)
    return false;
  const auto old_level = knowledge.level;
  const auto old_progress = knowledge.progress;
  knowledge.progress =
      std::clamp(source_max(knowledge.progress, progress_floor), 0.0, 0.999999);
  knowledge.level = SystemSurveyLevel::partially_surveyed;
  return knowledge.level != old_level ||
         std::abs(knowledge.progress - old_progress) > 0.0000001;
}

bool CivilizationKnowledgeState::advance_system_survey(int civilization_id,
                                                        int system_id,
                                                        double progress_delta) {
  if (progress_delta <= 0.0)
    return false;
  reveal_system(civilization_id, system_id);
  auto &knowledge = ensure_survey(civilization_id, system_id);
  if (knowledge.level == SystemSurveyLevel::fully_surveyed)
    return false;
  const bool was_fully_surveyed =
      knowledge.level == SystemSurveyLevel::fully_surveyed;
  knowledge.progress =
      std::clamp(knowledge.progress + progress_delta, 0.0, 1.0);
  knowledge.level = knowledge.progress >= 1.0
                        ? SystemSurveyLevel::fully_surveyed
                        : SystemSurveyLevel::partially_surveyed;
  return !was_fully_surveyed &&
         knowledge.level == SystemSurveyLevel::fully_surveyed;
}

bool CivilizationKnowledgeState::mark_system_fully_surveyed(
    int civilization_id, int system_id) {
  reveal_system(civilization_id, system_id);
  auto &knowledge = ensure_survey(civilization_id, system_id);
  const bool changed = knowledge.level != SystemSurveyLevel::fully_surveyed ||
                       knowledge.progress < 1.0;
  knowledge = {SystemSurveyLevel::fully_surveyed, 1.0};
  return changed;
}

bool CivilizationKnowledgeState::reveal_civilization(int observer_id,
                                                      int target_id) {
  if (observer_id == target_id)
    return false;
  auto found = civilizations_.find(observer_id);
  if (found == civilizations_.end()) {
    civilization_observer_order_.push_back(observer_id);
    found = civilizations_.emplace(observer_id, std::set<int>{}).first;
  }
  return found->second.insert(target_id).second;
}

int CivilizationKnowledgeState::reveal_within_sensor_range(
    int civilization_id, int origin_system_id,
    std::span<const StellarSystem> systems, float range) {
  const auto origin =
      std::find_if(systems.begin(), systems.end(), [&](const auto &system) {
        return system.id == origin_system_id;
      });
  if (origin == systems.end())
    throw std::runtime_error("Unknown sensor origin system " +
                             std::to_string(origin_system_id) + ".");
  const double range_squared = static_cast<double>(range) * range;
  int revealed = 0;
  for (const auto &system : systems)
    if (squared_distance_light_years(origin->position, system.position) <=
            range_squared &&
        reveal_system(civilization_id, system.id))
      ++revealed;
  return revealed;
}

KnowledgeSnapshot CivilizationKnowledgeState::snapshot() const {
  KnowledgeSnapshot result;
  for (const int observer : system_observer_order_)
    result.systems.push_back({observer, known_systems(observer)});
  for (const int observer : civilization_observer_order_)
    result.civilizations.push_back({observer, known_civilizations(observer)});
  return result;
}

CivilizationKnowledgeState CivilizationKnowledgeState::create_initial(
    std::span<const StellarSystem> systems,
    std::span<const Civilization> civilizations, float sensor_range) {
  CivilizationKnowledgeState result;
  for (const auto &civilization : civilizations) {
    result.mark_system_fully_surveyed(civilization.id,
                                      civilization.home_system_id);
    result.reveal_within_sensor_range(civilization.id,
                                      civilization.home_system_id, systems,
                                      sensor_range);
  }
  return result;
}
} // namespace stellar::core
