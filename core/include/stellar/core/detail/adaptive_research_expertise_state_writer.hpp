#pragma once

#include <stellar/core/adaptive_research_expertise.hpp>

#include <string>
#include <string_view>

namespace stellar::core::detail {

[[nodiscard]] std::int64_t
checked_next_adaptive_research_expertise_revision(std::int64_t current);

class AdaptiveResearchExpertiseStateWriter final {
public:
  static void set_field(AdaptiveResearchExpertiseState &state,
                        ResearchFieldCompetenceRuntimeState value);
  static void remove_field_if_zero(AdaptiveResearchExpertiseState &state,
                                   std::string_view field_id);
  static void set_institution(AdaptiveResearchExpertiseState &state,
                              ResearchInstitutionRuntimeState value);
  static bool remove_institution(AdaptiveResearchExpertiseState &state,
                                 std::string_view institution_instance_id);
  static void set_tacit_asset(AdaptiveResearchExpertiseState &state,
                              ResearchTacitAssetRuntimeState value);
  static bool remove_tacit_asset(AdaptiveResearchExpertiseState &state,
                                 std::string_view asset_id);
};

} // namespace stellar::core::detail
