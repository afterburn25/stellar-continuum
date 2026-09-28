using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Industry;

/// <summary>
/// The currently shared industrial consumers. More consumers can be added without moving
/// production policy into the individual construction systems.
/// </summary>
public enum IndustrialConsumer
{
    Construction,
    Shipbuilding,
}

public sealed record IndustryPriorityWeights(
    double ConstructionWeight,
    double ShipbuildingWeight
);

public interface IIndustryPriorityProvider
{
    IndustryPriorityWeights GetWeights(int civilizationId);
}

/// <summary>
/// Neutral fallback used until explicit player/AI industrial-priority controls exist.
/// Equal weight is an explicit policy, not a hidden production bonus or hard 50/50 cap:
/// unused capacity immediately flows to the other active claimant.
/// </summary>
public sealed class BalancedIndustryPriorityProvider : IIndustryPriorityProvider
{
    public IndustryPriorityWeights GetWeights(int civilizationId)
    {
        _ = civilizationId;
        return new IndustryPriorityWeights(1.0, 1.0);
    }
}

/// <summary>
/// Useful for tests and future policy adapters that want a stable global weighting.
/// Dynamic per-civilization/player policy can implement IIndustryPriorityProvider directly.
/// </summary>
public sealed class FixedIndustryPriorityProvider : IIndustryPriorityProvider
{
    private readonly IndustryPriorityWeights _weights;

    public FixedIndustryPriorityProvider(double constructionWeight, double shipbuildingWeight)
    {
        _weights = ValidateWeights(new IndustryPriorityWeights(constructionWeight, shipbuildingWeight));
    }

    public IndustryPriorityWeights GetWeights(int civilizationId)
    {
        _ = civilizationId;
        return _weights;
    }

    internal static IndustryPriorityWeights ValidateWeights(IndustryPriorityWeights weights)
    {
        if (!double.IsFinite(weights.ConstructionWeight) || weights.ConstructionWeight <= 0.0)
            throw new ArgumentOutOfRangeException(nameof(weights), "Construction weight must be finite and greater than zero.");
        if (!double.IsFinite(weights.ShipbuildingWeight) || weights.ShipbuildingWeight <= 0.0)
            throw new ArgumentOutOfRangeException(nameof(weights), "Shipbuilding weight must be finite and greater than zero.");
        return weights;
    }
}

/// <summary>Reads the persisted policy from the current campaign economy state.</summary>
public sealed class CampaignIndustryPriorityProvider : IIndustryPriorityProvider
{
    private GalaxyState? _galaxy;
    private readonly IIndustryPriorityProvider _fallback;
    public CampaignIndustryPriorityProvider(IIndustryPriorityProvider fallback) => _fallback = fallback;
    public void Bind(GalaxyState galaxy) => _galaxy = galaxy;
    public IndustryPriorityWeights GetWeights(int civilizationId)
    {
        var priority = _galaxy?.Economies.FirstOrDefault(e => e.CivilizationId == civilizationId)?.IndustryPriority;
        if (priority is null) return _fallback.GetWeights(civilizationId);
        return priority switch
        {
            IndustryPriority.Balanced => new(1, 1),
            IndustryPriority.InfrastructureFirst => new(3, 1),
            IndustryPriority.ShipbuildingFirst => new(1, 3),
            _ => throw new InvalidOperationException("Unknown persisted industry priority."),
        };
    }
}

public sealed record IndustryPriorityChangeResult(bool Accepted, string Message);

public static class IndustryPriorityCommands
{
    public static IndustryPriorityChangeResult Set(GalaxyState galaxy, int actorCivilizationId,
        int targetCivilizationId, IndustryPriority priority)
    {
        if (!Enum.IsDefined(priority)) return new(false, "Unknown industry priority.");
        if (actorCivilizationId != targetCivilizationId) return new(false, "Only the owning civilization can set its industry priority.");
        var economy = galaxy.Economies.FirstOrDefault(e => e.CivilizationId == targetCivilizationId);
        if (economy is null) return new(false, "Unknown civilization economy.");
        economy.IndustryPriority = priority;
        return new(true, $"Industry priority set to {Display(priority)}.");
    }

    private static string Display(IndustryPriority priority) => priority switch
    {
        IndustryPriority.InfrastructureFirst => "Infrastructure first",
        IndustryPriority.ShipbuildingFirst => "Shipbuilding first",
        _ => "Balanced",
    };
}

public sealed record IndustryAllocationContext(
    int CivilizationId,
    double AvailableIndustry,
    double ConstructionDemand,
    double ShipbuildingDemand
);

