#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>

#include <string_view>

namespace stellar::core {

// Authoritative construction prerequisite view for an Adaptive Research
// campaign. The campaign must outlive this view and remain unmoved. Queries do
// not inspect or fall back to legacy TechnologyState data.
class AdaptiveResearchConstructionCapabilityView final {
public:
  explicit AdaptiveResearchConstructionCapabilityView(
      const AdaptiveResearchCampaignState &campaign) noexcept;
  AdaptiveResearchConstructionCapabilityView(
      AdaptiveResearchCampaignState &&) = delete;

  [[nodiscard]] bool
  has_civilization_capability(int civilization_id,
                              std::string_view capability_id) const;

private:
  const AdaptiveResearchCampaignState *campaign_{};
};

// Authoritative ship-design prerequisite view for an Adaptive Research
// campaign. The campaign must outlive this view and remain unmoved. Queries do
// not inspect or fall back to legacy TechnologyState data.
class AdaptiveResearchShipbuildingCapabilityView final {
public:
  static constexpr std::string_view spacecraft_construction =
      "spacecraft_construction";
  static constexpr std::string_view experimental_interstellar_transit =
      "experimental_interstellar_transit";
  static constexpr std::string_view reliable_interstellar_transit =
      "reliable_ftl";
  static constexpr std::string_view extended_interstellar_transit =
      "extended_ftl_range";

  explicit AdaptiveResearchShipbuildingCapabilityView(
      const AdaptiveResearchCampaignState &campaign) noexcept;
  AdaptiveResearchShipbuildingCapabilityView(
      AdaptiveResearchCampaignState &&) = delete;

  [[nodiscard]] bool
  has_civilization_capability(int civilization_id,
                              std::string_view capability_id) const;

private:
  const AdaptiveResearchCampaignState *campaign_{};
};

} // namespace stellar::core
