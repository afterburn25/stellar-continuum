using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;

namespace Game.Simulation.Shipbuilding;

public static class ShipbuildingCapabilityIds
{
    public const string SpacecraftConstruction = "spacecraft_construction";
    public const string ExperimentalInterstellarTransit = "experimental_interstellar_transit";
    public const string ReliableInterstellarTransit = "reliable_ftl";
    public const string ExtendedInterstellarTransit = "extended_ftl_range";

    public static string DisplayName(string capabilityId) => capabilityId switch
    {
        SpacecraftConstruction => "Spacecraft Construction",
        ExperimentalInterstellarTransit => "Experimental Interstellar Transit",
        ReliableInterstellarTransit => "Reliable Interstellar Transit",
        ExtendedInterstellarTransit => "Extended Interstellar Transit",
        _ => capabilityId.Replace('_', ' '),
    };
}

public interface IShipbuildingCapabilityView
{
    bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId);
}

/// <summary>
/// Temporary bridge from the current fixed prototype research state into the capability-oriented
/// shipbuilding contract. Replace this adapter when the Adaptive Research runtime publishes
/// materialized civilization capabilities; the shipbuilding simulation itself should not need to change.
/// </summary>
public sealed class PrototypeShipbuildingCapabilityView : IShipbuildingCapabilityView
{
    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId)
    {
        var technology = galaxy.Technologies.FirstOrDefault(state => state.CivilizationId == civilizationId);
        if (technology is null)
            return false;

        return capabilityId switch
        {
            ShipbuildingCapabilityIds.SpacecraftConstruction => technology.CompletedTechnologyIds.Contains("orbital_industry"),
            ShipbuildingCapabilityIds.ExperimentalInterstellarTransit => technology.CompletedTechnologyIds.Contains("prototype_warp_drive"),
            _ => false,
        };
    }
}

/// <summary>Authoritative ship-design prerequisites for an Adaptive Research campaign.</summary>
public sealed class AdaptiveResearchShipbuildingCapabilityView : IShipbuildingCapabilityView
{
    private readonly AdaptiveResearchCampaignState _campaign;

    public AdaptiveResearchShipbuildingCapabilityView(AdaptiveResearchCampaignState campaign) =>
        _campaign = campaign;

    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId)
    {
        var state = _campaign.GetCivilization(civilizationId);
        return capabilityId switch
        {
            ShipbuildingCapabilityIds.SpacecraftConstruction =>
                state.HasCapability(ShipbuildingCapabilityIds.SpacecraftConstruction) ||
                state.HasCapability("orbital_industry") ||
                state.HasEstablishedKnowledge("orbital_manufacturing"),
            ShipbuildingCapabilityIds.ExperimentalInterstellarTransit =>
                state.HasCapability(ShipbuildingCapabilityIds.ExperimentalInterstellarTransit),
            _ => state.HasCapability(capabilityId),
        };
    }
}
