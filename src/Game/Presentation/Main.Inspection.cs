using System.Linq;
using System.Text;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Territory;

namespace Game.Presentation;

public sealed record UiInspectionFact(string Label, string Value, bool Positive);
public sealed record UiSystemInspectionSnapshot(string Name, string SurveyStatus, double SurveyProgress,
    bool HasDetailedSurvey, string Guidance, UiInspectionFact[] Facts, string ColonyName, string ColonyDetails);

/// <summary>
/// Player-facing inspection text assembled only from information the player is currently
/// allowed to know. Authoritative simulation state remains outside presentation controls.
/// </summary>
public partial class Main
{
    public int UiSelectedSystemId => _selectedSystemId;

    public (string Name, string Distance) UiSelectedSystemHomeReference
    {
        get
        {
            var selected = _galaxy?.Systems.FirstOrDefault(system => system.Id == _selectedSystemId);
            if (selected is null) return ("No target", "Unavailable");
            var known = _galaxy!.Knowledge.IsSystemKnown(_galaxy.PlayerCivilizationId, selected.Id);
            return (known ? selected.Name : "Unknown", FormatInterstellarDistance(selected));
        }
    }

    public string UiSelectedSystemInspection
    {
        get
        {
            if (_galaxy is null)
                return "Initializing campaign…";
            if (_selectedSystemId < 0)
                return "Select a star system to inspect it.";

            var playerId = _galaxy.PlayerCivilizationId;
            var selected = _galaxy.Systems.FirstOrDefault(system => system.Id == _selectedSystemId);
            if (selected is null)
                return "Selected system is no longer available.";

            var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(playerId, selected.Id);
            if (surveyLevel == SystemSurveyLevel.Unknown)
            {
                return "Unknown\n"
                     + "Status: Unknown\n"
                     + "Distance from homeworld: " + FormatInterstellarDistance(selected) + "\n"
                     + (PlayerCivilization.DevelopmentStage == Game.Simulation.Models.CivilizationDevelopmentStage.PreWarp
                         ? "Interstellar operations are not yet available."
                         : "Send an exploration vessel to establish local information.");
            }

            // Consume the simulation-owned fog-safe read model rather than reading detailed
            // system facts directly. Detected/partial systems intentionally carry null details.
            var exploration = new ExplorationReadModel().Build(_galaxy, playerId);
            var inspection = exploration.KnownSystems.First(system => system.SystemId == selected.Id);
            var builder = new StringBuilder();
            builder.AppendLine(inspection.CatalogName);
            builder.Append("Survey status: ").AppendLine(inspection.SurveyLevel.ToString());
            builder.Append("Survey progress: ").AppendLine(inspection.SurveyProgress.ToString("P0"));
            builder.Append("Distance from homeworld: ").AppendLine(FormatInterstellarDistance(selected));

            if (!inspection.HasDetailedSurvey)
            {
                if (inspection.SurveyLevel == SystemSurveyLevel.Detected)
                    builder.Append("Detailed planet, resource, anomaly, and native-civilization data remain unknown. Send a scout for reconnaissance or a science vessel for a detailed survey.");
                else
                    builder.Append("Reconnaissance is incomplete. A science vessel must finish the detailed survey before colonization-grade facts are available.");
                return builder.ToString();
            }

            builder.Append("Primary star: ").AppendLine(StellarClassLabel(inspection.StellarClass));
            builder.Append("System traits: ").AppendLine(inspection.Archetype?.ToString() ?? "Unknown");
            builder.Append("Habitable world detected: ").AppendLine(YesNo(inspection.HasHabitableWorld == true));
            builder.Append("Anomaly detected: ").AppendLine(YesNo(inspection.HasAnomaly == true));
            builder.Append("Rare resource signature: ").AppendLine(YesNo(inspection.HasRareResource == true));
            builder.Append("Known pre-warp civilization: ").AppendLine(YesNo(inspection.HasPreWarpCivilization == true));

            var colony = _galaxy.Colonies.FirstOrDefault(candidate => candidate.SystemId == selected.Id);
            if (colony is null)
            {
                builder.Append("Colony: none known");
                return builder.ToString();
            }

            var ownColony = colony.CivilizationId == playerId;
            var foreignColonyKnown = ownColony || _galaxy.Knowledge.IsCivilizationKnown(playerId, colony.CivilizationId);
            if (!foreignColonyKnown)
            {
                builder.Append("Colony: presence not reliably identified");
                return builder.ToString();
            }

            builder.Append("Colony: ").AppendLine(colony.Name);
            builder.Append("Population: ").Append(colony.PopulationMillions.ToString("0.0")).AppendLine("M");
            builder.Append("Infrastructure: ").AppendLine(colony.Infrastructure.ToString("0.00"));
            builder.Append("Stability: ").AppendLine(colony.Stability.ToString("P0"));

            if (ownColony)
            {
                var logistics = _economyLogisticsView.GetSnapshot(_galaxy, playerId);
                var colonyLogistics = logistics.Colonies.FirstOrDefault(item => item.ColonyId == colony.Id);
                if (colonyLogistics is not null)
                {
                    builder.Append("Supply: ").Append(colonyLogistics.Condition)
                        .Append(" · local coverage ").AppendLine(colonyLogistics.CoverageRatio.ToString("P0"));
                    builder.Append("Imported support needed: ")
                        .Append(colonyLogistics.ImportedSupportRequiredPerDay.ToString("0.00"))
                        .Append("/day");
                }
            }

            return builder.ToString();
        }
    }

