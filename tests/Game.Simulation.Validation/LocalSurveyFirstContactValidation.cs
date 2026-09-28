using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class LocalSurveyFirstContactValidation
{
    public static void ValidateScoutSurveyCreatesDirectionalContact()
    {
        var fixture = CreateFixture(FleetRole.Scout);
        var events = fixture.Simulation.Advance(fixture.Galaxy, 1.0);

        Require(fixture.Galaxy.Knowledge.IsCivilizationKnown(fixture.Player.Id, fixture.Foreign.Id),
            "active local scout reconnaissance did not reveal same-system foreign civilization presence");
        Require(events.Count(@event =>
                @event.Type == ExplorationEventType.FirstContact &&
                @event.CivilizationId == fixture.Player.Id &&
                @event.TargetCivilizationId == fixture.Foreign.Id &&
                @event.SystemId == fixture.ForeignHome.Id) == 1,
            "active local scout reconnaissance did not emit exactly one directional first-contact event");
        Require(!fixture.Galaxy.Knowledge.IsCivilizationKnown(fixture.Foreign.Id, fixture.Player.Id),
            "player scout reconnaissance incorrectly granted passive reciprocal contact knowledge");
    }

    public static void ValidateScienceSurveyCreatesDirectionalContact()
    {
        var fixture = CreateFixture(FleetRole.Science);
        var profile = fixture.Simulation.GetSurveyOperationsProfile(fixture.Galaxy, fixture.ForeignHome.Id);
        var events = fixture.Simulation.Advance(fixture.Galaxy, profile.EstimatedScienceSurveyDays * 0.25);

        Require(fixture.Galaxy.Knowledge.GetSystemSurveyLevel(fixture.Player.Id, fixture.ForeignHome.Id) == SystemSurveyLevel.PartiallySurveyed,
            "active local science survey did not establish partial survey knowledge in validation fixture");
        Require(fixture.Galaxy.Knowledge.IsCivilizationKnown(fixture.Player.Id, fixture.Foreign.Id),
            "active local science survey did not reveal same-system foreign civilization presence");
        Require(events.Count(@event =>
                @event.Type == ExplorationEventType.FirstContact &&
                @event.CivilizationId == fixture.Player.Id &&
                @event.TargetCivilizationId == fixture.Foreign.Id &&
                @event.SystemId == fixture.ForeignHome.Id) == 1,
            "active local science survey did not emit exactly one directional first-contact event");
        Require(!fixture.Galaxy.Knowledge.IsCivilizationKnown(fixture.Foreign.Id, fixture.Player.Id),
            "player science survey incorrectly granted passive reciprocal contact knowledge");
    }

    public static void ValidatePassiveColocationDoesNotCreateContact()
    {
        var fixture = CreateFixture(FleetRole.Scout);
        fixture.Galaxy.Knowledge.MarkSystemFullySurveyed(fixture.Player.Id, fixture.ForeignHome.Id);

        var events = fixture.Simulation.Advance(fixture.Galaxy, 1.0);

        Require(!fixture.Galaxy.Knowledge.IsCivilizationKnown(fixture.Player.Id, fixture.Foreign.Id),
            "passive same-system co-location incorrectly revealed foreign civilization identity");
        Require(events.All(@event => @event.Type != ExplorationEventType.FirstContact || @event.CivilizationId != fixture.Player.Id),
            "passive same-system co-location incorrectly emitted first contact");
    }

    private static ContactFixture CreateFixture(FleetRole role)
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4C4F_4341_4C43_5443L + (int)role,
            new GalaxyGenerationSettings
            {
                SystemCount = 48,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 520.0f,
            });

        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var foreign = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        var foreignHome = galaxy.Systems.First(system => system.Id == foreign.HomeSystemId);

        foreach (var fleet in galaxy.Fleets)
            fleet.IsActive = false;

        Require(galaxy.Colonies.Any(colony =>
                colony.CivilizationId == foreign.Id && colony.SystemId == foreignHome.Id),
            "validation foreign civilization did not have physical colony presence at its home system");
        Require(!galaxy.Knowledge.IsCivilizationKnown(player.Id, foreign.Id),
            "validation player already knew foreign civilization before local observation");

        var observer = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 90000 : galaxy.Fleets.Max(existing => existing.Id) + 90000,
            CivilizationId = player.Id,
            Name = role == FleetRole.Scout ? "Local Contact Scout" : "Local Contact Science Vessel",
            Role = role,
            Position = foreignHome.Position,
            CurrentSystemId = foreignHome.Id,
            DestinationSystemId = null,
            StrategicSpeed = role == FleetRole.Scout ? 22.0 : 18.0,
            SensorRange = role == FleetRole.Scout ? 135.0f : 185.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(observer);

        return new ContactFixture(galaxy, player, foreign, foreignHome, observer, new ExplorationSimulation());
    }

    private sealed record ContactFixture(
        GalaxyState Galaxy,
        CivilizationState Player,
        CivilizationState Foreign,
        StarSystemState ForeignHome,
        FleetState Observer,
        ExplorationSimulation Simulation);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
