using System;
using System.Collections.Generic;
using Godot;

namespace Game.Presentation;

/// <summary>
/// Lazy runtime loader for production-candidate visual SVGs.
/// Centralizing paths prevents presentation code from duplicating asset strings and lets
/// import failures surface immediately in the real Godot runtime/screenshot gate.
/// </summary>
public static class VisualIconLibrary
{
    private static readonly Dictionary<string, Texture2D> Cache = new(StringComparer.Ordinal);

    public const string PausePath = "res://assets/visual/icons/core/icon_hud_pause.svg";
    public const string SpeedPath = "res://assets/visual/icons/core/icon_hud_speed.svg";
    public const string SavePath = "res://assets/visual/icons/core/icon_hud_save.svg";
    public const string SupportPath = "res://assets/visual/icons/core/icon_hud_support.svg";
    public const string ResearchPath = "res://assets/visual/icons/core/icon_action_research.svg";
    public const string ResearchLockedPath = "res://assets/visual/icons/research/icon_research_locked.svg";
    public const string ConstructionPath = "res://assets/visual/icons/core/icon_action_construction.svg";
    public const string ExplorationPath = "res://assets/visual/icons/core/icon_system_exploration.svg";
    public const string LogisticsPath = "res://assets/visual/icons/core/icon_system_logistics.svg";
    public const string RelationsPath = "res://assets/visual/icons/core/icon_system_relations.svg";
    public const string InfoPath = "res://assets/visual/icons/core/icon_status_info.svg";
    public const string SuccessPath = "res://assets/visual/icons/core/icon_status_success.svg";
    public const string SurveyDetectedPath = "res://assets/visual/icons/map/icon_map_detected.svg";
    public const string SurveyPartialPath = "res://assets/visual/icons/map/icon_map_partially_surveyed.svg";
    public const string SurveyFullPath = "res://assets/visual/icons/map/icon_map_fully_surveyed.svg";
    public const string UnknownPath = "res://assets/visual/icons/core/icon_status_unknown.svg";
    public const string ColonyPath = "res://assets/visual/icons/core/icon_map_colony.svg";
    public const string ScoutPath = "res://assets/visual/icons/core/icon_map_scout.svg";
    public const string ScienceVesselPath = "res://assets/visual/icons/ships/icon_ship_science_vessel.svg";
    public const string PatrolCorvettePath = "res://assets/visual/icons/ships/icon_ship_patrol_corvette.svg";
    public const string ColonyShipPath = "res://assets/visual/icons/ships/icon_ship_colony_ship.svg";
    public const string DiplomacyContactPath = "res://assets/visual/icons/diplomacy/icon_diplomacy_contact.svg";
    public const string DiplomacyAgreementPath = "res://assets/visual/icons/diplomacy/icon_diplomacy_agreement.svg";
    public const string DiplomacyPeacePath = "res://assets/visual/icons/diplomacy/icon_diplomacy_peace.svg";
    public const string DiplomacyCeasefirePath = "res://assets/visual/icons/diplomacy/icon_diplomacy_ceasefire.svg";
    public const string DiplomacyAccessGrantedPath = "res://assets/visual/icons/diplomacy/icon_diplomacy_access_granted.svg";
    public const string DiplomacyAccessDeniedPath = "res://assets/visual/icons/diplomacy/icon_diplomacy_access_denied.svg";

