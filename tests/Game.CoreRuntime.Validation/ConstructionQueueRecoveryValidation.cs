using Game.Persistence;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using System.Text.Json.Nodes;

namespace Game.CoreRuntime.Validation;

internal static class ConstructionQueueRecoveryValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        QueueDebitsOnceAndPromotesInOrder();
        CancellationPreservesMaterialAccounting();
        BlockedHeadPreservesFifoAndCancelsIntoRecovery();
        AutomaticOrdersPayAndSkipUnaffordableProjects();
        QueueStatePersistsAndInvalidPayloadFailsClosed();
        Console.WriteLine("PASS: construction queue authorization, recovery and AI fairness");
    }

    private static void QueueDebitsOnceAndPromotesInOrder()
    {
        var galaxy = CreateGalaxy(); var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player); economy.Credits = 1000;
        var simulation = new ConstructionSimulation();
        Require(simulation.StartProject(galaxy, player, "research_network").Accepted, "opening project rejected");
        var afterActive = economy.Credits;
        Require(simulation.QueueProject(galaxy, player, "industrial_automation").Accepted, "queue project rejected");
        RequireNear(economy.Credits, afterActive - ConstructionRegistry.Get("industrial_automation").CreditCost, "queued project was not debited exactly once");
        var state = galaxy.ConstructionStates.Single(s => s.CivilizationId == player);
        economy.Industry = 10_000;
        simulation.AdvanceForCivilization(galaxy, player, 10_000, 100);
        Require(state.CompletedProjectIds.Contains("research_network") && state.ActiveProjectId == "industrial_automation", "queue did not promote FIFO after completion");
        RequireNear(economy.Credits, afterActive - ConstructionRegistry.Get("industrial_automation").CreditCost, "promotion charged authorization a second time");
        RequireNear(state.ActiveProjectProgress, 0, "promotion consumed a second project in the completion tick");
    }

    private static void CancellationPreservesMaterialAccounting()
    {
        var galaxy = CreateGalaxy(); var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player); economy.Credits = 1000;
        var simulation = new ConstructionSimulation();
        Require(simulation.StartProject(galaxy, player, "research_network").Accepted, "opening project rejected");
        var state = galaxy.ConstructionStates.Single(s => s.CivilizationId == player);
        state.ActiveProjectProgress = 350;
        var industryBeforeCancellation = economy.Industry;
        var before = economy.Credits;
        var cancelled = simulation.CancelProject(galaxy, player, "research_network");
        Require(cancelled.Accepted, "active cancellation rejected");
        RequireNear(cancelled.RefundedCredits, 75, "partial active cancellation refunded the wrong authorization fraction");
        RequireNear(economy.Credits, before + 75, "active cancellation credit accounting drifted");
        RequireNear(economy.Industry, industryBeforeCancellation, "cancellation refunded consumed materials");
        Require(state.ActiveProjectId is null && state.ActiveProjectProgress == 0, "cancelled project remained active");
        Require(simulation.StartProject(galaxy, player, "research_network").Accepted, "restart rejected");
        state.ActiveProjectAuthorizationCredits = 0; state.ActiveProjectProgress = 350;
        RequireNear(simulation.CancelProject(galaxy, player, "research_network").RefundedCredits, 0, "legacy active project minted a refund");
    }

    private static void BlockedHeadPreservesFifoAndCancelsIntoRecovery()
    {
        var galaxy = CreateGalaxy(); var player = galaxy.PlayerCivilizationId;
        var state = galaxy.ConstructionStates.Single(s => s.CivilizationId == player);
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player); economy.Credits = 1000;
        state.QueuedProjects.Add(new("orbital_shipyard", 350));
        state.QueuedProjects.Add(new("research_network", 150));
        var simulation = new ConstructionSimulation(); var before = economy.Credits;
        Require(!simulation.StartProject(galaxy, player, "research_network").Accepted, "blocked queued head was bypassed by direct start");
        Require(!simulation.QueueProject(galaxy, player, "orbital_shipyard").Accepted, "duplicate blocked head was accepted");
        RequireNear(economy.Credits, before, "rejected blocked-head request debited credits");
        Require(simulation.GetQueueBlockerReason(galaxy, player) is not null, "blocked queue head has no visible cause");
        Require(simulation.CancelProject(galaxy, player, "orbital_shipyard").Accepted, "blocked head cancellation rejected");
        Require(state.ActiveProjectId == "research_network" && state.QueuedProjects.Count == 0, "cancelling blocked head did not promote next FIFO order");
    }

    private static void AutomaticOrdersPayAndSkipUnaffordableProjects()
    {
        var galaxy = CreateGalaxy();
        var civilization = galaxy.Civilizations.First(c => !c.IsPlayer && !c.IsSeededAncient);
        var economy = galaxy.Economies.Single(e => e.CivilizationId == civilization.Id);
        var state = galaxy.ConstructionStates.Single(s => s.CivilizationId == civilization.Id);
        economy.Credits = 0;
        var simulation = new ConstructionSimulation(); simulation.EnsureAutomaticOrders(galaxy);
        Require(state.ActiveProjectId is null, "AI received a free construction project without funds");
        economy.Credits = 200;
        simulation.EnsureAutomaticOrders(galaxy);
        Require(state.ActiveProjectId is not null && economy.Credits < 200, "AI did not authorize an affordable project through the paid path");
        RequireNear(state.ActiveProjectAuthorizationCredits, ConstructionRegistry.Get(state.ActiveProjectId!).CreditCost, "AI active order did not retain its exact paid quote");
        var afterOrder = economy.Credits; simulation.EnsureAutomaticOrders(galaxy);
        RequireNear(economy.Credits, afterOrder, "repeated automatic ordering debited an already active AI project");
    }

    private static void QueueStatePersistsAndInvalidPayloadFailsClosed()
    {
        var galaxy = CreateGalaxy(); var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player); economy.Credits = 1000;
        var simulation = new ConstructionSimulation();
        Require(simulation.StartProject(galaxy, player, "research_network").Accepted && simulation.QueueProject(galaxy, player, "industrial_automation").Accepted, "queue setup rejected");
        var path = Path.Combine(Path.GetTempPath(), $"stellar-construction-{Guid.NewGuid():N}.json");
        try
        {
            var saves = new CampaignSaveService(); saves.Save(path, galaxy, 4);
            var restored = saves.Load(path).Galaxy.ConstructionStates.Single(s => s.CivilizationId == player);
            Require(restored.QueuedProjects.Single().ProjectId == "industrial_automation" && restored.ActiveProjectAuthorizationCredits == 150, "queue authorization did not survive save/load");
            var valid = JsonNode.Parse(File.ReadAllText(path))!;
            AssertInvalid(valid, path, saves, root => Queue(root)[0]!["ProjectId"] = "unknown_project", "unknown queued project");
            AssertInvalid(valid, path, saves, root => Queue(root)[0] = null, "null queued entry");
            AssertInvalid(valid, path, saves, root => Queue(root)[0]!["AuthorizationCredits"] = -1.0, "negative queued authorization");
            AssertInvalid(valid, path, saves, root => Queue(root).Add(Queue(root)[0]!.DeepClone()), "duplicate queued project");
            AssertInvalid(valid, path, saves, root => Queue(root)[0]!["ProjectId"] = Active(root), "active/queued overlap");
            AssertInvalid(valid, path, saves, root =>
            {
                var queue = Queue(root);
                while (queue.Count <= ConstructionState.MaxQueuedProjects) queue.Add(queue[0]!.DeepClone());
            }, "excess queued projects");
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }

    private static JsonArray Queue(JsonNode root) => Construction(root)["QueuedProjects"]!.AsArray();
    private static string Active(JsonNode root) => Construction(root)["ActiveProjectId"]!.GetValue<string>();
    private static JsonObject Construction(JsonNode root) => root["Galaxy"]!["ConstructionStates"]!.AsArray()
        .OfType<JsonObject>().First(item => item["QueuedProjects"]?.AsArray().Count > 0);
    private static void AssertInvalid(JsonNode valid, string path, CampaignSaveService saves,
        Action<JsonNode> corrupt, string description)
    {
        var payload = valid.DeepClone(); corrupt(payload);
        File.WriteAllText(path, payload.ToJsonString());
        try { saves.Load(path); throw new InvalidOperationException($"{description} loaded"); }
        catch (InvalidDataException) { }
    }

    private static Game.Simulation.Models.GalaxyState CreateGalaxy() => new GalaxyGenerator().Generate(77123,
        new GalaxyGenerationSettings { SystemCount = 18, PreWarpCivilizationCount = 4, AncientCivilizationCount = 0, Radius = 240 });
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void RequireNear(double actual, double expected, string message) { if (Math.Abs(actual - expected) > .0001) throw new InvalidOperationException($"{message}: {actual} != {expected}"); }
}
