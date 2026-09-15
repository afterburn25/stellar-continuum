#pragma once

#include <string>

namespace stellar::core {
class AdaptiveResearchAgendaRuntime;
class AdaptiveResearchAgendaState;
class AdaptiveResearchCivilizationState;

namespace detail {

class AdaptiveResearchAgendaSupportAccess final {
public:
  [[nodiscard]] static AdaptiveResearchAgendaState &
  state(const AdaptiveResearchAgendaRuntime &runtime,
        const AdaptiveResearchCivilizationState &civilization);
  // Fixture-only equivalent of the retained C# reflection probe. Gameplay
  // and restoration must use the validated StateWriter::mark_reviewed path.
  static void set_review_metadata_unchecked(
      AdaptiveResearchAgendaState &state, double current_year,
      std::string provenance);
};

} // namespace detail
} // namespace stellar::core
