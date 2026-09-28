using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Generation;
using Game.Simulation.Shipbuilding;

namespace Game.CoreRuntime.Validation;

internal static class ShipyardCancellationRecoveryValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        PaidCancellationAndExactSource();
        IdentityPromotionAndReload();
        InvalidSourcesRejectAtomicallyAndCompletionWorks();
        PersistenceAndCounterValidation();
        Console.WriteLine("PASS: shipyard cancellation preserves quotes, identities, materials and reserved people");
    }

    private static void PaidCancellationAndExactSource()
    {
        var galaxy = ReadyGalaxy(); var player = galaxy.PlayerCivilizationId; var simulation = new ShipbuildingSimulation();
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player);
        var source = galaxy.Colonies.Where(c => c.CivilizationId == player).MaxBy(c => c.PopulationMillions)!;
        var sourceBefore = source.PopulationMillions;
        Require(simulation.StartBuild(galaxy, player, ShipDesignRegistry.ColonyShipId).Accepted, "real colony order rejected");
        Require(simulation.StartBuild(galaxy, player, ShipDesignRegistry.ResourceOutpostShipId).Accepted, "real outpost queue rejected");
        var state = galaxy.ShipyardStates.Single(s => s.CivilizationId == player); var queued = state.QueuedBuilds.Single();
        Require(state.ReservedPopulationSourceColonyId == source.Id && queued.ReservedPopulationSourceColonyId == source.Id, "exact source was not recorded");
        var queuedCredits = economy.Credits; var queuedPaid = queued.AuthorizationCredits;
        var queuedResult = simulation.CancelBuild(galaxy, player, queued.OrderId);
        Require(queuedResult.Accepted, "queued cancellation rejected");
        Near(queuedResult.RefundedCredits, queuedPaid, "queued quote refund"); Near(economy.Credits, queuedCredits + queuedPaid, "queued treasury refund");
        Near(source.PopulationMillions, sourceBefore - ShipDesignRegistry.Get(ShipDesignRegistry.ColonyShipId).PopulationCostMillions, "outpost population return");

        var design = ShipDesignRegistry.Get(state.ActiveDesignId!); var paid = state.ActiveAuthorizationCredits;
        simulation.AdvanceForCivilization(galaxy, player, design.IndustryCost / 4, design.IndustryCost / ShipbuildingSimulation.IndustryPerDay);
        var industryAtCancellation = economy.Industry;
        var expected = paid * (design.IndustryCost - state.ActiveBuildProgress) / design.IndustryCost;
        var activeResult = simulation.CancelBuild(galaxy, player, state.ActiveOrderId!);
        Require(activeResult.Accepted, "active cancellation rejected"); Near(activeResult.RefundedCredits, expected, "active unused fraction");
        Near(source.PopulationMillions, sourceBefore, "active population return"); Near(economy.Industry, industryAtCancellation, "cancellation refunded materials");
    }

    private static void IdentityPromotionAndReload()
    {
        var galaxy = ReadyGalaxy(); var player = galaxy.PlayerCivilizationId; var simulation = new ShipbuildingSimulation();
        Require(simulation.StartBuild(galaxy, player, "warp_scout").Accepted && simulation.StartBuild(galaxy, player, "warp_scout").Accepted, "same-design orders rejected");
        var state = galaxy.ShipyardStates.Single(s => s.CivilizationId == player);
        var first = state.ActiveOrderId!; var second = state.QueuedBuilds.Single().OrderId; Require(first != second, "same-design identity collision");
        var before = galaxy.Economies.Single(e => e.CivilizationId == player).Credits;
        Require(simulation.CancelBuild(galaxy, player, first).Accepted && state.ActiveOrderId == second, "exact queued identity was not promoted");
        Near(galaxy.Economies.Single(e => e.CivilizationId == player).Credits, before + 70, "promotion charged twice");
        Require(!simulation.CancelBuild(galaxy, player, first).Accepted, "stale identity cancelled promoted order");
        WithSave((saves, path) =>
        {
            saves.Save(path, galaxy, 7); var restored = saves.Load(path).Galaxy;
            Require(restored.ShipyardStates.Single(s => s.CivilizationId == player).ActiveOrderId == second, "identity changed on reload");
            Require(!simulation.CancelBuild(restored, player, first).Accepted && simulation.CancelBuild(restored, player, second).Accepted, "reload identity targeting failed");
        });
    }

    private static void InvalidSourcesRejectAtomicallyAndCompletionWorks()
    {
        foreach (var failure in new[] { "missing", "foreign", "species" })
        {
            var galaxy = ReadyGalaxy(); var player = galaxy.PlayerCivilizationId; var simulation = new ShipbuildingSimulation();
            Require(simulation.StartBuild(galaxy, player, ShipDesignRegistry.ResourceOutpostShipId).Accepted, "population order rejected");
            var state = galaxy.ShipyardStates.Single(s => s.CivilizationId == player); var economy = galaxy.Economies.Single(e => e.CivilizationId == player);
            var source = galaxy.Colonies.Single(c => c.Id == state.ReservedPopulationSourceColonyId); var population = source.PopulationMillions;
            var credits = economy.Credits; var id = state.ActiveOrderId!;
            if (failure == "missing") galaxy.Colonies.Remove(source);
            else if (failure == "foreign") source = ReplaceOwner(galaxy, source, galaxy.Civilizations.First(c => c.Id != player).Id);
            else source.PopulationSpeciesId = "mismatched_species";
            Require(!simulation.AssessCancellation(galaxy, player, id).CanCancel && !simulation.CancelBuild(galaxy, player, id).Accepted,
                $"{failure} source cancellation accepted");
            Require(state.ActiveOrderId == id && economy.Credits == credits, $"{failure} rejection mutated order or credits");
            if (failure != "missing") Near(source.PopulationMillions, population, $"{failure} rejection mutated people");
            if (failure is "missing" or "foreign") WithSave((saves, path) =>
            {
                saves.Save(path, galaxy, 4); var restored = saves.Load(path).Galaxy;
                Require(!simulation.AssessCancellation(restored, player, id).CanCancel,
                    $"historical {failure} source was not loadable and cancellation-blocked");
            });
            state.ActiveBuildProgress = ShipDesignRegistry.Get(state.ActiveDesignId!).IndustryCost; economy.Industry = 1;
            Require(simulation.AdvanceForCivilization(galaxy, player, 1).Count == 1, $"{failure} source blocked completion");
        }

        var legacy = ReadyGalaxy(); var p = legacy.PlayerCivilizationId; var s = legacy.ShipyardStates.Single(x => x.CivilizationId == p);
        s.ActiveDesignId = "warp_scout"; s.ActiveOrderId = "legacy-active"; s.ActiveAuthorizationCredits = 0;
        Require(new ShipbuildingSimulation().CancelBuild(legacy, p, "legacy-active").Accepted, "zero-quote legacy order was not recoverable");
    }

    private static void PersistenceAndCounterValidation()
    {
        var galaxy = ReadyGalaxy(); var player = galaxy.PlayerCivilizationId; var simulation = new ShipbuildingSimulation();
        Require(simulation.StartBuild(galaxy, player, "warp_scout").Accepted && simulation.StartBuild(galaxy, player, "science_vessel").Accepted, "save fixture rejected");
        WithSave((saves, path) =>
        {
            saves.Save(path, galaxy, 2); var valid = JsonNode.Parse(File.ReadAllText(path))!;
            Invalid(valid, path, saves, root => Queue(root)[0]!["OrderId"] = State(root)["ActiveOrderId"]!.GetValue<string>(), "duplicate identity");
            Invalid(valid, path, saves, root => State(root)["NextOrderSequence"] = 1, "counter behind identity");
            Invalid(valid, path, saves, root => State(root)["ActiveAuthorizationCredits"] = -1, "negative quote");
            Invalid(valid, path, saves, root => State(root)["ReservedPopulationMillions"] = -1, "negative identified active population");
            Invalid(valid, path, saves, root => State(root)["ActiveBuildProgress"] = "NaN", "non-finite progress");
            Invalid(valid, path, saves, root => Queue(root)[0]!["ReservedPopulationSourceColonyId"] = -1, "source id shape");
            Invalid(valid, path, saves, root => Queue(root)[0]!["ReservedPopulationMillions"] = -1, "negative identified queued population");
            Invalid(valid, path, saves, root => Queue(root)[0]!["OrderId"] = "bad/path:@\"id", "UI-unsafe identity");
            Invalid(valid, path, saves, root =>
            {
                var queue = Queue(root); var template = queue[0]!.DeepClone();
                while (queue.Count < ShipyardState.MaxPendingBuilds)
                {
                    var item = template.DeepClone(); item!["OrderId"] = $"load-overflow-{queue.Count}"; item["AuthorizationCredits"] = 1; queue.Add(item);
                }
            }, "active plus eight paid queue entries");
            Invalid(valid, path, saves, root =>
            {
                var queue = Queue(root); var template = queue[0]!.DeepClone();
                while (queue.Count <= ShipyardState.MaxPendingBuilds)
                {
                    var item = template.DeepClone(); item!["OrderId"] = $"load-overflow-{queue.Count}"; item["AuthorizationCredits"] = 1; queue.Add(item);
                }
            }, "more than eight paid queue entries");
        });
        var invalidSave = ReadyGalaxy(); var invalidPlayer = invalidSave.PlayerCivilizationId;
        Require(simulation.StartBuild(invalidSave, invalidPlayer, "warp_scout").Accepted, "invalid-save fixture rejected");
        invalidSave.ShipyardStates.Single(x => x.CivilizationId == invalidPlayer).ActiveAuthorizationCredits = double.NaN;
        WithSave((saves, path) =>
        {
            try { saves.Save(path, invalidSave, 0); throw new InvalidOperationException("non-finite runtime accounting saved"); }
            catch (InvalidOperationException error) when (error.Message != "non-finite runtime accounting saved") { }
        });
        var unsafeId = ReadyGalaxy(); var unsafePlayer = unsafeId.PlayerCivilizationId;
        Require(simulation.StartBuild(unsafeId, unsafePlayer, "warp_scout").Accepted, "unsafe-id fixture rejected");
        unsafeId.ShipyardStates.Single(x => x.CivilizationId == unsafePlayer).ActiveOrderId = "bad/path";
        SaveRejected(unsafeId, "UI-unsafe runtime identity saved");

        var overflow = ReadyGalaxy(); var overflowPlayer = overflow.PlayerCivilizationId;
        Require(simulation.StartBuild(overflow, overflowPlayer, "warp_scout").Accepted, "overflow fixture rejected");
        var overflowState = overflow.ShipyardStates.Single(x => x.CivilizationId == overflowPlayer);
        for (var index = 0; index < ShipyardState.MaxPendingBuilds; index++)
            overflowState.QueuedBuilds.Add(new ShipBuildOrderState { OrderId = $"save-overflow-{index}", DesignId = "warp_scout", AuthorizationCredits = 1 });
        SaveRejected(overflow, "active plus eight paid runtime queue entries saved");
        overflowState.QueuedBuilds.Add(new ShipBuildOrderState { OrderId = "save-overflow-extra", DesignId = "warp_scout", AuthorizationCredits = 1 });
        SaveRejected(overflow, "more than eight paid runtime queue entries saved");
        foreach (var counter in new[] { 0L, long.MaxValue })
        {
            var invalid = ReadyGalaxy(); var p = invalid.PlayerCivilizationId; invalid.ShipyardStates.Single(x => x.CivilizationId == p).NextOrderSequence = counter;
            var economy = invalid.Economies.Single(e => e.CivilizationId == p); var credits = economy.Credits;
            var source = invalid.Colonies.Where(c => c.CivilizationId == p).MaxBy(c => c.PopulationMillions)!; var population = source.PopulationMillions;
            Require(!simulation.StartBuild(invalid, p, ShipDesignRegistry.ColonyShipId).Accepted, "invalid counter accepted");
            Near(economy.Credits, credits, "invalid counter debit"); Near(source.PopulationMillions, population, "invalid counter population debit");
        }
        var collision = ReadyGalaxy(); var collisionPlayer = collision.PlayerCivilizationId;
        Require(simulation.StartBuild(collision, collisionPlayer, "warp_scout").Accepted, "collision fixture rejected");
        var collisionState = collision.ShipyardStates.Single(x => x.CivilizationId == collisionPlayer); collisionState.NextOrderSequence--;
        var collisionEconomy = collision.Economies.Single(e => e.CivilizationId == collisionPlayer); var collisionCredits = collisionEconomy.Credits;
        Require(!simulation.StartBuild(collision, collisionPlayer, "science_vessel").Accepted && collisionEconomy.Credits == collisionCredits,
            "identity collision mutated treasury");
        var replay = ReadyGalaxy(); Require(simulation.StartBuild(replay, replay.PlayerCivilizationId, "warp_scout").Accepted, "replay fixture rejected");
        Require(replay.ShipyardStates.Single(x => x.CivilizationId == replay.PlayerCivilizationId).ActiveOrderId ==
                galaxy.ShipyardStates.Single(x => x.CivilizationId == player).ActiveOrderId,
            "deterministic replay produced a different first identity");

        var exhausted = ReadyGalaxy(); var exhaustedPlayer = exhausted.PlayerCivilizationId;
        var exhaustedState = exhausted.ShipyardStates.Single(x => x.CivilizationId == exhaustedPlayer); exhaustedState.NextOrderSequence = long.MaxValue - 1;
        Require(simulation.StartBuild(exhausted, exhaustedPlayer, "warp_scout").Accepted && exhaustedState.NextOrderSequence == long.MaxValue,
            "last representable identity was not allocated into an exhausted counter");
        WithSave((saves, path) =>
        {
            saves.Save(path, exhausted, 9); var restored = saves.Load(path).Galaxy;
            var restoredEconomy = restored.Economies.Single(e => e.CivilizationId == exhaustedPlayer); var credits = restoredEconomy.Credits;
            Require(!simulation.StartBuild(restored, exhaustedPlayer, "science_vessel").Accepted && restoredEconomy.Credits == credits,
                "exhausted counter did not round-trip as a non-mutating allocation blocker");
        });
    }

    private static Game.Simulation.Models.GalaxyState ReadyGalaxy()
    {
        var galaxy = new GalaxyGenerator().Generate(44091, new GalaxyGenerationSettings { SystemCount = 16, PreWarpCivilizationCount = 3, AncientCivilizationCount = 0, Radius = 200 });
        var player = galaxy.PlayerCivilizationId; var economy = galaxy.Economies.Single(e => e.CivilizationId == player); economy.Credits = economy.Industry = 10_000;
        var tech = galaxy.Technologies.Single(t => t.CivilizationId == player); tech.CompletedTechnologyIds.Add("orbital_industry"); tech.CompletedTechnologyIds.Add("prototype_warp_drive");
        galaxy.ConstructionStates.Single(c => c.CivilizationId == player).CompletedProjectIds.Add("orbital_shipyard");
        galaxy.Colonies.Where(c => c.CivilizationId == player).MaxBy(c => c.PopulationMillions)!.PopulationMillions = 10_000; return galaxy;
    }

    private static Game.Simulation.Models.ColonyState ReplaceOwner(Game.Simulation.Models.GalaxyState galaxy, Game.Simulation.Models.ColonyState old, int owner)
    {
        var replacement = new Game.Simulation.Models.ColonyState { Id = old.Id, CivilizationId = owner, SystemId = old.SystemId, PlanetaryBodyId = old.PlanetaryBodyId, Name = old.Name, Kind = old.Kind, PopulationSpeciesId = old.PopulationSpeciesId, PopulationMillions = old.PopulationMillions };
        galaxy.Colonies.Remove(old); galaxy.Colonies.Add(replacement); return replacement;
    }
    private static void WithSave(Action<CampaignSaveService, string> action) { var path = Path.Combine(Path.GetTempPath(), $"stellar-shipyard-{Guid.NewGuid():N}.json"); try { action(new(), path); } finally { if (File.Exists(path)) File.Delete(path); if (File.Exists(path + ".bak")) File.Delete(path + ".bak"); } }
    private static void SaveRejected(Game.Simulation.Models.GalaxyState galaxy, string message) => WithSave((saves, path) => { try { saves.Save(path, galaxy, 0); throw new InvalidOperationException(message); } catch (Exception error) when (error is InvalidOperationException or InvalidDataException && error.Message != message) { } });
    private static JsonObject State(JsonNode root) => root["Galaxy"]!["ShipyardStates"]!.AsArray().OfType<JsonObject>().First(x => x["ActiveDesignId"] is not null);
    private static JsonArray Queue(JsonNode root) => State(root)["QueuedBuilds"]!.AsArray();
    private static void Invalid(JsonNode valid, string path, CampaignSaveService saves, Action<JsonNode> corrupt, string description) { var payload = valid.DeepClone(); corrupt(payload); File.WriteAllText(path, payload.ToJsonString()); try { saves.Load(path); throw new InvalidOperationException($"{description} loaded"); } catch (Exception error) when (error is InvalidDataException or System.Text.Json.JsonException) { } }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Near(double actual, double expected, string message) { if (Math.Abs(actual - expected) > .0001) throw new InvalidOperationException($"{message}: {actual} != {expected}"); }
}
