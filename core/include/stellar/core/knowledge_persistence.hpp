#pragma once

#include <optional>
#include <stdexcept>
#include <vector>

#include <stellar/core/knowledge.hpp>

namespace stellar::core {

class KnowledgePersistenceDataError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class KnowledgePersistenceNullReferenceError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class KnowledgePersistenceRangeError : public std::out_of_range {
public:
  using std::out_of_range::out_of_range;
};

struct SystemSurveyPersistenceDto {
  int system_id{};
  SystemSurveyLevel level{SystemSurveyLevel::unknown};
  double progress{};
};

struct CivilizationKnowledgePersistenceDto {
  int civilization_id{};
  bool galactic_core_access_unlocked{};
  bool galactic_core_explored{};

  // These collections are initialized by authored DTOs but remain nullable at
  // the parser boundary. Survey elements are likewise nullable in malformed
  // serialized input despite the source annotation.
  std::optional<std::vector<int>> known_system_ids;
  std::optional<std::vector<int>> known_civilization_ids;
  std::optional<std::vector<std::optional<SystemSurveyPersistenceDto>>>
      system_surveys;
};

struct CivilizationKnowledgePersistenceInput {
  bool entries_present{};
  std::vector<std::optional<CivilizationKnowledgePersistenceDto>> entries;
};

// Creates a fresh state and replays the source public knowledge operations in
// DTO order. The input is owned by its caller and remains unchanged.
[[nodiscard]] CivilizationKnowledgeState restore_civilization_knowledge(
    const CivilizationKnowledgePersistenceInput &input);

// Projects a detached, always-present DTO list. Observer IDs and survey/system
// values follow the exact ordering of CampaignSaveService.ToKnowledgeDtos.
[[nodiscard]] CivilizationKnowledgePersistenceInput
capture_civilization_knowledge(const CivilizationKnowledgeState &knowledge);

} // namespace stellar::core
