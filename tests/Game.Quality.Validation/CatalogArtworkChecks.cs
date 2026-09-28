using Game.Presentation;
using Game.Validation;

namespace Game.Quality.Validation;

internal static class CatalogArtworkChecks
{
    [RegressionCheck]
    private static void ConcealedResearchNeverSelectsItsIllustration()
    {
        foreach (var entry in CatalogArtwork.Research)
        {
            if (CatalogArtwork.ResearchArt(entry.Id, false) != CatalogArtwork.Concealed)
                throw new InvalidOperationException("Unknown research leaked artwork: " + entry.Id);
            if (CatalogArtwork.ResearchArt(entry.Id, true) == CatalogArtwork.Concealed)
                throw new InvalidOperationException("Visible research has no artwork: " + entry.Id);
        }
        foreach (var id in new[] { "../secret", "future-technology", "", null })
            if (CatalogArtwork.ResearchArt(id, true) != CatalogArtwork.Concealed)
                throw new InvalidOperationException("Unregistered content must use the concealed fallback.");
    }

    [RegressionCheck]
    private static void ModuleVersionsRetainTheirOwnArtwork()
    {
        var modules = CatalogArtwork.Modules;
        if (modules.Select(x => x.Art).Distinct().Count() != modules.Count)
            throw new InvalidOperationException("Module versions unexpectedly share an image.");
        foreach (var entry in modules)
        {
            if (CatalogArtwork.ModuleArt(entry.Id) != entry.Art ||
                CatalogArtwork.ModuleArt(entry.Id, false) != CatalogArtwork.Concealed)
                throw new InvalidOperationException("Wrong module illustration: " + entry.Id);
        }
    }
}
