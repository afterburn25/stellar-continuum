using System;
using System.Collections.Generic;

namespace Game.Simulation.Territory;

public enum TerritorialInstallationKind { Relay, SupplyDepot, TradeHub, NavalBase, ResearchStation, Administration }
public enum ExpansionRegion { Established, Frontier, Remote }
public enum TerritorialControlStatus { Independent, Influenced, Dominant, Controlled, Contested }

/// <summary>Persistent facts only. Scores, territorial polygons and path caches are never saved.</summary>
public sealed class TerritorialState
{
    public int SchemaVersion { get; set; } = 1;
    public double ElapsedDays { get; set; }
    public double NextReviewDay { get; set; }
    public double NextDiplomaticReviewDay { get; set; }
    public List<TerritorialBorderIncident> BorderIncidents { get; set; } = new();
    /// <summary>Observer-specific, last legitimately reported control. This is fog memory,
    /// never an authoritative influence source or a substitute for simulation ownership.</summary>
    public List<TerritorialObservation> Observations { get; set; } = new();
    public List<TerritorialInstallation> Installations { get; set; } = new();
    public List<TerritorialExpedition> Expeditions { get; set; } = new();
}

public sealed class TerritorialInstallation
{
    public int Id { get; set; }
    public int CivilizationId { get; set; }
    public int SystemId { get; set; }
    public TerritorialInstallationKind Kind { get; set; }
    public int BuilderFleetId { get; set; }
    public double CompletedDays { get; set; }
    public double RequiredDays { get; set; }
    public double PaidCredits { get; set; }
    public double PaidIndustry { get; set; }
    public bool IsComplete => CompletedDays >= RequiredDays;
}

public sealed record TerritorialExpedition(int FleetId, int BodyId, double PaidCredits, double RequiredDays);
public sealed record TerritorialBorderIncident(int FirstCivilizationId, int SecondCivilizationId, double LastDay);
public sealed record TerritorialObservation(int ObserverCivilizationId, int SystemId, int CivilizationId,
    TerritorialControlStatus Status);
public sealed record TerritorialInstallationDefinition(TerritorialInstallationKind Kind, string Name,
    double Credits, double Industry, double Days, double Upkeep, double Political, double Administration,
    double Trade, double Military, double Range);

/// <summary>One balance surface. All reach distances are physical light years.</summary>
public static class TerritorialBalance
{
    private static readonly TerritorialTuning Tuning = TerritorialTuning.Load();
    public static readonly double ReviewDays = Tuning.Values["ReviewDays"];
    public static readonly double ColonyRange = Tuning.Values["ColonyRange"];
    public static readonly double OutpostRange = Tuning.Values["OutpostRange"];
    public static readonly double RelayConnectionRange = Tuning.Values["RelayConnectionRange"];
    public static readonly double RelayAttenuation = Tuning.Values["RelayAttenuation"];
    public static readonly double IndependentWeight = Tuning.Values["IndependentWeight"];
    public static readonly double FrontierMinimumPolitical = Tuning.Values["FrontierMinimumPolitical"];
    public static readonly double FrontierMinimumAdministration = Tuning.Values["FrontierMinimumAdministration"];
    public static readonly double FrontierMinimumSupply = Tuning.Values["FrontierMinimumSupply"];
    public static readonly double MinimumTaxCollection = Tuning.Values["MinimumTaxCollection"];
    public static readonly double ColonyStrength = Tuning.Values["ColonyStrength"];
    public static readonly double PopulationStrength = Tuning.Values["PopulationStrength"];
    public static readonly double InfrastructureStrength = Tuning.Values["InfrastructureStrength"];
    public static readonly double CapitalTierStrength = Tuning.Values["CapitalTierStrength"];
    public static readonly double OutpostStrength = Tuning.Values["OutpostStrength"];
    public static readonly double PoliticalFalloff = Tuning.Values["PoliticalFalloff"];
    public static readonly double AdministrationFalloff = Tuning.Values["AdministrationFalloff"];
    public static readonly double EstablishedPolitical = Tuning.Values["EstablishedPolitical"];
    public static readonly double EstablishedAdministration = Tuning.Values["EstablishedAdministration"];
    public static readonly double EstablishedSupply = Tuning.Values["EstablishedSupply"];
    public static readonly double ContestedMinimumShare = Tuning.Values["ContestedMinimumShare"];
    public static readonly double ContestedMaximumGap = Tuning.Values["ContestedMaximumGap"];
    public static readonly double ControlMinimum = Tuning.Values["ControlMinimum"];
    public static readonly double DominanceMinimumShare = Tuning.Values["DominanceMinimumShare"];
    public static readonly double FrontierSetupBase = Tuning.Values["FrontierSetupBase"];
    public static readonly double FrontierSetupIsolation = Tuning.Values["FrontierSetupIsolation"];
    public static readonly double FrontierTimeBase = Tuning.Values["FrontierTimeBase"];
    public static readonly double FrontierTimeIsolation = Tuning.Values["FrontierTimeIsolation"];
    public static readonly double AdministrationPenalty = Tuning.Values["AdministrationPenalty"];
    public static readonly double SupplyPenalty = Tuning.Values["SupplyPenalty"];
    public static readonly double ResearchNetworkInfluence = Tuning.Values["ResearchNetworkInfluence"];
    public static readonly double OrbitalShipyardInfluence = Tuning.Values["OrbitalShipyardInfluence"];
    public static readonly double MiningNetworkInfluence = Tuning.Values["MiningNetworkInfluence"];
    public static readonly double RecognizedClaimInfluence = Tuning.Values["RecognizedClaimInfluence"];
    public static IReadOnlyList<TerritorialInstallationDefinition> Installations { get; } = Array.AsReadOnly(Tuning.Installations);
    public static TerritorialInstallationDefinition Definition(TerritorialInstallationKind kind)
    {
        foreach (var definition in Installations) if (definition.Kind == kind) return definition;
        throw new ArgumentOutOfRangeException(nameof(kind));
    }
}

public sealed record TerritorialContribution(int SystemId, string Kind, double Political, double Administration);
public sealed record CivilizationSystemTerritory(int CivilizationId, int SystemId, double Political,
    double Share, double Administration, double Supply, double Trade, double Military, double EffectiveControl,
    double TaxCollection, double AdministrationMultiplier, ExpansionRegion Expansion,
    IReadOnlyList<TerritorialContribution> Sources)
{
    public double ExpeditionMultiplier => Expansion == ExpansionRegion.Established ? 1 : TerritorialBalance.FrontierSetupBase + (1 - Administration) * TerritorialBalance.FrontierSetupIsolation;
    public double EstablishmentMultiplier => Expansion == ExpansionRegion.Established ? 1 : TerritorialBalance.FrontierTimeBase + (1 - Supply) * TerritorialBalance.FrontierTimeIsolation;
    public double InstabilityRisk => Math.Clamp((1 - EffectiveControl) * .45 + (1 - Supply) * .25, 0, 1);
}
public sealed record SystemTerritory(int SystemId, int? ControllerId, TerritorialControlStatus Status,
    double IndependentShare, IReadOnlyList<CivilizationSystemTerritory> Civilizations);
public sealed record TerritorialOrderResult(bool Accepted, string Message, int? InstallationId = null);
