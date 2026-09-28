using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;

namespace Game.Simulation.Validation;

internal static class ExplorationObservationConfidenceValidation
{
    public static void ValidateConfidenceTracksObserverKnowledgeWithoutLeaks()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_4E46_4944_454EL,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });

        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var other = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        var target = galaxy.Systems
            .Where(system => galaxy.Knowledge.GetSystemSurveyLevel(player.Id, system.Id) == SystemSurveyLevel.Unknown)
            .Where(system => galaxy.Knowledge.GetSystemSurveyLevel(other.Id, system.Id) == SystemSurveyLevel.Unknown)
            .OrderBy(system => system.Id)
            .First();
        var authoritativeBodies = galaxy.PlanetaryBodies
            .Where(body => body.SystemId == target.Id)
            .OrderBy(body => body.Id)
            .ToArray();
        Require(authoritativeBodies.Length > 0, "confidence validation target contained no planetary bodies");

        var readModel = new ExplorationReadModel();

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var detected = GetSystem(readModel.Build(galaxy, player.Id), target.Id);
        Require(detected.SurveyLevel == SystemSurveyLevel.Detected,
            "detected confidence fixture did not remain detection-only");
        Require(detected.ObservationConfidence == ExplorationObservationConfidence.Detection,
            "detected system did not report detection-grade confidence");
        Require(detected.DetailedSystemFactsConfidence == ExplorationObservationConfidence.None,
            "detected system incorrectly reported confidence in hidden detailed system facts");
        Require(detected.PlanetaryBodies.Count == 0,
            "detected system exposed body confidence before reconnaissance");
        Require(!readModel.Build(galaxy, other.Id).KnownSystems.Any(system => system.SystemId == target.Id),
            "one civilization's detection confidence leaked the target into another civilization's knowledge");

        Require(galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id, 0.40),
            "confidence validation reconnaissance did not advance target knowledge");
        var reconnaissance = GetSystem(readModel.Build(galaxy, player.Id), target.Id);
        Require(reconnaissance.SurveyLevel == SystemSurveyLevel.PartiallySurveyed,
            "reconnaissance confidence fixture did not become partially surveyed");
        Require(reconnaissance.ObservationConfidence == ExplorationObservationConfidence.Reconnaissance,
            "partial system did not report reconnaissance-grade confidence");
        Require(reconnaissance.DetailedSystemFactsConfidence == ExplorationObservationConfidence.None,
            "partial system incorrectly reported confidence in full-survey-only system facts");
        Require(reconnaissance.PlanetaryBodies.Count == authoritativeBodies.Length,
            "reconnaissance did not expose the expected orbital catalog");

        foreach (var body in reconnaissance.PlanetaryBodies)
        {
            var authoritative = authoritativeBodies.First(candidate => candidate.Id == body.BodyId);
            Require(body.OrbitalCatalogConfidence == ExplorationObservationConfidence.Reconnaissance,
                $"body {body.BodyId} did not report reconnaissance-grade orbital confidence");
            Require(body.DetailedEnvironmentConfidence == ExplorationObservationConfidence.None,
                $"body {body.BodyId} incorrectly reported detailed environment confidence before full survey");
            Require(body.ResourceEvidenceConfidence == ExpectedReconEvidence(authoritative.HasRareResource),
                $"body {body.BodyId} resource confidence treated an unobserved signature as negative evidence");
            Require(body.AnomalyEvidenceConfidence == ExpectedReconEvidence(authoritative.HasAnomaly),
                $"body {body.BodyId} anomaly confidence treated an unobserved signature as negative evidence");
            Require(body.ActivityEvidenceConfidence == ExpectedReconEvidence(authoritative.HasPreWarpCivilization),
                $"body {body.BodyId} activity confidence treated an unobserved signature as negative evidence");
        }

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        var confirmed = GetSystem(readModel.Build(galaxy, player.Id), target.Id);
        Require(confirmed.ObservationConfidence == ExplorationObservationConfidence.Confirmed,
            "fully surveyed system did not report confirmed observation confidence");
        Require(confirmed.DetailedSystemFactsConfidence == ExplorationObservationConfidence.Confirmed,
            "fully surveyed system did not confirm detailed system facts");

        foreach (var body in confirmed.PlanetaryBodies)
        {
            Require(body.OrbitalCatalogConfidence == ExplorationObservationConfidence.Confirmed,
                $"fully surveyed body {body.BodyId} did not confirm orbital catalog data");
            Require(body.DetailedEnvironmentConfidence == ExplorationObservationConfidence.Confirmed,
                $"fully surveyed body {body.BodyId} did not confirm detailed environment data");
            Require(body.ResourceEvidenceConfidence == ExplorationObservationConfidence.Confirmed,
                $"fully surveyed body {body.BodyId} did not confirm resource presence/absence evidence");
            Require(body.AnomalyEvidenceConfidence == ExplorationObservationConfidence.Confirmed,
                $"fully surveyed body {body.BodyId} did not confirm anomaly presence/absence evidence");
            Require(body.ActivityEvidenceConfidence == ExplorationObservationConfidence.Confirmed,
                $"fully surveyed body {body.BodyId} did not confirm activity presence/absence evidence");
        }

        Require(!readModel.Build(galaxy, other.Id).KnownSystems.Any(system => system.SystemId == target.Id),
            "one civilization's confirmed survey confidence leaked into another civilization's knowledge");
    }

    private static KnownSystemExplorationView GetSystem(CivilizationExplorationView view, int systemId) =>
        view.KnownSystems.FirstOrDefault(system => system.SystemId == systemId)
        ?? throw new InvalidOperationException($"observer-safe view did not include expected system {systemId}");

    private static ExplorationObservationConfidence ExpectedReconEvidence(bool authoritativePositive) =>
        authoritativePositive
            ? ExplorationObservationConfidence.Reconnaissance
            : ExplorationObservationConfidence.None;

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
