using System;
using System.Linq;
using Game.Simulation.Research.Adaptive;

namespace Game.Presentation;

public partial class Main
{
    private sealed record ResearchExplanation(string WhatItDoes, string Benefits, string CostAndTime,
        string RequirementsStatus);

    private ResearchExplanation ExplainVisibleResearch(
        AdaptiveResearchNodeView visible,
        bool active,
        AdaptiveResearchProjectView? project,
        AdaptiveResearchFundingQuote? quote,
        AdaptiveResearchFundingQuote? estimateQuote,
        double milestoneRemaining,
        double freeLabs,
        string physicalRequirement,
        string? blocker)
    {
        // The caller projects only an observer-safe VisibleNodes entry; no hidden graph
        // elements or unrecognized capabilities are consulted here.
        var definition = _adaptiveResearch!.Runtime.Authority.Catalog.GetNode(visible.NodeId);
        var what = CuratedResearchPurpose(visible.NodeId) ??
            $"Explores a practical {DisplayResearchDomain(visible.DomainId).ToLowerInvariant()} approach in the {Humanize(visible.SolutionFamily)} field.";
        var benefits = ResearchBenefits(visible);

        if (visible.State == ResearchMaturity.Mature && !active)
        {
            return new ResearchExplanation(
                what,
                benefits,
                "Completed research has no ongoing program cost and is not charged again.",
                $"Completed. {physicalRequirement}.");
        }

        if (quote is null)
        {
            return new ResearchExplanation(
                what,
                benefits,
                "Detailed budget and staffing estimate becomes available when scientists can investigate this project.",
                blocker is null
                    ? "Scientists have recognized this question, but it is not ready for a directed program."
                    : $"{blocker} {physicalRequirement}.");
        }

        var staffing = active ? project!.AssignedEffectiveLabs : quote.AssignedEffectiveLabs;
        var cost = active
            ? $"Assigned labs: {staffing:0.#}. Current operating cost: {UiFormatMoney(quote.OperatingCreditsPerDay)} / day. " +
              $"Remaining milestone reserve: {UiFormatMoney(milestoneRemaining)}. Research work: {definition.ProjectRequirements.BaseResearchPoints:N0} RP. " +
              "The reserve is spent only as milestones are reached."
            : $"Required cash available: {UiFormatMoney(AdaptiveResearchCampaignCommands.CreditsNeededToStart(quote))}. " +
              $"Paid when starting: {UiFormatMoney(quote.AuthorizationCredits)} authorization + {UiFormatMoney(quote.MilestoneCommitmentCredits)} milestone reserve. " +
              $"Keep {UiFormatMoney(quote.OperatingCreditsPerDay)} available for the first operating day; charged as work runs. " +
              $"Operating cost: {UiFormatMoney(quote.OperatingCreditsPerDay)} / day. Research work: {definition.ProjectRequirements.BaseResearchPoints:N0} RP. " +
              $"Estimate assumes {(estimateQuote ?? quote).AssignedEffectiveLabs:0.#} labs: {ResearchEstimate(estimateQuote ?? quote)}.";

        var availability = active
            ? $"Labs: assigned {staffing:0.#}; minimum {visible.MinimumLabs}; recommended {visible.RecommendedLabs}."
            : freeLabs + .0001 < visible.MinimumLabs
                ? $"Unfunded: {freeLabs:0.#} free labs; estimate assumes {(estimateQuote ?? quote).AssignedEffectiveLabs:0.#} labs."
                : $"Labs: minimum {visible.MinimumLabs}, recommended {visible.RecommendedLabs}, available {freeLabs:0.#}; this program starts with {staffing:0.#}.";
        var status = active
            ? $"{availability} Current funding: {PlayerEconomy.LastResearchFundingFraction:P0}. " +
              (project!.Paused ? $"{PauseStatus(project.PauseReason)}." : $"{project.ReadinessBand} readiness.") +
              $" Facility requirement: {physicalRequirement}."
            : $"{availability} " + (blocker is null ? "Ready to authorize when cash and staffing are available." : blocker) +
              $" Facility requirement: {physicalRequirement}.";
        return new ResearchExplanation(what, benefits, cost, status);
    }

    private string ResearchBenefits(AdaptiveResearchNodeView visible)
    {
        var curated = CuratedResearchBenefit(visible.NodeId, visible.State);
        if (visible.KnownCapabilities.Count > 0)
        {
            var capabilities = string.Join(", ", visible.KnownCapabilities.Select(DisplayKnownCapability));
            return curated is null
                ? $"Confirmed capability: {capabilities}."
                : $"Confirmed capability: {capabilities}. {curated}";
        }

        return curated ?? (visible.State == ResearchMaturity.Hypothesized
            ? "This is an uncertain research question. Its eventual result is not yet known."
            : visible.State >= ResearchMaturity.Demonstrated
                ? "This result has been demonstrated. It contributes a confirmed research foundation, with no immediate production bonus."
                : "This work contributes knowledge for the next practical step in this field; it has no immediate production bonus.");
    }

