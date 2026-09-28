using System.Linq;
using Godot;

namespace Game.Presentation;

public sealed record UiHomeIdentitySnapshot(string SpeciesId, string SystemName, string ColonyName,
    int? BodyId, string? CatalogPresetId);

public partial class Main
{
    public UiHomeIdentitySnapshot UiHomeIdentity
    {
        get
        {
            if (_galaxy is null) return new("", "", "", null, null);
            var player = PlayerCivilization;
            var system = _galaxy.Systems.First(candidate => candidate.Id == player.HomeSystemId);
            var colony = _galaxy.Colonies.FirstOrDefault(candidate =>
                candidate.CivilizationId == player.Id && candidate.SystemId == system.Id);
            return new(player.SpeciesId, system.Name, colony?.Name ?? "", colony?.PlanetaryBodyId, system.CatalogPresetId);
        }
    }

    public int? UiSelectedBodyId => UiIsSystemSpatialView ? _systemSpatialCanvas?.SelectedBodyId : null;
    public Vector2? UiGetBodyScreenPosition(int bodyId) =>
        UiIsSystemSpatialView ? _systemSpatialCanvas?.GetBodyScreenPosition(bodyId) : null;
    public string? UiGetBodyLabel(int bodyId) =>
        UiIsSystemSpatialView ? _systemSpatialCanvas?.GetBodyLabel(bodyId) : null;
}
