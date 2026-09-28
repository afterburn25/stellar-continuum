using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Research.Adaptive;

public sealed record AdaptiveResearchFundingQuote(
    double AssignedEffectiveLabs,
    double AuthorizationCredits,
    double MilestoneCommitmentCredits,
    double OperatingCreditsPerDay,
    double EstimatedTotalOperatingCredits,
    double EstimatedTotalCredits,
    double EstimatedYearsAtFullFunding,
    string Complexity);

/// <summary>
/// Initial campaign funding policy for directed research. Credits pay the operating program while
/// RP remains the independent measure of completed scientific and engineering work.
/// </summary>
public static class AdaptiveResearchFundingPolicy
{
    // On the legacy prototype scale this represents $15M per effective lab-year before
    // complexity. Advanced and Frontier programs multiply the real staffing/equipment burden.
    public const double BaseAnnualCreditsPerEffectiveLab = 1.5;

    public static AdaptiveResearchFundingQuote Quote(
        AdaptiveResearchNodeDefinition node,
        double assignedEffectiveLabs,
        AdaptiveResearchCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(catalog);
        if (!double.IsFinite(assignedEffectiveLabs) || assignedEffectiveLabs < 0.0)
            throw new ArgumentOutOfRangeException(nameof(assignedEffectiveLabs));

        var complexityMultiplier = ComplexityMultiplier(node.Complexity);
        var authorizationCredits = AuthorizationCredits(node.Complexity);
        var milestoneCommitmentCredits = MilestoneCommitmentCredits(node.Complexity);
        var annualCost = assignedEffectiveLabs * BaseAnnualCreditsPerEffectiveLab * complexityMultiplier;
        var operatingPerDay = annualCost / 365.25;
        var scaledLabs = catalog.LabScaling.ScaleAssignedLabs(
            assignedEffectiveLabs,
            node.ProjectRequirements.RecommendedLabs);
        var rpPerYear = scaledLabs * catalog.Metadata.BaseRpPerEffectiveLabPerYear;
        var estimatedYears = rpPerYear <= 0.0
            ? double.PositiveInfinity
            : node.ProjectRequirements.BaseResearchPoints / rpPerYear;
        var estimatedTotal = double.IsFinite(estimatedYears)
            ? annualCost * estimatedYears
            : double.PositiveInfinity;

        return new AdaptiveResearchFundingQuote(
            assignedEffectiveLabs,
            authorizationCredits,
            milestoneCommitmentCredits,
            operatingPerDay,
            estimatedTotal,
            authorizationCredits + milestoneCommitmentCredits + estimatedTotal,
            estimatedYears,
            node.Complexity);
    }

    public static double AuthorizationCredits(string complexity) =>
        complexity.Trim().ToLowerInvariant() switch
        {
            // Initial contracting, prototype equipment, compliance, and program administration.
            // One campaign Credit is the legacy $10M reference unit.
            "foundation" => 0.5,
            "developing" => 2.0,
            "advanced" => 7.5,
            "frontier" => 25.0,
            _ => throw new InvalidOperationException(
                $"Research complexity '{complexity}' has no authorization cost policy."),
        };

    public static double MilestoneCommitmentCredits(string complexity) =>
        complexity.Trim().ToLowerInvariant() switch
        {
            "foundation" => 0.3,
            "developing" => 1.0,
            "advanced" => 3.0,
            "frontier" => 8.0,
            _ => throw new InvalidOperationException(
                $"Research complexity '{complexity}' has no milestone cost policy."),
        };

    public static double EstimateTreasuryRunwayDays(
        double availableCredits,
        double netCreditsPerDayBeforeResearch,
        double researchOperatingCreditsPerDay)
    {
        if (!double.IsFinite(availableCredits) || availableCredits < 0.0)
            throw new ArgumentOutOfRangeException(nameof(availableCredits));
        if (!double.IsFinite(netCreditsPerDayBeforeResearch))
            throw new ArgumentOutOfRangeException(nameof(netCreditsPerDayBeforeResearch));
        if (!double.IsFinite(researchOperatingCreditsPerDay) || researchOperatingCreditsPerDay < 0.0)
            throw new ArgumentOutOfRangeException(nameof(researchOperatingCreditsPerDay));

        var netBurnPerDay = researchOperatingCreditsPerDay - netCreditsPerDayBeforeResearch;
        return netBurnPerDay <= 0.0000001
            ? double.PositiveInfinity
            : availableCredits / netBurnPerDay;
    }

    public static double ComplexityMultiplier(string complexity) =>
        complexity.Trim().ToLowerInvariant() switch
        {
            "foundation" => 0.75,
            "developing" => 1.25,
            "advanced" => 2.50,
            "frontier" => 5.00,
            _ => throw new InvalidOperationException(
                $"Research complexity '{complexity}' has no financial cost policy."),
        };
}

/// <summary>
/// Campaign command boundary for starting funded research. The lower-level Adaptive Research
/// authority remains usable by isolated research tests and tooling that do not own an economy.
/// </summary>
public static class AdaptiveResearchCampaignCommands
{
    public static double CreditsNeededToStart(AdaptiveResearchFundingQuote quote) =>
        quote.AuthorizationCredits + quote.MilestoneCommitmentCredits + quote.OperatingCreditsPerDay;

