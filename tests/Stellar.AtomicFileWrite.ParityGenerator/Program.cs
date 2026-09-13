using System.Globalization;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 2) throw new ArgumentException("Expected source root and fixture path.");
    var sourceRoot = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var sourceFile = "src/Game/Persistence/CampaignStatePersistenceService.cs";
    string SourceHash() => Convert.ToHexString(SHA256.HashData(
        File.ReadAllBytes(Path.Combine(sourceRoot, sourceFile))));
    var sourceBefore = SourceHash();
    var writerType = typeof(Game.Persistence.CampaignStatePersistenceService)
        .Assembly.GetType("Game.Persistence.AtomicCampaignSaveWriter")
        ?? throw new TypeLoadException("AtomicCampaignSaveWriter");
    var writer = Activator.CreateInstance(writerType, nonPublic: true)
        ?? throw new InvalidOperationException("Cannot construct source writer.");
    var write = writerType.GetMethod("WriteAtomically",
        BindingFlags.Public | BindingFlags.Instance)
        ?? throw new MissingMethodException(writerType.FullName, "WriteAtomically");
    void Invoke(string path, string text, bool preserve)
    {
        try { write.Invoke(writer, new object[] { path, text, preserve }); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); }
    }
    byte[]? Read(string path) => File.Exists(path) ? File.ReadAllBytes(path) : null;
    string[] Temps(string path) => Directory.Exists(Path.GetDirectoryName(path))
        ? Directory.GetFiles(Path.GetDirectoryName(path)!, Path.GetFileName(path) + ".*.tmp")
            .Select(path => Path.GetFileName(path)!)
            .OrderBy(x => x, StringComparer.Ordinal).ToArray()
        : Array.Empty<string>();

    var scratch = CreateExclusiveScratchDirectory();
    try
    {
        var rows = new List<object>();
        void Add(string name, string relative, string text, bool preserve,
            string? primaryBefore = null, string? backupBefore = null,
            bool unrelatedTemp = false)
        {
            var path = Path.Combine(scratch, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            if (primaryBefore is not null) File.WriteAllText(path, primaryBefore, new UTF8Encoding(false));
            if (backupBefore is not null) File.WriteAllText(path + ".bak", backupBefore, new UTF8Encoding(false));
            var unrelated = path + ".unrelated.tmp";
            if (unrelatedTemp) File.WriteAllBytes(unrelated, new byte[] { 9, 0, 8 });
            var before = new { Primary = Read(path), Backup = Read(path + ".bak"),
                Unrelated = Read(unrelated), Temps = Temps(path) };
            Invoke(path, text, preserve);
            var after = new { Primary = Read(path), Backup = Read(path + ".bak"),
                Unrelated = Read(unrelated), Temps = Temps(path) };
            rows.Add(new { Name = name, RelativePath = relative, Text = text,
                PreserveExistingBackup = preserve, Before = before, After = after });
        }
        Add("new-nested-unicode-no-bom", "nested/星/save.json",
            "{\"name\":\"Ω雪\",\"zero\":\"\\u0000\"}", false,
            unrelatedTemp: true);
        Add("replace-rotates-primary", "replace/save.json", "new-value", false,
            primaryBefore: "old-value");
        Add("preserve-known-backup-once", "preserve/save.json", "repaired", true,
            primaryBefore: "corrupt", backupBefore: "known-good");
        Add("normal-rotation-resumes", "preserve/save.json", "ordinary", false);
        Add("empty-bytes-through-empty-string", "empty/save.json", "", false);

        var sourceAfter = SourceHash();
        if (sourceBefore != sourceAfter) throw new InvalidOperationException("Source changed during generation.");
        var fixture = new
        {
            Schema = "stellar.atomic-file-write.actual-source.v1",
            SourceFile = sourceFile,
            SourceHashBefore = sourceBefore,
            SourceHashAfter = sourceAfter,
            RowCount = rows.Count,
            Rows = rows,
        };
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        var json = JsonSerializer.Serialize(fixture, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(output, json, new UTF8Encoding(false));
        Console.WriteLine($"rows={rows.Count} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
    }
    finally
    {
        if (Directory.Exists(scratch)) Directory.Delete(scratch, recursive: true);
    }
}
catch (Exception error)
{
    Console.Error.WriteLine($"atomic file write oracle failure: {error.GetType()}: {error.Message}");
    Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
return 0;

static string CreateExclusiveScratchDirectory()
{
    for (var attempt = 0; attempt != 32; ++attempt)
    {
        var candidate = Path.Combine(Path.GetTempPath(),
            $"stellar-atomic-source-086-{Guid.NewGuid():N}");
        if (NativeMethods.CreateDirectory(candidate, IntPtr.Zero)) return candidate;
        var error = Marshal.GetLastWin32Error();
        if (error != 183) throw new IOException(
            $"Cannot create owned scratch directory '{candidate}'.",
            new Win32Exception(error));
    }
    throw new IOException("Cannot reserve a unique owned scratch directory after 32 attempts.");
}

static class NativeMethods
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool CreateDirectory(string path, IntPtr securityAttributes);
}
