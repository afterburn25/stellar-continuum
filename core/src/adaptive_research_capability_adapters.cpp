#include <stellar/core/adaptive_research_capability_adapters.hpp>

namespace stellar::core {

AdaptiveResearchConstructionCapabilityView::
    AdaptiveResearchConstructionCapabilityView(
        const AdaptiveResearchCampaignState &campaign) noexcept
    : campaign_(&campaign) {}

bool AdaptiveResearchConstructionCapabilityView::
    has_civilization_capability(int civilization_id,
                                std::string_view capability_id) const {
  const auto &state = campaign_->get_civilization(civilization_id);
  if (capability_id == "orbital_industry")
    return state.has_capability("orbital_industry") ||
           state.has_established_knowledge("orbital_manufacturing");
  if (capability_id == "warp_field_control")
    return state.has_established_knowledge("warp_field_control");
  if (capability_id == "fusion_power")
    return state.has_established_knowledge("fusion_power");
  if (capability_id == "additive_manufacturing")
    return state.has_established_knowledge("additive_manufacturing");
  if (capability_id == "interplanetary_trade_standards")
    return state.has_established_knowledge(
        "interplanetary_trade_standards");
  if (capability_id == "closed_loop_recycling")
    return state.has_established_knowledge("closed_loop_recycling");
  return state.has_capability(capability_id);
}

AdaptiveResearchShipbuildingCapabilityView::
    AdaptiveResearchShipbuildingCapabilityView(
        const AdaptiveResearchCampaignState &campaign) noexcept
    : campaign_(&campaign) {}

bool AdaptiveResearchShipbuildingCapabilityView::
    has_civilization_capability(int civilization_id,
                                std::string_view capability_id) const {
  const auto &state = campaign_->get_civilization(civilization_id);
  if (capability_id == spacecraft_construction)
    return state.has_capability(spacecraft_construction) ||
           state.has_capability("orbital_industry") ||
           state.has_established_knowledge("orbital_manufacturing");
  if (capability_id == experimental_interstellar_transit)
    return state.has_capability(experimental_interstellar_transit);
  return state.has_capability(capability_id);
}

} // namespace stellar::core
