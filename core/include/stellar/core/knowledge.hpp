#pragma once

#include <stellar/core/civilization_catalog.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <unordered_map>
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
  // Transient query hint: bumped whenever that civilization's survey
  // LEVEL can change (new detected entry, or a level transition).
  // Progress-only writes do not bump it, so callers may memoize
  // level-derived views per revision. Writes against one civilization
  // do not invalidate another's memoized views. Not serialized;
  // restores rebuild through the mutators.
  std::uint64_t survey_level_revision(int civilization_id) const noexcept {
    const auto found = survey_level_revisions_.find(civilization_id);
    return found == survey_level_revisions_.end() ? 0 : found->second;
  }
  // Transient per-(civilization, system) record of the widest sensor
  // sweep radius already performed this session. A repeat sweep with an
  // equal-or-smaller radius can only re-encounter already-known systems
  // — reveals are idempotent and the reveal count drives events — so
  // callers may skip them outright. Not serialized; after a load the map
  // is empty and sweeps simply re-run, producing identical state.
  bool sensor_sweep_needed(int civilization_id, int system_id,
                           double range) const;
  void record_sensor_sweep(int civilization_id, int system_id,
                           double range);
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
  std::unordered_map<int, std::uint64_t> survey_level_revisions_;
  // Transient sensor-sweep coverage: (civ << 32 | system) -> widest
  // radius swept. Derived runtime state only — see sensor_sweep_needed.
  std::unordered_map<std::int64_t, double> sensor_coverage_;
};
} // namespace stellar::core