public sealed record CivilizationIndustryAllocation(
    int CivilizationId,
    double AvailableIndustry,
    double ConstructionDemand,
    double ShipbuildingDemand,
    double ConstructionWeight,
    double ShipbuildingWeight,
    double ConstructionAllocated,
    double ShipbuildingAllocated
)
{
    public double TotalAllocated => ConstructionAllocated + ShipbuildingAllocated;
}

public interface IIndustryAllocationPolicy
{
    CivilizationIndustryAllocation Allocate(IndustryAllocationContext context);
}

/// <summary>
/// Deterministic weighted max-min allocation for the two current industrial consumers.
/// Both active claimants receive their weighted fair share; if one cannot consume that share,
/// the unused capacity is immediately reflowed to the other claimant. Total allocation never
/// exceeds either real demand or the civilization's available Industry stockpile.
/// </summary>
public sealed class WeightedFairIndustryAllocationPolicy : IIndustryAllocationPolicy
{
    private readonly IIndustryPriorityProvider _priorityProvider;

    public WeightedFairIndustryAllocationPolicy(IIndustryPriorityProvider? priorityProvider = null)
    {
        _priorityProvider = priorityProvider ?? new BalancedIndustryPriorityProvider();
    }

    public CivilizationIndustryAllocation Allocate(IndustryAllocationContext context)
    {
        ValidateFiniteNonNegative(context.AvailableIndustry, nameof(context.AvailableIndustry));
        ValidateFiniteNonNegative(context.ConstructionDemand, nameof(context.ConstructionDemand));
        ValidateFiniteNonNegative(context.ShipbuildingDemand, nameof(context.ShipbuildingDemand));

        var weights = FixedIndustryPriorityProvider.ValidateWeights(_priorityProvider.GetWeights(context.CivilizationId));
        var available = context.AvailableIndustry;
        var constructionDemand = context.ConstructionDemand;
        var shipbuildingDemand = context.ShipbuildingDemand;

        if (available <= 0.0 || (constructionDemand <= 0.0 && shipbuildingDemand <= 0.0))
            return CreateResult(context, weights, 0.0, 0.0);

        var totalDemand = constructionDemand + shipbuildingDemand;
        if (available >= totalDemand)
            return CreateResult(context, weights, constructionDemand, shipbuildingDemand);

        if (constructionDemand <= 0.0)
            return CreateResult(context, weights, 0.0, Math.Min(available, shipbuildingDemand));
        if (shipbuildingDemand <= 0.0)
            return CreateResult(context, weights, Math.Min(available, constructionDemand), 0.0);

        var totalWeight = weights.ConstructionWeight + weights.ShipbuildingWeight;
        var constructionShare = available * weights.ConstructionWeight / totalWeight;
        var shipbuildingShare = available * weights.ShipbuildingWeight / totalWeight;

        var constructionAllocated = Math.Min(constructionDemand, constructionShare);
        var shipbuildingAllocated = Math.Min(shipbuildingDemand, shipbuildingShare);
        var remaining = Math.Max(0.0, available - constructionAllocated - shipbuildingAllocated);

        // A positive remainder can exist only because one claimant saturated below its share.
        // Reflow to the still-unsatisfied claimant; there is no callback-order preference.
        if (remaining > 0.0)
        {
            var constructionUnmet = Math.Max(0.0, constructionDemand - constructionAllocated);
            var constructionExtra = Math.Min(remaining, constructionUnmet);
            constructionAllocated += constructionExtra;
            remaining -= constructionExtra;
        }

        if (remaining > 0.0)
        {
            var shipbuildingUnmet = Math.Max(0.0, shipbuildingDemand - shipbuildingAllocated);
            var shipbuildingExtra = Math.Min(remaining, shipbuildingUnmet);
            shipbuildingAllocated += shipbuildingExtra;
        }

        return CreateResult(context, weights, constructionAllocated, shipbuildingAllocated);
    }

    private static CivilizationIndustryAllocation CreateResult(
        IndustryAllocationContext context,
        IndustryPriorityWeights weights,
        double constructionAllocated,
        double shipbuildingAllocated) =>
        new(
            context.CivilizationId,
            context.AvailableIndustry,
            context.ConstructionDemand,
            context.ShipbuildingDemand,
            weights.ConstructionWeight,
            weights.ShipbuildingWeight,
            constructionAllocated,
            shipbuildingAllocated);

    private static void ValidateFiniteNonNegative(double value, string parameterName)
    {
        if (!double.IsFinite(value) || value < 0.0)
            throw new ArgumentOutOfRangeException(parameterName, "Industry values must be finite and non-negative.");
    }
}
