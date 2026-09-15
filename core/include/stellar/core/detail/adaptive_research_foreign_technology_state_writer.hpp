#pragma once

#include <stellar/core/adaptive_research_foreign_technology.hpp>

#include <cstdint>

namespace stellar::core::detail {

class AdaptiveResearchForeignTechnologyStateWriter final {
public:
  static void set_assessment(AdaptiveResearchForeignTechnologyState &state,
                             ForeignTechnologyAssessmentRuntimeState value);
  static void add_package(AdaptiveResearchForeignTechnologyState &state,
                          ForeignTechnologyPackageRuntimeState value);
  static void set_revision_for_recovery(
      AdaptiveResearchForeignTechnologyState &state,
      std::int64_t revision) noexcept;
};

} // namespace stellar::core::detail
