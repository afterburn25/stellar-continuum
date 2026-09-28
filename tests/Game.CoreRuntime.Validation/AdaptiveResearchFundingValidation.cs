using Game.Simulation.Generation;
using Game.Simulation.Research.Adaptive;

namespace Game.CoreRuntime.Validation;

internal static class AdaptiveResearchFundingValidation
{
    public static void Run()
    {
        var root = AdaptiveResearchDataLocator.FindDataRoot();
        var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
        var galaxy = new GalaxyGenerator().Generate(62941, new GalaxyGenerationSettings());
        var campaign = new AdaptiveResearchCampaignFactory(runtime).Create(galaxy);
        var playerId = galaxy.PlayerCivilizationId;
        var state = campaign.GetCivilization(playerId);
        var economy = galaxy.Economies.Single(value => value.CivilizationId == playerId);
        var node = runtime.Authority.Catalog.GetNode("fusion_power");
        var labs = Math.Min(node.ProjectRequirements.RecommendedLabs, state.FreeEffectiveLabs);
        var quote = AdaptiveResearchFundingPolicy.Quote(node, labs, runtime.Authority.Catalog);

        Require(quote.AuthorizationCredits > 0.0 && quote.MilestoneCommitmentCredits > 0.0 &&
                quote.OperatingCreditsPerDay > 0.0 &&
                quote.EstimatedTotalOperatingCredits > 0.0 &&
                quote.EstimatedTotalCredits > quote.EstimatedTotalOperatingCredits,
            "research funding policy produced a free directed program");
        var requiredToStart = AdaptiveResearchCampaignCommands.CreditsNeededToStart(quote);
        economy.Credits = requiredToStart - 0.01;
        var rejectedBalance = economy.Credits;
        var rejected = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            galaxy, campaign, playerId, node.Id, labs);
        Require(!rejected.Accepted && rejected.Message.Contains("first operating day", StringComparison.Ordinal),
            "underfunded research start did not return useful funding diagnostics");
        Near(economy.Credits, rejectedBalance,
            "rejected research authorization charged the treasury");
        Require(!state.ActiveProjects.ContainsKey(node.Id),
            "underfunded research start created a project");

