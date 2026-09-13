using System.Reflection;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

if (args.Length != 1)
    throw new ArgumentException("Expected output fixture path.");

var options = new JsonSerializerOptions
{
    WriteIndented = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

JsonElement Clone(object? value) => JsonSerializer.SerializeToElement(value, options);

object Snapshot(ShipyardState state, List<ShipBuildOrderState> queue) => new
{
    state.CivilizationId,
    state.NextOrderSequence,
    state.ActiveDesignId,
    state.ActiveOrderId,
    state.ActiveBuildProgress,
    state.ActiveAuthorizationCredits,
    state.ReservedPopulationMillions,
    state.ReservedPopulationSpeciesId,
    state.ReservedPopulationSourceColonyId,
    QueuedBuilds = queue,
};

object SnapshotWithPending(ShipyardState state, List<ShipBuildOrderState> queue) => new
{
    state.CivilizationId,
    state.NextOrderSequence,
    state.ActiveDesignId,
    state.ActiveOrderId,
    state.ActiveBuildProgress,
    state.ActiveAuthorizationCredits,
    state.ReservedPopulationMillions,
    state.ReservedPopulationSpeciesId,
    state.ReservedPopulationSourceColonyId,
    QueuedBuilds = queue,
    state.PendingBuildCount,
};

void Add(string name, string kind, object arguments, Func<object?> operation)
{
    var result = Clone(null);
    object? error = null;
    try { result = Clone(operation()); }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
    }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Result = result, Error = error });
}

List<ShipBuildOrderState> PrivateQueue(ShipyardState state) =>
    (List<ShipBuildOrderState>)typeof(ShipyardState)
        .GetField("_queuedBuilds", BindingFlags.Instance | BindingFlags.NonPublic)!
        .GetValue(state)!;

void Guard(string name, ShipyardState state, Action<List<ShipBuildOrderState>> inject)
{
    var queue = PrivateQueue(state);
    inject(queue);
    var before = Clone(Snapshot(state, queue));
    var result = Clone(null);
    object? error = null;
    try { result = Clone(state.QueuedBuilds.Count); }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
    }
    cases.Add(new
    {
        Name = name,
        Kind = "Guard",
        Arguments = before,
        Before = before,
        Result = result,
        Error = error,
        After = Clone(Snapshot(state, queue)),
    });
}

void StateCase(string name, ShipyardState state, Action<List<ShipBuildOrderState>> inject)
{
    var queue = PrivateQueue(state);
    inject(queue);
    var snapshot = Clone(Snapshot(state, queue));
    cases.Add(new
    {
        Name = name,
        Kind = "State",
        Arguments = snapshot,
        Before = snapshot,
        Result = Clone(SnapshotWithPending(state, queue)),
        Error = (object?)null,
        After = Clone(Snapshot(state, queue)),
    });
}

foreach (var (civilizationId, sequence) in new[]
{
    (1, 1L), (-2, 0L), (int.MaxValue, long.MaxValue), (int.MinValue, long.MinValue),
})
{
    Add($"format-{civilizationId}-{sequence}", "Format",
        new { CivilizationId = civilizationId, Sequence = sequence },
        () => ShipyardState.FormatOrderId(civilizationId, sequence));
}

var parseInputs = new (string? OrderId, int CivilizationId)[]
{
    (null, 1), ("", 1), ("shipyard-1-1", 1), ("wrong-1-1", 1),
    ("shipyard--1-1", -1), ("shipyard-1-9223372036854775807", 1),
    ("shipyard-1-9223372036854775808", 1), ("shipyard-1--9223372036854775808", 1),
        ("shipyard-1-0", 1), ("shipyard-1--1", 1), ("shipyard-1-+1", 1),
        ("shipyard-1-+9223372036854775807", 1), ("shipyard-1-+9223372036854775808", 1),
        ("shipyard-1-+-1", 1), ("shipyard-1-+-9223372036854775808", 1),
        ("shipyard-1-++1", 1), ("shipyard-1--+1", 1),
    ("shipyard-1-0001", 1), ("shipyard-1- 1", 1), ("shipyard-1-1 ", 1),
    ("shipyard-1-\t+1\r\n", 1), ("shipyard-1-1x", 1), ("shipyard-1-1 2", 1),
    ("shipyard-1-1\0", 1), ("shipyard-1-1\0\0", 1), ("shipyard-1-1 \0", 1),
    ("shipyard-1-1\0 ", 1), ("shipyard-1-\01", 1),
    ("shipyard-1-\u00a01", 1), ("shipyard-2-1", 1),
};
foreach (var (orderId, civilizationId) in parseInputs)
{
    Add($"parse-{cases.Count}", "Parse", new { OrderId = orderId, CivilizationId = civilizationId }, () =>
    {
        var ok = ShipyardState.TryReadCanonicalSequence(orderId, civilizationId, out var sequence);
        return new { Ok = ok, Sequence = sequence };
    });
}

foreach (var orderId in new string?[]
{
    null, "", " ", "\u00a0", "abc", "a_b-9", "é", new('a', 128), new('a', 129), "A-Z_09-",
})
{
    Add($"valid-id-{cases.Count}", "ValidId", new { OrderId = orderId },
        () => ShipyardState.IsValidPersistedOrderId(orderId));
}

