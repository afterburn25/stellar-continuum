using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Presentation.PlanetIdentity;
var root=Path.GetFullPath(args.Length>0?args[0]:".");
var directory=Path.Combine(root,"assets/visual/planets");Directory.CreateDirectory(directory);
var options=new JsonSerializerOptions{WriteIndented=true,PropertyNamingPolicy=JsonNamingPolicy.CamelCase};
options.Converters.Add(new JsonStringEnumConverter());
var manifest=new {
    version=PlanetVisualCatalog.Version,
    source="src/Game/Presentation/PlanetIdentity/PlanetVisualCatalog.cs",
    implementation="Original runtime procedural materials; no AI-generated artwork is represented as completed.",
    shaders=new[]{PlanetVisualCatalog.OrbitalShader,PlanetVisualCatalog.SurfaceShader,PlanetVisualCatalog.SkyShader},
    families=PlanetVisualCatalog.All,
    textureBudget="One shared 2048 square packed detail master with mipmaps; no per-body raster allocation.",
    canonicalSol="Unmodified authored assets/visual/sol; see docs/SOL_VISUAL_SOURCES.md"
};
File.WriteAllText(Path.Combine(directory,"identity-manifest.json"),JsonSerializer.Serialize(manifest,options)+"\n");
Console.WriteLine("Exported 20 families and 100 variants from the runtime catalog.");