        economy.Credits = 100.0;
        var authorizedBalance = economy.Credits;
        var authorized = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            galaxy, campaign, playerId, node.Id, labs);
        Require(authorized.Accepted && authorized.Message.Contains("Authorized", StringComparison.Ordinal),
            "funding validation could not authorize its visible research program");
        Near(economy.Credits, authorizedBalance - quote.AuthorizationCredits - quote.MilestoneCommitmentCredits,
            "accepted research start did not deduct its exact authorization and milestone reserve");
        var initialFunding = campaign.GetProjectFunding(playerId)[node.Id];
        Near(initialFunding.ReservedMilestoneCredits, quote.MilestoneCommitmentCredits,
            "accepted research start did not record its milestone reserve");
        Near(initialFunding.ConsumedMilestoneCredits, 0.0,
            "new research program consumed a milestone before reaching a stage boundary");
        Near(initialFunding.AuthorizationCredits, quote.AuthorizationCredits,
            "new research program did not record its paid authorization");

        var pause = AdaptiveResearchCampaignCommands.PauseDirectedResearch(
            campaign, playerId, node.Id);
        Require(pause.Accepted && state.ActiveProjects[node.Id].Paused,
            "campaign command could not pause funded research");
        economy.Credits = Math.Max(0.0, quote.OperatingCreditsPerDay - 0.01);
        var underfundedResume = AdaptiveResearchCampaignCommands.ResumeDirectedResearch(
            galaxy, campaign, playerId, node.Id, labs);
        Require(!underfundedResume.Accepted && state.ActiveProjects[node.Id].Paused &&
                underfundedResume.Message.Contains("resumed operating day", StringComparison.Ordinal),
            "underfunded research resumed or omitted useful diagnostics");
        economy.Credits = quote.OperatingCreditsPerDay * 2.0;
        var resume = AdaptiveResearchCampaignCommands.ResumeDirectedResearch(
            galaxy, campaign, playerId, node.Id, labs);
        Require(resume.Accepted && !state.ActiveProjects[node.Id].Paused,
            "funded campaign command could not resume research");

        var balanceBeforeDuplicate = economy.Credits;
        var duplicate = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            galaxy, campaign, playerId, node.Id, labs);
        Require(!duplicate.Accepted, "duplicate research program unexpectedly started");
        Near(economy.Credits, balanceBeforeDuplicate,
            "research command charged for a project rejected by the research authority");

        economy.Credits = quote.OperatingCreditsPerDay * 20.0;
        var startingCredits = economy.Credits;
        var startingRp = state.ActiveProjects[node.Id].TotalResearchPoints;
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 10.0, currentSimulationDay: 10.0);
        var fundedRp = state.ActiveProjects[node.Id].TotalResearchPoints;

        Near(economy.Credits, startingCredits - quote.OperatingCreditsPerDay * 10.0,
            "fully funded research did not deduct its exact operating expense");
        Near(economy.LastResearchSpendingPerDay, quote.OperatingCreditsPerDay,
            "economy did not expose the funded research burn rate");
        Near(economy.LastResearchFundingFraction, 1.0,
            "fully funded research did not report complete funding");
        Require(fundedRp > startingRp, "fully funded research did not advance RP");

        economy.Credits = 0.0;
        var shortfallEvents = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 10.0, currentSimulationDay: 20.0);
        Near(state.ActiveProjects[node.Id].TotalResearchPoints, fundedRp,
            "unfunded research advanced RP");
        Near(economy.LastResearchSpendingPerDay, 0.0,
            "unfunded research reported money it did not spend");
        Near(economy.LastResearchFundingFraction, 0.0,
            "unfunded research did not report its funding stall");
        Require(shortfallEvents.Count(value => value.CivilizationId == playerId &&
                value.Message.Contains("funding shortfall", StringComparison.Ordinal)) == 1,
            "research funding shortfall did not emit one useful transition event");

        economy.Credits = quote.OperatingCreditsPerDay * 5.0;
        var stillUnderfundedEvents = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 10.0, currentSimulationDay: 30.0);
        var partiallyFundedRp = state.ActiveProjects[node.Id].TotalResearchPoints;
        Require(partiallyFundedRp > fundedRp,
            "partially funded research made no progress");
        Require(partiallyFundedRp - fundedRp < fundedRp - startingRp,
            "partially funded research advanced as quickly as a fully funded interval");
        Near(economy.Credits, 0.0, "partial research funding overspent the treasury");
        Near(economy.LastResearchFundingFraction, 0.5,
            "partial research funding did not report the funded fraction");
        Require(!stillUnderfundedEvents.Any(value => value.CivilizationId == playerId &&
                value.Message.Contains("funding shortfall", StringComparison.Ordinal)),
            "research funding shortfall repeated on every simulation step");

        economy.Credits = quote.OperatingCreditsPerDay * 2.0;
        var restoredEvents = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1.0, currentSimulationDay: 31.0);
        Require(restoredEvents.Count(value => value.CivilizationId == playerId &&
                value.Message.Contains("funding restored", StringComparison.Ordinal)) == 1,
            "fully funded research recovery did not emit one useful transition event");

        var foundation = AdaptiveResearchFundingPolicy.Quote(
            runtime.Authority.Catalog.GetNode("fusion_power"), 4, runtime.Authority.Catalog);
        var developing = AdaptiveResearchFundingPolicy.Quote(
            runtime.Authority.Catalog.GetNode("high_temp_superconductors"), 4, runtime.Authority.Catalog);
        var advanced = AdaptiveResearchFundingPolicy.Quote(
            runtime.Authority.Catalog.GetNode("aneutronic_fusion"), 4, runtime.Authority.Catalog);
        var frontier = AdaptiveResearchFundingPolicy.Quote(
            runtime.Authority.Catalog.GetNode("prototype_warp_drive"), 4, runtime.Authority.Catalog);
        Require(foundation.OperatingCreditsPerDay < developing.OperatingCreditsPerDay &&
                developing.OperatingCreditsPerDay < advanced.OperatingCreditsPerDay &&
                advanced.OperatingCreditsPerDay < frontier.OperatingCreditsPerDay,
            "research operating costs do not rise across complexity bands at equal lab allocation");
        Require(foundation.AuthorizationCredits < developing.AuthorizationCredits &&
                developing.AuthorizationCredits < advanced.AuthorizationCredits &&
                advanced.AuthorizationCredits < frontier.AuthorizationCredits,
            "research authorization costs do not rise across complexity bands");
        Require(foundation.MilestoneCommitmentCredits < developing.MilestoneCommitmentCredits &&
                developing.MilestoneCommitmentCredits < advanced.MilestoneCommitmentCredits &&
                advanced.MilestoneCommitmentCredits < frontier.MilestoneCommitmentCredits,
            "research milestone commitments do not rise across complexity bands");
        Require(foundation.EstimatedTotalOperatingCredits < developing.EstimatedTotalOperatingCredits &&
                developing.EstimatedTotalOperatingCredits < advanced.EstimatedTotalOperatingCredits &&
                advanced.EstimatedTotalOperatingCredits < frontier.EstimatedTotalOperatingCredits,
            "estimated total research costs do not rise across representative complexity bands");
        Require(foundation.EstimatedTotalCredits < developing.EstimatedTotalCredits &&
                developing.EstimatedTotalCredits < advanced.EstimatedTotalCredits &&
                advanced.EstimatedTotalCredits < frontier.EstimatedTotalCredits,
            "estimated combined research costs do not rise across representative complexity bands");

        var finiteRunway = AdaptiveResearchFundingPolicy.EstimateTreasuryRunwayDays(
            availableCredits: 100.0,
            netCreditsPerDayBeforeResearch: 1.0,
            researchOperatingCreditsPerDay: 3.0);
        Near(finiteRunway, 50.0,
            "research treasury runway did not use the net burn after ordinary income and costs");
        Require(double.IsPositiveInfinity(AdaptiveResearchFundingPolicy.EstimateTreasuryRunwayDays(
                availableCredits: 1.0,
                netCreditsPerDayBeforeResearch: 3.0,
                researchOperatingCreditsPerDay: 3.0)),
            "self-sustaining research reported a finite treasury runway");

        var remainingDays = quote.EstimatedYearsAtFullFunding * 365.25;
        economy.Credits = quote.OperatingCreditsPerDay * remainingDays + 100.0;
        var milestoneEvents = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, remainingDays, currentSimulationDay: 31.0 + remainingDays);
        Require(milestoneEvents.Count(value => value.CivilizationId == playerId &&
                value.Message.Contains("Research milestone funded", StringComparison.Ordinal)) == 3,
            "research did not consume one reserved milestone at each maturity boundary");
        Require(!campaign.GetProjectFunding(playerId).ContainsKey(node.Id),
            "mature research retained a spent milestone reserve");
    }

    private static void Near(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > 0.000001)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
