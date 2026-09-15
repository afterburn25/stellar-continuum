using System.Globalization;
using System.Text;
using Game.Simulation.AI;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output path.");
using var output = new StreamWriter(args[0], false, new UTF8Encoding(false));
output.NewLine = "\n";
Console.SetOut(output);

static string F(double x) => double.IsNaN(x) ? "NaN" : double.IsPositiveInfinity(x) ? "Inf" : double.IsNegativeInfinity(x) ? "-Inf" : x.ToString("R", CultureInfo.InvariantCulture);
static string H(string x) => Convert.ToHexString(Encoding.UTF8.GetBytes(x));
static string Priorities(IEnumerable<StrategicPriority> xs) => string.Join(',', xs.Select(x => $"{(int)x.Type}:{F(x.Score)}:{H(x.Reason)}"));
static string Weights(CivilizationStrategicIntent i, IEnumerable<int> types) => string.Join(',', types.Select(t => $"{t}:{F(i.GetWeight((StrategicPriorityType)t))}"));

var builder = new CivilizationStrategicIntentBuilder();
var weightQueries = Enumerable.Range(0, 8).Append(99).ToArray();
var intentCases = 0;
void Intent(string name, CivilizationStrategicPlan plan)
{
    var before = Priorities(plan.Priorities);
    var i = builder.Build(plan);
    Console.WriteLine(string.Join('\t', new[] { "I", name, plan.CivilizationId.ToString(), plan.GeneratedAtTick.ToString(), plan.ReviewAfterTick.ToString(), before,
        Weights(i, weightQueries), i.PreferredNewFleetRole is null ? "-1" : ((int)i.PreferredNewFleetRole.Value).ToString(), i.DeferNewColonization ? "1" : "0", H(i.Summary), before == Priorities(plan.Priorities) ? "1" : "0" }));
    intentCases++;
}

var scores = new[] { -1d, -0d, 0d, .125, .375, .44, .45, .549999999999, .55, .599999999999, .6, .75, .750000000001, .899999999999, 1d, 1.15, 1.999999999999, 2d, 2.1, double.PositiveInfinity, double.NegativeInfinity, double.NaN };
var serial = 0;
foreach (var score in scores) { serial++; Intent($"single-{serial}", new(10 + serial, 100 + serial, 200 + serial, new[] { new StrategicPriority(StrategicPriorityType.Explore, score, "single") })); }
foreach (var score in scores) { serial++; Intent($"defend-{serial}", new(10 + serial, 100 + serial, 200 + serial, new[] { new StrategicPriority(StrategicPriorityType.Defend, score, "defend"), new StrategicPriority(StrategicPriorityType.Colonize, .55, "colonize") })); }
Intent("empty", new(1, long.MinValue, long.MaxValue, Array.Empty<StrategicPriority>()));
Intent("duplicate-last-unknown", new(2, 3, 4, new[] { new StrategicPriority(StrategicPriorityType.Explore, .9, "first"), new StrategicPriority(StrategicPriorityType.Explore, .2, "last"), new StrategicPriority((StrategicPriorityType)99, 1.5, "unknown") }));
Intent("supply-boundary", new(3, 4, 5, new[] { new StrategicPriority(StrategicPriorityType.StabilizeSupply, .75, "s"), new StrategicPriority(StrategicPriorityType.Colonize, .75, "c") }));
Intent("supply-above", new(4, 5, 6, new[] { new StrategicPriority(StrategicPriorityType.StabilizeSupply, .750000000001, "s"), new StrategicPriority(StrategicPriorityType.Colonize, .75, "c") }));
Intent("defense-boundary", new(5, 6, 7, new[] { new StrategicPriority(StrategicPriorityType.Defend, .9, "d"), new StrategicPriority(StrategicPriorityType.Colonize, .55, "c") }));
Intent("nan-branch", new(6, 7, 8, new[] { new StrategicPriority(StrategicPriorityType.Defend, double.NaN, "nan"), new StrategicPriority(StrategicPriorityType.Explore, .45, "x") }));
foreach (var value in new[] { double.NegativeInfinity, -.0, .0, .125, .375, double.PositiveInfinity, double.NaN }) Console.WriteLine($"F\t{F(value)}\t{H($"{value:0.00}")}");

