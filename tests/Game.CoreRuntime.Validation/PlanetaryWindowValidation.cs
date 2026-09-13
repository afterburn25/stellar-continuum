using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class PlanetaryWindowValidation
{
    [Game.Validation.RegressionCheck]
    public static void Run()
    {
        var galaxy = new GalaxyGenerator().Generate(2026090817,
            new GalaxyGenerationSettings { SystemCount = 48, PreWarpCivilizationCount = 4, AncientCivilizationCount = 1, Radius = 600 });
        var player = galaxy.PlayerCivilizationId;
        var colony = galaxy.Colonies.Where(c => c.CivilizationId == player).MaxBy(c => c.PopulationMillions)!;
        var economy = galaxy.Economies.Single(e => e.CivilizationId == player);
        economy.Credits = 100000; economy.Industry = 100000; economy.LastBaseOperationsFundingFraction = 1;
        var capabilities = new Capabilities(); var checks = 0;
        void Check(bool passed, string label) { if (!passed) throw new InvalidOperationException(label); checks++; }
        void Rejected(Func<ConstructionOrderResult> action, string label)
        {
            var credits = economy.Credits; var industry = economy.Industry; var buildings = JsonSerializer.Serialize(colony.SurfaceBuildings);
            Check(!action().Accepted, label);
            Check(credits == economy.Credits && industry == economy.Industry && buildings == JsonSerializer.Serialize(colony.SurfaceBuildings), label + " is atomic");
        }
        colony.SurfaceHubLevel = 0;
        Check(SurfaceConstruction.GetBuildingCapacity(colony) == 0, "No slots without a completed Command Center");
        Rejected(() => SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, 0, "power_generator"), "Slot construction requires a Command Center");
        Rejected(() => SurfaceConstruction.Place(galaxy, player, colony.Id, "power_generator", 100, 100, 0), "Legacy terrain API also requires the Command Center");
        Check(SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "A Command Center can be built without advanced research");
        var time = colony.SurfaceHubUpgradeDaysRemaining;
        Check(time > 0 && colony.SurfaceHubLevel == 0, "Foundation does not grant slots early");
        Rejected(() => SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities), "Duplicate Command Center order rejected");
        SurfaceConstruction.Advance(galaxy, player, 0, 0);
        Check(colony.SurfaceHubUpgradeDaysRemaining == time, "Paused construction does not advance");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-planetary-slots-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var service = new CampaignSaveService(); var path = Path.Combine(directory, "planet.json");
            service.Save(path, galaxy, 0);
            var restored = service.Load(path).Galaxy.Colonies.Single(c => c.Id == colony.Id);
            Check(restored.SurfaceHubLevel == 0 && restored.SurfaceHubUpgradeDaysRemaining == time, "Unbuilt Command Center and its work survive save/load");
            Check(JsonNode.Parse(File.ReadAllText(path))!["FormatVersion"]!.GetValue<int>() == 18, "New saves identify slot semantics as v18");
            SurfaceConstruction.Advance(galaxy, player, 0, time);
            Check(colony.SurfaceHubLevel == 1 && SurfaceConstruction.GetBuildingCapacity(colony) == 16, "Completing the Command Center unlocks 16 slots");
            Check(SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, 7, "power_generator").Accepted, "An explicit empty slot accepts construction");
            var first = colony.SurfaceBuildings.Single();
            Check(first.SlotIndex == 7 && !first.IsComplete, "An unfinished building reserves the requested slot");
            Rejected(() => SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, 7, "science_lab"), "Occupied slot rejects a duplicate order");
            Rejected(() => SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, 16, "science_lab"), "Locked slot rejects construction");
            Rejected(() => SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, -1, "science_lab"), "Negative slot rejects construction");
            Rejected(() => SurfaceConstruction.BuildInSlot(galaxy, player + 999, colony.Id, 0, "science_lab"), "Foreign planet rejects construction");
            Check(!SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "Tier-2 research/development requirement remains enforced");
            galaxy.ConstructionStates.Single(c => c.CivilizationId == player).CompletedProjectIds.Add("industrial_automation");
            Check(SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "Researched tier-2 upgrade begins");
            Check(SurfaceConstruction.GetBuildingCapacity(colony) == 16, "Upgrade reserves no premature capacity");
            SurfaceConstruction.Advance(galaxy, player, 0, colony.SurfaceHubUpgradeDaysRemaining);
            Check(SurfaceConstruction.GetBuildingCapacity(colony) == 32 && first.SlotIndex == 7, "Upgrade expands capacity without moving buildings");
            colony.SurfaceHubLevel = 3;
            for (var slot = 0; slot < 64; slot++) if (slot != 7)
                Check(SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, slot, "fabricator").Accepted, "Maximum-tier slot " + slot + " accepts a large-footprint building");
            Check(colony.SurfaceBuildings.Select(b => b.SlotIndex).Distinct().Count() == 64, "All 64 slots have distinct assignments");
            SurfaceConstruction.Validate(colony);
            service.Save(path, galaxy, 4);
            restored = service.Load(path).Galaxy.Colonies.Single(c => c.Id == colony.Id);
            Check(restored.SurfaceBuildings.Select(b => b.SlotIndex).SequenceEqual(colony.SurfaceBuildings.Select(b => b.SlotIndex)), "All slot identities persist");
            Check(SurfaceConstruction.Remove(galaxy, player, colony.Id, first.Id).Accepted, "Cancelling releases a slot");
            Check(!SurfaceConstruction.GetBuildingSlots(colony).Values.Contains(7), "Only the removed slot becomes empty");
            Check(SurfaceConstruction.BuildInSlot(galaxy, player, colony.Id, 7, "science_lab").Accepted, "Released slot can be reused");
            var conflict = colony.SurfaceBuildings.Last(); var oldSlot = conflict.SlotIndex; conflict.SlotIndex = colony.SurfaceBuildings.First().SlotIndex;
            var invalid = false; try { SurfaceConstruction.Validate(colony); } catch (InvalidDataException) { invalid = true; }
            Check(invalid, "Duplicate saved slot rejected"); conflict.SlotIndex = oldSlot;

            foreach (var building in colony.SurfaceBuildings) building.SlotIndex = null;
            var before = JsonSerializer.Serialize(colony.SurfaceBuildings); var map = SurfaceConstruction.GetBuildingSlots(colony);
            Check(map.Count == 64 && map.Values.Distinct().Count() == 64 && before == JsonSerializer.Serialize(colony.SurfaceBuildings), "Legacy slot projection is deterministic and read-only");
            service.Save(path, galaxy, 4);
            var legacy = JsonNode.Parse(File.ReadAllText(path))!; legacy["FormatVersion"] = 16;
            foreach (var c in legacy["Galaxy"]!["Colonies"]!.AsArray())
                foreach (var b in c!["SurfaceBuildings"]!.AsArray()) b!.AsObject().Remove("SlotIndex");
            File.WriteAllText(path, legacy.ToJsonString());
            restored = service.Load(path).Galaxy.Colonies.Single(c => c.Id == colony.Id);
            Check(restored.SurfaceBuildings.Count == 64 && restored.SurfaceHubLevel == 3 && restored.SurfaceBuildings[0].X == colony.SurfaceBuildings[0].X, "v16 free-placement save retains buildings, positions and capacity");
            var keep = colony.SurfaceBuildings.Last(); var keepSlot = map[keep.Id];
            SurfaceConstruction.Remove(galaxy, player, colony.Id, colony.SurfaceBuildings.First().Id);
            Check(keep.SlotIndex == keepSlot, "Removing a legacy building does not renumber the remaining slots");
            var total = EconomySimulation.GetCreditFlow(galaxy, player);
            var local = galaxy.Colonies.Where(c => c.CivilizationId == player).Select(c => EconomySimulation.GetColonyCreditFlow(galaxy, c)).ToArray();
            Check(Math.Abs(local.Sum(f => f.GrossIncomePerDay) - total.GrossIncomePerDay) < 1e-8, "Displayed planetary income reconciles to the empire");
            Check(Math.Abs(local.Sum(f => f.OperatingCostsPerDay) - (total.OperatingCostsPerDay - total.FleetOperationsPerDay - total.OrbitalMaintenancePerDay - total.ResearchOperationsPerDay)) < 1e-8, "Planetary costs reconcile without attributing empire obligations locally");
            var campaignService = new CampaignStatePersistenceService();
            var campaignPath = Path.Combine(directory, "campaign.json");
            campaignService.Save(campaignPath, galaxy, 4, new Game.Simulation.Diplomacy.DiplomacyState());
            var campaignDocument = JsonNode.Parse(File.ReadAllText(campaignPath))!;
            Check(campaignDocument["FormatVersion"]!.GetValue<int>() == 19 && campaignDocument["GalaxyFormatVersion"]!.GetValue<int>() == 18, "Current campaign wrapper identifies planetary slot saves");
            campaignDocument["FormatVersion"] = 17; campaignDocument["GalaxyFormatVersion"] = 16;
            foreach (var c in campaignDocument["Galaxy"]!["Colonies"]!.AsArray())
                foreach (var b in c!["SurfaceBuildings"]!.AsArray()) b!.AsObject().Remove("SlotIndex");
            File.WriteAllText(campaignPath, campaignDocument.ToJsonString());
            Check(campaignService.Load(campaignPath).Galaxy.Colonies.Single(c => c.Id == colony.Id).SurfaceBuildings.Count == 63, "Historical v17 campaign wrapper retains its v16 colony buildings");
            colony.SurfaceBuildings.Clear(); colony.SurfaceHubLevel = 0; colony.Kind = SettlementKind.ResourceOutpost;
            Check(SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "A new resource outpost can construct its Command Center");
            SurfaceConstruction.Advance(galaxy, player, 0, colony.SurfaceHubUpgradeDaysRemaining);
            Check(SurfaceConstruction.GetBuildingCapacity(colony) == 8 && !SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "Outpost Command Center unlocks eight slots and retains the colony-conversion requirement");
            Console.WriteLine($"PASS: Planetary Command Center and slots ({checks} checks)");
        }
        finally
        {
            if (Path.GetDirectoryName(Path.GetFullPath(directory)) != Path.GetTempPath().TrimEnd(Path.DirectorySeparatorChar)) throw new InvalidOperationException("Unexpected test cleanup path");
            Directory.Delete(directory, true);
        }
    }
    private sealed class Capabilities : IConstructionCapabilityView
    { public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) => false; }
}
