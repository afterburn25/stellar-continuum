namespace Game.Campaign;

/// <summary>Explicit session provenance. Null on GalaxyState denotes an ordinary Player campaign.</summary>
public sealed record DeveloperSessionState(bool ToolsUsed);
