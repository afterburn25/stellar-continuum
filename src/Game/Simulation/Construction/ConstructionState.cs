using System.Collections.Generic;

namespace Game.Simulation.Construction;

public sealed class ConstructionState
{
    public const int MaxQueuedProjects = 8;
    public required int CivilizationId { get; init; }
    public HashSet<string> CompletedProjectIds { get; } = new();
    public string? ActiveProjectId { get; set; }
    public double ActiveProjectProgress { get; set; }
    // This is the amount actually debited when the active order was authorized. Old saves
    // intentionally restore this as zero: cancelling a legacy project must never mint credits.
    public double ActiveProjectAuthorizationCredits { get; set; }
    public List<QueuedConstructionProject> QueuedProjects { get; } = new();
}

public sealed record QueuedConstructionProject(string ProjectId, double AuthorizationCredits);
