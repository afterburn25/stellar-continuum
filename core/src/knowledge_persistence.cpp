#include <stellar/core/knowledge_persistence.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace stellar::core {
namespace {

[[noreturn]] void null_reference() {
  throw KnowledgePersistenceNullReferenceError(
      "Object reference not set to an instance of an object.");
}

double source_clamp(double value, double minimum, double maximum) noexcept {
  if (std::isnan(value))
    return value;
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

} // namespace

CivilizationKnowledgeState restore_civilization_knowledge(
    const CivilizationKnowledgePersistenceInput &input) {
  CivilizationKnowledgeState knowledge;
  if (!input.entries_present)
    null_reference();

  for (const auto &entry_value : input.entries) {
    if (!entry_value)
      null_reference();
    const auto &entry = *entry_value;
    if (entry.galactic_core_explored && !entry.galactic_core_access_unlocked)
      throw KnowledgePersistenceDataError(
          "Landmark exploration requires its access unlock.");
    if (entry.galactic_core_access_unlocked) {
      try {
        knowledge.unlock_galactic_core_access(entry.civilization_id);
      } catch (const std::out_of_range &error) {
        throw KnowledgePersistenceRangeError(error.what());
      }
    }
    if (entry.galactic_core_explored)
      knowledge.record_galactic_core_exploration(entry.civilization_id);

    if (!entry.system_surveys)
      null_reference();
    if (entry.system_surveys->empty()) {
      if (!entry.known_system_ids)
        null_reference();
      for (const auto system_id : *entry.known_system_ids)
        knowledge.mark_system_fully_surveyed(entry.civilization_id, system_id);
    } else {
      if (!entry.known_system_ids)
        null_reference();
      for (const auto system_id : *entry.known_system_ids)
        knowledge.reveal_system(entry.civilization_id, system_id);

      for (const auto &survey_value : *entry.system_surveys) {
        if (!survey_value)
          null_reference();
        const auto &survey = *survey_value;
        switch (survey.level) {
        case SystemSurveyLevel::unknown:
          break;
        case SystemSurveyLevel::detected:
          knowledge.reveal_system(entry.civilization_id, survey.system_id);
          break;
        case SystemSurveyLevel::partially_surveyed:
          knowledge.advance_system_survey(
              entry.civilization_id, survey.system_id,
              source_clamp(survey.progress, 0.000001, 0.999999));
          break;
        case SystemSurveyLevel::fully_surveyed:
          knowledge.mark_system_fully_surveyed(entry.civilization_id,
                                               survey.system_id);
          break;
        }
      }
    }

    if (!entry.known_civilization_ids)
      null_reference();
    for (const auto civilization_id : *entry.known_civilization_ids)
      knowledge.reveal_civilization(entry.civilization_id, civilization_id);
  }
  return knowledge;
}

CivilizationKnowledgePersistenceInput
capture_civilization_knowledge(const CivilizationKnowledgeState &knowledge) {
  const auto snapshot = knowledge.snapshot();
  std::set<int> observer_ids;
  for (const auto &entry : snapshot.systems)
    observer_ids.insert(entry.observer_id);
  for (const auto &entry : snapshot.civilizations)
    observer_ids.insert(entry.observer_id);
  for (const auto observer_id : knowledge.galactic_core_observers())
    observer_ids.insert(observer_id);

  CivilizationKnowledgePersistenceInput result{true, {}};
  result.entries.reserve(observer_ids.size());
  for (const auto observer_id : observer_ids) {
    std::vector<std::optional<SystemSurveyPersistenceDto>> surveys;
    for (const auto &survey : knowledge.system_survey_knowledge(observer_id)) {
      surveys.emplace_back(SystemSurveyPersistenceDto{
          survey.system_id, survey.level, survey.progress});
    }
    result.entries.emplace_back(CivilizationKnowledgePersistenceDto{
        observer_id,
        knowledge.has_galactic_core_access(observer_id),
        knowledge.is_galactic_core_discovered(observer_id),
        knowledge.known_systems(observer_id),
        knowledge.known_civilizations(observer_id),
        std::move(surveys),
    });
  }
  return result;
}

} // namespace stellar::core