    public UiSystemInspectionSnapshot UiSelectedSystemIntelligence
    {
        get
        {
            if (_galaxy is null)
                return new("INITIALIZING", "Unavailable", 0, false, "Campaign intelligence is initializing.",
                    System.Array.Empty<UiInspectionFact>(), "NO COLONY DATA", string.Empty);
            if (_selectedSystemId < 0)
                return new("SELECT A STAR", "No target", 0, false,
                    "Select a star on the map to open its intelligence record.",
                    System.Array.Empty<UiInspectionFact>(), "NO COLONY DATA", string.Empty);

            var playerId = _galaxy.PlayerCivilizationId;
            var selected = _galaxy.Systems.FirstOrDefault(system => system.Id == _selectedSystemId);
            if (selected is null)
                return new("TARGET LOST", "Unavailable", 0, false, "The selected system is no longer available.",
                    System.Array.Empty<UiInspectionFact>(), "NO COLONY DATA", string.Empty);
            var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(playerId, selected.Id);
            var homeDistance = HomeDistanceFact(selected);
            if (surveyLevel == SystemSurveyLevel.Unknown)
                return new("UNKNOWN", "Unknown", 0, false,
                    PlayerCivilization.DevelopmentStage == Game.Simulation.Models.CivilizationDevelopmentStage.PreWarp
                        ? "Interstellar operations are not yet available."
                        : "Dispatch a scout or science vessel to establish local information.",
                    new[] { homeDistance }, "NO COLONY DATA", "Survey required");

            var exploration = new ExplorationReadModel().Build(_galaxy, playerId);
            var inspection = exploration.KnownSystems.First(system => system.SystemId == selected.Id);
            if (!inspection.HasDetailedSurvey)
            {
                var guidance = inspection.SurveyLevel == SystemSurveyLevel.Detected
                    ? "Planet, resource, anomaly and civilization data remain unknown. Send a scout for reconnaissance or a science vessel for a detailed survey."
                    : "Reconnaissance is incomplete. A science vessel must finish the detailed survey before settlement-grade facts are available.";
                return new(inspection.CatalogName.ToUpperInvariant(), inspection.SurveyLevel.ToString(),
                    inspection.SurveyProgress, false, guidance, new[] { homeDistance },
                    "COLONY STATUS UNKNOWN", "Detailed survey required");
            }

            var facts = new System.Collections.Generic.List<UiInspectionFact>
            {
                new UiInspectionFact("PRIMARY STAR", StellarClassLabel(inspection.StellarClass),
                    inspection.StellarClass.HasValue || inspection.Archetype.HasValue),
                new UiInspectionFact("SYSTEM TRAITS", inspection.Archetype?.ToString() ?? "Unknown", inspection.Archetype.HasValue),
                homeDistance,
                new UiInspectionFact("HABITABLE WORLD", YesNo(inspection.HasHabitableWorld == true), inspection.HasHabitableWorld == true),
                new UiInspectionFact("ANOMALY", YesNo(inspection.HasAnomaly == true), inspection.HasAnomaly == true),
                new UiInspectionFact("RARE RESOURCES", YesNo(inspection.HasRareResource == true), inspection.HasRareResource == true),
                new UiInspectionFact("PRE-WARP LIFE", YesNo(inspection.HasPreWarpCivilization == true), inspection.HasPreWarpCivilization == true),
            };
            var territory = TerritorialRuntime.Peek(_galaxy);
            var ownTerritory = territory?.Read(playerId, selected.Id);
            if (ownTerritory is not null)
            {
                facts.Add(new("POLITICAL INFLUENCE", ownTerritory.Political.ToString("0.0"), ownTerritory.Political >= 4));
                facts.Add(new("INFLUENCE SHARE", ownTerritory.Share.ToString("P0"), ownTerritory.Share >= TerritorialBalance.DominanceMinimumShare));
                facts.Add(new("EFFECTIVE CONTROL", ownTerritory.EffectiveControl.ToString("P0"), ownTerritory.EffectiveControl >= .45));
                facts.Add(new("ADMINISTRATION", ownTerritory.Administration.ToString("P0"), ownTerritory.Administration >= .12));
                facts.Add(new("OPERATIONAL SUPPLY (NOT CARGO)", ownTerritory.Supply.ToString("P0"), ownTerritory.Supply >= .12));
                facts.Add(new("TRADE REACH", ownTerritory.Trade.ToString("P0"), ownTerritory.Trade > 0));
                facts.Add(new("MILITARY PROJECTION", ownTerritory.Military.ToString("P0"), ownTerritory.Military > 0));
                facts.Add(new("EXPANSION REGION", ownTerritory.Expansion.ToString(), ownTerritory.Expansion != ExpansionRegion.Remote));
                facts.Add(new("EXPEDITION COST", ownTerritory.ExpeditionMultiplier.ToString("0.00×"), ownTerritory.Expansion == ExpansionRegion.Established));
                facts.Add(new("ESTABLISHMENT TIME", ownTerritory.EstablishmentMultiplier.ToString("0.00×"), ownTerritory.Expansion == ExpansionRegion.Established));
                facts.Add(new("CONTROL VULNERABILITY", ownTerritory.InstabilityRisk.ToString("P0"), ownTerritory.InstabilityRisk < .35));
                facts.Add(new("TAX COLLECTION", ownTerritory.TaxCollection.ToString("P0"), ownTerritory.TaxCollection >= .9));
                facts.Add(new("ADMINISTRATION COST", ownTerritory.AdministrationMultiplier.ToString("0.00×"), ownTerritory.AdministrationMultiplier <= 1.1));
                if (ownTerritory.Sources.Count > 0)
                    facts.Add(new("CONTROL SOURCES", string.Join(" · ", ownTerritory.Sources.Select(source =>
                        $"{source.Kind} {source.Political:0.0}/{source.Administration:P0}")), true));
            }
            SystemTerritory? regional = null;
            if (territory?.Systems.TryGetValue(selected.Id, out var territorialSystem) == true)
                regional = territorialSystem;
            if (regional is not null)
            {
                var ownerId = regional.ControllerId ?? regional.Civilizations.FirstOrDefault()?.CivilizationId;
                var controller = regional.ControllerId is int controllerId && _galaxy.Knowledge.IsCivilizationKnown(playerId, controllerId)
                    ? _galaxy.Civilizations.First(c => c.Id == controllerId).Name : regional.ControllerId is null ? "No effective controller" : "Unconfirmed";
                facts.Add(new("EFFECTIVE CONTROLLER", controller, regional.ControllerId == playerId));
                if (ownerId == playerId)
                    facts.Add(new("REGIONAL STATUS", regional.Status.ToString(), regional.Status != TerritorialControlStatus.Contested));
                else if (ownerId is int foreignId && _galaxy.Knowledge.IsCivilizationKnown(playerId, foreignId))
                    facts.Add(new("REGIONAL STATUS", regional.Status + " · " + _galaxy.Civilizations.First(c => c.Id == foreignId).Name,
                        regional.Status != TerritorialControlStatus.Contested));
                var knownShares = regional.Civilizations.Where(score => score.Share >= .005 && (score.CivilizationId == playerId ||
                        _galaxy.Knowledge.IsCivilizationKnown(playerId, score.CivilizationId)))
                    .Select(score => _galaxy.Civilizations.First(c => c.Id == score.CivilizationId).Name + " " + score.Share.ToString("P0"))
                    .ToArray();
                if (knownShares.Length > 1)
                    facts.Add(new("KNOWN INFLUENCE SHARES", string.Join(" · ", knownShares), regional.Status != TerritorialControlStatus.Contested));
            }
            var claims = _diplomacyRuntime?.BuildView(playerId).Claims.Where(claim => claim.Active && claim.SystemId == selected.Id &&
                (claim.ClaimantCivilizationId == playerId || _galaxy.Knowledge.IsCivilizationKnown(playerId, claim.ClaimantCivilizationId))).ToArray();
            if (claims is { Length: > 0 })
                facts.Add(new("CLAIMED BY", string.Join(" · ", claims.Select(claim =>
                    _galaxy.Civilizations.First(civilization => civilization.Id == claim.ClaimantCivilizationId).Name)), false));
            var colony = _galaxy.Colonies.FirstOrDefault(candidate => candidate.SystemId == selected.Id);
            if (colony is null)
                return new(inspection.CatalogName.ToUpperInvariant(), inspection.SurveyLevel.ToString(),
                    inspection.SurveyProgress, true, "Detailed intelligence available.", facts.ToArray(),
                    "NO KNOWN COLONY", "No represented settlement is known in this system.");

            var ownColony = colony.CivilizationId == playerId;
            var foreignColonyKnown = ownColony || _galaxy.Knowledge.IsCivilizationKnown(playerId, colony.CivilizationId);
            if (!foreignColonyKnown)
                return new(inspection.CatalogName.ToUpperInvariant(), inspection.SurveyLevel.ToString(),
                    inspection.SurveyProgress, true, "Detailed intelligence available.", facts.ToArray(),
                    "COLONY PRESENCE UNIDENTIFIED", "Ownership and settlement details are not reliably known.");

            var colonyDetails = $"Population {colony.PopulationMillions:0.0}M · Infrastructure {colony.Infrastructure:0.00} · Stability {colony.Stability:P0}";
            if (ownColony)
            {
                var logistics = _economyLogisticsView.GetSnapshot(_galaxy, playerId).Colonies
                    .FirstOrDefault(item => item.ColonyId == colony.Id);
                if (logistics is not null)
                    colonyDetails += $" · Supply {logistics.Condition} · Local coverage {logistics.CoverageRatio:P0} · Imports {logistics.ImportedSupportRequiredPerDay:0.00}/day";
            }
            return new(inspection.CatalogName.ToUpperInvariant(), inspection.SurveyLevel.ToString(),
                inspection.SurveyProgress, true, "Detailed intelligence available.", facts.ToArray(),
                colony.Name.ToUpperInvariant(), colonyDetails);
        }
    }