    public static AdaptiveResearchCommandResult StartDirectedResearch(
        GalaxyState galaxy,
        AdaptiveResearchCampaignState campaign,
        int civilizationId,
        string nodeId,
        double requestedAssignedLabs,
        string? targetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(campaign);

        var economy = galaxy.Economies.SingleOrDefault(value => value.CivilizationId == civilizationId);
        if (economy is null)
            return AdaptiveResearchCommandResult.Rejected(
                $"Civilization {civilizationId} has no economy available to fund research.");

        AdaptiveResearchCivilizationState state;
        AdaptiveResearchNodeDefinition node;
        try
        {
            state = campaign.GetCivilization(civilizationId);
            node = campaign.Runtime.Authority.Catalog.GetNode(nodeId);
        }
        catch (Exception exception) when (exception is KeyNotFoundException or ArgumentException)
        {
            return AdaptiveResearchCommandResult.Rejected(exception.Message);
        }

        AdaptiveResearchFundingQuote quote;
        try
        {
            quote = AdaptiveResearchFundingPolicy.Quote(
                node, requestedAssignedLabs, campaign.Runtime.Authority.Catalog);
        }
        catch (Exception exception) when (exception is ArgumentOutOfRangeException or InvalidOperationException)
        {
            return AdaptiveResearchCommandResult.Rejected(exception.Message);
        }

        if (campaign.GetProjectFunding(civilizationId).ContainsKey(nodeId))
            return AdaptiveResearchCommandResult.Rejected(
                $"{node.Name} already has an active milestone funding commitment.");

        var firstDayRequirement = CreditsNeededToStart(quote);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (economy.Credits + 0.000001 < firstDayRequirement)
            return AdaptiveResearchCommandResult.Rejected(
                $"{node.Name} requires {currency.Format(quote.AuthorizationCredits)} to authorize and " +
                $"{currency.Format(quote.MilestoneCommitmentCredits)} for prototype milestones, plus " +
                $"{currency.Format(quote.OperatingCreditsPerDay)} for its first operating day; " +
                $"{currency.Format(economy.Credits)} is available.");

        var result = campaign.Runtime.Authority.StartDirectedResearch(
            state, nodeId, requestedAssignedLabs, targetApplicabilityContextId);
        if (!result.Accepted) return result;

        campaign.ReserveProjectMilestones(
            civilizationId, nodeId, quote.AuthorizationCredits, quote.MilestoneCommitmentCredits);
        economy.Credits -= quote.AuthorizationCredits + quote.MilestoneCommitmentCredits;
        return result with
        {
            Message = $"{result.Message} Authorized for {currency.Format(quote.AuthorizationCredits)}; " +
                      $"{currency.Format(quote.MilestoneCommitmentCredits)} reserved for prototypes and validation; " +
                      $"planned operations cost {currency.FormatRate(-quote.OperatingCreditsPerDay)}.",
        };
    }

    public static AdaptiveResearchCommandResult PauseDirectedResearch(
        AdaptiveResearchCampaignState campaign,
        int civilizationId,
        string nodeId)
    {
        ArgumentNullException.ThrowIfNull(campaign);
        AdaptiveResearchCivilizationState state;
        try
        {
            state = campaign.GetCivilization(civilizationId);
        }
        catch (KeyNotFoundException exception)
        {
            return AdaptiveResearchCommandResult.Rejected(exception.Message);
        }
        return campaign.Runtime.Authority.PauseDirectedResearch(state, nodeId);
    }

    public static AdaptiveResearchCommandResult ResumeDirectedResearch(
        GalaxyState galaxy,
        AdaptiveResearchCampaignState campaign,
        int civilizationId,
        string nodeId,
        double requestedAssignedLabs)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(campaign);
        AdaptiveResearchCivilizationState state;
        AdaptiveResearchNodeDefinition node;
        try
        {
            state = campaign.GetCivilization(civilizationId);
            node = campaign.Runtime.Authority.Catalog.GetNode(nodeId);
        }
        catch (Exception exception) when (exception is KeyNotFoundException or ArgumentException)
        {
            return AdaptiveResearchCommandResult.Rejected(exception.Message);
        }

        if (!state.ActiveProjects.TryGetValue(nodeId, out var project) || !project.Paused)
            return AdaptiveResearchCommandResult.Rejected("The project is not currently paused.");
        if (string.Equals(project.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal))
            return AdaptiveResearchCommandResult.Rejected(
                $"{node.Name} requires scientific resolution before research can resume.");

        var economy = galaxy.Economies.SingleOrDefault(value => value.CivilizationId == civilizationId);
        if (economy is null)
            return AdaptiveResearchCommandResult.Rejected(
                $"Civilization {civilizationId} has no economy available to fund research.");
        var quote = AdaptiveResearchFundingPolicy.Quote(
            node, requestedAssignedLabs, campaign.Runtime.Authority.Catalog);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (economy.Credits + 0.000001 < quote.OperatingCreditsPerDay)
            return AdaptiveResearchCommandResult.Rejected(
                $"{node.Name} needs {currency.Format(quote.OperatingCreditsPerDay)} for its first resumed " +
                $"operating day; {currency.Format(economy.Credits)} is available.");

        return campaign.Runtime.Authority.ResumeDirectedResearch(
            state, nodeId, requestedAssignedLabs);
    }
}
