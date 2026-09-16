#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "stellar/core/fresh_campaign.hpp"
#include "stellar/core/galaxy_catalog.hpp"
#include "stellar/core/knowledge.hpp"
#include "stellar/engine/native_map_platform.hpp"

namespace stellar::native_inspection {

// UiInspectionFact/UiSystemInspectionSnapshot port — observer-gated system
// intelligence for the galaxy-map selected-star card.
struct NativeInspectionFact {
  std::string label, value;
  bool positive{};
};

struct NativeSystemInspection {
  std::string name{"SELECT A STAR"}, survey_status{"No target"},
      guidance{"Select a star on the map to open its intelligence record."};
  double survey_progress{};
  bool has_detailed_survey{};
  std::vector<NativeInspectionFact> facts;
  std::string colony_name{"NO COLONY DATA"}, colony_details;
};

[[nodiscard]] NativeSystemInspection build_system_inspection(
    const core::FreshCampaignState &campaign, int selected_system_id);

[[nodiscard]] std::string_view stellar_class_label(core::StellarClass) noexcept;
[[nodiscard]] std::string_view survey_level_name(core::SystemSurveyLevel) noexcept;

// Draws the reference sidebar card (name, survey status + progress, guidance,
// intelligence-signals facts grid, settlement card) with its bottom-left
// corner at `bottom_left`. Returns the card's bounds for hit testing.
[[nodiscard]] native_map::UiRect append_inspection_card(
    native_map::DrawList &out, const NativeSystemInspection &inspection,
    native_map::Point bottom_left, float scale);

} // namespace stellar::native_inspection
