#pragma once

#include <stellar/core/civilization_catalog.hpp>

#include <map>
#include <set>
#include <span>
#include <vector>

namespace stellar::core {

enum class SystemSurveyLevel {
  unknown = 0,
  detected = 1,
  partially_surveyed = 2,
  fully_surveyed = 3
};
struct SystemSurveyKnowledgeView {
  int system_id{};
  SystemSurveyLevel level{SystemSurveyLevel::unknown};
  double progress{};
};
struct KnowledgeSnapshotEntry {
  int observer_id{};
  std::vector<int> values;
};
struct KnowledgeSnapshot {
  std::vector<KnowledgeSnapshotEntry> systems;
  std::vector<KnowledgeSnapshotEntry> civilizations;
};

class CivilizationKnowledgeState {
public:
  bool has_galactic_core_access(int civilization_id) const;
  bool is_galactic_core_discovered(int civilization_id) const;
  std::vector<int> galactic_core_observers() const;
  void unlock_galactic_core_access(int civilization_id);
  bool record_galactic_core_exploration(int civilization_id);
  bool is_system_known(int civilization_id, int system_id) const;
  bool is_system_fully_surveyed(int civilization_id, int system_id) const;
  SystemSurveyLevel system_survey_level(int civilization_id, int system_id) const;
  double system_survey_progress(int civilization_id, int system_id) const;
  std::vector<SystemSurveyKnowledgeView> system_survey_knowledge(int civilization_id) const;
  bool is_civilization_known(int observer_id, int target_id) const;
  std::vector<int> known_systems(int civilization_id) const;
  std::vector<int> known_civilizations(int civilization_id) const;
  bool reveal_system(int civilization_id, int system_id);
  bool record_reconnaissance(int civilization_id, int system_id, double progress_floor = .35);
  bool advance_system_survey(int civilization_id, int system_id, double progress_delta);
  bool mark_system_fully_surveyed(int civilization_id, int system_id);
  bool reveal_civilization(int observer_id, int target_id);
  int reveal_within_sensor_range(int civilization_id, int origin_system_id,
                                 std::span<const StellarSystem> systems,
                                 float range);
  KnowledgeSnapshot snapshot() const;
  static CivilizationKnowledgeState
  create_initial(std::span<const StellarSystem> systems,
                 std::span<const Civilization> civilizations,
                 float sensor_range);
private:
  struct Survey { SystemSurveyLevel level{SystemSurveyLevel::detected}; double progress{}; };
  Survey &ensure_survey(int civilization_id, int system_id);
  std::map<int, std::set<int>> systems_;
  std::vector<int> system_observer_order_;
  std::set<int> core_access_, core_explored_;
  std::map<int, std::set<int>> civilizations_;
  std::vector<int> civilization_observer_order_;
  std::map<int, std::map<int, Survey>> surveys_;
};
} // namespace stellar::core