    public static Texture2D Pause => Get(PausePath);
    public const string CreditsPath = "res://assets/visual/icons/resources/icon_resource_credits.svg";
    public const string IndustryPath = "res://assets/visual/icons/resources/icon_resource_industry.svg";
    public const string SciencePath = "res://assets/visual/icons/resources/icon_resource_science.svg";
    public static Texture2D Credits => Get(CreditsPath);
    public static Texture2D Industry => Get(IndustryPath);
    public static Texture2D Science => Get(SciencePath);
    public const string NavGalaxyPath = "res://assets/visual/icons/navigation/icon_nav_galaxy.svg";
    public static Texture2D NavGalaxy => Get(NavGalaxyPath);
    public const string NavHomePath = "res://assets/visual/icons/navigation/icon_nav_home.svg";
    public static Texture2D NavHome => Get(NavHomePath);
    public const string NavSystemPath = "res://assets/visual/icons/navigation/icon_nav_system.svg";
    public static Texture2D NavSystem => Get(NavSystemPath);
    public const string NavShipsPath = "res://assets/visual/icons/navigation/icon_nav_ships.svg";
    public static Texture2D NavShips => Get(NavShipsPath);
    public const string NavMenuPath = "res://assets/visual/icons/navigation/icon_nav_menu.svg";
    public static Texture2D NavMenu => Get(NavMenuPath);
    public const string NavClosePath = "res://assets/visual/icons/navigation/icon_nav_close.svg";
    public static Texture2D NavClose => Get(NavClosePath);
    public const string NavBackPath = "res://assets/visual/icons/navigation/icon_nav_back.svg";
    public static Texture2D NavBack => Get(NavBackPath);
    public const string NavZoomInPath = "res://assets/visual/icons/navigation/icon_nav_zoom_in.svg";
    public static Texture2D NavZoomIn => Get(NavZoomInPath);
    public const string NavZoomOutPath = "res://assets/visual/icons/navigation/icon_nav_zoom_out.svg";
    public static Texture2D NavZoomOut => Get(NavZoomOutPath);
    public const string NavResearchPath = "res://assets/visual/ui/navigation/nav_research.svg";
    public static Texture2D NavResearch => Get(NavResearchPath);
    public const string NavEconomyPath = "res://assets/visual/ui/navigation/nav_economy.svg";
    public static Texture2D NavEconomy => Get(NavEconomyPath);
    public const string NavConstructionPath = "res://assets/visual/ui/navigation/nav_construction.svg";
    public static Texture2D NavConstruction => Get(NavConstructionPath);
    public const string NavShipyardPath = "res://assets/visual/ui/navigation/nav_shipyard.svg";
    public static Texture2D NavShipyard => Get(NavShipyardPath);
    public const string NavExplorationPath = "res://assets/visual/ui/navigation/nav_exploration.svg";
    public static Texture2D NavExploration => Get(NavExplorationPath);
    public const string NavColonizationPath = "res://assets/visual/ui/navigation/nav_colonization.svg";
    public static Texture2D NavColonization => Get(NavColonizationPath);
    public const string NavLogisticsPath = "res://assets/visual/ui/navigation/nav_logistics.svg";
    public static Texture2D NavLogistics => Get(NavLogisticsPath);
    public const string NavRelationsPath = "res://assets/visual/ui/navigation/nav_relations.svg";
    public static Texture2D NavRelations => Get(NavRelationsPath);
    public const string NavInspectionPath = "res://assets/visual/ui/navigation/nav_inspection.svg";
    public static Texture2D NavInspection => Get(NavInspectionPath);
    public const string NavHomeSemanticPath = "res://assets/visual/ui/navigation/nav_home.svg";
    public static Texture2D NavHomeSemantic => Get(NavHomeSemanticPath);
    public const string NavGalaxySemanticPath = "res://assets/visual/ui/navigation/nav_galaxy.svg";
    public static Texture2D NavGalaxySemantic => Get(NavGalaxySemanticPath);
    public const string NavSettingsPath = "res://assets/visual/ui/navigation/nav_settings.svg";
    public static Texture2D NavSettings => Get(NavSettingsPath);
    public static Texture2D Speed => Get(SpeedPath);
    public static Texture2D Save => Get(SavePath);
    public static Texture2D Support => Get(SupportPath);
    public static Texture2D Research => Get(ResearchPath);
    public static Texture2D ResearchLocked => Get(ResearchLockedPath);
    public static Texture2D Construction => Get(ConstructionPath);
    public static Texture2D Exploration => Get(ExplorationPath);
    public static Texture2D Logistics => Get(LogisticsPath);
    public static Texture2D Relations => Get(RelationsPath);
    public static Texture2D Info => Get(InfoPath);
    public static Texture2D Success => Get(SuccessPath);
    public static Texture2D SurveyDetected => Get(SurveyDetectedPath);
    public static Texture2D SurveyPartial => Get(SurveyPartialPath);
    public static Texture2D SurveyFull => Get(SurveyFullPath);
    public static Texture2D Unknown => Get(UnknownPath);
    public static Texture2D Colony => Get(ColonyPath);
    public static Texture2D Scout => Get(ScoutPath);
    public static Texture2D ScienceVessel => Get(ScienceVesselPath);
    public static Texture2D PatrolCorvette => Get(PatrolCorvettePath);
    public static Texture2D ColonyShip => Get(ColonyShipPath);
    public static Texture2D DiplomacyContact => Get(DiplomacyContactPath);
    public static Texture2D DiplomacyAgreement => Get(DiplomacyAgreementPath);
    public static Texture2D DiplomacyPeace => Get(DiplomacyPeacePath);
    public static Texture2D DiplomacyCeasefire => Get(DiplomacyCeasefirePath);
    public static Texture2D DiplomacyAccessGranted => Get(DiplomacyAccessGrantedPath);
    public static Texture2D DiplomacyAccessDenied => Get(DiplomacyAccessDeniedPath);

    public static Texture2D Get(string resourcePath)
    {
        if (Cache.TryGetValue(resourcePath, out var cached))
            return cached;

        var texture = GD.Load<Texture2D>(resourcePath)
            ?? throw new InvalidOperationException($"Visual asset could not be loaded: {resourcePath}");
        Cache.Add(resourcePath, texture);
        return texture;
    }
}