StateCase("state-default", new ShipyardState { CivilizationId = 1 }, _ => { });
StateCase("state-full-nondefault", new ShipyardState
{
    CivilizationId = -7,
    NextOrderSequence = long.MaxValue,
    ActiveDesignId = "bulk_freighter",
    ActiveOrderId = "custom_active",
    ActiveBuildProgress = -3.25,
    ActiveAuthorizationCredits = 44.5,
    ReservedPopulationMillions = -2.75,
    ReservedPopulationSpeciesId = "species-active",
    ReservedPopulationSourceColonyId = -9,
}, queue =>
{
    queue.Add(new ShipBuildOrderState
    {
        OrderId = "queued_one",
        DesignId = "warp_scout",
        AuthorizationCredits = 12.5,
        ReservedPopulationMillions = 0.125,
        ReservedPopulationSpeciesId = "species-queue",
        ReservedPopulationSourceColonyId = 17,
    });
    queue.Add(new ShipBuildOrderState
    {
        OrderId = "",
        DesignId = "science_vessel",
        AuthorizationCredits = -4,
        ReservedPopulationMillions = -8,
    });
});

Guard("active-unknown-formatted-positive", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "unknown", ReservedPopulationMillions = 1.23456,
}, _ => { });
Guard("active-whitespace-positive", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = " ", ReservedPopulationMillions = 2,
}, _ => { });
Guard("active-nbsp-positive", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "\u00a0", ReservedPopulationMillions = 2,
}, _ => { });
Guard("active-unknown-zero", new ShipyardState { CivilizationId = 2, ActiveDesignId = "unknown" }, _ => { });
Guard("active-unknown-negative", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "unknown", ReservedPopulationMillions = -2,
}, _ => { });
Guard("active-live-catalog-design", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "bulk_freighter", ReservedPopulationMillions = 2,
}, _ => { });
foreach (var value in new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity })
    Guard($"active-nonfinite-{value}", new ShipyardState
    {
        CivilizationId = 2, ReservedPopulationMillions = value,
    }, _ => { });

foreach (var value in new[] { 2d, 1.23456, 0d, -2d, double.NaN, double.PositiveInfinity, double.NegativeInfinity })
    Guard($"queue-unknown-{value}", new ShipyardState { CivilizationId = 2 }, queue =>
        queue.Add(new ShipBuildOrderState { DesignId = "unknown", ReservedPopulationMillions = value }));
Guard("queue-whitespace-positive", new ShipyardState { CivilizationId = 2 }, queue =>
    queue.Add(new ShipBuildOrderState { DesignId = "\u2003", ReservedPopulationMillions = 2 }));

Guard("active-valid-seven-known", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "warp_scout",
}, queue =>
{
    for (var index = 0; index < 7; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
});
Guard("active-valid-eighth-positive", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "warp_scout",
}, queue =>
{
    for (var index = 0; index < 8; ++index)
        queue.Add(new ShipBuildOrderState
        {
            DesignId = "warp_scout", ReservedPopulationMillions = index == 7 ? 1 : 0,
        });
});
Guard("active-valid-eighth-zero", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "warp_scout",
}, queue =>
{
    for (var index = 0; index < 8; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
});
Guard("unknown-zero-does-not-consume-capacity", new ShipyardState
{
    CivilizationId = 2, ActiveDesignId = "warp_scout",
}, queue =>
{
    queue.Add(new ShipBuildOrderState { DesignId = "unknown" });
    for (var index = 0; index < 7; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
});
Guard("index-eight-positive", new ShipyardState { CivilizationId = 2 }, queue =>
{
    for (var index = 0; index < 9; ++index)
        queue.Add(new ShipBuildOrderState
        {
            DesignId = "warp_scout", ReservedPopulationMillions = index == 8 ? 1 : 0,
        });
});
Guard("index-eight-zero", new ShipyardState { CivilizationId = 2 }, queue =>
{
    for (var index = 0; index < 9; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
});
Guard("index-eight-unknown-positive-uses-physical-cap", new ShipyardState { CivilizationId = 2 }, queue =>
{
    for (var index = 0; index < 8; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
    queue.Add(new ShipBuildOrderState { DesignId = "unknown", ReservedPopulationMillions = 1 });
});
Guard("index-eight-nonfinite-precedes-physical-cap", new ShipyardState { CivilizationId = 2 }, queue =>
{
    for (var index = 0; index < 8; ++index)
        queue.Add(new ShipBuildOrderState { DesignId = "warp_scout" });
    queue.Add(new ShipBuildOrderState
    {
        DesignId = "unknown", ReservedPopulationMillions = double.NaN,
    });
});

var seedCivilizations = new[]
{
    new CivilizationState(7, "Seven", 2, CivilizationArchetype.Scientific,
        new CivilizationTraits(.1, .2, .3, .4, .5, .6, true), true,
        CivilizationDevelopmentStage.WarpCapable, false, false, true, "species-seven"),
    new CivilizationState(-2, "Negative", 3, CivilizationArchetype.AncientCustodian,
        new CivilizationTraits(.6, .5, .4, .3, .2, .1, false), false,
        CivilizationDevelopmentStage.AncientSpacefaring, true, true, false, "species-negative"),
};
Add("seed-source-order", "Seed", new { Civilizations = seedCivilizations },
    () => new ShipyardSeeder().Seed(seedCivilizations));

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-shipyard-state-oracle-v2",
    Cases = cases,
}, options) + Environment.NewLine);
