using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class LeadershipPersistenceValidation
{
    public static void Run()
    {
        var campaign = new CampaignSessionService().CreateNew(9201080L);
        var own = campaign.Galaxy.Civilizations.Single(c => c.Id == campaign.Galaxy.PlayerCivilizationId);
        Require(own.Leadership.Current("ChiefScientist")?.DisplayName == "Dr. Amara Chen", "Missing founding scientist");
        var other = campaign.Galaxy.Civilizations.First(c => c.Id != own.Id);
        var previousOther = other.Leadership.Current("ChiefScientist");
        own.Leadership.Assign("ChiefScientist", new("scientist-successor", "Dr. Successor", "human_female_diplomat"));
        Require(other.Leadership.Current("ChiefScientist") == previousOther, "Appointment affected another civilization");
        var directory = Path.Combine(Path.GetTempPath(), "stellar-leadership-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var saves = new CampaignSaveService();
            saves.Save(path, campaign.Galaxy, 4);
            var restoredGalaxy = saves.Load(path).Galaxy;
            var restored = restoredGalaxy.Civilizations.Single(c => c.Id == own.Id);
            Require(restored.Leadership.Current("ChiefScientist")?.Id == "scientist-successor" &&
                restored.Leadership.Current("ChiefScientist")?.VoiceProfileId == "human_female_diplomat", "Save lost successor voice assignment");
            restored.Leadership.Vacate("ChiefScientist");
            Require(own.Leadership.Current("ChiefScientist") is not null && restored.Leadership.Current("ChiefScientist") is null,
                "Loaded leadership aliases original state or vacancy failed");
            saves.Save(path, restoredGalaxy, 4);
            Require(saves.Load(path).Galaxy.Civilizations.Single(c => c.Id == own.Id).Leadership.Current("ChiefScientist") is null,
                "A saved vacancy incorrectly restored the previous scientist");
            var json = JsonNode.Parse(File.ReadAllText(path))!;
            foreach (var civilization in json["Galaxy"]!["Civilizations"]!.AsArray()) civilization!.AsObject().Remove("Leadership");
            File.WriteAllText(path, json.ToJsonString());
            Require(saves.Load(path).Galaxy.Civilizations.Single(c => c.Id == own.Id).Leadership.Current("ChiefScientist") is not null,
                "Legacy save without leadership did not receive a founding roster");
            bool rejected = false;
            try { own.Leadership.Assign("ChiefScientist", new("", "Invalid")); } catch (ArgumentException) { rejected = true; }
            Require(rejected, "Malformed character identity was accepted");
        }
        finally { Directory.Delete(directory, true); }
    }

    private static void Require(bool condition, string message)
    { if (!condition) throw new InvalidOperationException(message); }
}
