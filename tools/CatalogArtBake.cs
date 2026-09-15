using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;
using Godot;

namespace Game.Tools;

/// <summary>Offline catalog production: photographs existing GLBs and makes delivery-size images.
/// No generated geometry, recoloring, cropping or model edits are performed here.</summary>
public partial class CatalogArtBake : Node
{
    public override async void _Ready()
    {
        try
        {
            var args = OS.GetCmdlineUserArgs();
            string Arg(string key) => Array.IndexOf(args, key) is var i && i >= 0 && i + 1 < args.Length
                ? args[i + 1] : throw new ArgumentException("Missing " + key);
            var output = Arg("--output");
            Directory.CreateDirectory(Path.Combine(output, "portraits"));
            Directory.CreateDirectory(Path.Combine(output, "thumbnails"));
            if (args.Contains("--images"))
            {
                foreach (var source in Directory.GetFiles(Arg("--images"), "*.png"))
                {
                    using var image = Image.LoadFromFile(source);
                    if (image is null || image.IsEmpty()) throw new InvalidDataException(source);
                    Deliver(image, Path.GetFileNameWithoutExtension(source), output);
                }
            }
            if (args.Contains("--models")) await RenderModules(Arg("--models"), output);
            GD.Print("CATALOG_ART_BAKE_COMPLETE");
            GetTree().Quit();
        }
        catch (Exception error) { GD.PushError(error.ToString()); GetTree().Quit(1); }
    }

    private static void Deliver(Image image, string id, string output)
    {
        // Masters are square and retained separately. Runtime sees only 512/128 px PNGs.
        if (image.GetWidth() != image.GetHeight()) throw new InvalidDataException("Non-square master: " + id);
        using var portrait = (Image)image.Duplicate();
        portrait.Resize(512, 512, Image.Interpolation.Lanczos);
        if (portrait.SavePng(Path.Combine(output, "portraits", id + ".png")) != Error.Ok)
            throw new IOException("Could not write portrait: " + id);
        using var thumbnail = (Image)image.Duplicate();
        thumbnail.Resize(128, 128, Image.Interpolation.Lanczos);
        if (thumbnail.SavePng(Path.Combine(output, "thumbnails", id + ".png")) != Error.Ok)
            throw new IOException("Could not write thumbnail: " + id);
    }

    private async Task RenderModules(string modelRoot, string output)
    {
        using var json = JsonDocument.Parse(System.IO.File.ReadAllText(ProjectSettings.GlobalizePath("res://assets/visual/catalog/catalog.json")));
        var viewport = new SubViewport { Size = new(1024, 1024), OwnWorld3D = true, Msaa3D = Viewport.Msaa.Msaa4X,
            RenderTargetUpdateMode = SubViewport.UpdateMode.Always };
        AddChild(viewport);
        var stage = new Node3D(); viewport.AddChild(stage);
        var skyMaterial = new ProceduralSkyMaterial
        {
            SkyTopColor = new("455976"), SkyHorizonColor = new("a3adbb"),
            GroundBottomColor = new("172535"), GroundHorizonColor = new("a3adbb"),
        };
        var world = new WorldEnvironment { Environment = new Godot.Environment
        {
            BackgroundMode = Godot.Environment.BGMode.Color, BackgroundColor = new("101b2a"),
            Sky = new Sky { SkyMaterial = skyMaterial },
            AmbientLightSource = Godot.Environment.AmbientSource.Sky, AmbientLightEnergy = .85f,
            ReflectedLightSource = Godot.Environment.ReflectionSource.Sky,
            TonemapMode = Godot.Environment.ToneMapper.Filmic,
        }};
        stage.AddChild(world);
        var key = new DirectionalLight3D { LightEnergy = 2.2f, RotationDegrees = new(-45, -35, 0), ShadowEnabled = true };
        var fill = new DirectionalLight3D { LightEnergy = .7f, LightColor = new("b8d6ff"), RotationDegrees = new(-15, 120, 0) };
        var rim = new DirectionalLight3D { LightEnergy = 1.3f, LightColor = new("ffe0aa"), RotationDegrees = new(-40, -160, 0) };
        stage.AddChild(key); stage.AddChild(fill); stage.AddChild(rim);
        var camera = new Camera3D { Projection = Camera3D.ProjectionType.Orthogonal, Size = 5.1f, Current = true, Position = new(5, 4, 6) };
        stage.AddChild(camera); camera.LookAt(Vector3.Zero);
        var count = 0;
        foreach (var entry in json.RootElement.GetProperty("modules").EnumerateArray().Where(x => x.TryGetProperty("model", out _)))
        {
            var source = Path.Combine(modelRoot, entry.GetProperty("model").GetString()!);
            var id = entry.GetProperty("id").GetString()!;
            var hash = Convert.ToHexString(SHA256.HashData(System.IO.File.ReadAllBytes(source))).ToLowerInvariant();
            if (hash != entry.GetProperty("modelSha256").GetString()) throw new InvalidDataException("Model changed: " + id);
            using var document = new GltfDocument(); using var state = new GltfState();
            if (document.AppendFromFile(source, state) != Error.Ok) throw new InvalidDataException(source);
            var model = document.GenerateScene(state) as Node3D ?? throw new InvalidDataException("No model: " + id);
            var holder = new Node3D(); stage.AddChild(holder); holder.AddChild(model);
            var meshes = Descendants(model).OfType<MeshInstance3D>().Where(x => x.Mesh is not null).ToArray();
            if (meshes.Length == 0) throw new InvalidDataException("Empty model: " + id);
            var box = meshes[0].GlobalTransform * meshes[0].GetAabb();
            foreach (var mesh in meshes.Skip(1)) box = box.Merge(mesh.GlobalTransform * mesh.GetAabb());
            var scale = 3.5f / box.Size.Length();
            holder.Scale = Vector3.One * scale;
            holder.Position = -box.GetCenter() * scale;
            var fitted = holder.GlobalTransform * box;
            var view = camera.GlobalTransform.AffineInverse();
            var corners = Enumerable.Range(0, 8).Select(i => view * fitted.GetEndpoint(i)).ToArray();
            camera.Size = Math.Max(corners.Max(p => p.X) - corners.Min(p => p.X),
                corners.Max(p => p.Y) - corners.Min(p => p.Y)) * 1.22f;
            for (var frame = 0; frame < 5; frame++) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
            using var image = viewport.GetTexture().GetImage();
            Deliver(image, id, output);
            holder.Free();
            GD.Print("MODULE_RENDER " + id); count++;
        }
        viewport.Free();
        GD.Print("MODULE_RENDERS_COMPLETE " + count);
    }
    private static IEnumerable<Node> Descendants(Node root)
    {
        yield return root;
        foreach (var child in root.GetChildren()) foreach (var node in Descendants(child)) yield return node;
    }
}