var industry = new StrategicIndustryPriorityProvider();
var shipbuilding = new StrategicShipbuildingPreferenceProvider();
var observedIds = new[] { 42, 43, 77, -9 };
var providerOperations = 0;
string State() => $"{industry.PublishedIntentCount}\t{shipbuilding.PublishedPreferenceCount}\t{string.Join(',', observedIds.Select(id => { var w = industry.GetWeights(id); var p = shipbuilding.GetPreference(id); return $"{id}:{F(w.ConstructionWeight)}:{F(w.ShipbuildingWeight)}:{(p.PreferredNewFleetRole is null ? -1 : (int)p.PreferredNewFleetRole.Value)}:{(p.DeferNewColonization ? 1 : 0)}"; }))}";
void Provider(string tag, string op, CivilizationStrategicIntent? intent = null, CivilizationStrategicReview? review = null, int id = 0)
{
    var effectiveIntent = intent ?? review?.Intent;
    var before = effectiveIntent is null ? "" : $"{effectiveIntent.CivilizationId}|{Weights(effectiveIntent, weightQueries)}|{effectiveIntent.PreferredNewFleetRole}|{effectiveIntent.DeferNewColonization}|{effectiveIntent.Summary}";
    if (op == "GET") { _ = industry.GetWeights(id); _ = shipbuilding.GetPreference(id); }
    else if (op == "PUBLISH_INTENT") { industry.Publish(intent!); shipbuilding.Publish(intent!); }
    else if (op == "PUBLISH_REVIEW") { industry.Publish(review!); shipbuilding.Publish(review!); }
    else if (op == "REMOVE") { industry.Remove(id); shipbuilding.Remove(id); }
    else if (op == "CLEAR") { industry.Clear(); shipbuilding.Clear(); }
    else throw new InvalidOperationException(op);
    var after = effectiveIntent is null ? "" : $"{effectiveIntent.CivilizationId}|{Weights(effectiveIntent, weightQueries)}|{effectiveIntent.PreferredNewFleetRole}|{effectiveIntent.DeferNewColonization}|{effectiveIntent.Summary}";
    var payload = effectiveIntent is null ? "-" : string.Join(';', new[] { effectiveIntent.CivilizationId.ToString(), effectiveIntent.GeneratedAtTick.ToString(), effectiveIntent.ReviewAfterTick.ToString(), Weights(effectiveIntent, weightQueries), effectiveIntent.PreferredNewFleetRole is null ? "-1" : ((int)effectiveIntent.PreferredNewFleetRole.Value).ToString(), effectiveIntent.DeferNewColonization ? "1" : "0", H(effectiveIntent.Summary) });
    Console.WriteLine($"P\t{tag}\t{op}\t{id}\t{payload}\t{review?.Plan.CivilizationId.ToString() ?? "-"}\t{(before == after ? 1 : 0)}\t{State()}");
    providerOperations++;
}
CivilizationStrategicIntent Raw(int id, long generated, long review, (int Type, double Weight)[] weights, FleetRole? role, bool defer, string summary) => new(id, generated, review, weights.ToDictionary(x => (StrategicPriorityType)x.Type, x => x.Weight), role, defer, summary);
var rawA = Raw(42, 11, 12, new[] { (0, 4d), (1, -4d), (2, .5), (3, .125), (4, .375), (5, 10d), (6, -2d), (99, 7d) }, FleetRole.Military, true, "raw A");
var rawB = Raw(42, 13, 14, new[] { (0, -10d), (1, -10d), (2, -10d), (3, -10d), (4, -10d), (5, -10d), (6, -10d) }, FleetRole.Colony, false, "raw B");
var rawC = Raw(43, long.MinValue, long.MaxValue, new[] { (3, .5), (4, .55) }, FleetRole.Scout, false, "raw C");
var rawNaN = Raw(77, 21, 22, new[] { (0, double.NaN), (5, double.PositiveInfinity) }, null, true, "raw NaN");
Provider("default-get", "GET", id: 42);
Provider("publish-raw-unclamped", "PUBLISH_INTENT", rawA);
Provider("overwrite", "PUBLISH_INTENT", rawB);
Provider("second-id", "PUBLISH_INTENT", rawC);
var review = new CivilizationStrategicReview(new(0, 0, 0, 0, false, false, false, false, false), new(999, 31, 32, Array.Empty<StrategicPriority>()), rawC);
Provider("review-intent-id-differs", "PUBLISH_REVIEW", review: review);
Provider("raw-nan-infinity", "PUBLISH_INTENT", rawNaN);
Provider("remove-missing", "REMOVE", id: -9);
Provider("remove-existing", "REMOVE", id: 42);
Provider("clear-nonempty", "CLEAR");
Provider("clear-empty", "CLEAR");

foreach (var label in new[] { "build", "ip-intent", "ip-review", "sp-intent", "sp-review" }) try {
    if (label == "build") builder.Build(null!); else if (label == "ip-intent") industry.Publish((CivilizationStrategicIntent)null!); else if (label == "ip-review") industry.Publish((CivilizationStrategicReview)null!); else if (label == "sp-intent") shipbuilding.Publish((CivilizationStrategicIntent)null!); else shipbuilding.Publish((CivilizationStrategicReview)null!);
} catch (Exception e) { Console.WriteLine($"E\t{label}\t{e.GetType().Name}\t{H(e.Message)}\tsource-only-null"); }
Console.Error.WriteLine($"generated {intentCases} intent cases, {providerOperations} provider operations, 7 formatter cases, 5 source-only null records");
