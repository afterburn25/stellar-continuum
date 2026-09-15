using System.Globalization;
using System.Numerics;
using Game.Simulation.Models;
try
{
    if (args.Length != 1) throw new ArgumentException("Provide golden CSV output path.");
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    using var output = new StreamWriter(args[0]);
    output.WriteLine("ax,ay,az,bx,by,bz,distance,squared");
    static StarSystemState Point(float x, float y, double? z) => new(0, "Parity reference", new Vector2(x,y), StarArchetype.Standard, false,false,false,false, GalacticDepthLightYears:z);
    void Write(StarSystemState a, StarSystemState b) => output.WriteLine($"{a.Position.X:R},{a.Position.Y:R},{a.GalacticDepthLightYears?.ToString("R")??"null"},{b.Position.X:R},{b.Position.Y:R},{b.GalacticDepthLightYears?.ToString("R")??"null"},{InterstellarDistance.Between(a,b):R},{InterstellarDistance.SquaredBetween(a,b):R}");
    Write(Point(0,0,null),Point(3,4,null)); Write(Point(0,0,0),Point(3,4,12));
    Write(Point(1,2,null),Point(1,2,0)); Write(Point(-120000,45000,7),Point(120000,-45000,null));
    Write(Point(123.456f,789.123f,null),Point(-.001f,456.789f,null));
    var random = new Random(8374837);
    for(int i=0;i<500;i++)
        Write(Point((float)(random.NextDouble()-.5)*100000,(float)(random.NextDouble()-.5)*100000,i%3==0?null:random.NextDouble()*1000),
              Point((float)(random.NextDouble()-.5)*100000,(float)(random.NextDouble()-.5)*100000,i%2==0?null:random.NextDouble()*1000));
    Console.WriteLine("Exported 505 cases from preserved InterstellarDistance implementation."); return 0;
}
catch(Exception error) { Console.Error.WriteLine(error); Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}"); return 1; }
