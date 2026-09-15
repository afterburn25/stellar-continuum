using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Simulation;

record Row(string Name, string Operation, JsonNode Input, JsonNode Before, JsonNode After,
    JsonNode? Result, string? ErrorType, string? ErrorMessage);

static class Program
{
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("Usage: StrategicClockOracle <fixture> <source-root>"); return 1; }
        try
        {
            var rows = Build();
            var sourceFiles = new[] { "Simulation/SimulationClock.cs", "Campaign/CampaignAutosaveScheduler.cs", "Campaign/PlayableDemoScenario.cs" };
            var document = new { SchemaVersion = 1, RowCount = rows.Count,
                SourceFiles = sourceFiles.Select(path => new { Path = path, Sha256 = Hash(File.ReadAllBytes(Path.Combine(args[1], path))) }), Rows = rows };
            File.WriteAllText(args[0], JsonSerializer.Serialize(document, Json) + Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"Strategic clock oracle: {rows.Count}/{rows.Count} rows written");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
    static List<Row> Build()
    {
        var rows = new List<Row>(); var clock = new SimulationClock(); var autosave = new CampaignAutosaveScheduler();
        Clock(rows, "restore-clamps-negative", clock, "Restore", new { Days = -2.0 }, () => { clock.Restore(-2); return null; });
        Clock(rows, "pause-zero-advance", clock, "SetSpeed", new { Speed = 0 }, () => { clock.SetSpeed(SimulationClock.SpeedLevel.Paused); return null; });
        Clock(rows, "paused-advance", clock, "Advance", new { Seconds = 10.0, MaximumStepDays = .25 }, () => clock.Advance(10));
        Clock(rows, "maximum-legacy-backlog", clock, "SetSpeed", new { Speed = 4 }, () => { clock.SetSpeed(SimulationClock.SpeedLevel.Maximum); return null; });
        Clock(rows, "maximum-legacy-advance", clock, "Advance", new { Seconds = 1.0, MaximumStepDays = .25 }, () => clock.Advance(1));
        Clock(rows, "legacy-drains-backlog", clock, "Advance", new { Seconds = .1, MaximumStepDays = .25 }, () => clock.Advance(.1));
        Clock(rows, "pause-remembers-maximum", clock, "SetSpeed", new { Speed = 0 }, () => { clock.SetSpeed(SimulationClock.SpeedLevel.Paused); return null; });
        Clock(rows, "resume-maximum", clock, "Resume", new { }, () => { clock.Resume(); return null; });
        Clock(rows, "select-resume-fast", clock, "SelectResumeSpeed", new { Speed = 2 }, () => { clock.SelectResumeSpeed(SimulationClock.SpeedLevel.Fast); return null; });
        Clock(rows, "pause-then-resume-fast", clock, "SetSpeed", new { Speed = 0 }, () => { clock.SetSpeed(SimulationClock.SpeedLevel.Paused); return null; });
        Clock(rows, "resume-selected-fast", clock, "Resume", new { }, () => { clock.Resume(); return null; });
        Clock(rows, "bounded-zero-frame", clock, "AdvanceBoundedFrame", new { Seconds = 0.0, MaximumDays = 1.0, MaximumBacklogDays = 2.0 }, () => clock.AdvanceBoundedFrame(0));
        Clock(rows, "bounded-fast-budget", clock, "AdvanceBoundedFrame", new { Seconds = 5.0, MaximumDays = .5, MaximumBacklogDays = .75 }, () => clock.AdvanceBoundedFrame(5, .5, .75));
        Clock(rows, "bounded-demo-large-jump", clock, "SetSpeed", new { Speed = 5 }, () => { clock.SetSpeed(SimulationClock.SpeedLevel.Demo); return null; });
        Clock(rows, "bounded-demo-clamped", clock, "AdvanceBoundedFrame", new { Seconds = 1e300, MaximumDays = .5, MaximumBacklogDays = .75 }, () => clock.AdvanceBoundedFrame(1e300, .5, .75));
        Clock(rows, "developer-frame-four-substeps", clock, "AdvanceDeveloperFrame", new { Seconds = 1.0 }, () => PlayableDemoScenario.AdvanceFrame(clock, 1.0));
        Clock(rows, "bounded-invalid-budget", clock, "AdvanceBoundedFrameInvalid", new { Seconds = 1.0, MaximumDays = 0.0, MaximumBacklogDays = 1.0 }, () => clock.AdvanceBoundedFrame(1, 0, 1));
        Clock(rows, "select-paused-invalid", clock, "SelectResumeSpeed", new { Speed = 0 }, () => { clock.SelectResumeSpeed(SimulationClock.SpeedLevel.Paused); return null; });

        Autosave(rows, "autosave-reset", autosave, "Reset", new { Days = 0.0 }, () => { autosave.Reset(0); return null; });
        Autosave(rows, "autosave-before-due", autosave, "IsDue", new { Days = 29.999 }, () => autosave.IsDue(29.999));
        Autosave(rows, "autosave-at-due", autosave, "IsDue", new { Days = 30.0 }, () => autosave.IsDue(30));
        Autosave(rows, "autosave-success", autosave, "MarkSuccess", new { Days = 30.0 }, () => { autosave.MarkSuccess(30); return null; });
        Autosave(rows, "autosave-large-jump-one-due", autosave, "IsDue", new { Days = 95.0 }, () => autosave.IsDue(95));
        Autosave(rows, "autosave-large-jump-success", autosave, "MarkSuccess", new { Days = 95.0 }, () => { autosave.MarkSuccess(95); return null; });
        Autosave(rows, "autosave-failure-retry", autosave, "MarkFailure", new { Days = 95.0 }, () => { autosave.MarkFailure(95); return null; });
        Autosave(rows, "autosave-retry-before-due", autosave, "IsDue", new { Days = 95.999 }, () => autosave.IsDue(95.999));
        Autosave(rows, "autosave-retry-due", autosave, "IsDue", new { Days = 96.0 }, () => autosave.IsDue(96));
        Autosave(rows, "autosave-overflow-saturates", autosave, "MarkSuccess", new { Days = double.MaxValue }, () => { autosave.MarkSuccess(double.MaxValue); return null; });
        Autosave(rows, "autosave-invalid-days", autosave, "ResetInvalid", new { Days = -1.0 }, () => { autosave.Reset(-1); return null; });
        var developerAutosave = PlayableDemoScenario.CreateAutosaveScheduler();
        Autosave(rows, "developer-autosave-reset", developerAutosave, "DeveloperReset", new { Days = 0.0 }, () => { developerAutosave.Reset(0); return null; });
        Autosave(rows, "developer-autosave-failure", developerAutosave, "DeveloperMarkFailure", new { Days = 720.0 }, () => { developerAutosave.MarkFailure(720); return null; });
        Policy(rows, "policy-invalid-interval", new { IntervalDays = 0.0, FailureRetryDays = 1.0 }, () => new CampaignAutosaveScheduler(new CampaignAutosavePolicy(0, 1)));
        Policy(rows, "policy-invalid-retry", new { IntervalDays = 30.0, FailureRetryDays = 0.0 }, () => new CampaignAutosaveScheduler(new CampaignAutosavePolicy(30, 0)));
        Clock(rows, "legacy-negative-zero-step-budget", clock, "AdvanceNegativeZeroBudget", new { Seconds = 0.0, MaximumStepDays = "-0" }, () => clock.Advance(0.0, -0.0));
        Clock(rows, "legacy-nan-step-budget", clock, "AdvanceNaNBudget", new { Seconds = 0.0, MaximumStepDays = "NaN" }, () => clock.Advance(0.0, double.NaN));
        Clock(rows, "restore-negative-zero", clock, "Restore", new { Days = -0.0 }, () => { clock.Restore(-0.0); return null; });
        Clock(rows, "restore-nan", clock, "RestoreNaN", new { Days = "NaN" }, () => { clock.Restore(double.NaN); return null; });
        Clock(rows, "legacy-nan-propagates", clock, "AdvanceNaN", new { Seconds = "NaN", MaximumStepDays = .25 }, () => clock.Advance(double.NaN));
        PoisonedDeveloperFrame(rows);
        InvalidSpeedLookup(rows);
        return rows;
    }
    static void Clock(List<Row> rows, string name, SimulationClock c, string operation, object input, Func<object?> action) => Call(rows, name, operation, input, () => ClockState(c), action, () => ClockState(c));
    static void Autosave(List<Row> rows, string name, CampaignAutosaveScheduler s, string operation, object input, Func<object?> action) => Call(rows, name, operation, input, () => AutosaveState(s), action, () => AutosaveState(s));
    static void Policy(List<Row> rows, string name, object input, Func<object> action) => Call(rows, name, "Policy", input, () => new { }, action, () => new { });
    static void Call(List<Row> rows, string name, string operation, object input, Func<object> before, Func<object?> action, Func<object> after)
    {
        JsonNode? result = null; string? type = null, message = null; var prior = Node(before());
        try { var value = action(); if (value is double scalar) result = Scalar(scalar); else if (value is not null) result = Node(value); }
        catch (Exception e) { type = e.GetType().Name; message = e.Message; }
        rows.Add(new Row(name, operation, Node(input), prior, Node(after()), result, type, message));
    }
    static JsonNode ClockState(SimulationClock c) => new JsonObject { ["Speed"] = (int)c.Speed, ["ResumeSpeed"] = (int)c.ResumeSpeed, ["SimulationDays"] = Scalar(c.SimulationDays), ["EffectiveMultiplier"] = Scalar(c.EffectiveMultiplier), ["RequestedMultiplier"] = Scalar(c.RequestedMultiplier), ["BacklogDays"] = Scalar(c.BacklogDays) };
    static JsonNode AutosaveState(CampaignAutosaveScheduler s) => new JsonObject { ["NextDueDay"] = Scalar(s.NextDueDay) };
    static JsonNode Scalar(double value) => double.IsNaN(value) ? JsonValue.Create("NaN")! : double.IsPositiveInfinity(value) ? JsonValue.Create("Infinity")! : double.IsNegativeInfinity(value) ? JsonValue.Create("-Infinity")! : value == 0 && BitConverter.DoubleToInt64Bits(value) < 0 ? JsonValue.Create("-0")! : JsonValue.Create(value)!;
    static void InvalidSpeedLookup(List<Row> rows)
    {
        var clock = new SimulationClock(); clock.SetSpeed((SimulationClock.SpeedLevel)99); string? type = null, message = null;
        try { _ = clock.RequestedMultiplier; } catch (Exception error) { type = error.GetType().Name; message = error.Message; }
        rows.Add(new Row("invalid-speed-fails-at-multiplier-lookup", "InvalidSpeedLookup", new JsonObject { ["Speed"] = 99 }, new JsonObject { ["Speed"] = 99, ["ResumeSpeed"] = 99 }, new JsonObject { ["Speed"] = 99, ["ResumeSpeed"] = 99 }, null, type, message));
    }
    static void PoisonedDeveloperFrame(List<Row> rows)
    {
        var clock = new SimulationClock(); clock.SetSpeed(SimulationClock.SpeedLevel.Demo); clock.Advance(0.0, double.NaN);
        Call(rows, "developer-frame-poisoned-nan-backlog", "AdvanceDeveloperFramePoisoned", new { Seconds = 1.0 }, () => ClockState(clock), () => PlayableDemoScenario.AdvanceFrame(clock, 1.0), () => ClockState(clock));
    }
    static JsonNode Node(object value) => JsonSerializer.SerializeToNode(value)!;
    static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes));
}
