using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;

namespace Game.Simulation.Construction;

public interface IConstructionCapabilityView
{
    bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId);
}

/// <summary>Compatibility adapter for isolated legacy simulations and old-save validation.</summary>
public sealed class PrototypeConstructionCapabilityView : IConstructionCapabilityView
{
    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) =>
        galaxy.Technologies.FirstOrDefault(state => state.CivilizationId == civilizationId)?
            .CompletedTechnologyIds.Contains(capabilityId) == true;
}

/// <summary>Authoritative construction prerequisites for an Adaptive Research campaign.</summary>
public sealed class AdaptiveResearchConstructionCapabilityView : IConstructionCapabilityView
{
    private readonly AdaptiveResearchCampaignState _campaign;

    public AdaptiveResearchConstructionCapabilityView(AdaptiveResearchCampaignState campaign) =>
        _campaign = campaign;

    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId)
    {
        var state = _campaign.GetCivilization(civilizationId);
        return capabilityId switch
        {
            "orbital_industry" => state.HasCapability("orbital_industry") ||
                state.HasEstablishedKnowledge("orbital_manufacturing"),
            "warp_field_control" => state.HasEstablishedKnowledge("warp_field_control"),
            "fusion_power" => state.HasEstablishedKnowledge("fusion_power"),
            "additive_manufacturing" => state.HasEstablishedKnowledge("additive_manufacturing"),
            "interplanetary_trade_standards" => state.HasEstablishedKnowledge("interplanetary_trade_standards"),
            "closed_loop_recycling" => state.HasEstablishedKnowledge("closed_loop_recycling"),
            _ => state.HasCapability(capabilityId),
        };
    }
}
