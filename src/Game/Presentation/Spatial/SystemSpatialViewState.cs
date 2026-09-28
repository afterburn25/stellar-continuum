using System;
using Game.Simulation.Knowledge;

namespace Game.Presentation.Spatial;

/// <summary>
/// One opened observer-local view, never serialized. Context changes discard it immediately;
/// ordinary refreshes are bounded because the current Exploration read model spans the galaxy.
/// </summary>
public sealed class SystemSpatialViewState
{
    public const double RefreshIntervalSeconds = 1.0;
    private object? _campaign;
    private int _observerId = -1;
    private double _refreshCountdown;

    public int SystemId { get; private set; } = -1;
    public SystemSurveyLevel SurveyLevel { get; private set; } = SystemSurveyLevel.Unknown;
    public double SurveyProgress { get; private set; }
    public bool IsOpen => SystemId >= 0;

    public void Open(object campaign, int observerId, int systemId)
    {
        ArgumentNullException.ThrowIfNull(campaign);
        _campaign = campaign;
        _observerId = observerId;
        SystemId = systemId;
        SurveyLevel = SystemSurveyLevel.Unknown;
        SurveyProgress = 0.0;
        _refreshCountdown = 0.0;
    }

    public bool MatchesContext(object? campaign, int observerId, int selectedSystemId) =>
        IsOpen && ReferenceEquals(_campaign, campaign) &&
        _observerId == observerId && SystemId == selectedSystemId;

    public bool NeedsRefresh(SystemSurveyLevel surveyLevel, double delta)
    {
        if (!IsOpen)
            return false;
        _refreshCountdown -= Math.Max(0.0, delta);
        // A changed confidence gate must never retain a more privileged marker class.
        return surveyLevel != SurveyLevel || _refreshCountdown <= 0.0;
    }

    public void Refreshed(SystemSurveyLevel surveyLevel, double surveyProgress)
    {
        SurveyLevel = surveyLevel;
        SurveyProgress = surveyProgress;
        _refreshCountdown = RefreshIntervalSeconds;
    }

    public void Close()
    {
        _campaign = null;
        _observerId = -1;
        SystemId = -1;
        SurveyLevel = SystemSurveyLevel.Unknown;
        SurveyProgress = 0.0;
        _refreshCountdown = 0.0;
    }
}