    private string DisplayKnownCapability(string capabilityId)
    {
        var catalog = _adaptiveResearch!.Runtime.Authority.Catalog;
        return catalog.Capabilities.TryGetValue(capabilityId, out var capability)
            ? capability.Name
            : Humanize(capabilityId.StartsWith("tech:", StringComparison.Ordinal)
                ? capabilityId["tech:".Length..]
                : capabilityId.StartsWith("capability:", StringComparison.Ordinal)
                    ? capabilityId["capability:".Length..]
                    : capabilityId);
    }

    private string ResearchEstimate(AdaptiveResearchFundingQuote quote) =>
        double.IsFinite(quote.EstimatedYearsAtFullFunding)
            ? $"about {quote.EstimatedYearsAtFullFunding:0.0} game years at full funding; estimated total {UiFormatMoney(quote.EstimatedTotalCredits)}"
            : "a staffed duration estimate is unavailable";

    private static string PauseStatus(string? reason) => reason switch
    {
        "hypothesis_resolution_required" => "Paused while a visible research uncertainty is resolved",
        null or "" => "Paused by the player",
        _ => "Paused because current requirements need attention",
    };

    private static string Humanize(string value) => string.Join(' ', value.Split('_', StringSplitOptions.RemoveEmptyEntries)
        .Select(word => word.Length == 0 ? word : char.ToUpperInvariant(word[0]) + word[1..]));

    private static string? CuratedResearchPurpose(string id) => id switch
    {
        "in_space_assembly" => "Develops methods for assembling and validating orbital structures.",
        "asteroid_prospecting" => "Develops ways to locate and assess resource deposits for later mining.",
        "asteroid_mining" => "Develops controlled extraction methods for accessible asteroid material.",
        "vacuum_refining" => "Studies processing and purification methods that work in vacuum.",
        "orbital_manufacturing" => "Develops repeatable manufacturing methods for orbital industry.",
        "orbital_shipyard" => "Develops the engineering basis for an orbital shipyard.",
        "gravitational_physics" => "Builds methods to measure gravity for advanced propulsion research.",
        "field_theory" => "Develops mathematical and experimental tools for controllable field research.",
        "warp_metric_theory" => "Investigates metric models relevant to spacetime-field research.",
        "exotic_energy_coupling" => "Investigates how exotic energy sources could couple to controlled fields.",
        "micro_field_distortion" => "Tests small-scale controlled distortion experiments.",
        "warp_field_control" => "Develops control methods for stable laboratory-scale distortion fields.",
        "prototype_warp_drive" => "Develops a vessel-scale prototype for interstellar transit.",
        _ => null,
    };

    private static string? CuratedResearchBenefit(string id, ResearchMaturity maturity) => id switch
    {
        "orbital_manufacturing" => "At mature completion, Orbital Industry enables spacecraft construction and makes the Orbital Shipyard construction project available once the Orbital Launch Complex is complete.",
        "orbital_shipyard" => "This research improves the knowledge base for operating orbital yards. Building an Orbital Shipyard still requires the separate construction project and its launch-complex prerequisite.",
        "prototype_warp_drive" when maturity >= ResearchMaturity.Demonstrated =>
            "Demonstration supports limited interstellar transit. Ships still require an operational Orbital Shipyard.",
        "prototype_warp_drive" => "A demonstrated result may support limited interstellar transit; ships still require an operational Orbital Shipyard.",
        "in_space_assembly" => "Provides assembly and validation methods needed before large orbital manufacturing can be attempted.",
        "asteroid_prospecting" => "Improves the basis for selecting targets before an extraction program is proposed.",
        "asteroid_mining" => "Provides the extraction methods needed before asteroid-material operations can be developed.",
        "vacuum_refining" => "Provides vacuum-processing knowledge needed for orbital manufacturing work.",
        "gravitational_physics" => "Supplies measurements and models used by later advanced-propulsion research.",
        "field_theory" => "Supplies the experimental language and controls for later field experiments.",
        "warp_metric_theory" => "Supplies spacetime models that later warp-field experiments can test.",
        "exotic_energy_coupling" => "Supplies an energy-coupling basis for controlled field experiments.",
        "micro_field_distortion" => "Tests whether small controlled distortions can inform larger field-control work.",
        "warp_field_control" => "Provides laboratory control methods required before vessel-scale warp experiments.",
        _ => null,
    };
}
