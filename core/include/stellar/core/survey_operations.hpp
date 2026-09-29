#pragma once

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>

#include <span>
#include <unordered_map>

namespace stellar::core {

enum class SurveyOperationalHazard { Routine, Elevated, Severe };

struct SurveyOperationsProfile {
  int system_id{};
  int planet_count{};
  int moon_count{};
  double estimated_science_survey_days{};
  SurveyOperationalHazard operational_hazard{};

  double progress_per_day() const;
};

class SurveyOperationsProfiler {
public:
  static constexpr double minimum_survey_days = 9.0;
  static constexpr double maximum_survey_days = 28.0;

  SurveyOperationsProfile build(std::span<const StellarSystem> systems,
                                std::span<const PlanetaryBody> bodies,
                                int system_id) const;
};

// Shared catalog lookups for profile builds. Scope to one read-only
// planning pass while the system/body spans stay immutable — both maps are
// populated lazily on first use. Callers that omit it keep the original
// eager whole-catalog preparation.
struct SurveyCatalogIndex {
  std::unordered_map<int, const StellarSystem *> systems_by_id;
  std::unordered_map<int, std::vector<const PlanetaryBody *>>
      bodies_by_system;
};

// Read-only scope: systems and bodies must remain unchanged for its lifetime.
// With no shared index the first query scans the catalog once; subsequent
// queries are constant-time. With one, each build resolves just that
// system's bodies through the shared maps. Create a new batch after world
// edits instead of persisting cached game state.
class SurveyOperationsBatch {
public:
  SurveyOperationsBatch(std::span<const StellarSystem> systems,
                        std::span<const PlanetaryBody> bodies,
                        SurveyCatalogIndex *shared = nullptr)
      : systems_(systems), bodies_(bodies), shared_(shared) {}
  SurveyOperationsProfile build(int system_id);
private:
  std::span<const StellarSystem> systems_;
  std::span<const PlanetaryBody> bodies_;
  SurveyCatalogIndex *shared_{};
  std::unordered_map<int, SurveyOperationsProfile> profiles_;
  bool prepared_{};
};

} // namespace stellar::core