    private static string StellarClassLabel(StellarPrimaryClass? stellarClass) => stellarClass switch
    {
        StellarPrimaryClass.MRedDwarf => "M-type red dwarf",
        StellarPrimaryClass.KOrangeDwarf => "K-type orange dwarf",
        StellarPrimaryClass.GYellowDwarf => "G-type yellow dwarf",
        StellarPrimaryClass.FYellowWhiteDwarf => "F-type yellow-white dwarf",
        StellarPrimaryClass.AWhiteStar => "A-type white star",
        StellarPrimaryClass.HotBlueStar => "Hot blue B/O star",
        StellarPrimaryClass.Giant => "Red/orange giant",
        StellarPrimaryClass.WhiteDwarf => "White dwarf",
        StellarPrimaryClass.NeutronStar => "Neutron star / pulsar",
        StellarPrimaryClass.Pulsar => "Pulsar",
        StellarPrimaryClass.BlackHole => "Black hole",
        StellarPrimaryClass.Protostar => "Young star / protostar",
        _ => "Legacy classification",
    };

    private string FormatInterstellarDistance(StarSystemState system)
    {
        var home = _galaxy.Systems.Single(candidate => candidate.Id == PlayerCivilization.HomeSystemId);
        var lightYears = InterstellarDistance.Between(home, system);
        return MetricFormat.InterstellarDistance(lightYears, AstronomicalDistance.LightYearsToParsecs(lightYears));
    }

    private UiInspectionFact HomeDistanceFact(StarSystemState system) =>
        new("DISTANCE FROM HOMEWORLD", FormatInterstellarDistance(system), true);
}
