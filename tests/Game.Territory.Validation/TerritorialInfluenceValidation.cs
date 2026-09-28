using System.Diagnostics;
using System.IO;
using Game.Persistence;
using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Territory;
using Game.Validation;

namespace Game.Territory.Validation;

internal static class TerritorialInfluenceValidation
{
    [RegressionCheck]
    private static void PermanentAnchorsFallOffAndNeverInventHomes()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, TerritoryScenario.System(galaxy, 0)));
        var runtime = TerritorialRuntime.Initialize(galaxy);
        var near = Score(runtime, owner, TerritoryScenario.System(galaxy, 1));
        var far = Score(runtime, owner, TerritoryScenario.System(galaxy, 5));
        Require(near.Political > far.Political && near.Administration > far.Administration, "a colony did not produce bounded distance falloff");
        galaxy.Colonies.Clear(); runtime.Advance(galaxy, .1);
        Require(runtime.Systems.Values.All(s => s.Civilizations.All(c => c.Political == 0)), "a civilization without a represented colony or installation projected a phantom home territory");
        galaxy.Colonies.Add(TerritoryScenario.Outpost(galaxy, owner, TerritoryScenario.System(galaxy, 0)));
        runtime.Advance(galaxy, .1);
        var outpost = Score(runtime, owner, TerritoryScenario.System(galaxy, 1));
        galaxy.Colonies[0].Kind = SettlementKind.Colony; galaxy.Colonies[0].PopulationMillions = 600; runtime.Advance(galaxy, .1);
        Require(Score(runtime, owner, TerritoryScenario.System(galaxy, 1)).Political > outpost.Political,
            "upgrading an outpost to a populated colony did not increase permanent influence");
    }

    [RegressionCheck]
    private static void ControlIsCompetitiveAndMilitaryIsTemporary()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var a = TerritoryScenario.Owner(galaxy); var b = TerritoryScenario.Owner(galaxy, 1);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, a, TerritoryScenario.System(galaxy, 0)));
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, b, TerritoryScenario.System(galaxy, 6)));
        var contestedSystem = TerritoryScenario.System(galaxy, 3);
        var runtime = TerritorialRuntime.Initialize(galaxy);
        var territory = runtime.Systems[contestedSystem];
        Require(territory.Status == TerritorialControlStatus.Contested && territory.ControllerId is null,
            "a close influence tie assigned sovereign control instead of remaining contested");
        var before = Score(runtime, a, contestedSystem).Political;
        galaxy.Fleets.Add(TerritoryScenario.Fleet(galaxy, a, contestedSystem, FleetRole.Military)); runtime.Advance(galaxy, .1);
        var during = Score(runtime, a, contestedSystem);
        Require(Math.Abs(during.Political - before) < .000001 && during.Military > 0, "a military fleet created permanent political sovereignty");
        galaxy.Fleets[0].IsActive = false; runtime.Advance(galaxy, .1);
        Require(Score(runtime, a, contestedSystem).Military == 0, "departed military presence persisted in the territory snapshot");
    }

    [RegressionCheck]
    private static void AdministrationSupplyTradeAndRelayChainsStaySeparate()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, TerritoryScenario.System(galaxy, 0)));
        var runtime = TerritorialRuntime.Initialize(galaxy); var target = TerritoryScenario.System(galaxy, 4);
        var initial = Score(runtime, owner, target);
        AddComplete(galaxy, owner, TerritoryScenario.System(galaxy, 2), TerritorialInstallationKind.Relay);
        AddComplete(galaxy, owner, TerritoryScenario.System(galaxy, 3), TerritorialInstallationKind.SupplyDepot);
        runtime.Advance(galaxy, .1); var connected = Score(runtime, owner, target);
        Require(connected.Administration > initial.Administration && connected.Supply >= initial.Supply, "relay/depot chain did not extend its respective support paths");
        Require(connected.Trade != connected.Supply || connected.Administration != connected.Supply, "administration, supply, and trade were collapsed into one reach score");
        galaxy.Territory!.Installations.RemoveAll(i => i.Kind == TerritorialInstallationKind.Relay); runtime.Advance(galaxy, .1);
        Require(Score(runtime, owner, target).Administration < connected.Administration, "destroying a relay did not invalidate the connected administration chain");
    }

    [RegressionCheck]
    private static void FrontierAndConstructionCommandsEnforceRealPrerequisites()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy); var home = TerritoryScenario.System(galaxy, 0); var remote = TerritoryScenario.System(galaxy, 6);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, home));
        var colonyShip = TerritoryScenario.Fleet(galaxy, owner, remote, FleetRole.Colony); galaxy.Fleets.Add(colonyShip);
        var runtime = TerritorialRuntime.Initialize(galaxy);
        var quote = TerritorialExpansion.Quote(galaxy, colonyShip, remote);
        Require(!quote.Allowed && quote.Region == ExpansionRegion.Remote && quote.Reason.Contains("relay", StringComparison.OrdinalIgnoreCase), "unsupported remote settlement was accepted without an actionable support diagnosis");
        var target = TerritoryScenario.System(galaxy, 1);
        var noShip = TerritorialConstruction.Preview(galaxy, owner, target, TerritorialInstallationKind.Relay);
        Require(!noShip.Accepted && noShip.Message.Contains("logistics", StringComparison.OrdinalIgnoreCase), "station construction ignored the on-site logistics requirement");
        var builder = TerritoryScenario.Fleet(galaxy, owner, target, FleetRole.Logistics); galaxy.Fleets.Add(builder);
        var beforeCredits = galaxy.Economies.Single(e => e.CivilizationId == owner).Credits;
        var started = TerritorialConstruction.Start(galaxy, owner, target, TerritorialInstallationKind.Relay);
        Require(started.Accepted && started.InstallationId is not null, "paid station construction was rejected with industry, technology, survey, and logistics present");
        runtime.Advance(galaxy, 2); var site = galaxy.Territory!.Installations.Single(); var progress = site.CompletedDays;
        builder.CurrentSystemId = home; runtime.Advance(galaxy, 3);
        Require(site.CompletedDays == progress, "construction continued after its required logistics ship left the site");
        var cancelled = TerritorialConstruction.Remove(galaxy, owner, site.Id); var credits = galaxy.Economies.Single(e => e.CivilizationId == owner).Credits;
        Require(cancelled.Accepted && credits > beforeCredits - TerritorialBalance.Definition(TerritorialInstallationKind.Relay).Credits,
            "cancellation did not return the documented unspent capital");
        Require(!TerritorialConstruction.Remove(galaxy, owner, site.Id).Accepted && galaxy.Economies.Single(e => e.CivilizationId == owner).Credits == credits,
            "a repeated cancellation refunded capital twice");
    }

    [RegressionCheck]
    private static void PersistenceAndScheduledReadsAreDeterministic()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy); var home = TerritoryScenario.System(galaxy, 0);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, home)); AddComplete(galaxy, owner, TerritoryScenario.System(galaxy, 1), TerritorialInstallationKind.Relay);
        var runtime = TerritorialRuntime.Initialize(galaxy); var baseline = Score(runtime, owner, TerritoryScenario.System(galaxy, 2)); var count = runtime.RecomputeCount;
        for (var i = 0; i < 100; i++) _ = runtime.Read(owner, TerritoryScenario.System(galaxy, 2));
        Require(runtime.RecomputeCount == count, "read-only territory access recalculated simulation state");
        runtime.Advance(galaxy, TerritorialBalance.ReviewDays - .01); Require(runtime.RecomputeCount == count, "territory recalculated before its five-day review");
        runtime.Advance(galaxy, .02); Require(runtime.RecomputeCount == count + 1, "territory did not recalculate on the five-day scheduler independently of frame cadence");
        var captured = TerritorialPersistence.Capture(galaxy)!;
        galaxy.Territory!.Installations[0].CompletedDays = 0;
        Require(captured.Installations[0].IsComplete, "territorial capture retained mutable installation references");
        galaxy.Territory = captured; TerritorialPersistence.Validate(galaxy); runtime.Recompute(galaxy);
        Require(Math.Abs(Score(runtime, owner, TerritoryScenario.System(galaxy, 2)).Political - baseline.Political) < .000001, "round-tripped persistent territory facts reconstructed different derived influence");
        galaxy.Territory.Installations.Add(new TerritorialInstallation { Id = captured.Installations[0].Id, CivilizationId = owner, SystemId = home, Kind = TerritorialInstallationKind.Relay, RequiredDays = 1 });
        RequireThrows(() => TerritorialPersistence.Validate(galaxy), "duplicate territorial installation data was silently accepted");
    }

    [RegressionCheck]
    private static void CompactAndRapidCompetitiveCampaignsConverge()
    {
        GalaxyState Build()
        {
            var galaxy = TerritoryScenario.AnchoredLine();
            galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, TerritoryScenario.Owner(galaxy), TerritoryScenario.System(galaxy, 0)));
            galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, TerritoryScenario.Owner(galaxy, 1), TerritoryScenario.System(galaxy, 6)));
            AddComplete(galaxy, TerritoryScenario.Owner(galaxy), TerritoryScenario.System(galaxy, 2), TerritorialInstallationKind.Relay);
            AddComplete(galaxy, TerritoryScenario.Owner(galaxy, 1), TerritoryScenario.System(galaxy, 4), TerritorialInstallationKind.Relay);
            return galaxy;
        }
        var compact = Build(); var rapid = Build(); var compactRuntime = TerritorialRuntime.Initialize(compact); var rapidRuntime = TerritorialRuntime.Initialize(rapid);
        compactRuntime.Advance(compact, 120);
        for (var day = 0; day < 120; day++) rapidRuntime.Advance(rapid, 1);
        var middle = TerritoryScenario.System(compact, 3);
        var compactScore = Score(compactRuntime, TerritoryScenario.Owner(compact), middle);
        var rapidScore = Score(rapidRuntime, TerritoryScenario.Owner(rapid), TerritoryScenario.System(rapid, 3));
        Require(Math.Abs(compactScore.Political - rapidScore.Political) < .000001 && Math.Abs(compactScore.Administration - rapidScore.Administration) < .000001,
            "rapid and compact long-running competitive simulations produced different territory results");
        foreach (var colony in compact.Colonies.Where(c => c.CivilizationId == TerritoryScenario.Owner(compact)).ToArray()) compact.Colonies.Remove(colony);
        compact.Territory!.Installations.RemoveAll(i => i.CivilizationId == TerritoryScenario.Owner(compact)); compactRuntime.Advance(compact, .1);
        Require(Score(compactRuntime, TerritoryScenario.Owner(compact), middle).Political == 0, "removing a permanent source did not invalidate long-running territorial influence");
    }

    [RegressionCheck]
    private static void FrontierColonyAuthorizationSurvivesCampaignSave()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, TerritoryScenario.System(galaxy, 0)));
        var runtime = TerritorialRuntime.Initialize(galaxy);
        var target = galaxy.Systems.First(s => runtime.Read(owner, s.Id)?.Expansion == ExpansionRegion.Frontier &&
            galaxy.PlanetaryBodies.Any(b => b.SystemId == s.Id && b.LegacyColonizationCandidate && b.Environment.HasSolidSurface));
        var fleet = TerritoryScenario.Fleet(galaxy, owner, target.Id, FleetRole.Colony); galaxy.Fleets.Add(fleet);
        var body = galaxy.PlanetaryBodies.First(b => b.SystemId == target.Id && b.LegacyColonizationCandidate && b.Environment.HasSolidSurface);
        Require(TerritorialExpansion.Authorize(galaxy, fleet, target.Id, body.Id, out var reason), "supported frontier colony authorization was rejected: " + reason);
        var required = TerritorialExpansion.RequiredDays(galaxy, fleet);
        Require(required > 30, "frontier authorization did not increase the 30-day colony establishment time");
        fleet.DestinationPlanetaryBodyId = body.Id; fleet.SettlementBodyId = body.Id; fleet.SettlementDaysCompleted = 31;
        fleet.EmbarkedPopulationMillions = 5; fleet.EmbarkedPopulationSpeciesId = galaxy.Civilizations.Single(c => c.Id == owner).SpeciesId;
        var path = Path.Combine(Path.GetTempPath(), "territorial-frontier-" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            new CampaignSaveService().Save(path, galaxy, 40);
            var loaded = new CampaignSaveService().Load(path).Galaxy;
            var restored = loaded.Fleets.Single(f => f.Id == fleet.Id);
            Require(Math.Abs(TerritorialExpansion.RequiredDays(loaded, restored) - required) < .000001,
                "save/load changed the paid frontier establishment duration");
            Require(restored.SettlementDaysCompleted == 31, "save/load lost frontier establishment work beyond the base 30-day duration");
            new ColonizationSimulation().Advance(loaded, 1);
            Require(restored.SettlementDaysCompleted > 31 || !restored.IsActive,
                "loaded frontier colony mission could not continue its persisted establishment work");
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }

    [RegressionCheck]
    private static void ConstructionAndLegacyExpeditionsRespectCommittedCapital()
    {
        var galaxy = TerritoryScenario.AnchoredLine(); var owner = TerritoryScenario.Owner(galaxy); var home = TerritoryScenario.System(galaxy, 0); var siteSystem = TerritoryScenario.System(galaxy, 1);
        galaxy.Colonies.Add(TerritoryScenario.Colony(galaxy, owner, home));
        var builder = TerritoryScenario.Fleet(galaxy, owner, siteSystem, FleetRole.Logistics); galaxy.Fleets.Add(builder); TerritorialRuntime.Initialize(galaxy);
        builder.FreightHomeColonyId = 1;
        Require(!TerritorialConstruction.Preview(galaxy, owner, siteSystem, TerritorialInstallationKind.Relay).Accepted, "a logistics ship assigned to a freight home was diverted into construction");
        builder.FreightHomeColonyId = null; builder.FreightTargetOutpostId = 1;
        Require(!TerritorialConstruction.Preview(galaxy, owner, siteSystem, TerritorialInstallationKind.Relay).Accepted, "a logistics ship assigned to an outpost route was diverted into construction");
        builder.FreightTargetOutpostId = null;
        var started = TerritorialConstruction.Start(galaxy, owner, siteSystem, TerritorialInstallationKind.Relay);
        Require(started.Accepted, "unassigned on-site logistics ship could not start construction");
        var project = galaxy.Territory!.Installations.Single(); builder.FreightHomeColonyId = 1;
        TerritorialConstruction.Advance(galaxy, 3); Require(project.CompletedDays == 0, "assigned freight ship continued regional construction");
        builder.FreightHomeColonyId = null; galaxy.Economies.Single(e => e.CivilizationId == owner).LastBaseOperationsFundingFraction = 0;
        TerritorialConstruction.Advance(galaxy, 3); Require(project.CompletedDays == 0, "unfunded regional construction continued work");

        var legacy = TerritoryScenario.AnchoredLine(); owner = TerritoryScenario.Owner(legacy); var target = TerritoryScenario.System(legacy, 1);
        var colonyFleet = TerritoryScenario.Fleet(legacy, owner, target, FleetRole.Colony); var body = legacy.PlanetaryBodies.First(b => b.SystemId == target);
        colonyFleet.DestinationSystemId = target; colonyFleet.DestinationPlanetaryBodyId = body.Id; colonyFleet.TransitPhase = FleetTransitPhase.InterstellarWarp; colonyFleet.PreventAutomaticSettlement = false;
        legacy.Fleets.Add(colonyFleet); var treasury = legacy.Economies.Single(e => e.CivilizationId == owner); var credits = treasury.Credits;
        TerritorialRuntime.Initialize(legacy);
        Require(legacy.Territory!.Expeditions.Single(e => e.FleetId == colonyFleet.Id).PaidCredits == ColonizationSimulation.ColonyExpeditionCreditCost && treasury.Credits == credits,
            "legacy in-flight colony expedition was debited again during territorial initialization");
        TerritorialRuntime.Initialize(legacy); Require(legacy.Territory.Expeditions.Count == 1, "legacy expedition grandfathering duplicated an existing paid authorization");
    }

    [RegressionCheck]
    private static void BalanceDataRejectsMissingAndInvalidValues()
    {
        RequireThrows(() => TerritorialTuning.Parse("{}"), "balance parser accepted missing named values");
        var json = File.ReadAllText(Path.Combine("data", "territory", "balance-v1.json"));
        RequireThrows(() => TerritorialTuning.Parse(json.Replace("\"ReviewDays\": 5", "\"ReviewDays\": 0", StringComparison.Ordinal)),
            "balance parser accepted a non-positive review interval");
        Require(TerritorialTuning.Parse(json).Installations.Length == Enum.GetValues<TerritorialInstallationKind>().Length,
            "packaged balance data did not provide every installation definition");
    }

    private static CivilizationSystemTerritory Score(TerritorialRuntime runtime, int owner, int system) => runtime.Read(owner, system) ?? throw new InvalidOperationException("missing territorial score");
    private static void AddComplete(GalaxyState galaxy, int owner, int system, TerritorialInstallationKind kind)
    {
        galaxy.Territory ??= new TerritorialState(); var definition = TerritorialBalance.Definition(kind);
        galaxy.Territory.Installations.Add(new TerritorialInstallation { Id = galaxy.Territory.Installations.Count + 1, CivilizationId = owner, SystemId = system, Kind = kind, RequiredDays = definition.Days, CompletedDays = definition.Days, PaidCredits = definition.Credits, PaidIndustry = definition.Industry });
    }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void RequireThrows(Action action, string message) { try { action(); } catch (InvalidDataException) { return; } throw new InvalidOperationException(message); }
}
